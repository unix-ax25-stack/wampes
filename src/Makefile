# @(#) $Id: Makefile,v 1.99 1999/01/24 22:23:24 deyke Exp $

DEST          = /tcp

EXTHDRS       = ../lib/buildsaddr.h \
		../lib/calc_crc.h \
		../lib/callvalid.h \
		../lib/configure.h \
		../lib/seek.h \
		../lib/seteugid.h \
		../lib/strdup.h \
		../lib/strerror.h \
		../lib/strtoul.h

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
		ping.h \
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
		krnlif.o \
		ksubr.o \
		lapb.o \
		lapbtime.o \
		lcsum.o \
		login.o \
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
		ping.o \
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
		tun.o \
		udp.o \
		udpcmd.o \
		udpdump.o \
		udphdr.o \
		version.o

PROGRAM       = net

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
		krnlif.c \
		ksubr.c \
		lapb.c \
		lapbtime.c \
		lcsum.c \
		login.c \
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
		ping.c \
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
		tun.c \
		udp.c \
		udpcmd.c \
		udpdump.c \
		udphdr.c \
		version.c

all:;        @. ../lib/configure.mak; $(MAKE) -i _all
install:;    @. ../lib/configure.mak; $(MAKE) -i _install

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
	FreeBSD*) \
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
	NeXT*) \
		gcc -E $< | grep -v '^#' > setsp.s; as setsp.s -o $@;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac
	@rm -f setsp.s

$(PROGRAM):     $(OBJS) $(LIBS)
		@echo "Linking $(PROGRAM) ..."
		$(RMTARGET)
		@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) $(SLIBS) -o $(PROGRAM)

clean:;         @rm -f *.o $(PROGRAM) lint tags TAGS *~ \#*\#

_install:       $(DEST)/$(PROGRAM)

lint:           $(HDRS) $(SRCS); @lih $(SRCS) > lint

tags:           $(HDRS) $(SRCS)
		@ctags -w $(HDRS) $(SRCS)

TAGS:           $(HDRS) $(SRCS)
		@etags $(HDRS) $(SRCS)

$(DEST)/$(PROGRAM): $(PROGRAM)
		$(MKDIR)
		$(RMTARGET)
		cp $(PROGRAM) $@

.c.o:
		@rm -f $@
		$(CC) $(CFLAGS) -c $<

###
adjtime.o: ../lib/configure.h
alloc.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h cmdparse.h
arp.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h iface.h \
	proc.h enet.h ax25.h icmp.h internet.h ip.h arp.h
arpcmd.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h enet.h \
	iface.h proc.h ax25.h arp.h netuser.h cmdparse.h commands.h
arpdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h arp.h iface.h \
	proc.h timer.h netuser.h trace.h
arpfile.o: global.h ../lib/configure.h ../lib/strdup.h timer.h arp.h mbuf.h \
	iface.h proc.h main.h
arphdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h arp.h iface.h \
	proc.h timer.h
asy.o: global.h ../lib/configure.h ../lib/strdup.h proc.h mbuf.h timer.h \
	iface.h netuser.h slhc.h ip.h internet.h tcp.h n8250.h asy.h ax25.h \
	kiss.h nrs.h slip.h commands.h
ax25.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h arp.h slip.h slhc.h ip.h internet.h tcp.h netuser.h ax25.h \
	lapb.h netrom.h session.h hardware.h telnet.h devparam.h
ax25cmd.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h proc.h \
	iface.h ax25.h lapb.h cmdparse.h socket.h session.h hardware.h \
	telnet.h tcp.h internet.h ip.h netuser.h tty.h commands.h
ax25dump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h ax25.h iface.h \
	proc.h timer.h lapb.h trace.h socket.h
ax25file.o: global.h ../lib/configure.h ../lib/strdup.h timer.h iface.h \
	mbuf.h proc.h ax25.h main.h
ax25hdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h ax25.h iface.h \
	proc.h timer.h
ax25subr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h ax25.h \
	iface.h proc.h lapb.h
ax25user.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h \
	iface.h proc.h lapb.h ax25.h
axclient.o: global.h ../lib/configure.h ../lib/strdup.h config.h mbuf.h \
	timer.h ax25.h iface.h proc.h lapb.h session.h hardware.h telnet.h \
	tcp.h internet.h ip.h netuser.h
