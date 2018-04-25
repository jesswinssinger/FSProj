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
 * TODOs:
 * Get Read and Write functionality working.
 * Make sdir's appear as files in call to getattr.
 * 
 */ 
/* Helper methods */

char *get_sdir_path(const char *path){
	char *hidden_path = malloc(PATH_MAX+1);
	char *base_cp = malloc(PATH_MAX+1);
	strcpy(base_cp, path);
	char *dir_cp = malloc(PATH_MAX+1);
	strcpy(dir_cp, path);

	base_cp = basename(base_cp);
	dir_cp  = dirname(dir_cp);

	strcpy(hidden_path, dir_cp);
	strcat(hidden_path, "/.");
	strcat(hidden_path, base_cp);
	strcat(hidden_path, SDIR_FILETYPE);
	return hidden_path;
}

char *get_metadata_path(const char *path){
	char *hidden_path = get_sdir_path(path);
	strcat(hidden_path, METADATA_FILENAME);
	return hidden_path;
}

int get_sdir_file_fd(const char *path) {
	char *meta_path = get_metadata_path(path);
	printf("metapath is %s\n", meta_path);
	FILE *meta_f = fopen(meta_path, "rb+");
	char vnum[MAX_VNUM_LEN];
	int sz = fread(vnum, 1, MAX_VNUM_LEN, meta_f);
	if (sz < 0) {
		printf("Error reading from metadata %d\n", errno);
		return sz;
		// Fuck the user!
		// exit(0);
	}
	fclose(meta_f);
	free(meta_path);
	
	char *hidden_path = get_sdir_path(path);
	strcat(hidden_path, "/");
	strcat(hidden_path, vnum);
	int fd = open(hidden_path, O_RDWR);
	free(hidden_path);
	return fd;
}

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

char *remove_SDIR_ftype(const char *path) {
	char *sdir_path = malloc(strlen(path)+1);
	if (!is_sdir_ftype(path)) {
		strcpy(sdir_path, path);
		return sdir_path;
	}
	strcpy(sdir_path, path);
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
		
	// Construct path to filename.
	char *sdir_file_path = malloc(strlen(path)+MAX_VNUM_LEN);
	strcpy(sdir_file_path, path);
	strcat(sdir_file_path, "/");
	strcat(sdir_file_path, filename);

	// Write to the file at the path.
	int fd = open(sdir_file_path, O_RDWR | O_CREAT | O_TRUNC, S_IRWXU | 0755);
	if (fd < 0) {
		return fd;
	}
	if (fsize > 0) {
		res |= write(fd, buf, fsize);
		if (res < 0) {
			return res;
		}
	}
	res |= close(fd);

	free(sdir_file_path);
	return res;
}

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

/* 
 * make the SDIR if it does not already exist.
 * If it does exist, return -1.
 * If there is a corresponding file, copy all the information into the snapshot "1"
 * otherwise, just create the directory and a blank file titled "1". 
 */
int mk_sdir(char *path) {
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
	res = mkdir(path, S_IFDIR | 0755);
	if (res < 0) {
		printf("failed to make SDIR directory\n");
		return res;
	}
	char *dest = malloc(strlen(path)+10);
	strcpy(dest, path);
	strcat(dest, "/1");

	res = open(dest, O_CREAT | O_RDWR | O_TRUNC, 0755);
	// Create the sdir file
	//res = create_sdir_file(path, "1", buf, fsize);
	if (res < 0) {
		return res;
	}
	free(buf);
	printf("after create sdir file\n");
	
	// Set the current vnum to be the first file
	char *ver_no = "1";
	char value[strlen(SDIR_XATTR)+1];
	res = getxattr(path, SDIR_XATTR, value, strlen(SDIR_XATTR));

	res |= studentfs_setxattr(path, SDIR_XATTR, SDIR_XATTR, strlen(SDIR_XATTR), XATTR_CREATE);
	printf("error is %d\n", errno);
	res |= studentfs_getxattr(path, SDIR_XATTR, value, strlen(SDIR_XATTR));
	printf("second error is %d\n", errno);
	printf("xattr is %s\n", value);
	printf("res is %d\n", res);
	exit(0);
	res |= setxattr(path, CURR_VNUM, ver_no, strlen(ver_no), 0);
	if (res < 0) {
		return res;
	}
	printf("after setxattr\n");

	if (res < 0) {
		return res;
	}
	
	return 0;
}

