# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.20 1993/04/11 07:07:25 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi
SHELL      = /bin/sh

TARGETS    = cnet mkhostdb netupdc netupds path pop2d udbm vitals

all:
	@if [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' _all; \
	elif [ `uname -s` = ULTRIX ] ; then \
	  make -i 'CC=cc' 'CFLAGS=-DULTRIX_RISC' _all; \
	else \
	  make -i 'CC=cc' 'CFLAGS=-O -s' _all; \
	fi

_all:   $(TARGETS)

cnet:   cnet.c $(BUILDSADDR)
	$(RMTARGET)
	$(CC) $(CFLAGS) -I../src cnet.c $(BUILDSADDR) -lcurses -ltermcap -o $@

mkhostdb: mkhostdb.c
	$(RMTARGET)
	@if [ `uname -s` = SunOS ] ; then \
	  $(CC) $(CFLAGS) mkhostdb.c -o $@ ; \
	elif [ `uname -s` = 386BSD ] ; then \
	  $(CC) $(CFLAGS) mkhostdb.c -o $@ ; \
	elif [ `uname -s` = Linux ] ; then \
	  $(CC) $(CFLAGS) mkhostdb.c -ldbm -o $@ ; \
	elif [ `uname -s` = ULTRIX ] ; then \
	  $(CC) $(CFLAGS) mkhostdb.c -ldbm -o $@ ; \
	else \
	  $(CC) $(CFLAGS) mkhostdb.c -lndbm -o $@ ; \
	fi

netupdc: netupdc.c $(BUILDSADDR)
	$(RMTARGET)
	$(CC) $(CFLAGS) -I../src netupdc.c $(BUILDSADDR) -o $@

netupds: netupds.c
	$(RMTARGET)
	$(CC) $(CFLAGS) netupds.c -o $@
	chmod 755 genupd

path:   path.c
	$(RMTARGET)
	$(CC) $(CFLAGS) path.c -o $@

pop2d:  pop2d.c
	$(RMTARGET)
	$(CC) $(CFLAGS) pop2d.c -o $@

udbm:   udbm.c
	$(RMTARGET)
	$(CC) $(CFLAGS) udbm.c -o $@

vitals: vitals.c
	$(RMTARGET)
	$(CC) $(CFLAGS) vitals.c -o $@

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

clean:
	rm -f *.o $(TARGETS) *~
