typedef Args = struct {
	x: u8,
	str: &const [_]u8,
};

typedef Super = Args;

fn print(str: &const [_]u8) s32
#extern("puts")

fn main() u8
{
	var args: Args = Args {.x = 7, .str = c"Hello, Typedef!"};
	foo(&args);
	const super: Super = args;
	const super2: Super = Args {.x = 5};
	return super.x + super2.x + args.x;
}

fn foo(args: &var Args) void
{
	args->x *= 2;
	args->x += 2;
	discard print(args->str);
}
