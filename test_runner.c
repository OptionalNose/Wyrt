#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

typedef struct {
	const char *file;
	int exitcode;
	bool should_fail;
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
	for(int i = 0; i < test_count; i++) {
		printf("Test '%s': ", tests[i].file);
		char *cmd = malloc(20 + strlen(tests[i].file) + 7 + 1);
		FILE *file = NULL;
		cmd[0] = 0;
		strcat(cmd, "./wyrt_Release test/");
		strcat(cmd, tests[i].file);
		strcat(cmd, " 2> err");
		
		int comp_status = system(cmd);
		if(tests[i].should_fail && !comp_status) {
			printf("[" RED "FAIL" RESET "] Compiler Accepted\n");
			goto CLEAN_AFTER;
		} else if(!tests[i].should_fail && comp_status) {
			printf("[" RED "FAIL" RESET "] Compiler Rejected\n");
			goto CLEAN_AFTER;
		}

		if(tests[i].should_fail) goto PASS;

		system("./a.out ; echo $? > exitcode");
		
		file = fopen("exitcode", "rb");
		if(!file) {
			printf("[" BRIGHT_RED "ERR" RESET "] Unable to open 'exitcode'.\n");
			goto CLEAN_AFTER;
		}

		int exitcode;
		fscanf(file, "%i", &exitcode);  

		if(exitcode != tests[i].exitcode) {
			printf("[" RED "FAIL" RESET "] Expected Exit Code %i, got %i\n", tests[i].exitcode, exitcode);
			goto CLEAN_AFTER;
		}
		
PASS:
		printf("[" BRIGHT_GREEN "PASS" RESET "]\n");

CLEAN_AFTER:
		free(cmd);
		if(file) fclose(file);
	}

	return 0;
}
