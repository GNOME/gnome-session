INCLUDES =						\
	$(GNOME_SESSION_CFLAGS)				\
	$(WARN_CFLAGS)					\
	-DGNOMELOCALEDIR=\""$(datadir)/locale\""

## FIXME: this sucks.  We should just rewrite to use Gtk and not Xt.
LDADD = $(X_LIBS) -lXt -lXmu $(SMPROXY_LIBS) $(INTLLIBS)

if SESSION
bin_PROGRAMS = gnome-smproxy
endif

gnome_smproxy_SOURCES = save.c smproxy.h smproxy.c

EXTRA_DIST = README.gnome
