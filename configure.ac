#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.69])
AC_INIT([systemd-echo], [1.0.0], [xiaosuo@gmail.com])
AC_CONFIG_SRCDIR([src/main.c])
AM_INIT_AUTOMAKE([foreign -Wall -Werror])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_CC_C99
AC_PROG_CC_C_O

AC_USE_SYSTEM_EXTENSIONS
AC_SYS_LARGEFILE

AC_PROG_SED

PKG_PROG_PKG_CONFIG
AC_ARG_WITH([systemdsystemunitdir],
	    AS_HELP_STRING([--with-systemdsystemunitdir=DIR],
		    [Directory for systemd service files]),
	    [],
	    [with_systemdsystemunitdir=$($PKG_CONFIG --variable=systemdsystemunitdir systemd)])
if test "x$with_systemdsystemunitdir" != xno; then
	AC_SUBST([systemdsystemunitdir], [$with_systemdsystemunitdir])
fi
AM_CONDITIONAL(HAVE_SYSTEMD, [test -n "$with_systemdsystemunitdir" -a "x$with_systemdsystemunitdir" != xno])

# Checks for libraries.
PKG_CHECK_MODULES(LIBSYSTEMD_DAEMON, [libsystemd-daemon])
AC_SUBST(LIBSYSTEMD_DAEMON_CFLAGS)
AC_SUBST(LIBSYSTEMD_DAEMON_LIBS)

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h limits.h netinet/in.h stdlib.h string.h unistd.h])
AC_CHECK_HEADERS([sys/epoll.h], [], AC_MSG_ERROR([no sys/epoll.h is found]))
AC_CHECK_HEADERS([sys/signalfd.h], [], AC_MSG_ERROR([no sys/signalfd.h is found]))
AC_CHECK_HEADERS([sys/timerfd.h], [], AC_MSG_ERROR([no sys/timerfd.h is found]))

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT64_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset socket strchr strerror strtoull])
AC_CHECK_FUNCS([epoll_create1], [], AC_MSG_ERROR([no epoll_create1 is found]))

AC_CONFIG_FILES([Makefile
                 src/Makefile
		 systemd/Makefile])
AC_OUTPUT
AC_MSG_RESULT([
        $PACKAGE_NAME $VERSION
        PACKAGE_TARNAME:           ${PACKAGE_TARNAME}
        prefix:                    ${prefix}
        exec_prefix:               ${exec_prefix}
        bindir:                    ${bindir}
        sysconfdir:                ${sysconfdir}
        datarootdir:               ${datarootdir}
        docdir:                    ${docdir}
        systemdsystemunitdir:      ${with_systemdsystemunitdir}

        CC:                        ${CC}
        CFLAGS:                    ${CFLAGS}
        CPPFLAGS:                  ${CPPFLAGS}
        LDPFLAGS:                  ${LDFLAGS}
        LIBSYSTEMD_DAEMON_CFLAGS:  ${LIBSYSTEMD_DAEMON_CFLAGS}
        LIBSYSTEMD_DAEMON_LIBS:    ${LIBSYSTEMD_DAEMON_LIBS}
])
