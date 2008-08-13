NULL =

INCLUDES =					\
	-I$(top_srcdir)/egg			\
	$(DBUS_GLIB_CFLAGS)			\
	$(LIBGNOMEUI_CFLAGS)			\
	$(STARTUP_NOTIFICATION_CFLAGS)

LDADD =						\
	$(top_builddir)/egg/libeggsmclient-gnome.la \
	$(LIBGNOMEUI_LIBS)

helperdir = $(pkglibdir)/helpers
helper_PROGRAMS =				\
	gnome-session-splash			\
	$(NULL)

default_sessiondir = $(datadir)/gnome/autostart
default_session_DATA =				\
	gnome-session-splash.desktop		\
	$(NULL)

gnome_session_splash_SOURCES =			\
	gnome-session-splash.c			\
	splash-window.c				\
	splash-window.h
gnome_session_splash_LDADD =			\
	$(STARTUP_NOTIFICATION_LIBS)		\
	$(DBUS_GLIB_LIBS)			\
	$(LDADD)

%.desktop: %.desktop.in
	sed -e 's,\@helperdir\@,$(helperdir),' \
		< $^ > $@

CLEANFILES =					\
	$(default_session_DATA)

EXTRA_DIST =					\
	README					\
	gnome-session-splash.desktop.in		\
	$(NULL)