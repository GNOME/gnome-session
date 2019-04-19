desktop_plain = 'gnome'

desktops = [
  desktop_plain,
  'gnome-xorg'
]

if enable_session_selector
  desktops += 'gnome-custom-session'
endif

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

unit_conf = configuration_data()
unit_conf.set('libexecdir', session_libexecdir)

# XXX: Once https://github.com/systemd/systemd/issues/9595 is fixed and we can
# depend on this version, replace with something like:
#  dep_systemd = dependency('systemd', required: true)
#  systemd_userunitdir = dep_systemd.get_pkgconfig_variable('systemduserunitdir')
# and uncomment systemd_dep below
systemd_userunitdir = join_paths(session_prefix, 'lib', 'systemd', 'user')

systemd_service = ['gnome-session-manager@.service',
                   'gnome-session-ctl@.service',
                   'gnome-session-failed.service']

foreach service: systemd_service
  configure_file(
    input: service + '.in',
    output: service,
    install: true,
    install_dir: systemd_userunitdir,
    configuration: unit_conf
  )
endforeach

configure_file(
  input: 'gnome-session-ctl@.service.in',
  output: 'gnome-session-ctl@.service',
  install: true,
  install_dir: systemd_userunitdir,
  configuration: unit_conf
)

systemd_target = files('gnome-session@.target',
                       'gnome-session.target',
                       'gnome-session-pre.target',
                       'gnome-session-manager.target',
                       'gnome-session-initialized.target',
                       'gnome-session-shutdown.target',
                       'gnome-session-failed.target',
                       )

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
