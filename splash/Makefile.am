INCLUDES =					\
	-I$(top_srcdir)/egg			\
	$(LIBGNOMEUI_CFLAGS)			\
	$(STARTUP_NOTIFICATION_CFLAGS)

LDADD =						\
	$(top_builddir)/egg/libeggsmclient-gnome.la \
	$(LIBGNOMEUI_LIBS)

helperdir = $(pkglibdir)/helpers
helper_PROGRAMS =				\
	gnome-session-splash			\
	gnome-login-sound

shutdowndir = $(datadir)/gnome/shutdown
shutdown_SCRIPTS =				\
	gnome-logout-sound.sh

default_sessiondir = $(datadir)/gnome/default-session
default_session_DATA =				\
	gnome-session-splash.desktop		\
	gnome-login-sound.desktop

gnome_session_splash_SOURCES =			\
	gnome-session-splash.c			\
	splash-window.c				\
	splash-window.h
gnome_session_splash_LDADD =			\
	$(STARTUP_NOTIFICATION_LIBS)		\
	$(LDADD)

gnome_login_sound_SOURCES =			\
	gnome-login-sound.c

%.desktop: %.desktop.in
	sed -e 's,\@helperdir\@,$(helperdir),' \
		< $^ > $@

CLEANFILES =					\
	$(default_session_DATA)

EXTRA_DIST =					\
	README					\
	gnome-session-splash.desktop.in		\
	gnome-login-sound.desktop.in		\
	gnome-logout-sound.sh
