# @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/src/Makefile,v 1.78 1994/12/11 17:00:37 deyke Exp $

CFLAGS        = -I../lib

DEST          = /tcp

EXTHDRS       = ../lib/bbs.h \
		../lib/buildsaddr.h \
		../lib/calc_crc.h \
		../lib/callvalid.h \
		../lib/configure.h \
		../lib/seteugid.h

HDRS          = arp.h \
		asy.h \
		ax25.h \
		ax25mail.h \
		axclient.h \
		bootp.h \
		cmdparse.h \
		commands.h \
		config.h \
		crc.h \
		daemon.h \
		devparam.h \
		dirutil.h \
		domain.h \
		enet.h \
		files.h \
		finger.h \
		flexnet.h \
		ftp.h \
		ftpserv.h \
		global.h \
		hardware.h \
		hpux.h \
		icmp.h \
		iface.h \
		internet.h \
		ip.h \
		ipfilter.h \
		kiss.h \
		lapb.h \
		login.h \
		mail.h \
		main.h \
		mbuf.h \
		n8250.h \
		netrom.h \
		netuser.h \
		nrs.h \
		pktdrvr.h \
		proc.h \
		remote.h \
		remote_net.h \
		rip.h \
		session.h \
		slhc.h \
		slip.h \
		socket.h \
		tcp.h \
		telnet.h \
		timer.h \
		tipmail.h \
		trace.h \
		transport.h \
		tty.h \
		udp.h

LD            = $(CC)

LDFLAGS       = $(CFLAGS) -s

LIBS          = ../lib/libutil.a

MAKEFILE      = Makefile

OBJS          = adjtime.o \
		alloc.o \
		arp.o \
		arpcmd.o \
		arpdump.o \
		arpfile.o \
		arphdr.o \
		asy.o \
		ax25.o \
		ax25cmd.o \
		ax25dump.o \
		ax25file.o \
		ax25hdr.o \
		ax25subr.o \
		ax25user.o \
		axclient.o \
		axheard.o \
		axip.o \
		axserver.o \
		cmdparse.o \
		config.o \
		crc.o \
		devparam.o \
		dirutil.o \
		domain.o \
		domhdr.o \
		files.o \
		finger.o \
		flexnet.o \
		ftp.o \
		ftpcli.o \
		ftpserv.o \
		hpux.o \
		icmp.o \
		icmpcmd.o \
		icmpdump.o \
		icmphdr.o \
		icmpmsg.o \
		iface.o \
		ip.o \
		ipcmd.o \
		ipdump.o \
		ipfile.o \
		ipfilter.o \
		iphdr.o \
		ipip.o \
		iproute.o \
		kernel.o \
		kiss.o \
		kissdump.o \
		ksubr.o \
		lapb.o \
		lapbtime.o \
		lcsum.o \
		login.o \
		mail_bbs.o \
		mail_daemn.o \
		mail_retrn.o \
		mail_smtp.o \
		mail_subr.o \
		main.o \
		mbuf.o \
		misc.o \
		n8250.o \
		netrom.o \
		netuser.o \
		ni.o \
		nrdump.o \
		nrs.o \
		pathname.o \
		remote_net.o \
		rip.o \
		ripcmd.o \
		ripdump.o \
		services.o \
		session.o \
		setsp.o \
		slhc.o \
		slhcdump.o \
		slip.o \
		smisc.o \
		sntp.o \
		tcpcmd.o \
		tcpdump.o \
		tcpgate.o \
		tcphdr.o \
		tcpin.o \
		tcpout.o \
		tcpsubr.o \
		tcptimer.o \
		tcpuser.o \
		telnet.o \
		timep.o \
		timer.o \
		tnserv.o \
		trace.o \
		transport.o \
		ttydriv.o \
		udp.o \
		udpcmd.o \
		udpdump.o \
		udphdr.o \
		version.o

PRINT         = pr

PROGRAM       = net

SLIBS         =

