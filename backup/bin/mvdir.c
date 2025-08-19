#include <ftw.h>
#include "mvdef.h"

#define MVPROG_END_OF_MACRO_FUNC(...) (void)0
#define MVPROG_RECALL_FUNC(func, ...) func(__VA_ARGS__)
#define MVPROG_PERROR_RET(cond, str, ...)\
    do {\
	if ((cond))\
	    { perror(str); return __VA_ARGS__; }	\
    } while (0)
#define MVPROG_MAX_MEM_ALLOC (INT_MAX - 1)
#define mvprog_do_nothing() (void)0


void *mvprog_malloc(void *ptr, size_t n, int reAlloc) {
    void *mp = NULL;

    if (n < 1) {
	return NULL;
    }
    if (ptr == NULL) {
	mp = malloc(n);
    }
    else if (reAlloc) {
	mp = realloc(ptr, n);
    }
    /* couldn't allocate memory */
    if (mp == NULL) {
	fprintf(stderr, "malloc: out of memory");
	return NULL;
    }
    ptr = mp;
    return mp;
}

MVPROG_STR mvprog_getcwd (MVPROG_STR buf, ssize_t *bufsz) {
    MVPROG_STR gtcwd = NULL;
    MVPROG_STR cwd;
    MVPROG_STR *env_ptr;
    int8_t is_null = (buf == NULL);
    register ssize_t len = 0;

    if (!bufsz || ((*bufsz < 0) && !is_null) || (*bufsz >= MVPROG_MAX_MEM_ALLOC)) {
	/* bufsz must be set correctly to the size allocated to buf */
	fprintf(stderr, "mvpg>mvprog_getcwd: invalid buffer size");
	return (MVPROG_STR)0;
    }

    env_ptr = environ;
    while ((gtcwd = *env_ptr++)) {
#if !__assert_pwd
#define __assert_pwd(c) (((c)[0] == 'P') && ((c)[1] == 'W') && ((c)[2] == 'D') && ((c)[3] == '='))
	if (__assert_pwd(gtcwd)) {
	    gtcwd += 4;
	    break;
	}
#undef assert_pwd
#endif
    }
    if ((gtcwd == NULL) || (*gtcwd == '\0')) {
	if (is_null) {
	    cwd = (MVPROG_STR)mvprog_malloc(NULL, PATH_MAX, 0);
	    if (cwd == NULL)
		return (MVPROG_STR)0;
	    *bufsz = PATH_MAX;
	}
	else
	    cwd = buf;
    re:
	errno = 0;
	if ((getcwd(cwd, PATH_MAX) == NULL)) {
	    if (errno == ERANGE) {
		cwd = (MVPROG_STR)mvprog_malloc(buf, (len = *bufsz + MVPROG_FILENMAX), 1);
		if (cwd == NULL)
		    return (MVPROG_STR)0;

		*bufsz = len;
		goto re;
	    }
	    MVPROG_PERROR_RET(true, "mvprog_getcwd", (MVPROG_STR)0);
	}

	/* "(unreachable)" attached by linux(>2.6.36) when accessing a dir below root of the process */
	if (*cwd == '(') {
	    fprintf(stderr,"mvprog_getcwd: unreachable");
	    return (MVPROG_STR)0;
	}
    }
    else {
	len = strlen(gtcwd);

	if (is_null) {
	    cwd = (MVPROG_STR)mvprog_malloc(NULL, len + 1, 0);

	    if (cwd == NULL)
		return (MVPROG_STR)0;
	    *bufsz = len;
	}
	else if (len > *bufsz) {
	    cwd = mvprog_malloc(buf, len + 1, 1);
	    if (cwd == NULL)
		return (MVPROG_STR)0;
	    *bufsz = len;
	}
	if (memcpy(cwd, gtcwd, len) == NULL) {
	    fprintf(stderr, "mvprog_getcwd: strcpy: unable to copy");
	    return (MVPROG_STR)0;
	}
	cwd[len] = 0;
    }
    buf = cwd;
    return cwd;
}
#define MVPROG_BKSZ 1024
#define MVPROG_RDFL_ROUT 0xff
#define MVPROG_RDFL_RIN 0xfe
#define MVPROG_SYMCURR_DIR "."
#define MVPROG_SYMPREV_DIR ".."

