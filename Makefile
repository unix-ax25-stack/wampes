# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/Makefile,v 1.2 1992/08/20 19:37:45 deyke Exp $

PATH  = /bin:/usr/bin:/usr/local/bin:/usr/contrib/bin:/usr/local/etc
SHELL = /bin/sh

.IGNORE:

all:
	@(dir=src     ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@(dir=convers ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@(dir=util    ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@(dir=bbs     ; if [ -d $$dir ]; then cd $$dir; make install; fi)
	@make /usr/local/lib/users
	@rm -f  /tmp/old.*

/usr/local/lib/users: users
	@cp users /usr/local/lib && udbm
