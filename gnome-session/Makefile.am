## Process this file with automake to produce Makefile.in.

INCLUDES = 			\
	-I$(includedir)

LDADD = \
	@LEXLIB@					\
	$(GNOME_LIBDIR)					\
	$(GNOMEUI_LIBS)					\
	$(DL_LIB)					\
        @XF86MISC_LIBS@

bin_PROGRAMS = gnome-session

gnome_session_SOURCES = manager.c manager.h ice.c auth.c auth.h	\
testbed.c prop.c save.c exec.c
