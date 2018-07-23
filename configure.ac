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
PKG_CHECK_MODULES([LIBSYSTEMD_DAEMON], [libsystemd])
AC_SUBST(LIBSYSTEMD_DAEMON_CFLAGS)
AC_SUBST(LIBSYSTEMD_DAEMON_LIBS)

AC_ARG_WITH([pam-appname],
	AC_HELP_STRING([--with-pam-appname=...],[Application name in /etc/pam.d]),
	[
		pam_appname="${withval}"
		AC_CHECK_HEADERS(
			security/pam_modules.h security/pam_appl.h security/pam_misc.h,
			[], [AC_MSG_ERROR([PAM headers are not installed])])
		AC_CHECK_LIB([pam],[pam_get_user],
			[PAM_LIBS="-lpam -lpam_misc"],
			[AC_MSG_ERROR([PAM libraries are not installed])])
		AC_DEFINE_UNQUOTED([XORG_PAM_APPNAME],["$pam_appname"],[Application name in /etc/pam.d])
	])
AC_SUBST(PAM_LIBS)

AC_ARG_ENABLE([plymouth],
	AC_HELP_STRING([--enable-plymouth],[Enable Plymouth smooth transition]),
	[AC_DEFINE([HAVE_PLYMOUTH],[1],[Enable Plymouth smooth transition])])

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

