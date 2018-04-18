#include "sys/stat.h";
#include "stdlib.h"
#define SDIR_FTYP ".sdr"
#define _SDIR_XATTR "IS_SDIR"
#define _VNUM "SDIR_VNUM"
#define _VCOUNT "SDIR_VCOUNT"
#define _MAX_VNUM_LEN 4096
#define _REG_O_DIR_MODE "RD_WR"
#define _REG_VAL 
#define _DIR_VAL

typedef struct SDir {
    size_t size;
    /* Strings of the form "a[.b[.c[...]]]" where {a,b,c,...}
     * are positive natural numbers.
     */
    // TODO: write a grammar that checks it as valid
    char *ver;
    sdir *children;
};

typedef struct SuperSDir {
    mode_t mode;
    int v_count;    
    char *v_num;
    char *file_name;
    sdir *root;
};