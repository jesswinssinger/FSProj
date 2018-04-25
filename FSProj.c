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

/* Important structs */
struct studentfs_dirp {
	DIR *dp;
	struct dirent *entry;
	off_t offset;
};

/*
 * TODO:
 * Test open
 * Test mk_sdir
 * Test read/write
 * Test get_curr_vnum
 * Test snap command
 * Test switch command
 * Test ver_changes
 * REFACTOR: Move snap, switch, mk_sdir to separate file
 * Make sdirs appear as files in call to getattr.
 */

/* Helper methods */
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
	// Make file readable
	//TODO : Debug all this
	/*int res = chmod(path, DIR_PERMS);
	if (res < 0) {
		printf("Couldn't change directory permissions in ver_changes\n");
		return res;
	}

	char *diff_str = malloc(strlen(buf)+30);
	char *diff_fmt = "echo %s | diff %s -";
	sprintf(diff_str, diff_fmt, buf, path);

	FILE *fp;
	fp = popen(diff_str);
	printf("fp:\n %s\n", fp);
	int res = chmod(path, REG_PERMS);
	if (res < 0) {
		printf("Couldn't change sdir permissions back to regular in ver_changes\n");
	}
	return something;*/
}

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
	chmod(path, DIR_PERMS);
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

	int res = getxattr(path, CURR_VNUM, curr_vnum, MAX_VNUM_LEN);
	if (res < 0) {
		printf("Error getting xattr %s, error is presumably that the wrong file was passed\n", CURR_VNUM);
		exit(0);
	}

	return _get_next_vnum(path, curr_vnum);
}

/* Create the file described by filename in the path to the sdir (path).
 *
 * Arguments:
 * path: path to sdir
 * filename: filename within the sdir located at path
 * buf: file contents to be written to the file at filename
 * fsize: number of bytes to write from buf.
 *
 * Return val: negative numbers if any system (or wrapper calls) return negative numbers.
 *
 */
int create_sdir_file(char *path, char *filename, char *buf, long fsize) {
	int res = 0;

	res |= chmod(path, DIR_PERMS);

	// Construct path to filename.
	char *sdir_file_path = malloc(strlen(path)+100);
	strcpy(sdir_file_path, path);
	strcat(sdir_file_path, "/");
	strcat(sdir_file_path, filename);

	// Write to the file at the path.
	FILE *f = fopen(sdir_file_path, "w+");
	res |= fwrite(buf, sizeof(char), fsize, f);
	res |= fclose(f);
	res |= chmod(sdir_file_path, REG_PERMS);

	// Add version number to xattrs in case user wants to rename a hidden file.
	// TODO: Only really necessary if we aren't doing sdir "appear as file" abstraction.
	setxattr(sdir_file_path, VNUM, filename, MAX_VNUM_LEN, 0);

	// TODO: This doesn't work to make something appear as a file.
	res |= chmod(path, REG_PERMS);
	free(sdir_file_path);
	return res;
}

/*
 * make the SDIR if it does not already exist.
 * If it does exist, return -1.
 * If there is a corresponding file, copy all the information into the snapshot "1"
 * otherwise, just create the directory and a blank file titled "1".
 */
int mk_sdir(const char *path) {
	long fsize = 0;
	char *buf = malloc(1024);
	int res;
	/* if there is a file, copy the contents into the buffer */
	if (access(path, F_OK) != -1) {
		FILE *f = fopen(path, "rb");
		fseek(f, 0, SEEK_END);
		fsize = ftell(f);
		fseek(f, 0, SEEK_SET);
		buf = realloc(buf, fsize+1);
		res = fread(buf, fsize, sizeof(char), f);
		if (res < 0) {
			printf("failed to read from file before creating sdir\n");
			return res;
		}
		fclose(f);

		int remove_res = remove(path);
		printf("remove_res is %d\n", remove_res);
		int unlink_res = unlink(path);
		printf("unlink res is %d\n", unlink_res);
	}
	/* Make the SDIR */
	res = mkdir(path, DIR_PERMS | O_CREAT);
	if (res < 0) {
		printf("failed to make SDIR directory\n");
		return res;
	}

	// Create the sdir file
	res = create_sdir_file((char *) path, "1", buf, fsize);
	if (res < 0) {
		return res;
	}
	free(buf);
	printf("after create sdir file\n");

	// Set the current vnum to be the first file
	char *ver_no = "1";
	res = setxattr(path, CURR_VNUM, ver_no, strlen(ver_no)+1, XATTR_CREATE);
	if (res < 0) {
		return res;
	}
	printf("after setxattr\n");

	res = chmod(path, REG_PERMS);
	if (res < 0) {
		return res;
	}

	return 0;
}

