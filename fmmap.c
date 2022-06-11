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

#define MAX(x,y)	((x) > (y) ? (x) : (y))
#define MIN(x,y)	((x) < (y) ? (x) : (y))
#define FMMAP_BLOCK_SIZE  (1 << 16)
#define CUROFF_OVERFLOW(f) ((f)->curoff > (f)->length)

struct fmmap {
	void *addr;
	size_t length;
	size_t curoff;
	int mode;
	int fd;
};

struct fmmap *fmmap_open_length(const char *file, int mode, size_t filelen)
{
	if (!file || mode < 0 || mode > 3) {
		errno = EINVAL;
		return NULL;
	}

	void *buf;
	struct fmmap *fm;
	int fd, fd_flag, fm_mode, fm_flag;

	fd_flag = O_RDONLY;
	fm_mode = PROT_READ;

	if ((mode & FMMAP_WRONLY) == FMMAP_WRONLY) {
		fd_flag = O_RDWR;
		fm_mode = PROT_WRITE;
	} else if ((mode & FMMAP_RDWR) == FMMAP_RDWR) {
		fd_flag = O_RDWR;
		fm_mode = PROT_READ | PROT_WRITE;
	}

	if (filelen >= FMMAP_MAX_SIZE) {
		errno = ERANGE;
		return NULL;
	}

	fd = open(file, fd_flag | O_CLOEXEC);
	if (fd < 0)
		return NULL;

	fm_flag = MAP_SHARED;
	buf = mmap(NULL, filelen, fm_mode, fm_flag, fd, 0);
	if (buf == MAP_FAILED)
		goto close_fd;

	if (madvise(buf, filelen, MADV_WILLNEED) < 0)
		goto delete_map;

	if (madvise(buf, filelen, MADV_SEQUENTIAL) < 0)
		goto delete_map;

	fm = malloc(sizeof(struct fmmap));
	if (!fm)
		goto delete_map;

	fm->addr = buf;
	fm->length = filelen;
	fm->mode = mode;
	fm->curoff = 0;
	fm->fd = fd;

	return fm;

delete_map:
	munmap(buf, filelen);

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

size_t fmmap_read(fmmap *restrict fm, void *restrict ptr, size_t size)
{
	if (!fm || fm->mode == FMMAP_WRONLY || !ptr) {
		errno = EINVAL;
		return 0;
	}

	uint8_t *dptr = ptr;
	size_t count = 0, copysz;

	if (!size)
		return 0;

	if (size > FMMAP_BLOCK_SIZE) {
		while (size > FMMAP_BLOCK_SIZE && fm->curoff < fm->length) {
			copysz = MIN(size, FMMAP_BLOCK_SIZE);
			memcpy(dptr + count, (uint8_t *)fm->addr + fm->curoff, copysz);
			fm->curoff += copysz;
			size       -= copysz;
			count      += copysz;
		}
	}

	if (size > 0 && fm->curoff < fm->length) {
		copysz = MIN(size, fm->length - fm->curoff);
		memcpy(dptr + count, (uint8_t *)fm->addr + fm->curoff, copysz);
		fm->curoff += copysz;
		count      += copysz;
	}

	if (CUROFF_OVERFLOW(fm))
		fm->curoff = fm->length;

	return count;
}

size_t fmmap_write(fmmap *restrict fm, const void *restrict ptr, size_t size)
{
	if (!fm || fm->mode == FMMAP_RDONLY || !ptr) {
		errno = EINVAL;
		return 0;
	}

	size_t count = 0;

	if (!size)
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
	if (!fm)
		return -1;

	int r;

	(void)close(fm->fd); /* ignore if error */
	r = munmap(fm->addr, fm->length);
	fm->length = fm->mode = fm->curoff = 0;
	free(fm);

	return r;
}
