# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.1 1991/04/16 11:43:33 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -Os
LDFLAGS    = -N
SHELL      = /bin/sh

.IGNORE:

all:    udbm netupdc netupds bridge path cnet

udbm:   udbm.c
	$(CC) $(CFLAGS) udbm.c $(LDFLAGS) -o $@

netupdc: netupdc.c $(BUILDSADDR)
	$(CC) $(CFLAGS) netupdc.c $(BUILDSADDR) $(LDFLAGS) -o $@
	chmod 700 $@

netupds: netupds.c
	$(CC) $(CFLAGS) netupds.c $(LDFLAGS) -o $@
	chmod 700 $@ genupd

bridge: bridge.c $(BUILDSADDR)
	$(CC) $(CFLAGS) bridge.c $(BUILDSADDR) $(LDFLAGS) -o $@
	chmod 700 $@

path:   path.c
	$(CC) $(CFLAGS) path.c $(LDFLAGS) -o $@

cnet:   cnet.c $(BUILDSADDR)
	$(CC) $(CFLAGS) cnet.c $(BUILDSADDR) $(LDFLAGS) -o $@
	chmod go-rwx cnet*

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
	rm -f udbm netupdc netupds bridge path cnet *.o

.SUFFIXES: .c,v

.c,v.c:
	co $<
