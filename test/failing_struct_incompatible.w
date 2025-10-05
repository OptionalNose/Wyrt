fn main() u8
{
	const args: struct {x: u8, y: u8} = _{.x = 2, .y = 5};
	return foo(args);
}

fn foo(args: struct {x: u8, y: u16}) u16
{
	return args.x + args.y;
}
