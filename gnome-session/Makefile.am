EXTRA_DIST = gnome-session.in
NULL =
bin_SCRIPTS = gnome-session
libexec_PROGRAMS = gnome-session-binary gnome-session-failed
noinst_LTLIBRARIES = libgsmutil.la
noinst_PROGRAMS = 		\
	test-client-dbus	\
	test-inhibit		\
	test-process-helper

AM_CPPFLAGS =					\
	$(GTK3_CFLAGS)				\
	$(GNOME_SESSION_CFLAGS)			\
	$(UPOWER_CFLAGS)			\
	$(DISABLE_DEPRECATED_CFLAGS)

AM_CFLAGS = $(WARN_CFLAGS)

gnome-session: gnome-session.in Makefile
	$(AM_V_GEN) sed \
		-e "s|\@libexecdir\@|$(libexecdir)|" \
                $< > $@.tmp && mv $@.tmp $@

gnome_session_binary_SOURCES =			\
	$(BUILT_SOURCES)			\
	gsm-app.h				\
	gsm-app.c				\
	gsm-autostart-app.h			\
	gsm-autostart-app.c			\
	gsm-client.c				\
	gsm-client.h				\
	gsm-xsmp-client.h			\
	gsm-xsmp-client.c			\
	gsm-dbus-client.h			\
	gsm-dbus-client.c			\
	gsm-fail-whale.h			\
	gsm-fail-whale.c			\
	gsm-system.h				\
	gsm-system.c				\
	gsm-systemd.h				\
	gsm-systemd.c				\
	gsm-presence.h				\
	gsm-presence-flag.h			\
	gsm-presence.c				\
	gdm-log.h				\
	gdm-log.c				\
	main.c					\
	gsm-store.h				\
	gsm-store.c				\
	gsm-inhibitor.h				\
	gsm-inhibitor.c				\
	gsm-inhibitor-flag.h			\
	gsm-manager.c				\
	gsm-manager.h				\
	gsm-manager-logout-mode.h		\
	gsm-process-helper.c			\
	gsm-process-helper.h			\
	gsm-session-fill.c			\
	gsm-session-fill.h			\
	gsm-session-save.c			\
	gsm-session-save.h			\
	gsm-shell-extensions.c			\
	gsm-shell-extensions.h			\
	gsm-shell.c				\
	gsm-shell.h				\
	gsm-xsmp-server.c			\
	gsm-xsmp-server.h

if HAVE_CONSOLEKIT
gnome_session_binary_SOURCES += gsm-consolekit.c gsm-consolekit.h
else
EXTRA_DIST += gsm-consolekit.c gsm-consolekit.h
endif

gnome_session_binary_CPPFLAGS =			\
	$(AM_CPPFLAGS)				\
	$(X11_CFLAGS)				\
	$(SM_CFLAGS)				\
	$(ICE_CFLAGS)				\
	$(XEXT_CFLAGS)				\
	$(GCONF_CFLAGS)				\
	$(CONSOLEKIT_CFLAGS)			\
	$(SYSTEMD_CFLAGS)			\
	$(GIOUNIX_CFLAGS)			\
	-DLOCALE_DIR=\""$(datadir)/locale"\"	\
	-DDATA_DIR=\""$(datadir)/gnome-session"\" \
        -DLIBEXECDIR=\"$(libexecdir)\"		\
	-DGTKBUILDER_DIR=\""$(pkgdatadir)"\"	\
	-DGCONF_SANITY_CHECK=\""$(GCONF_SANITY_CHECK)"\" \
	-DGCONFTOOL_CMD=\"$(GCONFTOOL)\"

gnome_session_binary_LDADD =			\
	libgsmutil.la 				\
	$(X11_LIBS)				\
	$(SM_LIBS)				\
	$(ICE_LIBS)				\
	$(XRENDER_LIBS)				\
	$(XTEST_LIBS)				\
	$(XEXT_LIBS)				\
	$(GNOME_SESSION_LIBS)			\
	$(UPOWER_LIBS)				\
	$(GCONF_LIBS)				\
	$(CONSOLEKIT_LIBS)			\
	$(SYSTEMD_LIBS)				\
	$(GIOUNIX_LIBS)				\
	$(EXECINFO_LIBS)

libgsmutil_la_SOURCES =				\
	gsm-util.c				\
	gsm-util.h

libgsmutil_la_CPPFLAGS =                       \
       $(AM_CPPFLAGS)

libgsmutil_la_LIBADD = 				\
	$(GNOME_SESSION_LIBS)			\
	$(NULL)

gnome_session_failed_SOURCES =         \
       gsm-fail-whale-dialog.c \
       gsm-fail-whale-dialog.h \
       gsm-icon-names.h

gnome_session_failed_CPPFLAGS = \
       $(GTK3_CFLAGS)          \
       -DLOCALE_DIR=\""$(datadir)/locale"\"

gnome_session_failed_LDADD =   \
       $(GTK3_LIBS)

