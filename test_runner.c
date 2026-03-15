#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
	const char *file;
	int exitcode;
	bool should_fail;
	const char *in;
	const char *out;
	int num;
} Test;

const Test tests[] = {
	#include "test_manifest"
};
const int test_count = (sizeof tests) / (sizeof tests[0]);

#define BRIGHT_GREEN "\x1b[92m"
#define RED "\x1b[31m"
#define BRIGHT_RED "\x1b[91m"
#define RESET "\x1b[0m"

#ifdef _WIN32
#define COMPILER "C:/msys64/msys2_shell.cmd -defterm -here -no-start -ucrt64 -c \"./wyrt_Release.exe "
#define EXE "a.exe"
#else
#define COMPILER "./wyrt_Release "
#define EXE "./a.out"
#endif

int main(void)
{
	remove("err");

	for(int i = 0; i < test_count; i++) {
		if(tests[i].in) {
			printf("Test '%s' ('%s'): ", tests[i].file, tests[i].in);
		} else {
			printf("Test '%s': ", tests[i].file);
		}
#ifdef _WIN32
		char *cmd = malloc(strlen(COMPILER) + strlen(tests[i].file) + strlen("test/ 2> err\"") + 1);
#else
		char *cmd = malloc(strlen(COMPILER) + strlen(tests[i].file) + strlen("test/ 2> err") + 1);
#endif
		FILE *file = NULL;
		FILE *err = NULL;
		FILE *in = NULL;
		FILE *out = NULL;
		cmd[0] = 0;
		strcat(cmd, COMPILER "test/");
		strcat(cmd, tests[i].file);
		strcat(cmd, " 2> err");
#ifdef _WIN32
		strcat(cmd, "\"");
#endif

		int comp_status = system(cmd);

		err = fopen("err", "r");
		if((err && fgetc(err) != EOF) || comp_status) {
			if(!tests[i].should_fail) {
				printf("[" RED "FAIL" RESET "] Compiler Rejected\n");
				goto CLEAN_AFTER;
			}
			goto PASS;
		} else if(tests[i].should_fail) {
			printf("[" RED "FAIL" RESET "] Compiler Accepted\n");
			goto CLEAN_AFTER;
		}

		int exitcode;
		if(tests[i].in) {
			in = fopen("in", "w");
			if(!in) {
				fprintf(stderr, "Could not open file to use as stdin!\n");
				return -1;
			}

			fwrite(tests[i].in, strlen(tests[i].in), 1, in);
			fclose(in);

			exitcode = system(EXE " > out < in");
		} else {
			exitcode = system(EXE " > out");
		}
#ifndef _WIN32
		exitcode /= 256; // Needed on Linux for some reason (probably endian?)
#endif

		if(tests[i].out) {
			out = fopen("out", "r");
			if(!out) {
				printf(
					"[" BRIGHT_RED "ERR" RESET "] Unable to open 'out'.\n"
				);
				goto CLEAN_AFTER;
			}

			const char *expected = tests[i].out;
			while(*expected) {
				int c = fgetc(out);
				if(c != *(expected++)) {
					printf("[" RED "FAIL" RESET "] Mismatched Output.\n");
					goto CLEAN_AFTER;
				}
			}
		}

		if(exitcode != tests[i].exitcode) {
			printf(
				"[" RED "FAIL" RESET "] Expected Exit Code %i, got %i\n",
				tests[i].exitcode,
				exitcode
			);
			goto CLEAN_AFTER;
		}

PASS:
		printf("[" BRIGHT_GREEN "PASS" RESET "]\n");

CLEAN_AFTER:
		free(cmd);
		if(file) fclose(file);
		if(err) {
			fclose(err);
			remove("err");
		}
		if(out) {
			fclose(out);
			remove("out");
		}
	}

	return 0;
}
