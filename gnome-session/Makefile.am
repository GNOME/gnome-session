## Process this file with automake to produce Makefile.in.

defaultdir = $(datadir)/gnome

INCLUDES = -I$(includedir) $(GNOME_INCLUDEDIR) \
	-I$(top_srcdir)/intl -I$(top_builddir)/intl	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale\"" \
	-DDEFAULTDIR="\"$(defaultdir)\""

LDADD = $(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS)

sysdir = $(datadir)/apps/Settings

if SESSION
noinst_DATA = session.desktop
noinst_PROGRAMS = session-properties
bin_PROGRAMS = gnome-session save-session
endif

gnome_session_SOURCES = manager.c manager.h ice.c main.c prop.c save.c \
command.c session.h remote.c

save_session_SOURCES = save-session.c

session_properties_SOURCES = session-properties.c \
gsm-client-list.c gsm-client-list.h gsm-client-editor.c gsm-client-editor.h \
gsm-client-row.c gsm-client-row.h gsm-protocol.c gsm-protocol.h \
session.h 

bin_SCRIPTS = gnome-wm

default_DATA = default.session default.wm

default.session: default.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.in > default.tmp \
	  && mv default.tmp default.session

default.wm: default.wm.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.wm.in > default.wm.tmp \
	  && mv default.wm.tmp default.wm

EXTRA_DIST =		\
	session.desktop	\
	default.in	\
	default.wm.in	\
	gnome-wm
