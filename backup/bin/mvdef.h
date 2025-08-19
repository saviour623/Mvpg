#ifndef _MVDEF_H
#define _MVDEF_H

#ifdef __GNUC__
#define _GNU_SOURCE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <math.h>

#define MVPROG_UINT8 int8_t;
#define MVPROG_INTTYPE int32_t

#define MVPROG_CHARTYPE unsigned char
#define MVPROG_STR char *
#define MVPROG_FILE unsigned int
#define MVPROG_SUCC 0
#define MVPROG_FAILURE -1024
#define MVPROG_FILENMAX 256 
#define MVPROG_PATHMAX
#define MVPROG_DEREFSYMLOOP 5
#define MVPROG_DEREFSYMLINK_PATH 40
#define MVPROG_DIRTYPE DIR *

#define MVPROG_HAS_PATHSEP 0x01
/* enable scaning of trailing chars in toInt */
#define  _STRIP_TRAILING_PREFIX 1
/* track calling stack */
#define MVPROG_RDPH_START 0x15
#define MVPROG_PREALLOC 0x14

extern char **environ;
#endif
