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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/param.h>
#include <string.h>
#ifdef HAVE_SETXATTR
#include <sys/xattr.h>
#endif

/* Project header files */
#include "consts.h"

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
	size_t scount;		//< # of snapshots
	char* curr_ver;		//< Current version
	uint64_t curr_fh;	//< Current version file handle
} SuperSdir;

struct studentfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

/* Helper methods */

/* Helper methods */
char *_get_next_vnum(const char *path, char *vnum) {
	// Get the first part of the string, and the last number as a series of tokens
	char *final_token  = malloc(MAX_VNUM_LEN);
	char *tokens[MAX_VNUM_LEN];
	char *vnum_branch = malloc(MAX_VNUM_LEN);

	// Split the tokens by the delimiter .
	int token_i = 0;
	char *res = strtok(vnum, ".");
	if (res != NULL) {
		strcpy(tokens[token_i], res);
		token_i++;
	}

	while ((res = strtok(NULL, ".")) != NULL) {
		strcpy(tokens[token_i], res);
		token_i++;
	}
	strcpy(final_token, tokens[token_i-1]);

	// Build the part of the vnum "branch" before the final delimited number (ie a.b.c.d -> a.b.c.)
	vnum_branch[0] = '\0';
	for(int i = 0; i < token_i; i++) {
		strcat(vnum_branch, tokens[i]);
	}

	// Make the sdir be a directory
	chmod(path, (0755 | S_IFDIR));
	int final_num = atoi(final_token);
	char *final_path = malloc(MAX_VNUM_LEN);
	char *final_num_str = malloc(MAX_VNUM_LEN);
	sprintf(final_num_str, "%d", final_num+1);

	// Increment the current version number by 1.
	strcpy(final_path, path);
	strcat(final_path, "/");
	strcat(final_path, vnum_branch);
	strcat(final_path, (const char *) final_num_str);

	// If there is already a child of the current directory, make a new branch (see Wiki research if this is confusing)
	if (access(final_path, F_OK) != -1) {
		strcpy(final_path, path);
		strcat(final_path, "/");
		strcat(final_path, vnum_branch);
		sprintf(final_num_str, "%d", final_num);
		strcat(final_path, (const char *) final_num_str);
		strcat(final_path, ".1");
		while (access(final_path, F_OK) != -1) {
			final_path[strlen(final_path)-1] = '0';
			strcat(final_path, ".1");
		}
	}

	free(final_token);
	free(res);	
	for (int i = 0; i < MAX_VNUM_LEN; i++) {
		free(tokens[i]);
	}
	free(vnum_branch);

	return final_path;
}

char *get_next_vnum(const char *path) {
	char *curr_vnum = malloc(MAX_VNUM_LEN);

	// File does not exist, so don't bother looking in the sdir.
	if (access(path, F_OK) == -1) {
		return "1";
	}

	#ifdef HAVE_SETXATTR
	int res = studentfs_getxattr(path, CURR_VNUM, curr_vnum, MAX_VNUM_LEN);
	if (res < 0) {
		printf("Error getting xattr %s, error is presumably that the wrong file was passed\n", CURR_VNUM);
		exit(0);
	}
	#endif
	
	return _get_next_vnum(path, curr_vnum);
}

/* 
 * make the SDIR if it does not already exist.
 * If it does exist, return -1.
 * If there is a corresponding file, copy all the information into the snapshot "1"
 * otherwise, just create the directory and a blank file titled "1". 
 */
int mk_sdir(const char *path) {
	// SDIR is already created, called improperly
	if (access(path, F_OK) == -1 && is_sdir_ftype(path)) {
		return -1;
	}
	long fsize = 0;
	char *buf = malloc(1024);
	int res;
	/* if there is a file, copy the contents into the buffer */
	if (access(path, F_OK) != -1) {
		FILE *f = fopen(path, "rb");
		fseek(f, 0, SEEK_END);
		fsize = ftell(f);
		fseek(f, 0, SEEK_SET);

		res = fread(buf, fsize, sizeof(char), f);
		if (res < 0) {
			printf("failed to read from file before creating sdir\n");
			return res;
		}
		fclose(f);

		/* delete the existing file now that the contents are in the buffer */
		unlink(path);
		res = mkdir(path, 0755 | S_IFDIR);
		if (res < 0) {
			printf("failed to make SDIR directory\n");
			return res;
		}
		free(f);
	}


	/* open the first version of the file in the sdir*/
	char *init_filepath = malloc(2*MAX_VNUM_LEN);
	strcpy(init_filepath, path);
	strcat(init_filepath, "/");
	strcat(init_filepath, "1");

	/* write the buffered info to the file if it exists (fsize = 0 initially)*/
	FILE *f_new = fopen(init_filepath, "w");
	fwrite(buf, fsize, sizeof(char), f_new);
	fclose(f_new);
	
	free(buf);
	free(f_new);
	free(init_filepath);

	res = chmod(path, 0755 | S_IFREG);
	if (res < 0) {
		return res;
	}

	return 0;
}

/* FUSE methods */

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
	res = chmod(path, mode);
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
	#ifdef HAVE_SETXATTR	
	int fd;
	int create_flag = (fi->flags & O_CREAT) == O_CREAT;
	char *sdir_str = malloc(sizeof(SDIR_XATTR));
	int is_sdir = getxattr(path, SDIR_XATTR, sdir_str, sizeof(SDIR_XATTR));
	
	if (is_sdir_ftype(path) && create_flag && access(path, F_OK) == -1) {
		mk_sdir(path);
	} else if (!create_flag && is_sdir) {
		chmod(path, 0755 | S_IFDIR);
		char *vnum = malloc(MAX_VNUM_LEN);
		getxattr(path, VNUM_XATTR, vnum, sizeof(MAX_VNUM_LEN));
		char *new_path = malloc(2*MAX_VNUM_LEN);
		strcpy(new_path, path);
		strcat(new_path, "/");
		strcat(new_path, vnum);

		fd = studentfs_open(new_path, fi);
		
		free(vnum);
		free(new_path);
	} else {
		fd = open(path, fi->flags);
		if (fd == -1)
		return -errno;	
	}
	fi->fh = fd;
	free(sdir_str);
	#endif
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

#ifdef HAVE_SETXATTR
/* xattr operations are optional and can safely be left unimplemented */
static int studentfs_setxattr(const char *path, const char *name, const char *value,
			size_t size, int flags)
{
	int res = lsetxattr(path, name, value, size, flags);
	if (res == -1)
		return -errno;
	return 0;
}

static int studentfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	int res = lgetxattr(path, name, value, size);

	if (res == -1)
		return -errno;
	return res;
}

static int studentfs_listxattr(const char *path, char *list, size_t size)
{
	int res = llistxattr(path, list, size);

	if (res == -1)
		return -errno;
	return res;
}

static int studentfs_removexattr(const char *path, const char *name)
{
	int res = lremovexattr(path, name);

	if (res == -1)
		return -errno;
	return 0;
}
#endif /* HAVE_SETXATTR */

void *
studentfs_init(struct fuse_conn_info *conn)
{
	return NULL;
}

void
studentfs_destroy(void *userdata)
{
}


static struct fuse_operations studentfs_oper = {
	.init	   	= studentfs_init,
	.destroy	= studentfs_destroy,
	.getattr	= studentfs_getattr,
	.fgetattr	= studentfs_fgetattr,
	.access		= studentfs_access,
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
#ifdef HAVE_SETXATTR
	.setxattr	= studentfs_setxattr,
	.getxattr	= studentfs_getxattr,
	.listxattr	= studentfs_listxattr,
	.removexattr	= studentfs_removexattr,
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
