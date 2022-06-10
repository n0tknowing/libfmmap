#include <stdio.h>
#include "../fmmap.h"

int main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	fmmap *fm;
	const char *f = argv[1];
	char buf[65] = {0};
	size_t r;

	fm = fmmap_open(f, FMMAP_RDONLY);
	if (!fm) {
		perror(f);
		return 1;
	}

	while ((r = fmmap_read(fm, buf, 64)) > 0) {
		printf("read %zu bytes\n", r);
		printf("-------------- DATA -------------\n");
		printf("%s\n", buf);
	}
	
	printf("offset=%zu\n", fmmap_tell(fm));
	fmmap_close(fm);
	return 0;
}
