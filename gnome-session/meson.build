script_conf = configuration_data()
script_conf.set('libexecdir', session_libexecdir)

script = 'gnome-session'

configure_file(
  input: script + '.in',
  output: script,
  install: true,
  install_dir: session_bindir,
  configuration: script_conf
)

libgsmutil = static_library(
  'gsmutil',
  sources: 'gsm-util.c',
  include_directories: top_inc,
  dependencies: session_deps
)

sources = files(
  'gsm-app.c',
  'gsm-autostart-app.c',
  'gsm-client.c',
  'gsm-dbus-client.c',
  'gsm-fail-whale.c',
  'gsm-inhibitor.c',
  'gdm-log.c',
  'gsm-manager.c',
  'gsm-presence.c',
  'gsm-process-helper.c',
  'gsm-session-fill.c',
  'gsm-session-save.c',
  'gsm-shell-extensions.c',
  'gsm-shell.c',
  'gsm-store.c',
  'gsm-system.c',
  'gsm-systemd.c',
  'main.c'
)

if have_x11
  sources += [
    'gsm-xsmp-client.c',
    'gsm-xsmp-server.c',
  ]
endif

dbus_ifaces = [
  ['org.gnome.SessionManager', 'ExportedManager'],
  ['org.gnome.SessionManager.Client', 'ExportedClient'],
  ['org.gnome.SessionManager.ClientPrivate', 'ExportedClientPrivate'],
  ['org.gnome.SessionManager.App', 'ExportedApp'],
  ['org.gnome.SessionManager.Inhibitor', 'ExportedInhibitor'],
  ['org.gnome.SessionManager.Presence', 'ExportedPresence']
]

foreach iface: dbus_ifaces
  sources += gnome.gdbus_codegen(
    iface[0],
    iface[0] + '.xml',
    interface_prefix: iface[0] + '.',
    namespace: 'Gsm',
    annotations: [iface[0], 'org.gtk.GDBus.C.Name', iface[1]]
  )
endforeach

cflags = [
  '-DLOCALE_DIR="@0@"'.format(session_localedir),
  '-DDATA_DIR="@0@"'.format(session_pkgdatadir),
  '-DLIBEXECDIR="@0@"'.format(session_libexecdir)
]

executable(
  meson.project_name() + '-binary',
  sources,
  include_directories: top_inc,
  dependencies: session_bin_deps,
  c_args: cflags,
  link_with: libgsmutil,
  install: true,
  install_dir: session_libexecdir
)

sources = files('gsm-fail-whale-dialog.c')

cflags = '-DLOCALE_DIR="@0@"'.format(session_localedir)

executable(
  meson.project_name() + '-failed',
  sources,
  include_directories: top_inc,
  dependencies: gtk_dep,
  c_args: cflags,
  install: true,
  install_dir: session_libexecdir
)

units = [
  ['test-client-dbus', [], [gio_dep]],
  ['test-process-helper', files('gsm-process-helper.c'), [gio_dep]]
]

foreach unit: units
  executable(
    unit[0],
    [unit[0] + '.c'] + unit[1],
    include_directories: top_inc,
    dependencies: unit[2]
  )
endforeach
