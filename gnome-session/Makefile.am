## Process this file with automake to produce Makefile.in.

SUBDIRS = # help

defaultdir = $(datadir)/gnome

INCLUDES = \
	$(GNOME_SESSION_CFLAGS)				\
	-I$(top_srcdir)/intl -I$(top_builddir)/intl	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale\"" \
	-DDEFAULTDIR="\"$(defaultdir)\""

LDADD = $(GNOME_SESSION_LIBS) $(LIBWRAP_LIBS) $(INTLLIBS)

sysdir = $(datadir)/apps/Settings

if SESSION
noinst_DATA = session.desktop
bin_PROGRAMS = \
	gnome-session		\
	save-session		\
	gnome-login-check	\
	session-properties	\
	gnome-libice-check

#	session-properties-capplet
endif

gnome_session_SOURCES = \
	manager.c	\
	manager.h	\
	ice.c		\
	ice.h		\
	main.c		\
	prop.c		\
	prop.h		\
	save.c		\
	save.h		\
	command.c	\
	command.h	\
	session.h	\
	remote.c	\
	remote.h	\
	logout.c	\
	logout.h	\
	splash.c	\
	splash.h	\
	gsm-protocol.c	\
	gsm-protocol.h	\
	headers.h

save_session_SOURCES = \
	save-session.c	\
	gsm-protocol.c	\
	gsm-protocol.h

session_properties_SOURCES = \
	session-properties.c	\
	gsm-client-list.c	\
	gsm-client-list.h	\
	gsm-client-editor.c	\
	gsm-client-editor.h	\
	gsm-client-row.c	\
	gsm-client-row.h	\
	gsm-protocol.c		\
	gsm-protocol.h		\
	session.h

session_properties_capplet_SOURCES_ = \
	session-properties-capplet.c	\
	session-properties-capplet.h	\
	session.h			\
	startup-programs.c		\
	session-names.c			\
	gsm-protocol.c			\
	gsm-protocol.h			\
	gsm-client-list.c		\
	gsm-client-list.h		\
	gsm-client-row.c		\
	gsm-client-row.h		\
	gsm-client-editor.c		\
	gsm-client-editor.h 

gnome_login_check_SOURCES = gnome-login-check.c

gnome_libice_check_SOURCES = gnome-libice-check.c

bin_SCRIPTS = gnome-wm

default_DATA = default.session default.wm

desktopdir = $(datadir)/gnome/apps/Settings/Session/
desktop_DATA = save-session.desktop session.desktop

sysdir = $(datadir)/control-center/Session

if SESSION
sys_DATA = session-properties.desktop
endif

settingsdir = $(datadir)/gnome/apps/Settings/Session

if SESSION
settings_DATA = session-properties.desktop
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

manuals = gnome-wm.1 default.session.5.in gnome-session.1.in save-session.1

pixmapdir = $(datadir)/pixmaps/splash/
pixmap_DATA = gnome-splash.png gnome-splash-1.4.0.png

EXTRA_DIST =			\
	default.in		\
	default.wm.in		\
	gnome-wm 		\
	save-session.desktop 	\
	session.desktop		\
	$(manuals)		\
	$(pixmap_DATA)		\
	session-properties.desktop

marshal_sources = gsm-marshal.c gsm-marshal.h

gsm-marshal.h: gsm-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=gsm_marshal > $@
gsm-marshal.c: gsm-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=gsm_marshal > $@

gsm-protocol.c: $(marshal_sources)

CLEANFILES = $(marshal_sources)

DONT_DIST_SOURCE = $(marshal_sources)

dist-hook:
	for file in $(DONT_DIST_SOURCE) ; do \
		rm -f $(distdir)/$$file ; \
	done
