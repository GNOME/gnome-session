project(
  'gnome-session', 'c',
  version: '3.26.0',
  license: 'GPL2+',
  default_options: 'buildtype=debugoptimized',
  meson_version: '>= 0.43.0'
)

session_version = meson.project_version()
version_array = session_version.split('.')
session_major_version = version_array[0].to_int()
session_minor_version = version_array[1].to_int()
session_micro_version = version_array[2].to_int()

session_api_version = '@0@.0'.format(session_major_version)
session_api_name = '@0@-@1@'.format(meson.project_name(), session_api_version)

session_prefix = get_option('prefix')
session_bindir = join_paths(session_prefix, get_option('bindir'))
session_datadir = join_paths(session_prefix, get_option('datadir'))
session_libdir = join_paths(session_prefix, get_option('libdir'))
session_libexecdir = join_paths(session_prefix, get_option('libexecdir'))
session_localedir = join_paths(session_prefix, get_option('localedir'))
session_localstatedir = join_paths(session_prefix, get_option('localstatedir'))
session_mandir = join_paths(session_prefix, get_option('mandir'))
session_sbindir = join_paths(session_prefix, get_option('sbindir'))
session_sysconfdir = join_paths(session_prefix, get_option('sysconfdir'))

session_pkgdatadir = join_paths(session_datadir, meson.project_name())

session_debug = get_option('buildtype').contains('debug')

cc = meson.get_compiler('c')

config_h = configuration_data()

# defines
set_defines = [
  # package
  ['PACKAGE', meson.project_name()],
  ['PACKAGE_BUGREPORT', 'http://bugzilla.gnome.org/enter_bug.cgi?product=' + meson.project_name()],
  ['PACKAGE_NAME', meson.project_name()],
  ['PACKAGE_STRING', '@0@ @1@'.format(meson.project_name(), session_version)],
  ['PACKAGE_TARNAME', meson.project_name()],
  ['PACKAGE_URL', ''],
  ['PACKAGE_VERSION', session_version],
  ['VERSION', session_version],
  # i18n
  ['GETTEXT_PACKAGE', session_api_name]
]

foreach define: set_defines
  config_h.set_quoted(define[0], define[1])
endforeach

# headers
check_headers = [
  ['HAVE_DLFCN_H', 'dlfcn.h'],
  ['HAVE_INTTYPES_H', 'inttypes.h'],
  ['HAVE_LOCALE_H', 'locale.h'],
  ['HAVE_MEMORY_H', 'memory.h'],
  ['HAVE_STDINT_H', 'stdint.h'],
  ['HAVE_STDLIB_H', 'stdlib.h'],
  ['HAVE_STRINGS_H', 'strings.h'],
  ['HAVE_STRING_H', 'string.h'],
  ['HAVE_SYS_PARAM_H', 'sys/param.h'],
  ['HAVE_SYS_STAT_H', 'sys/stat.h'],
  ['HAVE_SYS_TYPES_H', 'sys/types.h'],
  ['HAVE_SYSLOG_H', 'syslog.h'],
  ['HAVE_UNISTD_H', 'unistd.h']
]

foreach header: check_headers
  config_h.set(header[0], cc.has_header(header[1]))
endforeach

# functions
check_functions = [
  # i18n
  ['HAVE_DCGETTEXT', 'dcgettext'],
  ['HAVE_GETTEXT', 'gettext'],
  ['HAVE_ICONV', 'iconv']
]

if host_machine.system().contains('darwin')
  check_functions += [
    ['HAVE_CFLOCALECOPYCURRENT', 'CFLocaleCopyCurrent'],
    ['HAVE_CFPREFERENCESCOPYAPPVALUE', 'CFPreferencesCopyAppValue']
  ]
endif

foreach func: check_functions
  config_h.set(func[0], cc.has_function(func[1]))
endforeach

# symbols
check_symbols = [
  # i18n
  ['HAVE_LC_MESSAGES', 'locale.h', 'LC_MESSAGES'],
  ['HAVE_BIND_TEXTDOMAIN_CODESET', 'libintl.h', 'bind_textdomain_codeset']
]

# compiler flags
common_flags = ['-DHAVE_CONFIG_H']

enable_deprecation_flags = get_option('deprecation_flags')
if enable_deprecation_flags
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
if session_debug
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

  foreach cflag: test_cflags
    if cc.has_argument(cflag)
      compiler_flags += [cflag]
    endif
  endforeach
endif

