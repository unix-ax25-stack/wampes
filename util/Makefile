# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.6 1991/09/17 22:16:31 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

.IGNORE:

all:    bridge cnet netupdc netupds path pop2d udbm vitals

bridge: bridge.c $(BUILDSADDR)
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) bridge.c $(BUILDSADDR) -o $@

cnet:   cnet.c $(BUILDSADDR)
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) cnet.c $(BUILDSADDR) -o $@

netupdc: netupdc.c $(BUILDSADDR)
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) -I../src netupdc.c $(BUILDSADDR) -o $@

netupds: netupds.c
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) netupds.c -o $@
	chmod 755 genupd

path:   path.c
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) path.c -o $@

pop2d:  pop2d.c
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) pop2d.c -o $@

udbm:   udbm.c
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) udbm.c -o $@

vitals: vitals.c
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	$(CC) $(CFLAGS) vitals.c -o $@

install: \
	bridge \
	/usr/local/bin/cnet \
	netupdc \
	netupds \
	/usr/local/bin/path \
	pop2d \
	/usr/local/etc/udbm \
	vitals

/usr/local/bin/cnet: cnet
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp cnet $@ ; exit 0

/usr/local/bin/path: path
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp path $@ ; exit 0

/usr/local/etc/udbm: udbm
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp udbm $@ ; exit 0

clean:
	rm -f *.o bridge cnet netupdc netupds path pop2d udbm vitals