SRCS          = adjtime.c \
		alloc.c \
		arp.c \
		arpcmd.c \
		arpdump.c \
		arpfile.c \
		arphdr.c \
		asy.c \
		ax25.c \
		ax25cmd.c \
		ax25dump.c \
		ax25file.c \
		ax25hdr.c \
		ax25subr.c \
		ax25user.c \
		axclient.c \
		axheard.c \
		axip.c \
		axserver.c \
		cmdparse.c \
		config.c \
		crc.c \
		devparam.c \
		dirutil.c \
		domain.c \
		domhdr.c \
		files.c \
		finger.c \
		flexnet.c \
		ftp.c \
		ftpcli.c \
		ftpserv.c \
		hpux.c \
		icmp.c \
		icmpcmd.c \
		icmpdump.c \
		icmphdr.c \
		icmpmsg.c \
		iface.c \
		ip.c \
		ipcmd.c \
		ipdump.c \
		ipfile.c \
		ipfilter.c \
		iphdr.c \
		ipip.c \
		iproute.c \
		kernel.c \
		kiss.c \
		kissdump.c \
		ksubr.c \
		lapb.c \
		lapbtime.c \
		lcsum.c \
		login.c \
		mail_bbs.c \
		mail_daemn.c \
		mail_retrn.c \
		mail_smtp.c \
		mail_subr.c \
		main.c \
		mbuf.c \
		misc.c \
		n8250.c \
		netrom.c \
		netuser.c \
		ni.c \
		nrdump.c \
		nrs.c \
		pathname.c \
		remote_net.c \
		rip.c \
		ripcmd.c \
		ripdump.c \
		services.c \
		session.c \
		setsp.c \
		slhc.c \
		slhcdump.c \
		slip.c \
		smisc.c \
		sntp.c \
		tcpcmd.c \
		tcpdump.c \
		tcpgate.c \
		tcphdr.c \
		tcpin.c \
		tcpout.c \
		tcpsubr.c \
		tcptimer.c \
		tcpuser.c \
		telnet.c \
		timep.c \
		timer.c \
		tnserv.c \
		trace.c \
		transport.c \
		ttydriv.c \
		udp.c \
		udpcmd.c \
		udpdump.c \
		udphdr.c \
		version.c

# SYSHDRS       =

RMTARGET      = @if [ -f $@ ] ; then rm -f $@ || mv -f $@ $@~ ; fi

all:;   chmod 755 cc
	@case "`uname -sr`" in \
	HP-UX*) \
		make -i -f $(MAKEFILE) \
		'CC=c89' \
		'CFLAGS=-O -DUNIX -D_HPUX_SOURCE -DFD_SETSIZE=1024 -I../lib' \
		'SLIBS=-lndbm' \
		_all;; \
	AIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -qchars=signed -DUNIX -D_ALL_SOURCE -DFD_SETSIZE=1024 -I../lib' \
		_all;; \
	IRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -signed -DUNIX -DFD_SETSIZE=1024 -I../lib' \
		_all;; \
	Linux*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DUNIX -I./linux_include -I../lib' \
		'SLIBS=-ldbm' \
		_all;; \
	386BSD*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -DUNIX -DFD_SETSIZE=1024 -I../lib' \
		_all;; \
	BSD/386*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -DUNIX -DFD_SETSIZE=1024 -I../lib' \
		_all;; \
	SunOS\ 4.*) \
		make -i -f $(MAKEFILE) \
		'CC=acc' \
		'CFLAGS=-O -DUNIX -DFD_SETSIZE=1024 -I../lib' \
		_all;; \
	SunOS\ 5.*) \
		make -i -f $(MAKEFILE) \
		'CC=cc' \
		'CFLAGS=-O -DUNIX -DFD_SETSIZE=1024 -I../lib' \
		'SLIBS=-lsocket -lnsl' \
		_all;; \
	ULTRIX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc' \
		'CFLAGS=-O6 -DUNIX -DULTRIX_RISC -DFD_SETSIZE=64 -I../lib' \
		'SLIBS=-ldbm' \
		_all;; \
	A/UX*) \
		make -i -f $(MAKEFILE) \
		'CC=gcc -fpcc-struct-return' \
		'CFLAGS=-O -DUNIX -D_POSIX_SOURCE -I../lib' \
		'SLIBS=-ldbm -lposix' \
		_all;; \
	RISC\ iX*) \
		make -i -f $(MAKEFILE) \
		'CC=/tcp/src/cc' \
		'CFLAGS=-DUNIX -DRISCiX -I../lib' \
		_all;; \
	AOS*) \
		/usr/local/bin/make -i -f $(MAKEFILE) \
		'CC=/usr/local/bin/gcc' \
		'CFLAGS=-O2 -DUNIX -Dibm6153 -I../lib' \
		'LIBS=../aos/libaos.a ../lib/libutil.a' \
		'LDFLAGS=-g' \
		_all;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac

