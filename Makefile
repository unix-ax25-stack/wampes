# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/Makefile,v 1.6 1992/11/09 16:31:25 deyke Exp $

PATH  = /bin:/usr/bin:/usr/local/bin:/usr/contrib/bin:/usr/local/etc
SHELL = /bin/sh

.IGNORE:

all:
	@(dir=src     ; if [ -d $$dir ]; then cd $$dir; make; make install; fi)
	@(dir=convers ; if [ -d $$dir ]; then cd $$dir; make; make install; fi)
	@(dir=util    ; if [ -d $$dir ]; then cd $$dir; make; make install; fi)
	@(dir=bbs     ; if [ -d $$dir ]; then cd $$dir; make; make install; fi)
	@make /tcp/hostaddr.pag
	@make /usr/local/lib/users
	@rm -f /tmp/old.* >/dev/null 2>&1 ; exit 0

/tcp/hostaddr.pag: /tcp/hosts /tcp/domain.txt /usr/local/etc/mkhostdb
	/usr/local/etc/mkhostdb >/dev/null 2>&1

/usr/local/lib/users: users
	@cp users /usr/local/lib && udbm
