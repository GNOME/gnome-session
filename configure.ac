AC_INIT([gnome-session],
        [3.20.2],
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-session])

AC_CONFIG_SRCDIR([gnome-session])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_MACRO_DIR([m4])

AM_INIT_AUTOMAKE([1.11 no-dist-gzip dist-xz tar-ustar])

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# Check for programs
AC_PROG_CC
AM_PROG_CC_C_O
AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)
PKG_PROG_PKG_CONFIG()

# Initialize libtool
LT_PREREQ([2.2.6])
LT_INIT([dlopen disable-static])

GNOME_MAINTAINER_MODE_DEFINES
GNOME_COMPILE_WARNINGS([maximum])

AC_ARG_ENABLE(deprecation_flags,
              [AS_HELP_STRING([--enable-deprecation-flags],
                              [use *_DISABLE_DEPRECATED flags @<:@default=no@:>@])],,
              [enable_deprecation_flags=no])

if test "x$enable_deprecation_flags" = "xyes"; then
   DISABLE_DEPRECATED_CFLAGS=$DISABLE_DEPRECATED
   AC_SUBST([DISABLE_DEPRECATED_CFLAGS])
fi

GLIB_REQUIRED=2.46.0
GTK3_REQUIRED=3.18.0
DBUS_GLIB_REQUIRED=0.76
UPOWER_REQUIRED=0.9.0
JSON_GLIB_REQUIRED=0.10
GNOME_DESKTOP_REQUIRED=3.18.0

AC_ARG_ENABLE(session-selector, AS_HELP_STRING([--enable-session-selector],
                                               [enable building a custom session selector dialog]),
                                                enable_session_selector=$enableval,enable_session_selector=no)

AM_CONDITIONAL(BUILD_SESSION_SELECTOR,
               [test "$enable_session_selector" = yes])

if test "$enable_session_selector" = yes; then
        PKG_CHECK_MODULES(SESSION_SELECTOR, gtk+-3.0 gio-2.0)
fi

dnl ====================================================================
dnl Dependency Checks
dnl ====================================================================

dnl Standard vertical stacks
PKG_CHECK_MODULES(GIO, gio-2.0)
PKG_CHECK_MODULES(GIOUNIX, gio-unix-2.0 >= $GLIB_REQUIRED)
PKG_CHECK_MODULES(GTK3, gtk+-3.0 >= $GTK3_REQUIRED)

PKG_CHECK_MODULES(GNOME_SESSION,
        glib-2.0 >= $GLIB_REQUIRED
        gio-2.0 >= $GLIB_REQUIRED
        json-glib-1.0 >= $JSON_GLIB_REQUIRED
        gnome-desktop-3.0 >= $GNOME_DESKTOP_REQUIRED
)

dnl We can only support old upower
dnl https://bugzilla.gnome.org/show_bug.cgi?id=710383
PKG_CHECK_MODULES(UPOWER, upower-glib < 0.99.0, have_old_upower=yes, have_old_upower=no)
AS_IF([test x$have_old_upower = xyes], [
  AC_DEFINE([HAVE_OLD_UPOWER], [1], [Define if we have an older upower])
])
AM_CONDITIONAL(HAVE_OLD_UPOWER, test x$have_old_upower = xyes)

PKG_CHECK_MODULES(SESSION_PROPERTIES,
        glib-2.0 >= $GLIB_REQUIRED
        gtk+-3.0 >= $GTK3_REQUIRED
)

PKG_CHECK_MODULES(X11, x11)
PKG_CHECK_MODULES(SM, sm)
PKG_CHECK_MODULES(ICE, ice)
PKG_CHECK_MODULES(XEXT, xext xau)

PKG_CHECK_MODULES(GL_TEST, xcomposite gl glib-2.0)

dnl ====================================================================
dnl Check for gconf
dnl ====================================================================
AC_ARG_ENABLE([gconf],
              AS_HELP_STRING([--enable-gconf], [Support gconf-based autostart]),
              [enable_gconf=$enableval],
              [enable_gconf=auto])

