# Wyrt Lang (C Compiler)

Wyrt is a learning project, although the goal is to be actually useful.
It is currently less than skeletal, but the goal is to be eventually self-hosted.

See the Github Wiki for a pseudo-Tutorial.
See the Specification for a technical and precise definition of the language.
(Note: The Specification is currently outdated, and progress will continue once language features stabalize).

It is planned to strongly support Design by Contract, with syntax probably similar to:
```

var counter: u8 = 0;

fn foo(str: &const u8) u8
	#clobber(counter, stdout)
	#precond(str != null)
	#postcond(#return > 0)
{
	std.io.print(str);
	counter += 1;
	return counter;
}
```
The idea is that the better the programmer can understand what a function does and it's requirements, the less likely they are to create bugs.

It is called "wyrt" based off of the old-english word for "root", because it encourages you to get 'down in the dirt' and do-it-yourself.
It is _supposed_ to be pronounced "woo-rt", if you go by the original old-english.
I find it easier to pronounce it "whir-t" or "wee-rt".

---

## Building

To build the Compiler run:
```bash
CC build.c -O3 -o build<.exe>
./build<.exe>
```
To automatically configure the compiler (generates `config.h`) based on which backends were able to be built.
If a backend could not be built (i.e. missing dependency), then that backend will be skipped.
If you wish, you could also manually edit `config.h`.
Backends included in `config.h` can be used by using the `--backend=` option when you invoke the compiler.
Additional backends that are not included in `config.h` can be used at compiler-runtime with the `--backend-path=` option,
supplied with a path to a dynamic library containing the backend.

`build<.exe> release` will build an release version (optimized and no debug information) of the compiler and all backends (if possible).

Note: This will build a debug version of the compiler with Address and Undefined Behaviour Sanitizers.
These Sanitizers at times do not like the high-levels of recursion throughout the compiler, and can cause the compiler to Segfault.
If you get a Segfault message without any information from the Sanitizers, then it is highly likely they are the culprit.
Running the program repeatedly, or inside a debugger, will fix the problem.

The only dependency to build the compiler itself is `libc`.
To include the GCC backend (currently the only backend), you must also have `libgccjit` installed.

If you are on Linux: install `libgccjit` using your package manager
    
If you are on Windows: the only method I got to work was to install [MSYS2](https://www.msys2.org/), a minimalist Unix environment for Windows and then use its package manager to install.

---

## Running
Generally:
```bash
wyrt <src>.w -o <output>
```
to display the builtin help, run `wyrt --help` or `wyrt -h`

To only compile, but not link, use `wyrt -c`. The created object files can be linked normally with object files that also follow the C ABI.
---

## Testing
Build the Release build of the Compiler
(prevents Sanitizer Segfault issue, and improves performance), and Build and Run the Test Runner
```bash
./build<.exe> test
```

Some tests are designed to not compile, these have 'failing_' prefixing their names.

Expected Test output can be found in `test_manifest`.

---
