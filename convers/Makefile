# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.13 1993/05/17 13:48:34 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
OS         = "`uname -s`"
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = convers conversd

all:
	@if [ $(OS) = HP-UX ] ; then \
	  make -i 'CC=c89' 'CFLAGS=-O -s' _all; \
	elif [ $(OS) = SunOS ] ; then \
	  make -i 'CC=acc' 'CFLAGS=-O -s' _all; \
	elif [ $(OS) = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' _all; \
	elif [ $(OS) = ULTRIX ] ; then \
	  make -i 'CC=cc' 'CFLAGS=-DULTRIX_RISC' _all; \
	elif [ $(OS) = "RISC iX" ] ; then \
	  make -i 'CC=/tcp/cc' 'CFLAGS=-DRISCiX' _all; \
	else \
	  make -i 'CC=cc' 'CFLAGS=-O -s' _all; \
	fi

_all:   $(TARGETS)

convers: convers.c $(BUILDSADDR)
	$(RMTARGET)
	$(CC) $(CFLAGS) -I../src convers.c $(BUILDSADDR) $(LIBS) -o $@

conversd: conversd.c $(BUILDSADDR)
	$(RMTARGET)
	$(CC) $(CFLAGS) -I../src conversd.c $(BUILDSADDR) $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	$(RMTARGET)
	cp convers $@

/tcp/conversd: conversd
	$(RMTARGET)
	cp conversd $@

clean:
	rm -f *.o $(TARGETS) *~
