NULL =
SUBDIRS = icons

uidir = $(pkgdatadir)
ui_DATA = \
	session-properties.ui	\
	gsm-inhibit-dialog.ui	\
	$(NULL)

@INTLTOOL_DESKTOP_RULE@

xsessiondir = $(datadir)/xsessions
xsession_in_files = gnome.desktop.in
xsession_DATA = $(xsession_in_files:.desktop.in=.desktop)

@INTLTOOL_SCHEMAS_RULE@

pixmapdir = $(datadir)/pixmaps/splash/
pixmap_DATA = gnome-splash.png

schemas_in_files = gnome-session.schemas.in
schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

bin_SCRIPTS = gnome-wm

settingsdir = $(datadir)/applications
settings_in_files = session-properties.desktop.in
settings_DATA = $(settings_in_files:.desktop.in=.desktop)

install-data-local:
if GCONF_SCHEMAS_INSTALL
	if test -z "$(DESTDIR)" ; then \
	for p in $(schemas_DATA) ; do \
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p >&1 > /dev/null; \
	done \
	fi
endif

EXTRA_DIST =					\
	gnome-wm				\
	$(xsession_in_files)			\
	$(schemas_in_files)			\
	$(ui_DATA)				\
	$(pixmap_DATA)

CLEANFILES =					\
	$(schemas_DATA)				\
	$(xsession_DATA)			\
	$(settings_DATA)

DISTCLEANFILES =				\
	$(settings_in_files)

-include $(top_srcdir)/git.mk
