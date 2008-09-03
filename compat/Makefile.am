INCLUDES =					\
	$(WARN_CFLAGS)				\
	$(DBUS_GLIB_CFLAGS)			\
	$(GCONF_CFLAGS)				\
	$(GNOME_KEYRING_CFLAGS)			\
	$(GTK_CFLAGS)				\
	-I$(top_srcdir)/egg			\
	-DAT_SPI_REGISTRYD_DIR=\""$(AT_SPI_REGISTRYD_DIR)"\" \
	-DDEFAULT_SESSION_DIR=\""$(default_sessiondir)"\" \
	-DGNOME_KEYRING_DAEMON=\""$(GNOME_KEYRING_DAEMON)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\"

LDADD =						\
	$(DBUS_GLIB_LIBS)			\
	$(GTK_LIBS)

helperdir = $(pkglibdir)/helpers
helper_PROGRAMS =				\
	at-spi-registryd-wrapper		\
	gnome-keyring-daemon-wrapper		\
	gnome-settings-daemon-helper

default_sessiondir = $(datadir)/gnome/autostart
default_session_in_in_files = 				\
	at-spi-registryd-wrapper.desktop.in.in	\
	gnome-keyring-daemon-wrapper.desktop.in.in	\
	gnome-settings-daemon-helper.desktop.in.in
default_session_in_files = $(default_session_in_in_files:.desktop.in.in=.desktop.in)
default_session_DATA = $(default_session_in_files:.desktop.in=.desktop)

at_spi_registryd_wrapper_SOURCES =		\
	at-spi-registryd-wrapper.c

gnome_keyring_daemon_wrapper_SOURCES =		\
	gnome-keyring-daemon-wrapper.c
gnome_keyring_daemon_wrapper_LDADD =		\
	$(top_builddir)/egg/libeggsmclient.la	\
	$(GNOME_KEYRING_LIBS)			\
	$(LDADD)

gnome_settings_daemon_helper_SOURCES =		\
	gnome-settings-daemon-helper.c
gnome_settings_daemon_helper_LDADD =		\
	$(GCONF_LIBS)				\
	$(LDADD)

@INTLTOOL_DESKTOP_RULE@

%.desktop.in: %.desktop.in.in Makefile.am
	sed -e 's,\@helperdir\@,$(helperdir),' \
		< $< > $@

EXTRA_DIST =					\
	README

CLEANFILES =					\
	$(default_session_DATA)			\
	$(default_session_in_files)

DISTCLEANFILES =					\
	$(default_session_in_in_files)
