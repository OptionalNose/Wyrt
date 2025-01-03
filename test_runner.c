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

int main(void)
{
	for(int i = 0; i < test_count; i++) {
		printf("Test '%s': ", tests[i].file);
		char *cmd = malloc(20 + strlen(tests[i].file) + 1);
		FILE *file = NULL;
		cmd[0] = 0;
		strcat(cmd, "./wyrt_Release test/");
		strcat(cmd, tests[i].file);
		
		int comp_status = system(cmd);
		if(tests[i].should_fail && !comp_status) {
			printf("[FAIL] Compiler Accepted\n");
			goto CLEAN_AFTER;
		} else if(!tests[i].should_fail && comp_status) {
			printf("[FAIL] Compiler Rejected\n");
			goto CLEAN_AFTER;
		}

		if(tests[i].should_fail) goto PASS;

		system("./a.out ; echo $? > exitcode");
		
		file = fopen("exitcode", "rb");
		if(!file) {
			printf("[ERR] Unable to open 'exitcode'.\n");
			goto CLEAN_AFTER;
		}

		int exitcode;
		fscanf(file, "%i", &exitcode);  

		if(exitcode != tests[i].exitcode) {
			printf("[FAIL] Expected Exit Code %i, got %i\n", tests[i].exitcode, exitcode);
			goto CLEAN_AFTER;
		}
		
PASS:
		printf("[PASS]\n");

CLEAN_AFTER:
		free(cmd);
		if(file) fclose(file);
	}

	return 0;
}
