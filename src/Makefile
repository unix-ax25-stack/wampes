# @(#) $Id: Makefile,v 1.108 2006/03/12 10:24:06 dl9sau Exp $

DEST          = $(TCPDIR)

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
		cmdparse.h \
		commands.h \
		crc.h \
		dama.h \
		daemon.h \
		devparam.h \
		dirutil.h \
		domain.h \
		files.h \
		finger.h \
		flexnet.h \
		ftp.h \
		ftpserv.h \
		global.h \
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
		pidfilter.h \
		pktdrvr.h \
		proc.h \
		remote.h \
		remote_net.h \
		rip.h \
		routefilter.h \
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
		udp.h \
		uhnp.h

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
		axhc.o \
		axip.o \
		axserver.o \
		cmdparse.o \
		config.o \
		crc.o \
		dama.o \
		devparam.o \
		dirutil.o \
		domain.o \
		domhdr.o \
		ethertap.o \
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
		pidfilter.o \
		remote_net.o \
		rip.o \
		ripcmd.o \
		ripdump.o \
		routefilter.o \
		services.o \
		session.o \
		setsp.o \
		slhc.o \
		slhcdump.o \
		sixpack.o \
		slip.o \
		smisc.o \
		sockaddr_util.o \
		sntp.o \
		tncinit.o \
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
		uhnp.o \
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
		axhc.c \
		axip.c \
		axserver.c \
		cmdparse.c \
		config.c \
		crc.c \
		devparam.c \
		dirutil.c \
		domain.c \
		domhdr.c \
		ethertap.c \
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
		pidfilter.c \
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
		sockaddr_util.c \
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
		uhnp.c \
		version.c

min:         all

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
	NetBSD*) \
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
	Darwin*) \
		$(CC) -D__MACOSX__ -E $< > setsp.s; cc -c setsp.s;; \
	*) \
		echo System `uname -sr` is not supported;; \
	esac
	@rm -f setsp.s

$(PROGRAM):     $(OBJS) $(LIBS)
		@echo "Linking $(PROGRAM) ..."
		$(RMTARGET)
		@$(LD) $(LDFLAGS) $(OBJS) $(LIBS) $(SLIBS) -o $(PROGRAM)

# The banner used to say the same thing since the 5th of March 2000, so a
# running node could not be matched against a source tree - which cost real
# time on 2026-08-15, when the installed copy was three days old and nothing
# said so.  It now carries the commit date, the commit, and a "+" if the tree
# had uncommitted changes when it was built.  Not git's own "-dirty": nobody
# likes running something that calls itself dirty, and "+" says the same.
#
# version.c is tracked and must not be generated, so the string arrives
# through this header, which is not tracked.  FORCE makes the recipe run every
# time; the file is only replaced when its contents actually change, so an
# unchanged tree does not drag every build into recompiling version.o.
#
# Without git - a source archive - it falls back to the string from 2000.
version_git.h: FORCE
	@d=`git -C .. log -1 --format=%cd --date=short 2>/dev/null`; \
	h=`git -C .. rev-parse --short HEAD 2>/dev/null`; \
	if [ -n "$$h" ]; then \
		git -C .. diff --quiet HEAD 2>/dev/null || h="$$h+"; \
		v="WAMPES-$$d-g$$h"; \
	else \
		v="WAMPES-000305"; \
	fi; \
	echo "#define WAMPES_VERSION \"$$v\"" > $@.new; \
	if cmp -s $@.new $@; then rm -f $@.new; \
	else mv $@.new $@; echo "version: $$v"; fi

FORCE:

clean:;         @rm -f *.o $(PROGRAM) version_git.h lint tags TAGS *~ \#*\#

_install:       $(SBINDIR)/net

lint:           $(HDRS) $(SRCS); @lih $(SRCS) > lint

tags:           $(HDRS) $(SRCS)
		@ctags -w $(HDRS) $(SRCS)

TAGS:           $(HDRS) $(SRCS)
		@etags $(HDRS) $(SRCS)

