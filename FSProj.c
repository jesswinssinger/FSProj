/*
  studentfs: STUDENT FILE SYSTEM
  Authors: Jess Winssinger, Evan Chrisinger, Santi Weight

  Source: fuse-2.8.7.tar.gz examples directory
  	Used as FUSE basis for our features to be built on top of Unix.
  	Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>
  	Copyright (C) 2011       Sebastian Pipping <sebastian@pipping.org>

	fusexmp_fh.c can be distributed under the terms of the GNU GPL.
  gcc -Wall studentfs.c `pkg-config fuse --cflags --libs` -o studentfs
*/

#define FUSE_USE_VERSION 26

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef __APPLE__
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include <fuse.h>
#ifndef __APPLE__
#include <ulockmgr.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif
#ifndef __APPLE__
#include <sys/file.h> /* flock(2) */
#endif

#include <sys/param.h>

#ifdef __APPLE__

#include <fcntl.h>
#include <sys/vnode.h>

#if defined(_POSIX_C_SOURCE)
typedef unsigned char  u_char;
typedef unsigned short u_short;
typedef unsigned int   u_int;
typedef unsigned long  u_long;
#endif

#include <sys/attr.h>

#define G_PREFIX			"org"
#define G_KAUTH_FILESEC_XATTR G_PREFIX 	".apple.system.Security"
#define A_PREFIX			"com"
#define A_KAUTH_FILESEC_XATTR A_PREFIX 	".apple.system.Security"
#define XATTR_APPLE_PREFIX		"com.apple."

#endif /* __APPLE__ */

typedef struct Sdir {
	char* ver_num; 	  //< Version number
	uint64_t sfh; 	  //< File handle of snapfile
	mode_t smode;	  //< Mode for snapfile
	size_t ssize;	  //< Size of snapfile
	size_t size;	  //< ssize + size of subtree
	size_t entry_cnt; //< # of entries
} Sdir;

typedef struct SuperSdir {
	char* fname;    	//< Name of file
	size_t scount; 	//< # of snapshots
	char* curr_ver;		//< Current version
	uint64_t curr_fh;	//< Current version file handle
} SuperSdir;

