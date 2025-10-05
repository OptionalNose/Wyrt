fn main() u8
{
	var a: u8 = 0;
	var b: u8;

	foo(&a, &b);

	return b;
}

fn foo(a: &abyss u8, b: &abyss u8) void
{
	*b = *a;
}
