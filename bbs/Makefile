# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.11 1993/04/11 07:07:11 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi
SHELL      = /bin/sh

TARGETS    = bbs findpath killdup sfstat

all:
	@if [ `uname -s` = SunOS ] ; then \
	  make -i 'CC=acc' 'CFLAGS=-O -s' _all; \
	elif [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' _all; \
	elif [ `uname -s` = 386BSD ] ; then \
	  make -i 'CC=cc' 'CFLAGS=-O -s' _all; \
	else \
	  make -i 'CC=c89' 'CFLAGS=-O -s' _all; \
	fi

_all:   $(TARGETS)

bbs:    bbs.c bbs.h $(BUILDSADDR)
	$(RMTARGET)
	$(CC) $(CFLAGS) bbs.c $(BUILDSADDR) -o $@

findpath: findpath.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) findpath.c -o $@

killdup: killdup.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) killdup.c -o $@

sfstat: sfstat.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) sfstat.c -o $@

install: /usr/local/bin/bbs findpath killdup /usr/local/bin/sfstat /users/bbs/help

/usr/local/bin/bbs: bbs
	cp bbs /usr/local/bin/bbs.tmp
	cd /usr/local/bin; chmod 4555 bbs.tmp; mv -f bbs bbs.old.$$$$; mv -f bbs.tmp bbs; rm -f BBS; ln bbs BBS; rm -f bbs.old.*

/usr/local/bin/sfstat: sfstat
	$(RMTARGET)
	cp sfstat $@

/users/bbs/help: help
	rm -f $@
	cp help $@
clean:
	rm -f *.o $(TARGETS) *~
