/*
 * Copyright (c) 2022, Nathanael Eka Oktavian <nathekavian@gmail.com>
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
#define FMMAP_BLOCK_SIZE	(1 << 16)
#define FMMAP_NEWFMAPSZ		4096

/* private function */
static int fmmap_remap(struct fmmap *, size_t);
static int fmmap_sync(struct fmmap *);

struct fmmap {
	void *addr;
	size_t mapsz;
	size_t length;
	size_t curoff;
	int mode;
	int fd;
};

struct fmmap *fmmap_open_length(const char *file, int mode, size_t filelen)
{
	if (!file || filelen == 0) {
		errno = EINVAL;
		return NULL;
	}

	void *buf;
	struct fmmap *fm;
	struct stat sb;
	int fd, fd_flag, fm_mode, fm_flag;
	size_t mapsz = filelen;

	if (filelen >= FMMAP_MAX_SIZE) {
		errno = ERANGE;
		return NULL;
	}

	fd_flag = O_RDONLY;
	fm_mode = PROT_READ;

	if ((mode & FMMAP_WRONLY) == FMMAP_WRONLY) {
		fd_flag = O_RDWR;
		fm_mode = PROT_WRITE;
	} else if ((mode & FMMAP_RDWR) == FMMAP_RDWR) {
		fd_flag = O_RDWR;
		fm_mode = PROT_READ | PROT_WRITE;
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
	madvise(buf, mapsz, MADV_WILLNEED);
	madvise(buf, mapsz, MADV_SEQUENTIAL);

	fm = malloc(sizeof(struct fmmap));
	if (!fm)
		goto delete_map;

	fm->addr = buf;
	fm->mapsz = mapsz;
	fm->length = filelen;
	fm->mode = mode;
	fm->curoff = 0;
	fm->fd = fd;

	return fm;

delete_map:
	munmap(buf, mapsz);

close_fd:
	close(fd);
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
	if (!filename) {
		errno = EINVAL;
		return NULL;
	} else if ((mode & FMMAP_RDONLY) == FMMAP_RDONLY) {
		errno = EPERM;
		return NULL;
	}

	int fd;

	fd = open(filename, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC, perms);
	if (fd < 0)
		return NULL;

	write(fd, "", 1);
	close(fd);

	return fmmap_open_length(filename, mode, 1);
}

size_t fmmap_read(fmmap *restrict fm, void *restrict ptr, size_t size)
{
	if (!fm || !ptr) {
		errno = EINVAL;
		return 0;
	} else if ((fm->mode & FMMAP_WRONLY) == FMMAP_WRONLY) {
		errno = EINVAL;
		return 0;
	}

	uint8_t *dptr = ptr;
	size_t count = 0, copysz;

	if (!size)
		return 0;

	if (CUROFF_OVERFLOW(fm))
		fm->curoff = fm->length;

	while (size > FMMAP_BLOCK_SIZE && fm->curoff < fm->length) {
		copysz = MIN(FMMAP_BLOCK_SIZE, fm->length - fm->curoff);
		memcpy(dptr + count, (uint8_t *)fm->addr + fm->curoff, copysz);
		fm->curoff += copysz;
		size       -= copysz;
		count      += copysz;
	}

	if (size > 0 && fm->curoff < fm->length) {
		copysz = MIN(size, fm->length - fm->curoff);
		memcpy(dptr + count, (uint8_t *)fm->addr + fm->curoff, copysz);
		fm->curoff += copysz;
		count      += copysz;
	}

	return count;
}

size_t fmmap_write(fmmap *restrict fm, const void *restrict ptr, size_t size)
{
	if (!fm || !ptr) {
		errno = EINVAL;
		return 0;
	} else if ((fm->mode & FMMAP_RDONLY) == FMMAP_RDONLY) {
		errno = EPERM;
		return 0;
	}

	const uint8_t *sptr = ptr;
	size_t count = 0, copysz;

	if (!size)
		return 0;

	if ((fm->mode & FMMAP_APPEND) == FMMAP_APPEND)
		fm->curoff = fm->length;

	if (fmmap_remap(fm, fm->length + size) < 0)
		return 0;

	while (size > FMMAP_BLOCK_SIZE) {
		copysz = MIN(FMMAP_BLOCK_SIZE, fm->mapsz - fm->curoff);
		memmove((uint8_t *)fm->addr + fm->curoff, sptr + count, copysz);
		fm->curoff += copysz;
		size       -= copysz;
		count      += copysz;
	}

	if (size > 0) {
		copysz = MIN(size, fm->mapsz - fm->curoff);
		memmove((uint8_t *)fm->addr + fm->curoff, sptr + count, copysz);
		fm->curoff += copysz;
		count      += copysz;
	}

	if (fmmap_sync(fm) < 0)
		return 0;

	return count;
}

off_t fmmap_seek(struct fmmap *fm, off_t offset, int whence)
{
	if (!fm) {
		errno = EINVAL;
		return -1;
	}

	size_t oldoff = fm->curoff;

	/* off_t is signed integer */
	if (offset < 0 || offset > (off_t)fm->length)
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
		return -1;
	}

	return (off_t)fm->curoff;

beyond_size:
	fm->curoff = oldoff;
	errno = ERANGE;
	return -1;
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
	if (!fm) {
		errno = EINVAL;
		return 0;
	}

	return fm->curoff;
}

size_t fmmap_length(struct fmmap *fm)
{
	if (!fm) {
		errno = EINVAL;
		return 0;
	}

	return fm->length;
}

bool fmmap_iseof(struct fmmap *fm)
{
	if (!fm)
		return true;

	return fm->length > 0 && fm->curoff == fm->length;
}

int fmmap_close(struct fmmap *fm)
{
	if (!fm) {
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
	madvise(new, newsz, MADV_WILLNEED);
	madvise(new, newsz, MADV_SEQUENTIAL);

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
	if (msync(fm->addr, fm->mapsz, MS_ASYNC) < 0)
		return -1;

	return 0;
}