PKG_CHECK_MODULES(GCONF, gconf-2.0, [have_gconf=yes], [have_gconf=no])

if test x$enable_gconf = xauto ; then
        enable_gconf=$have_gconf
elif test x$enable_gconf = xyes -a x$have_gconf = xno ; then
        AC_MSG_ERROR([GConf support explicitly required, but gconf not found])
fi

if test x$enable_gconf = xyes ; then
        AC_DEFINE([HAVE_GCONF], [1], [Define if we support gconf-based autostart])
fi

dnl ====================================================================
dnl Check for systemd
dnl ====================================================================
AC_ARG_ENABLE([systemd],
              AS_HELP_STRING([--enable-systemd], [Use systemd]),
              [enable_systemd=$enableval],
              [enable_systemd=auto])

PKG_CHECK_MODULES(SYSTEMD,
                  [gio-unix-2.0 libsystemd >= 209],
                  [have_systemd=yes],
                  [PKG_CHECK_MODULES([SYSTEMD],
                                     [gio-unix-2.0 libsystemd-login >= 183 libsystemd-daemon libsystemd-journal],
                                     [have_systemd=yes],[have_systemd=no])])

AC_MSG_CHECKING([whether to use systemd])

if test x$enable_systemd = xauto ; then
        if test x$have_systemd = xno ; then
                enable_systemd=no
        else
                enable_systemd=yes
        fi
fi

AC_MSG_RESULT($enable_systemd)

if test x$enable_systemd = xyes; then
        if test x$have_systemd = xno; then
                AC_MSG_ERROR([Systemd support explicitly required, but systemd not found])
        fi
        AC_DEFINE(HAVE_SYSTEMD, 1, [Define if systemd is used for session tracking])
fi

AC_SUBST(SYSTEMD_CFLAGS)
AC_SUBST(SYSTEMD_LIBS)

AM_CONDITIONAL(HAVE_SYSTEMD, [test "$enable_systemd" = "yes"], [Using systemd])

dnl ====================================================================
dnl Check for ConsoleKit
dnl ====================================================================

AC_ARG_ENABLE([consolekit],
              AS_HELP_STRING([--enable-consolekit], [Use consolekit]),
              [enable_consolekit=$enableval],
              [enable_consolekit=no])

AC_MSG_CHECKING([whether to use consolekit])
AC_MSG_RESULT($enable_consolekit)

if test x$enable_consolekit = xyes; then
        PKG_CHECK_MODULES(CONSOLEKIT, [dbus-glib-1 >= $DBUS_GLIB_REQUIRED],
                          [have_consolekit=yes], [have_consolekit=no])
        if test x$have_consolekit = xno; then
                AC_MSG_ERROR([ConsoleKit support explicitly required, but dbus-glib not found])
        fi
        AC_DEFINE(HAVE_CONSOLEKIT, 1, [Define if ConsoleKit is used for session tracking])
fi

AC_SUBST(CONSOLEKIT_CFLAGS)
AC_SUBST(CONSOLEKIT_LIBS)

AM_CONDITIONAL(HAVE_CONSOLEKIT, [test "$enable_consolekit" = "yes"], [Using ConsoleKit])

if test x$enable_systemd = xyes; then
        if test x$enable_consolekit = xyes; then
                session_tracking="systemd (with fallback to ConsoleKit)"
        else
                session_tracking="systemd"
        fi
else
        if test x$enable_consolekit = xyes; then
                session_tracking="ConsoleKit"
        else
                session_tracking="null backend"
        fi
fi

dnl ====================================================================
dnl Check for XSync extension
dnl ====================================================================

