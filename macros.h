#include "consts.h"

#define is_sdir_ftype(path) !strcmp(path+strlen(path)-strlen(SDIR_FILETYPE), SDIR_FILETYPE)
#define is_snap(path) !strcmp(path+strlen(path)-strlen(VER_SUFFIX), VER_SUFFIX)
#define is_switch(path) !strcmp(path+strlen(path)-strlen(SWITCH_SUFFIX), SWITCH_SUFFIX)
/* Helper Functions */
static int snap(const char* path);
static int switch_curr_verr(const char* path);
