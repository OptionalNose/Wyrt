fn main() u8
{
	const args1: [3]u8 = {3, 5, 2};
	const args2: [_]u8 = {7, 5, 2};
	return operate(&args1) + operate(&args2);
}

fn operate(list: []const u8) u8
{
	var ret: u8 = 0;
	ret += list[0];
	ret *= list[1];
	ret -= list[2];

	return ret;
}