add_project_arguments(common_flags + compiler_flags, language: 'c')

glib_req_version = '>= 2.46.0'

gio_dep = dependency('gio-2.0', version: glib_req_version)
glib_dep = dependency('glib-2.0', version: glib_req_version)
gtk_dep = dependency('gtk+-3.0', version: '>= 3.18.0')
ice_dep = dependency('ice')
sm_dep = dependency('sm')
x11_dep = dependency('x11')

session_deps = [
  gio_dep,
  glib_dep,
  dependency('gnome-desktop-3.0', version: '>= 3.18.0'),
  dependency('json-glib-1.0', version: '>= 0.10')
]

session_bin_deps = session_deps + [
  ice_dep,
  sm_dep
]

enable_session_selector = get_option('session_selector')

session_tracking = 'null backend'

enable_systemd = get_option('systemd')
enable_consolekit = get_option('consolekit')
if enable_systemd or enable_consolekit
  session_bin_deps += dependency('gio-unix-2.0', version: glib_req_version)

  # Check for systemd
  if enable_systemd
    libsystemd_dep = dependency('libsystemd', version: '>= 209', required: false)
    session_bin_deps += libsystemd_dep

    if not libsystemd_dep.found()
      libsystemd_login_dep = dependency('libsystemd-login', version: '>= 183', required: false)
      libsystemd_daemon_dep = dependency('libsystemd-daemon', required: false)
      libsystemd_journal_dep = dependency('libsystemd-journal', required: false)
      assert(libsystemd_login_dep.found() and libsystemd_daemon_dep.found() and libsystemd_journal_dep.found(),
             'Systemd support explicitly required, but systemd not found')

      session_bin_deps += [
        libsystemd_login_dep,
        libsystemd_daemon_dep,
        libsystemd_journal_dep
      ]
    endif

    session_tracking = 'systemd'
  endif

  # Check for ConsoleKit
  if enable_consolekit
    dbus_glib_dep = dependency('dbus-glib-1', version: '>= 0.76')
    assert(dbus_glib_dep.found(), 'ConsoleKit support explicitly required, but dbus-glib not found')

    session_bin_deps += dbus_glib_dep

    if enable_systemd
      session_tracking += ' (with fallback to ConsoleKit)'
    else
      session_tracking = 'ConsoleKit'
    endif
  endif
endif
config_h.set('HAVE_SYSTEMD', enable_systemd)
config_h.set('HAVE_CONSOLEKIT', enable_consolekit)

configure_file(
  output: 'config.h',
  configuration: config_h
)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

po_dir = join_paths(meson.source_root(), 'po')

intltool_merge = find_program('intltool-merge')
intltool_cache = join_paths(po_dir, '.intltool-merge-cache')
intltool_desktop_cmd = [intltool_merge, '-d', '-u', '-c', intltool_cache, po_dir, '@INPUT@', '@OUTPUT@']

top_inc = include_directories('.')

subdir('gnome-session')
subdir('tools')
subdir('data')

enable_docbook = get_option('docbook')
enable_man = get_option('man')
if enable_docbook or enable_man
  subdir('doc')
endif

subdir('po')

meson.add_install_script(
  'meson_post_install.py',
  session_datadir
)

output = '\n                gnome-session ' + session_version + '\n'
output += '                ====================\n\n'
output += '        prefix:                   ' + session_prefix + '\n'
output += '        exec_prefix:              ' + session_libexecdir + '\n'
output += '        libdir:                   ' + session_libdir + '\n'
output += '        bindir:                   ' + session_bindir + '\n'
output += '        sbindir:                  ' + session_sbindir + '\n'
output += '        sysconfdir:               ' + session_sysconfdir + '\n'
output += '        localstatedir:            ' + session_localstatedir + '\n'
output += '        datadir:                  ' + session_datadir + '\n'
output += '        source code location:     ' + meson.source_root() + '\n'
output += '        compiler:                 ' + cc.get_id() + '\n'
output += '        cflags:                   ' + ' '.join(compiler_flags) + '\n'
output += '        Debug mode:               ' + session_debug.to_string() + '\n'
output += '        Use *_DISABLE_DEPRECATED: ' + enable_deprecation_flags.to_string() + '\n\n'
output += '        Session tracking:         ' + session_tracking + ' \n'
output += '        Build Docbook:            ' + enable_docbook.to_string() + '\n'
output += '        Build manpages:           ' + enable_man.to_string()
message(output)
