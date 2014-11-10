DESTDIR=
sysconfdir = /etc
prefix     = /usr
libdir     = $(prefix)/lib/exim4-smtorp

INSTALL      = install -m700
INSTALL_DATA = install -m644

MAKE=make

all clean:
	$(MAKE) -C tor-smtp $@
install:
	for f in $$(cd exim4-conf.d; find . -type f); do \
		$(INSTALL_DATA) -D exim4-conf.d/$$f $(DESTDIR)$(sysconfdir)/exim4/conf.d/$$f; \
	done
	$(INSTALL_DATA) -D xinetd $(DESTDIR)$(sysconfdir)/xinetd.d/exim4-smtorp
	$(INSTALL) -D setup-exim-hidden-service $(DESTDIR)$(libdir)/setup-exim-hidden-service
