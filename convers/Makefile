# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/convers/Makefile,v 1.1 1991/06/19 10:12:38 deyke Exp $

BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -O -s
SHELL      = /bin/sh

.IGNORE:

all:    convers conversd

convers: convers.c $(BUILDSADDR)
	$(CC) $(CFLAGS) convers.c $(BUILDSADDR) -o $@

conversd: conversd.c $(BUILDSADDR)
	$(CC) $(CFLAGS) conversd.c $(BUILDSADDR) -o $@

install: /usr/local/bin/convers /tcp/conversd

/usr/local/bin/convers: convers
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp convers $@ ; exit 0

/tcp/conversd: conversd
	@mv $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
	cp conversd $@ ; exit 0

clean:
	rm -f *.o convers conversd
