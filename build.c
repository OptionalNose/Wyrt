#include <stdio.h>
#include <stdlib.h>

#include "src/util.c" // Make compiling build script easier

#define CC "gcc "

#define CFLAGS "-std=c99 -Wall -Wpedantic "

#ifdef _WIN32
#define EXT ".exe "
#define DLEXT ".dll "
#define LDFLAGS "-lkernel32 "
#define DBGFLAGS "-O0 -g -fsanitize=undefined -fsanitize-trap=all "
#include "build_win.c"
#else
#define EXT " "
#define DLEXT ".so "
#define LDFLAGS "-ldl "
#define DBGFLAGS "-O0 -g -fsanitize=undefined,address "
#include "build_posix.c"
#endif

const char *(sources[]) = {
	"main",
	"util",
	"lexer",
	"parser",
	"codegen",
	"types",
	"ui",
};
const int source_count = (sizeof sources) / sizeof sources[0];

typedef struct {
	const char *name;
	const char *desc;
	const char *ld;
} BackendOption;

const BackendOption backends[] = {
	{"gcc", "GCC via libgccjit", "-lgccjit"},
};
const int backend_count = (sizeof backends) / sizeof backends[0];

int main(int argc, char **argv)
{
	Error err = ERROR_OK;
	bool release = false;
	bool test = false;
	StringBuilder cmd = { 0 };
	StringBuilder srcpath = { 0 };
	StringBuilder dstpath = { 0 };
	FILE *config = NULL;

	if(argc > 1 && argc > 2) {
		fprintf(stderr, "Expected 1 argument to build script, found %d\n", argc - 1);
		err = ERROR_UNEXPECTED_DATA;
		goto RET;
	}

	if(argc > 1) {
		if(!strcmp(argv[1], "release")) {
			release = true;
		} else if(!strcmp(argv[1], "test")) {
			test = true;
		} else {
			fprintf(stderr, "Expected either 'release' or 'test', found '%s'\n", argv[1]);
			err = ERROR_UNEXPECTED_DATA;
			goto RET;
		}
	}

	mkdir_if_nexist("obj", &err);
	if(err) goto RET;

	config = fopen("config.h", "w");
	if(!config) {
		fprintf(stderr, "Could not create config file!\n");
		err = ERROR_IO;
		goto RET;
	}
	
	fprintf(
		config,
		"typedef struct {\n"
		"\tconst char *name;\n"
		"\tconst char *desc;\n"
		"\tconst char *path;\n"
		"} Backend;\n\n"
		"const Backend backends[] = {\n"
	);

	if(release || test) {	
		for(int i = 0; i < backend_count; i++) {
			string_builder_printf(
				&cmd, &err, CC "-O3 -o wyrt_%s_backend" DLEXT "%s src/backends/wyrt_%s_backend.c",
				backends[i].name, backends[i].ld, backends[i].name
			);
			if(err) goto RET;
			
			int res = system(cmd.str);
			if(res) {
				fflush(stderr);
				fprintf(
					stderr,
					"\n"
					"============================================\n"
					"Could not build '%s' backend. Skipping\n"
					"============================================\n\n",
					backends[i].name
				);
			} else {
				fprintf(
					config,
					"\t{\"%s\", \"%s\", wyrt_%s_backend" DLEXT "},\n",
					backends[i].name, backends[i].desc, backends[i].name
				);
			}
			cmd.count = 0;
		}

		fprintf(
			config,
			"{\"none\", \"Do not use any backend. (You probably also want to use --ast-dump)\", NULL}\n};\n"
		);
		fflush(config);

		string_builder_append(&cmd, CC "-O3 -o wyrt_Release" EXT LDFLAGS CFLAGS, &err);
		if(err) goto RET;

		for(int i = 0; i < source_count; i++) {
			string_builder_printf(&cmd, &err, " src/%s.c", sources[i]);
			if(err) goto RET;
		}

		system(cmd.str);
	} else {
		for(int i = 0; i < backend_count; i++) {
			srcpath.count = 0;
			dstpath.count = 0;
			string_builder_printf(&srcpath, &err, "src/backends/wyrt_%s_backend.c", backends[i].name);
			if(err) goto RET;
			string_builder_printf(&dstpath, &err, "wyrt_%s_backend" DLEXT, backends[i].name);
			if(err) goto RET;

			if(!file_is_newer(srcpath.str, dstpath.str)) continue;

			cmd.count = 0;
			string_builder_printf(
				&cmd, &err, CC DBGFLAGS "%s --shared -o %s %s",
				backends[i].ld, dstpath.str, srcpath.str
			);
			if(err) goto RET;
			
			int res = system(cmd.str);
			if(res) {
				fflush(stderr);
				fprintf(
					stderr,
					"\n"
					"============================================\n"
					"Could not build '%s' backend. Skipping\n"
					"============================================\n\n",
					backends[i].name
				);
			} else {
				fprintf(
					config,
					"\t{\"%s\", \"%s\", wyrt_%s_backend" DLEXT "},\n",
					backends[i].name, backends[i].desc, backends[i].name
				);
			}
		}

		fprintf(
			config,
			"{\"none\", \"Do not use any backend. (You probably also want to use --ast-dump)\", NULL}\n};\n"
		);
		
		fflush(config);

		for(int i = 0; i < source_count; i++) {
			srcpath.count = 0;
			dstpath.count = 0;
			string_builder_printf(&srcpath, &err, "src/%s.c", sources[i]);
			if(err) goto RET;
			string_builder_printf(&dstpath, &err, "obj/%s.o", sources[i]);
			if(err) goto RET;

			//if(!file_is_newer(srcpath.str, dstpath.str)) continue;

			cmd.count = 0;
			string_builder_printf(
				&cmd, &err, CC CFLAGS DBGFLAGS "-c %s -o %s",
				srcpath.str, dstpath.str
			);
			if(err) goto RET;
			system(cmd.str);
		}
		cmd.count = 0;
		system(CC LDFLAGS "obj/*.o -o wyrt" EXT);
	}

	if(test) {
		system(CC "test_runner.c -O3 -o test_runner" EXT);
		system("test_runner" EXT);
	}

RET:
	free(srcpath.str);
	free(dstpath.str);
	free(cmd.str);
	if(config) fclose(config);
	return err;
}
