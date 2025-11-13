# Install the .desktop file that tells GDM about the GNOME session

i18n.merge_file(
  type: 'desktop',
  input: configure_file(
    input: 'gnome.desktop.in.in',
    output: '@BASENAME@',
    configuration: {
      'bindir': session_bindir,
    },
  ),
  output: '@BASENAME@',
  po_dir: po_dir,
  install: true,
  install_dir: session_datadir / 'wayland-sessions',
)

# Next, let's install the necessary systemd scaffolding

install_data(
    'gnome.session.conf',
    install_dir: systemd_userunitdir / 'gnome-session@gnome.target.d',
)

systemd_service = [
  'gnome-session-manager@.service',
  'gnome-session-signal-init.service',
  'gnome-session-restart-dbus.service',
  'gnome-session-monitor.service',
]

foreach service: systemd_service
  configure_file(
    input: service + '.in',
    output: service,
    install: true,
    install_dir: systemd_userunitdir,
    configuration: {
      'libexecdir': session_libexecdir,
    },
  )
endforeach

systemd_target = [
  'gnome-session@.target',
  'gnome-session.target',
  'gnome-session-basic-services.target',
  'gnome-session-services.target',
  'gnome-session-pre.target',
  'gnome-session-manager.target',
  'gnome-session-initialized.target',
  'gnome-session-shutdown.target',
  'gnome-session-x11-services.target',
  'gnome-session-x11-services-ready.target',
]

install_data(
  systemd_target,
  install_dir: systemd_userunitdir
)

# Install resource limits that are applied to GNOME-launched apps

install_data(
  'app-override.scope.conf',
  rename: 'override.conf',
  install_dir : join_paths(systemd_userunitdir, 'app-gnome-.scope.d')
)

# FIXME: https://github.com/systemd/systemd/issues/37104
install_data(
  'app-override.scope.conf',
  rename: 'override.conf',
  install_dir : join_paths(systemd_userunitdir, 'app-flatpak-.scope.d')
)

# Install some other misc. configuration files

i18n.merge_file(
  type: 'desktop',
  input: 'gnome.session.desktop',
  output: 'gnome.session',
  po_dir: po_dir,
  install: true,
  install_dir: join_paths(session_pkgdatadir, 'sessions')
)

install_data(
  'org.gnome.SessionManager.gschema.xml',
  install_dir: join_paths(session_datadir, 'glib-2.0', 'schemas'),
)

install_data(
  'gnome-portals.conf',
  install_dir: session_datadir / 'xdg-desktop-portal',
)

if get_option('mimeapps')
  install_data(
    'gnome-mimeapps.list',
    install_dir: session_datadir / 'applications',
  )
endif
