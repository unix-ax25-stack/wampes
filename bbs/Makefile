# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/bbs/Makefile,v 1.16 1993/06/06 08:23:42 deyke Exp $

OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = bbs findpath killdup sfstat

all:;   @if [ $(OS) = HP-UX ] ; then \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = Linux ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DLINUX -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = 386BSD ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = SunOS ] ; then \
		make -i \
		'CC=acc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = ULTRIX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	elif [ $(OS) = A/UX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lposix' \
		_all; \
	elif [ $(OS) = "RISC iX" ] ; then \
		make -i \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'LIBS=../lib/libutil.a' \
		_all; \
	else \
		echo System $(OS) is not supported; \
	fi

_all:   $(TARGETS)

bbs:    bbs.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) bbs.c $(LIBS) -o $@

findpath: findpath.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) findpath.c $(LIBS) -o $@

killdup: killdup.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) killdup.c $(LIBS) -o $@

sfstat: sfstat.c bbs.h
	$(RMTARGET)
	$(CC) $(CFLAGS) sfstat.c $(LIBS) -o $@

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

clean:; rm -f *.o $(TARGETS) *~