_all:           $(PROGRAM)

setsp.o: setsp.c
	@case "`uname -sr`" in \
	HP-UX*) \
		$(CC) -E $< > setsp.s; as setsp.s -o $@;; \
	AIX*) \
		$(CC) $(CFLAGS) -c setsp.c;; \
	IRIX*) \
		$(CC) $(CFLAGS) -c setsp.c;; \
	Linux*) \
		$(CC) -E $< > setsp.s; cc -c setsp.s;; \
	386BSD*) \
		$(CC) -E $< > setsp.s; cc -c setsp.s;; \
	BSD/386*) \
		cpp $< > setsp.s; cc -c setsp.s;; \
	SunOS\ 4.*) \
		$(CC) $(CFLAGS) -c setsp.c;; \
	SunOS\ 5.*) \
		$(CC) $(CFLAGS) -c setsp.c;; \
	ULTRIX*) \
		/bin/cc -c setsp.c;; \
	A/UX*) \
		$(CC) $(CFLAGS) -c setsp.c;; \
	RISC\ iX*) \
		$(CC) -E -DRISCiX $< > setsp.s; cc -c setsp.s;; \
	AOS*) \
		$(CC) $(CFLAGS) -c setsp.c;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac
	@rm -f setsp.s

$(PROGRAM):     $(OBJS) $(LIBS)
		@echo "Linking $(PROGRAM) ..."
		$(RMTARGET)
		@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) $(SLIBS) -o $(PROGRAM)

clean:;         @rm -f *.o *~

clobber:        clean; @rm -f $(PROGRAM) lint tags

depend:;        @mkmf -f $(MAKEFILE)

echo:;          @echo $(HDRS) $(SRCS)

index:;         @ctags -wx $(HDRS) $(SRCS)

install:        $(DEST)/$(PROGRAM)

lint:           $(HDRS) $(SRCS); @lih $(SRCS) > lint

print:;         @$(PRINT) $(HDRS) $(SRCS)

tags:           $(HDRS) $(SRCS); @ctags $(HDRS) $(SRCS)

update:         $(DEST)/$(PROGRAM)

$(DEST)/$(PROGRAM): $(PROGRAM)
		$(RMTARGET)
		cp $(PROGRAM) $@

.c.o:
		@rm -f $@
		$(CC) $(CFLAGS) -c $<

###
adjtime.o: ../lib/configure.h
alloc.o: global.h mbuf.h cmdparse.h
arp.o: global.h mbuf.h timer.h iface.h proc.h enet.h ax25.h icmp.h internet.h \
	ip.h session.h hardware.h telnet.h tcp.h netuser.h arp.h
arpcmd.o: global.h mbuf.h timer.h enet.h iface.h proc.h ax25.h arp.h \
	netuser.h cmdparse.h commands.h
arpdump.o: global.h mbuf.h arp.h iface.h proc.h timer.h netuser.h trace.h
arpfile.o: global.h timer.h arp.h mbuf.h iface.h proc.h main.h
arphdr.o: global.h mbuf.h arp.h iface.h proc.h timer.h
asy.o: global.h proc.h mbuf.h timer.h iface.h netuser.h slhc.h ip.h \
	internet.h tcp.h n8250.h asy.h ax25.h kiss.h nrs.h pktdrvr.h slip.h \
	commands.h
ax25.o: global.h mbuf.h iface.h proc.h timer.h arp.h slip.h slhc.h ip.h \
	internet.h tcp.h netuser.h ax25.h lapb.h netrom.h session.h \
	hardware.h telnet.h devparam.h
ax25cmd.o: global.h mbuf.h timer.h proc.h iface.h ax25.h lapb.h cmdparse.h \
	socket.h session.h hardware.h telnet.h tcp.h internet.h ip.h \
	netuser.h tty.h commands.h
ax25dump.o: global.h mbuf.h ax25.h iface.h proc.h timer.h lapb.h trace.h \
	socket.h
