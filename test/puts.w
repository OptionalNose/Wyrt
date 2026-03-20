fn print(str: &const [_]u8) u32
#extern("puts")

fn main() u8
{
	print(c"Hello, World!\n");
	return 0;
}
