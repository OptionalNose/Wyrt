#include <sys/stat.h>
#include <errno.h>

void mkdir_if_nexist(const char *dir, Error *err)
{
	int res = mkdir(dir, 0777);
	if(res && errno != EEXIST) {
		fprintf(stderr, "Could not create build directory!\n");
		*err = ERROR_IO;
		goto RET;
	}
RET:
	return;
}

int file_is_newer(const char *a, const char *b)
{
	struct stat stat_a;
	struct stat stat_b;

	if(stat(a, &stat_a)) return true; // If we can't read the file time, default to recompile
	if(stat(b, &stat_b)) return true;

	return stat_a.st_mtime > stat_b.st_mtime;
}
