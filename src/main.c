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

#include <config.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#include <linux/limits.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <systemd/sd-daemon.h>
#ifdef XORG_PAM_APPNAME
#include <pwd.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#endif

static int xpid;

static void termhandler(int foo)
{
	if (foo++) foo--;

	kill(xpid, SIGTERM);
}

static int start_xserver(int argc, char **argv)
{
	int i;
	int count = 0;
	char *ptrs[32];
	char all[PATH_MAX] = "";
	char *xserver = NULL;

	if (!xserver) {
		if (!access("/usr/bin/Xorg", X_OK))
			xserver = "/usr/bin/Xorg";
		else if (!access("/usr/bin/X", X_OK))
			xserver = "/usr/bin/X";
		else {
			fprintf(stderr, "No X server found!");
			return EXIT_FAILURE;
		}
	}

	/* assemble command line */
	memset(ptrs, 0, sizeof(ptrs));

	ptrs[0] = xserver;

	for (i = 1; i < argc; i++)
		ptrs[++count] = strdup(argv[i]);

	for (i = 0; i <= count; i++) {
		strncat(all, ptrs[i], PATH_MAX - strlen(all) - 1);
		if (i < count)
			strncat(all, " ", PATH_MAX - strlen(all) - 1);
	}

	fprintf(stderr, "Starting Xorg server with: \"%s\"\n", all);
	execv(ptrs[0], ptrs);
	fprintf(stderr, "Failed to execv() the X server.\n");
	return EXIT_FAILURE;
}

int main(int argc, char **argv)
{
	struct sigaction term;
	pid_t pid;
	sigset_t mask;
	sigset_t oldmask;
	int retval;
#ifdef XORG_PAM_APPNAME
	struct pam_conv conv = {
		misc_conv,
		NULL
	};
	pam_handle_t *pamh = NULL;
	int myuid;
	int pamval;
	struct passwd *pw;
#endif

	/* Step 1: block sigusr1 until we wait for it */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

	/* Step 2: fork */
	pid = fork();
	if (pid) {
		struct timespec starttime;
		struct timespec timeout;
		int status;
		int retries = 3;

		xpid = pid;

		/* wait up to retries * 10 seconds for X server to start */
		clock_gettime(CLOCK_REALTIME, &starttime);
		timeout.tv_sec = 10;
		timeout.tv_nsec = 0;
		do {
			int ret =  sigtimedwait(&mask, NULL, &timeout);
			if (ret > 0) {
				assert(ret == SIGUSR1);
				/* got SIGUSR1, X server has started */
				sd_notify(0, "READY=1");
				break;
			}
			else if (errno == EINTR) {
				struct timespec currenttime;
				/* interrupted by other signal, update timeout and retry */
				clock_gettime(CLOCK_REALTIME, &currenttime);
				timeout.tv_sec = (starttime.tv_sec + 10) - currenttime.tv_sec;
				if (currenttime.tv_nsec > starttime.tv_nsec) {
					timeout.tv_nsec = starttime.tv_nsec + 1000000000L - currenttime.tv_nsec;
					timeout.tv_sec--;
				}
				else {
					timeout.tv_nsec = currenttime.tv_nsec - starttime.tv_nsec ;
				}
				/* printf("New timeout: %d s, %d ns\n",
					(int)timeout.tv_sec, (int)timeout.tv_nsec); */
				if (timeout.tv_sec < 0 || timeout.tv_nsec < 0) {
					timeout.tv_sec = 0;
					timeout.tv_nsec = 0;
				}
			}
			else if (errno == EAGAIN) {
				retries--;
				fprintf(stderr, "X server startup timed out (10secs). This "
					"indicates an issue in the server configuration or drivers "
					"or slow startup, %s...\n", (retries ? "retrying" : "exiting"));
				if (!retries)
					exit(EXIT_FAILURE);
				/* Make sure the timeout is restarted from scratch. */
				timeout.tv_sec = 10;
				timeout.tv_nsec = 0;
			}
			else {
				perror("sigtimedwait");
				exit(EXIT_FAILURE);
			}
		} while (1);

		/* handle TERM gracefully and pass it on to xpid */
		memset(&term, 0, sizeof(struct sigaction));
		term.sa_handler = termhandler;
		sigaction(SIGTERM, &term, NULL);

		/* sit and wait for Xorg to exit */
		pid = waitpid(xpid, &status, 0);
		if (WIFEXITED(status))
			exit(WEXITSTATUS(status));
		exit(EXIT_FAILURE);
	}

	/* if we get here we're the child */


	/*
	 * reset signal mask and set the X server sigchld to SIG_IGN, that's the
	 * magic to make X send the parent the signal.
	 */
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	signal(SIGUSR1, SIG_IGN);

	/* Step 3: find the X server */
#ifdef XORG_PAM_APPNAME
	/*
	 * Authenticate with PAM using the application name
	 */
	myuid = getuid();
	pw = getpwuid(myuid);
	pamval = pam_start(XORG_PAM_APPNAME, pw->pw_name, &conv, &pamh);
	if (pamval == PAM_SUCCESS) {
		pamval = pam_authenticate(pamh, 0);
		if (pamval == PAM_SUCCESS) {
			pamval = pam_acct_mgmt(pamh, 0);
			if (pamval == PAM_SUCCESS) {
				pamval = pam_open_session(pamh, 0);
				if (pamval == PAM_SUCCESS)
					retval = start_xserver(argc, argv);
			}
		}
	}

	if (pamh)
		pam_end(pamh, pamval);

#else
	retval = start_xserver(argc, argv);
#endif

	return retval;
}
