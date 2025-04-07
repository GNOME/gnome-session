cflags = ['-DLOCALE_DIR="@0@"'.format(session_localedir)]

programs = [
  # name, deps, cflags, install_dir
  ['gnome-session-ctl', session_bin_deps, cflags, session_libexecdir],
  ['gnome-session-inhibit', session_deps, cflags, session_bindir],
]

if have_x11
  deps = session_deps + [
    sm_dep,
    ice_dep
  ]

  programs += [
    # name, deps, cflags, install_dir
    ['gnome-session-quit', deps, cflags, session_bindir],
  ]
endif

if enable_session_selector
  deps = [
    glib_dep,
    gtk_dep
  ]

  install_data(
    'gnome-session-custom-session',
    install_dir: session_bindir
  )

  cflags += '-DGTKBUILDER_DIR="@0@"'.format(session_pkgdatadir)

  programs += [['gnome-session-selector', deps, cflags, session_bindir]]
endif

foreach program: programs
  executable(
    program[0],
    program[0] + '.c',
    include_directories: top_inc,
    dependencies: program[1],
    c_args: program[2],
    install: true,
    install_dir: program[3]
  )
endforeach
