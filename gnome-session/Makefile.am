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
	-DRSH_COMMAND=\""$(RSH_COMMAND)\""		\
	-DGCONFTOOL_CMD=\""$(GCONFTOOL)\""		\
	-DDEFAULTDIR="\"$(defaultdir)\""		\
	-DAT_SPI_REGISTRYD_DIR="\"$(AT_SPI_REGISTRYD_DIR)\""	\
	-DTIME_UTILITY="\"$(TIME_UTILITY)\""		\
	-DSYSCONFDIR=\""$(sysconfdir)"\"

# Used by the GNOME_PROGRAM_STANDARD_PROPERTIES macros
STANDARD_PROPERTIES_CFLAGS =                                    \
	-DPREFIX=\""$(prefix)"\"                                \
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
	gdm-logout-action.c	\
	gdm-logout-action.h	\
	gsm-autostart.c		\
	gsm-autostart.h		\
	gsm-keyfile.c		\
	gsm-keyfile.h		\
	headers.h		\
	util.c			\
	util.h

gnome_session_save_SOURCES =	\
	gnome-session-save.c		\
	gsm-typebuiltins.c	\
	gsm-typebuiltins.h	\
	gsm-protocol.c		\
	gsm-protocol.h

gnome_session_remove_SOURCES = \
	gnome-session-remove.c \
	gsm-typebuiltins.c \
	gsm-typebuiltins.h \
	gsm-protocol.c \
	gsm-protocol.h

gnome_session_properties_SOURCES = \
	session-properties-capplet.c	\
	session-properties-capplet.h	\
	startup-programs.c		\
	session-properties.c		\
	session-properties.h		\
	gsm-atk.c			\
	gsm-atk.h			\
	gsm-autostart.c			\
	gsm-autostart.h			\
	gsm-keyfile.c			\
	gsm-keyfile.h			\
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

EXTRA_DIST =			\
	gnome-wm 		\
	gsm-marshal.list

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
CLEANFILES = xgen-gtc xgen-gth xgen-gmc xgen-gmh

# if srcdir!=builddir, clean out maintainer-clean files from builddir
# this allows dist to pass.
distclean-local:
	if test $(srcdir) != .; then \
	  rm -f $(MAINTAINERCLEANFILES); \
	fi
