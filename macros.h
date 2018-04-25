#include "consts.h"

#define is_sdir_ftype(path) !strcmp(path+strlen(path)-strlen(SDIR_FILETYPE), SDIR_FILETYPE)
<<<<<<< HEAD
=======
#define is_snap(path) !strcmp(path+strlen(path)-strlen(VER_SUFFIX), VER_SUFFIX)

/* Helper Functions */
// static int snap(const char* path);
>>>>>>> origin/jess
