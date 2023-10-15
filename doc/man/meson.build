xsltproc = find_program('xsltproc')
man1_dir = join_paths(get_option('mandir'), 'man1')

xsltproc_cmd = [
  xsltproc,
  '--output', '@OUTPUT@',
  '--nonet',
  '--stringparam', 'man.output.quietly', '1',
  '--stringparam', 'funcsynopsis.style', 'ansi',
  '--stringparam', 'man.th.extra1.suppress', '1',
  '--stringparam', 'man.authors.section.enabled', '0',
  '--stringparam', 'man.copyright.section.enabled', '0',
  'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
  '@INPUT@'
]

mans = ['gnome-session-inhibit']

if enable_session_selector
  mans += 'gnome-session-selector'
endif

foreach man: mans
  output = man + '.1'

  custom_target(
    output,
    input: man + '.xml',
    output: output,
    command: xsltproc_cmd,
    install: true,
    install_dir: man1_dir
  )
endforeach

man_data = files(
  'gnome-session.1',
  'gnome-session-quit.1'
)

install_data(
  man_data,
  install_dir: man1_dir
)
