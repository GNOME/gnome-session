bin_PROGRAMS = gnome-session

noinst_LTLIBRARIES = libgsmutil.la

INCLUDES =					\
	$(WARN_CFLAGS)				\
	$(GNOME_SESSION_CFLAGS)			\
	$(DBUS_GLIB_CFLAGS)			\
	$(GCONF_CFLAGS)				\
	$(GTK_CFLAGS)				\
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
	$(GCONF_LIBS)

gnome_session_SOURCES =				\
	app-autostart.c				\
	app-autostart.h				\
	app-resumed.c				\
	app-resumed.h				\
	app.c					\
	app.h					\
	client-xsmp.c				\
	client-xsmp.h				\
	client.c				\
	client.h				\
	dbus.c					\
	dbus.h					\
	gconf.c					\
	gconf.h					\
	gdm.h					\
	gdm.c					\
	gsm.h					\
	logout-dialog.h				\
	logout-dialog.c				\
	main.c					\
	power-manager.h				\
	power-manager.c				\
	session.c				\
	session.h				\
	xsmp.c					\
	xsmp.h

libgsmutil_la_LIBADD = 				\
	$(GNOME_SESSION_LIBS)

libgsmutil_la_SOURCES =				\
	util.c					\
	util.h

dbus.o dbus.lo: dbus-glue.h

dbus-glue.h: org.gnome.SessionManagement.xml
	dbus-binding-tool --mode=glib-server --prefix=gsm_dbus_server $^ > $@

CLEANFILES =					\
	dbus-glue.h

EXTRA_DIST =					\
	README					\
	org.gnome.SessionManagement.xml
