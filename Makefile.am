ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

# this should really be retrieved from pkg-config
mozillalibdir = $(datadir)/mozilla/plugins

mozillalib_LTLIBRARIES = libsweettooth.la

libsweettooth_la_LDFLAGS = -module -avoid-version -no-undefined
libsweettooth_la_LIBADD = $(MOZILLA_LIBS) $(GLIB_LIBS)
libsweettooth_la_SOURCES = sweettooth-plugin.c
libsweettooth_la_CFLAGS = $(MOZILLA_CFLAGS) $(GLIB_CFLAGS) -DG_LOG_DOMAIN=\"SweetTooth\"