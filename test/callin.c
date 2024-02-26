#include <stdio.h>

unsigned char foo(void);

int main(void)
{
	printf("foo(): %ud", foo());
	return 0;
}
