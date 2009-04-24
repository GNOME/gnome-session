NULL =
QUIET_GEN = $(Q:@=@echo '  GEN   '$@;)

INCLUDES =					\
	-I$(top_srcdir)/egg			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	$(WARN_CFLAGS)				\
	$(DISABLE_DEPRECATED_CFLAGS)		\
	$(SPLASH_CFLAGS)

LDADD =						\
	$(top_builddir)/egg/libeggsmclient-gnome.la \
	$(SPLASH_LIBS)

helperdir = $(pkglibdir)/helpers
helper_PROGRAMS =				\
	gnome-session-splash			\
	$(NULL)

default_sessiondir = $(datadir)/gnome/autostart
default_session_in_in_files = gnome-session-splash.desktop.in.in
default_session_in_files = $(default_session_in_in_files:.desktop.in.in=.desktop.in)
default_session_DATA = $(default_session_in_files:.desktop.in=.desktop)

gnome_session_splash_SOURCES =			\
	gnome-session-splash.c			\
	splash-window.c				\
	splash-window.h
gnome_session_splash_LDADD =			\
	$(LDADD)

@INTLTOOL_DESKTOP_RULE@

%.desktop.in: %.desktop.in.in Makefile.am
	$(QUIET_GEN)sed -e 's,\@helperdir\@,$(helperdir),' \
		< $< > $@

EXTRA_DIST =					\
	README					\
	$(NULL)

CLEANFILES =					\
	$(default_session_DATA)			\
	$(default_session_in_files)

DISTCLEANFILES =					\
	$(default_session_in_in_files)
