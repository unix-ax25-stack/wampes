# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/Makefile,v 1.21 1994/11/23 09:36:31 deyke Exp $

MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi

all:;   @-rm -f bbs/bbs.h src/linux_include/stdlib.h
	-chmod 755 cc
	-cd lib;     make -i -f $(MAKEFILE) all install
	-cd aos;     make -i -f $(MAKEFILE) all install
	-cd src;     make -i -f $(MAKEFILE) all install
	-cd convers; make -i -f $(MAKEFILE) all install
	-cd util;    make -i -f $(MAKEFILE) all install
	-cd bbs;     make -i -f $(MAKEFILE) all install
	-            make -i -f $(MAKEFILE) /tcp/hostaddr.pag
	-if [ -f users ]; then make -i -f $(MAKEFILE) /usr/local/lib/users; fi

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	/usr/local/etc/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

/usr/local/lib/users: users
	$(MKDIR)
	cp $? $@ && /usr/local/etc/udbm

clean:;

depend:;
