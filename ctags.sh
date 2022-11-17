#!/bin/bash
cd /root/com/wiredtiger
#ctags --file-scope=yes --language-force=C --links=yes --c-kinds=+lxp -R .
/usr/local/uctags/bin/ctags --extras=+Fq --language-force=C --links=yes --c-kinds=+lxpzLD --fields-C=+{macrodef}{properties} --fields=+{line} -R .
