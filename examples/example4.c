#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "../fmmap.h"

/* like example1 but without reading and just seeking to EOF */
int main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	fmmap *fm = fmmap_open(argv[1], FMMAP_RDONLY);
	if (!fm)
		return 1;

	printf("off=%zu\n", fmmap_tell(fm));
	if (fmmap_seek(fm, 0, FMMAP_SEEK_END) < 0)
		perror("seek failed");
	printf("off=%zu\n", fmmap_tell(fm));

	return fmmap_close(fm);
}
