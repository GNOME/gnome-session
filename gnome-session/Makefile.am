## Process this file with automake to produce Makefile.in.

INCLUDES = 			\
	-I../.. 		\
	-I$(srcdir)/../.. 	\
	-I$(includedir)		\
	-I$(top_srcdir)/intl	\
	$(X_CFLAGS)

LDADD = \
	$(top_builddir)/libgnome/libgnome.la		\
	$(top_builddir)/libgnomeui/libgnomeui.la 	\
	$(GTK_LIBS)					\
        $(X_LIBS) $(x_libs)				\
        @XF86MISC_LIBS@					\
	@LEXLIB@					\
	@INTLLIBS@

bin_PROGRAMS = gsm

gsm_SOURCES = manager.c manager.h ice.c auth.c auth.h testbed.c
