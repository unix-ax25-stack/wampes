# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/Makefile,v 1.31 1996/08/11 18:15:53 deyke Exp $

MAKEFILE   = Makefile
MKDIR      = @if [ ! -d `dirname $@` ]; then mkdir -p `dirname $@`; fi

OBSOLETE   = bbs/bbs.h \
	     bbs/findpath* \
	     bbs/help* \
	     bbs/killdup* \
	     bbs/sfstat* \
	     doc/bbs.2.gif \
	     lib/bbs.h \
	     src/linux_include/stdlib.h \
	     src/mail_bbs.* \
	     users \
	     util/genupd \
	     /usr/local/bin/sfstat

all:;   @-rm -f $(OBSOLETE)
	-chmod 755 cc
	-cd lib;     $(MAKE) -i -f $(MAKEFILE) all install
	-cd aos;     $(MAKE) -i -f $(MAKEFILE) all install
	-cd src;     $(MAKE) -i -f $(MAKEFILE) all install
	-cd convers; $(MAKE) -i -f $(MAKEFILE) all install
	-cd util;    $(MAKE) -i -f $(MAKEFILE) all install
	-cd bbs;     $(MAKE) -i -f $(MAKEFILE) all install
	-            $(MAKE) -i -f $(MAKEFILE) /tcp/hostaddr.pag
	-if [ -d tools ]; then \
	 cd tools;   $(MAKE) -i -f $(MAKEFILE) all install; \
	 fi

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	/usr/local/etc/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

distrib:
	@version=`awk -F- '/.#.WAMPES-/ {print substr($$2,1,6)}' < src/version.c`; \
	sources=`find \
		aos/Makefile \
		aos/*.[ch] \
		bbs/bbs.help \
		bbs/Makefile \
		bbs/*.[ch] \
		cc \
		ChangeLog \
		convers/Makefile \
		convers/*.[ch] \
		doc/?*.* \
		domain.txt \
		examples/?*.* \
		hosts \
		lib/configure \
		lib/Makefile \
		lib/*.[ch] \
		Makefile \
		README \
		src/cc \
		src/linux_include/*/*.h \
		src/Makefile \
		src/*.[ch] \
		util/Makefile \
		util/*.[ch] \
		*.R \
		! -name configure.h -print`; \
	tar cvf - $$sources | gzip -9 -v > wampes-$$version.tar.gz; \
	cp README wampes-$$version.txt

clean:;

depend:;
