## Process this file with automake to produce Makefile.in.

INCLUDES = -I$(includedir) $(GNOME_INCLUDEDIR) \
	-DGNOMELOCALEDIR=\""$(datadir)/locale\"" \
	-DDEFAULTDIR="\"$(datadir)\""

LDADD = $(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS)

if SESSION
bin_PROGRAMS = gnome-session save-session session-properties
endif

gnome_session_SOURCES = manager.c manager.h ice.c auth.c auth.h	\
main.c prop.c save.c session.h

save_session_SOURCES = save-session.c

session_properties_SOURCES = session-properties.c session.h

sysdir = $(datadir)/apps/System
sys_DATA = session.desktop

data_DATA = default.session

default.session: default.in
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.in > default.tmp \
	  && mv default.tmp default.session

EXTRA_DIST = $(sys_DATA) default.in
