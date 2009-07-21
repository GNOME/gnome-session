INCLUDES =					\
	$(WARN_CFLAGS)				\
	$(DISABLE_DEPRECATED_CFLAGS)		\
	$(COMPAT_CFLAGS)			\
	$(GCONF_CFLAGS)				\
	-I$(top_srcdir)/egg			\
	-DAT_SPI_REGISTRYD_DIR=\""$(AT_SPI_REGISTRYD_DIR)"\" \
	-DDEFAULT_SESSION_DIR=\""$(default_sessiondir)"\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\"

LDADD =						\
	$(COMPAT_LIBS)

helperdir = $(pkglibdir)/helpers
helper_PROGRAMS =				\
	at-spi-registryd-wrapper		\
	gnome-settings-daemon-helper

default_sessiondir = $(sysconfdir)/xdg/autostart
default_session_in_in_files = 				\
	at-spi-registryd-wrapper.desktop.in.in	\
	gnome-settings-daemon-helper.desktop.in.in
default_session_in_files = $(default_session_in_in_files:.desktop.in.in=.desktop.in)
default_session_DATA = $(default_session_in_files:.desktop.in=.desktop)

at_spi_registryd_wrapper_SOURCES =		\
	at-spi-registryd-wrapper.c

gnome_settings_daemon_helper_SOURCES =		\
	gnome-settings-daemon-helper.c
gnome_settings_daemon_helper_LDADD =		\
	$(GCONF_LIBS)				\
	$(LDADD)

@INTLTOOL_DESKTOP_RULE@

%.desktop.in: %.desktop.in.in Makefile.am
	$(AM_V_GEN)sed -e 's,\@helperdir\@,$(helperdir),' \
		< $< > $@

EXTRA_DIST =					\
	README

CLEANFILES =					\
	$(default_session_DATA)			\
	$(default_session_in_files)

DISTCLEANFILES =					\
	$(default_session_in_in_files)

-include $(top_srcdir)/git.mk
