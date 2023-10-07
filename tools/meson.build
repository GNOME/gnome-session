cflags = ['-DLOCALE_DIR="@0@"'.format(session_localedir)]

programs = [
  # name, deps, cflags, install_dir
  ['gnome-session-quit', session_deps, cflags, session_bindir],
  ['gnome-session-inhibit', session_deps, cflags, session_bindir]
]

programs += [['gnome-session-ctl', session_bin_deps, cflags, session_libexecdir]]

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