test_inhibit_SOURCES = test-inhibit.c
test_inhibit_LDADD = $(GTK3_LIBS) $(GNOME_SESSION_LIBS)

test_client_dbus_SOURCES = test-client-dbus.c
test_client_dbus_LDADD = $(GIO_LIBS)

test_process_helper_SOURCES = test-process-helper.c gsm-process-helper.c gsm-process-helper.h
test_process_helper_CFLAGS = $(AM_CFLAGS) $(GIO_CFLAGS)
test_process_helper_LDADD = $(GIO_LIBS)

org.gnome.SessionManager.h: org.gnome.SessionManager.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.gnome.SessionManager. \
	--generate-c-code org.gnome.SessionManager \
	--c-namespace Gsm \
        --annotate "org.gnome.SessionManager" "org.gtk.GDBus.C.Name" ExportedManager \
	$(srcdir)/org.gnome.SessionManager.xml
org.gnome.SessionManager.c: org.gnome.SessionManager.h
	@: # generated as a side-effect

org.gnome.SessionManager.Client.h: org.gnome.SessionManager.Client.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.gnome.SessionManager.Client. \
	--generate-c-code org.gnome.SessionManager.Client \
	--c-namespace Gsm \
        --annotate "org.gnome.SessionManager.Client" "org.gtk.GDBus.C.Name" ExportedClient \
	$(srcdir)/org.gnome.SessionManager.Client.xml
org.gnome.SessionManager.Client.c: org.gnome.SessionManager.Client.h
	@: # generated as a side-effect

org.gnome.SessionManager.ClientPrivate.h: org.gnome.SessionManager.ClientPrivate.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.gnome.SessionManager.ClientPrivate. \
	--generate-c-code org.gnome.SessionManager.ClientPrivate \
	--c-namespace Gsm \
        --annotate "org.gnome.SessionManager.ClientPrivate" "org.gtk.GDBus.C.Name" ExportedClientPrivate \
	$(srcdir)/org.gnome.SessionManager.ClientPrivate.xml
org.gnome.SessionManager.ClientPrivate.c: org.gnome.SessionManager.ClientPrivate.h
	@: # generated as a side-effect

org.gnome.SessionManager.App.h: org.gnome.SessionManager.App.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.gnome.SessionManager.App. \
	--generate-c-code org.gnome.SessionManager.App \
	--c-namespace Gsm \
        --annotate "org.gnome.SessionManager.App" "org.gtk.GDBus.C.Name" ExportedApp \
	$(srcdir)/org.gnome.SessionManager.App.xml
org.gnome.SessionManager.App.c: org.gnome.SessionManager.App.h
	@: # generated as a side-effect

org.gnome.SessionManager.Inhibitor.h: org.gnome.SessionManager.Inhibitor.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.gnome.SessionManager.Inhibitor. \
	--generate-c-code org.gnome.SessionManager.Inhibitor \
	--c-namespace Gsm \
        --annotate "org.gnome.SessionManager.Inhibitor" "org.gtk.GDBus.C.Name" ExportedInhibitor \
	$(srcdir)/org.gnome.SessionManager.Inhibitor.xml
org.gnome.SessionManager.Inhibitor.c: org.gnome.SessionManager.Inhibitor.h
	@: # generated as a side-effect

org.gnome.SessionManager.Presence.h: org.gnome.SessionManager.Presence.xml Makefile.am
	$(AM_V_GEN) gdbus-codegen --interface-prefix org.gnome.SessionManager.Presence. \
	--generate-c-code org.gnome.SessionManager.Presence \
	--c-namespace Gsm \
        --annotate "org.gnome.SessionManager.Presence" "org.gtk.GDBus.C.Name" ExportedPresence \
	$(srcdir)/org.gnome.SessionManager.Presence.xml
org.gnome.SessionManager.Presence.c: org.gnome.SessionManager.Presence.h
	@: # generated as a side-effect

BUILT_SOURCES =				\
	org.gnome.SessionManager.App.h		\
	org.gnome.SessionManager.App.c		\
	org.gnome.SessionManager.Client.h		\
	org.gnome.SessionManager.Client.c		\
	org.gnome.SessionManager.ClientPrivate.h	\
	org.gnome.SessionManager.ClientPrivate.c	\
	org.gnome.SessionManager.Inhibitor.h	\
	org.gnome.SessionManager.Inhibitor.c	\
	org.gnome.SessionManager.h		\
	org.gnome.SessionManager.c		\
	org.gnome.SessionManager.Presence.h	\
	org.gnome.SessionManager.Presence.c

EXTRA_DIST +=						\
	README						\
	org.gnome.SessionManager.xml			\
	org.gnome.SessionManager.App.xml		\
	org.gnome.SessionManager.Client.xml		\
	org.gnome.SessionManager.ClientPrivate.xml	\
	org.gnome.SessionManager.Inhibitor.xml		\
	org.gnome.SessionManager.Presence.xml

CLEANFILES =	\
	gnome-session \
	gnome-session.tmp \
	$(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
