BUILDSADDR = ../src/buildsaddr.o
CFLAGS     = -Aa -O -s
SHELL      = /bin/sh

.IGNORE:

all:    bbs findpath killdup sfstat

bbs:    bbs.c bbs.h $(BUILDSADDR)
	$(CC) $(CFLAGS) bbs.c $(BUILDSADDR) -o $@

findpath: findpath.c bbs.h
	$(CC) $(CFLAGS) findpath.c -o $@

killdup: killdup.c bbs.h
	$(CC) $(CFLAGS) killdup.c -o $@

sfstat: sfstat.c bbs.h
	$(CC) $(CFLAGS) sfstat.c -o $@

install: /usr/local/bin/bbs findpath killdup sfstat

/usr/local/bin/bbs: bbs
	cp bbs /usr/local/bin/bbs.tmp
	cd /usr/local/bin; chmod 4555 bbs.tmp; mv -f bbs bbs.old.$$$$; mv -f bbs.tmp bbs; rm -f BBS; ln -f bbs BBS; rm -f bbs.old.*

clean:
	rm -f *.o bbs findpath killdup sfstat

