# CS137 Final Project
## A Student-Centric Versioning File System With Autosave
We hope to implement a simple versioning file system that supports configurable auto-saving. The high-level design strategy consists of implementing checkpoints and snapshots similar to implementations we have read in papers in class, but only at the file level.

## Purpose
The purpose of this project is obviously not to compete with leading versioning systems like git so much as for academic exploration through a project with a relatable target use case: the average college student, especially one who often has writing and/or programming projects. We hope that our FS will be useful and intuitive for writing papers, working on programming assignments, and organizing personal programming projects.

## MVP Features
We hope to allow users to optionally (The default settings are to be determined.) configure the frequency, lifetime, and maximum number of autosave checkpoints for a given file. Frequency will be measured based on the size of the change (i.e. number of characters, with the extremes being never and whenever there is any modification to the file). Lifetime will be measured based on the age of the checkpoint or when the file was last modified (or perhaps a function of both, to be determined). A user will also be able to save, or “snapshot,” a version of a file with a short, fixed-sized message similar to git commits. A user should be able to read, write, and erase all of these checkpoints or snapshots.

## Stretch Features
As a stretch goal, we may explore delayed writes, i.e. perhaps using B-trees in a similar way to BetrFS, which would potentially help significantly with time and space efficiency given that autosaved checkpoints may often be deleted without ever being opened. The scalability of this file system, as a design principle, will otherwise be entirely left to the user’s discretion. Users will be able to keep as many versions or checkpoints of any given file as they so please, subject only to their disk’s space constraints and not to configuration constraints. A user is permitted to have no cap on number of checkpoints existing at a given time, set an infinite lifetime on checkpoints unless deleted, set the frequency to every time a single character is modified in the file, and create as many versions as they choose.

## Potential Obstacles
The process of designing this file system will likely bottleneck at deciding how to keep track of versions and checkpoints. We have explored different metadata structures to store the namespace used by different file systems, distributed and not, including superblock and node-level trees and tables. Tracking modifications that branch from an old checkpoint or version will also pose a challenge. Much of our initial research will therefore be dedicated to designing the best metadata structure for a file system whose primary feature is version control. We will reconsider prior readings, turn to well-known version control systems for additional research, and consider time and resource constraints on this project’s scope in making this decision.

## Wiki
More up-to-date information can be found in the wiki.

## Important Notes
Certain editors like Vim do not interact well with how we handle our file descriptors. We recommend testing with command line functions such as echo and a text editor such as nano.

## References/Credits
Some of our code was taken from Miklos Szeredi's file fusexmp_fh.c:
    fuse-2.8.7.tar.gz examples directory
        Used as FUSE basis for our features to be built on top of Unix.
        Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
        Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>
    fusexmp_fh.c can be distributed under the terms of the GNU GPL.
