# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.9 1993/03/30 17:24:18 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = bbs findpath killdup sfstat

all:
	@if [ `uname -n` = mdddhd -o `uname -n` = deyke2 ] ; then \
	  make -i 'CFLAGS=-Aa -g' $(TARGETS); \
	elif [ `uname -s` = SunOS ] ; then \
	  make -i 'CC=acc' 'CFLAGS=-O -s' $(TARGETS); \
	elif [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' $(TARGETS); \
	else \
	  make -i $(TARGETS); \
	fi

bbs:    bbs.c bbs.h $(BUILDSADDR)
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) bbs.c $(BUILDSADDR) -o $@

findpath: findpath.c bbs.h
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) findpath.c -o $@

killdup: killdup.c bbs.h
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) killdup.c -o $@

sfstat: sfstat.c bbs.h
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) sfstat.c -o $@

install: /usr/local/bin/bbs findpath killdup /usr/local/bin/sfstat /users/bbs/help

/usr/local/bin/bbs: bbs
	cp bbs /usr/local/bin/bbs.tmp
	cd /usr/local/bin; chmod 4555 bbs.tmp; mv -f bbs bbs.old.$$$$; mv -f bbs.tmp bbs; rm -f BBS; ln bbs BBS; rm -f bbs.old.*

/usr/local/bin/sfstat: sfstat
	mv -f $@ /tmp/old.$$$$
	cp sfstat $@

/users/bbs/help: help
	rm -f $@
	cp help $@
clean:
	rm -f *.o $(TARGETS)
