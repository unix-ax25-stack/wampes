# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.15 1992/11/09 16:32:34 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = cnet mkhostdb netupdc netupds path pop2d udbm vitals

.IGNORE:

all_test:
	@if [ `uname -s` = Linux ] ; then \
	  make 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' all; \
	else \
	  make all; \
	fi

all:    $(TARGETS)

cnet:   cnet.c $(BUILDSADDR)
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) -I../src cnet.c $(BUILDSADDR) -lcurses -ltermcap -o $@

mkhostdb: mkhostdb.c
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	if [ `uname -s` = SunOS ] ; then \
	  $(CC) $(CFLAGS) mkhostdb.c -o $@ ; \
	else \
	  $(CC) $(CFLAGS) mkhostdb.c -lndbm -o $@ ; \
	fi

netupdc: netupdc.c $(BUILDSADDR)
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) -I../src netupdc.c $(BUILDSADDR) -o $@

netupds: netupds.c
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) netupds.c -o $@
	chmod 755 genupd

path:   path.c
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) path.c -o $@

pop2d:  pop2d.c
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) pop2d.c -o $@

udbm:   udbm.c
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) udbm.c -o $@

vitals: vitals.c
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
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
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp cnet $@ ; exit 0

/usr/local/etc/mkhostdb: mkhostdb
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp mkhostdb $@ ; exit 0

/usr/local/bin/path: path
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp path $@ ; exit 0

/usr/local/etc/udbm: udbm
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp udbm $@ ; exit 0

clean:
	rm -f *.o $(TARGETS)
