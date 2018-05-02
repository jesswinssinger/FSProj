/*
    FUSE: Filesystem in Userspace
    Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

    This program can be distributed under the terms of the GNU GPL.
    See the file COPYING.

    gcc -Wall `pkg-config fuse --cflags --libs` fusexmp.c -o fusexmp


  If a filesystem is mounted over a directory, how can I access
  the old contents?

There are two possibilities:

The first is to use 'mount --bind DIR TMPDIR' to create a copy
of the namespace under DIR. After mounting the FUSE filesystem
over DIR, files can still be accessed through TMPDIR. This needs
root privileges.

The second is to set the working directory to DIR after mounting
the FUSE filesystem. For example before fuse_main() do

save_dir = open(DIR, O_RDONLY);

And from the init() method do

fchdir(save_dir); close(save_dir);

Then access the files with relative paths (with newer LIBC versions
the *at() functions may also be used instead of changing the CWD).

This method doesn't need root privileges, but only works on Linux
(FreeBSD does path resolving in a different way), and it's not even
guaranteed to work on future Linux versions.



*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

#define PATHLEN_MAX 1024

#include "debugf.c"

//#include "cache.c"

static char initial_working_dir[PATHLEN_MAX+1] ={ '\0' };
static char cached_mountpoint[PATHLEN_MAX+1] ={ '\0' };
static int save_dir;

const char *full(const char *path) /* add mountpoint to path */;//shut up bogus gcc warnings
const char *full(const char *path) /* add mountpoint to path */
{

// PROBLEMS:
//   1) HEAP LEAKAGE
//   2) <fixed>
//   3) canonicalising the path (removing .. etc) - there's
//      a call for it if I can just remember the name.
//   4) what to do if malloc returns NULL - should we also 'umount'?

  char *ep, *buff;

  buff = strdup(path+1); if (buff == NULL) exit(1);

  ep = buff + strlen(buff) - 1; if (*ep == '/') *ep = '\0'; /* don't think this ever happens */

  if (*buff == '\0') strcpy(buff, "."); /* (but this definitely does...) */

  return buff;
}

