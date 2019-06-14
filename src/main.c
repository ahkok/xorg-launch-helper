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
#ifdef HAVE_PLYMOUTH
#include <poll.h>
#include <X11/Xlib.h> /* for Display */
#include <X11/Xatom.h> /* for XA_PIXMAP */
#endif
#ifdef XORG_PAM_APPNAME
#include <pwd.h>
#include <security/pam_appl.h>
#include <security/pam_misc.h>
#endif

static pid_t xpid;
#ifdef HAVE_PLYMOUTH
static int pipe_fds[2];
#endif

/* Set this to 0 to deduce ourselves the next available VTn */
#define INITIAL_VT	1

static void termhandler(int signum)
{
	kill(xpid, signum);
}

#ifdef HAVE_PLYMOUTH
static int plymouth_is_running(void)
{
	FILE *fp = popen("/bin/plymouth --ping", "r");
	int	 status;

	if (!fp) {
		fprintf(stderr, "Could not ping plymouth\n");
		return 0;
	}

	status = pclose(fp);

	return WIFEXITED (status) && WEXITSTATUS (status) == 0;
}

static void plymouth_prepare_for_transition(void)
{
	FILE *fp = popen("/bin/plymouth deactivate", "r");

	if (!fp) {
		fprintf(stderr, "Could not deactivate plymouth\n");
		return;
	}

	pclose(fp);
}

static void plymouth_quit_with_transition(void)
{
	FILE *fp = popen("/bin/plymouth quit --retain-splash", "r");

	if (!fp) {
		fprintf(stderr, "Could not deactivate plymouth\n");
		return;
	}

	pclose(fp);
}

static void slave_save_root_window_of_screen(Display *display, Atom id_atom, int screen_number)
{
	Window root_window;
	GC gc;
	XGCValues values;
	Pixmap pixmap;
	int width, height, depth;
	root_window = RootWindow(display, screen_number);
	width = DisplayWidth(display, screen_number);
	height = DisplayHeight(display, screen_number);
	depth = DefaultDepth(display, screen_number);
	pixmap = XCreatePixmap(display, root_window, width, height, depth);
	values.function = GXcopy;
	values.plane_mask = AllPlanes;
	values.fill_style = FillSolid;
	values.subwindow_mode = IncludeInferiors;
	gc = XCreateGC(display, root_window, GCFunction | GCPlaneMask | GCFillStyle | GCSubwindowMode, &values);
	if (XCopyArea(display, root_window, pixmap, gc, 0, 0, width, height, 0, 0)) {
		long pixmap_as_long;
		pixmap_as_long = (long)pixmap;
		XChangeProperty(display, root_window, id_atom, XA_PIXMAP, 32, PropModeReplace, (unsigned char *) &pixmap_as_long, 1);
	}
	XFreeGC(display, gc);
}

void slave_save_root_windows(Display *display)
{
	int i, number_of_screens;
	Atom atom;
	number_of_screens = ScreenCount(display);
	atom = XInternAtom(display, "_XROOTPMAP_ID", False);

	if (atom == 0)
		return;

	for (i = 0; i < number_of_screens; i++)
		slave_save_root_window_of_screen(display, atom, i);

	XSync(display, False);
}
#endif

#ifdef XORG_PAM_APPNAME
static void xdg_vtnr_current_vt(pam_handle_t *pamh, int vtnr)
#else
static void xdg_vtnr_current_vt(int vtnr)
#endif
{
	char vt_string[256];

	if (!vtnr) {
		int fd;
		struct vt_stat vt_state = { 0 };

		fd = open ("/dev/tty0", O_RDWR | O_NOCTTY);

		if (fd < 0)
			return;

		if (ioctl(fd, VT_GETSTATE, &vt_state) < 0)
			return;

		close (fd);

		vtnr = vt_state.v_active;
	}

	snprintf(vt_string, sizeof(vt_string), "%d", vtnr);
	setenv("XDG_VTNR", vt_string, 1);
#ifdef XORG_PAM_APPNAME
	snprintf(vt_string, sizeof(vt_string), "XDG_VTNR=%d", vtnr);
	pam_putenv(pamh, vt_string);
	snprintf(vt_string, sizeof(vt_string), "/dev/tty%d", vtnr);
	pam_set_item(pamh, PAM_TTY, vt_string);
#endif
}

