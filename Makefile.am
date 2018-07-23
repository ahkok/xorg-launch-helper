
EXTRA_DIST = AUTHORS COPYING INSTALL

systemduserunitdir = @SYSTEMD_USERUNITDIR@
systemduserunit_DATA = xorg.service xorg.target

systemduserunit-install-hook:
	mkdir -p $(DESTDIR)$(systemduserunitdir)/xorg.target.wants
	ln -sf ../xorg.service $(DESTDIR)$(systemduserunitdir)/xorg.target.wants/xorg.service

install-data-hook: systemduserunit-install-hook

AM_CFLAGS = $(LIBSYSTEMD_DAEMON_CFLAGS) -Wall -Wno-uninitialized
AUTOMAKE_OPTIONS = subdir-objects

bin_PROGRAMS = xorg-launch-helper
xorg_launch_helper_SOURCES = src/main.c
xorg_launch_helper_LDADD = $(LIBSYSTEMD_DAEMON_LIBS) $(PAM_LIBS)

