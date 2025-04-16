xml_man_pages = []
if get_option('man')
  subdir('man')
endif

if not get_option('docbook')
  subdir_done()
endif

xmlto_searchpath = [
  meson.current_build_dir(),
  meson.current_build_dir() / '..' / 'gnome-session',
]

xmlto_command = [
  find_program('xmlto'),
  '-o', '@OUTPUT@',
  '--searchpath', ':'.join(xmlto_searchpath),
  '-m', files('config.xsl'),
  'html',
  '@INPUT@'
]

xml_version = configure_file(
  input: 'version.xml.in',
  output: 'version.xml',
  configuration: {
    'VERSION': meson.project_version(),
  },
)

custom_target(
  'gnome-session-docs',
  input: 'gnome-session.xml',
  output: 'html',
  command: xmlto_command,
  depend_files: xml_man_pages + xml_version,
  depends: xml_dbus_docs,
  install: true,
  install_dir: session_datadir / 'doc' / meson.project_name()
)