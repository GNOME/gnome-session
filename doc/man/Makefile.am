XSLTPROC_FLAGS = \
        --nonet \
        --stringparam man.output.quietly 1 \
        --stringparam funcsynopsis.style ansi \
        --stringparam man.th.extra1.suppress 1 \
        --stringparam man.authors.section.enabled 0 \
        --stringparam man.copyright.section.enabled 0

.xml.1:
	$(AM_V_GEN) $(XSLTPROC) $(XSLTPROC_FLAGS) http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $<

man_MANS =				\
	gnome-session.1			\
	gnome-session-properties.1	\
	gnome-session-quit.1		\
	gnome-session-inhibit.1

if BUILD_SESSION_SELECTOR
man_MANS += gnome-session-selector.1
endif

EXTRA_DIST =				\
	gnome-session-inhibit.xml	\
	gnome-session-selector.xml	\
	$(man_MANS)

CLEANFILES =				\
	gnome-session-inhibit.1

-include $(top_srcdir)/git.mk
