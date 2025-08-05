fn print(str: &const u8) u32
#extern("puts")

fn main() u8
{
	print("Hello, World!\n".ptr);
	return 0;
}
