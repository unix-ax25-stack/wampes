ANSI          =
ANSI          = -Aa -D_HPUX_SOURCE

DEFINES       = -DUNIX

CFLAGS        = $(ANSI) $(DEFINES) -O
CFLAGS        = $(ANSI) $(DEFINES) -g

DEST          = /tcp

EXTHDRS       =

HDRS          = 8250.h \
		arp.h \
		asy.h \
		ax25.h \
		axclient.h \
		bootp.h \
		buildsaddr.h \
		cmdparse.h \
		commands.h \
		config.h \
		crc.h \
		daemon.h \
		devparam.h \
		dirutil.h \
		enet.h \
		files.h \
		finger.h \
		ftp.h \
		global.h \
		hardware.h \
		hpux.h \
		icmp.h \
		iface.h \
		internet.h \
		ip.h \
		kiss.h \
		lapb.h \
		login.h \
		mail.h \
		main.h \
		mbuf.h \
		netrom.h \
		netuser.h \
		nrs.h \
		pktdrvr.h \
		proc.h \
		remote.h \
		remote_net.h \
		rip.h \
		session.h \
		slip.h \
		socket.h \
		tcp.h \
		telnet.h \
		timer.h \
		trace.h \
		transport.h \
		tty.h \
		udp.h

LD            = $(CC)

LDFLAGS       = $(CFLAGS)

LIBS          =

MAKEFILE      = Makefile

OBJS          = 8250.o \
		alloc.o \
		arp.o \
		arpcmd.o \
		arpdump.o \
		arpfile.o \
		arphdr.o \
		asy.o \
		ax25.o \
		ax25dump.o \
		ax25file.o \
		ax25hdr.o \
		ax25subr.o \
		axclient.o \
		axip.o \
		axserver.o \
		buildsaddr.o \
		cmdparse.o \
		config.o \
		crc.o \
		devparam.o \
		dirutil.o \
		domain.o \
		files.o \
		finger.o \
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
		iphdr.o \
		iproute.o \
		kernel.o \
		kiss.o \
		kissdump.o \
		ksubr.o \
		lapb.o \
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
		netlog.o \
		netrom.o \
		netuser.o \
		nrdump.o \
		nrs.o \
		pathname.o \
		remote_kbd.o \
		remote_net.o \
		rip.o \
		ripcmd.o \
		ripdump.o \
		services.o \
		session.o \
		slip.o \
		smisc.o \
		stime.o \
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

SHELL         = /bin/sh

SRCS          = 8250.c \
		alloc.c \
		arp.c \
		arpcmd.c \
		arpdump.c \
		arpfile.c \
		arphdr.c \
		asy.c \
		ax25.c \
		ax25dump.c \
		ax25file.c \
		ax25hdr.c \
		ax25subr.c \
		axclient.c \
		axip.c \
		axserver.c \
		buildsaddr.c \
		cmdparse.c \
		config.c \
		crc.c \
		devparam.c \
		dirutil.c \
		domain.c \
		files.c \
		finger.c \
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
		iphdr.c \
		iproute.c \
		kernel.c \
		kiss.c \
		kissdump.c \
		ksubr.c \
		lapb.c \
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
		netlog.c \
		netrom.c \
		netuser.c \
		nrdump.c \
		nrs.c \
		pathname.c \
		remote_kbd.c \
		remote_net.c \
		rip.c \
		ripcmd.c \
		ripdump.c \
		services.c \
		session.c \
		slip.c \
		smisc.c \
		stime.c \
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

all:            $(PROGRAM)

setsp.o:        setsp.300 setsp.800
		if hp9000s300; then as setsp.300 -o setsp.o; fi
		if hp9000s800; then as setsp.800 -o setsp.o; fi

$(PROGRAM):     $(OBJS) setsp.o $(LIBS)
		@echo "Linking $(PROGRAM) ..."
		@$(LD) $(LDFLAGS) $(OBJS) setsp.o $(LIBS) -o $(PROGRAM)

clean:;         @rm -f $(OBJS) *.o core

clobber:;       @rm -f $(OBJS) $(PROGRAM) *.o core tags

depend:;        @mkmf -f $(MAKEFILE) ROOT=$(ROOT)

echo:;          @echo $(HDRS) $(SRCS)

index:;         @ctags -wx $(HDRS) $(SRCS)

install:        $(DEST)/$(PROGRAM)

lint:;          @lint $(ANSI) $(DEFINES) $(SRCS)

print:;         @$(PRINT) $(HDRS) $(SRCS)

tags:           $(HDRS) $(SRCS); @ctags $(HDRS) $(SRCS)

update:         $(DEST)/$(PROGRAM)

$(DEST)/$(PROGRAM): $(PROGRAM)
		@mv -f $@ /tmp/old.$$$$ >/dev/null 2>&1 ; exit 0
		cp $(PROGRAM) $@

.c.o:
		@rm -f $@
		-$(CC) $(CFLAGS) -c $<

###
8250.o: global.h mbuf.h proc.h timer.h iface.h 8250.h asy.h devparam.h \
	buildsaddr.h hpux.h
