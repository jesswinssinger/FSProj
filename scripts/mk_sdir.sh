#!/bin/bash
if [ -e $1 ]; then
    echo "SnapDirectory $1 already exists!"
else
    if [ "$#" -eq 1 ]; then
        echo "-1;" > "/tmp/SDIR_info"
        echo -1 > "/tmp/SDIR_info"
        mkdir ".$1.SDIR"
    elif [ "$#" -eq 2 ]; then
        # Only size_freq configured
        echo "$2;" > "/tmp/SDIR_info"
        echo -1 > "/tmp/SDIR_info"
        mkdir ".$1.SDIR"
    elif [ "$#" -eq 3 ]; then
        # Both size_freq and vmax configured
        echo "$2;" > "/tmp/SDIR_info"
        echo $3 >> "/tmp/SDIR_info"
        mkdir ".$1.SDIR"
    else
        echo "Incorrent number of arguments supplied."
    fi

fi
