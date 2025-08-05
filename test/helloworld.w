fn cputs(s: &const u8) s32
#extern("puts")

fn main() u8
{
	discard cputs(c"Hello, World!");
	return 0;
}
