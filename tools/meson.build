programs = [
  # name, deps, install_dir
  ['gnome-session-ctl', session_bin_deps, session_libexecdir],
  ['gnome-session-inhibit', session_deps, session_bindir],
]

if have_x11
  deps = session_deps + [
    sm_dep,
    ice_dep
  ]

  programs += [
    # name, deps, install_dir
    ['gnome-session-quit', deps, session_bindir],
  ]
endif

foreach program: programs
  executable(
    program[0],
    program[0] + '.c',
    include_directories: top_inc,
    dependencies: program[1],
    c_args: ['-DLOCALE_DIR="@0@"'.format(session_localedir)],
    install: true,
    install_dir: program[2]
  )
endforeach
