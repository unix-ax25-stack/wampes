# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.3 1991/11/22 16:17:01 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

.IGNORE:

all:    convers conversd

convers: convers.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src convers.c $(BUILDSADDR) -o $@

conversd: conversd.c $(BUILDSADDR)
	$(CC) $(CFLAGS) -I../src conversd.c $(BUILDSADDR) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp convers $@ ; exit 0

/tcp/conversd: conversd
	@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp conversd $@ ; exit 0

clean:
	rm -f *.o convers conversd
