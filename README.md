# Wyrt Lang (C Compiler)

Wyrt is a learning project, although the goal is to be actually useful.
It is currently less than skeletal, but the goal is to be eventually self-hosted.

See the Github Wiki for a pseudo-Tutorial.
See the Specification for a technical and precise definition of the language.
(Note: The Specification is currently outdated, and progress will continue once language features stabalize).

It is planned to strongly support Design by Contract, with probably syntax similar to:
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
CC src/*.c -o wyrt
```
or

```bash
make
```
Note: This will build a debug version of the compiler with Address and Undefined Behaviour Sanitizers.
These Sanitizers at times do not like the high-levels of recursion throughout the compiler, and can cause the compiler to Segfault.
If you get a Segfault message without any information from the Sanitizers, then it is highly likely they are the culprit.
Running the program repeatedly, or inside a debugger, will fix the problem.

The only _build_ dependencies are `libc` and `libgccjit`.

    If you are on Linux: install `libgccjit` using your package manager
    
    If you are on Windows: the only method I got to work was to install [MSYS2](https://www.msys2.org/), a minimalist Unix environment for Windows and use its package manager to install.

---

## Running
Generally:
```bash
wyrt <src>.wyrt -o <output>
```
to display the builtin help, run `wyrt --help` or `wyrt -h`

Currently multiple input files and glob-patterns are not supported.

To only compile, but not link, use `wyrt -c`. The created object files can be linked normally.

---

## Testing
Build the Release build of the Compiler
(prevents Sanitizer Segfault issue), and Build and Run the Test Runner
```bash
make test
```

Some tests are designed to not compile, these have 'failing_' prefixing their names.

Expected Test output can be found in `test_manifest`.

---

## Known Issues
The Makefile compiles a debug version of the compiler with Address and Undefined Santizers enabled.
These Sanitizers sometimes get upset about the large stack-depths in the parser and segfault :upside_down_face:.
Just run the compiler repeatedly and it should work eventually.
Alternatively, you could also just run it inside a debugger, and that seems to keep the Sanitzers from crashing.
