#!/bin/bash
if [ -e $1 ]; then
    echo "SnapDirectory $1 already exists!"
else
    touch "$1.SDIR"
fi