# @(#) $Id: Makefile,v 1.48 2000/02/12 19:39:20 deyke Exp $

OBSOLETE   = /usr/local/bin/sfstat \
	     /usr/local/etc/mkhostdb \
	     bbs/bbs.h \
	     bbs/findpath* \
	     bbs/help* \
	     bbs/killdup* \
	     bbs/sfstat* \
	     doc/bbs.2.gif \
	     lib/bbs.h \
	     netrom_links \
	     src/bootp.h \
	     src/cc \
	     src/config.h \
	     src/enet.h \
	     src/hardware.h \
	     src/linux_include/stdlib.h \
	     src/mail_bbs.* \
	     users \
	     util/genupd

DIRS       = lib \
	     aos \
	     NeXT \
	     src \
	     convers \
	     util \
	     bbs

all:;   @-rm -f $(OBSOLETE)
	-chmod 755 cc
	@-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i all install ); done
	@-$(MAKE) -i /tcp/hostaddr.pag
	@-if [ -d tools ]; then ( cd tools; $(MAKE) -i all install ); fi

/tcp/hosts:
	[ -f /tcp/hosts ] || touch /tcp/hosts

/tcp/domain.txt:
	[ -f /tcp/domain.txt ] || touch /tcp/domain.txt

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt util/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	util/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

distrib:
	@-rm -f wampes-*.t*; \
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
		examples/?*.* \
		lib/*.[ch] \
		lib/Makefile \
		lib/configure \
		src/*.[ch] \
		src/Makefile \
		src/linux_include/*/*.h \
		util/*.[ch] \
		util/Makefile \
		! -name configure.h -print`; \
	tar cf wampes-latest.tar $$sources; \
	version=`awk -F- '/.#.WAMPES-/ {print substr($$2,1,6)}' < src/version.c`; \
	ln wampes-latest.tar wampes-$$version.tar; \
	ln README wampes-$$version.txt; \
	gzip -9 < wampes-latest.tar > wampes-$$version.tar.gz; \
	gpg --detach-sign --force-v3-sigs --armor wampes-$$version.tar.gz

clean:; @-for dir in $(DIRS); do ( cd $$dir; $(MAKE) -i clean ); done
	@-if [ -d tools ]; then  ( cd tools; $(MAKE) -i clean ); fi
	@-rm -f wampes-??????.t*
	@-find . -name .pure -exec rm {} \;
	@-find . -name .trimtime -exec rm {} \;
###
