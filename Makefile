# @(#) $Id: Makefile,v 1.34 1998/03/09 20:15:16 deyke Exp $

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
	-cd NeXT;    $(MAKE) -i -f $(MAKEFILE) all install
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
		*.R \
		ChangeLog \
		Makefile \
		NeXT/*.[ch] \
		NeXT/*.sh \
		NeXT/Makefile \
		NeXT/README.NeXT \
		NeXT/uname-next \
		README \
		aos/*.[ch] \
		aos/Makefile \
		bbs/*.[ch] \
		bbs/Makefile \
		bbs/bbs.help \
		cc \
		convers/*.[ch] \
		convers/Makefile \
		doc/?*.* \
		domain.txt \
		examples/?*.* \
		hosts \
		lib/*.[ch] \
		lib/Makefile \
		lib/configure \
		src/*.[ch] \
		src/Makefile \
		src/cc \
		src/linux_include/*/*.h \
		util/*.[ch] \
		util/Makefile \
		! -name configure.h -print`; \
	tar cvf - $$sources | gzip -9 -v > wampes-$$version.tar.gz; \
	cp README wampes-$$version.txt

clean:;

depend:;