ax25file.o: global.h timer.h iface.h mbuf.h proc.h ax25.h main.h
ax25hdr.o: global.h mbuf.h ax25.h iface.h proc.h timer.h
ax25subr.o: global.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h
ax25user.o: global.h mbuf.h timer.h iface.h proc.h lapb.h ax25.h
axclient.o: global.h config.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h \
	session.h hardware.h telnet.h tcp.h internet.h ip.h netuser.h
axheard.o: global.h mbuf.h iface.h proc.h timer.h ax25.h ip.h internet.h
axip.o: global.h mbuf.h iface.h proc.h timer.h internet.h netuser.h ax25.h \
	socket.h trace.h pktdrvr.h cmdparse.h hpux.h crc.h
axserver.o: global.h mbuf.h ax25.h iface.h proc.h timer.h lapb.h login.h
cmdparse.o: global.h proc.h mbuf.h timer.h cmdparse.h
config.o: global.h config.h mbuf.h timer.h proc.h iface.h ip.h internet.h \
	tcp.h netuser.h udp.h lapb.h ax25.h enet.h kiss.h nrs.h netrom.h \
	session.h hardware.h telnet.h pktdrvr.h slip.h slhc.h arp.h icmp.h \
	cmdparse.h commands.h ax25mail.h tipmail.h daemon.h bootp.h socket.h \
	asy.h trace.h flexnet.h
crc.o: global.h mbuf.h crc.h ../lib/calc_crc.h
devparam.o: global.h devparam.h
dirutil.o: global.h hpux.h dirutil.h commands.h ../lib/configure.h
domain.o: global.h mbuf.h iface.h proc.h timer.h socket.h tcp.h internet.h \
	ip.h netuser.h udp.h cmdparse.h domain.h
domhdr.o: global.h mbuf.h domain.h proc.h timer.h
files.o: global.h netuser.h files.h
finger.o: config.h global.h mbuf.h timer.h internet.h icmp.h iface.h proc.h \
	ip.h session.h hardware.h telnet.h tcp.h netuser.h ax25.h ftp.h \
	finger.h
flexnet.o: global.h mbuf.h timer.h iface.h proc.h ax25.h lapb.h trace.h \
	flexnet.h cmdparse.h commands.h
ftp.o: global.h mbuf.h netuser.h timer.h tcp.h iface.h proc.h internet.h ip.h \
	ftp.h session.h hardware.h telnet.h ax25.h
ftpcli.o: global.h mbuf.h session.h proc.h timer.h hardware.h telnet.h tcp.h \
	iface.h internet.h ip.h netuser.h ax25.h cmdparse.h socket.h icmp.h \
	ftp.h commands.h dirutil.h
ftpserv.o: ../lib/seteugid.h global.h mbuf.h socket.h proc.h timer.h \
	netuser.h tcp.h iface.h internet.h ip.h dirutil.h login.h ftp.h \
	ftpserv.h
hpux.o: ../lib/configure.h global.h iface.h mbuf.h proc.h timer.h files.h \
	hardware.h login.h commands.h main.h hpux.h
icmp.o: global.h mbuf.h iface.h proc.h timer.h ip.h internet.h icmp.h \
	session.h hardware.h telnet.h tcp.h netuser.h ax25.h
icmpcmd.o: global.h icmp.h mbuf.h iface.h proc.h timer.h internet.h ip.h \
	session.h hardware.h telnet.h tcp.h netuser.h ax25.h socket.h \
	cmdparse.h commands.h
icmpdump.o: global.h mbuf.h internet.h netuser.h icmp.h iface.h proc.h \
	timer.h ip.h session.h hardware.h telnet.h tcp.h ax25.h trace.h
icmphdr.o: global.h mbuf.h internet.h ip.h iface.h proc.h timer.h icmp.h \
	session.h hardware.h telnet.h tcp.h netuser.h ax25.h
icmpmsg.o: global.h
iface.o: global.h mbuf.h proc.h timer.h iface.h ip.h internet.h icmp.h \
	session.h hardware.h telnet.h tcp.h netuser.h ax25.h enet.h pktdrvr.h \
	cmdparse.h commands.h trace.h
ip.o: global.h mbuf.h timer.h internet.h netuser.h iface.h proc.h pktdrvr.h \
	ip.h icmp.h session.h hardware.h telnet.h tcp.h ax25.h
