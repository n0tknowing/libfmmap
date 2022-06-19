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

#ifndef FMMAP_H
#define FMMAP_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

#define FMMAP_MAX_SIZE  ((size_t)-1 - sizeof(size_t))

#define FMMAP_SEEK_SET	0
#define FMMAP_SEEK_CUR	1
#define FMMAP_SEEK_END	2

/* list of 'mode's for fmmap_open_length and fmmap_open */
#define FMMAP_RDONLY	(1 << 0)
#define FMMAP_WRONLY	(1 << 1)
#define FMMAP_RDWR	(1 << 2)
#define FMMAP_TRUNC	(1 << 3)
#define FMMAP_APPEND	(1 << 4)
/* only for fmmap_create */
#define FMMAP_EXCL	(1 << 5)

typedef struct fmmap fmmap;

/* open a file with fixed length.
 * 
 * cannot be used on stdin, stdout, and stderr since they are not
 * regular files or devices.
 */
fmmap *fmmap_open_length(const char *fname, int mode, size_t length);

/* fmmap_open() doesn't work on pseudo filesystem like procfs.
 * 
 * st_size from stat() returns 0 therefore use fmmap_open_length() instead
 * and provide the 'len' manually by yourself.
 */
fmmap *fmmap_open(const char *fname, int mode);
fmmap *fmmap_create(const char *fname, int mode, int perms);

size_t fmmap_read(fmmap *restrict fm, void *restrict buf, size_t length);
size_t fmmap_write(fmmap *restrict fm, const void *restrict buf, size_t length);

/* fmmap_seek() returns -1 and set ERANGE errno if offset is beyond
 * the actual file size.
 */
off_t fmmap_seek(fmmap *fm, off_t offset, int whence);

/* in practice, fmmap_seek() is enough to covers these three functions.
 * but i will leave it to provide easy interfaces.
 */
size_t fmmap_tell(fmmap *fm);
size_t fmmap_length(fmmap *fm);
void fmmap_rewind(fmmap *fm);

bool fmmap_iseof(fmmap *fm);
int fmmap_close(fmmap *fm);
#endif  /* FMMAP_H */
