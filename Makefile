CC=gcc
CFLAGS=-std=c11 -Wall -Wextra -pedantic -Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wstrict-aliasing
OBJS=fmmap.o

ifdef DEBUG
ifeq ($(DEBUG),1)
	CFLAGS+=-g -Og
endif
ifeq ($(DEBUG),2)
	CFLAGS+=-g -Og -fsanitize=address,undefined
endif
else
	CFLAGS+=-O2
endif

ifdef FMMAP_MAX_SIZE
	CFLAGS+=-DFMMAP_MAX_SIZE=$(FMMAP_MAX_SIZE)
endif

all: append copyfile create fork read_all read_block_big read_block_small seek_and_tell seek_end

append: $(OBJS)
	$(CC) $(CFLAGS) $< examples/append.c -o $@

copyfile: $(OBJS)
	$(CC) $(CFLAGS) $< examples/copyfile.c -o $@

create: $(OBJS)
	$(CC) $(CFLAGS) $< examples/create.c -o $@

fork: $(OBJS)
	$(CC) $(CFLAGS) $< examples/fork.c -o $@

read_all: $(OBJS)
	$(CC) $(CFLAGS) $< examples/read_all.c -o $@

read_block_big: $(OBJS)
	$(CC) $(CFLAGS) $< examples/read_block_big.c -o $@

read_block_small: $(OBJS)
	$(CC) $(CFLAGS) $< examples/read_block_small.c -o $@

seek_and_tell: $(OBJS)
	$(CC) $(CFLAGS) $< examples/seek_and_tell.c -o $@

seek_end: $(OBJS)
	$(CC) $(CFLAGS) $< examples/seek_end.c -o $@

clean:
	rm -f $(OBJS) append copyfile create fork read_all read_block_big read_block_small seek_and_tell seek_end

