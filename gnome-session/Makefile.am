NULL =

defaultdir = $(datadir)/gnome

INCLUDES =						\
	$(GNOME_SESSION_CFLAGS)				\
	$(STANDARD_PROPERTIES_CFLAGS)			\
	$(WARN_CFLAGS)					\
	$(DISABLE_DEPRECATED_CFLAGS)			\
	-DGNOMELOCALEDIR=\""$(prefix)/${DATADIRNAME}/locale\""	\
	-DGCONF_SANITY_CHECK=\""$(GCONF_SANITY_CHECK)"\"	    \
	-DGNOME_KEYRING_DAEMON=\""$(GNOME_KEYRING_DAEMON)"\"	    \
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps\""	\
	-DLIBEXECDIR=\""$(libexecdir)"\"                \
	-DREBOOT_COMMAND=\""$(REBOOT_COMMAND)\""	\
	-DHALT_COMMAND=\""$(HALT_COMMAND)\""		\
	-DRSH_COMMAND=\""$(RSH_COMMAND)\""		\
	-DGCONFTOOL_CMD=\""$(GCONFTOOL)\""		\
	-DDEFAULTDIR="\"$(defaultdir)\""		\
	-DESD_SERVER="\"$(ESD_SERVER)\""

# Used by the GNOME_PROGRAM_STANDARD_PROPERTIES macros
STANDARD_PROPERTIES_CFLAGS =                                    \
	-DPREFIX=\""$(prefix)"\"                                \
	-DSYSCONFDIR=\""$(sysconfdir)"\"                        \
	-DLIBDIR=\""$(libdir)"\"                                \
	-DDATADIR=\""$(datadir)"\"                              \
	$(NULL)

gnome_session_LDADD = $(X_LIBS) $(GNOME_SESSION_LIBS) $(LIBWRAP_LIBS)
gnome_session_save_LDADD = $(GNOME_SESSION_LIBS)
gnome_session_remove_LDADD = $(GNOME_SESSION_LIBS)
gnome_session_properties_LDADD = $(GNOME_SESSION_LIBS)
splash_test_LDADD = $(X_LIBS) $(GNOME_SESSION_LIBS)
logout_test_LDADD = $(X_LIBS) $(GNOME_SESSION_LIBS)

if SESSION
@INTLTOOL_DESKTOP_RULE@

noinst_PROGRAMS = \
	splash-test \
	logout-test

bin_PROGRAMS = \
	gnome-session		\
	gnome-session-save	\
	gnome-session-remove    \
	gnome-session-properties
endif

splash_test_SOURCES =	\
	splash-widget.c	\
	splash-widget.h	\
	splash-test.c

logout_test_SOURCES =		\
	logout-test.c		\
	util.c			\
	util.h			\
	gsm-multiscreen.c	\
	gsm-multiscreen.h	\
	gdm-logout-action.c	\
	gdm-logout-action.h

gnome_session_SOURCES =		\
	manager.c		\
	manager.h		\
	ice.c			\
	ice.h			\
	main.c			\
	prop.c			\
	prop.h			\
	save.c			\
	save.h			\
	command.c		\
	command.h		\
	session.h		\
	remote.c		\
	remote.h		\
	logout.c		\
	logout.h		\
	splash-widget.c		\
	splash-widget.h		\
	gsm-dbus.c		\
	gsm-dbus.h		\
	gsm-xrandr.c		\
	gsm-xrandr.h		\
	gsm-keyring.c		\
	gsm-keyring.h		\
	gsm-gsd.c		\
	gsm-gsd.h		\
	gsm-protocol.c		\
	gsm-protocol.h		\
	gsm-remote-desktop.c	\
	gsm-remote-desktop.h	\
	gsm-sound.c		\
	gsm-sound.h		\
	gsm-at-startup.c	\
	gsm-at-startup.h	\
	gsm-multiscreen.c	\
	gsm-multiscreen.h	\
	gsm-typebuiltins.c	\
	gsm-typebuiltins.h	\
	gsm-proxy.h		\
	gsm-proxy.c		\
	gdm-logout-action.c	\
	gdm-logout-action.h	\
	headers.h		\
	util.c			\
	util.h

gnome_session_save_SOURCES =	\
	save-session.c		\
	gsm-typebuiltins.c	\
	gsm-typebuiltins.h	\
	gsm-protocol.c		\
	gsm-protocol.h

gnome_session_remove_SOURCES = \
	gsm-remove-client.c \
	gsm-typebuiltins.c \
	gsm-typebuiltins.h \
	gsm-protocol.c \
	gsm-protocol.h

