# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.8 1993/03/30 17:23:33 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

TARGETS    = convers conversd

all:
	@if [ `uname -s` = Linux ] ; then \
	  make -i 'CC=gcc' 'CFLAGS=-O6 -s -DLINUX' $(TARGETS); \
	else \
	  make -i $(TARGETS); \
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
