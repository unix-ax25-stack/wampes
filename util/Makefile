# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.27 1993/06/17 07:28:04 deyke Exp $

CFLAGS     = -I../lib
OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = cnet md5 mkhostdb netupdc netupds path pop2d udbm vitals

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
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -ldbm -lposix' \
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

cnet:   cnet.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -lcurses -ltermcap $(LIBS) -o $@

md5:    md5.o md5drivr.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) md5.o md5drivr.o $(LIBS) -o $@

mkhostdb: mkhostdb.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

netupdc: netupdc.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

netupds: netupds.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

path:   path.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

pop2d:  pop2d.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

udbm:   udbm.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

vitals: vitals.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

install: \
	/usr/local/bin/cnet \
	/usr/local/bin/md5 \
	/usr/local/etc/mkhostdb \
	netupdc \
	netupds \
	/usr/local/bin/path \
	pop2d \
	/usr/local/etc/udbm \
	vitals

/usr/local/bin/cnet: cnet
	$(RMTARGET)
	cp $? $@

/usr/local/bin/md5: md5
	$(RMTARGET)
	cp $? $@

/usr/local/etc/mkhostdb: mkhostdb
	$(RMTARGET)
	cp $? $@

/usr/local/bin/path: path
	$(RMTARGET)
	cp $? $@

/usr/local/etc/udbm: udbm
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
cnet.o: ../lib/buildsaddr.h
md5.o: md5.h
md5drivr.o: md5.h
netupdc.o: ../lib/buildsaddr.h netupd.h ../lib/strdup.h
netupds.o: netupd.h
path.o: ../lib/strdup.h
