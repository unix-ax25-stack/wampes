# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.8 1993/03/30 17:23:46 deyke Exp $

CC         = c89
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = connect connect2 filter nos-cb

all:
	make -i $(TARGETS)

connect: connect.c
	$(CC) $(CFLAGS) connect.c -o $@

connect2: connect2.c
	$(CC) $(CFLAGS) connect2.c -o $@

filter: filter.c
	$(CC) $(CFLAGS) filter.c -o $@

nos-cb: nos-cb.c
	cc $(CFLAGS) nos-cb.c -o $@

clean:
	rm -f *.o $(TARGETS)
