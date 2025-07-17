# Install the .desktop files that tell GDM about the GNOME session

wayland_desktops = [
  'gnome',
  'gnome-wayland',
]
foreach name : wayland_desktops
  desktop = name + '.desktop'
  desktop_in = configure_file(
    input: desktop + '.in.in',
    output: desktop + '.in',
    configuration: {
      'bindir': session_bindir,
      'canrunheadless': 'true',
    },
  )
  desktop_target = i18n.merge_file(
    type: 'desktop',
    input: desktop_in,
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: session_datadir / 'wayland-sessions',
  )
endforeach

if have_x11
  xorg_desktops = [
    'gnome',
    'gnome-xorg',
  ]
  foreach name : xorg_desktops
    desktop = name + '.desktop'

    # 'gnome' target name is already used for wayland desktop,
    # use 'gnome-copy' and on meson_post_install
    # modify its name to 'gnome' again
    if name == 'gnome'
      desktop_out = 'gnome-copy.desktop'
    else
      desktop_out = desktop
    endif

    desktop_in = configure_file(
      input: desktop + '.in.in',
      output: desktop_out + '.in',
      configuration: {
        'bindir': session_bindir,
        'canrunheadless': 'false',
      },
    )
    desktop_target = i18n.merge_file(
      type: 'desktop',
      input: desktop_in,
      output: desktop_out,
      po_dir: po_dir,
      install: true,
      install_dir: session_datadir / 'xsessions',
    )
  endforeach
endif

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
  'gnome-session-wayland@.target',
  'gnome-session-wayland.target',
  'gnome-session@.target',
  'gnome-session.target',
  'gnome-session-basic-services.target',
  'gnome-session-pre.target',
  'gnome-session-manager.target',
  'gnome-session-initialized.target',
  'gnome-session-shutdown.target',
  'gnome-session-x11-services.target',
  'gnome-session-x11-services-ready.target',
]
if have_x11
  systemd_target += [
    'gnome-session-x11@.target',
    'gnome-session-x11.target',
  ]
endif

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
