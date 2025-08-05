fn main() u8
{
	var foo: u8;
	var bar: u8 = 8;

	foo = 6 + 7;
	foo *= 3;
	foo -= 4 + 5;

	bar /= 2;
	bar /= 2;

	foo += bar;
	return foo + 1;
}
