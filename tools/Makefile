# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.4 1992/09/05 08:15:17 deyke Exp $

CC         = c89
CFLAGS     = -O -s -D_HPUX_SOURCE
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
