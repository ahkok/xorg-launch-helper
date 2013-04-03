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


static int xpid;

static void termhandler(int foo)
{
	if (foo++) foo--;

	kill(xpid, SIGTERM);
}

int main(int argc, char **argv)
{
	struct sigaction term;
	char *xserver = NULL;
	char *ptrs[32];
	int count = 0;
	pid_t pid;
	char all[PATH_MAX] = "";
	int i;
	sigset_t mask;
	sigset_t oldmask;
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

		xpid = pid;

		/* wait up to 10 seconds for X server to start */
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
				fprintf(stderr, "X server startup timed out (10secs). This "
					"indicates an issue in the server configuration or drivers.\n");
				exit(EXIT_FAILURE);
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
	exit(EXIT_FAILURE);
}
