# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.9 1993/04/02 14:25:25 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
SHELL      = /bin/sh

TARGETS    = convers conversd

all:
	@if [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' $(TARGETS); \
	else \
	  make -i 'CC=cc' 'CFLAGS=-O -s' $(TARGETS); \
	fi

convers: convers.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src convers.c $(BUILDSADDR) $(LIBS) -o $@

conversd: conversd.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src conversd.c $(BUILDSADDR) $(LIBS) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	mv -f $@ /tmp/old.$$$$
	cp convers $@

/tcp/conversd: conversd
	mv -f $@ /tmp/old.$$$$
	cp conversd $@

clean:
	rm -f *.o $(TARGETS)
