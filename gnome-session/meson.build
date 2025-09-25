sources = files(
  'gsm-util.c',
  'leader-main.c'
)

cflags = [
  '-DLOCALE_DIR="@0@"'.format(session_localedir),
  '-DDATA_DIR="@0@"'.format(session_pkgdatadir),
  '-DLIBEXECDIR="@0@"'.format(session_libexecdir)
]

executable(
  meson.project_name(),
  sources,
  include_directories: top_inc,
  dependencies: session_bin_deps,
  c_args: cflags,
  install: true,
  install_dir: session_bindir
)

sources = files(
    'gsm-util.c',
    'leader-systemd.c',
)

executable(
  meson.project_name() + '-init-worker',
  sources,
  include_directories: top_inc,
  dependencies: session_bin_deps,
  c_args: cflags,
  install: true,
  install_dir: session_libexecdir
)

sources = files(
  'gsm-app.c',
  'gsm-client.c',
  'gsm-inhibitor.c',
  'gsm-manager.c',
  'gsm-presence.c',
  'gsm-session-fill.c',
  'gsm-session-save.c',
  'gsm-shell.c',
  'gsm-store.c',
  'gsm-system.c',
  'gsm-systemd.c',
  'gsm-util.c',
  'gsm-xdp-session.c',
  'service-main.c'
)

dbus_ifaces = [
  'org.gnome.SessionManager',
  'org.gnome.SessionManager.ClientPrivate',
  'org.gnome.SessionManager.Inhibitor',
  'org.gnome.SessionManager.Presence',
]

xml_dbus_docs = []

foreach iface: dbus_ifaces
  out = gnome.gdbus_codegen(
    iface,
    iface + '.xml',
    interface_prefix: iface + '.',
    namespace: 'Gsm',
    docbook: 'ref'
  )
  sources += [out[0], out[1]] # .c/.h sources
  xml_dbus_docs += out[2] # docbook XML
endforeach

portal_ifaces = [
	'org.freedesktop.impl.portal.Session',
]

foreach iface: portal_ifaces
  sources += gnome.gdbus_codegen(
    iface,
    xdg_desktop_portal_interfaces_dir / '@0@.xml'.format(iface),
    interface_prefix: 'org.freedesktop.impl.portal.',
    namespace: 'XdpImpl',
  )
endforeach

executable(
  meson.project_name() + '-service',
  sources,
  include_directories: top_inc,
  dependencies: session_bin_deps,
  c_args: cflags,
  install: true,
  install_dir: session_libexecdir
)

units = [
  ['test-client-dbus', [], [gio_dep]],
]

foreach unit: units
  executable(
    unit[0],
    [unit[0] + '.c'] + unit[1],
    include_directories: top_inc,
    dependencies: unit[2]
  )
endforeach
