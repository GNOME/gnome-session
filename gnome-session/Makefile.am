bin_PROGRAMS = gnome-session

noinst_LTLIBRARIES = libgsmutil.la

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
	-DGCONF_SANITY_CHECK=\""$(GCONF_SANITY_CHECK)"\" \
	-DGCONFTOOL_CMD=\"$(GCONFTOOL)\"

gnome_session_LDADD =				\
	-lSM -lICE				\
	libgsmutil.la 				\
	$(top_builddir)/egg/libeggdesktopfile.la \
	$(GNOME_SESSION_LIBS)			\
	$(DBUS_GLIB_LIBS)			\
	$(GCONF_LIBS)				\
	$(POLKIT_GNOME_LIBS)

gnome_session_SOURCES =				\
	gsm-autostart-app.h			\
	gsm-autostart-app.c			\
	gsm-resumed-app.h			\
	gsm-resumed-app.c			\
	gsm-app.h				\
	gsm-app.c				\
	gsm-xsmp-client.h			\
	gsm-xsmp-client.c			\
	gsm-client.c				\
	gsm-client.h				\
	gsm-marshal.h				\
	gsm-marshal.c				\
	gsm-consolekit.c			\
	gsm-consolekit.h			\
	gsm-logout-dialog.h			\
	gsm-logout-dialog.c			\
	gsm-power-manager.h			\
	gsm-power-manager.c			\
	gconf.c					\
	gconf.h					\
	gdm.h					\
	gdm.c					\
	main.c					\
	gsm-client-store.h			\
	gsm-client-store.c			\
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
	README					\
	gsm-marshal.list        		\
	gsm-manager.xml
