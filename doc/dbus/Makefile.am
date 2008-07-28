NULL =

SPEC_XML_FILES =			\
	gnome-session.xml		\
	org.gnome.SessionManager.ref.xml \
	org.gnome.SessionManager.Client.ref.xml \
	org.gnome.SessionManager.DBusClient.ref.xml \
	org.gnome.SessionManager.Inhibitor.ref.xml \
	$(NULL)

if DOCBOOK_DOCS_ENABLED

htmldocdir = $(docdir)/dbus
htmldoc_DATA = gnome-session.html

gnome-session.html : $(SPEC_XML_FILES)
	$(XMLTO) xhtml-nochunks -m $(top_srcdir)/doc/dbus/config.xsl gnome-session.xml

endif # DOCBOOK_DOCS_ENABLED

BUILT_SOURCES =	\
	org.gnome.SessionManager.ref.xml \
	org.gnome.SessionManager.Client.ref.xml \
	org.gnome.SessionManager.DBusClient.ref.xml \
	org.gnome.SessionManager.Inhibitor.ref.xml \
	$(NULL)

org.gnome.SessionManager.ref.xml : $(top_srcdir)/gnome-session/gsm-manager.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.gnome.SessionManager.Client.ref.xml : $(top_srcdir)/gnome-session/gsm-client.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.gnome.SessionManager.DBusClient.ref.xml : $(top_srcdir)/gnome-session/gsm-dbus-client.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@
org.gnome.SessionManager.Inhibitor.ref.xml : $(top_srcdir)/gnome-session/gsm-inhibitor.xml spec-to-docbook.xsl
	$(XSLTPROC) $(top_srcdir)/doc/dbus/spec-to-docbook.xsl $< | tail -n +2 > $@

EXTRA_DIST =				\
	spec-to-docbook.xsl		\
	gnome-session.xml.in		\
	config.xsl			\
	docbook.css			\
	dbus-introspect-docs.dtd	\
	$(NULL)


MAINTAINERCLEANFILES =		\
	*~			\
	Makefile.in		\
	$(NULL)

clean-local:
	rm -f *~
	rm -f gnome-session.html
	rm -f *.ref.xml
