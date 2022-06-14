#include <err.h>
#include <errno.h>
#include <stdio.h>
#include "../fmmap.h"

int main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	fmmap *fm = fmmap_create(argv[1], FMMAP_RDONLY|FMMAP_APPEND, 0600);
	if (!fm)
		err(1, "fmmap_create");

	printf("len=%zu\n", fmmap_length(fm));
	printf("off=%zu\n", fmmap_tell(fm));
	if (fmmap_seek(fm, 0, FMMAP_SEEK_END) < 0)
		perror("seek fail");
	printf("off=%zu\n", fmmap_tell(fm));

	return fmmap_close(fm);
}
