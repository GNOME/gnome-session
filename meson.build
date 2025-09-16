project(
  'gnome-session', 'c',
  version: '49.0',
  license: 'GPL2+',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.60.0'
)

session_version = meson.project_version()
session_api_name = '@0@-@1@'.format(meson.project_name(), session_version.split('.')[0])

session_prefix = get_option('prefix')
session_bindir = join_paths(session_prefix, get_option('bindir'))
session_datadir = join_paths(session_prefix, get_option('datadir'))
session_libexecdir = join_paths(session_prefix, get_option('libexecdir'))
session_localedir = join_paths(session_prefix, get_option('localedir'))
session_sysconfdir = join_paths(session_prefix, get_option('sysconfdir'))

session_pkgdatadir = join_paths(session_datadir, meson.project_name())

cc = meson.get_compiler('c')

have_x11 = get_option('x11')
config_h = configuration_data()

# defines
set_defines = [
  # package
  ['PACKAGE', meson.project_name()],
  ['PACKAGE_VERSION', session_version],
  ['VERSION', session_version],
  # i18n
  ['GETTEXT_PACKAGE', session_api_name]
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# compiler flags
common_flags = ['-DHAVE_CONFIG_H']

if get_option('deprecation_flags')
  common_flags += [
    '-DBONOBO_DISABLE_DEPRECATED',
    '-DBONOBO_DISABLE_SINGLE_INCLUDES',
    '-DBONOBO_UI_DISABLE_DEPRECATED',
    '-DBONOBO_UI_DISABLE_SINGLE_INCLUDES',
    '-DGNOME_DISABLE_DEPRECATED',
    '-DGNOME_DISABLE_SINGLE_INCLUDES',
    '-DLIBGLADE_DISABLE_DEPRECATED',
    '-DLIBGLADE_DISABLE_SINGLE_INCLUDES',
    '-DGNOME_VFS_DISABLE_DEPRECATED',
    '-DGNOME_VFS_DISABLE_SINGLE_INCLUDES',
    '-DWNCK_DISABLE_DEPRECATED',
    '-DWNCK_DISABLE_SINGLE_INCLUDES',
    '-DLIBSOUP_DISABLE_DEPRECATED',
    '-DLIBSOUP_DISABLE_SINGLE_INCLUDES'
  ]
endif

compiler_flags = []
if get_option('debug')
  test_cflags = [
    '-Werror=format=2',
    '-Werror=implicit-function-declaration',
    '-Werror=init-self',
    '-Werror=missing-include-dirs',
    '-Werror=missing-prototypes',
    '-Werror=pointer-arith',
    '-Werror=return-type',
    '-Wnested-externs',
    '-Wstrict-prototypes'
  ]

  compiler_flags += cc.get_supported_arguments(test_cflags)
endif

add_project_arguments(common_flags + compiler_flags, language: 'c')

glib_req_version = '>= 2.82.0'

gio_dep = dependency('gio-2.0', version: glib_req_version)
gtk_dep = dependency('gtk4')

session_deps = [
  gio_dep,
  dependency('glib-2.0', version: glib_req_version),
  dependency('gnome-desktop-4'),
  dependency('json-glib-1.0', version: '>= 0.10')
]

libsystemd_dep = dependency('libsystemd', version: '>= 209', required: true)
config_h.set('SYSTEMD_STRICT_ENV',
              libsystemd_dep.version().version_compare('< 248'))

session_bin_deps = session_deps + [
  libsystemd_dep,
  dependency('gio-unix-2.0', version: glib_req_version),
]

systemd_userunitdir = get_option('systemduserunitdir')
if systemd_userunitdir == ''
  systemd_dep = dependency('systemd', version: '>= 242', required: true)
  systemd_userunitdir = systemd_dep.get_variable(pkgconfig: 'systemduserunitdir',
                                                 pkgconfig_define: ['prefix', session_prefix])
endif

configure_file(
  output: 'config.h',
  configuration: config_h
)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

po_dir = join_paths(meson.project_source_root(), 'po')

top_inc = include_directories('.')

subdir('gnome-session')
subdir('tools')
subdir('data')
subdir('doc')
subdir('po')

if have_x11
  meson.add_install_script(
    'meson_post_install.py',
    session_datadir
  )
endif

gnome.post_install(
  glib_compile_schemas: true,
)

summary_options = {
 'Debug mode': get_option('debug'),
 'Use *_DISABLE_DEPRECATED': get_option('deprecation_flags'),
 'Build Docbook': get_option('docbook'),
 'Build manpages': get_option('man'),
 'Systemd Units Directory': systemd_userunitdir,
}

summary_dirs = {
  'prefix': get_option('prefix'),
  'datadir': session_datadir,
  'bindir': session_bindir,
  'libexecdir': session_libexecdir,
  'localedir': session_localedir,
  'mandir': get_option('mandir'),
  'sysconfdir': session_sysconfdir,
  'pkgdatadir': session_pkgdatadir,
}

summary(summary_dirs, section: 'Directories')
summary(summary_options, section: 'Build Options')
