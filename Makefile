# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/Makefile,v 1.7 1993/03/30 17:23:16 deyke Exp $

PATH  = /bin:/usr/bin:/usr/lang:/usr/local/bin:/usr/contrib/bin:/usr/local/etc
SHELL = /bin/sh

all:
	-(dir=src     ; if [ -d $$dir ]; then cd $$dir; make -i; make -i install; fi)
	-(dir=convers ; if [ -d $$dir ]; then cd $$dir; make -i; make -i install; fi)
	-(dir=util    ; if [ -d $$dir ]; then cd $$dir; make -i; make -i install; fi)
	-(dir=bbs     ; if [ -d $$dir ]; then cd $$dir; make -i; make -i install; fi)
	-make -i /tcp/hostaddr.pag
	-make -i /usr/local/lib/users
	-rm -f /tmp/old.*

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	-rm -f /tcp/hostaddr.* /tcp/hostname.*
	-/usr/local/etc/mkhostdb >/dev/null 2>&1
	-if [ -f /tcp/hostaddr.db ]; then ln /tcp/hostaddr.db $@; fi

/usr/local/lib/users: users
	-cp users /usr/local/lib && udbm
