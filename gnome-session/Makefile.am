NULL =

bin_PROGRAMS = gnome-session

noinst_LTLIBRARIES = libgsmutil.la

noinst_PROGRAMS = 		\
	test-client-method	\
	test-client-service	\
	test-inhibit		\
	$(NULL)

INCLUDES =					\
	$(WARN_CFLAGS)				\
	$(GNOME_SESSION_CFLAGS)			\
	$(DBUS_GLIB_CFLAGS)			\
	$(GCONF_CFLAGS)				\
	$(GTK_CFLAGS)				\
	$(POLKIT_GNOME_CFLAGS)			\
	-I$(top_srcdir)/egg			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	-DDATA_DIR=\""$(datadir)/gnome-session"\" \
	-DDBUS_LAUNCH=\"dbus-launch\"		\
	-DGLADEDIR=\""$(pkgdatadir)"\"		\
	-DGCONF_SANITY_CHECK=\""$(GCONF_SANITY_CHECK)"\" \
	-DGCONFTOOL_CMD=\"$(GCONFTOOL)\"

gladedir = $(pkgdatadir)
glade_DATA = 					\
	gsm-logout-inhibit-dialog.glade		\
	$(NULL)

test_inhibit_SOURCES = 	\
	test-inhibit.c	\
	$(NULL)

test_inhibit_LDADD =				\
	$(DBUS_GLIB_LIBS)			\
	$(NULL)

test_client_method_SOURCES = 	\
	test-client-method.c	\
	$(NULL)

test_client_method_LDADD =			\
	$(DBUS_GLIB_LIBS)			\
	$(NULL)

test_client_service_SOURCES = 	\
	test-client-service.c	\
	$(NULL)

test_client_service_LDADD =			\
	$(DBUS_GLIB_LIBS)			\
	$(NULL)

gnome_session_LDADD =				\
	-lSM -lICE				\
	libgsmutil.la 				\
	$(top_builddir)/egg/libeggdesktopfile.la \
	$(GNOME_SESSION_LIBS)			\
	$(DBUS_GLIB_LIBS)			\
	$(GCONF_LIBS)				\
	$(POLKIT_GNOME_LIBS)

gnome_session_SOURCES =				\
	gsm-app.h				\
	gsm-app.c				\
	gsm-autostart-app.h			\
	gsm-autostart-app.c			\
	gsm-resumed-app.h			\
	gsm-resumed-app.c			\
	gsm-client.c				\
	gsm-client.h				\
	gsm-xsmp-client.h			\
	gsm-xsmp-client.c			\
	gsm-dbus-client.h			\
	gsm-dbus-client.c			\
	gsm-method-client.h			\
	gsm-method-client.c			\
	gsm-service-client.h			\
	gsm-service-client.c			\
	gsm-marshal.h				\
	gsm-marshal.c				\
	gsm-consolekit.c			\
	gsm-consolekit.h			\
	gsm-logout-dialog.h			\
	gsm-logout-dialog.c			\
	gsm-logout-inhibit-dialog.h		\
	gsm-logout-inhibit-dialog.c		\
	gsm-power-manager.h			\
	gsm-power-manager.c			\
	gconf.c					\
	gconf.h					\
	gdm.h					\
	gdm.c					\
	main.c					\
	gsm-client-store.h			\
	gsm-client-store.c			\
	gsm-inhibitor.h				\
	gsm-inhibitor.c				\
	gsm-inhibitor-store.h			\
	gsm-inhibitor-store.c			\
	gsm-manager.c				\
	gsm-manager.h				\
	gsm-xsmp-server.c			\
	gsm-xsmp-server.h

libgsmutil_la_LIBADD = 				\
	$(GNOME_SESSION_LIBS)

libgsmutil_la_SOURCES =				\
	util.c					\
	util.h

gsm-marshal.c: gsm-marshal.list
	echo "#include \"gsm-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gsm_marshal --body >> $@

gsm-marshal.h: gsm-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gsm_marshal --header > $@

gsm-manager-glue.h: gsm-manager.xml Makefile.am
	dbus-binding-tool --prefix=gsm_manager --mode=glib-server --output=gsm-manager-glue.h $(srcdir)/gsm-manager.xml

BUILT_SOURCES =			\
	gsm-marshal.c           \
	gsm-marshal.h           \
	gsm-manager-glue.h

CLEANFILES =					\
	$(BUILT_SOURCES)

EXTRA_DIST =					\
	$(glade_DATA)				\
	README					\
	gsm-marshal.list        		\
	gsm-manager.xml

MAINTAINERCLEANFILES =                  \
        *~                              \
        Makefile.in