alloc.o: global.h
arp.o: global.h mbuf.h timer.h iface.h proc.h enet.h ax25.h icmp.h internet.h \
	ip.h session.h hardware.h telnet.h arp.h
arpcmd.o: global.h mbuf.h timer.h enet.h iface.h proc.h ax25.h arp.h \
	netuser.h cmdparse.h commands.h
arpdump.o: global.h mbuf.h arp.h iface.h proc.h timer.h netuser.h trace.h
arpfile.o: global.h timer.h arp.h mbuf.h iface.h proc.h
arphdr.o: global.h mbuf.h arp.h iface.h proc.h timer.h
asy.o: global.h config.h proc.h mbuf.h timer.h iface.h netuser.h 8250.h asy.h \
	ax25.h kiss.h nrs.h pktdrvr.h slip.h commands.h
ax25.o: global.h mbuf.h iface.h proc.h timer.h arp.h slip.h ax25.h lapb.h \
	netrom.h session.h hardware.h telnet.h ip.h internet.h
ax25dump.o: global.h mbuf.h ax25.h iface.h proc.h timer.h lapb.h trace.h \
	socket.h
ax25file.o: global.h timer.h iface.h mbuf.h proc.h ax25.h
ax25hdr.o: global.h mbuf.h ax25.h iface.h proc.h timer.h
ax25subr.o: global.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h
axclient.o: global.h config.h netuser.h mbuf.h timer.h ax25.h iface.h proc.h \
	lapb.h session.h hardware.h telnet.h
axip.o: global.h mbuf.h iface.h proc.h timer.h netuser.h ax25.h socket.h \
	trace.h pktdrvr.h cmdparse.h hpux.h
axserver.o: global.h timer.h ax25.h mbuf.h iface.h proc.h lapb.h login.h
buildsaddr.o: global.h
cmdparse.o: global.h proc.h mbuf.h timer.h cmdparse.h
config.o: global.h config.h mbuf.h timer.h proc.h iface.h ip.h internet.h \
	tcp.h netuser.h udp.h lapb.h ax25.h enet.h kiss.h netrom.h session.h \
	hardware.h telnet.h pktdrvr.h slip.h arp.h icmp.h cmdparse.h \
	commands.h daemon.h bootp.h socket.h trace.h
crc.o: global.h mbuf.h crc.h
devparam.o: global.h devparam.h
dirutil.o: global.h dirutil.h commands.h
domain.o: global.h timer.h netuser.h cmdparse.h
files.o: global.h netuser.h files.h
finger.o: config.h global.h mbuf.h timer.h internet.h icmp.h iface.h proc.h \
	ip.h session.h hardware.h telnet.h ax25.h netuser.h tcp.h ftp.h \
	finger.h
ftp.o: global.h mbuf.h netuser.h timer.h tcp.h iface.h proc.h internet.h ip.h \
	ftp.h session.h hardware.h telnet.h ax25.h
ftpcli.o: global.h mbuf.h session.h proc.h timer.h hardware.h telnet.h ax25.h \
	iface.h cmdparse.h socket.h icmp.h internet.h ip.h tcp.h netuser.h \
	ftp.h commands.h dirutil.h
ftpserv.o: global.h mbuf.h socket.h proc.h timer.h netuser.h tcp.h iface.h \
	internet.h ip.h dirutil.h login.h ftp.h
hpux.o: global.h iface.h mbuf.h proc.h timer.h files.h hardware.h login.h \
	commands.h main.h hpux.h
icmp.o: global.h mbuf.h iface.h proc.h timer.h ip.h internet.h icmp.h \
	session.h hardware.h telnet.h ax25.h netuser.h
icmpcmd.o: global.h icmp.h mbuf.h iface.h proc.h timer.h internet.h ip.h \
	session.h hardware.h telnet.h ax25.h netuser.h socket.h cmdparse.h \
	commands.h
icmpdump.o: global.h mbuf.h internet.h netuser.h icmp.h iface.h proc.h \
	timer.h ip.h session.h hardware.h telnet.h ax25.h trace.h
icmphdr.o: global.h mbuf.h internet.h ip.h iface.h proc.h timer.h icmp.h \
	session.h hardware.h telnet.h ax25.h
icmpmsg.o: global.h
iface.o: global.h mbuf.h proc.h timer.h iface.h ip.h internet.h netuser.h \
	ax25.h enet.h pktdrvr.h cmdparse.h commands.h
ip.o: global.h mbuf.h timer.h internet.h netuser.h iface.h proc.h pktdrvr.h \
	ip.h icmp.h session.h hardware.h telnet.h ax25.h
ipcmd.o: global.h mbuf.h internet.h timer.h netuser.h iface.h proc.h ip.h \
	cmdparse.h commands.h rip.h udp.h
ipdump.o: global.h mbuf.h internet.h iface.h proc.h timer.h ip.h trace.h \
	netuser.h
