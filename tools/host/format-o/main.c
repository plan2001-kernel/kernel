/*
 * format-o: Linux host GE/O v1 formatter.
 *
 * It runs ge/format/ream.c::reamo() -- the real GE arena, log, B-tree,
 * block-hash and superblock code -- through a small Plan 9 libc shim.
 * It operates only on a caller-selected regular image file, never on a
 * block device; writing a physical partition remains the separate,
 * hardened tools/gcc-ovol-ream.sh --install path.
 *
 *   format-o [--seed DIR] --size MIB FILE
 *       create (or replace) FILE with a fresh MIB-MiB O volume, staged
 *       under a temporary name in the same directory and renamed only
 *       after a successful ream and fsync.
 *
 *   format-o [--seed DIR] FILE
 *       ream the existing regular FILE in place; its current size is
 *       used and it must be at least 128 MiB + one 16 KiB GE block.
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int o_format_run(char *path, char *seed);

static const char *prog = "format-o";

static void
usage(void)
{
	fprintf(stderr,
		"usage: format-o [--seed DIR] --size MIB FILE\n"
		"       format-o [--seed DIR] FILE\n");
	exit(2);
}

static unsigned long long
parse_mib(const char *s)
{
	char *e;
	unsigned long long v;

	errno = 0;
	v = strtoull(s, &e, 10);
	if(errno != 0 || e == s || *e != 0)
		usage();
	if(v < 129 || v > 1024*1024){
		fprintf(stderr, "%s: size must be between 129 and 1048576 MiB\n", prog);
		exit(2);
	}
	return v;
}

static int
regular_target(const char *path, int allow_missing)
{
	struct stat st;

	if(lstat(path, &st) < 0){
		if(errno == ENOENT && allow_missing)
			return 0;
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
		exit(1);
	}
	if(!S_ISREG(st.st_mode)){
		fprintf(stderr, "%s: %s: not a regular image file\n", prog, path);
		exit(1);
	}
	return 1;
}

static void
check_existing(const char *path)
{
	struct stat st;
	off_t min;

	if(!regular_target(path, 0)){
		fprintf(stderr, "%s: %s: no such file\n", prog, path);
		exit(1);
	}
	if(stat(path, &st) < 0){
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
		exit(1);
	}
	min = (off_t)128*1024*1024 + 16384;
	if(st.st_size < min){
		fprintf(stderr, "%s: %s: image is %lld bytes; at least %lld are required\n",
			prog, path, (long long)st.st_size, (long long)min);
		exit(1);
	}
}

static char *
temp_name(const char *path)
{
	char *p;
	size_t n;

	n = strlen(path) + sizeof ".tmp.XXXXXX";
	if((p = malloc(n)) == NULL){
		fprintf(stderr, "%s: out of memory\n", prog);
		exit(1);
	}
	snprintf(p, n, "%s.tmp.XXXXXX", path);
	return p;
}

int
main(int argc, char **argv)
{
	char *seed, *path, *tmp;
	unsigned long long mib;
	int i, fd, size_mode;

	seed = NULL;
	mib = 0;
	size_mode = 0;
	path = NULL;
	for(i = 1; i < argc; i++){
		if(strcmp(argv[i], "--seed") == 0){
			if(++i >= argc)
				usage();
			seed = argv[i];
		}else if(strcmp(argv[i], "--size") == 0){
			if(++i >= argc)
				usage();
			mib = parse_mib(argv[i]);
			size_mode = 1;
		}else if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0){
			usage();
		}else if(argv[i][0] == '-'){
			usage();
		}else{
			if(path != NULL)
				usage();
			path = argv[i];
		}
	}
	if(path == NULL)
		usage();
	if(seed != NULL){
		struct stat st;

		if(stat(seed, &st) < 0 || !S_ISDIR(st.st_mode)){
			fprintf(stderr, "%s: --seed %s: not a directory\n", prog, seed);
			return 1;
		}
	}

	if(!size_mode){
		check_existing(path);
		if(o_format_run(path, seed) < 0){
			fprintf(stderr, "%s: ream failed: %s\n", prog, path);
			return 1;
		}
		return 0;
	}

	regular_target(path, 1);
	tmp = temp_name(path);
	if((fd = mkstemp(tmp)) < 0){
		fprintf(stderr, "%s: %s: %s\n", prog, tmp, strerror(errno));
		free(tmp);
		return 1;
	}
	close(fd);
	if(truncate(tmp, (off_t)(mib*1024*1024)) < 0){
		fprintf(stderr, "%s: %s: cannot size image: %s\n", prog, tmp, strerror(errno));
		unlink(tmp);
		free(tmp);
		return 1;
	}
	if(o_format_run(tmp, seed) < 0){
		fprintf(stderr, "%s: ream failed; %s not installed\n", prog, path);
		unlink(tmp);
		free(tmp);
		return 1;
	}
	if(rename(tmp, path) < 0){
		fprintf(stderr, "%s: %s: %s\n", prog, path, strerror(errno));
		unlink(tmp);
		free(tmp);
		return 1;
	}
	free(tmp);
	return 0;
}
