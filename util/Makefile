# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.3 1991/05/07 18:24:09 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s -N
SHELL      = /bin/sh

.IGNORE:

all:    udbm netupdc netupds bridge path cnet vitals

udbm:   udbm.c
	$(CC) $(CFLAGS) udbm.c -o $@

netupdc: netupdc.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src netupdc.c $(BUILDSADDR) -o $@
	chmod 700 $@

netupds: netupds.c
	$(CC) $(CFLAGS) netupds.c -o $@
	chmod 700 $@ genupd

bridge: bridge.c $(BUILDSADDR)
	$(CC) $(CFLAGS) bridge.c $(BUILDSADDR) -o $@
	chmod 700 $@

path:   path.c
	$(CC) $(CFLAGS) path.c -o $@

cnet:   cnet.c $(BUILDSADDR)
	$(CC) $(CFLAGS) cnet.c $(BUILDSADDR) -o $@
	chmod go-rwx cnet*

vitals: vitals.c
	$(CC) $(CFLAGS) vitals.c -o $@

install: \
	/usr/local/etc/udbm \
	netupdc \
	netupds \
	bridge \
	/usr/local/bin/path \
	/usr/local/bin/cnet

/usr/local/etc/udbm: udbm
	cp udbm $@

/usr/local/bin/path: path
	cp path $@

/usr/local/bin/cnet: cnet
	cp cnet $@
	chmod 700 $@

clean:
	rm -f udbm netupdc netupds bridge path cnet vitals *.o

.SUFFIXES: .c,v

.c,v.c:
	co $<