axheard.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h ax25.h ip.h internet.h
axip.o: global.h ../lib/configure.h ../lib/strdup.h ../lib/strerror.h mbuf.h \
	iface.h proc.h timer.h internet.h netuser.h ax25.h socket.h trace.h \
	cmdparse.h hpux.h crc.h
axserver.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h ax25.h iface.h \
	proc.h timer.h lapb.h login.h
cmdparse.o: ../lib/strtoul.h ../lib/configure.h global.h ../lib/strdup.h \
	proc.h mbuf.h timer.h cmdparse.h
config.o: ../lib/configure.h global.h ../lib/strdup.h config.h mbuf.h timer.h \
	proc.h iface.h ip.h internet.h tcp.h netuser.h udp.h lapb.h ax25.h \
	enet.h kiss.h nrs.h netrom.h session.h hardware.h telnet.h pktdrvr.h \
	slip.h slhc.h arp.h icmp.h cmdparse.h commands.h ax25mail.h tipmail.h \
	daemon.h bootp.h socket.h asy.h trace.h flexnet.h
crc.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h crc.h \
	../lib/calc_crc.h
devparam.o: global.h ../lib/configure.h ../lib/strdup.h devparam.h
dirutil.o: global.h ../lib/configure.h ../lib/strdup.h hpux.h dirutil.h \
	commands.h
domain.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h socket.h tcp.h internet.h ip.h netuser.h udp.h cmdparse.h \
	domain.h
domhdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h domain.h proc.h \
	timer.h
files.o: global.h ../lib/configure.h ../lib/strdup.h netuser.h files.h
finger.o: config.h global.h ../lib/configure.h ../lib/strdup.h mbuf.h \
	socket.h proc.h timer.h internet.h icmp.h iface.h ip.h netuser.h \
	tcp.h ftp.h session.h hardware.h telnet.h ax25.h finger.h
flexnet.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h iface.h \
	proc.h ax25.h lapb.h trace.h flexnet.h cmdparse.h commands.h
ftp.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h timer.h \
	tcp.h iface.h proc.h internet.h ip.h ftp.h session.h hardware.h \
	telnet.h ax25.h
ftpcli.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h session.h proc.h \
	timer.h hardware.h telnet.h tcp.h iface.h internet.h ip.h netuser.h \
	ax25.h cmdparse.h socket.h icmp.h ftp.h commands.h dirutil.h \
	../lib/seek.h
ftpserv.o: ../lib/seek.h ../lib/seteugid.h ../lib/strerror.h \
	../lib/configure.h global.h ../lib/strdup.h mbuf.h socket.h proc.h \
	timer.h netuser.h tcp.h iface.h internet.h ip.h dirutil.h login.h \
	ftp.h session.h hardware.h telnet.h ax25.h ftpserv.h
hpux.o: ../lib/configure.h global.h ../lib/strdup.h iface.h mbuf.h proc.h \
	timer.h files.h hardware.h login.h commands.h main.h hpux.h
icmp.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h ip.h internet.h icmp.h ping.h netuser.h
icmpcmd.o: global.h ../lib/configure.h ../lib/strdup.h icmp.h mbuf.h iface.h \
	proc.h timer.h internet.h ip.h netuser.h socket.h cmdparse.h \
	commands.h
icmpdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h internet.h \
	netuser.h icmp.h iface.h proc.h timer.h ip.h trace.h
icmphdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h internet.h ip.h \
	iface.h proc.h timer.h icmp.h
icmpmsg.o: global.h ../lib/configure.h ../lib/strdup.h
iface.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h proc.h timer.h \
	iface.h ip.h internet.h icmp.h netuser.h ax25.h enet.h cmdparse.h \
	commands.h trace.h
ip.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h internet.h \
	netuser.h iface.h proc.h ip.h icmp.h
ipcmd.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h internet.h \
	timer.h netuser.h iface.h proc.h ip.h cmdparse.h commands.h rip.h \
	udp.h
ipdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h internet.h \
	iface.h proc.h timer.h ip.h trace.h netuser.h
ipfile.o: global.h ../lib/configure.h ../lib/strdup.h timer.h iface.h mbuf.h \
	proc.h ip.h internet.h main.h
ipfilter.o: global.h ../lib/configure.h ../lib/strdup.h netuser.h cmdparse.h
iphdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h ip.h iface.h \
	proc.h timer.h internet.h
