#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include "../fmmap.h"

int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;

    const char *f = argv[1];
    unsigned char *s;
    struct stat sb;
    size_t r;

    if (stat(f, &sb) < 0)
        return 1;

    s = malloc(sb.st_size);
    if (!s)
        return 1;

    fmmap *fm = fmmap_open(argv[1], FMMAP_RDONLY);
    if (!fm)
        return 1;

    r = fmmap_read(fm, s, sb.st_size);
    printf("read=%zu, size=%zu\n", r, (size_t)sb.st_size);
    printf("off=%zu\n", fmmap_tell(fm));

    free(s);
    fmmap_close(fm);
    return r==0;
}
