## Process this file with automake to produce Makefile.in.

INCLUDES = -I$(includedir) $(GNOME_INCLUDEDIR) \
	-I$(top_srcdir)/intl -I$(top_builddir)/intl	\
	-DGNOMELOCALEDIR=\""$(datadir)/locale\"" \
	-DDEFAULTDIR="\"$(datadir)\""

LDADD = $(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS)

sysdir = $(datadir)/apps/Settings

#if HAVE_CONTROL_CENTER
NONCC =
noinst_DATA = session.desktop
noinst_PROGRAMS = session-properties
#else
#NONCC = session-properties
#sys_DATA = session.desktop
#endif

if SESSION
bin_PROGRAMS = gnome-session save-session $(NONCC)
endif

gnome_session_SOURCES = manager.c manager.h ice.c auth.c auth.h	\
main.c prop.c save.c session.h

save_session_SOURCES = save-session.c

session_properties_SOURCES = session-properties.c session.h

data_DATA = default.session default.wm

default.session: default.in
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.in > default.tmp \
	  && mv default.tmp default.session

default.wm: default.wm.in
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.wm.in > default.wm.tmp \
	  && mv default.wm.tmp default.wm

EXTRA_DIST = session.desktop default.in default.wm.in

