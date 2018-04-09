CC	=	gcc
SHELL   =	/bin/sh
CFLAGS  =	-g -Og $(PKGFLAGS)

PKGFLAGS        =	`pkg-config fuse --cflags --libs`

sfs.o : FSProj.c
	gcc -Wall -g -Og $(PKGFLAGS) FSProj.c -o sfs
