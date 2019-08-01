# We'd like to have gnome-wayland have output: 'gnome.desktop', but this
# doesn't work https://github.com/mesonbuild/meson/issues/3759 So this is later
# renamed in meson_post_install.py
desktops = [
  ['gnome',        'xsessions'],
  ['gnome-xorg',   'xsessions'],
  ['gnome-wayland', 'wayland-sessions']
]

if enable_session_selector
  desktops += ['gnome-custom-session', 'xsessions']
endif

foreach name: desktops
  desktop_conf = configuration_data()
  desktop_conf.set('bindir', session_bindir)

  desktop = name[0] + '.desktop'
  dir = name[1]

  desktop_in = configure_file(
    input: desktop + '.in.in',
    output: desktop + '.in',
    configuration: desktop_conf
  )

  i18n.merge_file(
    desktop,
    type: 'desktop',
    input: desktop_in,
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: join_paths(session_datadir, dir)
  )
endforeach

sessions = [
  'gnome.session',
  'gnome-dummy.session'
]

foreach session: sessions
  desktop_conf = configuration_data()
  desktop_conf.set('libexecdir', session_libexecdir)

  desktop = session + '.desktop'

  desktop_in = configure_file(
    input: desktop + '.in.in',
    output: desktop + '.in',
    configuration: desktop_conf
  )

  i18n.merge_file(
    session,
    type: 'desktop',
    input: desktop_in,
    output: session,
    po_dir: po_dir,
    install: true,
    install_dir: join_paths(session_pkgdatadir, 'sessions')
  )
endforeach

schema_conf = configuration_data()

schema = 'org.gnome.SessionManager.gschema.xml'

configure_file(
  input: schema + '.in',
  output: schema,
  install: true,
  install_dir: join_paths(session_datadir, 'glib-2.0', 'schemas'),
  configuration: schema_conf
)

install_data(
  'gnome-session.convert',
  install_dir: join_paths(session_datadir, 'GConf', 'gsettings')
)

unit_conf = configuration_data()
unit_conf.set('libexecdir', session_libexecdir)

dep_systemd = dependency('systemd', required: true)
systemd_userunitdir = dep_systemd.get_pkgconfig_variable('systemduserunitdir')

configure_file(
  input: 'gnome-session@.service.in',
  output: 'gnome-session@.service',
  install: true,
  install_dir: systemd_userunitdir,
  configuration: unit_conf
)

systemd_target = files('gnome-session-bus.target',
                       'gnome-session-wayland.target',
                       'gnome-session-x11.target')

install_data(
  systemd_target,
  install_dir: systemd_userunitdir
)

data = files('hardware-compatibility')

if enable_session_selector
  data += files('session-selector.ui')
endif

install_data(
  data,
  install_dir: session_pkgdatadir
)
