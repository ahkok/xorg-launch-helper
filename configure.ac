#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([xorg-launch-helper], [1], [auke-jan.h.kok@intel.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# FIXME: Replace `main' with a function in `-lpthread':
AC_CHECK_LIB([pthread], [main], ,
	     AC_MSG_ERROR([libpthread is required but was not found]))
# FIXME: Replace `main' with a function in `-lrt':
AC_CHECK_LIB([rt], [main], ,
	     AC_MSG_ERROR([librt is required but was not found]))

PKG_CHECK_MODULES([SYSTEMD], [systemd])

AC_ARG_WITH([systemdunitdir], AC_HELP_STRING([--with-systemdunitdir=DIR],
	[path to systemd service directory]), [path_systemdunit=${withval}],
	[path_systemdunit="`$PKG_CONFIG --variable=systemdsystemunitdir systemd`"])
if (test -n "${path_systemdunit}"); then
SYSTEMD_UNITDIR="${path_systemdunit}"
AC_SUBST(SYSTEMD_UNITDIR)
AM_CONDITIONAL(SYSTEMD, test -n "${path_systemdunit}")
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

