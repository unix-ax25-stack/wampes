# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/Makefile,v 1.13 1993/08/01 18:07:12 deyke Exp $

PATH       = /opt/SUNWspro/bin:/usr/lang:/bin:/usr/bin:/usr/ccs/bin:/usr/ucb:/usr/contrib/bin:/usr/local/bin:/usr/local/etc
MKDIR      = @if [ ! -d `dirname $@` ] ; then mkdir -p `dirname $@` ; fi

all:;   chmod 755 cc
	-(dir=lib    ; if [ -d $$dir ]; then cd $$dir; make -i all install; fi)
	-(dir=src    ; if [ -d $$dir ]; then cd $$dir; make -i all install; fi)
	-(dir=convers; if [ -d $$dir ]; then cd $$dir; make -i all install; fi)
	-(dir=util   ; if [ -d $$dir ]; then cd $$dir; make -i all install; fi)
	-(dir=bbs    ; if [ -d $$dir ]; then cd $$dir; make -i all install; fi)
	-make -i _all

_all:   /tcp/hostaddr.pag
	if [ -f users ]; then make /usr/local/lib/users; fi

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	rm -f /tcp/hostaddr.* /tcp/hostname.*
	/usr/local/etc/mkhostdb >/dev/null 2>&1
	if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

/usr/local/lib/users: users
	$(MKDIR)
	cp users /usr/local/lib && udbm

clean:;

depend:;
