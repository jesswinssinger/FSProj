#include "consts.h"

#define is_sdir_ftype(path) !strcmp(path+strlen(path)-strlen(SDIR_FILETYPE), SDIR_FILETYPE)
#define is_snap(path) !strcmp(path+strlen(path)-strlen(SNAP_SUFFIX), SNAP_SUFFIX)
#define is_switch(path) !strcmp(path+strlen(path)-strlen(SWITCH_SUFFIX), SWITCH_SUFFIX)
