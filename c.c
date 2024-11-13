#include <fcntl.h>
#include "mvdef.h"

#define _GNU_SOURCE
#define S_DOTREF_2DOTREF(s)												\
	(((s) != NULL) && ((*(s) != '\0')) && (strlen(s) <= 2) ? (*(s) == '.' && *((s) + 1) == '.') || (*(s) == '.') : 0)

#define bool char
#define TRUE ((char)1)
#define FALSE ((char)0)
#define MVPROG_END_OF_MACRO_FUNC(...) (void)0

#define MVPROG_RECALL_FUNC(func, ...) func(__VA_ARGS__)
#define MVPROG_PERROR_RET(cond, str, ...) do { if ((cond)) { perror(str); return __VA_ARGS__; }} while (0)
#define MVPROG_MAX_MEM_ALLOC (INT_MAX - 1)

#define mvprog_do_nothing() (void)0

extern char **environ;

void *mvprog_malloc(void *ptr, size_t n, int reAlloc) {
	void *mp = NULL;

	if (n < 1) {
		return NULL;
	}
	if (ptr == NULL) {
		mp = malloc(n);
	}
	if (ptr && reAlloc) {
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
MVPROG_STR mvprog_getcwd (MVPROG_STR *buf, ssize_t *bufsz) {
	MVPROG_STR gtcwd = NULL;
	MVPROG_STR cwd = *buf;
	register ssize_t len = 0;
	unsigned char is_null = buf == NULL || *buf == NULL;

	if ((*bufsz < 0) && is_null)
		bufsz = 0;
	if (((*bufsz < 1) && is_null) || (*bufsz >= MVPROG_MAX_MEM_ALLOC)) {
		/* bufsz must be set correctly to the size allocated to buf */
		return (MVPROG_STR)0;
	}

	for (; (environ[len] != NULL); len++) {
#if !__assert_pwd
#define __assert_pwd(c)													\
		(((c)[0] && (c)[1] && (c)[2] && (c)[3]) && ((c)[0] == 'P') && ((c)[1] == 'W') && ((c)[2] == 'D') && ((c)[3] == '='))
		if (__assert_pwd(environ[len])) {
			gtcwd = (environ[len]) + 4;
			break;
		}
#undef assert_pwd
#endif
	}
	gtcwd = NULL;
	if ((gtcwd == NULL) || (*(gtcwd) == '\0')) {
		if (is_null) {
			cwd = (MVPROG_STR)mvprog_malloc(NULL, PATH_MAX, 0);
			if (cwd == NULL)
				return (MVPROG_STR)0;
			*bufsz = PATH_MAX;
		}
	re:
		errno = 0;
		if ((getcwd(cwd, PATH_MAX) == NULL)) {
			if (errno == ERANGE) {
				if ((MVPROG_STR)mvprog_malloc(cwd, 256, 1) == NULL)
					return (MVPROG_STR)0;
				bufsz += 256;
				goto re;
			}
			MVPROG_PERROR_RET(TRUE, "mvprog_getcwd", (MVPROG_STR)0);
		}
		/* "(unreachable)" attached by linux(>2.6.36) when accessing a dir below root of the process */
		if (*cwd == '(') {
			fprintf(stderr,"mvprog_getcwd: unreachable");
			return (MVPROG_STR)0;
		}
	}
	else {
		len = strlen(gtcwd) + 1;

		if (is_null) {
			cwd = (MVPROG_STR)mvprog_malloc(buf, len, 0);

			if (cwd == NULL)
				return (MVPROG_STR)0;
			*bufsz = len;
		}
		else if (len > *bufsz) {
			cwd = mvprog_malloc(*buf, len - *bufsz, 1);
			if (cwd == NULL)
				return (MVPROG_STR)0;
			*bufsz = len;
		}
		if (strcpy(cwd, gtcwd) == NULL) {
			fprintf(stderr, "mvprog_getcwd: strcpy: unable to copy");
			return (MVPROG_STR)0;
		}
		cwd[len] = 0;
	}
	return is_null ? cwd : (*buf = cwd), *buf;
}
void mvprog_read_dir(MVPROG_INTTYPE fd) {
	DIR *dir = NULL;
	struct dirent *entry;
	static char *c_entry_name;
	static char *cwd = (char *)NULL;
	static ssize_t alloc_mem = 0;
	struct stat statbuf __attribute__((unused));
	static int efd __attribute__((unused));
	static int recursive_count = 0;

	MVPROG_PERROR_RET(!(dir = fdopendir(fd)), "fdopendir");

	while ((entry = readdir(dir)) != NULL) {
		c_entry_name =  entry->d_name;
		if (S_DOTREF_2DOTREF(c_entry_name)) {
			continue;
		}
#define RECURSIVELY_CALL_MVPROG_READ_DIR(...)							\
		if (!alloc_mem) {												\
			if ((cwd = (char *)malloc(PATH_MAX)) == NULL) {				\
				fprintf(stderr, "malloc: could not allocate memory");	\
				exit(MVPROG_FAILURE);									\
			}															\
			alloc_mem = PATH_MAX;										\
		}																\
		/* store current working directory in cwd */					\
		MVPROG_PERROR_RET((getcwd(cwd, alloc_mem) == NULL), "getcwd");	\
		/* temporarily change directory to fd to directly access c_entry_name */ \
		MVPROG_PERROR_RET(fchdir(fd), "fchdir");						\
		efd = open(c_entry_name, O_DIRECTORY, S_IRWXU | S_IRGRP | S_IWGRP | S_IROTH); \
		MVPROG_PERROR_RET((efd == -1) && (errno != EEXIST), "open");	\
		recursive_count += 1;											\
		/* recursively call function */									\
		MVPROG_RECALL_FUNC(mvprog_read_dir, efd);						\
		MVPROG_END_OF_MACRO_FUNC(NULL)

#if defined(_BSD_SOURCE) || defined(_DIRENT_HAVE_D_TYPE)
		/* bsd systems and some gblic implementation provides a d_type assigned with the type of the file */
		if ((entry->d_type == DT_DIR)) {
			RECURSIVELY_CALL_MVPROG_READ_DIR(NULL);
		}
#else
		/* use stat to get file type */
		MVPROG_PERROR_RET(lstat(c_entry_name, &statbuf), "lstat");

		if (S_ISDIR(statbuf.st_mode)) {
			RECURSIVELY_CALL_MVPROG_READ_DIR(NULL);
		}
#endif
		else
			(void)0;//puts(c_entry_name);
	}

	if (recursive_count != 0) {
		MVPROG_PERROR_RET(chdir(cwd), "chdir");
		recursive_count -= 1;
		if (closedir(dir) == -1) {
			perror("closedir");
		}
	}
	else {
		cwd = cwd != (char *)0 ? free(cwd), (char *)0 : (char *)0;
		if (closedir(dir) == -1)
			perror("closedir");
	}
}
typedef struct {
	ssize_t aci_bflen;
	ssize_t aci_dirlen;
	ssize_t aci_trace_bflen;
	ssize_t aci_prog_info;
} _atstack_current_info;

#define mvprog_rdpath_macro(dir) mvprog_rdpath(dir, 0, 0, NULL)
char *mvprog_rdpath(char *dir, ssize_t blen, ssize_t len, ssize_t *ptr_blen) {
	DIR *PDIR;
	struct dirent *dir_entry;
	register char *crr_entry, *memp;
	register ssize_t len_crr_entry, dir_crr_entry, ep;
	register size_t prog_stat = 1;

	if ((dir == NULL) || (*dir == 0)) {
		return NULL;
	}
	PDIR = opendir(dir);
	if (PDIR == NULL) {
		perror("mvprog>opendir");
		return NULL;
	}
	if (ptr_blen == NULL) {
		ptr_blen = &blen;
	}
	errno = 0;

	while ((dir_entry = readdir(PDIR))) {
		crr_entry = dir_entry->d_name;

		if (S_DOTREF_2DOTREF(crr_entry)) {
			continue;
		}
		//some implementations provide a d_type in struct dirent
#if defined(_BSD_SOURCE) || defined(_DIRENT_HAVE_D_TYPE)
		dir_crr_entry = dir_entry->d_type == DT_DIR;
#else
		//alot of things changes
		char *statbuf = stat(crr_entry, statbuf);
#endif
		len_crr_entry = strlen(crr_entry);
		if (dir_crr_entry) {
			if (blen == 0) {
				len = strlen(dir);
				blen = len + len_crr_entry + 2;
				memp = malloc(blen);

				if (memp == NULL) {
					perror("mvprog>malloc");
					blen = len = 0;
					break;
				}
				//cpy1
				for (ep = 0; (memp[ep] = *dir); ep++, dir++)
					mvprog_do_nothing();
				memp[ep] = 0, dir = memp;
				memp = NULL;
			}
			prog_stat &= (size_t) dir[len - 1] != '/';
			ep = len + len_crr_entry + (prog_stat & MVPROG_HAS_PATHSEP);
			if (blen < ep) {
				memp = realloc(dir, ep + 1);
				if (memp == NULL) {
					perror("mvprog>realloc");
					break;
				}
				dir = memp, memp = NULL;
				blen = ep + 1;
			}
			//Append path prefix
			(void)(prog_stat & MVPROG_HAS_PATHSEP ? (dir[len] = '/'), len++ : 0);

            //Cpy2
			for (ep = len; (dir[ep] = *crr_entry++); ep++)
				mvprog_do_nothing();
			dir[ep] = 0;

			//save state
			*ptr_blen = blen;

			//Recursively expand till we get to the end of each tree
			memp = mvprog_rdpath(dir, blen, ep, ptr_blen);
			(void)(memp != NULL ? (dir = memp) : 0);

            //restore state
			if (ptr_blen != NULL) {
				blen = *ptr_blen;
			}
			ep = len, dir[ep] = 0;
		}
		else {
			//do work here
		}
	}

	if (closedir(PDIR)) {
		perror("mvprog>closedir");
		exit(MVPROG_FAILURE);
	}
	return dir;
}
MVPROG_INTTYPE main(int argc __attribute__((unused)), char **argv __attribute__((unused))) {
	int fd;
	if (mkdir("newdir", S_IRWXU | S_IRGRP) && (errno != EEXIST)) {
		perror("");
		exit(-1);
	}
	if ((symlink("newdir", "symlink_newdir")) && (errno != EEXIST)) {
		perror("");
		exit(-1);
	}
	if ((fd = open("/", O_DIRECTORY, S_IRWXU | S_IRGRP | S_IWGRP | S_IROTH)) && (errno != EEXIST)){
		puts("here");
		perror("open");
		return -1;
	}
	mvprog_rdpath_macro("/");
	//char *pd = malloc(8);
	//pd = realloc(pd, 2);
	return MVPROG_SUCC;
}
