# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.19 1993/04/06 13:14:02 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
SHELL      = /bin/sh

TARGETS    = cnet mkhostdb netupdc netupds path pop2d udbm vitals

all:
	@if [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' $(TARGETS); \
	elif [ `uname -s` = ULTRIX ] ; then \
	  make -i 'CC=cc' 'CFLAGS=-DULTRIX_RISC' $(TARGETS); \
	else \
	  make -i 'CC=cc' 'CFLAGS=-O -s' $(TARGETS); \
	fi

cnet:   cnet.c $(BUILDSADDR)
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) -I../src cnet.c $(BUILDSADDR) -lcurses -ltermcap -o $@

mkhostdb: mkhostdb.c
	mv -f $@ /tmp/old.$$$$
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
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) -I../src netupdc.c $(BUILDSADDR) -o $@

netupds: netupds.c
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) netupds.c -o $@
	chmod 755 genupd

path:   path.c
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) path.c -o $@

pop2d:  pop2d.c
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) pop2d.c -o $@

udbm:   udbm.c
	mv -f $@ /tmp/old.$$$$
	$(CC) $(CFLAGS) udbm.c -o $@

vitals: vitals.c
	mv -f $@ /tmp/old.$$$$
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
	mv -f $@ /tmp/old.$$$$
	cp cnet $@

/usr/local/etc/mkhostdb: mkhostdb
	mv -f $@ /tmp/old.$$$$
	cp mkhostdb $@

/usr/local/bin/path: path
	mv -f $@ /tmp/old.$$$$
	cp path $@

/usr/local/etc/udbm: udbm
	mv -f $@ /tmp/old.$$$$
	cp udbm $@

clean:
	rm -f *.o $(TARGETS)