ipip.o: global.h ../lib/configure.h ../lib/strdup.h ../lib/strerror.h mbuf.h \
	iface.h proc.h timer.h internet.h netuser.h socket.h trace.h \
	cmdparse.h hpux.h
iproute.o: config.h global.h ../lib/configure.h ../lib/strdup.h mbuf.h \
	iface.h proc.h timer.h internet.h ip.h tcp.h netuser.h icmp.h rip.h \
	udp.h trace.h bootp.h socket.h ipfilter.h
kernel.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h proc.h timer.h \
	socket.h daemon.h hardware.h
kiss.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h kiss.h devparam.h slip.h slhc.h ip.h internet.h tcp.h \
	netuser.h asy.h ax25.h pktdrvr.h crc.h
kissdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h kiss.h iface.h \
	proc.h timer.h devparam.h ax25.h trace.h crc.h
krnlif.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h netuser.h commands.h devparam.h trace.h hpux.h config.h \
	ax25.h
ksubr.o: global.h ../lib/configure.h ../lib/strdup.h proc.h mbuf.h timer.h \
	commands.h main.h
lapb.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h ax25.h \
	iface.h proc.h lapb.h ip.h internet.h
lapbtime.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h ax25.h iface.h \
	proc.h timer.h lapb.h
lcsum.o: global.h ../lib/configure.h ../lib/strdup.h ip.h mbuf.h iface.h \
	proc.h timer.h internet.h
login.o: ../lib/callvalid.h ../lib/configure.h ../lib/seek.h global.h \
	../lib/strdup.h mbuf.h timer.h hpux.h telnet.h tcp.h iface.h proc.h \
	internet.h ip.h netuser.h cmdparse.h commands.h login.h
mail_daemn.o: ../lib/configure.h global.h ../lib/strdup.h timer.h mail.h \
	cmdparse.h commands.h
mail_retrn.o: ../lib/configure.h hpux.h global.h ../lib/strdup.h mail.h
mail_smtp.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h transport.h \
	timer.h mail.h
mail_subr.o: timer.h global.h ../lib/configure.h ../lib/strdup.h mail.h
main.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h proc.h \
	iface.h arp.h ip.h internet.h tcp.h netuser.h udp.h ax25.h kiss.h \
	enet.h netrom.h session.h hardware.h telnet.h ftp.h tty.h socket.h \
	cmdparse.h commands.h daemon.h devparam.h files.h main.h remote.h \
	trace.h hpux.h remote_net.h
mbuf.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h proc.h timer.h
misc.o: global.h ../lib/configure.h ../lib/strdup.h socket.h mbuf.h proc.h \
	timer.h
n8250.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h proc.h timer.h \
	iface.h n8250.h asy.h devparam.h hpux.h
netrom.o: global.h ../lib/configure.h ../lib/strdup.h config.h netuser.h \
	mbuf.h timer.h iface.h proc.h arp.h ip.h internet.h ax25.h lapb.h \
	netrom.h session.h hardware.h telnet.h tcp.h cmdparse.h trace.h \
	login.h
netuser.o: global.h ../lib/configure.h ../lib/strdup.h netuser.h
ni.o: ../lib/configure.h global.h ../lib/strdup.h ../lib/strerror.h mbuf.h \
	iface.h proc.h timer.h netuser.h trace.h hpux.h
nrdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h ax25.h \
	iface.h proc.h netrom.h session.h hardware.h telnet.h tcp.h \
	internet.h ip.h netuser.h trace.h
nrs.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h ax25.h nrs.h asy.h trace.h commands.h
pathname.o: global.h ../lib/configure.h ../lib/strdup.h dirutil.h
ping.o: global.h ../lib/configure.h ../lib/strdup.h icmp.h mbuf.h iface.h \
	proc.h timer.h internet.h ip.h netuser.h socket.h session.h \
	hardware.h telnet.h tcp.h ax25.h commands.h ping.h
remote_net.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h \
	proc.h timer.h transport.h hpux.h ../lib/buildsaddr.h main.h \
	cmdparse.h
rip.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h udp.h \
	iface.h proc.h timer.h internet.h ip.h rip.h arp.h
ripcmd.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h \
	internet.h cmdparse.h timer.h iface.h proc.h udp.h ip.h rip.h \
	commands.h
ripdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h \
	timer.h rip.h iface.h proc.h udp.h internet.h ip.h trace.h
