# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/util/Makefile,v 1.40 1993/10/13 22:31:22 deyke Exp $

CFLAGS     = -I../lib
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi
OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = cnet md5 mkhostdb netupdc netupds path pop2d qth udbm

all:;   chmod 755 genupd
	@if [ $(OS) = HP-UX ] ; then \
		make -i \
		'CC=c89' \
		'CFLAGS=-O -s -D_HPUX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -lndbm' \
		_all; \
	elif [ $(OS) = AIX ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -qchars=signed -D_ALL_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lcurses' \
		_all; \
	elif [ $(OS) = Linux ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap -ldbm' \
		_all; \
	elif [ $(OS) = 386BSD ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all; \
	elif [ $(OS) = BSD/386 ] ; then \
		make -i \
		'CC=cc' \
		'CFLAGS=-O -s -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all; \
	elif [ $(OS) = SunOS ] ; then \
		(if [ -x /usr/lang/acc ] ; then \
			make -i \
			'CC=acc' \
			'CFLAGS=-O -s -I../lib' \
			'LIBS=../lib/libutil.a -lcurses -ltermcap' \
			_all; \
		else \
			make -i \
			'CC=cc' \
			'CFLAGS=-O -s -I../lib' \
			'LIBS=../lib/libutil.a -lcurses -lsocket -lnsl' \
			_all; \
		fi) \
	elif [ $(OS) = ULTRIX ] ; then \
		make -i \
		'CC=gcc' \
		'CFLAGS=-O6 -s -DULTRIX_RISC -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap -ldbm' \
		_all; \
	elif [ $(OS) = A/UX ] ; then \
		make -i \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -s -D_POSIX_SOURCE -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap -ldbm -lposix' \
		_all; \
	elif [ $(OS) = "RISC iX" ] ; then \
		make -i \
		'CC=/tcp/cc' \
		'CFLAGS=-DRISCiX -I../lib' \
		'LIBS=../lib/libutil.a -lcurses -ltermcap' \
		_all; \
	else \
		echo System $(OS) is not supported; \
	fi

_all:   $(TARGETS)

cnet:   cnet.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -o $@

md5:    md5drivr.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) md5drivr.o $(LIBS) -o $@

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

qth:    qth.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? $(LIBS) -lm -o $@

udbm:   udbm.o
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
	/usr/local/bin/qth \
	/usr/local/etc/udbm

/usr/local/bin/cnet: cnet
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/md5: md5
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/etc/mkhostdb: mkhostdb
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/path: path
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/bin/qth: qth
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

/usr/local/etc/udbm: udbm
	$(MKDIR)
	$(RMTARGET)
	cp $? $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
cnet.o: ../lib/buildsaddr.h
md5drivr.o: ../lib/md5.h
netupdc.o: ../lib/buildsaddr.h ../lib/configure.h netupd.h ../lib/strdup.h
netupds.o: ../lib/configure.h netupd.h
path.o: ../lib/strdup.h
pop2d.o: ../lib/configure.h
udbm.o: ../lib/configure.h
