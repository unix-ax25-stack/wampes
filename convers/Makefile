# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.7 1992/09/30 15:53:55 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = convers conversd

.IGNORE:

all_test:
	@if [ `uname -s` = Linux ] ; then \
	  make 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' all; \
	else \
	  make all; \
	fi

all:    $(TARGETS)

convers: convers.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src convers.c $(BUILDSADDR) $(LIBS) -o $@

conversd: conversd.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src conversd.c $(BUILDSADDR) $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp convers $@ ; exit 0

/tcp/conversd: conversd
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp conversd $@ ; exit 0

linux:
	@make "CFLAGS=-Os -DLINUX" "LIBS=-linet" all

isc:
	@make "CFLAGS=-Os -DISC -DISC3" "LIBS=-linet" all

clean:
	rm -f *.o $(TARGETS)
