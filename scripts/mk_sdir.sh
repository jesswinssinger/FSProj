#!/bin/bash
if [ -e $1 ]; then
    echo "SnapDirectory $1 already exists!"
else
    if [ "$#" -eq 1 ]; then
        echo -1 > "/tmp/SDIR_info"
        mkdir ".$1.SDIR"
    elif [ "$#" -eq 2 ]; then
        echo $2 > "/tmp/SDIR_info"
        mkdir ".$1.SDIR"
    else
        echo "Incorrent number of args supplied"
    fi

fi
