/*
 * main.c: desktop session starter
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

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
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


static char displayname[256] = ":0";   /* ":0" */
static int tty = 1; /* tty1 */

static pthread_mutex_t notify_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_condition = PTHREAD_COND_INITIALIZER;


static void usr1handler(int foo)
{
	/* Got the signal from the X server that it's ready */
	if (foo++) foo--; /*  shut down warning */

	pthread_mutex_lock(&notify_mutex);
	pthread_cond_signal(&notify_condition);
	pthread_mutex_unlock(&notify_mutex);
}



int main(int argc, char **argv)
{
	struct sigaction usr1;
	char *xserver = NULL;
	int ret;
	char vt[80];
	char xorg_log[PATH_MAX];
	struct stat statbuf;
	char *ptrs[32];
	int count = 0;
	char all[PATH_MAX] = "";
	int i;

	/* Step 1: arm the signal */
	memset(&usr1, 0, sizeof(struct sigaction));
	usr1.sa_handler = usr1handler;
	sigaction(SIGUSR1, &usr1, NULL);

	/* Step 2: fork */
	ret = fork();
	if (ret) {
		struct timespec tv;
		char *xdg;
		char pidfile[PATH_MAX];
		FILE *fp;

		fprintf(stderr, "Started Xorg[%d]\n", ret);

		/* setup sighandler for main thread */
		clock_gettime(CLOCK_REALTIME, &tv);
		tv.tv_sec += 10;

		pthread_mutex_lock(&notify_mutex);
		pthread_cond_timedwait(&notify_condition, &notify_mutex, &tv);
		pthread_mutex_unlock(&notify_mutex);

		xdg = getenv("XDG_RUNTIME_DIR");
		if (!xdg) {
			fprintf(stderr, "Unable to create pidfile: XDG_RUNTIME_DIR is not set.\n");
			exit(EXIT_FAILURE);
		}

		snprintf(pidfile, PATH_MAX, "%s/Xorg.pid", xdg);
		fp = fopen(pidfile, "w");
		if (!fp) {
			fprintf(stderr, "Unable to write pidfile.\n");
			exit(EXIT_FAILURE);
		}
		fprintf(fp, "%d\n", ret);
		fclose(fp);

		//FIXME - return an error code if timer expired instead.
		exit(EXIT_SUCCESS);
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
			fprintf(stderr, "No X server found!");
			exit(EXIT_FAILURE);
		}
	}

	/* assemble command line */
	memset(ptrs, 0, sizeof(ptrs));

	ptrs[0] = xserver;

	ptrs[++count] = displayname;

	/* non-suid root Xorg? */
	ret = stat(xserver, &statbuf);
	if (!(!ret && (statbuf.st_mode & S_ISUID))) {
		snprintf(xorg_log, PATH_MAX, "%s/.Xorg.0.log", getenv("HOME"));
		ptrs[++count] = strdup("-logfile");
		ptrs[++count] = xorg_log;
	} else {
		fprintf(stderr, "WARNING: Xorg is setuid root - bummer.");
	}

	ptrs[++count] = strdup("-nolisten");
	ptrs[++count] = strdup("tcp");

	ptrs[++count] = strdup("-noreset");

	for (i = 1; i < argc; i++)
		ptrs[++count] = strdup(argv[i]);

	snprintf(vt, 80, "vt%d", tty);
	ptrs[++count] = vt;

	for (i = 0; i <= count; i++) {
		strncat(all, ptrs[i], PATH_MAX - strlen(all) - 1);
		if (i < count)
			strncat(all, " ", PATH_MAX - strlen(all) - 1);
	}
	fprintf(stderr, "starting X server with: \"%s\"", all);

	execv(ptrs[0], ptrs);

	exit(EXIT_FAILURE);
}