services.o: global.h ../lib/configure.h ../lib/strdup.h socket.h mbuf.h \
	proc.h timer.h netuser.h
session.o: global.h ../lib/configure.h ../lib/strdup.h config.h mbuf.h proc.h \
	timer.h tcp.h iface.h internet.h ip.h netuser.h ftp.h session.h \
	hardware.h telnet.h ax25.h icmp.h tty.h socket.h cmdparse.h \
	commands.h main.h axclient.h lapb.h finger.h netrom.h
slhc.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h internet.h ip.h \
	iface.h proc.h timer.h tcp.h netuser.h slhc.h
slhcdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h internet.h \
	ip.h iface.h proc.h timer.h slhc.h tcp.h netuser.h trace.h
slip.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h ip.h internet.h slhc.h tcp.h netuser.h asy.h slip.h trace.h \
	pktdrvr.h
smisc.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h socket.h proc.h \
	timer.h remote.h tcp.h iface.h internet.h ip.h netuser.h udp.h \
	commands.h hardware.h main.h
sntp.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h socket.h udp.h internet.h ip.h netuser.h cmdparse.h session.h \
	hardware.h telnet.h tcp.h ax25.h
tcpcmd.o: global.h ../lib/configure.h ../lib/strdup.h timer.h mbuf.h \
	netuser.h internet.h tcp.h iface.h proc.h ip.h cmdparse.h commands.h \
	socket.h session.h hardware.h telnet.h ax25.h main.h
tcpdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h \
	internet.h tcp.h iface.h proc.h timer.h ip.h trace.h
tcpgate.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h tcp.h \
	iface.h proc.h timer.h internet.h ip.h hpux.h ../lib/buildsaddr.h
tcphdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h tcp.h iface.h \
	proc.h timer.h internet.h ip.h netuser.h
tcpin.o: global.h ../lib/configure.h ../lib/strdup.h timer.h mbuf.h netuser.h \
	internet.h tcp.h iface.h proc.h ip.h icmp.h
tcpout.o: global.h ../lib/configure.h ../lib/strdup.h timer.h mbuf.h \
	netuser.h internet.h tcp.h iface.h proc.h ip.h
tcpsubr.o: global.h ../lib/configure.h ../lib/strdup.h timer.h mbuf.h \
	netuser.h internet.h tcp.h iface.h proc.h ip.h
tcptimer.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h timer.h \
	netuser.h internet.h tcp.h iface.h proc.h ip.h
tcpuser.o: global.h ../lib/configure.h ../lib/strdup.h timer.h mbuf.h \
	netuser.h socket.h proc.h internet.h tcp.h iface.h ip.h icmp.h
telnet.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h socket.h proc.h \
	timer.h telnet.h tcp.h iface.h internet.h ip.h netuser.h session.h \
	hardware.h ax25.h icmp.h tty.h commands.h cmdparse.h
timep.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h socket.h udp.h internet.h ip.h netuser.h
timer.o: global.h ../lib/configure.h ../lib/strdup.h timer.h proc.h mbuf.h \
	commands.h daemon.h hardware.h socket.h
tnserv.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h socket.h proc.h \
	timer.h netuser.h tcp.h iface.h internet.h ip.h login.h tipmail.h
trace.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h iface.h proc.h \
	timer.h commands.h trace.h session.h hardware.h telnet.h tcp.h \
	internet.h ip.h netuser.h ax25.h
transport.o: global.h ../lib/configure.h ../lib/strdup.h netuser.h mbuf.h \
	timer.h ax25.h iface.h proc.h lapb.h netrom.h session.h hardware.h \
	telnet.h tcp.h internet.h ip.h transport.h
ttydriv.o: global.h ../lib/configure.h ../lib/strdup.h tty.h
tun.o: global.h ../lib/configure.h ../lib/strdup.h ../lib/strerror.h mbuf.h \
	iface.h proc.h timer.h netuser.h trace.h hpux.h
udp.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h iface.h \
	proc.h timer.h udp.h internet.h ip.h icmp.h
udpcmd.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h udp.h \
	iface.h proc.h timer.h internet.h ip.h cmdparse.h commands.h
udpdump.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h netuser.h \
	internet.h udp.h iface.h proc.h timer.h ip.h socket.h trace.h
udphdr.o: global.h ../lib/configure.h ../lib/strdup.h mbuf.h ip.h iface.h \
	proc.h timer.h internet.h udp.h netuser.h
