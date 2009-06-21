NULL =

bin_PROGRAMS = gnome-session-properties

INCLUDES =						\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED_CFLAGS)			\
	$(SESSION_PROPERTIES_CFLAGS)			\
	$(GCONF_CFLAGS)					\
	-I$(top_srcdir)/egg				\
	-I$(top_srcdir)/gnome-session			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"		\
	-DGTKBUILDER_DIR=\""$(pkgdatadir)"\"		\
	$(NULL)

gnome_session_properties_LDADD =			\
	$(SESSION_PROPERTIES_LIBS)			\
	$(top_builddir)/egg/libeggdesktopfile.la 	\
	$(top_builddir)/gnome-session/libgsmutil.la 	\
	$(GCONF_LIBS)					\
	$(NULL)

gnome_session_properties_SOURCES =			\
	main.c						\
	gsm-properties-dialog.h				\
	gsm-properties-dialog.c				\
	gsm-app-dialog.h				\
	gsm-app-dialog.c				\
	gsp-app.h					\
	gsp-app.c					\
	gsp-app-manager.h				\
	gsp-app-manager.c				\
	gsp-keyfile.h					\
	gsp-keyfile.c					\
	$(NULL)

-include $(top_srcdir)/git.mk