int is_sdir(const char *path) {
	char *value = malloc(1);
	//TODO: Can't we just check for CURR_VNUM xattr here and remove unnecessary
	// additional xattr?
	int res = getxattr(path, SDIR_XATTR, value, 0);
	free(value);
	return res >= 0 ? 1 : 0;
}

char *remove_SDIR_ftype(const char *path) {
	char *sdir_path = malloc(strlen(path)+1);
	if (!is_sdir_ftype(path)) {
		strcpy(sdir_path, path);
		return sdir_path;
	}
	sdir_path[strlen(path)-strlen(SDIR_FILETYPE)] = '\0';
	return sdir_path;
}

/* FUSE methods */
static int studentfs_getattr(const char *path, struct stat *stbuf)
{
	char *checked_path = remove_SDIR_ftype(path);
	int res;

	res = lstat(checked_path, stbuf);
	if (res == -1) {
		free(checked_path);
		return -errno;
	}

	if (is_sdir(checked_path)) {
		printf("inside is_sdir\n");
		stbuf->st_mode = REG_PERMS;
		printf("after changing mode\n");
	}

	free(checked_path);
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
	char *checked_path = remove_SDIR_ftype(path);

	res = access(checked_path, mask);
	if (res == -1)
		return -errno;

	free(checked_path);
	return 0;
}

static int studentfs_readlink(const char *path, char *buf, size_t size)
{
	int res;
	char *checked_path = remove_SDIR_ftype(path);

	res = readlink(checked_path, buf, size - 1);
	if (res == -1)
		return -errno;

	buf[res] = '\0';
	free(checked_path);
	return 0;
}

static int studentfs_opendir(const char *path, struct fuse_file_info *fi)
{
	int res;
	char *checked_path = remove_SDIR_ftype(path);

	struct studentfs_dirp *d = malloc(sizeof(struct studentfs_dirp));
	if (d == NULL)
		return -ENOMEM;

	d->dp = opendir(checked_path);
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
	char *checked_path = remove_SDIR_ftype(path);

	if (S_ISFIFO(mode))
		res = mkfifo(checked_path, mode);
	else
		res = mknod(checked_path, mode, rdev);
	if (res == -1)
		return -errno;
	free(checked_path);
	return 0;
}

/**
 * Used for when user wants to save current version permanently and save
 * new changes to a new copy.
 */
static int snap(const char *path)
{
	int res;
	/* Get base filename and sdir path. */
	char base[MAX_VNUM_LEN];
	char sdir_path[PATH_MAX];
	strcpy(base, path);
	strcpy(sdir_path, path);
	memcpy(base, basename(base), strlen(base) - sizeof(SNAP_SUFFIX)); // Remove .SNA
	dirname(sdir_path);

	/* BASED ON VERSIONING VS CHECKPOINTING DISCREP (SEE SNAP.SH)
	// Parse new filename and optional msg from base
	char* new_fname = strtok(base, ";");
	char* msg = strtok(NULL, ";");
	*/

	/* If there is a message, add it to current version. */
	if (base != NULL) {
		char curr_ver[MAX_VNUM_LEN];
		res = getxattr(sdir_path, CURR_VNUM, curr_ver, MAX_VNUM_LEN);
		if (res < 0) {
			#ifdef DEBUG
			printf("DEBUG: SDIR DOES NOT EXIST.\n");
			#endif

			return res;
		}

		// Get path to current version
		char curr_ver_path[PATH_MAX];
		strcpy(curr_ver_path, sdir_path);
		strcat(curr_ver_path, "/");
		strcat(curr_ver_path, curr_ver);

		setxattr(curr_ver_path, "msg", base, MAX_VMSG_LEN, 0);
		// TODO: SHOULD FILENAME BE VNUM? ALL FILES CONTAIN IT IN XATTR.
		// SWITCH THIS TO EITHER CHECKING CURR_VER (WHICH SHOULD BE A NAME)
		// OR LOOKING FOR HIDDEN FILES. (see snap.sh)

		printf("Message added to old version: %s\n", base);
	}

	/* Create a new version with the contents of the current version.
	   (CURR_VER should be updated in get_next_vnum method) */
	// TODO: what kind of naming scheme do we want for the visible versions?
	// Are they renameable? Does that defeat the purpose of msg?
	char new_ver_path[PATH_MAX];
	strcpy(new_ver_path, get_next_vnum(sdir_path));

	// TODO: once it works in create_sdir_file, use copy file code here
	// copy contents of current_ver_path to new_ver_path.

	printf("Archived this version of %s.\n", basename(sdir_path));
	return 0;
}

