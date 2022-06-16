CC=gcc
CFLAGS=-std=c99 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE
OBJS=fmmap.o

ifdef SANITIZER
ifeq ($(SANITIZER),1)
	CFLAGS+=-g -Og -fsanitize=address,undefined
endif
else
	CFLAGS+=-O2
endif

all: ex1 ex2 ex3 ex4 ex5 ex6 ex7 ex8

ex1: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example.c -o $@

ex2: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example1.c -o $@

ex3: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example2.c -o $@

ex4: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example3.c -o $@

ex5: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example4.c -o $@

ex6: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example5.c -o $@

ex7: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example6.c -o $@

ex8: $(OBJS)
	$(CC) $(CFLAGS) $< examples/example7.c -o $@

clean:
	rm -f $(OBJS) ex1 ex2 ex3 ex4 ex5 ex6 ex7 ex8
