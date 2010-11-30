bin_PROGRAMS = gnome-session-save
libexec_PROGRAMS = gnome-session-is-accelerated

AM_CPPFLAGS =

AM_CFLAGS = $(WARN_CFLAGS)

gnome_session_save_SOURCES =			\
	gnome-session-save.c

gnome_session_save_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(GNOME_SESSION_CFLAGS)			\
	$(DBUS_GLIB_CFLAGS)			\
	$(GCONF_FLAGS)				\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	$(DISABLE_DEPRECATED_CFLAGS)

gnome_session_save_LDADD =			\
	$(SM_LIBS)				\
	$(ICE_LIBS)				\
	$(GNOME_SESSION_LIBS)			\
	$(DBUS_GLIB_LIBS)			\
	$(GCONF_LIBS)

gnome_session_is_accelerated_SOURCES = 		\
	gnome-session-is-accelerated.c

gnome_session_is_accelerated_CPPFLAGS =		\
	$(GL_TEST_CFLAGS)

gnome_session_is_accelerated_LDADD = 		\
	$(GL_TEST_LIBS)				\
	$(X_LIBS)

-include $(top_srcdir)/git.mk
