fn print(s: &const [_]u8) s32
#extern("puts")

fn main() u8
{
	const nums: [_]u8 = {3, 4, 5};
	discard print(c"You won't see this. Hello, World!\n" + 20);
	return foo(&nums);
}

fn foo(nums: []const u8) u8
{
	return *(nums.ptr + 1) + nums.ptr[2];
}
