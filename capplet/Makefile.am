bin_PROGRAMS = gnome-session-properties

INCLUDES =						\
	$(WARN_CFLAGS)					\
	$(SESSION_PROPERTIES_CFLAGS)			\
	$(GCONF_CFLAGS)					\
	-I$(top_srcdir)/egg				\
	-I$(top_srcdir)/gnome-session			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"		\
	-DDATA_DIR=\""$(datadir)/gnome-session"\"

gnome_session_properties_LDADD =			\
	$(SESSION_PROPERTIES_LIBS)			\
	$(top_builddir)/egg/libeggdesktopfile.la 	\
	$(top_builddir)/gnome-session/libgsmutil.la 	\
	$(GCONF_LIBS)

gnome_session_properties_SOURCES =			\
	main.c						\
	commands.h					\
	commands.c					\
	ui.h						\
	ui.c
