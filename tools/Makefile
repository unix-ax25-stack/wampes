# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.17 1994/01/24 13:59:37 deyke Exp $

CC         = c89
CFLAGS     = -O -s -D_HPUX_SOURCE
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS = \
	check_seteugid \
	connect \
	connect2 \
	filter \
	makeiprt \
	mp \
	nos-cb

all:;   make -i _all

_all:   $(TARGETS)

nos-cb: nos-cb.c
	$(RMTARGET)
	@sleep 1
	cc $(CFLAGS) $? -o $@

.c:
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -lndbm -o $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf
###
check_seteugid.o: ../lib/seteugid.c ../lib/seteugid.h
