# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.3 1992/08/25 09:28:36 deyke Exp $

CC         = c89
CFLAGS     = -O -s -D_HPUX_SOURCE
SHELL      = /bin/sh

TARGETS    = \
	     connect \
	     connect2

.IGNORE:

all:    $(TARGETS)

connect: connect.c
	$(CC) $(CFLAGS) connect.c -o $@

connect2: connect2.c
	$(CC) $(CFLAGS) connect2.c -o $@

clean:
	rm -f $(TARGETS) *.o core
