## Process this file with automake to produce Makefile.in.

SUBDIRS = help

defaultdir = $(datadir)/gnome

INCLUDES = -I$(includedir) $(GNOME_INCLUDEDIR) \
	-I$(top_srcdir)/intl -I$(top_builddir)/intl	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale\"" \
	-DDEFAULTDIR="\"$(defaultdir)\"" @CANVAS_PIXBUF_CFLAGS@

LDADD = $(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS)

sysdir = $(datadir)/apps/Settings

if SESSION
noinst_DATA = session.desktop
bin_PROGRAMS = gnome-session save-session gnome-login-check session-properties gnome-libice-check
endif

gnome_session_SOURCES = manager.c manager.h ice.c main.c prop.c save.c \
	command.c session.h remote.c logout.c splash.h splash.c

gnome_session_LDADD=$(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS) @LIBWRAP_LIBS@ @CANVAS_PIXBUF_LIBS@

save_session_SOURCES = save-session.c gsm-protocol.c

session_properties_SOURCES = session-properties.c \
gsm-client-list.c gsm-client-list.h gsm-client-editor.c gsm-client-editor.h \
gsm-client-row.c gsm-client-row.h gsm-protocol.c gsm-protocol.h \
session.h

gnome_login_check_SOURCES = gnome-login-check.c

gnome_libice_check_SOURCES = gnome-libice-check.c

bin_SCRIPTS = gnome-wm

default_DATA = default.session default.wm

desktopdir = $(datadir)/gnome/apps/Settings/Session/
desktop_DATA = save-session.desktop session.desktop

default.session: default.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.in > default.tmp \
	  && mv default.tmp default.session

default.wm: default.wm.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.wm.in > default.wm.tmp \
	  && mv default.wm.tmp default.wm

man_MANS = gnome-wm.1 save-session.1 default.session.5 gnome-session.1

manuals = gnome-wm.1 default.session.5.in gnome-session.1.in save-session.1

pixmapdir = $(datadir)/pixmaps/splash/
pixmap_DATA = gnome-splash.png

EXTRA_DIST =			\
	default.in		\
	default.wm.in		\
	gnome-wm 		\
	save-session.desktop 	\
	session.desktop		\
	$(manuals)		\
	gnome-splash.png


