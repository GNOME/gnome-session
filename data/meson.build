desktop_plain = 'gnome'
desktop_xorg = 'gnome-xorg'

desktops = [
  desktop_plain,
  desktop_xorg
]

foreach name: desktops
  desktop_conf = configuration_data()
  desktop_conf.set('bindir', session_bindir)

  desktop = name + '.desktop'

  desktop_in = configure_file(
    input: desktop + '.in.in',
    output: desktop + '.in',
    configuration: desktop_conf
  )

  install_dir = join_paths(session_datadir, 'xsessions')
  # FIXME: The same target can not be copied into two directories.
  #        There is a workaround in meson_post_install.py until proper solution arises:
  #        https://groups.google.com/forum/#!topic/mesonbuild/3iIoYPrN4P0
  if name == desktop_plain
    #install_dir: [
    #  join_paths(session_datadir, 'xsessions'),
    #  join_paths(session_datadir, 'wayland-sessions')
    #]
  endif

  desktop_target = i18n.merge_file(
    desktop,
    type: 'desktop',
    input: desktop_in,
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: install_dir
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

data = files('hardware-compatibility')

if enable_session_selector
  data += files('session-selector.ui')

  desktop = 'gnome-custom-session.desktop'

  i18n.merge_file(
    desktop,
    type: 'desktop',
    input: desktop + '.in',
    output: desktop,
    po_dir: po_dir,
    install: true,
    install_dir: join_paths(session_datadir, 'xsessions')
  )
endif

install_data(
  data,
  install_dir: session_pkgdatadir
)
