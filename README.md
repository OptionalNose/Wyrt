# Wyrt Lang (C Compiler)

Wyrt is a learning project, although the goal is to be actually useful.
It is currently less than skeletal, but the goal is to be eventually self-hosted.

See the Github Wiki for a pseudo-Tutorial.
See the Specification for a technical and precise definition of the language.

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

It is called "wyrt" based off of the old-english word for "root", because it encourages you to get 'down in the dirt'.
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

The only dependency is `libc`, so the compiler should be able to run on any platform, although currently only Linux x86_64 codegen is supported.
(Windows coming soon, hopefully). I don't have a Mac, so I can't really support it as a platform.
See below for a possible work around.

---

## Running
Generally:
```bash
wyrt <src>.wyrt -o <output>
```
to display the builtin help, run `wyrt --help` or `wyrt -h`

Currently multiple input files and glob-patterns are not supported.

The compiler only produces assembly files, so it assumes that you have `nasm` and `ld` installed to create executable binaries.
If you do not have these installed, you can make it just output the assembly by `wyrt -S` (it will output nasm-syntax asm, so you will probably need to make some edits on the outputted file to get it to compile with GNU's assembler or others).
To only compile, but not link, use `wyrt -c`. The created object files can be linked normally (assuming everything else uses the System V ABI Calling Convention).

Currently, only Linux x86_64 codegen is supported.

You *might* be able to get it to work on other platforms by compiling to an object file and then linking with `libc`, assuming you define the `main` function somewhere.
The assembly it outputs uses the System V ABI Calling Convention.
So as long as the `libc` you are using follows the System V ABI Calling Convention, it *should* work.

---

## Testing
Build the Test Runner with
```bash
CC test_runner.c -o test_runner
```
Make sure you have built a release version of the Compiler (makes testing faster and removes the Sanitizer Segfault issue) with
```bash
make release
```
And then to run the tests:
```bash
./test_runner
```

Some tests are designed to not compile, these have 'failing_' prefixing their names.

---

## Known Issues
The Makefile compiles a debug version of the compiler with Address and Undefined Santizers enabled.
These Sanitizers sometimes get upset about the large stack-depths in the parser and segfault :upside_down_face:.
Just run the compiler repeatedly and it should work eventually.
Alternatively, you could also just run it inside a debugger, and that seems to keep the Sanitzers from crashing. 
