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
	gnome-login-sound

shutdowndir = $(datadir)/gnome/shutdown
shutdown_SCRIPTS =				\
	gnome-logout-sound.sh

default_sessiondir = $(datadir)/gnome/autostart
default_session_DATA =				\
	gnome-login-sound.desktop

gnome_login_sound_SOURCES =			\
	gnome-login-sound.c

%.desktop: %.desktop.in
	sed -e 's,\@helperdir\@,$(helperdir),' \
		< $^ > $@

CLEANFILES =					\
	$(default_session_DATA)

EXTRA_DIST =					\
	README					\
	gnome-login-sound.desktop.in		\
	gnome-logout-sound.sh
