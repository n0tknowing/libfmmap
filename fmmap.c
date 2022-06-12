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
static int fmmap_sync(struct fmmap *, size_t);

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
	if (!file) {
		errno = EINVAL;
		return NULL;
	}

	if (filelen >= FMMAP_MAX_SIZE) {
		errno = ERANGE;
		return NULL;
	}

	void *buf;
	struct fmmap *fm;
	int fd, fd_flag, fm_mode, fm_flag;
	size_t mapsz = filelen;

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

	fm_flag = MAP_SHARED;
	buf = mmap(NULL, mapsz, fm_mode, fm_flag, fd, 0);
	if (buf == MAP_FAILED)
		goto close_fd;

	if ((mode & FMMAP_TRUNC) == FMMAP_TRUNC) {
		if ((mode & FMMAP_RDONLY) == FMMAP_RDONLY) {
			errno = EPERM;
			goto delete_map;
		} else if ((mode & FMMAP_APPEND) == FMMAP_APPEND) {
			goto skip_trunc;
		}

		memset(buf, 0, filelen);
		filelen = 0;
	}

skip_trunc:
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

	if (!S_ISREG(sb.st_mode)) {
		errno = EINVAL;
		return NULL;
	}

	return fmmap_open_length(file, mode, sb.st_size);
}

struct fmmap *fmmap_create(const char *filename, int perms)
{
	if (!filename) {
		errno = EINVAL;
		return NULL;
	}

	void *buf;
	struct fmmap *fm;
	int fd;

	fd = open(filename, O_RDWR|O_CREAT|O_EXCL|O_CLOEXEC, perms);
	if (fd < 0)
		return NULL;

	write(fd, "\n", 1);

	buf = mmap(NULL, FMMAP_NEWFMAPSZ, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (buf == MAP_FAILED)
		goto close_fd;

	/* ignore these madvise if error */
	madvise(buf, FMMAP_NEWFMAPSZ, MADV_WILLNEED);
	madvise(buf, FMMAP_NEWFMAPSZ, MADV_SEQUENTIAL);

	fm = malloc(sizeof(struct fmmap));
	if (!fm)
		goto delete_map;

	fm->addr = buf;
	fm->mapsz = FMMAP_NEWFMAPSZ;
	fm->length = 1;
	fm->mode = FMMAP_RDWR;
	fm->curoff = 0;
	fm->fd = fd;

	return fm;

delete_map:
	munmap(buf, FMMAP_NEWFMAPSZ);

close_fd:
	close(fd);
	return NULL;
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
	size_t count = 0;

	if (!size)
		return 0;

	if ((fm->mode & FMMAP_APPEND) == FMMAP_APPEND)
		fm->curoff = fm->length;

	if (fmmap_remap(fm, fm->length + size) < 0)
		return 0;

	while (size > FMMAP_BLOCK_SIZE) {
		memmove((uint8_t *)fm->addr + fm->curoff, sptr + count, FMMAP_BLOCK_SIZE);
		fm->curoff += FMMAP_BLOCK_SIZE;
		size       -= FMMAP_BLOCK_SIZE;
		count      += FMMAP_BLOCK_SIZE;
		if (fmmap_sync(fm, fm->curoff) < 0)
			return 0;
	}

	if (size > 0) {
		memmove((uint8_t *)fm->addr + fm->curoff, sptr + count, size);
		fm->curoff += size;
		count      += size;
		if (fmmap_sync(fm, fm->curoff) < 0)
			return 0;
	}

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

/* private functions */
static int fmmap_remap(struct fmmap *fm, size_t newsz)
{
	if (newsz <= fm->mapsz)
		return 0;

	if (munmap(fm->addr, fm->mapsz) < 0)
		return -1;

	fm->addr = NULL;
	fm->mapsz = 0;

	int fm_mode = PROT_WRITE, save = errno;
	if (fm->mode == FMMAP_RDWR)
		fm_mode |= PROT_READ;

	void *new = mmap(NULL, newsz, fm_mode, MAP_SHARED, fm->fd, 0);
	if (new == MAP_FAILED) {
		fm->addr = NULL;
		return -1;
	}

	madvise(new, newsz, MADV_WILLNEED);
	madvise(new, newsz, MADV_SEQUENTIAL);

	fm->addr = new;
	fm->mapsz = newsz;

	errno = save;
	return 0;
}

static int fmmap_sync(struct fmmap *fm, size_t newflen)
{
	if (newflen <= fm->length)
		return 0;

	if (ftruncate(fm->fd, newflen) < 0)
		return -1;

	if (munmap(fm->addr, fm->mapsz) < 0)
		return -1;

	fm->addr = NULL;

	int fm_mode = PROT_WRITE, save = errno;
	if (fm->mode == FMMAP_RDWR)
		fm_mode |= PROT_READ;

	void *new = mmap(NULL, fm->mapsz, fm_mode, MAP_SHARED, fm->fd, 0);
	if (new == MAP_FAILED) {
		fm->addr = NULL;
		return -1;
	}

	madvise(new, fm->mapsz, MADV_WILLNEED);
	madvise(new, fm->mapsz, MADV_SEQUENTIAL);

	fm->addr = new;
	fm->length = newflen;

	errno = save;
	return 0;
}
