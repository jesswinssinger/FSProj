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
#define DEBUG 1

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
#include <libgen.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/param.h>
#include <string.h>
#include <sys/xattr.h>

/* Project header files */
#include "consts.h"
#include "macros.h"
#include "structs.h"

/* Helper methods */

/* Returns the SDIR path for a given file path. Does not need to exist
 * to return successfully.
 */
char *get_sdir_path(const char *path){
	char *sdir_path = malloc(PATH_MAX+1);
	char *base_cp = malloc(PATH_MAX+1);
	char *dir_cp = malloc(PATH_MAX+1);
	strcpy(base_cp, path);
	strcpy(dir_cp, path);

	base_cp = basename(base_cp);
	dir_cp  = dirname(dir_cp);

	strcpy(sdir_path, dir_cp);
	strcat(sdir_path, "/.");
	strcat(sdir_path, base_cp);
	strcat(sdir_path, SDIR_FILETYPE);
	return sdir_path;
}

char *get_metadata_path(const char *path){
	char *meta_path = get_sdir_path(path);
	strcat(meta_path, METADATA_FILENAME);
	return meta_path;
}

char *get_curr_verr_path(const char *path) {
	/* Get current vnum from sdir's metadata. */
	char *meta_path = get_metadata_path(path);
	FILE *meta_f = fopen(meta_path, "rb+");

	char vnum[MAX_VNUM_LEN];
	int sz = fread(vnum, 1, MAX_VNUM_LEN, meta_f);
	if (sz < 0) {
		fprintf(stderr, "Error reading from metadata: %d\n", errno);
		exit(0);
	}
	fclose(meta_f);
	free(meta_path);

	/* Return current version's path. */
	char *curr_verr_path = get_sdir_path(path);
	strcat(curr_verr_path, "/");
	strcat(curr_verr_path, vnum);
	return curr_verr_path;
}

/* Gets file descriptor of the current version and opens the file. */
int get_sdir_file_fd(const char *path) {
	char *curr_verr_path = get_curr_verr_path(path);
	int fd = open(curr_verr_path, O_RDWR);
	free(curr_verr_path);
	return fd;
}

/* Checks if file whose path is given in the argument is being versioned
 * (i.e. has an SDIR)
 */
int is_sdir(const char *path) {
	char base_cp[PATH_MAX];
	strcpy(base_cp, path);
	char dir_cp[PATH_MAX];
	strcpy(dir_cp, path);

	char path_w_sdir[PATH_MAX];
	char *base = basename(base_cp);
	char *dir = dirname(dir_cp);

	strcpy(path_w_sdir, dir);
	strcat(path_w_sdir, "/.");
	strcat(path_w_sdir, base);
	strcat(path_w_sdir, SDIR_FILETYPE);

	return access(path_w_sdir, F_OK) != -1;
}

/* Return path with .SDIR removed from end of basename. */
//TODO: Shouldn't this also remove the "." that makes it hidden?
char *remove_SDIR_ftype(const char *path) {
	char *sdir_path = malloc(strlen(path)+1);
	strcpy(sdir_path, path);

	if (!is_sdir_ftype(path)) {
		return sdir_path;
	}

	sdir_path[strlen(path)-strlen(SDIR_FILETYPE)] = '\0';
	return sdir_path;
}

// TODO: Write this
/* Version changes gets the number of changes bytewise made to a file
 * at a file descriptor.
 *
 * My thinking on implementation:
 * Store the fd's and associated number of changes made to them in
 * a data structure of file descriptors globally.
 */
int ver_changes(char *path, char *buf) {
	return 0;
}

char *_get_next_ver(const char *path, char *vnum) {
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

	// Build the part of the vnum "branch" before the final
	// delimited number (ie a.b.c.d -> a.b.c.)
	vnum_branch[0] = '\0';
	for(int i = 0; i < token_i; i++) {
		strcat(vnum_branch, tokens[i]);
		strcat(vnum_branch, ".");
	}

	// Set up path with all but final delimited number.
	char *final_path = malloc(PATH_MAX);
	strcpy(final_path, path);
	strcat(final_path, "/");
	strcat(final_path, vnum_branch);

	// Calculate final number.
	char *final_num_str = malloc(MAX_VNUM_LEN);

	// If there is already a child of the current directory,
	// make a new branch (see Wiki research if this is confusing)
	if (access(final_path, F_OK) != -1) {
		strcat(final_path, final_token);
		strcat(final_path, ".1");
		while (access(final_path, F_OK) != -1) {
			final_path[strlen(final_path)-1] = '0';
			strcat(final_path, ".1");
		}
	}
	else { //If not, simply increment the old vnum's final index by 1.
		int final_num = atoi(final_token);
		sprintf(final_num_str, "%d", final_num+1);
		strcat(final_path, final_num_str);
	}
	free(final_token);
	free(res);
	for (int i = 0; i < MAX_VNUM_LEN; i++) {
		free(tokens[i]);
	}
	free(vnum_branch);

	return final_path;
}