/* For "switch" command. Must be called in working sdir. */
static int switch_curr_verr(const char* path)
{
	char base[MAX_VNUM_LEN];
	char sdir_path[PATH_MAX];

	// Get base
	strcpy(base, path);
	memcpy(base, basename(base), strlen(base) - strlen(SWITCH_SUFFIX)); // Remove .SWI

	// Get directory path
	strcpy(sdir_path, path);
	dirname(sdir_path);

	// Parse new curr_vnum and optional msg from base
	char* new_curr_vnum = strtok(base, ";");
	char* msg = strtok(NULL, ";");

	return _switch_curr_verr(sdir_path, new_curr_vnum, msg);
}

/* Switch current version number based on user command
 */
static int _switch_curr_verr(const char* sdir_path, const char *new_curr_vnum,
	const char *msg)
{
	int res;
	//TODO: this relies on get_next_vnum working properly, i.e. branching
	// correctly from this vnum for future edits (if we care about that)...
	char old_verr_path[PATH_MAX];
	char old_verr_num[MAX_VNUM_LEN];

	res = getxattr(sdir_path, CURR_VNUM, old_verr_num, MAX_VNUM_LEN);
	if (res < 0)
		return res;

	strcpy(old_verr_path, sdir_path);
	strcat(old_verr_path, old_verr_num);

	res = setxattr(old_verr_path, MSG_XATTR, msg, MAX_VMSG_LEN, 0);

	if (res < 0)
		return res;

	res = setxattr(sdir_path, CURR_VNUM, new_curr_vnum, MAX_VNUM_LEN, 0);

	if (res < 0)
		return res;

	printf("Switched working version to %s.", new_curr_vnum);
	return res;
}

