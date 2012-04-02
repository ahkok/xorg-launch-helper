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
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <pwd.h>

#include "user-session.h"

int tty = 1;
char username[256] = DEFAULT_USERNAME;
char dpinum[256] = "auto";
char addn_xopts[256] = "";
int session_pid;


static void
start_systemd_session(void)
{
	char *ptrs[3];
	int ret;

	ret = fork();
	if (ret) {
		session_pid = ret;
		return; /* parent remains active */
	}

	ret = system("/usr/bin/xdg-user-dirs-update");
	if (ret)
		lprintf("/usr/bin/xdg-user-dirs-update failed");

	ptrs[0] = strdup("/usr/lib/systemd/systemd");
	ptrs[1] = strdup("--user");
	ptrs[2] = NULL;
	ret = execv(ptrs[0], ptrs);

	if (ret != EXIT_SUCCESS)
		lprintf("Failed to start systemd --user");

	return;
}

/*
 * Launch apps that form the user's X session
 */
static void
launch_user_session(void)
{
	char xhost_cmd[80];

	dprintf("entering launch_user_session()");

	setup_user_environment();

	start_systemd_session();

	/* finally, set local username to be allowed at any time,
	 * which is not depenedent on hostname changes */
	snprintf(xhost_cmd, 80, "/usr/bin/xhost +SI:localuser:%s",
		 pass->pw_name);
	if (system(xhost_cmd) != 0)
		lprintf("%s failed", xhost_cmd);

	dprintf("leaving launch_user_session()");
}

int main(int argc, char **argv)
{
	/*
	 * General objective:
	 * Do the things that need root privs first,
	 * then switch to the final user ASAP.
	 *
	 * Once we're at the target user ID, we need
	 * to start X since that's the critical element
	 * from that point on.
	 *
	 * While X is starting, we can do the things
	 * that we need to do as the user UID, but that
	 * don't need X running yet.
	 *
	 * We then wait for X to signal that it's ready
	 * to draw stuff.
	 *
	 * Once X is running, we set up the ConsoleKit session,
	 * check if the screensaver needs to lock the screen
	 * and then start the window manager.
	 * After that we go over the autostart .desktop files
	 * to launch the various autostart processes....
	 * ... and we're done.
	 */

	pass = getpwnam(username);
	
	set_tty();

	setup_pam_session();

	switch_to_user();

	start_X_server();

	/*
	 * These steps don't need X running
	 * so can happen while X is talking to the
	 * hardware
	 */
	wait_for_X_signal();

	launch_user_session();

	/*
	 * The desktop session runs here
	 */
	wait_for_X_exit();

	set_text_mode();

	// close_consolekit_session();
	close_pam_session();

	unlink(xauth_cookie_file);

	/* Make sure that we clean up after ourselves */
	sleep(1);

	lprintf("Terminating user-session and all children");
	kill(0, SIGKILL);

	return EXIT_SUCCESS;
}
