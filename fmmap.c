/*
 * Copyright (c) 2022, Widianto Nur Firmansah <xnaltasee@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "fmmap.h"

#define MAX(x,y)		((x) > (y) ? (x) : (y))
#define MIN(x,y)		((x) < (y) ? (x) : (y))
#define CUROFF_OVERFLOW(f)	((f)->curoff > (f)->length)

/* private function */
static int fmmap_remap(struct fmmap *, size_t);
static int fmmap_sync(struct fmmap *);

struct fmmap {
    void *addr;
    size_t mapsz; /* for writing */
    size_t length; /* for reading */
    size_t curoff;
    int mode;
    int fd;
};

struct fmmap *fmmap_open_length(const char *file, int mode, size_t filelen)
{
    if (file == NULL || filelen == 0) {
        errno = EINVAL;
        return NULL;
    }

    void *buf;
    struct fmmap *fm;
    struct stat sb;
    int fd, fd_flag, fm_mode, fm_flag;
    size_t mapsz = filelen;

    if (filelen >= FMMAP_MAX_SIZE) {
        errno = EOVERFLOW;
        return NULL;
    }

    if ((mode & FMMAP_WRONLY) == FMMAP_WRONLY) {
        fd_flag = O_RDWR;
        fm_mode = PROT_WRITE;
    } else if ((mode & FMMAP_RDWR) == FMMAP_RDWR) {
        fd_flag = O_RDWR;
        fm_mode = PROT_READ | PROT_WRITE;
    } else if ((mode & FMMAP_RDONLY) == FMMAP_RDONLY) {
        fd_flag = O_RDONLY;
        fm_mode = PROT_READ;
    } else {
        /* none of three flags above present */
        errno = EINVAL;
        return NULL;
    }

    fd = open(file, fd_flag | O_CLOEXEC);
    if (fd < 0)
        return NULL;

    if (fstat(fd, &sb) < 0)
        goto close_fd;

    if (!S_ISREG(sb.st_mode)) {
        errno = EINVAL;
        goto close_fd;
    }

    fm_flag = MAP_SHARED;
    buf = mmap(NULL, mapsz, fm_mode, fm_flag, fd, 0);
    if (buf == MAP_FAILED)
        goto close_fd;

    /* when we encounter FMMAP_TRUNC and FMMAP_APPEND
     * at the same time, ignore FMMAP_TRUNC.
     */
    if ((mode & FMMAP_TRUNC) == FMMAP_TRUNC &&
        (mode & FMMAP_APPEND) != FMMAP_APPEND) {
        if ((mode & FMMAP_RDONLY) == FMMAP_RDONLY) {
            errno = EPERM;
            goto delete_map;
        }

        memset(buf, 0, filelen);
        filelen = 0;
    }

    /* ignore these madvise if error */
    (void)madvise(buf, mapsz, MADV_WILLNEED);
    (void)madvise(buf, mapsz, MADV_SEQUENTIAL);

    fm = malloc(sizeof(struct fmmap));
    if (fm == NULL)
        goto delete_map;

    fm->addr = buf;
    fm->mapsz = mapsz;
    fm->length = filelen;
    fm->mode = mode;
    fm->curoff = 0;
    fm->fd = fd;

    return fm;

delete_map:
    (void)munmap(buf, mapsz);

close_fd:
    (void)close(fd);
    return NULL;
}

struct fmmap *fmmap_open(const char *file, int mode)
{
    struct stat sb;

    if (stat(file, &sb) < 0)
        return NULL;

    return fmmap_open_length(file, mode, sb.st_size);
}

struct fmmap *fmmap_create(const char *filename, int mode, int perms)
{
    if (filename == NULL) {
        errno = EINVAL;
        return NULL;
    }

    int fd;
    int open_flags = O_RDWR | O_CREAT;

    if ((mode & FMMAP_EXCL) == FMMAP_EXCL)
        open_flags |= O_EXCL;

    fd = open(filename, open_flags, perms);
    if (fd < 0)
        return NULL;

    (void)ftruncate(fd, 1);
    (void)close(fd);

    return fmmap_open_length(filename, FMMAP_RDWR | FMMAP_TRUNC, 1);
}