static int studentfs_getattr(const char *path, struct stat *stbuf)
{
	int res;

	res = lstat(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_fgetattr(const char *path, struct stat *stbuf,
			struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = fstat(fi->fh, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_access(const char *path, int mask)
{
	int res;

	res = access(path, mask);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_readlink(const char *path, char *buf, size_t size)
{
	int res;

	res = readlink(path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	return 0;
}

struct studentfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

static int studentfs_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
	struct studentfs_dirp *d = malloc(sizeof(struct studentfs_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(path);
	if (d->dp == NULL) {
		res = -errno;
		free(d);
		return res;
	}
	d->offset = 0;
	d->entry = NULL;

	fi->fh = (unsigned long) d;
	return 0;
}

static inline struct studentfs_dirp *get_dirp(struct fuse_file_info *fi)
{
	return (struct studentfs_dirp *) (uintptr_t) fi->fh;
}

static int studentfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
		       off_t offset, struct fuse_file_info *fi)
{
	struct studentfs_dirp *d = get_dirp(fi);

	(void) path;
	if (offset != d->offset) {
		seekdir(d->dp, offset);
		d->entry = NULL;
		d->offset = offset;
	}
	while (1) {
		struct stat st;
		off_t nextoff;

		if (!d->entry) {
			d->entry = readdir(d->dp);
			if (!d->entry)
				break;
		}

		memset(&st, 0, sizeof(st));
		st.st_ino = d->entry->d_ino;
		st.st_mode = d->entry->d_type << 12;
		nextoff = telldir(d->dp);
		if (filler(buf, d->entry->d_name, &st, nextoff))
			break;

		d->entry = NULL;
		d->offset = nextoff;
	}

	return 0;
}

static int studentfs_releasedir(const char *path, struct fuse_file_info *fi)
{
	struct studentfs_dirp *d = get_dirp(fi);
	(void) path;
	closedir(d->dp);
	free(d);
	return 0;
}

static int studentfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
	int res;

	if (S_ISFIFO(mode))
		res = mkfifo(path, mode);
	else
		res = mknod(path, mode, rdev);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_mkdir(const char *path, mode_t mode)
{
	int res;

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_unlink(const char *path)
{
	int res;

	res = unlink(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_rmdir(const char *path)
{
	int res;

	res = rmdir(path);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_symlink(const char *from, const char *to)
{
	int res;

	res = symlink(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_rename(const char *from, const char *to)
{
	int res;

	res = rename(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_link(const char *from, const char *to)
{
	int res;

	res = link(from, to);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_chmod(const char *path, mode_t mode)
{
	int res;

#ifdef __APPLE__
	res = lchmod(path, mode);
#else
	res = chmod(path, mode);
#endif
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;

	res = lchown(path, uid, gid);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_truncate(const char *path, off_t size)
{
	int res;

	res = truncate(path, size);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_ftruncate(const char *path, off_t size,
			 struct fuse_file_info *fi)
{
	int res;

	(void) path;

	res = ftruncate(fi->fh, size);
	if (res == -1)
		return -errno;

	return 0;
}

#ifdef HAVE_UTIMENSAT
static int studentfs_utimens(const char *path, const struct timespec ts[2])
{
	int res;

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int studentfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, fi->flags, mode);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int studentfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd;

	fd = open(path, fi->flags);
	if (fd == -1)
		return -errno;

	fi->fh = fd;
	return 0;
}

static int studentfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pread(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int studentfs_read_buf(const char *path, struct fuse_bufvec **bufp,
			size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec *src;

	(void) path;

	src = malloc(sizeof(struct fuse_bufvec));
	if (src == NULL)
		return -ENOMEM;

	*src = FUSE_BUFVEC_INIT(size);

	src->buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	src->buf[0].fd = fi->fh;
	src->buf[0].pos = offset;

	*bufp = src;

	return 0;
}

static int studentfs_write(const char *path, const char *buf, size_t size,
		     off_t offset, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	res = pwrite(fi->fh, buf, size, offset);
	if (res == -1)
		res = -errno;

	return res;
}

static int studentfs_write_buf(const char *path, struct fuse_bufvec *buf,
		     off_t offset, struct fuse_file_info *fi)
{
	struct fuse_bufvec dst = FUSE_BUFVEC_INIT(fuse_buf_size(buf));

	(void) path;

	dst.buf[0].flags = FUSE_BUF_IS_FD | FUSE_BUF_FD_SEEK;
	dst.buf[0].fd = fi->fh;
	dst.buf[0].pos = offset;

	return fuse_buf_copy(&dst, buf, FUSE_BUF_SPLICE_NONBLOCK);
}

static int studentfs_statfs(const char *path, struct statvfs *stbuf)
{
	int res;

	res = statvfs(path, stbuf);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_flush(const char *path, struct fuse_file_info *fi)
{
	int res;

	(void) path;
	/* This is called from every close on an open file, so call the
	   close on the underlying filesystem.	But since flush may be
	   called multiple times for an open file, this must not really
	   close the file.  This is important if used on a network
	   filesystem like NFS which flush the data/metadata on close() */
	res = close(dup(fi->fh));
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_release(const char *path, struct fuse_file_info *fi)
{
	(void) path;
	close(fi->fh);

	return 0;
}

static int studentfs_fsync(const char *path, int isdatasync,
		     struct fuse_file_info *fi)
{
	int res;
	(void) path;

#ifndef HAVE_FDATASYNC
	(void) isdatasync;
#else
	if (isdatasync)
		res = fdatasync(fi->fh);
	else
#endif
		res = fsync(fi->fh);
	if (res == -1)
		return -errno;

	return 0;
}

#if defined(HAVE_POSIX_FALLOCATE) || defined(__APPLE__)
static int studentfs_fallocate(const char *path, int mode,
			off_t offset, off_t length, struct fuse_file_info *fi)
{
#ifdef __APPLE__
	fstore_t fstore;

	if (!(mode & PREALLOCATE))
		return -ENOTSUP;

	fstore.fst_flags = 0;
	if (mode & ALLOCATECONTIG)
		fstore.fst_flags |= F_ALLOCATECONTIG;
	if (mode & ALLOCATEALL)
		fstore.fst_flags |= F_ALLOCATEALL;

	if (mode & ALLOCATEFROMPEOF)
		fstore.fst_posmode = F_PEOFPOSMODE;
	else if (mode & ALLOCATEFROMVOL)
		fstore.fst_posmode = F_VOLPOSMODE;

	fstore.fst_offset = offset;
	fstore.fst_length = length;

	if (fcntl(fi->fh, F_PREALLOCATE, &fstore) == -1)
		return -errno;
	else
		return 0;
#else
	(void) path;

	if (mode)
		return -EOPNOTSUPP;

	return -posix_fallocate(fi->fh, offset, length);
#endif
}
#endif

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
#ifdef __APPLE__
static int studentfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags, uint32_t position)
#else
static int studentfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
#endif
{
#ifdef __APPLE__
	int res;
	if (!strncmp(name, XATTR_APPLE_PREFIX, sizeof(XATTR_APPLE_PREFIX) - 1)) {
		flags &= ~(XATTR_NOSECURITY);
	}
	if (!strcmp(name, A_KAUTH_FILESEC_XATTR)) {
		char new_name[MAXPATHLEN];
		memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
		memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);
		res = setxattr(path, new_name, value, size, position, flags);
	} else {
		res = setxattr(path, name, value, size, position, flags);
	}
#else
	int res = lsetxattr(path, name, value, size, flags);
#endif
	if (res == -1)
		return -errno;
	return 0;
}

#ifdef __APPLE__
static int studentfs_getxattr(const char *path, const char *name, char *value,
			size_t size, uint32_t position)
#else
static int studentfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
#endif
{
#ifdef __APPLE__
	int res;
	if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
		char new_name[MAXPATHLEN];
		memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
		memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);
		res = getxattr(path, new_name, value, size, position, XATTR_NOFOLLOW);
	} else {
		res = getxattr(path, name, value, size, position, XATTR_NOFOLLOW);
	}
#else
	int res = lgetxattr(path, name, value, size);
#endif
	if (res == -1)
		return -errno;
	return res;
}

static int studentfs_listxattr(const char *path, char *list, size_t size)
{
#ifdef __APPLE__
	ssize_t res = listxattr(path, list, size, XATTR_NOFOLLOW);
	if (res > 0) {
		if (list) {
			size_t len = 0;
			char *curr = list;
			do {
				size_t thislen = strlen(curr) + 1;
				if (strcmp(curr, G_KAUTH_FILESEC_XATTR) == 0) {
					memmove(curr, curr + thislen, res - len - thislen);
					res -= thislen;
					break;
				}
				curr += thislen;
				len += thislen;
			} while (len < res);
		} else {
			/*
			ssize_t res2 = getxattr(path, G_KAUTH_FILESEC_XATTR, NULL, 0, 0,
						XATTR_NOFOLLOW);
			if (res2 >= 0) {
				res -= sizeof(G_KAUTH_FILESEC_XATTR);
			}
			*/
		}
	}
#else
	int res = llistxattr(path, list, size);
#endif
	if (res == -1)
		return -errno;
	return res;
}

static int studentfs_removexattr(const char *path, const char *name)
{
#ifdef __APPLE__
	int res;
	if (strcmp(name, A_KAUTH_FILESEC_XATTR) == 0) {
		char new_name[MAXPATHLEN];
		memcpy(new_name, A_KAUTH_FILESEC_XATTR, sizeof(A_KAUTH_FILESEC_XATTR));
		memcpy(new_name, G_PREFIX, sizeof(G_PREFIX) - 1);
		res = removexattr(path, new_name, XATTR_NOFOLLOW);
	} else {
		res = removexattr(path, name, XATTR_NOFOLLOW);
	}
#else
	int res = lremovexattr(path, name);
#endif
	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

#ifndef __APPLE__
static int studentfs_lock(const char *path, struct fuse_file_info *fi, int cmd,
		    struct flock *lock)
{
	(void) path;

	return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
			   sizeof(fi->lock_owner));
}
#endif

void *
studentfs_init(struct fuse_conn_info *conn)
{
#ifdef __APPLE__
	FUSE_ENABLE_SETVOLNAME(conn);
	FUSE_ENABLE_XTIMES(conn);
#endif
	return NULL;
}

void
studentfs_destroy(void *userdata)
{
}

#ifndef __APPLE__
static int studentfs_flock(const char *path, struct fuse_file_info *fi, int op)
{
	int res;
	(void) path;

	res = flock(fi->fh, op);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static struct fuse_operations studentfs_oper = {
	.init	   	= studentfs_init,
	.destroy	= studentfs_destroy,
	.getattr	= studentfs_getattr,
	.fgetattr	= studentfs_fgetattr,
#ifndef __APPLE__
	.access		= studentfs_access,
#endif
	.readlink	= studentfs_readlink,
	.opendir	= studentfs_opendir,
	.readdir	= studentfs_readdir,
	.releasedir	= studentfs_releasedir,
	.mknod		= studentfs_mknod,
	.mkdir		= studentfs_mkdir,
	.symlink	= studentfs_symlink,
	.unlink		= studentfs_unlink,
	.rmdir		= studentfs_rmdir,
	.rename		= studentfs_rename,
	.link		= studentfs_link,
	.chmod		= studentfs_chmod,
	.chown		= studentfs_chown,
	.truncate	= studentfs_truncate,
	.ftruncate	= studentfs_ftruncate,
#ifdef HAVE_UTIMENSAT
	.utimens	= studentfs_utimens,
#endif
	.create		= studentfs_create,
	.open		= studentfs_open,
	.read		= studentfs_read,
	.read_buf	= studentfs_read_buf,
	.write		= studentfs_write,
	.write_buf	= studentfs_write_buf,
	.statfs		= studentfs_statfs,
	.flush		= studentfs_flush,
	.release	= studentfs_release,
	.fsync		= studentfs_fsync,
#if defined(HAVE_POSIX_FALLOCATE) || defined(__APPLE__)
	.fallocate	= studentfs_fallocate,
#endif
#ifdef HAVE_SETXATTR
	.setxattr	= studentfs_setxattr,
	.getxattr	= studentfs_getxattr,
	.listxattr	= studentfs_listxattr,
	.removexattr	= studentfs_removexattr,
#endif
#ifndef __APPLE__
	.lock		= studentfs_lock,
	.flock		= studentfs_flock,
#endif

	.flag_nullpath_ok = 1,
#if HAVE_UTIMENSAT
	.flag_utime_omit_ok = 1,
#endif
};

int main(int argc, char *argv[])
{
	umask(0);
	return fuse_main(argc, argv, &studentfs_oper, NULL);
}
