#include <err.h>
#include <stdlib.h>
#include "../fmmap.h"

int main(int argc, char **argv)
{
    if (argc != 3)
        errx(1, "usage: %s srcfile dstfile", argv[0]);

    fmmap *src, *dst;
    unsigned char *buf;
    size_t len;

    src = fmmap_open(argv[1], FMMAP_RDONLY);
    if (!src)
        err(1, "failed to open %s for reading", argv[1]);

    len = fmmap_length(src);

    buf = malloc(len);
    if (!buf)
        err(1, "failed to allocate memory");

    if (!fmmap_read(src, buf, len))
        errx(1, "failed to read %s, %s", argv[1],
                fmmap_iseof(src)?"EOF reached":"I/O error");

    fmmap_close(src);

    dst = fmmap_create(argv[2], FMMAP_EXCL, 0600);
    if (!dst)
        err(1, "failed to create %s for writing", argv[2]);

    if (!fmmap_write(dst, buf, len))
        errx(1, "failed to write to %s, %s", argv[2],
                fmmap_iseof(dst)?"EOF reached":"I/O error");

    free(buf);
    return fmmap_close(dst);
}
