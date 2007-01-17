defaultdir = $(datadir)/gnome

default_DATA = default.session

default.session: default.session.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.session.in > default.session.tmp \
	  && mv default.session.tmp default.session

settingsdir = $(datadir)/applications

if SESSION
@INTLTOOL_DESKTOP_RULE@

settings_in_files = session-properties.desktop.in
settings_DATA = $(settings_in_files:.desktop.in=.desktop)
endif

pixmapdir = $(datadir)/pixmaps/splash/
pixmap_DATA = gnome-splash.png

schemasdir       = $(GCONF_SCHEMA_FILE_DIR)
schemas_in_files = gnome-session.schemas.in
schemas_DATA     = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

if GCONF_SCHEMAS_INSTALL
# don't do this if we are building in eg. rpm
install-data-local:
	if test -z "$(DESTDIR)" ; then \
	  for p in $(schemas_DATA) ; do \
	    GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p ; \
	  done \
	fi
else
install-data-local:
endif

EXTRA_DIST =			\
	$(default_DATA)		\
	$(pixmap_DATA)		\
	$(schemas_in_files)	\
	session-properties.desktop.in

CLEANFILES =  default.session gnome-session.schemas session-properties.desktop
