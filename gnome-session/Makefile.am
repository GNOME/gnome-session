NULL =

defaultdir = $(datadir)/gnome

INCLUDES =						\
	$(GNOME_SESSION_CFLAGS)				\
	$(DISABLE_DEPRECATED_CFLAGS)			\
	$(STANDARD_PROPERTIES_CFLAGS)			\
	$(WARN_CFLAGS)					\
	-DGNOMELOCALEDIR=\""$(prefix)/${DATADIRNAME}/locale\""	\
	-DGCONF_SANITY_CHECK=\""$(GCONF_SANITY_CHECK)"\"	    \
	-DGNOME_ICONDIR=\""$(datadir)/pixmaps\""	\
	-DREBOOT_COMMAND=\""$(REBOOT_COMMAND)\""	\
	-DHALT_COMMAND=\""$(HALT_COMMAND)\""		\
	-DRSH_COMMAND=\""$(RSH_COMMAND)\""		\
	-DDEFAULTDIR="\"$(defaultdir)\""		\
	-DESD_SERVER="\"$(ESD_SERVER)\""

# Flags to make sure we're not using any deprecated functions
DISABLE_DEPRECATED_CFLAGS = 
#	-DG_DISABLE_DEPRECATED                                  \
#	-DGDK_DISABLE_DEPRECATED                                \
#	-DGTK_DISABLE_DEPRECATED                                \
#	-DGDK_PIXBUF_DISABLE_DEPRECATED                         \
#	-DGNOME_DISABLE_DEPRECATED                              \
#	$(NULL)

# Used by the GNOME_PROGRAM_STANDARD_PROPERTIES macros
STANDARD_PROPERTIES_CFLAGS =                                    \
	-DPREFIX=\""$(prefix)"\"                                \
	-DSYSCONFDIR=\""$(sysconfdir)"\"                        \
	-DLIBDIR=\""$(libdir)"\"                                \
	-DDATADIR=\""$(datadir)"\"                              \
	$(NULL)

gnome_session_LDADD = $(X_LIBS) $(GNOME_SESSION_LIBS) $(LIBWRAP_LIBS)
gnome_session_save_LDADD = $(GNOME_SESSION_LIBS)
gnome_session_properties_LDADD = $(GNOME_SESSION_LIBS)
splash_test_LDADD = $(GNOME_SESSION_LIBS)

if SESSION
@INTLTOOL_DESKTOP_RULE@

noinst_PROGRAMS = \
	splash-test

bin_PROGRAMS = \
	gnome-session		\
	gnome-session-save	\
	gnome-session-properties
endif

splash_test_SOURCES =	\
	splash-widget.c	\
	splash-widget.h	\
	splash-test.c

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
	gsm-gsd.c		\
	gsm-gsd.h		\
	gsm-protocol.c		\
	gsm-protocol.h		\
	gsm-sound.c		\
	gsm-sound.h		\
	gsm-multiscreen.c	\
	gsm-multiscreen.h	\
	gsm-typebuiltins.c	\
	gsm-typebuiltins.h	\
	headers.h		\
	util.c			\
	util.h

gnome_session_save_SOURCES = \
	save-session.c

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

# 	session.h			

bin_SCRIPTS = gnome-wm

default_DATA = default.session default.wm

settingsdir = $(datadir)/control-center-2.0/capplets

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

schemasdir       = @GCONF_SCHEMA_FILE_DIR@
schemas_in_files = gnome-session.schemas.in
schemas_DATA     = $(schemas_in_files:.schemas.in=.schemas)

@INTLTOOL_SCHEMAS_RULE@

# don't do this if we are building in eg. rpm
install-data-local:
	if test -z "$(DESTDIR)" ; then \
	  for p in $(schemas_DATA) ; do \
	    GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) --makefile-install-rule $(top_builddir)/gnome-session/$$p ; \
	  done \
	fi

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

gsm-typebuiltins.c: @REBUILD@ $(gsm_enum_headers)
	glib-mkenums    --fhead "#include <glib-object.h>\n" \
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
		$(gsm_enum_headers) > $@

gsm-typebuiltins.h: @REBUILD@ $(gsm_enum_headers)
	glib-mkenums    --fhead "#ifndef __GSM_TYPEBUILTINS_H__\n" \
			--fhead "#define __GSM_TYPEBUILTINS_H__ 1\n\n" \
			--fhead "G_BEGIN_DECLS\n\n" \
			--ftail "G_END_DECLS\n\n" \
			--ftail "#endif /* __GSM_TYPEBUILTINS_H__ */\n" \
			--fprod "\n/* --- @filename@ --- */" \
			--eprod "#define GSM_TYPE_@ENUMNAME@ @enum_name@_get_type()\n" \
			--eprod "GType @enum_name@_get_type (void);\n" \
		$(gsm_enum_headers) >  $@

$(gnome_session_OBJECTS) $(save_session_OBJECTS) $(session_properties_OBJECTS): $(built_sources)

gsm-marshal.h: gsm-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --header --prefix=gsm_marshal > $@
gsm-marshal.c: gsm-marshal.list $(GLIB_GENMARSHAL)
	$(GLIB_GENMARSHAL) $< --body --prefix=gsm_marshal > $@

built_sources =			\
	gsm-marshal.c		\
	gsm-marshal.h		\
	gsm-typebuiltins.c	\
	gsm-typebuiltins.h

CLEANFILES = $(built_sources)

dist-hook:
	cd $(distdir) ; rm -f $(CLEANFILES)