static int xmp_getattr(const char *path, struct stat *stbuf)
{
    int res;

path = full(path);
debugf("fusexmp: getattr(%s)\n", path);
    res = lstat(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_access(const char *path, int mask)
{
    int res;

path = full(path);
debugf("fusexmp: access(%s)\n", path);
    res = access(path, mask);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_readlink(const char *path, char *buf, size_t size)
{
    int res;

path = full(path);
debugf("fusexmp: readlink(%s)\n", path);
    res = readlink(path, buf, size - 1);
    if (res == -1)
        return -errno;

    buf[res] = '\0';
    return 0;
}


static int xmp_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                       off_t offset, struct fuse_file_info *fi)
{
    DIR *dp;
    struct dirent *de;

    (void) offset;
    (void) fi;

path = full(path);
debugf("fusexmp: readdir(%s)\n", path);
    dp = opendir(path);
    if (dp == NULL)
        return -errno;

    while ((de = readdir(dp)) != NULL) {
        struct stat st;
        memset(&st, 0, sizeof(st));
        st.st_ino = de->d_ino;
        st.st_mode = de->d_type << 12;
        if (filler(buf, de->d_name, &st, 0))
            break;
    }

    closedir(dp);
    return 0;
}

static int xmp_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int res;

    /* On Linux this could just be 'mknod(path, mode, rdev)' but this
       is more portable */
path = full(path);
debugf("fusexmp: mknod(%s)\n", path);
    if (S_ISREG(mode)) {
        res = open(path, O_CREAT | O_EXCL | O_WRONLY, mode);
        if (res >= 0)
            res = close(res);
    } else if (S_ISFIFO(mode))
        res = mkfifo(path, mode);
    else
        res = mknod(path, mode, rdev);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_mkdir(const char *path, mode_t mode)
{
    int res;

path = full(path);
debugf("fusexmp: mkdir(%s)\n", path);
    res = mkdir(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_unlink(const char *path)
{
    int res;

path = full(path);
debugf("fusexmp: unlink(%s)\n", path);
    res = unlink(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rmdir(const char *path)
{
    int res;

path = full(path);
debugf("fusexmp: rmdir(%s)\n", path);
    res = rmdir(path);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_symlink(const char *from, const char *to)
{
    int res;

from = full(from);
to = full(to);
debugf("fusexmp: symlink(%s, %s)\n", from, to);
    res = symlink(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_rename(const char *from, const char *to)
{
    int res;

from = full(from);
to = full(to);
debugf("fusexmp: rename(%s, %s)\n", from, to);
    res = rename(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_link(const char *from, const char *to)
{
    int res;

from = full(from);
to = full(to);
debugf("fusexmp: link(%s, %s)\n", from, to);
    res = link(from, to);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chmod(const char *path, mode_t mode)
{
    int res;

path = full(path);
debugf("fusexmp: chmod(%s)\n", path);
    res = chmod(path, mode);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_chown(const char *path, uid_t uid, gid_t gid)
{
    int res;

path = full(path);
debugf("fusexmp: lchown(%s)\n", path);
    res = lchown(path, uid, gid);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_truncate(const char *path, off_t size)
{
    int res;

path = full(path);
debugf("fusexmp: truncate(%s)\n", path);
    res = truncate(path, size);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_utimens(const char *path, const struct timespec ts[2])
{
    int res;
    struct timeval tv[2];

    tv[0].tv_sec = ts[0].tv_sec;
    tv[0].tv_usec = ts[0].tv_nsec / 1000;
    tv[1].tv_sec = ts[1].tv_sec;
    tv[1].tv_usec = ts[1].tv_nsec / 1000;

path = full(path);
debugf("fusexmp: utimes(%s)\n", path);
    res = utimes(path, tv);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_open(const char *path, struct fuse_file_info *fi)
{
    int res;

path = full(path);
debugf("fusexmp: open(%s)\n", path);
    res = open(path, fi->flags);
    if (res == -1)
        return -errno;

    close(res);
    return 0;
}

static int xmp_read(const char *path, char *buf, size_t size, off_t offset,
                    struct fuse_file_info *fi)
{
    int fd;
    int res;

    (void) fi;
path = full(path);
debugf("fusexmp: read(%s)\n", path);
    printf("in read\n\n");
    exit(0);
    fd = open(path, O_RDONLY);
    if (fd == -1)
        return -errno;

    res = pread(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int xmp_write(const char *path, const char *buf, size_t size,
                     off_t offset, struct fuse_file_info *fi)
{

// actually we don't need to look at the 'open' calls - when
// we call 'write', flag the file's private data with the
// fact that the file was updated, so that we can take a
// copy when we get the 'release' call.

    int fd;
    int res;

    (void) fi;
path = full(path);
debugf("fusexmp: write(%s)\n", path);
    fd = open(path, O_WRONLY);
    if (fd == -1)
        return -errno;

    res = pwrite(fd, buf, size, offset);
    if (res == -1)
        res = -errno;

    close(fd);
    return res;
}

static int xmp_statfs(const char *path, struct statvfs *stbuf)
{
    int res;

path = full(path);
debugf("fusexmp: statvfs(%s)\n", path);
    res = statvfs(path, stbuf);
    if (res == -1)
        return -errno;

    return 0;
}

static int xmp_release(const char *path, struct fuse_file_info *fi)
{
// Release is called when there are no more open handles.  This is where
// we do whatever action we want to with the file as all updates are
// now complete.  For example, calling gpg to encrypt it, or rsync
// to transfer it to disaster-recovery storage

// OR look at fi->flags for write access, and assume if opened
// for write, it will have been written to

path = full(path);
debugf("fusexmp: release(%s) flags=%02x\n", path, fi->flags);
    if ((fi->flags&1) != 0) {
      debugf("fusexmp TRIGGER: save file to /mnt/backup/%s\n", path);
    }
    (void) path;
    (void) fi;
    return 0;
}

static int xmp_fsync(const char *path, int isdatasync,
                     struct fuse_file_info *fi)
{
    /* Just a stub.  This method is optional and can safely be left
       unimplemented */

path = full(path);
debugf("fusexmp: fsync(%s)\n", path);
    (void) path;
    (void) isdatasync;
    (void) fi;
    return 0;
}

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int xmp_setxattr(const char *path, const char *name, const char *value,
                        size_t size, int flags)
{
    int res;
path = full(path);
debugf("fusexmp: setxattr(%s)\n", path);
    res = lsetxattr(path, name, value, size, flags);
    if (res == -1)
        return -errno;
    return 0;
}

static int xmp_getxattr(const char *path, const char *name, char *value,
                    size_t size)
{
    int res;

path = full(path);
debugf("fusexmp: getxattr(%s)\n", path);
    res = lgetxattr(path, name, value, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_listxattr(const char *path, char *list, size_t size)
{
    int res;

path = full(path);
debugf("fusexmp: listxattr(%s)\n", path);
    res = llistxattr(path, list, size);
    if (res == -1)
        return -errno;
    return res;
}

static int xmp_removexattr(const char *path, const char *name)
{
    int res;

path = full(path);
debugf("fusexmp: removexattr(%s)\n", path);
    res = lremovexattr(path, name);
    if (res == -1)
        return -errno;
    return 0;
}
#endif /* HAVE_SETXATTR */

static void *xmp_init(void)
{
  debugf("fusexmp: init()\n");
  // trick to allow mounting as an overlay - doesn't work on freebsd
  fchdir(save_dir);
  close(save_dir);
  return NULL;
}

static struct fuse_operations xmp_oper = {
    .init       = xmp_init, /* fusexmp.c:486: warning: initialization from incompatible pointer type */
    .getattr	= xmp_getattr,
    .access	= xmp_access,
    .readlink	= xmp_readlink,
    .readdir	= xmp_readdir,
    .mknod	= xmp_mknod,
    .mkdir	= xmp_mkdir,
    .symlink	= xmp_symlink,
    .unlink	= xmp_unlink,
    .rmdir	= xmp_rmdir,
    .rename	= xmp_rename,
    .link	= xmp_link,
    .chmod	= xmp_chmod,
    .chown	= xmp_chown,
    .truncate	= xmp_truncate,
    .utimens	= xmp_utimens,
    .open	= xmp_open,
    .read	= xmp_read,
    .write	= xmp_write,
    .statfs	= xmp_statfs,
    .release	= xmp_release,
    .fsync	= xmp_fsync,
#ifdef HAVE_SETXATTR
    .setxattr	= xmp_setxattr,
    .getxattr	= xmp_getxattr,
    .listxattr	= xmp_listxattr,
    .removexattr= xmp_removexattr,
#endif
};

int main(int argc, char *argv[])
{
int rc;

    umask(0);
    getcwd(initial_working_dir, PATHLEN_MAX);
debugf("fusexmp: cwd=%s\n", initial_working_dir);

//    cache_init();
//    cache_save(initial_working_dir, 0);

debugf("fusexmp: main(%s, %s, %s, %s)\n", argv[0], argv[1], argv[2], argv[3]);
    strncpy(cached_mountpoint, argv[1], strlen(argv[1]));
debugf("fusexmp: mountpoint=%s\n", cached_mountpoint);

    save_dir = open(cached_mountpoint, O_RDONLY);
    rc = fuse_main(argc, argv, &xmp_oper, NULL);

debugf("fusexmp: umount(%s, %s, %s, %s)\n", argv[0], argv[1], argv[2], argv[3]);

//    cache_invalidate(initial_working_dir);
    return rc;
}