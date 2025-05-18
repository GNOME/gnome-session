#!/usr/bin/env python3

import os
import shutil
import sys

if os.environ.get('DESTDIR'):
  install_root = os.environ.get('DESTDIR') + os.path.abspath(sys.argv[1])
else:
  install_root = sys.argv[1]

src = os.path.join(install_root, 'xsessions', 'gnome-copy.desktop')
dst = os.path.join(install_root, 'xsessions', 'gnome.desktop')
shutil.move(src, dst)
