#!/bin/bash
if [ -e $1 ]; then
    echo "SnapDirectory $1 already exists!"
else
    if [ "$#" -eq 1 ]; then
        mkdir ".$1.SDIR;-1"
    elif [ "$#" -eq 2 ]; then
        mkdir ".$1.SDIR;$2"
    else
        echo "Incorrent number of args supplied"
    fi

fi
