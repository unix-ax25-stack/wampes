# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.11 1993/05/17 13:46:45 deyke Exp $

CC         = c89
CFLAGS     = -O -s
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = connect connect2 filter mp nos-cb

all:
	make -i _all

_all:   $(TARGETS)

connect: connect.c
	$(RMTARGET)
	$(CC) $(CFLAGS) connect.c -o $@

connect2: connect2.c
	$(RMTARGET)
	$(CC) $(CFLAGS) connect2.c -o $@

filter: filter.c
	$(RMTARGET)
	$(CC) $(CFLAGS) filter.c -o $@

mp:     mp.c
	$(RMTARGET)
	$(CC) $(CFLAGS) mp.c -o $@

nos-cb: nos-cb.c
	$(RMTARGET)
	cc $(CFLAGS) nos-cb.c -o $@

clean:
	rm -f *.o $(TARGETS) *~
