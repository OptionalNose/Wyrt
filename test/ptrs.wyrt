fn main() u8
{
	const a: u8 = 2;
	var b: u8 = 5;
	var c: u8;

	foo(&a, &b, &c);

	return c;
}

fn foo(a: &const u8, b: &var u8, c: &abyss u8) void
{
	*b += *a;
	*b *= *b;
	*c = *b;
	return;
}