/* FUSE methods */



static int studentfs_getattr(const char *path, struct stat *stbuf)
{
	int res;

	//TODO if sdir get recent version

	res = lstat(path, stbuf);
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

	//if (is_sdir_ftype(path)) {
	//	char base_cp[PATH_MAX];
	//	strcpy(base_cp, path);
	//	char *base = basename(base_cp);
	//	char dir_cp[PATH_MAX];
	//	strcpy(dir_cp, path);
	//	char *dir = basename(dir_cp);
	//	
	//	if (base_cp[0] != '.'){
	//		char new_path[PATH_MAX];
	//		strcpy(new_path, dir);
	//		strcat(new_path, "/.");
	//		strcat(new_path, base);
	//		res = access(new_path, mask);
	//	}
	//} else {
	//	res = access(path, mask);
	//}
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

// static int snap(const char *path)
// {
// 	int res;
// 	/* Get base filename and sdir path. */
// 	char* base = malloc(MAX_VNUM_LEN);
// 	char* sdir_path = malloc(PATH_MAX);
// 	strcpy(base, path);
// 	strcpy(sdir_path, path);
// 	base = memcpy(base, basename((char *) base), strlen(base) - 4); // Remove .VER
// 	sdir_path = dirname((char *) sdir_path);

// 	/* BASED ON VERSIONING VS CHECKPOINTING DISCREP (SEE SNAP.SH)
// 	// Parse new filename and optional msg from base
// 	char* new_fname = strtok(base, ';');
// 	char* msg = strtok(NULL, ';');
// 	*/

// 	/* If there is a message, add it to current version. */
// 	if (base != NULL) {
// 		char* curr_ver;
// 		res = getxattr(sdir_path, CURR_VNUM, curr_ver, MAX_VNUM_LEN);
// 		if (res < 0) {
// 			#ifdef DEBUG
// 			printf("DEBUG: SDIR DOES NOT EXIST.\n");
// 			#endif

// 			mk_sdir(sdir_path);
// 			return res;
// 		}

// 		// Get path to current version
// 		char* curr_ver_path;
// 		strcpy(curr_ver_path, sdir_path);
// 		curr_ver_path = strcat(curr_ver_path, "/");
// 		curr_ver_path = strcat(curr_ver_path, curr_ver);

// 		setxattr(curr_ver_path, "msg", base, MAX_VMSG_LEN, 0);
// 		// TODO: SHOULD FILENAME BE VNUM? ALL FILES CONTAIN IT IN XATTR.
// 		// SWITCH THIS TO EITHER CHECKING CURR_VER (WHICH SHOULD BE A NAME)
// 		// OR LOOKING FOR HIDDEN FILES. (see snap.sh)

// 		printf("Message added to old version: %s\n", base);
// 	}

// 	/* Create a new version with the contents of the current version. 
// 	   (CURR_VER should be updated in get_next_vnum method) */
// 	char* new_ver_path = get_next_vnum(sdir_path);

// 	//TODO: CALL CP COMMAND IN UNIX
// 	printf("New snapfile created.\n");
// 	return 0;
// }

static int studentfs_mkdir(const char *path, mode_t mode)
{
	//TODO: Do we need to check if it has an sdir already?
	// if (is_snap(path)) {
	// 	snap(path);
	// }
	printf("in mkdir\n");
	printf("path is %s", path);
	printf("string being compared is %s", path+strlen(path)-strlen(SDIR_FILETYPE));
	
	int res = mkdir(path, mode);
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
	free(path);
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

static int studentfs_open(const char *path, struct fuse_file_info *fi) {
	int fd = -1;
	int create_flag = (fi->flags & O_CREAT) == O_CREAT;
	int res = 0; 

	char path_w_sdir[PATH_MAX];
	strcpy(path_w_sdir, path);
	strcat(path_w_sdir, SDIR_FILETYPE);
	
	if (create_flag && is_sdir_ftype(path)) {
		char base_cp[PATH_MAX];
		strcpy(base_cp, path);
		// Create a hidden directory
		char *base = basename((char *) base_cp);
		
		char dir_cp[PATH_MAX];
		strcpy(dir_cp, path);
		char *dir  = dirname((char *) dir_cp);
		
		char hidden_path[PATH_MAX];
		strcpy(hidden_path, dir);
		strcat(hidden_path, "/");
		strcat(hidden_path, ".");
		strcat(hidden_path, base);
		
		res |= mkdir(hidden_path, S_IRWXU | 0755);
		
		// Create a file in the SDIR directory
		char hidden_file_name[PATH_MAX];
		strcpy(hidden_file_name, hidden_path);
		strcat(hidden_file_name, "/1");
		fd = creat(hidden_file_name, S_IRWXU | 0755);
		
		// Create a file with information on the SDir.
		char metadata_file[PATH_MAX];
		strcpy(metadata_file, hidden_path);
		strcat(metadata_file, METADATA_FILENAME);
		int meta_fd = creat(metadata_file, S_IRWXU | 0755);
		if (meta_fd < 0) {
			printf("trouble making the metadata file %d\n", errno);
			return meta_fd;
		}
		
		res |= write(meta_fd, "1", 2);
		res |= close(meta_fd);
		if (res < 0) {
			printf("error writing metadata\n");
			return res;
		}
		
		// Create the corresponding file
		char *path_wo_sdir = remove_SDIR_ftype(path);
		res |= open(path_wo_sdir, O_CREAT | O_RDWR, 0755 | S_IRWXU);
		if (res < 0) {
			printf("error making sdir \n");
			return res;
		}
		close(res);
		free(path_wo_sdir);

	} else if (is_sdir(path) && access(path, F_OK) != -1) {
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

// static int studentfs_read(const char *path, char *buf, size_t size, off_t offset,
// 		    struct fuse_file_info *fi)
// {
// 	int res;
// 	char* new_path = malloc(2*MAX_VNUM_LEN);
// 	char temp[sizeof(SDIR_XATTR)];
// 	char* vnum = malloc(MAX_VNUM_LEN);

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

// 	free(new_path);
// 	free(vnum);
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

//static int studentfs_write(const char *path, const char *buf, size_t size,
//		     off_t offset, struct fuse_file_info *fi)
//{
//	int res;
//
//	(void) path;
//	res = pwrite(fi->fh, buf, size, offset);
//	if (res == -1)
//		res = -errno;
//
//	return res;
//}

 static int studentfs_write(const char *path, const char *buf, size_t size,
 		     off_t offset, struct fuse_file_info *fi)
 {
 	int res;
 	char *val = malloc(sizeof(SDIR_XATTR));
 	if (getxattr(path, SDIR_XATTR, val, 0) >= 0) {
 		char *curr_ver = malloc(MAX_VNUM_LEN);
 		int sz = getxattr(path, CURR_VNUM, curr_ver, MAX_VNUM_LEN);
 		if (sz < 0) {
 			return sz;
 		}

 		/* Construct path to current/potentially previous version */
 		char *prev_ver_path = malloc(MAX_VNUM_LEN*2);
 		strcpy(prev_ver_path, path);
 		strcat(prev_ver_path, "/");
 		strcat(prev_ver_path, curr_ver);

 		/* Make underlying files accessible */
 		int sdir_perms = chmod(path, DIR_PERMS);
 		if (sdir_perms < 0) {
 			return sdir_perms;
 		}
		
 		/* Construct path to new file -- May be deleted after diffing files */
 		char *next_vnum = get_next_vnum(path);
 		char *next_ver_path = malloc(MAX_VNUM_LEN*2);
 		strcpy(next_ver_path, path);
 		strcat(next_ver_path, "/");
 		strcat(next_ver_path, next_vnum);

		
 		/* Make the new file */
 		FILE *next_ver = fopen(next_ver_path, "w+");
 		FILE *prev_ver = fopen(prev_ver_path, "r");
		
 		/* Write the contents of the previous file to the next */
 		fseek(prev_ver, 0L, SEEK_END);
 		int prev_sz = ftell(prev_ver);
 		fseek(prev_ver, 0L, SEEK_SET);
 		char *prev_buf = malloc(prev_sz);
 		int read_res = fread(prev_buf, sizeof(char), prev_sz, prev_ver);
 		int write_res = fwrite(prev_buf, sizeof(char), prev_sz, next_ver);
 		if (read_res != write_res) {
 			printf("Didn't write previous version of file to next version properly\n");
 			return -1;
 		}
 		/* Apply the current changes to the file */
 		fseek(next_ver, offset, SEEK_SET);
 		fwrite(buf, sizeof(char), size, next_ver);
		
 		fclose(next_ver);
 		fclose(prev_ver);
		
 		/*
 		 * TODO:
 		 * Make number of changes before creating a new version file specific (xattr)
 		 * Use diff to compute the differences instead of using the size of the write
 		 */

 		/* Get the number of changes made previously to the current version */
		//TODO: All SDIR's need this xattr by default.
 		char *chng_xattr = malloc(MAX_VNUM_LEN);
 		int chng_res = getxattr(path, NUM_CHANGES_XATTR, chng_xattr, MAX_VNUM_LEN);
 		if (chng_res < 0) {
 			printf("Trouble getting # of changes xattr in write\n");
 			return chng_res;
 		}
 		int prev_changes = atoi(chng_xattr);
		
 		/* Get the number of changes made between the files with diff */
 		int curr_changes = ver_changes(prev_ver_path, next_ver_path);
		//TODO: Reset the number of changes after each new version
 		if (prev_changes && (curr_changes + prev_changes > 2*MAX_NO_CHANGES)) {
 			/* 
 			 * Write two files if there were previous changes and the new changes on top
 			 * of the old changes will go over the size of the maximum number of changes. 
 			 */
 			char *next_next_vnum = _get_next_vnum(path, next_vnum);
 			char *next_next_path = malloc(2*MAX_VNUM_LEN);
 			strcpy(next_next_path, path);
 			strcat(next_next_path, "/");
 			strcat(next_next_path, next_next_vnum);

 			FILE *next_next_ver = fopen(next_next_path, "w+");
 			next_ver = fopen(next_ver_path, "r");

 			/* Write the contents of the previous file to the next */
 			fseek(next_ver, 0L, SEEK_END);
 			int next_sz = ftell(next_ver);
 			fseek(next_ver, 0L, SEEK_SET);
 			char *next_buf = malloc(next_sz);
 			int read_res = fread(next_buf, sizeof(char), next_sz, next_ver);
 			int write_res = fwrite(next_buf, sizeof(char), next_sz, next_next_ver);
 			if (read_res != write_res) {
 				printf("Didn't write previous version of file to next version properly\n");
 				return -1;
 			}

 			/* Update the xattr to be the newest vnum*/
 			int set_res = setxattr(path, CURR_VNUM, next_next_vnum, strlen(next_next_vnum)+1, 0);
 			if (set_res < 0) {
 				printf("Couldn't update setxattr\n");
 				return set_res;
 			}
 			set_res = setxattr(path, NUM_CHANGES_XATTR, "0", 2, 0);
 			if (set_res < 0) {
 				printf("Couldn't update setxattr\n");
 				return set_res;
 			}
 			fclose(next_ver);
 			fclose(next_next_ver);
 		} else if (curr_changes + prev_changes > MAX_NO_CHANGES) {
 			/*
 			 * Write one file if the new changes put the maximum number of changes over the
 			 * limit of changes allotted for the file.
 			 */
 			int set_res = setxattr(path, CURR_VNUM, next_vnum, strlen(next_vnum)+1, 0);
 			if (set_res < 0) {
 				printf("Couldn't update setxattr\n");
 				return set_res;
 			}
			//TODO: Write number of changes
 		} else {
 			/*
 			 * Write to the old version of the file if there are not enough changes to trigger
 			 * the creation of a new file.
 			 * TODO: Delete the old version of the file
 			 */
 			if(remove(next_ver_path) != 0) {
 				printf("Couldn't delete new file\n");
 			}

 			prev_ver = fopen(prev_ver_path, "w");
 			fseek(prev_ver, offset, SEEK_SET);
 			res = fwrite(buf, sizeof(char), size, prev_ver);
 			fclose(prev_ver);
 		}

 	}
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

/* xattr operations are optional and can safely be left unimplemented */


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