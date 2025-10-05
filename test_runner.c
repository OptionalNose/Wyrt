#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
	const char *file;
	int exitcode;
	bool should_fail;
	const char *out;
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
#define COMPILER "wyrt.exe_Release "
#define EXE "a.exe"
#else
#define COMPILER "./wyrt_Release "
#define EXE "a.out"
#endif

int main(void)
{
	remove("err");
	for(int i = 0; i < test_count; i++) {
		printf("Test '%s': ", tests[i].file);
		char *cmd = malloc(strlen(COMPILER) + strlen(tests[i].file) + strlen("test/ 2> err") + 1);
		FILE *file = NULL;
		FILE *err = NULL;
		FILE *out = NULL;
		cmd[0] = 0;
		strcat(cmd, COMPILER "test/");
		strcat(cmd, tests[i].file);
		strcat(cmd, " 2> err");

		int comp_status = system(cmd);

		err = fopen("err", "rb");
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

		int exitcode = system(EXE " > out");

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
