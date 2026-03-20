fn read() s32
#extern("getchar")

fn print(s: &const [_]u8) s32
#extern("puts")

fn putc(c: u8) s32
#extern("putchar")

fn main() u8
{
	discard print(c"Please enter an option (a, b, c-f, or A): ");

	var x: u8 = 'm';
	const char: s32 = read();

	if(char == 'a') {
		x += 2;
	} else if(char == 'b') {
		x -= 2;
	} else if(char >= 'c' && char <= 'f' || char == 'A') {
		x *= 2;
		x -= 200;
		x += 'a';
	} else if(char > 'f' && !(char > 'j') && char != 'h') {
	} else if(const a: u8 = 'a' + ('m' - 'a') - 1; char < a && char > 'h') {
		x = a;
	} else if(const a: bool = char == 'l';) {
		x = 'z';
	} else {
		discard print(c"Invalid Option\n");
		return 1;
	}

	discard print(c"Result: ");
	discard putc(x);
	discard putc('\n');

	return 0;
}
