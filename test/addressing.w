fn main() u8
{
	const arr: [_]u8 = {1, 2, 3, 4, 5};
	const str: struct {x: []const u8} = _{.x = &arr};
	return foo(&str.x[2]);
} 

fn foo(arg: &const u8) u8
{
	return *arg + 5;
}
