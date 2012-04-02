#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([user-session], [1], [auke-jan.h.kok@intel.com])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_FILES([Makefile src/Makefile])
AC_CONFIG_SRCDIR([src/main.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# FIXME: Replace `main' with a function in `-lXau':
AC_CHECK_LIB([Xau], [main], ,
	     AC_MSG_ERROR([libXau is required but was not found]))
# FIXME: Replace `main' with a function in `-lpam':
AC_CHECK_LIB([pam], [main], ,
	     AC_MSG_ERROR([libpam is required but was not found]))
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

echo "checking for default username to logon... "
AC_ARG_WITH([default_username], AC_HELP_STRING([--with-default-username=USER],
	[default username to logon (default: unset)]), [default_username=${withval}],)
if (test -n "${default_username}"); then
	AC_DEFINE_UNQUOTED([DEFAULT_USERNAME], ["$default_username"], [Which user to logon])
else
	AC_ERROR([Required value for --with-default-username=<USER> is missing.])
fi

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h stdlib.h string.h sys/ioctl.h sys/time.h syslog.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_CHOWN
AC_FUNC_FORK
AC_CHECK_FUNCS([clock_gettime gettimeofday memset mkdir setenv strchr strdup strstr uname])

AC_OUTPUT(user-session.service)

