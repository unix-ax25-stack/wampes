# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.12 1993/06/06 08:24:08 deyke Exp $

CC         = c89
CFLAGS     = -O -s
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS    = connect connect2 filter mp nos-cb

all:;   make -i _all

_all:   $(TARGETS)

nos-cb: nos-cb.c
	$(RMTARGET)
	cc $(CFLAGS) nos-cb.c -o $@

.c:
	$(RMTARGET)
	$(CC) $(CFLAGS) $< -o $@

clean:; rm -f *.o $(TARGETS) *~
