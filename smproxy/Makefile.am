INCLUDES = \
	$(GNOME_SESSION_CFLAGS) \
	-DGNOMELOCALEDIR=\""$(datadir)/locale\""

## FIXME: this sucks.  We should just rewrite to use Gtk and not Xt.
LDADD = -lXt -lXmu $(GNOME_SESSION_LIBS) $(INTLLIBS)

if SESSION
bin_PROGRAMS = gnome-smproxy
endif

gnome_smproxy_SOURCES = save.c smproxy.h smproxy.c

man1_MANS = smproxy.man

EXTRA_DIST = README.gnome