ipfile.o: global.h timer.h iface.h mbuf.h proc.h ip.h internet.h
iphdr.o: global.h mbuf.h ip.h iface.h proc.h timer.h internet.h
iproute.o: global.h mbuf.h iface.h proc.h timer.h internet.h ip.h netuser.h \
	icmp.h session.h hardware.h telnet.h ax25.h rip.h udp.h trace.h \
	pktdrvr.h bootp.h socket.h
kernel.o: global.h mbuf.h proc.h timer.h socket.h daemon.h hardware.h
kiss.o: global.h mbuf.h iface.h proc.h timer.h kiss.h devparam.h slip.h asy.h \
	ax25.h crc.h
kissdump.o: global.h mbuf.h kiss.h iface.h proc.h timer.h devparam.h ax25.h \
	trace.h crc.h
ksubr.o: global.h proc.h mbuf.h timer.h commands.h
lapb.o: global.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h netrom.h \
	session.h hardware.h telnet.h asy.h cmdparse.h netuser.h
lcsum.o: global.h ip.h mbuf.h iface.h proc.h timer.h internet.h
login.o: global.h mbuf.h timer.h hpux.h telnet.h session.h proc.h hardware.h \
	ax25.h iface.h login.h
mail_bbs.o: global.h mbuf.h transport.h timer.h mail.h
mail_daemn.o: global.h timer.h mail.h cmdparse.h commands.h
mail_retrn.o: mail.h global.h
mail_smtp.o: global.h mbuf.h transport.h timer.h mail.h
mail_subr.o: timer.h global.h mail.h
main.o: global.h mbuf.h timer.h proc.h iface.h ip.h internet.h tcp.h \
	netuser.h udp.h ax25.h kiss.h enet.h netrom.h session.h hardware.h \
	telnet.h ftp.h tty.h socket.h cmdparse.h commands.h daemon.h \
	devparam.h files.h main.h remote.h trace.h hpux.h remote_net.h
mbuf.o: global.h mbuf.h proc.h timer.h
misc.o: global.h socket.h mbuf.h proc.h timer.h
netlog.o: global.h
netrom.o: global.h config.h netuser.h mbuf.h timer.h iface.h proc.h arp.h \
	ip.h internet.h ax25.h lapb.h netrom.h session.h hardware.h telnet.h \
	cmdparse.h login.h
netuser.o: global.h netuser.h
nrdump.o: global.h mbuf.h timer.h ax25.h iface.h proc.h netrom.h session.h \
	hardware.h telnet.h trace.h
nrs.o: global.h timer.h mbuf.h iface.h proc.h pktdrvr.h ax25.h nrs.h asy.h \
	trace.h commands.h
pathname.o: global.h dirutil.h
remote_kbd.o: global.h hpux.h buildsaddr.h main.h proc.h mbuf.h timer.h
remote_net.o: global.h mbuf.h timer.h transport.h hpux.h buildsaddr.h
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
slip.o: global.h config.h mbuf.h iface.h proc.h timer.h ip.h internet.h asy.h \
	slip.h trace.h
smisc.o: global.h mbuf.h socket.h proc.h timer.h remote.h tcp.h iface.h \
	internet.h ip.h netuser.h udp.h commands.h hardware.h
stime.o: global.h mbuf.h netuser.h timer.h udp.h iface.h proc.h internet.h \
	ip.h
tcpcmd.o: global.h timer.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h \
	ip.h cmdparse.h commands.h
tcpdump.o: global.h mbuf.h netuser.h internet.h tcp.h iface.h proc.h timer.h \
	ip.h trace.h
tcpgate.o: global.h mbuf.h netuser.h tcp.h iface.h proc.h timer.h internet.h \
	ip.h hpux.h buildsaddr.h
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
telnet.o: global.h mbuf.h socket.h proc.h timer.h telnet.h session.h \
	hardware.h ax25.h iface.h icmp.h internet.h ip.h tcp.h netuser.h \
	tty.h commands.h
timer.o: global.h timer.h proc.h mbuf.h commands.h daemon.h hardware.h \
	socket.h
tnserv.o: global.h mbuf.h socket.h proc.h timer.h netuser.h tcp.h iface.h \
	internet.h ip.h login.h
trace.o: global.h mbuf.h iface.h proc.h timer.h pktdrvr.h commands.h trace.h
transport.o: global.h netuser.h mbuf.h timer.h ax25.h iface.h proc.h lapb.h \
	netrom.h session.h hardware.h telnet.h tcp.h internet.h ip.h \
	transport.h
ttydriv.o: global.h tty.h
udp.o: global.h mbuf.h netuser.h iface.h proc.h timer.h udp.h internet.h ip.h \
	icmp.h session.h hardware.h telnet.h ax25.h
udpcmd.o: global.h mbuf.h netuser.h udp.h iface.h proc.h timer.h internet.h \
	ip.h cmdparse.h commands.h
udpdump.o: global.h mbuf.h netuser.h internet.h udp.h iface.h proc.h timer.h \
	ip.h socket.h trace.h
udphdr.o: global.h mbuf.h ip.h iface.h proc.h timer.h internet.h udp.h \
	netuser.h