#ifdef __MVPG_USENATIVE
#define getcwd __mvprog_getcwd
#else
#define getcwd getcwd
#endif
#ifdef PDIR
#define __MVPG_RPDIR__ PDIR
#undef PDIR
#endif
#if !defined(PATH_MAX)
#define PATH_MAX pathconf(_PC_PATH_MAX)
#endif
#if !__MVPG_ERRLOGGER__
#define log_error(err) perror(err)
#else
#define __tostrtok(tok) #tok
#define log_error(err)(__func__, __tostrtok(__line__ ## : ## __counter__), err)
#undef __tostrtok
#endif
#if __GNUC__
#define __FORCE_INLINE__ __attribute ((always_inline))
#else
#define __FORCE_INLINE__
#endif
#if !defined(O_NDELAY) && defined(O_NONBLOCK)
#define O_NDELAY O_NONBLOCK
#endif
#if ! _HAVE_RDLW_OPENDIR
#define RD_NBLOCK_NFOLL_MODE (O_RDONLY | O_NDELAY | O_NOFOLLOW)
#define rdlw_opendir(fd, fn, pd)					\
    (((fd) = open((fn), O_DIRECTORY | RD_NBLOCK_NFOLL_MODE)) != -1 ? ((pd) = fdopendir(fd)) : 0)
#endif
#define  S_DOTREF_2DOTREF(s)						\
    (((s) != NULL) && ((*(s) != '\0')) && (strlen(s) <= 2) ? (*(s) == '.' && *((s) + 1) == '.') || (*(s) == '.') : 0)

#define errp(crr_entry) char bv[1024]; sprintf(bv, "%s:%s", getcwd(NULL, 0), crr_entry); perror(bv);

__extension__ static __inline__ char * __FORCE_INLINE__  mvprog_rdpath(char *dir) {
    DIR *PDIR;
    struct dirent *dir_entry;
    register char *crr_entry, *proc_cwd;
    register ssize_t dir_crr_entry;
    register size_t rs = 0, ts = 0;
    register size_t rddir_stat = 0;
    int dirfd;

    DIR *bkfd[MVPROG_BKSZ];
    char *initcwd;

    if ((dir == NULL) || (*dir == 0)) {
	return NULL;
    }
    PDIR = opendir(dir);
    if (PDIR == NULL) {
	perror("mvprog>opendir");
	return NULL;
    }
    errno = 0;
    while ((dir_entry = readdir(PDIR)) || rddir_stat) {
	if (dir_entry == NULL) {
	    PDIR = bkfd[--rddir_stat];
	    if (rs && !ts) {
		if (chdir("..") == -1) {
		    perror("chdir");
		    return NULL;
		}
	    }
	    ts = 0;
	    rs = 1;
	    dir = ".";
	    continue;
	}
	crr_entry = dir_entry->d_name;
	
	if (S_DOTREF_2DOTREF(crr_entry)) {
	    continue;
	}
	/* some fs implementations provide a d_type in struct dirent */
#if defined(_BSD_SOURCE) || defined(_DIRENT_HAVE_D_TYPE)
	dir_crr_entry = (dir_entry->d_type == DT_DIR);
#else
#endif
	if (dir_crr_entry) {
	    /* printf("%d %s %s\n", rddir_stat, getcwd(NULL, 0), dir); */
	    if (!rs && !ts){
		if (chdir(dir)) {
		    perror("chdir");
		    return NULL;
		}
	    }
	    bkfd[ts ? --rddir_stat : rddir_stat] = PDIR;
	    ts = rs = 0;
	    if (! rdlw_opendir(dirfd, crr_entry, PDIR)) {
		if (errno == EACCES) {
		    PDIR = bkfd[rddir_stat];
		    rs = ts = 1;
		}
		else {
		    errp(crr_entry);
		    return NULL;
		}
	    }
	    dir = crr_entry;
	    rddir_stat += 1;
	}
	else {
	    printf("%s/%s\n", getcwd(NULL, 0), crr_entry);
	}
    }
#define PDIR __MVPG_RPDIR__
#undef __MVPG_RPDIR__
}

