#include <stdlib.h>
#include "../fmmap.h"

#define _BLOCK_SIZE (1 << 16)

int main(int argc, char **argv)
{
	if (argc < 2)
		return 1;

	unsigned char *s = malloc(_BLOCK_SIZE);
	size_t r;

	fmmap *f = fmmap_open(argv[1], FMMAP_RDONLY);
	if (!f)
		return 1;

	while ((r = fmmap_read(f, s, _BLOCK_SIZE)) > 0);

	free(s);
	fmmap_close(f);
	return r!=0;
}