have_xsync=no
AC_CHECK_HEADER(X11/extensions/sync.h, [have_xsync=yes],,
                    [#include <X11/Xlib.h>])
if test "$have_xsync" = yes; then
  AC_DEFINE(HAVE_XSYNC, 1, [Have the SYNC extension library])
fi

dnl ====================================================================
dnl Check for XTest extension
dnl ====================================================================

have_xtest=no
PKG_CHECK_MODULES(XTEST, xtst, have_xtest=yes, have_xtest=no)
if test "$have_xtest" = yes; then
    AC_DEFINE(HAVE_XTEST, 1, [Have the XTest extension library])
fi
AC_SUBST(HAVE_XTEST)
AC_SUBST(XTEST_CFLAGS)
AC_SUBST(XTEST_LIBS)

dnl ====================================================================
dnl XRender checks
dnl ====================================================================

PKG_CHECK_MODULES(XRENDER, xrender, have_xrender=yes, have_xrender=no)
AM_CONDITIONAL(HAVE_XRENDER, test x$have_xrender = xyes)
if test $have_xrender=yes; then
    AC_DEFINE(HAVE_XRENDER, 1, [Have the Render X extension])
fi
AC_SUBST(HAVE_XRENDER)
AC_SUBST(XRENDER_CFLAGS)
AC_SUBST(XRENDER_LIBS)

dnl ====================================================================
dnl - DocBook Documentation
dnl ====================================================================

AC_ARG_ENABLE(docbook-docs,
              [AS_HELP_STRING([--enable-docbook-docs],
                              [build documentation (requires xmlto)])],
              [enable_docbook_docs=$enableval],
              [enable_docbook_docs=auto])
AC_PATH_PROG(XMLTO, xmlto, no)
AC_MSG_CHECKING([whether to build DocBook documentation])
if test x$XMLTO = xno ; then
	have_docbook=no
else
	have_docbook=yes
fi
if test x$enable_docbook_docs = xauto ; then
	if test x$have_docbook = xno ; then
        	enable_docbook_docs=no
	else
		enable_docbook_docs=yes
	fi
fi
if test x$enable_docbook_docs = xyes; then
	if test x$have_docbook = xno; then
		AC_MSG_ERROR([Building DocBook docs explicitly required, but DocBook not found])
	fi
fi
AM_CONDITIONAL(DOCBOOK_DOCS_ENABLED, test x$enable_docbook_docs = xyes)
AC_MSG_RESULT($enable_docbook_docs)
AC_ARG_ENABLE(man,
              [AS_HELP_STRING([--enable-man],
                              [generate man pages [default=yes]])],,
              enable_man=yes)
if test "$enable_man" != no; then
  AC_PATH_PROG([XSLTPROC], [xsltproc])
  if test -z "$XSLTPROC"; then
    AC_MSG_ERROR([xsltproc is required for --enable-man])
  fi
fi
AM_CONDITIONAL(ENABLE_MAN, test "$enable_man" != no)

dnl ====================================================================
dnl Check for xsltproc
dnl ====================================================================
AC_PATH_PROG([XSLTPROC], [xsltproc])

dnl ====================================================================
dnl Language Support
dnl ====================================================================
IT_PROG_INTLTOOL([0.40.6])

GETTEXT_PACKAGE=gnome-session-3.0
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE",
                   [The gettext translation domain])
AC_SUBST(GETTEXT_PACKAGE)

AM_GLIB_GNU_GETTEXT
GLIB_GSETTINGS

dnl ====================================================================
dnl Headers
dnl ====================================================================
AC_HEADER_STDC
AC_CHECK_HEADERS(syslog.h tcpd.h sys/param.h)

dnl ====================================================================
dnl check for backtrace support
dnl ====================================================================

AC_CHECK_HEADERS(execinfo.h)
AC_CHECK_LIB(execinfo, backtrace, [have_backtrace="yes"], [have_backtrace="no"])
EXECINFO_LIBS=""
if test "x$have_backtrace" = "xyes"; then
        EXECINFO_LIBS="-lexecinfo"
fi
AC_SUBST(EXECINFO_LIBS)

dnl ====================================================================
dnl Check for X transport interface - allows to disable ICE Transports
dnl See also https://bugzilla.gnome.org/show_bug.cgi?id=725100
dnl ====================================================================
AC_ARG_WITH([xtrans],
	    [AS_HELP_STRING([--without-xtrans],
			   [Build without xtrans support (results in ICE listening on remote TCP ports)])],
	    [],
	    [with_xtrans=yes])
AS_IF([test "$with_xtrans" != "no"],
      [PKG_CHECK_MODULES(XTRANS, xtrans,
      [AC_DEFINE(HAVE_XTRANS, 1, [Have the X Transport library])])])
AC_SUBST(HAVE_XTRANS)

dnl ====================================================================
dnl Code for checking whether IPv6 is enabled on the system....
dnl ====================================================================
AC_MSG_CHECKING([whether to enable ipv6])
AC_ARG_ENABLE(ipv6,
              [AS_HELP_STRING([--enable-ipv6],
                              [enable IPv6 extensions])],,
              [enable_ipv6=yes])
have_full_ipv6=no
if test $enable_ipv6 = yes; then

  dnl ====================================================================
  dnl Code for checking presence of AF_INET6 on the system....
  dnl ====================================================================
  AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[
    #include <sys/types.h>
    #include <sys/socket.h>]], [[
    socket(AF_INET6, SOCK_STREAM, 0)
    ]])],
    [have_ipv6=yes],
    [have_ipv6=no]
  )
  AC_MSG_RESULT($have_ipv6)

  dnl =================================================================
  dnl Now we would check for specific function like getaddrinfo.
  dnl =================================================================
  have_getaddrinfo=no
  if test $have_ipv6=yes; then
    AC_CHECK_FUNC(getaddrinfo, have_getaddrinfo=yes)
    if test $have_getaddrinfo != yes; then
      # getaddrinfo is not in the default libraries.  See if it's in some other.
      for lib in bsd socket inet; do
        AC_CHECK_LIB($lib, getaddrinfo, [LIBS="$LIBS -l$lib";have_getaddrinfo=yes; break])
      done
    fi
    if test $have_getaddrinfo=yes; then
      AC_DEFINE(ENABLE_IPV6, 1, [Define if IPV6 is supported])
      have_full_ipv6=yes
    fi
  fi