# The daemon moves to sbin - it is not a user command - but keeps its plain
# name: inside $(TCPDIR) nothing else competes for it.  The "wampes-" prefix
# is for the shared directories only.
#
# The symlink at $(TCPDIR)/net keeps existing start scripts working, and
# doc/wampes.mm has called it that for thirty years.
$(SBINDIR)/net: $(PROGRAM)
		$(MKDIR)
		$(RMTARGET)
		cp $(PROGRAM) $@
		rm -f $(TCPDIR)/net
		ln -s sbin/net $(TCPDIR)/net

.c.o:
		@rm -f $@
		$(CC) $(CFLAGS) -DAX25_VJCOMP -c $<

###
adjtime.o:\
	../lib/configure.h\
	adjtime.c
alloc.o:\
	../lib/configure.h\
	../lib/strdup.h\
	alloc.c\
	cmdparse.h\
	global.h\
	mbuf.h
arp.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.c\
	arp.h\
	ax25.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	proc.h\
	timer.h
arpcmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	arpcmd.c\
	ax25.h\
	cmdparse.h\
	commands.h\
	global.h\
	iface.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h
arpdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	arpdump.c\
	global.h\
	iface.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h
arpfile.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	arpfile.c\
	global.h\
	iface.h\
	main.h\
	mbuf.h\
	proc.h\
	timer.h
arphdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	arphdr.c\
	global.h\
	iface.h\
	mbuf.h\
	proc.h\
	timer.h
asy.o:\
	../lib/configure.h\
	../lib/strdup.h\
	asy.c\
	asy.h\
	ax25.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	kiss.h\
	mbuf.h\
	n8250.h\
	netuser.h\
	nrs.h\
	proc.h\
	slhc.h\
	slip.h\
	tcp.h\
	timer.h
ax25.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	ax25.c\
	ax25.h\
	devparam.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.h\
	mbuf.h\
	netrom.h\
	netuser.h\
	proc.h\
	session.h\
	slhc.h\
	slip.h\
	tcp.h\
	telnet.h\
	timer.h
ax25cmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ax25cmd.c\
	cmdparse.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h\
	tty.h
ax25dump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ax25dump.c\
	global.h\
	iface.h\
	lapb.h\
	mbuf.h\
	proc.h\
	socket.h\
	timer.h\
	trace.h
ax25file.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ax25file.c\
	global.h\
	iface.h\
	main.h\
	mbuf.h\
	proc.h\
	timer.h
ax25hdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ax25hdr.c\
	global.h\
	iface.h\
	mbuf.h\
	proc.h\
	timer.h
ax25subr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ax25subr.c\
	global.h\
	iface.h\
	lapb.h\
	mbuf.h\
	proc.h\
	timer.h
ax25user.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ax25user.c\
	global.h\
	iface.h\
	lapb.h\
	mbuf.h\
	proc.h\
	timer.h
axclient.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	axclient.c\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	tcp.h\
	telnet.h\
	timer.h
axheard.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	axheard.c\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	proc.h\
	timer.h
axip.o:\
	../lib/configure.h\
	../lib/strdup.h\
	../lib/strerror.h\
	ax25.h\
	axip.c\
	cmdparse.h\
	crc.h\
	global.h\
	hpux.h\
	iface.h\
	internet.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	timer.h\
	trace.h\
	uhnp.h
axserver.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	axserver.c\
	global.h\
	iface.h\
	lapb.h\
	login.h\
	mbuf.h\
	proc.h\
	timer.h
cmdparse.o:\
	../lib/configure.h\
	../lib/strdup.h\
	../lib/strtoul.h\
	cmdparse.c\
	cmdparse.h\
	global.h\
	mbuf.h\
	proc.h\
	timer.h
config.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	asy.h\
	ax25.h\
	ax25mail.h\
	cmdparse.h\
	commands.h\
	config.c\
	daemon.h\
	flexnet.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	kiss.h\
	lapb.h\
	mbuf.h\
	netrom.h\
	netuser.h\
	nrs.h\
	pktdrvr.h\
	proc.h\
	session.h\
	slhc.h\
	slip.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h\
	tipmail.h\
	trace.h\
	udp.h
crc.o:\
	../lib/calc_crc.h\
	../lib/configure.h\
	../lib/strdup.h\
	crc.c\
	crc.h\
	global.h\
	mbuf.h
dama.o:\
	../lib/configure.h\
	ax25.h\
	cmdparse.h\
	dama.c\
	dama.h\
	global.h\
	iface.h\
	lapb.h\
	mbuf.h\
	timer.h

