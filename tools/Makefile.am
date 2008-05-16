bin_PROGRAMS = gnome-session-save

INCLUDES =					\
	$(WARN_CFLAGS)				\
	$(GNOME_SESSION_CFLAGS)			\
	$(DBUS_GLIB_CFLAGS)			\
	$(GCONF_FLAGS)				\
	-DLOCALE_DIR=\""$(datadir)/locale"\"

gnome_session_save_LDADD =			\
	-lSM -lICE				\
	$(GNOME_SESSION_LIBS)			\
	$(DBUS_GLIB_LIBS)			\
	$(GCONF_LIBS)

gnome_session_save_SOURCES =			\
	gnome-session-save.c
