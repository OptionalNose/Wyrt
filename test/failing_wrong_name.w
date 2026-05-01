typedef Foo = struct {
	x: u8,
};

typedef Bar = struct {
	x: u8,
};

fn main() u8
{
	const foo: Foo = Bar {.x = 8};
	return foo.x;
}
