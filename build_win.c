// avoid including windows.h
int CreateDirectoryA(const char *path, void *);
int GetLastError(void);
void *CreateFileA(const char *path, int access, int share_mode, void *, int dispos, int flags, void *);
int GetFileTime(void *fd, uint64_t *creation, uint64_t *access, uint64_t *write);
int CloseHandle(void *);

#define GENERIC_READ 0x80000000
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_NORMAL 0x80
// MS-ERREF
#define EXISTS 0x000000B7

void mkdir_if_nexist(const char *dir, Error *err)
{
	int res = CreateDirectoryA(dir, NULL);
	if(res && GetLastError() != EXISTS) {
		fprintf(stderr, "Could not create '%s' directory!\n", dir);
		*err = ERROR_IO;
		goto RET;
	}
RET:
	return;
}

int file_is_newer(const char *a, const char *b)
{
	int ret = 0;

	void *a_fd = CreateFileA(a, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	void *b_fd = CreateFileA(b, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	uint64_t a_time;
	uint64_t b_time;

	if(!GetFileTime(a_fd, NULL, NULL, &a_time)) {
		ret = 1;
		goto RET;
	}
	if(!GetFileTime(b_fd, NULL, NULL, &b_time)) {
		ret = 1;
		goto RET;
	}

	ret = a_time > b_time;

RET:
	CloseHandle(a_fd);
	CloseHandle(b_fd);
	return ret;
}