fi
dnl ==============================================================================
dnl End of IPv6 checks
dnl ==============================================================================

AC_CONFIG_FILES([
Makefile
doc/Makefile
doc/dbus/Makefile
doc/dbus/gnome-session.xml
doc/man/Makefile
data/Makefile
data/org.gnome.SessionManager.gschema.xml
data/icons/Makefile
data/icons/16x16/Makefile
data/icons/22x22/Makefile
data/icons/24x24/Makefile
data/icons/32x32/Makefile
data/icons/48x48/Makefile
data/icons/scalable/Makefile
data/icons/symbolic/Makefile
gnome-session/Makefile
tools/Makefile
po/Makefile.in
])
AC_OUTPUT

dnl ---------------------------------------------------------------------------
dnl - Show summary
dnl ---------------------------------------------------------------------------

echo "
              gnome-session $VERSION
              `echo gnome-session $VERSION | sed "s/./=/g"`

        prefix:                   ${prefix}
        exec_prefix:              ${exec_prefix}
        libdir:                   ${libdir}
        bindir:                   ${bindir}
        sbindir:                  ${sbindir}
        sysconfdir:               ${sysconfdir}
        localstatedir:            ${localstatedir}
        datadir:                  ${datadir}
        source code location:     ${srcdir}
        compiler:                 ${CC}
        cflags:                   ${CFLAGS}
        Maintainer mode:          ${USE_MAINTAINER_MODE}
        Use *_DISABLE_DEPRECATED: ${enable_deprecation_flags}

        GConf support:            ${enable_gconf}
        Session tracking:         ${session_tracking}
        IPv6 support:             ${have_full_ipv6}
        Backtrace support:        ${have_backtrace}
        XRender support:          ${have_xrender}
        XSync support:            ${have_xsync}
        XTest support:            ${have_xtest}
	Legacy UPower backend:    ${have_old_upower}
        Build Docbook:            ${enable_docbook_docs}
        Build manpages:           ${enable_man}

"
