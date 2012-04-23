SUBDIRS = src

EXTRA_DIST = AUTHORS COPYING INSTALL

systemduserunitdir = @SYSTEMD_USERUNITDIR@
systemduserunit_DATA = xorg.service xorg.target

systemduserunit-install-hook:
	mkdir -p $(DESTDIR)$(systemduserunitdir)/xorg.target.wants
	ln -sf ../xorg.service $(DESTDIR)$(systemduserunitdir)/xorg.target.wants/xorg.service

install-data-hook: systemduserunit-install-hook

