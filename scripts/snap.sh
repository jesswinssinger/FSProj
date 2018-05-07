#!/bin/bash

# snap [fname]
printf '' > "$1.SNA"
# I guess for now all this does is provide a manual
# option to create another snap. We can decide later whether manual
# ones can be given names like the github branch scheme.

if [ "$#" -eq 2 ]; then
    date >> ".$1.SDIR/log.txt"
    printf $2 >> ".$1.SDIR/log.txt"
fi