ipcmd.o: global.h mbuf.h internet.h timer.h netuser.h iface.h proc.h ip.h \
	cmdparse.h commands.h rip.h udp.h
ipdump.o: global.h mbuf.h internet.h iface.h proc.h timer.h ip.h trace.h \
	netuser.h
ipfile.o: global.h timer.h iface.h mbuf.h proc.h ip.h internet.h main.h
ipfilter.o: global.h netuser.h cmdparse.h
iphdr.o: global.h mbuf.h ip.h iface.h proc.h timer.h internet.h
ipip.o: global.h mbuf.h iface.h proc.h timer.h internet.h netuser.h socket.h \
	trace.h pktdrvr.h cmdparse.h hpux.h
iproute.o: global.h mbuf.h iface.h proc.h timer.h internet.h ip.h tcp.h \
	netuser.h icmp.h session.h hardware.h telnet.h ax25.h rip.h udp.h \
	trace.h pktdrvr.h bootp.h socket.h ipfilter.h
kernel.o: global.h mbuf.h proc.h timer.h socket.h daemon.h hardware.h
kiss.o: global.h mbuf.h iface.h proc.h timer.h kiss.h devparam.h slip.h \
	slhc.h ip.h internet.h tcp.h netuser.h asy.h ax25.h pktdrvr.h crc.h
kissdump.o: global.h mbuf.h kiss.h iface.h proc.h timer.h devparam.h ax25.h \
	trace.h crc.h
ksubr.o: global.h proc.h mbuf.h timer.h commands.h
lapb.o: global.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h ip.h internet.h
lapbtime.o: global.h mbuf.h ax25.h iface.h proc.h timer.h lapb.h
lcsum.o: global.h ip.h mbuf.h iface.h proc.h timer.h internet.h
login.o: ../lib/callvalid.h ../lib/configure.h global.h mbuf.h timer.h hpux.h \
	telnet.h tcp.h iface.h proc.h internet.h ip.h netuser.h cmdparse.h \
	commands.h login.h
mail_bbs.o: ../lib/bbs.h ../lib/configure.h global.h mbuf.h timer.h \
	transport.h mail.h
mail_daemn.o: ../lib/configure.h global.h timer.h mail.h cmdparse.h \
	commands.h
mail_retrn.o: ../lib/configure.h hpux.h global.h mail.h
mail_smtp.o: global.h mbuf.h transport.h timer.h mail.h
mail_subr.o: timer.h global.h mail.h
main.o: global.h mbuf.h timer.h proc.h iface.h arp.h ip.h internet.h tcp.h \
	netuser.h udp.h ax25.h kiss.h enet.h netrom.h session.h hardware.h \
	telnet.h ftp.h tty.h socket.h cmdparse.h commands.h daemon.h \
	devparam.h files.h main.h remote.h trace.h hpux.h remote_net.h
mbuf.o: global.h mbuf.h proc.h timer.h
misc.o: global.h socket.h mbuf.h proc.h timer.h
n8250.o: global.h mbuf.h proc.h timer.h iface.h n8250.h asy.h devparam.h \
	hpux.h
netrom.o: global.h config.h netuser.h mbuf.h timer.h iface.h proc.h arp.h \
	ip.h internet.h ax25.h lapb.h netrom.h session.h hardware.h telnet.h \
	tcp.h cmdparse.h trace.h login.h
netuser.o: global.h netuser.h
ni.o: global.h mbuf.h iface.h proc.h timer.h netuser.h trace.h hpux.h
nrdump.o: global.h mbuf.h timer.h ax25.h iface.h proc.h netrom.h session.h \
	hardware.h telnet.h tcp.h internet.h ip.h netuser.h trace.h
nrs.o: global.h mbuf.h iface.h proc.h timer.h pktdrvr.h ax25.h nrs.h asy.h \
	trace.h commands.h
pathname.o: global.h dirutil.h
remote_net.o: global.h mbuf.h iface.h proc.h timer.h transport.h hpux.h \
	../lib/buildsaddr.h main.h
rip.o: global.h mbuf.h netuser.h udp.h iface.h proc.h timer.h internet.h ip.h \
	rip.h arp.h
