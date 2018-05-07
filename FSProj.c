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

/** TODOS
 *TODO: Separate source files:
 *	sdir.c (helpers for paths, is_sdir, mk sdir, mk metadata, etc)
 *	version.c (snap, switch, ver changes, delete file, update metadata, etc.)
 *	or just include all of thse in one file
 * TODO: file deleted -> delete .SDIR
 * TODO: .SDIR deleted -> load contents into the file
 * TODO: check through document we sent to Kuenning and
 * TODO: create a tree visualization
*/

/* Helper methods */

char *get_sdir_path(const char *path)
{
	char *sdir_path = malloc(PATH_MAX);

	char base[PATH_MAX];
	char dir[PATH_MAX];
	strcpy(base, path);
	strcpy(dir, path);
	strcpy(base, basename(base));
	strcpy(dir, dirname(dir));

	strcpy(sdir_path, dir);
	strcat(sdir_path, "/.");
	strcat(sdir_path, base);
	strcat(sdir_path, SDIR_FILETYPE);
	return sdir_path;
}

char *get_metadata_path(const char *path)
{
	char *meta_path = get_sdir_path(path);
	strcat(meta_path, METADATA_FILENAME);

	#ifdef DEBUG
	printf("	Metadata path is %s\n", meta_path);
	#endif
	return meta_path;
}

char *get_file_path(const char* sdir_path)
{
	char *file_path = malloc(PATH_MAX);

	char dir[PATH_MAX];
	char base[PATH_MAX];
	strcpy(dir, sdir_path);
	strcpy(base, sdir_path);
	strcpy(dir, dirname(dir));
	strcpy(base, basename(base));

	strcpy(file_path, dir);
	strcat(file_path, "/");
	strcat(file_path, base+1);
	file_path[strlen(file_path)-strlen(SDIR_FILETYPE)] = '\0';

	return file_path;
}

