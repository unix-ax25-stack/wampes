# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.10 1993/05/10 11:22:58 deyke Exp $

CC         = c89
CFLAGS     = -O -s
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = connect connect2 filter nos-cb

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

nos-cb: nos-cb.c
	$(RMTARGET)
	cc $(CFLAGS) nos-cb.c -o $@

clean:
	rm -f *.o $(TARGETS) *~
