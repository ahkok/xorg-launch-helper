/*
 * This file is part of user-session
 *
 * (C) Copyright 2009 Intel Corporation
 * Authors:
 *     Auke Kok <auke@linux.intel.com>
 *     Arjan van de Ven <arjan@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <pthread.h>
#include <string.h>
#include <linux/limits.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <pwd.h>
#include <time.h>

#include "user-session.h"

#include <X11/Xauth.h>

char displaydev[PATH_MAX];	/* "/dev/tty1" */
char displayname[256] = ":0";	/* ":0" */
char xauth_cookie_file[PATH_MAX];
Xauth x_auth;

static pthread_mutex_t notify_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_condition = PTHREAD_COND_INITIALIZER;

int xpid;

static volatile int exiting = 0;

/*
 * We need to know the DISPLAY and TTY values to use, for passing
 * to PAM, ConsoleKit but also X.
 * TIOCLINUX will tell us which console is currently showing
 * for this purpose.
 */
void set_tty(void)
{
	int fd;
	struct vt_stat v;

	d_in();

	/* switch to this console */
	fd = open("/dev/console", O_RDWR);
	if (fd < 0) {
		lprintf("Unable to open /dev/console, using stdin");
		fd = 0;
	}

	if (ioctl(fd, VT_GETSTATE, &v)) {
		lprintf("VT_GETSTATE failed");
		close(fd);
		return;
	}

	if (v.v_active != tty) {
		if (ioctl(fd, VT_ACTIVATE, tty))
			lprintf("VT_ACTIVATE failed");
	}
	close(fd);

	snprintf(displaydev, PATH_MAX, "/dev/tty%d", tty);

	lprintf("Using %s as display device", displaydev);

	d_out();
}

void setup_xauth(void)
{
	FILE *fp;
	int fd;
	static char cookie[16];
	struct utsname uts;

	static char xau_address[80];
	static char xau_number[] = "0"; // FIXME, detect correct displaynum
	static char xau_name[] = "MIT-MAGIC-COOKIE-1";

	d_in();

	fp = fopen("/dev/urandom", "r");
	if (!fp)
		return;
	if (fgets(cookie, sizeof(cookie), fp) == NULL) {
		fclose(fp);
		return;
	}
	fclose(fp);

	/* construct xauth data */
	if (uname(&uts) < 0) {
		lprintf("uname failed");
		return;
	}

	sprintf(xau_address, "%s", uts.nodename);
	x_auth.family = FamilyLocal;
	x_auth.address = xau_address;
	x_auth.number = xau_number;
	x_auth.name = xau_name;
	x_auth.address_length = strlen(xau_address);
	x_auth.number_length = strlen(xau_number);
	x_auth.name_length = strlen(xau_name);
	x_auth.data = (char *) cookie;
	x_auth.data_length = sizeof(cookie);

	snprintf(xauth_cookie_file, PATH_MAX, "/run/user/%s/Xauth-XXXXXX",
		 pass->pw_name);

	fd = mkstemp(xauth_cookie_file);
	if (fd < 0) {
		lprintf("unable to make tmp file for xauth");
		return;
	}

	lprintf("Xauth cookie file: %s", xauth_cookie_file);

	fp = fdopen(fd, "a");
	if (!fp) {
		lprintf("unable to open xauth fp");
		close(fd);
		return;
	}

	/* write it out to disk */
	if (XauWriteAuth(fp, &x_auth) != 1)
		lprintf("unable to write xauth data to disk");

	fclose(fp);

	d_out();
}

static void usr1handler(int foo)
{
	/* Got the signal from the X server that it's ready */
	if (foo++) foo--; /*  shut down warning */

	dprintf("received USR1");
	pthread_mutex_lock(&notify_mutex);
	pthread_cond_signal(&notify_condition);
	pthread_mutex_unlock(&notify_mutex);
}


static void termhandler(int foo)
{
	if (foo++) foo--; /*  shut down warning */

	d_in();

	exiting = 1;
	/*
	 * we received either:
	 * - a TERM from init when switching to init 3
	 * - an INT from a ^C press in the console when running in fg
	 *
	 * This kills ONLY the X server, everything else will be killed
	 * when we exit the waitpid() loop.
	 */
	if (session_pid)
		kill(session_pid, SIGKILL);

	kill(xpid, SIGTERM);
	d_out();
}


/*
 * start the X server
 * Step 1: arm the signal
 * Step 2: fork to get ready for the exec, continue from the main thread
 * Step 3: find the X server
 * Step 4: start the X server
 */
