# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.24 1993/06/06 08:23:34 deyke Exp $

OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = cnet mkhostdb netupdc netupds path pop2d udbm vitals

all:;   chmod 755 genupd
	@if [ $(OS) = HP-UX ] ; then \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lndbm' \
		_all; \
	elif [ $(OS) = Linux ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DLINUX -I../lib' \
		'LIBS=../lib/libutil.a -ldbm' \
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
		'LIBS=../lib/libutil.a -ldbm' \
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

cnet:   cnet.c
	$(RMTARGET)
	$(CC) $(CFLAGS) cnet.c -lcurses -ltermcap $(LIBS) -o $@

.c:
	$(RMTARGET)
	$(CC) $(CFLAGS) $< $(LIBS) -o $@

install: \
	/usr/local/bin/cnet \
	/usr/local/etc/mkhostdb \
	netupdc \
	netupds \
	/usr/local/bin/path \
	pop2d \
	/usr/local/etc/udbm \
	vitals

/usr/local/bin/cnet: cnet
	$(RMTARGET)
	cp cnet $@

/usr/local/etc/mkhostdb: mkhostdb
	$(RMTARGET)
	cp mkhostdb $@

/usr/local/bin/path: path
	$(RMTARGET)
	cp path $@

/usr/local/etc/udbm: udbm
	$(RMTARGET)
	cp udbm $@

clean:; rm -f *.o $(TARGETS) *~
