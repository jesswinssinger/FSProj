#!/bin/bash
# snap [new_fname] [optional msg]
# should be able to deduce old version from
# xattrs of current sdir
if [ -e $1 ]; then
    echo "File $1 already exists!"
else
    mkdir "$1;${2:-""}.VER"
fi
