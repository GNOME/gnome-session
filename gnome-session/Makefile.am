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
noinst_in_files = session.desktop.in
noinst_DATA = $(noinst_in_files:.desktop.in=.desktop)
@XML_I18N_MERGE_DESKTOP_RULE@
bin_PROGRAMS = gnome-session save-session gnome-login-check session-properties gnome-libice-check session-properties-capplet
endif

gnome_session_SOURCES = manager.c manager.h ice.c ice.h main.c prop.c prop.h \
	save.c save.h command.c command.h session.h remote.c remote.h \
	logout.c logout.h splash.c splash.h gsm-protocol.c gsm-protocol.h \
	headers.h

gnome_session_LDADD=$(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS) @LIBWRAP_LIBS@ @CANVAS_PIXBUF_LIBS@

save_session_SOURCES = save-session.c gsm-protocol.c gsm-protocol.h

session_properties_SOURCES = session-properties.c \
gsm-client-list.c gsm-client-list.h gsm-client-editor.c gsm-client-editor.h \
gsm-client-row.c gsm-client-row.h gsm-protocol.c gsm-protocol.h \
session.h

session_properties_capplet_SOURCES = session-properties-capplet.c \
session-properties-capplet.h session.h startup-programs.c session-names.c \
gsm-protocol.c gsm-protocol.h gsm-client-list.c gsm-client-list.h \
gsm-client-row.c gsm-client-row.h gsm-client-editor.c gsm-client-editor.h 

session_properties_capplet_LDADD = $(GNOME_LIBDIR) $(ORB_LIBS) \
$(GNOMEUI_LIBS) $(INTLLIBS) -lgnorba -lcapplet

gnome_login_check_SOURCES = gnome-login-check.c

gnome_libice_check_SOURCES = gnome-libice-check.c

bin_SCRIPTS = gnome-wm

default_DATA = default.session default.wm

desktopdir = $(datadir)/gnome/apps/Settings/Session/
desktop_DATA = save-session.desktop session.desktop

sysdir = $(datadir)/control-center/Session

if SESSION
sys_in_files = session-properties.desktop.in
sys_DATA = $(sys_in_files:.desktop.in=.desktop)
endif

settingsdir = $(datadir)/gnome/apps/Settings/Session

if SESSION
settings_in_files = session-properties.desktop.in
settings_DATA = $(settings_in_files:.desktop.in=.desktop)
endif

default.session: default.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.in > default.tmp \
	  && mv default.tmp default.session

default.wm: default.wm.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.wm.in > default.wm.tmp \
	  && mv default.wm.tmp default.wm

man_MANS = gnome-wm.1 save-session.1 default.session.5 gnome-session.1

manuals = gnome-wm.1	\
	default.session.5.in	\
	gnome-session.1.in	\
	save-session.1		\
	gnome-libice-check.1	\
	gnome-login-check.1	\
	gnome-smproxy.1		\
	session-properties.1	\
	session-properties-capplet.1

pixmapdir = $(datadir)/pixmaps/splash/
pixmap_DATA = gnome-splash.png gnome-splash-1.4.0.png

EXTRA_DIST =			\
	default.in		\
	default.wm.in		\
	gnome-wm 		\
	save-session.desktop.in 	\
	session.desktop.in		\
	$(manuals)		\
	$(pixmap_DATA)		\
	session-properties.desktop.in


