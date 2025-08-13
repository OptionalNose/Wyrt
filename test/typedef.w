typedef Args = struct {
	x: u8,
	str: &const u8,
};

fn print(str: &const u8) s32
#extern("puts")

fn main() u8
{
	var args: Args = _{.x = 7, .str = c"Hello, Typedef!"};
	foo(&args);
	return args.x;
}

fn foo(args: &var Args) void
{
	args->x *= 2;
	args->x += 2;
	discard print(args->str);
}
