## Process this file with automake to produce Makefile.in.

INCLUDES = 			\
	-I$(includedir)

LDADD = $(GNOME_LIBDIR) $(GNOMEUI_LIBS) $(INTLLIBS)

if SESSION
bin_PROGRAMS = gnome-session save-session
endif

gnome_session_SOURCES = manager.c manager.h ice.c auth.c auth.h	\
testbed.c prop.c save.c exec.c

save_session_SOURCES = save-session.c
