CC=gcc
CFLAGS=-std=c99 -g -O2 -Wall -Wextra -pedantic -D_DEFAULT_SOURCE
OBJS=fmmap.o

all: ex1 ex2 ex3 ex4 ex5

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

clean:
	rm -f $(OBJS) ex1 ex2 ex3 ex4 ex5
