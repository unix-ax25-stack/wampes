# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/tools/Makefile,v 1.19 1994/04/09 15:30:44 deyke Exp $

CC         = c89
CFLAGS     = -O -s -D_HPUX_SOURCE
MAKEFILE   = Makefile
RMTARGET   = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

TARGETS = \
	check_seteugid \
	connect \
	connect2 \
	filter \
	makeiprt \
	mp \
	nos-cb

all:;   make -i -f $(MAKEFILE) _all

_all:   $(TARGETS)

check_seteugid: check_seteugid.o
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -o $@
	mk chown root $@
	mk chmod 4555 $@

nos-cb: nos-cb.c
	$(RMTARGET)
	@sleep 1
	cc $(CFLAGS) $? -o $@

.c:
	$(RMTARGET)
	@sleep 1
	$(CC) $(CFLAGS) $? -lndbm -o $@

clean:; rm -f *.o $(TARGETS) *~

depend:; @mkmf -f $(MAKEFILE)
###
check_seteugid.o: ../lib/seteugid.c ../lib/seteugid.h
