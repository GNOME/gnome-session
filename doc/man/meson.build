xsltproc_cmd = [
  find_program('xsltproc'),
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

mans = [
  'gnome-session',
  'gnome-session-inhibit',
  'gnome-session-quit',
]

foreach man: mans
  output = man + '.1'
  custom_target(
    output,
    input: man + '.xml',
    output: output,
    command: xsltproc_cmd,
    install: true,
    install_dir: get_option('mandir') / 'man1'
  )

  xml_man_pages += files(man + '.xml')
endforeach