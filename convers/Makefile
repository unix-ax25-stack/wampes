# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.11 1993/04/11 07:05:59 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi
SHELL      = /bin/sh

TARGETS    = convers conversd

all:
	@if [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' _all; \
	elif [ `uname -s` = ULTRIX ] ; then \
	  make -i 'CC=cc' 'CFLAGS=-DULTRIX_RISC' _all; \
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
