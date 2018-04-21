#include "consts.h"

#define is_sdir_ftype(path) !strcmp(path+strlen(path)-strlen(SDIR_FILETYPE), SDIR_FILETYPE)