devparam.o:\
	../lib/configure.h\
	../lib/strdup.h\
	devparam.c\
	devparam.h\
	global.h
dirutil.o:\
	../lib/configure.h\
	../lib/strdup.h\
	commands.h\
	dirutil.c\
	dirutil.h\
	global.h\
	hpux.h
domain.o:\
	../lib/configure.h\
	../lib/hostdb.h\
	../lib/strdup.h\
	cmdparse.h\
	domain.c\
	domain.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	tcp.h\
	timer.h\
	udp.h
domhdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	domain.h\
	domhdr.c\
	global.h\
	mbuf.h\
	proc.h\
	timer.h
ethertap.o:\
	../lib/configure.h\
	../lib/strdup.h\
	../lib/strerror.h\
	ethertap.c\
	global.h\
	hpux.h\
	iface.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h
files.o:\
	files.c\
	files.h
finger.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	finger.c\
	finger.h\
	ftp.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h
flexnet.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	cmdparse.h\
	commands.h\
	flexnet.c\
	flexnet.h\
	global.h\
	iface.h\
	lapb.h\
	mbuf.h\
	proc.h\
	routefilter.h\
	timer.h\
	trace.h
ftp.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	ftp.c\
	ftp.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	tcp.h\
	telnet.h\
	timer.h
ftpcli.o:\
	../lib/configure.h\
	../lib/seek.h\
	../lib/strdup.h\
	ax25.h\
	cmdparse.h\
	commands.h\
	dirutil.h\
	ftp.h\
	ftpcli.c\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h
ftpserv.o:\
	../lib/configure.h\
	../lib/seek.h\
	../lib/seteugid.h\
	../lib/strdup.h\
	../lib/strerror.h\
	ax25.h\
	dirutil.h\
	ftp.h\
	ftpserv.c\
	ftpserv.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	login.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h
hpux.o:\
	../lib/configure.h\
	../lib/strdup.h\
	commands.h\
	files.h\
	global.h\
	hpux.c\
	hpux.h\
	iface.h\
	login.h\
	main.h\
	mbuf.h\
	proc.h\
	timer.h
icmp.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.c\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	ping.h\
	proc.h\
	timer.h
icmpcmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	commands.h\
	global.h\
	icmp.h\
	icmpcmd.c\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	timer.h
icmpdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	icmpdump.c\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h
icmphdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	icmphdr.c\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	proc.h\
	timer.h
icmpmsg.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmpmsg.c
iface.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	cmdparse.h\
	commands.h\
	global.h\
	icmp.h\
	iface.c\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h
ip.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.c\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h
ipcmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	ipcmd.c\
	mbuf.h\
	netuser.h\
	proc.h\
	rip.h\
	timer.h\
	udp.h
ipdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	ipdump.c\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h
ipfile.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	ipfile.c\
	main.h\
	mbuf.h\
	proc.h\
	timer.h
ipfilter.o:\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	global.h\
	ipfilter.c\
	netuser.h
iphdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	iphdr.c\
	mbuf.h\
	proc.h\
	timer.h
ipip.o:\
	../lib/configure.h\
	../lib/strdup.h\
	../lib/strerror.h\
	cmdparse.h\
	global.h\
	hpux.h\
	iface.h\
	internet.h\
	ipip.c\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	timer.h\
	trace.h\
	uhnp.h
iproute.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	ipfilter.h\
	iproute.c\
	mbuf.h\
	netuser.h\
	proc.h\
	rip.h\
	tcp.h\
	timer.h\
	trace.h\
	udp.h
kernel.o:\
	../lib/configure.h\
	../lib/strdup.h\
	daemon.h\
	global.h\
	kernel.c\
	mbuf.h\
	proc.h\
	socket.h\
	timer.h
kiss.o:\
	../lib/configure.h\
	../lib/strdup.h\
	asy.h\
	ax25.h\
	crc.h\
	devparam.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	kiss.c\
	kiss.h\
	mbuf.h\
	netuser.h\
	pktdrvr.h\
	proc.h\
	slhc.h\
	slip.h\
	tcp.h\
	timer.h
kissdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	crc.h\
	devparam.h\
	global.h\
	iface.h\
	kiss.h\
	kissdump.c\
	mbuf.h\
	proc.h\
	timer.h\
	trace.h
krnlif.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	commands.h\
	devparam.h\
	global.h\
	hpux.h\
	iface.h\
	krnlif.c\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h
ksubr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	commands.h\
	global.h\
	ksubr.c\
	main.h\
	mbuf.h\
	proc.h\
	timer.h
lapb.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	dama.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.c\
	lapb.h\
	mbuf.h\
	proc.h\
	timer.h
lapbtime.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	dama.h\
	global.h\
	iface.h\
	lapb.h\
	lapbtime.c\
	mbuf.h\
	proc.h\
	timer.h
lcsum.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lcsum.c\
	mbuf.h\
	proc.h\
	timer.h
login.o:\
	../lib/callvalid.h\
	../lib/configure.h\
	../lib/seek.h\
	../lib/strdup.h\
	cmdparse.h\
	commands.h\
	global.h\
	hpux.h\
	iface.h\
	internet.h\
	ip.h\
	login.c\
	login.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	telnet.h\
	timer.h
mail_daemn.o:\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	commands.h\
	global.h\
	mail.h\
	mail_daemn.c\
	timer.h
mail_retrn.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	hpux.h\
	mail.h\
	mail_retrn.c
mail_smtp.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	mail.h\
	mail_smtp.c\
	mbuf.h\
	timer.h\
	transport.h
mail_subr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	mail.h\
	mail_subr.c\
	timer.h
main.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	ax25.h\
	cmdparse.h\
	commands.h\
	daemon.h\
	devparam.h\
	files.h\
	ftp.h\
	global.h\
	hpux.h\
	iface.h\
	internet.h\
	ip.h\
	kiss.h\
	main.c\
	main.h\
	mbuf.h\
	netrom.h\
	netuser.h\
	proc.h\
	remote.h\
	remote_net.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h\
	trace.h\
	tty.h\
	udp.h
mbuf.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	mbuf.c\
	mbuf.h\
	proc.h\
	timer.h
misc.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	mbuf.h\
	misc.c\
	proc.h\
	socket.h\
	timer.h
n8250.o:\
	../lib/configure.h\
	../lib/strdup.h\
	asy.h\
	devparam.h\
	global.h\
	hpux.h\
	iface.h\
	mbuf.h\
	n8250.c\
	n8250.h\
	proc.h\
	timer.h
netrom.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	ax25.h\
	cmdparse.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.h\
	login.h\
	mbuf.h\
	netrom.c\
	netrom.h\
	netuser.h\
	proc.h\
	routefilter.h\
	session.h\
	tcp.h\
	telnet.h\
	timer.h\
	trace.h
netuser.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	netuser.c\
	netuser.h
ni.o:\
	../lib/configure.h\
	../lib/strdup.h\
	../lib/strerror.h\
	global.h\
	hpux.h\
	iface.h\
	mbuf.h\
	netuser.h\
	ni.c\
	proc.h\
	timer.h\
	trace.h
nrdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netrom.h\
	netuser.h\
	nrdump.c\
	proc.h\
	session.h\
	tcp.h\
	telnet.h\
	timer.h\
	trace.h
nrs.o:\
	../lib/configure.h\
	../lib/strdup.h\
	asy.h\
	ax25.h\
	commands.h\
	global.h\
	iface.h\
	mbuf.h\
	nrs.c\
	nrs.h\
	proc.h\
	timer.h\
	trace.h
pathname.o:\
	../lib/configure.h\
	../lib/strdup.h\
	dirutil.h\
	global.h\
	pathname.c
ping.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	commands.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	ping.c\
	ping.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h
remote_net.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	global.h\
	hpux.h\
	iface.h\
	main.h\
	mbuf.h\
	proc.h\
	remote_net.c\
	timer.h\
	transport.h
rip.o:\
	../lib/configure.h\
	../lib/strdup.h\
	arp.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	rip.c\
	rip.h\
	timer.h\
	udp.h
ripcmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	rip.h\
	ripcmd.c\
	timer.h\
	udp.h
ripdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	rip.h\
	ripdump.c\
	timer.h\
	trace.h\
	udp.h
