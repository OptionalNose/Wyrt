fn main() u8
{
	const arrs: [_][_]u8 = {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}};
	const slices: [_][]const u8 = {&arrs[0], &arrs[1], &arrs[2]};

	return fma(arr[0]) - fma(arr[2]);
}

fn fma(arr: [3]u8) u8
{
	return arr[0] * arr[1] + arr[2];
}
