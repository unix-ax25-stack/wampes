# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.4 1991/06/05 16:34:15 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

.IGNORE:

all:    udbm netupdc netupds bridge path cnet vitals

udbm:   udbm.c
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) udbm.c -o $@

netupdc: netupdc.c $(BUILDSADDR)
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) -I../src netupdc.c $(BUILDSADDR) -o $@
	chmod 700 $@

netupds: netupds.c
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) netupds.c -o $@
	chmod 700 $@ genupd

bridge: bridge.c $(BUILDSADDR)
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) bridge.c $(BUILDSADDR) -o $@
	chmod 700 $@

path:   path.c
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) path.c -o $@

cnet:   cnet.c $(BUILDSADDR)
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) cnet.c $(BUILDSADDR) -o $@
	chmod go-rwx cnet*

vitals: vitals.c
	mv $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) vitals.c -o $@

install: \
	/usr/local/etc/udbm \
	netupdc \
	netupds \
	bridge \
	/usr/local/bin/path \
	/usr/local/bin/cnet

/usr/local/etc/udbm: udbm
	mv $@ /tmp/old.$$$$
	cp udbm $@

/usr/local/bin/path: path
	mv $@ /tmp/old.$$$$
	cp path $@

/usr/local/bin/cnet: cnet
	mv $@ /tmp/old.$$$$
	cp cnet $@
	chmod 700 $@

clean:
	rm -f udbm netupdc netupds bridge path cnet vitals *.o
