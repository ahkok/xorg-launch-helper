#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([xorg-launch-helper], [4], [auke-jan.h.kok@intel.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_FILES([Makefile])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# FIXME: Replace `main' with a function in `-lrt':
AC_CHECK_LIB([rt], [main], ,
	     AC_MSG_ERROR([librt is required but was not found]))

PKG_CHECK_MODULES([SYSTEMD], [systemd])
PKG_CHECK_MODULES([LIBSYSTEMD_DAEMON], [libsystemd-daemon])
AC_SUBST(LIBSYSTEMD_DAEMON_CFLAGS)
AC_SUBST(LIBSYSTEMD_DAEMON_LIBS)

AC_ARG_WITH([systemduserunitdir], AC_HELP_STRING([--with-systemduserunitdir=DIR],
	[path to systemd user service directory]), [path_systemduserunit=${withval}],
	[path_systemduserunit="`$PKG_CONFIG --variable=systemduserunitdir systemd`"])
if (test -n "${path_systemduserunit}"); then
SYSTEMD_USERUNITDIR="${path_systemduserunit}"
AC_SUBST(SYSTEMD_USERUNITDIR)
AM_CONDITIONAL(SYSTEMD, test -n "${path_systemduserunit}")
fi

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/ioctl.h unistd.h])

# Checks for library functions.
AC_FUNC_FORK
AC_CHECK_FUNCS([clock_gettime memset strdup])

AC_OUTPUT([
xorg.service
xorg.target
])

