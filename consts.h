#include <sys/stat.h>

#define CURR_VNUM "CURR_VNUM"
#define MAX_VNUM_LEN 1024
#define MAX_VMSG_LEN 50
#define SDIR_XATTR "SDIR_XATTR"
#define NUM_CHANGES_XATTR "NUM_CHANGES"
#define SDIR_FILETYPE ".SDIR"
#define VER_SUFFIX "VER"
#define DIR_PERMS 0755 | S_IFDIR
#define REG_PERMS 0755 | S_IRWXU
#define MAX_NO_CHANGES 50



// NEW STUFF
#define METADATA_FILENAME "/metadata"
