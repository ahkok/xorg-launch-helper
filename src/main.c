/*
 * main.c: desktop session starter
 *
 * (C) Copyright 2009 Intel Corporation
 * Authors:
 *     Auke Kok <auke@linux.intel.com>
 *     Arjan van de Ven <arjan@linux.intel.com>
 *     Ben Boeckel <mathstuf@gmail.com>
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
#include <limits.h>
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
	int have_display;
	char disp_path[PATH_MAX];
	char vtname[13]; /* 13 == 2 + 1 + strlen(ULONG_MAX) */

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

	/* Step 4: find an available DISPLAY (if not given) */
	have_display = 0;

	if (1 < argc) {
		/* Check whether the first argument (which has to be the
		 * display). If it doesn't match ":[0-9]+", we assume it is
		 * *not* a valid DISPLAY and generate our own. */
		if (*argv[1] == ':') {
			char* ptr;

			strtoul(argv[1] + 1, &ptr, 10);

			if (!*ptr)
				have_display = 1;
		}
	}

	/* Let's generate our own display string */
	if (!have_display) {
		struct stat buf;
		int sz;

		for (i = 0; i < INT_MAX; ++i) {
			sz = snprintf(disp_path, PATH_MAX, "/tmp/.X%d-lock", i);
			disp_path[sz] = '\0';

			if (lstat(disp_path, &buf)) {
				if (errno == ENOENT)
					break;
			}
		}

		if (i == INT_MAX) {
			fprintf(stderr, "Failed to find an available DISPLAY!");
			exit(EXIT_FAILURE);
		}

		sz = snprintf(disp_path, PATH_MAX, ":%d", i);
		disp_path[sz] = '\0';

		ptrs[++count] = disp_path;
	}

	for (i = 1; i < argc; i++)
		ptrs[++count] = strdup(argv[i]);

	for (i = 0; i <= count; i++) {
		strncat(all, ptrs[i], PATH_MAX - strlen(all) - 1);
		if (i < count)
			strncat(all, " ", PATH_MAX - strlen(all) - 1);
	}

	/* Step 5: Spawn on the current TTY (if possible) */
	if (isatty(STDIN_FILENO)) {
		char* tty;

		tty = ttyname(STDIN_FILENO);

		if (!strncmp("/dev/tty", tty, 8)) {
			unsigned long vtnum;
			char *ptr;

			vtnum = strtoul(tty + 8, &ptr, 10);

			if (!ptr) {
				int sz;

				sz = snprintf(vtname, 13, "vt%lu", vtnum);
				vtname[sz] = '\0';

				ptrs[++count] = vtname;
			}
		}
	}

	fprintf(stderr, "Starting Xorg server with: \"%s\"", all);
	execv(ptrs[0], ptrs);
	fprintf(stderr, "Failed to execv() the X server.\n");

	exit(EXIT_FAILURE);
}