pidfilter.o:\
	../lib/configure.h\
	ax25.h\
	cmdparse.h\
	global.h\
	iface.h\
	mbuf.h\
	pidfilter.c\
	pidfilter.h

routefilter.o:\
	../lib/configure.h\
	ax25.h\
	global.h\
	iface.h\
	routefilter.c\
	routefilter.h

services.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	mbuf.h\
	netuser.h\
	proc.h\
	services.c\
	socket.h\
	timer.h

session.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	axclient.h\
	cmdparse.h\
	commands.h\
	finger.h\
	ftp.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.h\
	main.h\
	mbuf.h\
	netrom.h\
	netuser.h\
	proc.h\
	session.c\
	session.h\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h\
	tty.h
setsp.o:\
	setsp.c
slhc.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	slhc.c\
	slhc.h\
	tcp.h\
	timer.h
slhcdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	slhc.h\
	slhcdump.c\
	tcp.h\
	timer.h\
	trace.h
slip.o:\
	../lib/configure.h\
	../lib/strdup.h\
	asy.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	pktdrvr.h\
	proc.h\
	slhc.h\
	slip.c\
	slip.h\
	tcp.h\
	timer.h\
	trace.h
smisc.o:\
	../lib/configure.h\
	../lib/strdup.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	main.h\
	mbuf.h\
	netuser.h\
	proc.h\
	remote.h\
	smisc.c\
	socket.h\
	tcp.h\
	timer.h\
	udp.h
sntp.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	cmdparse.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	sntp.c\
	socket.h\
	tcp.h\
	telnet.h\
	timer.h\
	udp.h
tcpcmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	cmdparse.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	main.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	tcpcmd.c\
	telnet.h\
	timer.h
tcpdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcpdump.c\
	timer.h\
	trace.h
tcpgate.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	hpux.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcpgate.c\
	timer.h
tcphdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcphdr.c\
	timer.h
tcpin.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcpin.c\
	timer.h
tcpout.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcpout.c\
	timer.h
tcpsubr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcpsubr.c\
	timer.h
tcptimer.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	tcp.h\
	tcptimer.c\
	timer.h
tcpuser.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	tcp.h\
	tcpuser.c\
	timer.h
telnet.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	cmdparse.h\
	commands.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	socket.h\
	tcp.h\
	telnet.c\
	telnet.h\
	timer.h\
	tty.h
timep.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	timep.c\
	timer.h\
	udp.h
timer.o:\
	../lib/configure.h\
	../lib/strdup.h\
	commands.h\
	daemon.h\
	global.h\
	mbuf.h\
	proc.h\
	socket.h\
	timer.c\
	timer.h
tnserv.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	login.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	tcp.h\
	timer.h\
	tipmail.h\
	tnserv.c
trace.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	session.h\
	tcp.h\
	telnet.h\
	timer.h\
	trace.c\
	trace.h
transport.o:\
	../lib/configure.h\
	../lib/strdup.h\
	ax25.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	lapb.h\
	mbuf.h\
	netrom.h\
	netuser.h\
	proc.h\
	session.h\
	tcp.h\
	telnet.h\
	timer.h\
	transport.c\
	transport.h
ttydriv.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	tty.h\
	ttydriv.c
tun.o:\
	../lib/configure.h\
	../lib/strdup.h\
	../lib/strerror.h\
	global.h\
	hpux.h\
	iface.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	trace.h\
	tun.c
udp.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	icmp.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	udp.c\
	udp.h
udpcmd.o:\
	../lib/configure.h\
	../lib/strdup.h\
	cmdparse.h\
	commands.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	udp.h\
	udpcmd.c
udpdump.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	socket.h\
	timer.h\
	trace.h\
	udp.h\
	udpdump.c
udphdr.o:\
	../lib/configure.h\
	../lib/strdup.h\
	global.h\
	iface.h\
	internet.h\
	ip.h\
	mbuf.h\
	netuser.h\
	proc.h\
	timer.h\
	udp.h\
	udphdr.c
sockaddr_util.o:\
	../lib/buildsaddr.h\
	../lib/configure.h\
	global.h\
	sockaddr_util.h\
	sockaddr_util.c

uhnp.o:\
	global.h\
	timer.h\
	uhnp.h\
	uhnp.c
version.o:\
	version.c\
	version_git.h
