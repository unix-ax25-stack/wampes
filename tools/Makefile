CC         = c89
CFLAGS     = -Os -D_HPUX_SOURCE
SHELL      = /bin/sh

TARGETS    = \
	     connect \
	     connect2

.IGNORE:

all:    $(TARGETS)

connect: connect.c
	$(CC) $(CFLAGS) connect.c -o $@

connect2: connect2.c
	$(CC) $(CFLAGS) connect2.c -o $@

clean:
	rm -f $(TARGETS) *.o core
