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
#include <pthread.h>
#include <string.h>
#include <linux/limits.h>
#include <linux/vt.h>
#include <linux/kd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/utsname.h>
#include <systemd/sd-daemon.h>


static pthread_mutex_t notify_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t notify_condition = PTHREAD_COND_INITIALIZER;

static int xpid;

static void usr1handler(int foo)
{
	/* Got the signal from the X server that it's ready */
	if (foo++) foo--;

	sd_notify(0, "READY=1");

	pthread_mutex_lock(&notify_mutex);
	pthread_cond_signal(&notify_condition);
	pthread_mutex_unlock(&notify_mutex);
}

static void termhandler(int foo)
{
	if (foo++) foo--;

	kill(xpid, SIGTERM);
}


int main(int argc, char **argv)
{
	struct sigaction usr1;
	struct sigaction term;
	char *xserver = NULL;
	char *ptrs[32];
	int count = 0;
	pid_t pid;
	char all[PATH_MAX] = "";
	int i;

	/* Step 1: arm the signal */
	memset(&usr1, 0, sizeof(struct sigaction));
	usr1.sa_handler = usr1handler;
	sigaction(SIGUSR1, &usr1, NULL);

	/* Step 2: fork */
	pid = fork();
	if (pid) {
		struct timespec tv;
		int err;
		int status;

		xpid = pid;

		/* setup sighandler for main thread */
		clock_gettime(CLOCK_REALTIME, &tv);
		tv.tv_sec += 10;

		pthread_mutex_lock(&notify_mutex);
		err = pthread_cond_timedwait(&notify_condition, &notify_mutex, &tv);
		pthread_mutex_unlock(&notify_mutex);

		if (err == ETIMEDOUT) {
			fprintf(stderr, "X server startup timed out (10secs). This indicates an"
				"an issue in the server configuration or drivers.\n");
			exit(EXIT_FAILURE);
		}

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
	 * set the X server sigchld to SIG_IGN, that's the
         * magic to make X send the parent the signal.
	 */
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

	fprintf(stderr, "Starting Xorg server with: \"%s\"", all);
	execv(ptrs[0], ptrs);
	fprintf(stderr, "Failed to execv() the X server.\n");

	exit(EXIT_FAILURE);
}
