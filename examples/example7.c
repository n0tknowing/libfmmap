#include <err.h>
#include <errno.h>
#include <stdio.h>
#include "../fmmap.h"

int main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	fmmap *fm = fmmap_open(argv[1], FMMAP_WRONLY|FMMAP_APPEND);
	if (!fm)
		err(1, "fmmap_open: %s", argv[1]);

	size_t r;

	r = fmmap_write(fm, "hi\n", 3);
	if (r != 3)
		perror("fmmap_write");
	printf("r=%zu\n", r);
	printf("off=%zu\n", fmmap_tell(fm));
	printf("len=%zu\n", fmmap_length(fm));

	r = fmmap_write(fm, "hi\n", 3);
	if (r != 3)
		perror("fmmap_write");
	printf("r=%zu\n", r);
	printf("off=%zu\n", fmmap_tell(fm));
	printf("len=%zu\n", fmmap_length(fm));

	r = fmmap_write(fm, "hi\n", 3);
	if (r != 3)
		perror("fmmap_write");
	printf("r=%zu\n", r);
	printf("off=%zu\n", fmmap_tell(fm));
	printf("len=%zu\n", fmmap_length(fm));

	r = fmmap_write(fm, "hi\n", 3);
	if (r != 3)
		perror("fmmap_write");
	printf("r=%zu\n", r);
	printf("off=%zu\n", fmmap_tell(fm));
	printf("len=%zu\n", fmmap_length(fm));

	return fmmap_close(fm);
}
