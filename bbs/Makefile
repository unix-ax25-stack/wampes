# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.5 1992/08/25 09:29:28 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CC         = c89
CFLAGS     = -O -s
SHELL      = /bin/sh

.IGNORE:

all:    bbs findpath killdup sfstat

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

install: /usr/local/bin/bbs findpath killdup /usr/local/bin/sfstat

/usr/local/bin/bbs: bbs
	cp bbs /usr/local/bin/bbs.tmp ; exit 0
	cd /usr/local/bin; chmod 4555 bbs.tmp; mv -f bbs bbs.old.$$$$; mv -f bbs.tmp bbs; rm -f BBS; ln -f bbs BBS; rm -f bbs.old.*; exit 0

/usr/local/bin/sfstat: sfstat
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp sfstat $@ ; exit 0

clean:
	rm -f *.o bbs findpath killdup sfstat

