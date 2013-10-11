AM_CFLAGS = -Wall -Werror $(LIBSYSTEMD_DAEMON_CFLAGS)
AM_CPPFLAGS = -DSYSCONFDIR='"$(sysconfdir)"'
AM_LDFLAGS = $(LIBSYSTEMD_DAEMON_LIBS)
bin_PROGRAMS = systemd-echo
systemd_echo_SOURCES = main.c
dist_sysconf_DATA = systemd-echo.conf
