#!/usr/bin/env bash

xgettext --c++ --keyword=_ --keyword=N_ src/beast.h -o po/base.pot
xgettext --language=Desktop src/beast.conf.in.in -j -o po/base.pot

while read -r line; do
  msgmerge -U po/$line.po po/base.pot
done < po/LINGUAS
