programs = [
  # name, deps, install_dir
  ['gnome-session-ctl', session_bin_deps, session_libexecdir],
  ['gnome-session-inhibit', session_deps, session_bindir],
  ['gnome-session-quit', session_deps, session_bindir],
]

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