char *get_curr_verr_path(const char *path)
{
	char *meta_path = get_metadata_path(path);
	FILE *meta_f = fopen(meta_path, "rb+");

	/* Get current version number from metadata. */
	char vnum[MAX_VNUM_LEN];
	int sz = fread(vnum, 1, MAX_VNUM_LEN, meta_f);
	if (sz <= 0) {
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

int get_sdir_file_fd(const char *path)
{
	char *curr_verr_path = get_curr_verr_path(path);
	int fd = open(curr_verr_path, O_RDWR);
	#ifdef DEBUG
		printf("Opened fd\n");
	#endif
	return fd;
}

int get_metadata(const char *path, struct metadata *md)
{
	int res;

	char *meta_path = get_metadata_path(path);
	FILE *meta_fp = fopen(meta_path, "rb+");

	res = fread(md, sizeof(struct metadata), 1, meta_fp);
	if (res == 0) {
		fprintf(stderr, "Trouble reading metadata file.\n");
		return -1;
	}

	return 0;
}

int is_sdir(const char *path)
{
	char path_w_sdir[PATH_MAX];

	char base[PATH_MAX];
	char dir[PATH_MAX];
	strcpy(base, path);
	strcpy(dir, path);
	strcpy(base, basename(base));
	strcpy(dir, dirname(dir));

	strcpy(path_w_sdir, dir);
	strcat(path_w_sdir, "/.");
	strcat(path_w_sdir, base);
	strcat(path_w_sdir, SDIR_FILETYPE);

	return access(path_w_sdir, F_OK) != -1;
}

char *remove_SDIR_ftype(const char *path)
{
	char *sdir_path = malloc(strlen(path)+1);
	strcpy(sdir_path, path);

	if (!is_sdir_ftype(path)) {
		return sdir_path;
	}

	sdir_path[strlen(path)-strlen(SDIR_FILETYPE)] = '\0';
	return sdir_path;
}

static int mk_metadata_file(const char* sdir_path)
{
	#ifdef DEBUG
	printf("Making metadata file\n");
	#endif
	int res;

	// Create metadata struct
	struct metadata md;
	strcpy(md.curr_vnum, "1");
	md.vcount = 1;

	int freq_fd = open(SDIR_INFO_PATH, O_RDWR);
	if (freq_fd == -1) {
		md.size_freq = -1;
		md.vmax = -1;
	}
	else {
		int len = lseek(freq_fd, 0, SEEK_END);
		char *buf = malloc(len);
		lseek(freq_fd, 0, SEEK_SET);
		res = read(freq_fd, buf, len);
		close(freq_fd);

		md.size_freq = atoi(strtok(buf, ";"));
		md.vmax = atoi(strtok(NULL, ";"));
	}

	// Create path for metadata
	char mpath[PATH_MAX];
	strcpy(mpath, sdir_path);
	strcat(mpath, METADATA_FILENAME);

	// Write metadata to file
	FILE *meta_fp = fopen(mpath, "wb+");
	res = fwrite(&md, sizeof(struct metadata), 1, meta_fp);
	if (res == 0) {
		fprintf(stderr, "Trouble making metadata file.\n");
		return -1;
	}
	fclose(meta_fp);

	return 0;
}

static int mk_sdir(const char* path)
{
	#ifdef DEBUG
	printf("In mk_sdir\n");
	#endif
	int res;

	char *fname = malloc(PATH_MAX);
	char *dir   = malloc(PATH_MAX);
	strcpy(fname, path);
	strcpy(dir, path);
	dir   = dirname(dir);
	fname = basename(fname);
	fname = strtok(fname, ".");

	// Check if file already exists. If so, we version this file.
	char *orig_file = malloc(PATH_MAX);
	strcpy(orig_file, dir);
	strcat(orig_file, "/");
	strcat(orig_file, fname);

	printf("orig_file is %s\n", orig_file);
	char *orig_buf = "";
	size_t orig_size = 0;

	if (access(orig_file, F_OK) != -1) {
		// File exists, copy contents
		int orig_res = 0;
		int orig_fd = open(orig_file, O_RDONLY);
		if (orig_fd < 0) {
			return -1;
		}
		orig_size = lseek(orig_fd, 0, SEEK_END);
		if (orig_size < 0) {
			return -1;
		}
		orig_res |= lseek(orig_fd, 0, SEEK_SET);
		if (orig_res < 0) {
			return -1;
		}
		orig_buf = malloc(orig_size);
		orig_res |= read(orig_fd, orig_buf, orig_size);
		if (orig_res < 0) {
			return -1;
		}
		orig_res |= close(orig_fd);
		if (orig_res < 0) {
			return -1;
		}
		printf("Made it to the end\n");
		sleep(3);
	}

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

	res = open(sfile_path, O_WRONLY);
	int write_sz = write(res, orig_buf, orig_size);
	if (write_sz < 0) {
		fprintf(stderr, "Couldn't write to file.\n");
	}
	close(res);

	#ifdef DEBUG
	printf("	Created first sfile: %s\n",sfile_path);
	#endif

	// Create metadata file
	mk_metadata_file(path);

	// Create corresponding file
	if (access(orig_file, F_OK) == -1) {
		char *file_path = get_file_path(path);
		res = open(file_path, O_CREAT | O_RDWR, 0755 | S_IRWXU);
		if (res < 0) {
			fprintf(stderr, "Error making SDIR's corresponding file %s.\n", file_path);
			return res;
		}

		#ifdef DEBUG
		printf("Created corresponding file: %s\n", file_path);
		#endif
		free(file_path);
	}

	close(res);

	return 0;
}

char *_get_next_ver(const char *path, char *vnum)
{
	#ifdef DEBUG
	printf("In _get_next_ver\n");
	#endif
	// Get the first part of the string, and the last number as a series of tokens
	char final_token[MAX_VNUM_LEN];
	char tokens[MAX_VNUM_LEN][MAX_VNUM_LEN];
	char vnum_branch[MAX_VNUM_LEN];

	#ifdef DEBUG
	printf("Allocated space for char*s\n");
	#endif

	// Split the tokens by the delimiter "."
	int token_i = 0;
	char *res = strtok(vnum, ".");
	#ifdef DEBUG
	printf("Res is %s\n", res);
	#endif

	if (res != NULL) {
		strcpy(tokens[token_i], res);
		token_i++;
		#ifdef DEBUG
		printf("	First token is %s\n", res);
		printf("	tokens[token_i] is %s\n", res);
		#endif
	}

	while ((res = strtok(NULL, ".")) != NULL) {
		#ifdef DEBUG
		printf("	Next token is %s\n", res);
		#endif
		strcpy(tokens[token_i], res);
		token_i++;
	}
	strcpy(final_token, tokens[token_i-1]);

	#ifdef DEBUG
	printf("	Final token is %s\n", final_token);
	printf("	token_i is %d\n", token_i);
	#endif

	// Build the part of the vnum "branch" before the final
	// delimited number (ie a.b.c.d -> a.b.c.)
	vnum_branch[0] = '\0';
	for(int i = 0; i < token_i - 1; i++) {
		strcat(vnum_branch, tokens[i]);
		strcat(vnum_branch, ".");
	}

	#ifdef DEBUG
	printf("	Vnum branch is %s\n", vnum_branch);
	#endif

	// Set up path with all but final delimited number.
	char *final_path = malloc(PATH_MAX);
	strcpy(final_path, path);
	strcat(final_path, "/");
	strcat(final_path, vnum_branch);

	#ifdef DEBUG
	printf("	Final path before last is %s\n", final_path);
	#endif

	// Increment the final vnum number by 1 to test for existence.
	int final_num = atoi(final_token);
	char final_num_str[MAX_VNUM_LEN];
	sprintf(final_num_str, "%d", final_num+1);
	strcat(final_path, final_num_str);

	// If there is already a child of the current directory,
	// make a new branch (see Wiki research if this is confusing)
	if (access(final_path, F_OK) != -1) {
		strcpy(final_path, path);
		strcat(final_path, "/");
		strcat(final_path, vnum_branch);
		strcat(final_path, final_token);
		strcat(final_path, ".1");
		while (access(final_path, F_OK) != -1) {
			final_path[strlen(final_path)-1] = '0';
			strcat(final_path, ".1");
		}
	}
	// If it falls through, then there does not exist a child.

	#ifdef DEBUG
	printf("	Final path is %s\n", final_path);
	#endif

	free(res);

	return final_path;
}

char *get_next_ver(const char *path)
{
	#ifdef DEBUG
	printf("Getting next version\n");
	#endif
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

	#ifdef DEBUG
	printf("	Current vnum is %s\n", curr_vnum);
	#endif

	close(mfd);

	return _get_next_ver(get_sdir_path(path), curr_vnum);
}

/* Helper for update_metadata(): deletes oldest sfile in sdir based on
 * modification time.
 * Metadata updates are all handled in update_metadata().
 */
//TODO: test this!
static int delete_oldest_sfile(const char* path, uint32_t vmax)
{
	int status;
	char command[39 + sizeof(int) + PATH_MAX];

	char *sdir_path = get_sdir_path(path);

	#ifdef DEBUG
	printf("	sdir_path is: %s\n", sdir_path);
	#endif
	sprintf(command, "pushd %s", sdir_path);
	status = system(command);
	if (status < 0)
		return status;

	// sprintf(command, "ls -1t | tail -n +%d | grep -v 'metadata'| xargs rm -f", vmax + 1);
	sprintf(command, "ls -1t | grep -v 'metadata' | tail -1 | xargs rm -f");
	#ifdef DEBUG
	printf("	Deleting oldest sfile: %s\n", command);
	#endif
	status = system(command);
	if (status < 0)
		return status;

	#ifdef DEBUG
	printf("	Successfully deleted file.\n");
	#endif

	status = system("popd");
	return status;
}

/* Increment vcount by 1 and update curr_vnum. If the new vcount is past vmax,
 * delete oldest checkpoint in the SDIR.
 */
static int update_metadata(const char* path, const char* new_curr_vnum)
{
	#ifdef DEBUG
	printf("Updating metadata: new_curr_vnum is %s\n", new_curr_vnum);
	#endif
	int res;

	/* Get metadata from metadata file. */
	struct metadata md;
	res = get_metadata(path, &md);
	if (res != 0)
		return res;

	#ifdef DEBUG
	printf("	Vmax is %d\n", md.vmax);
	#endif

	/* Update curr_vnum and vcount (if necessary) */
	strcpy(md.curr_vnum, new_curr_vnum);

	int vmax_exceeded = (md.vmax != -1) && ((md.vcount + 1) > md.vmax);
	if (vmax_exceeded) {
		#ifdef DEBUG
		printf("	Vmax (%d) exceeded!\n", md.vmax);
		#endif
		// # of sfiles exceeds maximum: delete oldest sfile.
		delete_oldest_sfile(path, md.vmax);
	}
	else {
		md.vcount++;
	}

	/* Update metadata file with new information. */
	FILE *meta_fp = fopen(get_metadata_path(path), "wb+");
	res |= fwrite(&md, sizeof(struct metadata), 1, meta_fp);
	if (res <= 0) {
		fprintf(stderr, "Trouble writing to metadata file.\n");
		return -1;
	}
	fclose(meta_fp);

	return res;
}

static int snap(const char *path)
{
	#ifdef DEBUG
	printf("In snap for %s\n", path);
	#endif

 	int res;

	/* Remove .SNA from filename */
	char fpath[PATH_MAX];
	strcpy(fpath, path);
	fpath[strlen(fpath) + 1 - sizeof(SNAP_SUFFIX)] = '\0';

	#ifdef DEBUG
	printf("Fpath is %s\n", fpath);
	#endif

	/* Get size of file being "snapped" */
	int old_fd = get_sdir_file_fd(fpath);
	off_t old_sz = lseek(old_fd, 0, SEEK_END);

	#ifdef DEBUG
	printf("Size of file is %o\n", (unsigned int) old_sz);
	#endif

	/* Get contents of file being "snapped" */
	char buf[old_sz];
	lseek(old_fd, 0, SEEK_SET);
	res = read(old_fd, &buf, old_sz);
	if (res < 0) {
		fprintf(stderr, "Error while making snapshot: %d\n", errno);
		return res;
	}

	close(old_fd);

	#ifdef DEBUG
	printf("Read %o bytes from old version\n", (unsigned int) res);
	#endif

	/* Create new sfile with same contents */
	char *next_path = get_next_ver(fpath);

	#ifdef DEBUG
	printf("Next path is %s\n", next_path);
	#endif

	int new_fd = open(next_path, O_CREAT | O_WRONLY, 0755 | S_IRWXU);
	res = write(new_fd, buf, old_sz);
	if (res < 0) {
		printf("Error while making snapshot %d\n", errno);
		return res;
	}

	close(new_fd);

	/* Update metadata's current vnum and vcount. */
	update_metadata(fpath, basename(next_path));

 	return res;
}

static int switch_current_version(const char *path)
{
	#ifdef DEBUG
	printf("In switch\n");
	#endif
	int res;

	/* Get new version number and filename. */
	char base[PATH_MAX];
	strcpy(base, path);
	strcpy(base, basename(base));
	base[strlen(base) + 1 - sizeof(SWITCH_SUFFIX)] = '\0';

	char fname[PATH_MAX];
	char new_vnum[MAX_VNUM_LEN];
	strcpy(fname, strtok(base, ";"));
	strcpy(new_vnum, strtok(NULL, ";"));

	#ifdef DEBUG
	printf("	fname is: %s\n", fname);
	printf("	new vnum is: %s\n", new_vnum);
	#endif

	/* Rebuild filename path. */
	char fpath[PATH_MAX];
	strcpy(fpath, path);
	strcpy(fpath, dirname(fpath));
	strcat(fpath, "/");
	strcat(fpath, fname);

	#ifdef DEBUG
	printf("	fpath is: %s\n", fpath);
	#endif

	/* Update metadata file. */
	struct metadata md;
	res = get_metadata(fpath, &md);
	if (res != 0)
		return res;

	strcpy(md.curr_vnum, new_vnum);

	FILE *meta_fp = fopen(get_metadata_path(fpath), "rb+");
	res = fwrite(&md, sizeof(struct metadata), 1, meta_fp);
	if (res == 0) {
		fprintf(stderr, "Trouble updating version number.\n");
		return 1;
	}
	fclose(meta_fp);

	return 0;
}


int ver_changes(char *curr_path, char *parent_path)
{
	int res;

	char *command = malloc(19 + PATH_MAX + PATH_MAX);
	sprintf(command, "diff %s %s >> diff.txt", curr_path, parent_path);
	res = system(command);
	if (res < 0)
		return res;

	FILE *diff_fp = fopen("diff.txt", "r");
	fseek(diff_fp, 0L, SEEK_END);
	int diff_size = ftell(diff_fp);
	fclose(diff_fp);

	free(command);
	return diff_size;
}


char *get_parent_path(char *path, char *curr_vnum) {
	char *parent_path = malloc(PATH_MAX);
	char *base = get_sdir_path(path);
	strcpy(parent_path, base);
	strcat(parent_path, "/");
	char *temp = strtok(curr_vnum, ".");
	char *temp2 = '\0';
	while (temp != NULL) {
		temp2 = strtok(NULL, ".");
		if (temp2 == NULL) {
			// last currently in temp
			break;
		}
		strcat(parent_path, temp);
		strcat(parent_path, ".");
		temp = temp2;
	}
	free(temp2);

	int final_num = atoi(temp);
	char final_num_str[PATH_MAX];
	sprintf(final_num_str, "%d", final_num-1);

	strcat(parent_path, final_num_str);
	return parent_path;
}

/* FUSE methods */
//TODO: Returns for snap and switch should be more elegant
static int studentfs_getattr(const char *path, struct stat *stbuf)
{
	int res;
	char *new_path;

	if (is_snap(path)) {
		printf("In getattr for %s\n", path);
		char fpath[PATH_MAX];
		strcpy(fpath, path);
		fpath[strlen(fpath) + 1 - sizeof(SNAP_SUFFIX)] = '\0';
		res = lstat(fpath, stbuf);
	}
	else if (is_switch(path)) {
		printf("In getattr for %s\n", path);

		/* Isolate fname */
		char *base = malloc(PATH_MAX);
		strcpy(base, path);
		base = basename((char*) base);
		base[strlen(base) + 1 - sizeof(SWITCH_SUFFIX)] = '\0';

		char *fname = malloc(PATH_MAX);
		char *new_vnum = malloc(MAX_VNUM_LEN);
		strcpy(fname, strtok(base, ";"));
		strcpy(new_vnum, strtok(NULL, ";"));

		/* Rebuild filename path. */
		char *fpath = malloc(PATH_MAX);
		strcpy(fpath, path);
		fpath = dirname((char*) fpath);
		strcat(fpath, "/");
		strcat(fpath, fname);

		res = lstat(fpath, stbuf);
	}
	else if (is_sdir(path)) {
		new_path = get_curr_verr_path(path);
		res = lstat(new_path, stbuf);
	}
	else {
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
	#ifdef DEBUG
	printf("In open for %s\n", path);
	#endif

	int fd = -1;
	int create_flag = (fi->flags & O_CREAT) == O_CREAT;
	int write_flag  = (fi->flags & O_WRONLY) == O_WRONLY;

	if (is_snap(path)) {
		printf("Is snap\n");
		snap(path);
		return 0;
	}
	else if (is_switch(path)) {
		printf("Is switch\n");
		switch_current_version(path);
		return 0;
	}
	else if (is_sdir(path) && access(path, F_OK) != -1) {
		printf("Is sdir\n");
		// An SDIR exists, open a path to the current file
		if (write_flag) {
			// Create the next copy for writing, so we can compare the two.
			char snap_path[PATH_MAX+4];
			strcpy(snap_path, path);
			strcat(snap_path, ".SNA");
			snap(snap_path);
		}
		fd = get_sdir_file_fd(path);
		if (fd < 0) {
			printf("Couldn't open hidden file, errno: %d\n", errno);
			return fd;
		}
	}
	else {
		printf("Other stuff\n");
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
	#ifdef DEBUG
	printf("In create\n");
	#endif
	int fd;

	if (is_sdir_ftype(path) || is_snap(path) || is_switch(path)) {
		// For unknown reasons, FUSE sometimes does not route throug our open method
		// within the mount point AFAWK thus far. Worth investigating more, but this
		// works for the time being.
		//TODO: Still necessary?
		studentfs_open(path, fi);
	} else {
		fd = open(path, fi->flags, mode);
		if (fd < 0)
			return -errno;
		fi->fh = fd;
	}
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
	}
	else {
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
		//TODO: is this sufficient? Does buf really hold everything
		// that needs to be written...?
		//ver_changes(path, buf, size);
		res = pwrite(fd, buf, size, offset);
		close(fd);
	}
	else {
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
	if (is_sdir(path)) {
		if ((fi->flags & O_WRONLY) == O_WRONLY) {
			struct metadata meta;
			get_metadata(path, &meta);

			// Get final number off of vnum
			int final_num = 0;
			char *temp = strtok(meta.curr_vnum, ".");
			while (temp != NULL) {
				final_num = atoi((const char *) temp);
				temp = strtok(NULL, ".");
			}

			// If there are no other children
			if (final_num > 1) {
				// Construct the parent's path
				char *parent_path = get_parent_path((char *) path, meta.curr_vnum);

				// Get most recent (currently open) version's file path
				char *child_path = get_curr_verr_path(path);
				int res = ver_changes(child_path, parent_path);

				// If new file's changes are too small
				if (res < meta.size_freq) {
					// that file must be removed and all changes written to parent file
					size_t child_sz = lseek(fi->fh, 0, SEEK_END);
					FILE *child = fopen(child_path, "r");
					if (child == NULL) {
						printf("couldn't open child file\n");
						return -1;
					}

					res = lseek(fi->fh, 0, SEEK_SET);
					if (res < 0) {
						printf("Couldn't seek from child file\n");
						return -errno;
					}

					char child_buf[child_sz];
					res = fread(child_buf, 1, child_sz, child);
					if (res < 0) {
						printf("Couldn't read from child file\n");
						return -errno;
					}

					// Write all of child_path's contents to parent_path with nothing inside it.
					FILE *parent = fopen(parent_path, "w+");
					if (parent == NULL) {
						printf("Couldn't find parent of current file\n");
						return -1;
					}
					fwrite(child_buf, 1, child_sz, parent);
					fclose(parent);
					close(fi->fh);
					char *parent_num = malloc(PATH_MAX);
					strcpy(parent_num, parent_path);
					parent_num = basename(parent_num);
					update_metadata(path, parent_num);
					remove((const char *) child_path);
					return 0;
				}
			}
			// Else it is fine, and we keep the current version.
		}
	}
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