void start_X_server(void)
{
	struct sigaction usr1;
	struct sigaction term;
	char *xserver = NULL;
	int ret;
	char vt[80];
	char xorg_log[PATH_MAX];
	struct stat statbuf;
	char *ptrs[32];
	int count = 0;
	char all[PATH_MAX] = "";
	int i;
	char *opt;
	FILE *fp;
	char fn[PATH_MAX];

	d_in();

	/* Step 1: arm the signal */
	memset(&usr1, 0, sizeof(struct sigaction));
	usr1.sa_handler = usr1handler;
	sigaction(SIGUSR1, &usr1, NULL);

	/* Step 2: fork */
	ret = fork();
	if (ret) {
		xpid = ret;
		lprintf("Started Xorg[%d]", xpid);
		/* setup sighandler for main thread */
		memset(&term, 0, sizeof(struct sigaction));
		term.sa_handler = termhandler;
		sigaction(SIGTERM, &term, NULL);
		sigaction(SIGINT, &term, NULL);
		d_out();
		return; /* we're the main thread */
	}

	/* if we get here we're the child */

	/* Step 3: find the X server */

	/*
	 * set the X server sigchld to SIG_IGN, that's the
         * magic to make X send the parent the signal.
	 */
	signal(SIGUSR1, SIG_IGN);

	if (!xserver) {
		if (!access("/usr/bin/Xorg", X_OK))
			xserver = "/usr/bin/Xorg";
		else if (!access("/usr/bin/X", X_OK))
			xserver = "/usr/bin/X";
		else {
			lprintf("No X server found!");
			_exit(EXIT_FAILURE);
		}
	}

	snprintf(vt, 80, "vt%d", tty);

	/* assemble command line */
	memset(ptrs, 0, sizeof(ptrs));

	ptrs[0] = xserver;

	ptrs[++count] = displayname;

	/* non-suid root Xorg? */
	ret = stat(xserver, &statbuf);
	if (!(!ret && (statbuf.st_mode & S_ISUID))) {
		snprintf(xorg_log, PATH_MAX, "%s/.Xorg.0.log", pass->pw_dir);
		ptrs[++count] = strdup("-logfile");
		ptrs[++count] = xorg_log;
	} else {
		lprintf("WARNING: Xorg is setuid root - bummer.");
	}

	/* dpi */
	if (strcmp(dpinum, "auto")) {
		lprintf("Forcing DPI=%s", dpinum);
		/* hard-coded dpi */
		ptrs[++count] = strdup("-dpi");
		ptrs[++count] = dpinum;
	} /* else dpi==auto */

	ptrs[++count] = strdup("-nolisten");
	ptrs[++count] = strdup("tcp");

	ptrs[++count] = strdup("-noreset");

	ptrs[++count] = strdup("-auth");
	ptrs[++count] = user_xauth_path;

	opt = strtok(addn_xopts, " ");
	while (opt) {
	  dprintf("adding xopt: \"%s\"", opt);
		ptrs[++count] = strdup(opt);
		opt = strtok(NULL, " ");
	}
	ptrs[++count] = vt;

	for (i = 0; i <= count; i++) {
		strncat(all, ptrs[i], PATH_MAX - strlen(all) - 1);
		if (i < count)
			strncat(all, " ", PATH_MAX - strlen(all) - 1);
	}
	lprintf("starting X server with: \"%s\"", all);

	execv(ptrs[0], ptrs);

	d_out();

	exit(EXIT_FAILURE);
}

/*
 * The X server will send us a SIGUSR1 when it's ready to serve clients,
 * wait for this.
 */
void wait_for_X_signal(void)
{
	struct timespec tv;

	d_in();

	clock_gettime(CLOCK_REALTIME, &tv);
	tv.tv_sec += 10;

	pthread_mutex_lock(&notify_mutex);
	pthread_cond_timedwait(&notify_condition, &notify_mutex, &tv);
	pthread_mutex_unlock(&notify_mutex);

	d_out();
}

void wait_for_X_exit(void)
{
	int ret;
	int status;

	d_in();

	while (!exiting) {
		ret = waitpid(-1, &status, 0);

		if (WIFEXITED(status))
			lprintf("process %d exited with exit code %d",
				ret, WEXITSTATUS(status));
		if (WIFSIGNALED(status))
			lprintf("process %d was killed by signal %d",
				ret, WTERMSIG(status));
		if (WIFCONTINUED(status))
			lprintf("process %d continued", ret);

		if (ret == xpid) {
			lprintf("Xorg[%d] exited, cleaning up", ret);
			break;
		}
		if (ret == session_pid) {
			lprintf("Session process [%d] exited, cleaning up",
				ret);
			kill(xpid, SIGTERM);
		}
	}

	d_out();
}

void set_text_mode(void)
{
	int fd;

	d_in();

	fd = open(displaydev, O_RDWR);

	if (fd < 0) {
		lprintf("Unable to open /dev/console, using stdin");
		fd = 0;
	}
	ioctl(fd, KDSETMODE, KD_TEXT);
	if (fd != 0)
		close(fd);

	d_out();
}