char *get_next_ver(const char *path) {
	int res;

	// File does not exist, so don't bother looking in the sdir.
	if (access(path, F_OK) == -1) {
		return "1";
	}

	// Get current vnum from metadata file of SDIR.
	char *curr_vnum = malloc(MAX_VNUM_LEN);
	char *meta_path = get_metadata_path(path);
	int mfd = open(meta_path, O_RDONLY);
	res = read(mfd, curr_vnum, MAX_VNUM_LEN);

	if (res < 0) {
		printf("Error reading current vnum from metadata\n");
		exit(0);
	}

	return _get_next_ver(get_sdir_path(path), curr_vnum);
}

/* Helper for update_metadata(): deletes oldest sfile in sdir based on
 * modification time.
 * Metadata updates are all handled in update_metadata().
 */
//TODO: test this!
static int delete_oldest_sfile(const char* path, int vmax)
{
	char *command;

	char *sdir_path = get_sdir_path(path);
	sprintf(command, "ls %s | sed -e '1, %dd' | xargs -d '\n' rm",
		sdir_path, vmax);

	return system(command);
}

/* Increment vcount by 1 and update curr_vnum. If the new vcount is past vmax,
 * delete oldest checkpoint in the SDIR.
 */
static int update_metadata(const char* path, const char* new_curr_vnum)
{
	int res;

	/* Get metadata from metadata file. */
	struct metadata md;
	char *meta_path = get_metadata_path(path);
	int meta_fd = open(meta_path, O_TRUNC | O_WRONLY);
	res = read(meta_fd, &md, sizeof(struct metadata));

	/* Update curr_vnum and vcount (if necessary) */
	strcpy(md.curr_vnum, new_curr_vnum);

	int vmax_exceeded = (md.vmax != -1) && ((md.vcount + 1) > md.vmax);
	if (vmax_exceeded) {
		// # of sfiles exceeds maximum: delete oldest sfile.
		delete_oldest_sfile(path, md.vmax);
	}
	else {
		md.vcount++;
	}

	/* Update metadata file with new information. */
	lseek(meta_fd, 0, SEEK_SET);
	res = write(meta_fd, &md, sizeof(struct metadata));
	close(meta_fd);

	return res;
}

static int snap(const char *path)
{
	#ifdef DEBUG
		printf("In snap for %s\n", path);
	#endif

 	int res;

	/* Get size of file being "snapped" */
	int old_fd = get_sdir_file_fd(path);
	off_t old_sz = lseek(old_fd, 0, SEEK_END);

	/* Get contents of file being "snapped" */
	char old_buf[old_sz];
	res = read(old_fd, &old_buf, old_sz);
	if (res < 0) {
		fprintf(stderr, "Error while making snapshot %d\n", errno);
		return res;
	}

	/* Create new sfile with same contents */
	char *next_path = get_next_ver(path);

	#ifdef DEBUG
	printf("Next path is %s\n", next_path);
	#endif

	int new_fd = open(next_path, O_CREAT | O_WRONLY, S_IRWXU);
	res = write(new_fd, old_buf, old_sz);
	if (res < 0) {
		printf("Error while making snapshot %d\n", errno);
		return res;
	}

	close(old_fd);
	close(new_fd);

	/* Update metadata's current vnum and vcount. */
	update_metadata(path, basename(next_path));

 	return res;
}

static int mk_metadata_file(const char* sdir_path)
{
	int res;

	// Create SDIR's metadata file
	char mpath[PATH_MAX];
	strcpy(mpath, sdir_path);
	strcat(mpath, METADATA_FILENAME);
	int meta_fd = creat(mpath, S_IRWXU | 0755);
	if (meta_fd < 0) {
		printf("trouble making the metadata file %d\n", errno);
		return meta_fd;
	}

	// Write metadata to file
	// TODO: make vmax and size_freq configurable with mkdir
	// (requires revisiting mk_sdir)
	struct metadata md;
		strcpy(md.curr_vnum, "1");
		md.vcount = 1;
		md.vmax = -1;
		md.size_freq = -1;
	res = open(mpath, O_RDWR);
	res |= write(meta_fd, &md, sizeof(struct metadata));
	res |= close(meta_fd);
	if (res < 0) {
		fprintf(stderr, "Error writing metadata.\n");
		return res;
	}

	return 0;
}

char *get_file_path(const char* sdir_path)
{
	char dir_cp[PATH_MAX];
	char base_cp[PATH_MAX];
	strcpy(dir_cp, sdir_path);
	strcpy(base_cp, sdir_path);
	char dir[PATH_MAX];
	char base[PATH_MAX];
	strcpy(dir, dirname((char *) dir_cp));
	strcpy(base, basename((char *) base_cp));

	char *file_path = malloc(PATH_MAX);
	strcpy(file_path, dir);
	strcat(file_path, "/");
	strcat(file_path, base+1);
	file_path[strlen(file_path)-strlen(SDIR_FILETYPE)] = '\0';

	return file_path;
}