size_t fmmap_read(fmmap *restrict fm, void *restrict ptr, size_t size)
{
    if (fm == NULL || ptr == NULL) {
        errno = EINVAL;
        return 0;
    } else if ((fm->mode & FMMAP_WRONLY) == FMMAP_WRONLY) {
        errno = EPERM;
        return 0;
    }

    uint8_t *dptr = ptr;
    size_t count = 0;

    /* It's safe to remove this check, but i prefer this way.
     * Another reason is, of course, to avoid memcpy() call with
     * just 0 copy size.
     */
    if (fm->curoff < fm->length) {
        count = MIN(fm->length - fm->curoff, size);
        memcpy(dptr, (uint8_t *)fm->addr + fm->curoff, count);
        fm->curoff += count;
    }

    return count;
}

size_t fmmap_write(fmmap *restrict fm, const void *restrict ptr, size_t size)
{
    if (fm == NULL || ptr == NULL) {
        errno = EINVAL;
        return 0;
    } else if ((fm->mode & FMMAP_RDONLY) == FMMAP_RDONLY) {
        errno = EPERM;
        return 0;
    }

    size_t count = 0;

    if ((fm->mode & FMMAP_APPEND) == FMMAP_APPEND)
        fm->curoff = fm->length;

    if (fmmap_remap(fm, fm->length + size) < 0)
        return 0;

    count = MIN(fm->mapsz - fm->curoff, size);
    memcpy((uint8_t *)fm->addr + fm->curoff, ptr, count);
    fm->curoff += count;

    if (fmmap_sync(fm) < 0)
        return 0;

    return count;
}

size_t fmmap_seek(struct fmmap *fm, size_t offset, int whence)
{
    if (fm == NULL) {
        errno = EINVAL;
        return 0;
    }

    size_t oldoff = fm->curoff;

    if (offset > fm->length)
        goto beyond_size;

    switch (whence) {
    case FMMAP_SEEK_SET:
        fm->curoff = offset;
        break;
    case FMMAP_SEEK_CUR:
        fm->curoff += offset;
        if (CUROFF_OVERFLOW(fm))
            goto beyond_size;
        break;
    case FMMAP_SEEK_END:
        fm->curoff = fm->length - offset;
        if (CUROFF_OVERFLOW(fm))
            goto beyond_size;
        break;
    default:
        errno = EINVAL;
        return 0;
    }

    return fm->curoff;

beyond_size:
    fm->curoff = oldoff;
    errno = EOVERFLOW;
    return 0;
}

void fmmap_rewind(struct fmmap *fm)
{
    int save = errno;

    /* fmmap_seek checks for 'fm' if NULL, so we no need to
     * check again
     */
    (void)fmmap_seek(fm, 0, FMMAP_SEEK_SET);
    errno = save;
}

size_t fmmap_tell(struct fmmap *fm)
{
    if (fm == NULL) {
        errno = EINVAL;
        return 0;
    }

    return fm->curoff;
}

size_t fmmap_length(struct fmmap *fm)
{
    if (fm == NULL) {
        errno = EINVAL;
        return 0;
    }

    return fm->length;
}

bool fmmap_iseof(struct fmmap *fm)
{
    if (fm == NULL)
        return true;

    return fm->length > 0 && fm->curoff == fm->length;
}

int fmmap_close(struct fmmap *fm)
{
    if (fm == NULL) {
        errno = EINVAL;
        return -1;
    }

    int r;

    (void)close(fm->fd); /* ignore if error */
    r = munmap(fm->addr, fm->mapsz);
    fm->addr = NULL;
    fm->fd = -1;
    fm->mapsz = fm->length = fm->mode = fm->curoff = 0;
    free(fm);

    return r;
}

/* update both mapping size and file size for writing */
static int fmmap_remap(struct fmmap *fm, size_t newsz)
{
    if (newsz <= fm->mapsz)
        return 0;

    /* first, truncate file to the desired file size for writing. */
    if (ftruncate(fm->fd, newsz) < 0)
        return -1;

    int save = errno;

    /* second, create new mapping.
     *
     * MREMAP_MAYMOVE to avoid ENOMEM error.
     */
    void *new = mremap(fm->addr, fm->mapsz, newsz, MREMAP_MAYMOVE);
    if (new == MAP_FAILED)
        return -1;

    /* third, request advise again.
     *
     * manual page doesn't describe what happen to the previous
     * advise after calling mremap.
     */
    (void)madvise(new, newsz, MADV_WILLNEED);
    (void)madvise(new, newsz, MADV_SEQUENTIAL);

    /* last, update our structure. */
    fm->addr = new;
    fm->mapsz = newsz;
    fm->length = newsz;

    errno = save;
    return 0;
}

/* synchronize memory with file */
static int fmmap_sync(struct fmmap *fm)
{
    return msync(fm->addr, fm->mapsz, MS_ASYNC);
}
