#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../fmmap.h"

int main(int argc, char **argv)
{
    if (argc < 2)
        return 1;

    const char *f = argv[1];
    fmmap *fm = fmmap_open(f, FMMAP_RDONLY);
    if (!fm) {
        perror(f);
        return 1;
    }

    unsigned char buf[5] = {0};
    pid_t ch = fork();
    if (ch < 0) {
        perror(f);
        goto closeit;
    } else if (ch == 0) {
        printf("child process! %ld\n", (long)getpid());
        printf("child offset %zu\n", fmmap_tell(fm));
        if (fmmap_read(fm, buf, 5) != 5)
            fprintf(stderr, "child failed reading 5 bytes\n");
        printf("child offset after reading %zu\n", fmmap_tell(fm));
        printf("seek to EOF!\n");
        if (fmmap_seek(fm, 0, FMMAP_SEEK_END) == 0) {
            perror("child seek");
            goto closeit;
        }
        printf("child offset %zu\n\n", fmmap_tell(fm));
        execl("/bin/notexists", "sh", "-c", "ech", (char *)NULL);
        perror("child execl");
    } else {
        (void)wait(NULL);
        printf("parent process! %ld\n", (long)getpid());
        printf("parent offset %zu\n", fmmap_tell(fm));
        if (fmmap_read(fm, buf, 5) != 5)
            fprintf(stderr, "parent failed reading 5 bytes\n");
        printf("parent offset after reading %zu\n", fmmap_tell(fm));
        printf("seek to EOF!\n");
        if (fmmap_seek(fm, 0, FMMAP_SEEK_END) == 0) {
            perror("parent seek");
            goto closeit;
        }
        printf("parent offset %zu\n\n", fmmap_tell(fm));
    }

closeit:
    return fmmap_close(fm);
}
