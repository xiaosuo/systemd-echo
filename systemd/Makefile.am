do_subst = $(SED) -e 's,[@]bindir[@],$(bindir),g'

if HAVE_SYSTEMD
nodist_systemdsystemunit_DATA = \
	 systemd-echo.socket \
	 systemd-echo.service

CLEANFILES = $(nodist_systemdsystemunit_DATA)

systemd-echo.service: systemd-echo.service.in Makefile
	$(do_subst) < $< > $@

systemd-echo.socket: systemd-echo.socket.in Makefile
	$(do_subst) < $< > $@

EXTRA_DIST = \
	 systemd-echo.socket.in \
	 systemd-echo.service.in
endif
