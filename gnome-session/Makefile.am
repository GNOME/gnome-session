## Process this file with automake to produce Makefile.in.

INCLUDES = -I$(includedir) $(GNOME_INCLUDEDIR) \
	-DGNOMELOCALEDIR=\""$(datadir)/locale\""

LDADD = $(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS)

if SESSION
bin_PROGRAMS = gnome-session save-session session-properties
endif

gnome_session_SOURCES = manager.c manager.h ice.c auth.c auth.h	\
main.c prop.c save.c

save_session_SOURCES = save-session.c

session_properties_SOURCES = session-properties.c
