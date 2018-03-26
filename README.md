# 137 Final Proj: versioning file system with autosave.
Target user is a student. Can benefit from features when writing a paper or working on a personal programming project. Take advantage of versioning, with the additional benefit of autosaving at a settable frequency.

## MVP MILESTONES
### 0. Research
- Git, svn: get a general idea of their file sys implementation, though ours won't be nearly as complex (and won't be distributed)
- VMS for versioning.

### 1. Design Decision: Metadata and Namespace
- metadata and namespace data structure(s)

### 2. FUSE file system as base.
- Set up base Unix file system using FUSE, similar to fusexmp; will serve as foundation to features we build on top.

### 2. Snapshots (Versioning)
- (Per Prof. Kuenning's suggestion) VMS file system: every file has a version number, delimited using a semicolon.
-- i.e. "foo.txt;1", "foo.txt;2"
#### Features
1. Similar to git commit; snapshot version with a short, fixed-sized message.

### 3. Checkpoints as autosave
#### Configurable Settings
1. *Frequency*: based on size of the change (i.e. number of characters)
2. *Lifetime*: age of checkpoint, when file was last modified (TODO: What does this function look like?)
3. *Maximum*: no more than x amount of checkpoints allowed to exist at once (i.e. delete old ones as new ones are made, for sake of space efficiency)

## STRETCH MILESTONES
### B-trees for Delayed Writes
### Copy-On-Write (COW)