static int studentfs_mkdir(const char *path, mode_t mode)
{
	//TODO: Do we need to check if it has an sdir already?

	#ifdef DEBUG
	printf("in mkdir\n");
	printf("path is %s", path);
	printf("string being compared is %s", path+strlen(path)-strlen(SDIR_FILETYPE));
	#endif

	int res;

	if (is_sdir_ftype(path)) {
		char *checked_path = remove_SDIR_ftype(path);
		#ifdef DEBUG
		printf("DEBUG: MAKING SDIR for path %s \n", checked_path);
		#endif
		res = mk_sdir(checked_path);
		free(checked_path);
		if (res < 0) {
			return -errno;
		}
		return 0;
	}

	if (is_snap(path)) {
		snap(path);
	}

	else if (is_switch(path)) {
		switch_curr_verr(path);
	}

	res = mkdir(path, mode);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_unlink(const char *path)
{
	int res;
	char *checked_path = remove_SDIR_ftype(path);

	res = unlink(checked_path);
	free(checked_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_rmdir(const char *path)
{
	int res;
	char *checked_path = remove_SDIR_ftype(path);

	res = rmdir(checked_path);
	free(checked_path);
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
	// TODO: see if this can be of use in cleaning up the code.
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
	char *checked_path = remove_SDIR_ftype(path);
	int res;
	res = chmod(checked_path, mode);
	free(checked_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_chown(const char *path, uid_t uid, gid_t gid)
{
	int res;
	char *checked_path = remove_SDIR_ftype(path);

	res = lchown(checked_path, uid, gid);
	free(checked_path);
	if (res == -1)
		return -errno;

	return 0;
}

static int studentfs_truncate(const char *path, off_t size)
{
	int res;
	char *checked_path = remove_SDIR_ftype(path);

	res = truncate(checked_path, size);
	free(checked_path);
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
	char *checked_path = remove_SDIR_ftype(path);

	/* don't use utime/utimes since they follow symlinks */
	res = utimensat(0, checked_path, ts, AT_SYMLINK_NOFOLLOW);
	free(checked_path);
	if (res == -1)
		return -errno;

	return 0;
}
#endif

static int studentfs_open(const char *path, struct fuse_file_info *fi)
{
	int fd = -1;
	int create_flag = (fi->flags & O_CREAT) == O_CREAT;
	int path_is_sdir = is_sdir(path);

	/* Final path used to get the file descriptor for the file */
	if (create_flag && path_is_sdir) {
		printf("Tried to create an sdir when one was already created\n");
		return -1;
	}

	char file_path[PATH_MAX];

	if (is_sdir_ftype(path) && create_flag) {
		/* Path ends with .SDIR here */
		/* Create an sdir with no corresponding file */
		char *sdir_path = remove_SDIR_ftype(path);
		mk_sdir((const char *) sdir_path);

		strcpy(file_path, sdir_path);
		strcat(file_path, "/1");

		free(sdir_path);
	} else if (!create_flag && path_is_sdir) {
		/* Path does not end with .SDIR in this case */

		/* Open an fd to an sdir's contained file */
		/* TODO: Should we store "vnum" (which is really checkpoint num) in xattrs
		 * and keep checkpoints hidden with certain commands to navigate them,
		 * so easier for user to just navigate versions or "snaps" by filename?
		 * Think branches are the versions and commits are the checkpoints that are
		 * automatically created based on certain parameters.
		 */
		chmod(path, DIR_PERMS);
		char vnum[MAX_VNUM_LEN];
		getxattr(path, CURR_VNUM, vnum, sizeof(MAX_VNUM_LEN));

		strcpy(file_path, path);
		strcat(file_path, "/");
		strcat(file_path, vnum);

		chmod(path, REG_PERMS);
	} else {
		strcpy(file_path, path);
	}

	fd = open(file_path, fi->flags);
	if (fd == -1)
		return -errno;
	fi->fh = fd;

	return 0;
}

static int studentfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int fd;
	if (is_sdir_ftype(path)) {
		// For unknown reasons, FUSE sometimes does not route through our open method
		// within the mount point AFAWK thus far. Worth investigating more, but this
		// works for the time being.
		fi->flags |= O_CREAT;
		int res = studentfs_open(path, fi);
		if (res < 0)
			return -errno;
		return 0;
	}

	// Otherwise, system call.
	fd = open(path, fi->flags, mode);
	if (fd < 0)
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

// static int studentfs_read(const char *path, char *buf, size_t size, off_t offset,
// 		    struct fuse_file_info *fi)
// {
// 	int res;
// 	char new_path[2*MAX_VNUM_LEN];
// 	char temp[sizeof(SDIR_XATTR)];
// 	char vnum[MAX_VNUM_LEN];

// 	int is_sdir = getxattr(path, SDIR_XATTR, temp, sizeof(SDIR_XATTR));
// 	if (is_sdir != -1) {
// 		/* If the file has the xattr do the following:
// 		 * Make it accessible as a directory
// 		 */
// 		int perm_res = chmod(path, DIR_PERMS);
// 		if (perm_res == -1) {
// 			printf("Failed to change permissions to directory in read\n");
// 			return -errno;
// 		}

// 		/* Get the file name of the current version */
// 		perm_res = getxattr(path, CURR_VNUM, vnum, MAX_VNUM_LEN);
// 		if(perm_res == -1) {
// 			printf("Failed to get VNUM xattr in read\n");
// 			return -errno;
// 		}

// 		/* Construct path to that directory */
// 		strcpy(new_path, path);
// 		strcat(new_path, "/");
// 		strcat(new_path, vnum);

// 		/* Read from that file at that offset */
// 		FILE *curr_ver = fopen(new_path, "r");
// 		fseek(curr_ver, offset, SEEK_SET);
// 		res = fread(buf, sizeof(char), size, curr_ver);
// 		if (res == -1) {
// 			printf("Failed to read in read\n");
// 			return -errno;
// 		}

// 		/* Make the directory appear as a file again */
// 		perm_res = chmod(path, REG_PERMS);
// 		if (perm_res < 0) {
// 			printf("Failed to change permissions to file in read\n");
// 			return perm_res;
// 		}
// 	} else {
// 		/* Otherwise treat files as normal */
// 		(void) path;
// 		res = pread(fi->fh, buf, size, offset);
// 		if (res == -1)
// 			res = -errno;
// 	}

// 	return res;
// }

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

// static int studentfs_write(const char *path, const char *buf, size_t size,
// 		     off_t offset, struct fuse_file_info *fi)
// {
// 	int res;
// 	char *val = malloc(sizeof(SDIR_XATTR));
// 	if (getxattr(path, SDIR_XATTR, val, 0) >= 0) {
// 		char *curr_ver = malloc(MAX_VNUM_LEN);
// 		int sz = getxattr(path, CURR_VNUM, curr_ver, MAX_VNUM_LEN);
// 		if (sz < 0) {
// 			return sz;
// 		}

// 		/* Construct path to current/potentially previous version */
// 		char *prev_ver_path = malloc(MAX_VNUM_LEN*2);
// 		strcpy(prev_ver_path, path);
// 		strcat(prev_ver_path, "/");
// 		strcat(prev_ver_path, curr_ver);

// 		/* Make underlying files accessible */
// 		int sdir_perms = chmod(path, DIR_PERMS);
// 		if (sdir_perms < 0) {
// 			return sdir_perms;
// 		}

// 		/* Construct path to new file -- May be deleted after diffing files */
// 		char *next_vnum = get_next_vnum(path);
// 		char *next_ver_path = malloc(MAX_VNUM_LEN*2);
// 		strcpy(next_ver_path, path);
// 		strcat(next_ver_path, "/");
// 		strcat(next_ver_path, next_vnum);


// 		/* Make the new file */
// 		FILE *next_ver = fopen(next_ver_path, "w+");
// 		FILE *prev_ver = fopen(prev_ver_path, "r");

// 		/* Write the contents of the previous file to the next */
// 		fseek(prev_ver, 0L, SEEK_END);
// 		int prev_sz = ftell(prev_ver);
// 		fseek(prev_ver, 0L, SEEK_SET);
// 		char *prev_buf = malloc(prev_sz);
// 		int read_res = fread(prev_buf, sizeof(char), prev_sz, prev_ver);
// 		int write_res = fwrite(prev_buf, sizeof(char), prev_sz, next_ver);
// 		if (read_res != write_res) {
// 			printf("Didn't write previous version of file to next version properly\n");
// 			return -1;
// 		}
// 		/* Apply the current changes to the file */
// 		fseek(next_ver, offset, SEEK_SET);
// 		fwrite(buf, sizeof(char), size, next_ver);

// 		fclose(next_ver);
// 		fclose(prev_ver);

// 		/*
// 		 * TODO:
// 		 * Make number of changes before creating a new version file specific (xattr)
// 		 * Use diff to compute the differences instead of using the size of the write
// 		 */

// 		/* Get the number of changes made previously to the current version */
// 		// TODO: All SDIR's need this xattr by default.
// 		char *chng_xattr = malloc(MAX_VNUM_LEN);
// 		int chng_res = getxattr(path, NUM_CHANGES_XATTR, chng_xattr, MAX_VNUM_LEN);
// 		if (chng_res < 0) {
// 			printf("Trouble getting # of changes xattr in write\n");
// 			return chng_res;
// 		}
// 		int prev_changes = atoi(chng_xattr);

// 		/* Get the number of changes made between the files with diff */
// 		int curr_changes = ver_changes(prev_ver_path, next_ver_path);
// 		// TODO: Reset the number of changes after each new version
// 		if (prev_changes && (curr_changes + prev_changes > 2*MAX_NO_CHANGES)) {
// 			/*
// 			 * Write two files if there were previous changes and the new changes on top
// 			 * of the old changes will go over the size of the maximum number of changes.
// 			 */
// 			char *next_next_vnum = _get_next_vnum(path, next_vnum);
// 			char *next_next_path = malloc(2*MAX_VNUM_LEN);
// 			strcpy(next_next_path, path);
// 			strcat(next_next_path, "/");
// 			strcat(next_next_path, next_next_vnum);

// 			FILE *next_next_ver = fopen(next_next_path, "w+");
// 			next_ver = fopen(next_ver_path, "r");

// 			/* Write the contents of the previous file to the next */
// 			fseek(next_ver, 0L, SEEK_END);
// 			int next_sz = ftell(next_ver);
// 			fseek(next_ver, 0L, SEEK_SET);
// 			char *next_buf = malloc(next_sz);
// 			int read_res = fread(next_buf, sizeof(char), next_sz, next_ver);
// 			int write_res = fwrite(next_buf, sizeof(char), next_sz, next_next_ver);
// 			if (read_res != write_res) {
// 				printf("Didn't write previous version of file to next version properly\n");
// 				return -1;
// 			}

// 			/* Update the xattr to be the newest vnum*/
// 			int set_res = setxattr(path, CURR_VNUM, next_next_vnum, strlen(next_next_vnum)+1, 0);
// 			if (set_res < 0) {
// 				printf("Couldn't update setxattr\n");
// 				return set_res;
// 			}
// 			set_res = setxattr(path, NUM_CHANGES_XATTR, "0", 2, 0);
// 			if (set_res < 0) {
// 				printf("Couldn't update setxattr\n");
// 				return set_res;
// 			}
// 			fclose(next_ver);
// 			fclose(next_next_ver);
// 		} else if (curr_changes + prev_changes > MAX_NO_CHANGES) {
// 			/*
// 			 * Write one file if the new changes put the maximum number of changes over the
// 			 * limit of changes allotted for the file.
// 			 */
// 			int set_res = setxattr(path, CURR_VNUM, next_vnum, strlen(next_vnum)+1, 0);
// 			if (set_res < 0) {
// 				printf("Couldn't update setxattr\n");
// 				return set_res;
// 			}
// 			// TODO: Write number of changes
// 		} else {
// 			/*
// 			 * Write to the old version of the file if there are not enough changes to trigger
// 			 * the creation of a new file.
// 			 * TODO: Delete the old version of the file
// 			 */
// 			if(remove(next_ver_path) != 0) {
// 				printf("Couldn't delete new file\n");
// 			}

// 			prev_ver = fopen(prev_ver_path, "w");
// 			fseek(prev_ver, offset, SEEK_SET);
// 			res = fwrite(buf, sizeof(char), size, prev_ver);
// 			fclose(prev_ver);
// 		}

// 	}
// 	(void) path;
// 	res = pwrite(fi->fh, buf, size, offset);
// 	if (res == -1)
// 		res = -errno;

// 	return res;
// }


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
	char *checked_path = remove_SDIR_ftype(path);

	res = statvfs(checked_path, stbuf);
	free(checked_path);
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
	char *checked_path = remove_SDIR_ftype(path);
	int res = lsetxattr(path, name, value, size, flags);
	free(checked_path);
	if (res == -1)
		return -errno;
	return 0;
}

static int studentfs_getxattr(const char *path, const char *name, char *value,
			size_t size)
{
	char *checked_path = remove_SDIR_ftype(path);
	int res = lgetxattr(checked_path, name, value, size);

	if (res == -1)
		return -errno;
	return res;
}

static int studentfs_listxattr(const char *path, char *list, size_t size)
{
	char *checked_path = remove_SDIR_ftype(path);
	int res = llistxattr(path, list, size);
	free(checked_path);

	if (res == -1)
		return -errno;
	return res;
}

static int studentfs_removexattr(const char *path, const char *name)
{
	char *checked_path = remove_SDIR_ftype(path);
	int res = lremovexattr(path, name);
	free(checked_path);

	if (res == -1)
		return -errno;
	return 0;
}

void *
studentfs_init(struct fuse_conn_info *conn)
{
	/* Make aliases for bashscripts */
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
#if HAVE_UTIMENSAT
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
	.setxattr	= studentfs_setxattr,
	.getxattr	= studentfs_getxattr,
	.listxattr	= studentfs_listxattr,
	.removexattr	= studentfs_removexattr,
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