ripcmd.o: global.h mbuf.h netuser.h internet.h cmdparse.h timer.h iface.h \
	proc.h udp.h ip.h rip.h commands.h
ripdump.o: global.h mbuf.h netuser.h timer.h rip.h iface.h proc.h udp.h \
	internet.h ip.h trace.h
services.o: global.h socket.h mbuf.h proc.h timer.h netuser.h
session.o: global.h config.h mbuf.h proc.h timer.h tcp.h iface.h internet.h \
	ip.h netuser.h ftp.h icmp.h session.h hardware.h telnet.h ax25.h \
	tty.h socket.h cmdparse.h commands.h main.h axclient.h lapb.h \
	finger.h netrom.h
slhc.o: global.h mbuf.h internet.h ip.h iface.h proc.h timer.h tcp.h \
	netuser.h slhc.h
slhcdump.o: global.h mbuf.h internet.h ip.h iface.h proc.h timer.h slhc.h \
	tcp.h netuser.h trace.h
slip.o: global.h mbuf.h iface.h proc.h timer.h ip.h internet.h slhc.h tcp.h \
	netuser.h asy.h slip.h trace.h pktdrvr.h
smisc.o: global.h mbuf.h socket.h proc.h timer.h remote.h tcp.h iface.h \
	internet.h ip.h netuser.h udp.h commands.h hardware.h main.h
sntp.o: global.h mbuf.h iface.h proc.h timer.h socket.h udp.h internet.h ip.h \
	netuser.h cmdparse.h session.h hardware.h telnet.h tcp.h ax25.h \
	../lib/configure.h
tcpcmd.o: global.h timer.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h \
	ip.h cmdparse.h commands.h socket.h session.h hardware.h telnet.h \
	ax25.h
tcpdump.o: global.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h timer.h \
	ip.h trace.h
tcpgate.o: global.h mbuf.h netuser.h tcp.h iface.h proc.h timer.h internet.h \
	ip.h hpux.h ../lib/buildsaddr.h
tcphdr.o: global.h mbuf.h tcp.h iface.h proc.h timer.h internet.h ip.h \
	netuser.h
tcpin.o: global.h timer.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h \
	ip.h icmp.h session.h hardware.h telnet.h ax25.h
tcpout.o: global.h timer.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h \
	ip.h
tcpsubr.o: global.h timer.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h \
	ip.h
tcptimer.o: global.h mbuf.h timer.h netuser.h internet.h tcp.h iface.h proc.h \
	ip.h
tcpuser.o: global.h timer.h mbuf.h netuser.h socket.h proc.h internet.h tcp.h \
	iface.h ip.h icmp.h session.h hardware.h telnet.h ax25.h
telnet.o: global.h mbuf.h socket.h proc.h timer.h telnet.h tcp.h iface.h \
	internet.h ip.h netuser.h session.h hardware.h ax25.h icmp.h tty.h \
	commands.h
timep.o: global.h mbuf.h iface.h proc.h timer.h socket.h udp.h internet.h \
	ip.h netuser.h
timer.o: global.h timer.h proc.h mbuf.h commands.h daemon.h hardware.h \
	socket.h
tnserv.o: global.h mbuf.h socket.h proc.h timer.h netuser.h tcp.h iface.h \
	internet.h ip.h login.h tipmail.h
trace.o: global.h mbuf.h iface.h proc.h timer.h pktdrvr.h commands.h trace.h \
	session.h hardware.h telnet.h tcp.h internet.h ip.h netuser.h ax25.h
transport.o: global.h netuser.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h \
	netrom.h session.h hardware.h telnet.h tcp.h internet.h ip.h \
	transport.h
ttydriv.o: global.h tty.h
udp.o: global.h mbuf.h netuser.h iface.h proc.h timer.h udp.h internet.h ip.h \
	icmp.h session.h hardware.h telnet.h tcp.h ax25.h
udpcmd.o: global.h mbuf.h netuser.h udp.h iface.h proc.h timer.h internet.h \
	ip.h cmdparse.h commands.h
udpdump.o: global.h mbuf.h netuser.h internet.h udp.h iface.h proc.h timer.h \
	ip.h socket.h trace.h
udphdr.o: global.h mbuf.h ip.h iface.h proc.h timer.h internet.h udp.h \
	netuser.h