#if !defined(_STDINT_H) || !defined (INT32_MAX) || !defined (INT32_MIN)
typedef signed char int8_t;
typedef unsigned char uint8_t;
/* this is an assumption; int maybe 16 bit in some systems */ 
typedef int int32_t;
typedef unsigned int uint32_t;
/* However it's still neccessary to get the correct max and min */
#define INT32_WIDTH (sizeof (int))
#define INT32_MAX (int32_t)((1u << INT32_WIDTH) - 1)
#define INT32_MIN -(1u << INT32_WIDTH)
#define _MSB_AT32 (1u << INT32_WIDTH)
#endif
#undef U
#define U uint32_t
#ifndef _MSB_AT32
#define _MSB_AT32 0x80000000
#endif
#define TOINT_ERRCONV INT32_MIN
#if defined(_STDBOOL_H) || defined(__BOOL__)
#define CAST_TO_BOOL(exp) ((_Bool)(exp))
#else
#define CAST_TO_BOOL(exp) (!!(exp))
#endif
#define _ISASCII_NUM(c) (((c) ^ 0x30) < 0x0a)
#define _INT32_OVERFLOW(n) CAST_TO_BOOL((U)n & (U)(_MSB_AT32))
#define _SCAN_OPREFIX(c) && (c ^ 48)
#define _SPCNUM_SGN(c)						\
    (!(((c) ^ 45) & ((c) ^ 43) & ((c) ^ 32) _SCAN_OPREFIX(c)))
#define _TURNOFF_SGN 0x3f
#define _TURNON_SGN 0x40
#if ! _STRIP_TRAILING_PREFIX
#define  _STRIP_TRAILING_PREFIX 0
#endif
#ifndef _SKIP
#define _SKIP()
#endif

__extension__ static __inline__ int32_t __FORCE_INLINE__ toInt(char *s) {
    register U num = 0;
    register uint8_t c = 0, sn = 0;

    if (s == NULL || *s == 0)
	goto nullRoverflow;
#if _STRIP_TRAILING_PREFIX == 1
    /* strip +, -, ' ', 0 prefix(es) in string */
    for (; (c = *s) && _SPCNUM_SGN((U)c); s++) {
	/* allow repetition of only one prefix; */
	if (sn && ((sn & _TURNOFF_SGN) != c) && (c != 0x30))
	    goto nullRoverflow;
	/* store the sign bit at the 7th bit. */
	sn |= !sn ? c | ((U)(c == 0x2d) << 6) : c;
    }
    /* retrieve only the 7th bit */
    sn &= _TURNON_SGN;
#else
#define _X_OR_Y_AND_Z(x, y, z) (((x) || (y)) && (z))
    /* increment str if its first char is a sign (+ or -) */
    (void) _X_OR_Y_AND_Z((sn = (U)c == 0x2d), ((U)c == 0x2d), s++);
#endif
    /* to int */
    for (; (c = *s) &&  _ISASCII_NUM(c); s++) {
	num = ((num << 1) + (num << 3)) + ((U)c ^ 0x30);
	if (_INT32_OVERFLOW(num)) {
	nullRoverflow:
	    sn = !(num = (U)INT32_MAX + 1);
	    break;
	}
    }
    return sn ? -(int32_t)num : (int32_t)num;
}
/* we have no use scanning for preceding 0 */
#undef _SCAN_OPREFIX
#define _SCAN_OPREFIX(c)

__extension__ static __inline__ int8_t __FORCE_INLINE__ isInt(char *s) {
    register uint8_t c __attribute__((unused));

    if (s == NULL || *s == 0)
	return 0;
    /* strip +, -, ' ' prefix(es) in string; however the checks for trailing zero(s) is eliminated */
    for (; (c = *s) && _SPCNUM_SGN(c); s++) {
	_SKIP()
	    }
    /* !expr preserves the implication of the null byte from been treated as a false assertion */
    for (; (c = *s) && ((c = !_ISASCII_NUM(c)), !c); s++) {
	_SKIP();
    }
    return !c;
}

int nothing(const char *fpath, const struct stat *sb, int typeflag) {
    return puts(fpath);
}

MVPROG_INTTYPE main(int argc __attribute__((unused)), char **argv __attribute__((unused))) {
    if (argc < 2 || *argv[1] == '\0') {
	return 0;
    }
    if (argc > 3 && isInt(argv[1])) {
	if (toInt(argv[1]) == TOINT_ERROR)
    }
       switch (:
       case 1:*/
	
    printf("%d\n", toInt("70008"));
    
    //    puts(p);    //mvprog_rdpath("/sys");
    //ftw("/proc", nothing, 32000);
    return MVPROG_SUCC;
}
