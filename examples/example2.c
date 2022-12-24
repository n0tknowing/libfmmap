#include <stdio.h>
#include <string.h>
#include "../fmmap.h"

/* test fmmap_seek() and fmmap_tell() */
int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;

    const char *f = argv[1];
    char buf[11] = {0};
    size_t r;
    fmmap *fm = fmmap_open(f, FMMAP_RDONLY);
    if (!fm)
        return 1;

    r = fmmap_read(fm, buf, 10);
    printf("r=%zu\n", r);
    if (r>0)
        printf("data: \"%s\"\n", buf);
    printf("jump to EOF!\n");
    if (fmmap_seek(fm, 0, FMMAP_SEEK_END) < 0)
        perror("fmmap_seek SEEK_END failed");
    printf("current offset=%zu\n", fmmap_tell(fm));
    printf("jump to SOF!\n");
    if (fmmap_seek(fm, 0, FMMAP_SEEK_SET) < 0)
        perror("fmmap_seek SEEK_SET failed");
    printf("current offset=%zu\n", fmmap_tell(fm));
    memset(buf, 0, 11);
    r = fmmap_read(fm, buf, 5);
    printf("r=%zu\n", r);
    if (r>0)
        printf("data: \"%s\"\n", buf);
    printf("jump 15 bytes!\n");
    if (fmmap_seek(fm, 15, FMMAP_SEEK_CUR) < 0)
        perror("fmmap_seek SEEK_CUR failed");
    printf("current offset=%zu\n", fmmap_tell(fm));
    memset(buf, 0, 11);
    r = fmmap_read(fm, buf, 3);
    printf("r=%zu\n", r);
    if (r>0)
        printf("data: \"%s\"\n", buf);

    return fmmap_close(fm);
}
