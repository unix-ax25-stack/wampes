# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.15 1993/09/17 09:32:47 deyke Exp $

CC         = c89
CFLAGS     = -O -s -D_HPUX_SOURCE
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = connect connect2 filter makeiprt mp nos-cb

all:;   make -i _all

_all:   $(TARGETS)

connect: connect.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@

connect2: connect2.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@

filter: filter.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@

makeiprt: makeiprt.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@

mp:     mp.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@

nos-cb.o: nos-cb.c
	cc $(CFLAGS) $? -c

nos-cb: nos-cb.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