static int mk_sdir(const char* path)
{
	#ifdef DEBUG
		printf("In mk_sdir\n");
	#endif
	int res;

	// Create an SDIR
	res = mkdir(path, S_IRWXU | 0755);
	if (res < 0)
		return res;

	#ifdef DEBUG
		printf("	Created SDIR: %s\n", path);
	#endif

	// Create first sfile in the SDIR directory
	char sfile_path[PATH_MAX];
	strcpy(sfile_path, path);
	strcat(sfile_path, "/1");
	res = creat(sfile_path, S_IRWXU | 0755);
	if (res < 0)
		return res;

	#ifdef DEBUG
		printf("	Created first sfile: %s\n",sfile_path);
	#endif

	// Create metadata file
	mk_metadata_file(path);

	// Create the corresponding file
	char *file_path = get_file_path(path);
	res |= open(file_path, O_CREAT | O_RDWR, 0755 | S_IRWXU);
	if (res < 0) {
		fprintf(stderr, "Error making SDIR's corresponding file %s.\n", file_path);
		return res;
	}

	#ifdef DEBUG
		printf("Created corresponding file: %s\n", file_path);
	#endif

	close(res);
	free(file_path);

	return 0;
}

/* FUSE methods */
//TODO: Test if this happens successfully!
static int studentfs_getattr(const char *path, struct stat *stbuf)
{
	int res;

	if (is_sdir(path)) {
		char *new_path = get_curr_verr_path(path);
		res = lstat(new_path, stbuf);
	} else {
		res = lstat(path, stbuf);
	}

	if (res == -1) {
		return -errno;
	}

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

	if (is_sdir_ftype(path)) {
		res = mk_sdir(path);
	}
	else {
		res = mkdir(path, mode);
		if (res == -1)
			return -errno;
	}

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
	if (res == -1) {
		return -errno;
	}
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

static int studentfs_utimens(const char *path, const struct timespec ts[2])
{
	int res;
	printf("path is in utimens %s", path);
	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd = -1;
	int create_flag = (fi->flags & O_CREAT) == O_CREAT;

	if (is_sdir(path) && access(path, F_OK) != -1) {
		// An SDIR exists, open a path to the current file
		fd = get_sdir_file_fd(path);
		if (fd < 0) {
			printf("Couldn't open hidden file, errno: %d\n", errno);
			return fd;
		}
	} else {
		// Open a normal file as usual.
		if (create_flag) {
			fd = open(path, fi->flags, S_IRWXU | 0755);
		} else {
			fd = open(path, fi->flags);
		}
		if (fd == -1)
			return -errno;
	}
	fi->fh = fd;
	return 0;
}

static int studentfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;

	if (is_sdir_ftype(path)) {
		// For unknown reasons, FUSE sometimes does not route throug our open method
		// within the mount point AFAWK thus far. Worth investigating more, but this
		// works for the time being.
		studentfs_open(path, fi);
		if (fi->fh < 0)
			return -errno;
		return 0;
	}

	fd = open(path, fi->flags, mode);
	if (fd < 0)
		return -errno;
	fi->fh = fd;

	return 0;
}

static int studentfs_read(const char *path, char *buf, size_t size, off_t offset,
		    struct fuse_file_info *fi)
{
	#ifdef DEBUG
		printf("In read\n");
	#endif
	int res;

	if (is_sdir(path)) {
		int fd = get_sdir_file_fd(path);
		res = pread(fd, buf, size, offset);
	} else {
		res = pread(fi->fh, buf, size, offset);
	}
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
	#ifdef DEBUG
		printf("In write\n");
	#endif

	int res;

	if (is_sdir(path)) {
		int fd = get_sdir_file_fd(path);
		res = pwrite(fd, buf, size, offset);
		close(fd);
	} else {
		res = pwrite(fi->fh, buf, size, offset);
	}
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

void *
studentfs_init(struct fuse_conn_info *conn)
{
	/* Make aliases for bashscripts */
	//TODO: Update this ...
	int res = system("chmod u+x scripts/cmdscripts.sh");
	printf("result of chmod: %d\n", res);
	res |= system("./scripts/cmdscripts.sh");
	printf("result of running scripts: %d\n", res);

	if (res == -1)
		exit(1);

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
	.utimens	= studentfs_utimens,
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
	.setxattr	= studentfs_setxattr,
	.getxattr	= studentfs_getxattr,
	.listxattr	= studentfs_listxattr,
	.removexattr	= studentfs_removexattr,
	.flag_nullpath_ok = 1,
	.flag_utime_omit_ok = 1,
};

int main(int argc, char *argv[])
{
	umask(0);
	return fuse_main(argc, argv, &studentfs_oper, NULL);
}
