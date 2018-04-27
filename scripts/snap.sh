#!/bin/bash
# snap [new_fname] [optional msg]
# should be able to deduce old version from
# xattrs of current sdir
# if [ -e $1 ]; then
#     echo "File $1 already exists!"
# else
#     mkdir "$1;${2:-""}.VER"
# fi


# snap [fname]
# I guess for now all this does is provide a manual
# option to create another snap. We can decide later whether manual
# ones can be given names like the github branch scheme.
printf '' > "$1.SNA"
