#define CURR_VNUM "CURR_VNUM"
#define MAX_VNUM_LEN 1024
#define SDIR_XATTR "SDIR_XATTR"
#define SDIR_FILETYPE "SDIR"

#define is_sdir_ftype(path) !strcmp(path+strlen(path)-strlen(SDIR_FILETYPE), SDIR_FILETYPE)