#ifdef HAVE_PLYMOUTH
static int start_xserver(int argc, char **argv, int pl_is_running)
#else
static int start_xserver(int argc, char **argv)
#endif
{
	int i;
	int count = 0;
	char *ptrs[32];
	char all[PATH_MAX] = "";
	char *xserver = NULL;
#ifdef HAVE_PLYMOUTH
	char vtnr[16];
	char dispn[16];
#endif

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

	ptrs[count] = xserver;

#ifdef HAVE_PLYMOUTH
	if (pl_is_running)
		plymouth_prepare_for_transition();

	if (getenv("XDG_VTNR") != NULL) {
		int vt;

		vt = atoi(getenv("XDG_VTNR"));

		if (vt > 0 && vt < 64) {

			sprintf(vtnr, "vt%d", vt);
			ptrs[++count] = vtnr;
			fprintf(stderr, "Using XDG_VTNR=%d / vt%d!\n", vt, vt);
		} else
			fprintf(stderr, "XDG_VTNR is invalid!\n");
	} else
		fprintf(stderr, "XDG_VTNR is unset!\n");

	if (getenv("XDG_SEAT") != NULL) {
		ptrs[++count] = "-seat";
		ptrs[++count] = getenv("XDG_SEAT");
	}

	if (pipe_fds[0] >= 0) {
		sprintf(dispn, "%d", pipe_fds[0]);
		ptrs[++count] = "-displayfd";
		ptrs[++count] = dispn;
	}
	ptrs[++count] = "-background";
	ptrs[++count] = "none";
	ptrs[++count] = "-noreset";
	ptrs[++count] = "-keeptty";
#endif

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
#ifdef HAVE_PLYMOUTH
	struct pollfd pfd;
	int pollval, pl_is_running;
#endif

	/* Step 1: block sigusr1 until we wait for it */
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigprocmask(SIG_BLOCK, &mask, &oldmask);

#ifdef HAVE_PLYMOUTH
	/* Step 2: open a pipe(2) to wait for Xorg to report its DISPLAY value */
	pl_is_running = plymouth_is_running();
	if (pipe(pipe_fds) != 0) {
		pipe_fds[0] = -1;
		pipe_fds[1] = -1;
	}
#endif

	/* Step 3: fork */
	pid = fork();
	if (pid) {
		struct timespec starttime;
		struct timespec timeout;
		int status, exitcode;
		int retries = 3;

		xpid = pid;

#ifdef HAVE_PLYMOUTH
		/* close the write end of the pipe */
		close(pipe_fds[1]);
#endif

		/* wait up to retries * 10 seconds for X server to start */
		clock_gettime(CLOCK_REALTIME, &starttime);
		timeout.tv_sec = 10;
		timeout.tv_nsec = 0;
		do {
			int ret =  sigtimedwait(&mask, NULL, &timeout);
			if (ret > 0) {
				assert(ret == SIGUSR1);
#ifndef HAVE_PLYMOUTH
				/* got SIGUSR1, X server has started */
				sd_notify(0, "READY=1");
#endif
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

#ifdef HAVE_PLYMOUTH
		/*
		 * Poll for the arrival of the DISPLAY value,
		 * this indicates the X server has started.
		 */
		pfd.fd = pipe_fds[0];
		pfd.events = POLLIN | POLLERR | POLLHUP;
		do {
			pollval = poll(&pfd, 1, -1);
		} while (pollval < 0 && errno == EINTR);

		if (pollval >= 0) {
			char disp[16];
			char xdisp[24];

			if (read(pipe_fds[0], disp, sizeof(disp)) > 0) {
				sprintf(xdisp, ":%s", disp);

				if (pl_is_running) {
					Display *display;
					int retries = 10;


					do {
						display = XOpenDisplay(xdisp);
						if (display) {
							slave_save_root_windows(display);
							XCloseDisplay(display);
							break;
						}

						sleep(1);
						retries--;
					} while (!display && retries);
				}

				sd_notifyf(0, "READY=1\nSTATUS=Xorg server started on DISPLAY=%s\n", xdisp);
			} else
				sd_notifyf(0, "READY=1\nSTATUS=Reading failed from -displayfd with errno %d\n", errno);
		} else
			sd_notifyf(0, "READY=1\nSTATUS=Polling failed on -displayfd with errno %d\n", errno);

		if (pl_is_running)
			plymouth_quit_with_transition();
#endif

		/* handle TERM gracefully and pass it on to xpid */
		memset(&term, 0, sizeof(struct sigaction));
		term.sa_handler = termhandler;
		sigaction(SIGINT, &term, NULL);
		sigaction(SIGTERM, &term, NULL);

		/* sit and wait for Xorg to exit */
		pid = waitpid(xpid, &status, 0);
		exitcode = (WIFEXITED(status) ? WEXITSTATUS(status) : EXIT_FAILURE);
		sd_notifyf(0, "STOPPING=1\nSTATUS=Xorg exited with %d\n", exitcode);
		return exitcode;
	}

	/* if we get here we're the child */
	retval = EXIT_FAILURE;

#ifdef HAVE_PLYMOUTH
	/*
	 * close the read end of the pipe and duplicate the writer end
	 * as the smaller numbered file descriptor.
	 */
	if (pipe_fds[0] >= 0) {
		dup2(pipe_fds[1], pipe_fds[0]);
		close(pipe_fds[1]);
	}
#endif

	/*
	 * reset signal mask and set the X server sigchld to SIG_IGN, that's the
	 * magic to make X send the parent the signal.
	 */
	sigprocmask(SIG_SETMASK, &oldmask, NULL);
	signal(SIGUSR1, SIG_IGN);

	/* Step 4: find the X server */
#ifdef XORG_PAM_APPNAME
	/*
	 * Authenticate with PAM using the application name
	 */
	myuid = getuid();
	pw = getpwuid(myuid);
	pamval = pam_start(XORG_PAM_APPNAME, pw->pw_name, &conv, &pamh);
	if (pamval == PAM_SUCCESS) {
		setenv("XDG_SEAT", "seat0", 1);
		pam_putenv(pamh, "XDG_SEAT=seat0");
		pam_putenv(pamh, "XDG_SESSION_CLASS=greeter");
		xdg_vtnr_current_vt(pamh, INITIAL_VT);

		pamval = pam_authenticate(pamh, 0);
		if (pamval == PAM_SUCCESS) {
			pamval = pam_acct_mgmt(pamh, 0);
			if (pamval == PAM_SUCCESS) {
				pamval = pam_open_session(pamh, 0);
				if (pamval == PAM_SUCCESS)
#ifdef HAVE_PLYMOUTH
					retval = start_xserver(argc, argv, pl_is_running);
#else
					retval = start_xserver(argc, argv);
#endif
			}
		}
	}

	if (pamh)
		pam_end(pamh, pamval);

#else

	xdg_vtnr_current_vt(INITIAL_VT);
#ifdef HAVE_PLYMOUTH
	retval = start_xserver(argc, argv, pl_is_running);
#else
	retval = start_xserver(argc, argv);
#endif

#endif

	return retval;
}
