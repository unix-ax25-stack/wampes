# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.8 1992/09/30 15:54:58 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CC         = c89
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = bbs findpath killdup sfstat

.IGNORE:

all_test:
	@if [ `uname -s` = Linux ] ; then \
	  make 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' all; \
	else \
	  make all; \
	fi

all:    $(TARGETS)

bbs:    bbs.c bbs.h $(BUILDSADDR)
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) bbs.c $(BUILDSADDR) -o $@

findpath: findpath.c bbs.h
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) findpath.c -o $@

killdup: killdup.c bbs.h
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) killdup.c -o $@

sfstat: sfstat.c bbs.h
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) sfstat.c -o $@

install: /usr/local/bin/bbs findpath killdup /usr/local/bin/sfstat /users/bbs/help

/usr/local/bin/bbs: bbs
	cp bbs /usr/local/bin/bbs.tmp ; exit 0
	cd /usr/local/bin; chmod 4555 bbs.tmp; mv -f bbs bbs.old.$$$$; mv -f bbs.tmp bbs; rm -f BBS; ln -f bbs BBS; rm -f bbs.old.*; exit 0

/usr/local/bin/sfstat: sfstat
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp sfstat $@ ; exit 0

/users/bbs/help: help
	rm -f $@ ; exit 0
	cp help $@
clean:
	rm -f *.o $(TARGETS)
