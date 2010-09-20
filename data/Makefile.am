SUBDIRS = icons

bin_SCRIPTS = gnome-wm

uidir = $(pkgdatadir)
ui_DATA = \
	session-properties.ui	\
	gsm-inhibit-dialog.ui

xsessiondir = $(datadir)/xsessions
xsession_in_files = gnome.desktop.in
xsession_DATA = $(xsession_in_files:.desktop.in=.desktop)

desktopdir = $(datadir)/applications
desktop_in_files = session-properties.desktop.in
desktop_DATA = $(desktop_in_files:.desktop.in=.desktop)

if USE_GNOME_WM
desktop_in_files += gnome-wm.desktop.in
endif

@INTLTOOL_DESKTOP_RULE@

schemas_in_in_files = gnome-session.schemas.in.in
schemas_in_files = $(schemas_in_in_files:.schemas.in.in=.schemas.in)
schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

$(schemas_in_files): $(schemas_in_files).in Makefile
	$(AM_V_GEN)sed \
		-e "s|\@DEFAULT_WM\@|$(DEFAULT_WM)|" \
		$< > $@

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
install-data-local:
	if test -z "$(DESTDIR)" ; then \
		for p in $(schemas_DATA) ; do \
			GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p ; \
		done \
	fi
uninstall-local:
	for p in $(schema_DATA) ; do \
		GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-uninstall-rule $(top_builddir)/data/$$p ; \
	done
endif

EXTRA_DIST =					\
	gnome-wm				\
	$(xsession_in_files)			\
	$(schemas_in_in_files)			\
	$(ui_DATA)				\
	$(pixmap_DATA_dist)

CLEANFILES =					\
	$(schemas_in_files)			\
	$(schemas_DATA)				\
	$(xsession_DATA)			\
	$(desktop_DATA)				\
	gnome-wm.desktop

DISTCLEANFILES =				\
	$(desktop_in_files)

-include $(top_srcdir)/git.mk
