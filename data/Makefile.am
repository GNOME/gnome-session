default_sessiondir = $(datadir)/gnome/default-session
default_session_DATA =				\
	gnome-panel.desktop			\
	gnome-settings-daemon.desktop		\
	metacity.desktop			\
	nautilus.desktop

@INTLTOOL_SCHEMAS_RULE@

schemas_in_files = gnome-session.schemas.in
schemasdir = $(GCONF_SCHEMA_FILE_DIR)
schemas_DATA = $(schemas_in_files:.schemas.in=.schemas)

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
	$(schemas_in_files)			\
	$(default_session_DATA)
