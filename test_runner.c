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

int main(void)
{
	remove("err");
	for(int i = 0; i < test_count; i++) {
		printf("Test '%s': ", tests[i].file);
		char *cmd = malloc(20 + strlen(tests[i].file) + 7 + 1);
		FILE *file = NULL;
		FILE *err = NULL;
		FILE *out = NULL;
		cmd[0] = 0;
		strcat(cmd, "./wyrt_Release test/");
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
		}

		system("./a.out > out ; echo $? > exitcode");

		out = fopen("out", "rb");
		if(!out) {
			printf(
				"[" BRIGHT_RED "ERR" RESET "] Unable to open 'out'.\n"
			);
			goto CLEAN_AFTER;
		}
		if(tests[i].out) {
			const char *expected = tests[i].out;
			while(*expected) {
				if(*(expected++) != fgetc(out)) {
					printf("[" RED "FAIL" RESET "] Mismatched Output.\n");
					goto CLEAN_AFTER;
				}
			}
		}


		file = fopen("exitcode", "rb");
		if(!file) {
			printf(
				"[" BRIGHT_RED "ERR" RESET "] Unable to open 'exitcode'.\n"
			);
			goto CLEAN_AFTER;
		}

		int exitcode;
		fscanf(file, "%i", &exitcode);

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
