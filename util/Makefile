# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.12 1992/09/01 16:58:44 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

.IGNORE:

all:    cnet netupdc netupds path pop2d udbm vitals

cnet:   cnet.c $(BUILDSADDR)
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) -I../src cnet.c $(BUILDSADDR) -lcurses -ltermcap -o $@

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
	netupdc \
	netupds \
	/usr/local/bin/path \
	pop2d \
	/usr/local/etc/udbm \
	vitals

/usr/local/bin/cnet: cnet
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp cnet $@ ; exit 0

/usr/local/bin/path: path
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp path $@ ; exit 0

/usr/local/etc/udbm: udbm
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp udbm $@ ; exit 0

clean:
	rm -f *.o cnet netupdc netupds path pop2d udbm vitals