gnome_session_properties_SOURCES = \
	session-properties-capplet.c	\
	session-properties-capplet.h	\
	startup-programs.c		\
	session-names.c			\
	session-properties.c		\
	session-properties.h		\
	gsm-atk.c			\
	gsm-atk.h			\
	gsm-client-list.c		\
	gsm-client-list.h		\
	gsm-client-editor.c		\
	gsm-client-editor.h		\
	gsm-client-row.c		\
	gsm-client-row.h		\
	gsm-protocol.c			\
	gsm-protocol.h			\
	session.h			\
	gsm-typebuiltins.c		\
	gsm-typebuiltins.h

bin_SCRIPTS = gnome-wm

default_DATA = default.session default.wm

settingsdir = $(datadir)/applications

if SESSION
settings_in_files = session-properties.desktop.in
settings_DATA = $(settings_in_files:.desktop.in=.desktop)
endif

default.session: default.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.in > default.tmp \
	  && mv default.tmp default.session

default.wm: default.wm.in ../config.status
	sed -e 's,\@WINDOW_MANAGER\@,$(WINDOW_MANAGER),g' \
		< $(srcdir)/default.wm.in > default.wm.tmp \
	  && mv default.wm.tmp default.wm

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
	    GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/gnome-session/$$p ; \
	  done \
	fi
else
install-data-local:
endif

EXTRA_DIST =			\
	default.in		\
	default.wm.in		\
	gnome-wm 		\
	$(pixmap_DATA)		\
	$(schemas_in_files)	\
	gsm-marshal.list	\
	session-properties.desktop.in

gsm_enum_headers =					\
	$(top_srcdir)/gnome-session/gsm-protocol.h

BUILT_SOURCES = \
  gsm-typebuiltins.c gsm-typebuiltins.h gsm-marshal.c gsm-marshal.h

gsm-typebuiltins.c: @REBUILD@ $(gsm_enum_headers)
	(cd $(srcdir) \
	&&glib-mkenums  --fhead "#include <glib-object.h>\n" \
			--fhead "#include \"gsm-typebuiltins.h\"\n\n" \
			--fprod "\n/* enumerations from \"@filename@\" */" \
			--fprod "\n#include \"@filename@\"\n" \
			--vhead "static const G@Type@Value _@enum_name@_values[] = {" \
			--vprod "  { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
			--vtail "  { 0, NULL, NULL }\n};\n\n" \
			--vtail "GType\n@enum_name@_get_type (void)\n{\n" \
			--vtail "  static GType type = 0;\n\n" \
			--vtail "  if (!type)\n" \
			--vtail "    type = g_@type@_register_static (\"@EnumName@\", _@enum_name@_values);\n\n" \
			--vtail "  return type;\n}\n\n" \
		$(gsm_enum_headers) ) > xgen-gtc \
	&& cp xgen-gtc $(@F) \
	&& rm -f xgen-gtc

gsm-typebuiltins.h: stamp-gsm-typebuiltins.h
	@true
stamp-gsm-typebuiltins.h: @REBUILD@ $(gsm_enum_headers)
	(cd $(srcdir) \
	&& glib-mkenums --fhead "#ifndef __GSM_TYPEBUILTINS_H__\n" \
			--fhead "#define __GSM_TYPEBUILTINS_H__ 1\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* __GSM_TYPEBUILTINS_H__ */\n" \
			--fprod "\n/* --- @filename@ --- */" \
			--eprod "#define GSM_TYPE_@ENUMNAME@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(gsm_enum_headers) ) > xgen-gth \
	&& (cmp -s xgen-gth gsm-typebuiltins.h || cp xgen-gth gsm-typebuiltins.h) \
	&& rm -f xgen-gth \
	&& echo timestamp > $(@F)

gsm-marshal.h: stamp-gsm-marshal.h
	@true
stamp-gsm-marshal.h: gsm-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $(srcdir)/gsm-marshal.list --header --prefix=gsm_marshal > xgen-gmh \
	&& (cmp -s xgen-gmh gsm-marshal.h || cp xgen-gmh gsm-marshal.h) \
	&& rm -f xgen-gmh \
	&& echo timestamp > $(@F)

gsm-marshal.c: gsm-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $(srcdir)/gsm-marshal.list --body --prefix=gsm_marshal > xgen-gmc \
	&& cp xgen-gmc $@ \
	&& rm -f xgen-gmc

MAINTAINERCLEANFILES = stamp-gsm-typebuiltins.h stamp-gsm-marshal.h \
  $(BUILT_SOURCES)
CLEANFILES = xgen-gtc xgen-gth xgen-gmc xgen-gmh \
  default.wm default.session gnome-session.schemas session-properties.desktop

# if srcdir!=builddir, clean out maintainer-clean files from builddir
# this allows dist to pass.
distclean-local:
	if test $(srcdir) != .; then \
	  rm -f $(MAINTAINERCLEANFILES); \
	fi
