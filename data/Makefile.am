SUBDIRS = icons

default_sessiondir = $(datadir)/gnome/default-session
default_session_DATA =				\
	gnome-settings-daemon.desktop		\
	nautilus.desktop

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

install-data-local:
if GCONF_SCHEMAS_INSTALL
	if test -z "$(DESTDIR)" ; then \
	for p in $(schemas_DATA) ; do \
	GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/data/$$p >&1 > /dev/null; \
	done \
	fi
endif

EXTRA_DIST =					\
	README					\
	gnome-wm				\
	$(xsession_in_files)			\
	$(schemas_in_files)			\
	$(default_session_DATA)

DISTCLEANFILES =				\
	$(schemas_DATA)				\
	$(xsession_DATA)
