# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.13 1993/05/10 11:24:17 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = bbs findpath killdup sfstat

all:
	@if [ $(OS) = SunOS ] ; then \
	  make -i 'CC=acc' 'CFLAGS=-O -s' _all; \
	elif [ $(OS) = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' _all; \
	elif [ $(OS) = 386BSD ] ; then \
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
	rm -f /usr/local/bin/BBS
	$(RMTARGET)
	cp bbs $@
	chmod 4555 $@
	ln $@ /usr/local/bin/BBS

/usr/local/bin/sfstat: sfstat
	$(RMTARGET)
	cp sfstat $@

/users/bbs/help: help
	$(RMTARGET)
	cp help $@
clean:
	rm -f *.o $(TARGETS) *~
