# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.5 1992/09/07 19:19:59 deyke Exp $

CC         = c89
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = connect connect2

.IGNORE:

all:    $(TARGETS)

connect: connect.c
	$(CC) $(CFLAGS) connect.c -o $@

connect2: connect2.c
	$(CC) $(CFLAGS) connect2.c -o $@

clean:
	rm -f *.o $(TARGETS)
