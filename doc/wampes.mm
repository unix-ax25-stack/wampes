.\" @(#) $Header: /Users/thomas/direwolf-libax25-agwpe/wampes-import/wampes-cvs/wampes/doc/wampes.mm,v 1.3 1994/07/27 09:49:36 deyke Exp $
.\"
.\" Format this manual with:
.\" tbl -TX < manual.mm | nroff -mm | col
.\" or:
.\" tbl < manual.mm | troff -mm
.\"
.PH "" \" Page header
.SA 1 \" Right justified
.ds Ci 0 0 0 0 0 0 0 \" Table of contents indent list
.ds HF 3 3 3 3 3 3 3 \" All headers bold
.ds HP +2 +2 +2 +2 +2 +2 +2 \" All headers 2 points bigger
.nr Cl 7 \" Save all headers for table of contents
.nr Ej 1 \" Level 1 headers on new page
.nr Hb 7 \" Break after all headers
.nr Hs 7 \" Empty line after all headers
.nr Hy 1 \" Hyphenation on
.\"
.PF "^WAMPES Reference Manual^-\\\\nP-^Version 940727" \" Page footer
.\"
.S 30
.ce
\fBWAMPES Reference Manual\fP
.ce
Version 940727
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
.H 1 "Credits"
This manual is based in part on publications authored by
.DS I
Phil Karn, KA9Q
Bdale Garbee, N3EUA
Gerard van der Grinten, PA0GRI
.DE
.H 1 "Commands"
This section describes the commands recognized in command mode, or
within a startup file such as \fBnet.rc\fP. These are given in the following
notation:
.DS
\fBcommand\fP
\fBcommand\fP \fIparameter\fP
\fBcommand subcommand\fP \fIparameter\fP
\fBcommand\fP [\fIoptional_parameter\fP]
\fBcommand a|b\fP
.DE
Many commands take subcommands or parameters, which may be optional or
required. In general, if a required subcommand or parameter is omitted,
an error message will summarize the available subcommands or required
parameters. (Giving a '?' in place of the subcommand will also
generate the message. This is useful when the command word alone is a
valid command.) If a command takes an optional value parameter, issuing
the command without the parameter generally displays the current value
of the variable. (Exceptions to this rule are noted in the individual
command descriptions.)
.P
Two or more parameters separated by vertical bar(s) denote a choice
between the specified values. Optional parameters are shown enclosed in
[brackets], and a parameter shown as \fIparameter\fP should be
replaced with an actual value or string. For example, the notation
\fIhostid\fP denotes an actual host or gateway, which may be specified in
one of two ways: as a numeric IP address in dotted decimal notation
(eg. 44.0.0.1), or as a symbolic name listed in the file domain.txt.
.P
All commands and many subcommands may be abbreviated. You only need
type enough of a command's name to distinguish it from others that begin
with the same series of letters. Parameters, however, must be typed in
full.
.P
FTP subcommands (eg. put, get, dir, etc) are recognized only in
converse mode with the appropriate FTP session, they are not recognized
in command mode. (See the \fBFTP Subcommands\fP chapter.)
.H 2 "<cr>"
Entering a carriage return (empty line) while in command mode
puts you in converse mode with the current session. If there is
no current session, \fBWAMPES\fP remains in command mode.
.H 2 "!" " \fIshell_command_line\fP"
An alias for the \fBshell\fP command.
.H 2 "?"
Display a brief summary of top-level commands.
.H 2 "arp" " [\fIsubcommand\fP]"
Without an argument,
display the Address Resolution Protocol table that maps IP addresses
to their subnet (link) addresses.
For each IP address entry the subnet type (eg. \fBax25\fP, \fBnetrom\fP), link
address and time to expiration is shown. If the link address is currently
unknown, the number of IP datagrams awaiting resolution is also shown.
.H 3 "arp add" " \fIhostid\fP ax25|netrom \fIlink_addr\fP"
Add a permanent entry to the ARP table. It will not time out as
will an automatically created entry, but must be removed with the
\fBarp drop\fP command.
.H 3 "arp drop" " \fIhostid\fP ax25|netrom"
Remove the specified entry from the ARP table.
.H 3 "arp flush"
Drop all automatically created entries from the ARP table, permanent
entries are not affected.
.H 3 "arp publish" " \fIhostid\fP ax25|netrom \fIlink_addr\fP"
The \fBarp publish\fP command is similar to the \fBarp add\fP command,
but \fBWAMPES\fP will also respond to any ARP request it sees on the network
that seeks the specified address.
This is commonly referred to as "proxy arp",
and is considered a fairly dangerous tool.
The basic idea is that if you have two machines,
one on the air with a TNC,
and one connected to the first with a slip link,
you might want the first machine to publish it's own AX.25 address
as the right answer for ARP queries addressing the second machine.
This way, the rest of the world doesn't know the second machine
isn't really on the air.
(Use this feature with great care.)
.H 2 "asystat" " [\fIinterface\fP ...]"
Display statistics on the specified or all
attached asynchronous communications interfaces.
The display for each interface consists of three lines.
.P
The first line shows the interface name and the speed in
bits per second.
.P
The second line shows receiver (RX) event
counts: the total number of read system calls, received
characters, and the receiver high water mark. The receiver high water
mark is the maximum number of characters ever read from the
device during a single system call. This is useful for
monitoring system interrupt latency margins as it shows how
close the port hardware has come to overflowing due to the
inability of the CPU to respond in time.
The high water mark will be reset to 0 after \fBasystat\fP has
displayed its value.
.P
The third line shows transmit (TX) statistics, including the total
number of write system calls and transmitted characters.
.H 2 "attach" " \fItype\fP [\fItype specific options\fP]
Configure and attach an interface to the system.
The details are highly interface type dependent.
.H 3 "attach asy 0" " 0 \fIencapsulation\fP \fIname\fP 0 \fImtu\fP \fIspeed\fP"
Configure and attach an asynchronous communications interface to the system.
See the \fBifconfig encapsulation\fP command for the list of
available encapsulations.
\fIname\fP specifies the name of the interface,
and is also used to open the interface device file in the directory \fB/dev\fP.
\fImtu\fP is the Maximum Transmission Unit size, in bytes.
Datagrams larger than this limit will be fragmented at the IP layer
into smaller pieces.
For AX.25 UI frames, this limits the size of the information field.
For AX.25 I frames, however, the \fBax25 paclen\fP parameter is also relevant.
If the datagram or fragment is still larger than \fBpaclen\fP,
it is also fragmented at the AX.25 level (as opposed to the IP level) before
transmission.
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
\fIspeed\fP is the transmission speed in bits per second (eg. 9600).
.H 3 "attach asy \fIip-addr\fP" " \fIport\fP \fIencapsulation\fP \fIname\fP 0 \fImtu\fP \fIspeed\fP"
Configure and attach a UNIX TCP connection based interface to the system.
This is very similar to the asynchronous communications interface described above,
but instead of talking directly to a hardware device file, this interface type
will open a UNIX TCP connection to \fIip-addr\fP and \fIport\fP.
The primary use of this interface type is to talk to some TNC which is
connected to the system via the LAN.
\fIip-addr\fP is the destination IP address.
It has to be specified as one hexadecimal number.
For example, 127.0.0.1 has to be given as 7f000001.
\fIport\fP is the numeric destination TCP port address.
See the \fBifconfig encapsulation\fP command for the list of
available encapsulations.
\fIname\fP specifies the name of the interface.
\fImtu\fP is the Maximum Transmission Unit size, in bytes.
Datagrams larger than this limit will be fragmented at the IP layer
into smaller pieces.
For AX.25 UI frames, this limits the size of the information field.
For AX.25 I frames, however, the \fBax25 paclen\fP parameter is also relevant.
If the datagram or fragment is still larger than \fBpaclen\fP,
it is also fragmented at the AX.25 level (as opposed to the IP level) before
transmission.
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
\fIspeed\fP must be specified, but is ignored.
.H 3 "attach axip" " [\fIname\fP [ip|udp [\fIport\fP]]]"
This creates an AX.25 frame encapsulator for transmission
of AX.25 frames over the UNIX's networking system.
The interface will be named \fIname\fP,
or \fBaxip\fP if \fIname\fP is not specified.
The default encapsulation will use IP protocol 93,
but it is possible to use UDP instead.
If \fIport\fP is specified that IP protocol number or UDP port number
will be used instead of 93.
See also RFC1226 and the \fBaxip\fP command.
.H 3 "attach ipip" " [\fIname\fP [ip|udp [\fIport\fP]]]"
This creates an IP frame encapsulator for transmission
of IP frames over the UNIX's networking system.
The interface will be named \fIname\fP,
or \fBipip\fP if \fIname\fP is not specified.
The default encapsulation will use IP protocol 4,
but it is possible to use UDP instead.
If \fIport\fP is specified that IP protocol number or UDP port number
will be used instead of 4.
.H 3 "attach netrom"
This creates an IP frame encapsulator for transmission
of IP frames over the NET/ROM transport.
The interface will be named
\fBnetrom\fP.
.H 3 "attach ni" " \fIname\fP \fIip-addr\fP [\fInetmask\fP]"
Currently \fBattach ni\fP is available on HP-UX systems only.
.P
MORE TO BE WRITTEN.
.H 2 "ax25" " \fIsubcommand\fP"
These commands control the AX.25 service.
.H 3 "ax25 blimit" " [\fIlimit\fP]"
Display or set the AX.25 retransmission backoff limit. Normally each
successive AX.25 retransmission is delayed by a factor of 1.25 compared to
the previous interval, this is called \fBexponential backoff\fP.
When the number of retries reaches the \fBblimit\fP setting the backoff
is held at its current value, and not increased anymore.
Note that this is applicable only to actual AX.25 connections, UI frames
will never be retransmitted by the AX.25 layer.
The default is 16.
.H 3 "ax25 destlist" " [\fIinterface\fP]"
Display the destination list, i.e. the addressed-to stations. Next to the
time of the last transmission to that station the time that station replied
(if heard) is displayed. This gives a good reference to see if a station
is reachable and responding.
If \fIinterface\fP is given, only the list for that interface is displayed.
.H 3 "ax25 digipeat" " [0|1|2]"
Display or set the digipeat mode. The default is 2. MORE TO BE WRITTEN.
.H 3 "ax25 flush"
Clear the AX.25 "heard" list (see \fBax25 heard\fP).
.H 3 "ax25 heard" " [\fIinterface\fP]"
Display the AX.25 "heard" list. For each interface that is configured to
use AX.25, a list of all ax25_addresses heard through that interface is
shown, along with a count of the number of packets heard from each station
and the interval, in days:hr:min:sec format, since each station was last heard.
The local station appears first in the listing, the packet count
actually reflects the number of packets transmitted.
If \fIinterface\fP is given, only the list for that interface is displayed.
.H 3 "ax25 jumpstart" " \fIax25_addr\fP [on|off]"
The default is \fBoff\fP.
MORE TO BE WRITTEN.
.H 3 "ax25 kick" " \fIaxcb_addr\fP"
If there is unacknowledged data on the send queue of the specified AX.25
control block, this command forces an immediate retransmission.
The control block address can be found with the \fBax25 status\fP command.
.H 3 "ax25 maxframe" " [\fIcount\fP]"
Display or set the maximum number of frames that will be allowed to remain
unacknowledged at one time on AX.25 connections. This number cannot
be greater than 7.
Note that the maximum outstanding frame count only works with
virtual connections, UI frames are not affected.
The default is 7 frames.
.H 3 "ax25 mycall" " [\fIax25_addr\fP]"
Display or set the default local AX.25 address. The standard format is used,
(eg. KA9Q-0 or WB6RQN-5).
This command must be given before any \fBattach\fP commands
using AX.25 mode are given.
.H 3 "ax25 paclen" " [\fIsize\fP]"
Limit the size of I-fields on new AX.25 connections. If IP
datagrams or fragments larger than this are transmitted, they will be
transparently fragmented at the AX.25 level, sent as a series of I
frames, and reassembled back into a complete IP datagram or fragment at
the other end of the link. To have any effect on IP datagrams,
this parameter should be less than or equal to
the MTU of the associated interface.
The default is 256 bytes.
.H 3 "ax25 pthresh" " [\fIsize\fP]"
Display or set the poll threshold to be used for new AX.25 Version 2
connections. The poll threshold controls retransmission behavior as
follows. If the oldest unacknowledged I-frame size is less than the poll
threshold, it will be sent with the poll (P) bit set if a timeout occurs.
If the oldest unacked I-frame size is equal to or greater than the
threshold, then a RR or RNR frame, as appropriate, with the poll bit set
will be sent if a timeout occurs.
.P
The idea behind the poll threshold is that the extra time needed to send a
"small" I-frame instead of a supervisory frame when polling after a timeout
is small, and since there's a good chance the I-frame will have to be sent
anyway (i.e., if it were lost previously) then you might as well send it as
the poll. But if the I-frame is large, send a supervisory (RR/RNR) poll
instead to determine first if retransmitting the oldest unacknowledged
I-frame is necessary, the timeout might have been caused by a lost
acknowledgement. This is obviously a tradeoff, so experiment with the
poll threshold setting. The default is 64 bytes.
.H 3 "ax25 reset" " \fIaxcb_addr\fP"
Delete the AX.25 control block at the specified address.
The control block address can be found with the \fBax25 status\fP command.
.H 3 "ax25 retry" " [\fIcount\fP]"
Limit the number of successive unsuccessful transmission attempts on
new AX.25 connections. If this limit is exceeded, then the connection
is abandoned and all queued data is deleted.
A \fIcount\fP of 0 allows unlimited transmission attempts.
The default is 10 tries.
.H 3 "ax25 route" " [stat]"
Display the AX.25 routing table that
specifies the interface and digipeaters to be used in reaching a given station.
MORE TO BE WRITTEN.
.H 4 "ax25 route add" " [permanent] \fIinterface\fP default|\fIpath\fP"
Add an entry to the AX.25 routing table. An automatic \fBax25 route add\fP
is executed if digipeaters are specified in an AX.25 \fBconnect\fP
command, or if a connection is received from a remote station via
digipeaters. Such automatic routing table entries won't override locally
created entries, however. MORE TO BE WRITTEN.
.H 4 "ax25 route list" " [\fIax25_addr\fP ...]"
TO BE WRITTEN.
.H 3 "ax25 status" " [\fIaxcb_addr\fP]"
Without an argument, display a one-line summary of each AX.25 control block.
If the address of a particular control block is specified, the contents of
that control block are dumped in more detail.
.H 3 "ax25 t1" " [\fImilliseconds\fP]"
Display or set the AX.25 retransmission timer.
The default is 5000 milliseconds (5 seconds). MORE TO BE WRITTEN.
.H 3 "ax25 t3" " [\fImilliseconds\fP]"
Display or set the AX.25 idle "keep alive" timer.
The default is 900000 milliseconds (15 minutes).
.H 3 "ax25 t4" " [\fImilliseconds\fP]"
The default is 60000 milliseconds (1 minute).
MORE TO BE WRITTEN.
.H 3 "ax25 version" " [1|2]"
Display or set the version of the AX.25 protocol to attempt to use on
new connections. The default is 2 (the version
that does use the poll/final bits).
.H 3 "ax25 window" " [\fIsize\fP]"
Set the number of bytes that can be pending on an AX.25 receive queue
beyond which I frames will be answered with RNR (Receiver Not Ready)
responses. This presently applies only to suspended interactive AX.25
sessions, since incoming I-frames containing network (IP, NET/ROM) packets
are always processed immediately and are not placed on the receive queue.
However, when an AX.25 connection carries both interactive
and network packet traffic, an RNR generated because of
backlogged interactive traffic will also stop network
packet traffic from being sent.
The default is 2048 bytes.
.H 2 "axip" " \fIsubcommand\fP"
TO BE WRITTEN.
.H 3 "axip route"
TO BE WRITTEN.
.H 4 "axip route add" " \fIax25_addr\fP \fIhostid\fP"
TO BE WRITTEN.
.H 4 "axip route drop" " \fIax25_addr\fP"
TO BE WRITTEN.
.H 2 "bye"
TO BE WRITTEN.
.H 2 "close" " [\fIsession#\fP]"
Close the specified session, without an argument, close the
current session. On an AX.25 session, this command initiates a
disconnect. On a FTP or Telnet session, this command sends a
FIN (i.e., initiates a close) on the session's TCP connection.
This is an alternative to asking the remote server to initiate a
close (QUIT to FTP, or the logout command appropriate for the
remote system in the case of Telnet). When either FTP or Telnet
sees the incoming half of a TCP connection close, it
automatically responds by closing the outgoing half of the
connection. Close is more graceful than the \fBreset\fP command, in
that it is less likely to leave the remote TCP in a "half-open"
state.
.H 2 "connect" " \fIax25_addr\fP [\fIdigipeater\fP ...]"
Initiate a "vanilla" AX.25 session to the specified \fIax25_addr\fP.
Up to 8 optional digipeaters may be given, note that the word
\fBvia\fP is NOT needed.
Data sent on this session goes out in conventional AX.25 packets
with no upper layer protocol. The de-facto presentation
standard format is used, in that each packet holds one line of
text, terminated by a carriage return. A single AX.25
connection may be used for terminal-to-terminal, IP and NET/ROM
traffic, with the three types of data being automatically separated
by their AX.25 Level 3 Protocol IDs.
.H 2 "delete" " \fIfilename\fP ..."
Remove the specified files from the file system.
.H 2 "disconnect" " [\fIsession#\fP]"
An alias for the \fBclose\fP command (for the benefit of AX.25 users).
.H 2 "domain" " \fIsubcommand\fP"
These commands control the Domain Name Service.
.H 3 "domain cache list"
Show the current contents of the in-memory cache for resource
records.
.H 3 "domain cache flush"
Clear the in-memory cache for resource records.
This command is executed automatically every 24 hours to
remove old cache entries.
.H 3 "domain query" " \fIname|addr\fP"
Attempt to map a host name to an IP address or vice versa
using the built-in domain name server.
.H 3 "domain trace" " [on|off]"
Display or set the flag controlling the tracing of domain name server
requests and responses.
The default is \fBoff\fP.
.H 3 "domain usegethostby" " [on|off]"
Enable or disable the use of the UNIX functions gethostbyname()
and gethostbyaddr().
The default is \fBoff\fP.
MORE TO BE WRITTEN.
.H 2 "echo" " [accept|refuse]"
Display or set the flag controlling client Telnet's response to
a remote WILL ECHO offer.
The default is \fBaccept\fP.
.P
The Telnet presentation protocol specifies that in the absence
of a negotiated agreement to the contrary, neither end echoes
data received from the other. In this mode, a Telnet client
session echoes keyboard input locally and nothing is actually
sent until a carriage return is typed. Local line editing is
also performed: backspace deletes the last character typed,
while control-U deletes the entire line.
.P
When communicating from keyboard to keyboard the standard local
echo mode is used, so the setting of this parameter has no
effect. However, many timesharing systems (eg. UNIX) prefer to
do their own echoing of typed input. (This makes screen editors
work right, among other things). Such systems send a Telnet
WILL ECHO offer immediately upon receiving an incoming Telnet
connection request. If \fBecho accept\fP is in effect, a client
Telnet session will automatically return a DO ECHO response. In
this mode, local echoing and editing is turned off and each key
stroke is sent immediately (subject to the Nagle tinygram
algorithm in TCP). While this mode is just fine across an
Ethernet, it is clearly inefficient and painful across slow
paths like packet radio channels. Specifying \fBecho refuse\fP
causes an incoming WILL ECHO offer to be answered with a DONT
ECHO, the client Telnet session remains in the local echo mode.
Sessions already in the remote echo mode are unaffected. (Note:
Berkeley UNIX has a bug in that it will still echo input even
after the client has refused the WILL ECHO offer. To get around
this problem, enter the \fBstty -echo\fP command to the shell once
you have logged in.)
.H 2 "eol" " [standard|null]"
Display or set Telnet's end-of-line behavior when in remote echo mode.
In \fBstandard\fP mode, each key is sent as-is.
In \fBnull\fP mode, carriage returns are translated to line feeds.
This command is not necessary with all UNIX systems,
use it only when you find that a particular system responds to line feeds
but not carriage returns.
Only SunOS release 3.2 seems to exhibit this behavior, later releases are fixed.
The default is \fBstandard\fP.
.H 2 "escape" " [\fIchar\fP]"
Display or set the current command-mode escape character.
Control characters have to be prefixed by control-V.
The default is control-] (0x1d).
.H 2 "exit"
Exit (terminate) \fBWAMPES\fP.
.H 2 "finger" " [\fIuser\fP]@\fIhostid\fP"
Issue a network finger request for user \fIuser\fP at host \fIhostid\fP. If only
@\fIhostid\fP is given, all users on that host are identified.
.H 2 "fkey" " \fIkey#\fP \fItext\fP"
Set the value for a programmable key on the keyboard.
Control characters have to be prefixed by control-V.
\fIText\fP has to be enclosed in double quotes if it
contains white space.
.H 2 "ftp" " \fIhostid\fP"
Open a FTP control channel to the specified remote host and
enter converse mode on the new session. Responses from the
remote server are displayed directly on the screen. See the
\fBFTP Subcommands\fP chapter for descriptions of the commands available
in a FTP session.
.H 2 "hostname" " [\fIhostname\fP]"
Display or set the local host's name. By convention this should
be the same as the host's primary domain name. This string is
used only in the greeting messages of the various network
servers, and in the command line prompt.
Note that \fBhostname\fP does NOT set the system's IP address.
.P
If \fIhostname\fP is the same as the name of an interface,
\fIhostname\fP will be substituted by the canonical host name
which corresponds to the IP address of that interface.
.H 2 "icmp" " \fIsubcommand\fP"
These commands control the Internet Control Message Protocol service.
.H 3 "icmp echo" " [on|off]"
Display or set the flag controlling the asynchronous display of
ICMP Echo Reply packets. This flag must be \fBon\fP for one-shot
pings to work (see the \fBping\fP command). The default is \fBon\fP.
.H 3 "icmp status"
Display statistics about the Internet Control Message Protocol
(ICMP), including the number of ICMP messages of each type sent
or received.
.H 3 "icmp trace" " [on|off]"
Display or set the flag controlling the display of ICMP error
messages. These informational messages are generated by
routers in response to routing, protocol or congestion
problems.
The default is \fBoff\fP.
.H 2 "ifconfig" " [\fIinterface\fP [[\fIsubcommand\fP \fIparameter\fP] ...]"
Without arguments display the status of all interfaces.
When only \fIinterface\fP is given, the status of that interface is displayed.
Multiple subcommand/parameter pairs can be put on one line.
.H 3 "ifconfig \fIinterface\fP broadcast" " \fIaddr\fP"
Set the broadcast address of \fIinterface\fP to \fIaddr\fP.
This is related to the \fBnetmask\fP subcommand.
See also the \fBarp\fP command.
.H 3 "ifconfig \fIinterface\fP encapsulation" " \fIencapsulation\fP"
Set the encapsulation for \fIinterface\fP to \fIencapsulation\fP.
\fIEncapsulation\fP may be one of
\fBnone\fP,
\fBax25ui\fP,
\fBax25i\fP,
\fBkissui\fP,
\fBkissi\fP,
\fBslip\fP,
\fBvjslip\fP,
\fBnetrom\fP,
or
\fBnrs\fP.
MORE TO BE WRITTEN.
.H 3 "ifconfig \fIinterface\fP forward" " \fIinterface-2\fP"
When a forward is defined, all output for \fIinterface\fP is redirected to
\fIinterface-2\fP. To remove the forward, set \fIinterface-2\fP to \fIinterface\fP.
.H 3 "ifconfig \fIinterface\fP ipaddress" " \fIaddr\fP"
Set the IP address to \fIaddr\fP for this interface. This might be necessary
when a system acts as a gateway.
See also the \fBhostname\fP and \fBip address\fP commands.
.H 3 "ifconfig \fIinterface\fP linkaddress" " \fIhardware-dependent\fP"
Set the hardware dependent address for this interface. For
AX.25 this can be the callsign, for ethernet an ethernet
address.
.H 3 "ifconfig \fIinterface\fP mtu" " \fIparameter\fP"
Set the maximum transfer unit to \fIparameter\fP octeds (bytes).
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
.H 3 "ifconfig \fIinterface\fP netmask" " \fIaddress\fP"
Set the subnet mask for this interface.
The \fIaddress\fP takes the form of an IP address
with 1's in the network and subnet parts of the address,
and 0's in the host part of the address.
Sample:
.DS I
.ft CW
ifconfig ec0 netmask 0xffffff00
.ft P
.DE
for a class C network (24 bits).
This is related to the \fBbroadcast\fP subcommand.
See also the \fBroute\fP command.
.H 3 "ifconfig \fIinterface\fP rxbuf" " \fIsize\fP"
Set the receive buffer size.
This value is currently not used by \fBWAMPES\fP.
.H 3 "ifconfig \fIinterface\fP txqlen" " \fIsize\fP"
TO BE WRITTEN.
.H 2 "ip" " \fIsubcommand\fP"
These commands control the Internet Protocol service.
.H 3 "ip address" " [\fIhostid\fP]"
Display or set the default local IP address. This command must be given before
an \fBattach\fP command if it is to be used as the default IP address for
that interface.
.H 3 "ip rtimer" " [\fIseconds\fP]"
Display or set the IP reassembly timeout. The default is 30 seconds.
.H 3 "ip status"
Display Internet Protocol (IP) statistics, such as total packet counts
and error counters of various types.
.H 3 "ip ttl" " [\fIhops\fP]"
Display or set the default time-to-live value placed in each outgoing IP
datagram. This limits the number of switch hops the datagram will be allowed to
take. The idea is to bound the lifetime of the packet should it become caught
in a routing loop, so make the value slightly larger than the number of
hops across the network you expect to transit packets.
The default is 255 hops.
.H 2 "ipfilter" " [\fIsubcommand\fP]"
Without an argument, display the IP filter table.
If the IP filter table is empty, \fBWAMPES\fP will allow IP packets
to be sent to any destination.
If the IP filter table contains at least one entry,
\fBWAMPES\fP will allow IP packets to be sent only to destinations listed
in the IP filter table, IP packets to other destinations will be
discarded.
.H 3 "ipfilter add" " \fIhostid\fP[/\fIbits\fP]"
This command adds an entry to the IP filter table.
The optional /\fIbits\fP suffix to \fIhostid\fP specifies how
many leading bits in \fIhostid\fP are to be considered significant.
If not specified, 32 bits (i.e., full significance) is
assumed. With this option, a single IP filter table entry may refer to
many hosts all sharing a common bit string prefix in their IP addresses.
For example, ARPA Class A, B and C networks would use suffixes of /8,
/16 and /24 respectively. The command
.DS I
.ft CW
ipfilter add 44/8
.ft P
.DE
causes any IP addresses beginning with "44" in the first 8 bits to be
allowed, the remaining 24 bits are "don't-cares".
.H 3 "ipfilter drop" " \fIhostid\fP[/\fIbits\fP]"
Remove the specified entry from the IP filter table.
.H 2 "kick" " [\fIsession#\fP]"
Kick all sockets associated with a session.
If no argument is given, kick the current session.
Performs the same function as the \fBax25 kick\fP,
\fBnetrom kick\fP, and \fBtcp kick\fP commands,
but is easier to type.
.H 2 "log" " [stop|\fIfilename\fP]"
Display the current log filename or set the filename for logging
server sessions. If \fBstop\fP is given as the argument,
logging is terminated (the servers themselves are unaffected).
If a file name is given as an argument, server session log
entries will be appended to it.
.H 2 "login" " \fIsubcommand\fP"
TO BE WRITTEN.
.H 3 "login auto" " [on|off]"
Enable or disable automatic login of users. If automatic login
is enabled the user name is derived from the incoming AX.25
call, the NET/ROM user name, or the IP host name. If automatic
login is disabled the user has to supply the user name at the
login: prompt.
The default is \fBon\fP.
.H 3 "login create" " [on|off]"
Enable or disable automatic creation of user accounts (entries
in the system passwd file and home directories) if automatic
login is enabled.
The default is \fBon\fP.
.H 3 "login defaultuser" " [\fIusername\fP]"
Display or set the user name to be used for login if the
incoming AX.25 call, the NET/ROM user name, or the IP host name
cannot be mapped to a valid user name. If \fIusername\fP is set to
"" this mapping is disabled.
The default is guest.
.H 3 "login gid" " [\fIgid\fP]"
Display or set the group id to be used when creating new user
accounts (see \fBlogin create\fP).
The default is 400.
.H 3 "login homedir" " [\fIhomedir\fP]"
Display or set the base directory to be used for user home
directories when creating new user accounts (see
\fBlogin create\fP).
.P
To avoid huge directories the actual home directory is created
in a subdirectory:
.DS I
\fIhomedir\fP/\fIfirst 3 characters of user name\fP.../\fIuser name\fP
.DE
Example:
.P
If \fBlogin homedir\fP is set to /home/radio the home directory for
n0pra will be:
.DS I
/home/radio/n0p.../n0pra
.DE
The default is HOME_DIR/funk.
.P
HOME_DIR is defined in /tcp/lib/configure.h and has a value of
.DS I
/home
/u
/users
/usr/people
.DE
depending on the system configuration.
.H 3 "login logfiledir" " [\fIlogfiledir\fP]"
Display or set the directory to be used for user log files. If
set to "" no logging is done.
The default is "".
.H 3 "login maxuid" " [\fImaxuid\fP]"
Display or set the maximum user id to be used when creating new
user accounts (see \fBlogin create\fP).
\fImaxuid\fP must be between 1 and 32000.
The default is 32000.
.H 3 "login minuid" " [\fIminuid\fP]"
Display or set the minimum user id to be used when creating new
user accounts (see \fBlogin create\fP).
\fIminuid\fP must be between 1 and 32000.
The default is 400.
.H 3 "login shell" " [\fIshell\fP]"
Display or set the login shell to be used when creating new user
accounts (see \fBlogin create\fP). If set to "" UNIX will
choose its own default shell.
The default is "".
.H 2 "memory" " \fIsubcommand\fP"
These commands control memory allocation.
.H 3 "memory debug" " [on|off]"
Enable or disable heap debugging. If heap debugging is turned on, all
memory blocks returned by malloc will be filled with the value 0xdd, and
all memory blocks freed will be filled with the value 0xbb. This makes
it easier to track uninitialized and freed memory reads.
The default is \fBoff\fP.
.H 3 "memory freelist"
Display the number of all currently free memory blocks. Different block
sizes (rounded up to the next power of two) are counted separately.
.H 3 "memory merge" " [on|off]"
Enable or disable the attempt to merge adjacent free memory blocks into
bigger blocks. Since merging is CPU intensive in the current
implementation, and since the probability is high that a merged block
will have to be split again soon, this option is currently not
recommended. The default is \fBoff\fP.
.H 3 "memory sizes"
Display the total number (counted since program startup) of all memory
allocation requests. Requests for different block sizes (rounded up to
the next power of two) are counted separately.
.H 3 "memory status"
Display a summary of storage allocator statistics. The first line
shows the total size of the heap, the amount of heap
memory available in bytes and as a percentage of the total heap size,
and the number of sbrk() system calls used to increase the heap size.
.P
The second line shows the total number of calls to allocate and free blocks
of memory, the difference of these two values (i.e., the number of allocated
blocks outstanding), the number of allocation requests that were denied
due to lack of memory, and the number of calls to free() that attempted to
free garbage
(eg. by freeing the same block twice or freeing a garbled pointer).
.P
The third line shows the total number of splits
(breaking a big heap block into two small blocks)
and merges
(combining two adjacent small blocks into one big block),
and the difference of these two values.
.P
The fourth line shows the number of calls to pushdown, and the number of calls to pushdown which resulted in a call to malloc.
.H 2 "mkdir" " \fIdirname\fP"
Create the specified directory.
.H 2 "netrom" " \fIsubcommand\fP"
These commands control the NET/ROM service.
.H 3 "netrom broadcast" " [\fIinterface\fP \fIax25_addr\fP \fIdigipeater\fP ...]"
TO BE WRITTEN.
.H 3 "netrom connect" " \fInode\fP [\fIuser\fP]"
TO BE WRITTEN.
.H 3 "netrom ident" " [\fIident\fP]"
TO BE WRITTEN.
.H 3 "netrom kick" " \fInrcb_addr\fP"
If there is unacknowledged data on the send queue of the specified NET/ROM
control block, this command forces an immediate retransmission.
The control block address can be found with the \fBnetrom status\fP command.
.H 3 "netrom links" " [\fInode\fP [\fInode2\fP \fIquality\fP [permanent]]]"
TO BE WRITTEN.
.H 3 "netrom nodes" " [\fInode\fP]"
TO BE WRITTEN.
.H 3 "netrom parms" " [\fIparm#\fP [\fIvalue\fP ...]]"
TO BE WRITTEN.
.H 3 "netrom reset" " \fInrcb_addr\fP"
Delete the NET/ROM control block at the specified address.
The control block address can be found with the \fBnetrom status\fP command.
.H 3 "netrom status" " [\fInrcb_addr\fP]"
Without an argument, display a one-line summary of each NET/ROM control block.
If the address of a particular control block is specified, the contents of
that control block are dumped in more detail.
.H 2 "nrstat"
Displays the netrom interface statistics.
.H 2 "param" " \fIinterface\fP [\fIparameter-name\fP [\fIparameter-value\fP]]"
Invoke a device-specific control routine.
The following parameter names are recognized by the \fBparam\fP command,
but not all are supported by each device type.
Most commands deal only with half-duplex packet radio interfaces.
.DS I
.TS
center box tab(;) ;
cB | cB
l | l.
Name;Meaning
_
Data;
TxDelay;transmit keyup delay
Persist;P-persistence setting
SlotTime;persistence slot time setting
TxTail;transmit done holdup delay
FullDup;enable/disable full duplex
Hardware;hardware specific command
TxMute;experimental transmit mute command
DTR;control Data Terminal Ready (DTR) signal to modem
RTS;control Request to Send (RTS) signal to modem
Speed;set line speed
EndDelay;
Group;
Idle;
Min;
MaxKey;
Wait;
Down;drop modem control lines
Up;raise modem control lines
Blind;
Return;return a KISS TNC to command mode
.TE
.DE
Depending on the interface type, some parameters can be read back by
omitting a new value. This is not possible with KISS TNCs as
there are no KISS commands for reading back previously sent
parameters.
.P
On a KISS TNC interface, the \fBparam\fP command generates and sends
control packets to the TNC. Data bytes are treated as decimal.
For example,
.DS I
.ft CW
param ax0 txdelay 255
.ft P
.DE
will set the keyup timer
(type field = 1) on the KISS TNC configured as ax0 to 2.55
seconds (255 x .01 sec). On all asy interfaces (slip, kiss/ax25,
nrs) the \fBparam\fP \fIinterface\fP \fBspeed\fP command allows the baud rate to
be read or set.
.P
The implementation of this command for the various interface
drivers is incomplete and subject to change.
.H 2 "ping" " [\fIsubcommand\fP]"
TO BE WRITTEN.
.H 3 "ping clear"
TO BE WRITTEN.
.H 3 "ping \fIhostid\fP" " [\fIpacketsize\fP [\fIinterval\fP]]"
Ping (send ICMP ECHO_REQUEST packets to) the specified host.
ECHO_REQUEST datagrams have an IP and ICMP header,
followed by a \fBstruct timeval\fP,
and an arbitrary number of pad bytes used to fill out the packet.
Default datagram length is 64 bytes,
but this can be changed by specifying \fIpacketsize\fP.
The minimum value allowed for \fIpacketsize\fP is 8 bytes.
If \fIpacketsize\fP is smaller than 16 bytes,
there is not enough room for timing information.
In this case the round-trip times are not displayed.
.P
If \fIinterval\fP is specified, pings will be repeated indefinitely
at the specified interval (in seconds),
otherwise a single, "one shot" ping is done.
Responses to one-shot pings appear asynchronously on the command screen.
.H 2 "ps"
Display all current processes in the system. The fields are as follows:
.P
\fBPID\fP - Process ID (the address of the process control block).
.P
\fBSP\fP - The current value of the process stack pointer.
.P
\fBstksize\fP - The size of the stack allocated to the process.
.P
\fBmaxstk\fP - The apparent peak stack utilization of this process. This is
done in a somewhat heuristic fashion, so the numbers should be treated
as approximate. If this number reaches or exceeds the stksize figure,
the system is almost certain to crash,
and \fBWAMPES\fP should be recompiled
to give the process a larger allocation when it is started.
.P
\fBevent\fP - The event this task is waiting for, if it is not runnable.
.P
\fBfl\fP - Process status flags. There are two:
W (Waiting for event) and S (suspended - not currently used).
The W flag indicates that the process is waiting for an event, the \fBevent\fP
column will be non-zero. Note that although there may be several
runnable processes at any time (shown in the \fBps\fP listing as those
without the W flag and with zero event fields) only one
process is actually running at any one instant.
.H 2 "record" " [off|\fIfilename\fP]"
Append to \fIfilename\fP all data received on the current
session. Data sent on the current session is also written into the file
except for Telnet sessions in remote echo mode. The command \fBrecord off\fP
stops recording and closes the file. If no
argument is specified the current status is displayed.
.H 2 "remote" " [-p \fIport\fP] [-k \fIkey\fP] [-a \fIkickaddr\fP] \fIhostid\fP exit|reset|kick"
Send a UDP packet to the specified host commanding it
to exit the \fBWAMPES\fP or \fBNOS\fP program, reset the processor,
or force a retransmission on TCP connections. For this
command to be accepted, the remote system must be running the \fBremote\fP
server and the port number specified in the \fBremote\fP command must match
the port number given when the server was started on the remote system.
If the port numbers do not match, or if the remote server is not running
on the target system, the command packet is ignored. Even if the
command is accepted there is no acknowledgement.
.P
The \fBkick\fP command forces a retransmission timeout on all
TCP connections that the remote node may have with the local node.
If the \fB-a\fP option is used, connections to the specified host are
kicked instead. No key is required for the \fBkick\fP subcommand.
.P
The \fBexit\fP subcommand is mainly useful for
restarting the \fBWAMPES\fP program on a remote
unattended system after a configuration file has been updated. The
remote system should invoke the \fBWAMPES\fP program automatically upon booting,
preferably from \fB/etc/inittab\fP.
For example:
.DS I
.ft CW
net :23456:respawn:env TZ=MST7MDT /tcp/net
.ft P
.DE
.H 3 "remote -s" " \fIkey\fP"
The \fBexit\fP and \fBreset\fP subcommands of \fBremote\fP require a password.
The password is set
on a given system with the \fB-s\fP option, and it is specified in a command
to a remote system with the \fB-k\fP option. If no password is set with the
\fB-s\fP option, then the \fBexit\fP and \fBreset\fP subcommands are disabled.
.H 2 "rename" " \fIoldfilename\fP \fInewfilename\fP"
Rename \fIoldfilename\fP to \fInewfilename\fP.
.H 2 "repeat" " \fImilliseconds\fP \fIcommand\fP [\fIarguments\fP ...]"
TO BE WRITTEN.
.H 2 "reset" " [\fIsession#\fP]"
Reset the specified session, if no argument is given, reset the current
session. This command should be used with caution since it does not
reliably inform the remote end that the connection no longer exists. (In
TCP a reset (RST) message will be automatically generated should the remote
TCP send anything after a local \fBreset\fP has been done. In AX.25 the DM
message performs a similar role. Both are used to get rid of a lingering
half-open connection after a remote system has crashed.)
.H 2 "rip" " \fIsubcommand\fP"
These commands control the Routing Information Protocol (RIP) service.
.H 3 "rip accept" " \fIgateway\fP"
Remove the specified gateway from the RIP filter table, allowing future
broadcasts from that gateway to be accepted.
.H 3 "rip add" " \fIhostid\fP \fIseconds\fP [\fIflags\fP]"
Add an entry to the RIP broadcast table. The IP routing table will be sent
to \fIhostid\fP every interval of \fIseconds\fP. If
\fIflags\fP is specified as 1, then "split horizon" processing will
be performed
for this destination. That is, any IP routing table entries pointing to the
interface that will be used to send this update will be removed from the
update. If split horizon processing is not specified, then all routing
table entries except those marked "private" will be sent in each update.
(Private entries are never sent in RIP packets).
.P
Triggered updates are always done. That is, any change in the routing table
that causes a previously reachable destination to become unreachable will
trigger an update that advertises the destination with metric 15, defined to
mean "infinity".
.P
Note that for RIP packets to be sent properly to a broadcast address, there
must exist correct IP routing and ARP table entries that will first steer
the broadcast to the correct interface and then place the correct link-level
broadcast address in the link-level destination field. If a standard IP
broadcast address convention is used (eg. 128.96.0.0 or 128.96.255.255)
then chances are you already have the necessary IP routing table entry, but
unusual subnet or cluster-addressed networks may require special attention.
However, an \fBarp add\fP command will be required to translate this address to
the appropriate link level broadcast address. For example,
.DS I
.ft CW
arp add 128.96.0.0 ethernet ff:ff:ff:ff:ff:ff
.ft P
.DE
for an Ethernet network, and
.DS I
.ft CW
arp add 44.255.255.255 ax25 qst-0
.ft P
.DE
for an AX25 packet radio channel.
.H 3 "rip drop" " \fIhostid\fP"
Remove an entry from the RIP broadcast table.
.H 3 "rip merge" " [on|off]"
This flag controls an experimental feature for consolidating redundant
entries in the IP routing table. When rip merging is enabled, the table is
scanned after processing each RIP update. An entry is considered redundant
if the target(s) it covers would be routed identically by a less "specific"
entry already in the table. That is, the target address(es) specified
by the entry in question must also match the target addresses of the
less specific entry and the two entries must have the same interface
and gateway fields. For example, if the routing table contains
.DS I
.TS
tab(;) ;
lB lB lB lB lB lB lB lB
l l l l l l l l.
Dest;Len;Interface;Gateway;Metric;P;Timer;Use
1.2.3.4;32;ethernet0;128.96.1.2;1;0;0;0
1.2.3;24;ethernet0;128.96.1.2;1;0;0;0
.TE
.DE
then the first entry would be deleted as redundant since packets sent to
1.2.3.4 will still be routed correctly by the second entry. Note that the
relative metrics of the entries are ignored.
The default is \fBoff\fP.
.H 3 "rip refuse" " \fIgateway\fP"
Refuse to accept RIP updates from the specified gateway by adding the
gateway to the RIP filter table. It may be later removed with the \fBrip
accept\fP command.
.H 3 "rip request" " \fIgateway\fP"
Send a RIP Request packet to the specified gateway, causing it to reply
with a RIP Response packet containing its routing table.
.H 3 "rip status"
Display RIP status, including a count of the number of packets sent
and received, the number of requests and responses, the number of
unknown RIP packet types, and the number of refused RIP updates from hosts
in the filter table. A list of the addresses and intervals
to which periodic RIP updates are being sent is also shown, along with
the contents of the filter table.
.H 3 "rip trace" " [0|1|2]"
This variable controls the tracing of incoming and outgoing RIP packets.
Setting it to 0 disables all RIP tracing. A value of 1 causes changes
in the routing table to be displayed, while packets that cause no changes
cause no output. Setting the variable to 2 produces maximum output,
including tracing of RIP packets that cause no change in the routing table.
The default is 0.
.H 2 "rmdir" " \fIdirname\fP"
Remove the specified directory.
.H 2 "route" " [\fIsubcommand\fP]"
Without an argument, display the IP routing table.
.H 3 "route add" " \fIhostid\fP[/\fIbits\fP]|default \fIinterface\fP [\fIgateway_hostid\fP [\fImetric\fP]]"
Add a permanent entry to the IP routing table.
It will not time out as will an automatically created entry,
but must be removed with the \fBroute drop\fP command.
\fBroute add\fP requires at least two more arguments,
the \fIhostid\fP of the destination and the name of
the \fIinterface\fP to which its packets should be sent. If the destination is
not local, the gateway's hostid should also be specified. (If the interface
is a point-to-point link, then \fIgateway_hostid\fP may be omitted even if the
target is non-local because this field is only used to determine the
gateway's link level address, if any. If the destination is directly
reachable, \fIgateway_hostid\fP is also unnecessary since the destination
address is used to determine the link address).
.P
The optional /\fIbits\fP suffix to the destination \fIhostid\fP specifies how
many leading bits in \fIhostid\fP are to be considered significant in
routing comparisons. If not specified, 32 bits (i.e., full significance) is
assumed. With this option, a single IP routing table entry may refer to
many hosts all sharing a common bit string prefix in their IP addresses.
For example, ARPA Class A, B and C networks would use suffixes of /8,
/16 and /24 respectively. The command
.DS I
.ft CW
route add 44/8 sl0 44.64.0.2
.ft P
.DE
causes any IP addresses beginning with "44" in the first 8 bits to be
routed to 44.64.0.2, the remaining 24 bits are "don't-cares".
.P
When an IP address to be routed matches more than one entry in the IP routing
table, the entry with the largest \fIbits\fP parameter (i.e., the "best" match)
is used. This allows individual hosts or blocks of hosts to be exceptions
to a more general rule for a larger block of hosts.
.P
The special destination \fBdefault\fP is used to route datagrams to
addresses not matched by any other entry
in the IP routing table, it is equivalent to specifying a
/\fIbits\fP suffix of /0 to any destination \fIhostid\fP.
Care must be taken with
default entries since two nodes with default entries pointing at each
other will route packets to unknown addresses back and forth in a loop
until their time-to-live (TTL) fields expire. (Routing loops for
specific addresses can also be created, but this is less likely to occur
accidentally).
.P
There are two built-in interfaces: \fBloopback\fP and \fBencap\fP.
\fBLoopback\fP is for internal purposes only. \fBEncap\fP is an IP
encapsulator interface. It is used to encapsulate a complete IP datagram
into an IP datagram so that it gets "piggy-backed".
.P
Here are some examples of the \fBroute\fP command:
.DS I
.ft CW
# Route datagrams to IP address 44.0.0.3 to SLIP line #0.
# No gateway is needed because SLIP is point-to point.
route add 44.0.0.3 sl0
.ft P
.DE
.DS I
.ft CW
# Route all default traffic to the gateway on the local Ethernet
# with IP address 44.0.0.1
route add default ec0 44.0.0.1
.ft P
.DE
.DS I
.ft CW
# The local Ethernet has an ARPA Class-C address assignment.
# Route all IP addresses beginning with 192.4.8 to it
route add 192.4.8/24 ec0
.ft P
.DE
.DS I
.ft CW
# The station with IP address 44.0.0.10 is on the local AX.25 channel
route add 44.0.0.10 ax0
.ft P
.DE
.H 3 "route addprivate" " \fIhostid\fP[/\fIbits\fP]|default \fIinterface\fP [\fIgateway_hostid\fP [\fImetric\fP]]"
This command is identical to \fBroute add\fP except that it also marks the new
entry as private, it will never be included in outgoing RIP updates.
.H 3 "route drop" " \fIhostid\fP[/\fIbits\fP]|default"
Remove the specified entry from the IP routing table.
If a packet arrives for the deleted address
and a \fBdefault\fP route is in effect, it will be used.
.H 3 "route flush"
Drop all automatically created entries from the IP routing table, permanent
entries are not affected.
.H 3 "route lookup" " \fIhostid\fP"
TO BE WRITTEN.
.H 2 "session" " [\fIsession#\fP]"
Without arguments, display the list of current sessions, including
session number, remote TCP or AX.25 address and the address of the TCP or
AX.25 control block. An asterisk (*) is shown next to the current
session. Entering a session number as an argument to the \fBsession\fP
command will put you in converse mode with that session.
.H 2 "shell" " \fIshell_command_line\fP"
Suspend \fBWAMPES\fP and execute a subshell (/bin/sh).
When the subshell exits, \fBWAMPES\fP resumes.
Background activity (FTP servers, etc) is also suspended
while the subshell executes.
.H 2 "smtp" " \fIsubcommand\fP"
These commands control the Simple Message Transport Protocol service
(and all other mail delivery clients).
.H 3 "smtp kick" " [\fIdestination\fP]"
Run through the outgoing mail queue and attempt to deliver any pending mail.
If \fIdestination\fP is specified try only to deliver mail to this system.
This command allows the user to "kick" the mail system manually.
Normally, this command is periodically invoked by a timer
whenever \fBWAMPES\fP is running (see \fBsmtp timer\fP). MORE TO BE WRITTEN.
.H 3 "smtp list"
List the current jobs. MORE TO BE WRITTEN.
.H 3 "smtp maxclients" " [\fIcount\fP]"
Display or set the maximum number of simultaneous outgoing mail sessions
that will be allowed. The default is 10, reduce it if network congestion
is a problem.
.H 3 "smtp timer" " [\fIseconds\fP]"
Display or set the interval between scans of the outbound
mail queue. For example, \fBsmtp timer 600\fP will cause the system to check
for outgoing mail every 10 minutes and attempt to deliver anything it finds,
subject of course to the \fBsmtp maxclients\fP limit. Setting a value of zero
disables
queue scanning altogether, note that this is the default!
.H 2 "sntp" " \fIsubcommand\fP"
TO BE WRITTEN.
.H 3 "sntp add" " \fIhostid\fP [\fIinterval\fP]"
Add \fIhostid\fP to the list of sntp servers.
\fIinterval\fP specifies the poll interval in seconds (default 3333).
.H 3 "sntp drop" " \fIhostid\fP"
Remove \fIhostid\fP from the list of sntp servers.
.H 3 "sntp status"
List the currently configured sntp servers, along with
statistics on how many polls and replies have been exchanged
with each one, response times, etc.
.H 3 "sntp step_threshold" " [\fIstep_threshold\fP]"
Display or set the step threshold (measured in seconds). If the
required time adjustment is less than the step threshold, the
adjtime() system call will be used to slow down or to accelerate
the clock, otherwise the clock will be stepped using
settimeofday().
The default is 1 second.
.H 3 "sntp sys"
Display the sntp server configuration.
.H 4 "sntp sys leap" " [\fIleap\fP]"
Display or set the sntp server leap indicator. This is a
two-bit code warning of an impending leap second to be
inserted/deleted in the last minute of the current day, coded as
follows:
.DS
.TS
center box tab(;) ;
cB | cB
l | l.
Value;Meaning
_
0;no warning
1;last minute has 61 seconds
2;last minute has 59 seconds)
3;alarm condition (clock not synchronized)
.TE
.DE
The default is 0.
.H 4 "sntp sys precision" " [\fIprecision\fP]"
Display or set the sntp server precision. This is an eight-bit
signed integer indicating the precision of the local clock, in
seconds to the nearest power of two. The values that normally
appear in this field range from -6 for mains-frequency clocks to
-18 for microsecond clocks found in some workstations.
The default is -10.
.H 4 "sntp sys refid" " [\fIrefid\fP]"
Display or set the sntp server reference identifier. This is
identifying the particular reference clock. In the case of
stratum 0 (unspecified) or stratum 1 (primary reference), this
is a string of up to 4 characters. While not enumerated as part
of the NTP specification, the following are representative ASCII
identifiers:
.DS
.TS
center box tab(;) ;
cB | cB | cB
l | l | l.
Stratum;Code;Meaning
_
0;ascii;generic time service other than NTP, such as
;;ACTS (Automated Computer Time Service),
;;TIME (UDP/Time Protocol),
;;TSP (TSP UNIX time protocol),
;;DTSS (Digital Time Synchronization Service), etc.
1;ATOM;calibrated atomic clock
1;VLF;VLF radio (OMEGA, etc.)
1;callsign;Generic radio
1;LORC;LORAN-C radionavigation system
1;GOES;Geostationary Operational Environmental Satellite
1;GPS;Global Positioning Service
2;address;secondary reference (four-octet Internet address
;;of the NTP server)
.TE
.DE
The default is UNIX.
.H 4 "sntp sys reftime"
Display the sntp server reference time.
.H 4 "sntp sys rootdelay" " [\fIrootdelay\fP]"
Display or set the sntp server root delay. This is a floating
point number indicating the total roundtrip delay to the primary
reference source, in seconds. Note that this variable can take
on both positive and negative values, depending on the relative
time and frequency errors. The values that normally appear in
this field range from negative values of a few milliseconds to
positive values of several hundred milliseconds.
The default is 0.
.H 4 "sntp sys rootdispersion" " [\fIrootdispersion\fP]"
Display or set the sntp server root dispersion. This is a
floating point number indicating the maximum error relative to
the primary reference source, in seconds. The values that
normally appear in this field range from zero to several hundred
milliseconds.
The default is 0.
.H 4 "sntp sys stratum" " [\fIstratum\fP]"
Display or set the sntp server stratum. This is a eight-bit
integer indicating the stratum level of the local clock, with
values defined as follows:
.DS
.TS
center box tab(;) ;
cB | cB
l | l.
Stratum;Meaning
_
0;unspecified or unavailable
1;primary reference (e.g., radio clock)
2-15;secondary reference (via NTP or SNTP)
16-255;reserved
.TE
.DE
The default is 1.
.H 3 "sntp trace" " [on|off]"
Enable or disable sntp tracing.
The default is \fBoff\fP.
.H 2 "source" " \fIfilename\fP"
Read subsequent commands from \fIfilename\fP until EOF, then resume reading
commands from the previous stream. This can be used to keep routing statements
in a separate file, which can be read at some point in \fBnet.rc\fP.
.H 2 "start" " \fIserver\fP [\fIarguments\fP]"
Start the specified server, allowing remote connection requests.
.H 3 "start ax25"
Start the AX.25 Login server.
MORE TO BE WRITTEN.
.H 3 "start discard" " [\fIport\fP]"
Start the TCP DISCARD server.
The default \fIport\fP is 9.
MORE TO BE WRITTEN.
.H 3 "start domain" " [\fIport\fP]"
Start the TCP/UDP DOMAIN server.
The default \fIport\fP is 53.
MORE TO BE WRITTEN.
.H 3 "start echo" " [\fIport\fP]"
Start the TCP ECHO server.
The default \fIport\fP is 7.
MORE TO BE WRITTEN.
.H 3 "start ftp" " [\fIport\fP]"
Start the TCP FTP server.
The default \fIport\fP is 21.
MORE TO BE WRITTEN.
.H 3 "start tcpgate" " \fIport\fP [\fIhost\fP[:\fIservice\fP]]"
TO BE WRITTEN.
.H 3 "start netrom"
Start the NET/ROM Login server.
MORE TO BE WRITTEN.
.H 3 "start rip"
Start the UDP RIP server on port 520.
MORE TO BE WRITTEN.
.H 3 "start sntp"
Start the UDP SNTP server on port 123.
MORE TO BE WRITTEN.
.H 3 "start telnet" " [\fIport\fP]"
Start the TCP TELNET server.
The default \fIport\fP is 23.
MORE TO BE WRITTEN.
.H 3 "start time"
Start the UDP TIME server on port 37.
MORE TO BE WRITTEN.
.H 3 "start remote" " [\fIport\fP]"
Start the UDP REMOTE server.
The default \fIport\fP is 1234.
MORE TO BE WRITTEN.
.H 2 "status"
TO BE WRITTEN.
.H 2 "stop" " \fIserver\fP"
Stop the specified server, rejecting any further remote connect
requests. Existing connections are allowed to complete normally.
.H 3 "stop ax25"
TO BE WRITTEN.
.H 3 "stop discard"
TO BE WRITTEN.
.H 3 "stop domain"
TO BE WRITTEN.
.H 3 "stop echo"
TO BE WRITTEN.
.H 3 "stop ftp"
TO BE WRITTEN.
.H 3 "stop netrom"
TO BE WRITTEN.
.H 3 "stop rip"
TO BE WRITTEN.
.H 3 "stop sntp"
TO BE WRITTEN.
.H 3 "stop telnet"
TO BE WRITTEN.
.H 3 "stop time"
TO BE WRITTEN.
.H 3 "stop remote"
TO BE WRITTEN.
.H 2 "tcp" " \fIsubcommand\fP"
These commands control the Transmission Control Protocol service.
.H 3 "tcp irtt" " [\fImilliseconds\fP]"
Display or set the initial round trip time estimate, in milliseconds, to be
used for new TCP connections until they can measure and adapt to the
actual value. The default is 5000 milliseconds (5 seconds).
Increasing this when operating
over slow channels will avoid the flurry of retransmissions that would
otherwise occur as the smoothed estimate settles down at the correct
value. Note that this command should be given before servers are started in
order for it to have effect on incoming connections.
.P
TCP also keeps a cache of measured smoothed round trip times (SRTT) and mean
deviations (MDEV) for current and recent destinations. Whenever a new
TCP connection is opened, the system first looks in this cache. If the
destination is found, the cached SRTT and MDEV values are used. If not,
the default IRTT value mentioned above is used, along with a MDEV of 0.
This feature is fully automatic, and it can improve performance greatly
when a series of connections are opened and closed to a given destination
(eg. a series of FTP file transfers or directory listings).
.H 3 "tcp irtt \fIhostid\fP" " \fImilliseconds\fP"
Update the SRTT/MDEV cache entry for \fIhostid\fP by simulating a RTT
measurement of \fImilliseconds\fP. If there was no previous cache entry
for \fIhostid\fP, this will result in a new entry having a SRTT of
\fImilliseconds\fP and a MDEV of 0. This command is most useful when
executed from a startup file such as \fBnet.rc\fP.
.H 3 "tcp kick" " \fItcb_addr\fP"
If there is unacknowledged data on the send queue of the specified TCP
control block, this command forces an immediate retransmission.
The control block address can be found with the \fBtcp status\fP command.
.H 3 "tcp mss" " [\fIsize\fP]"
Display or set the TCP Maximum Segment Size in bytes that will be sent on all
outgoing TCP connect request (SYN segments). This tells the remote end the
size of the largest segment (packet) it may send. Changing MSS affects
only future connections, existing connections are unaffected.
The default is 512 bytes.
.H 3 "tcp reset" " \fItcb_addr\fP"
Delete the TCP control block at the specified address.
The control block address can be found with the \fBtcp status\fP command.
.H 3 "tcp rtt" " \fItcb_addr\fP \fImilliseconds\fP"
Replaces the automatically computed round trip time in the specified TCB
with the rtt in milliseconds. This command is useful to speed up
recovery from a series of lost packets since it provides a manual bypass
around the normal backoff retransmission timing mechanisms.
.H 3 "tcp status" " [\fItcb_addr\fP]"
Without arguments, displays several TCP-level statistics, plus a summary of
all existing TCP connections, including TCB address, send and receive queue
sizes, local and remote sockets, and connection state. If \fItcb_addr\fP is
specified, a more detailed dump of the specified TCB is generated, including
send and receive sequence numbers and timer information.
.H 3 "tcp syndata" " [on|off]"
Display or set the TCP SYN+data piggybacking flag.
Some TCP implementations cannot handle SYN+data together.
The default is \fBoff\fP.
.H 3 "tcp trace" " [on|off]"
Display or set the TCP trace flag.
The default is \fBoff\fP.
.H 3 "tcp window" " [\fIsize\fP]"
Display or set the default receive window size in bytes to be used by TCP
when creating new connections. Existing connections are unaffected.
The default is 2048 bytes.
.H 2 "telnet" " \fIhostid\fP [\fIport\fP]"
Create a Telnet session to the specified host and enter converse mode.
If \fIport\fP is given that port is used. Default port is 23.
.H 2 "trace" " [\fIinterface\fP [\fIflags\fP|subcommand [\fItracefile\fP]]]"
Control packet tracing by the interface drivers.
Specific bits in \fIflags\fP enable tracing of the various interfaces
and control the amount of information produced.
Tracing is controlled on a per-interface basis.
Without arguments, \fBtrace\fP displays a list of all defined interfaces
and their tracing status.
Output can be limited to a single interface by specifying it,
and the control flags can be changed by specifying them as well.
\fIflags\fP is constructed from the logical OR of the following flag bits:
.DS
.TS
center box tab(;) ;
cB | cB
l | l.
Flag;Meaning
_
0001;Trace output packets
0010;Trace input packets
_
0000;Decode headers
0100;Print data (but not headers) in ASCII
0200;Print headers and data in HEX and ASCII
_
1000;Trace only packets addressed to me, suppress broadcasts
_
2000;Print all packet bytes (input and output), no interpretation
.TE
.DE
Instead of defining the trace flags numerically one of the following
subcommands may be given:
.DS
.TS
center box tab(;) ;
cB | cB
l | l.
Subcommand;Meaning
_
-ascii;Decode headers only
-broadcast;Disable trace of broadcasts
-hex;Print data (but not headers) in ASCII
-input;Disable input trace
-output;Disable output trace
-raw;Disable raw packet dumps
ascii;Print data (but not headers) in ASCII
broadcast;Enable trace of broadcasts
hex;Print headers and data in HEX and ASCII
input;Enable input trace
off;Turn off all trace output
output;Enable output trace
raw;Enable raw packet dumps
.TE
.DE
If \fItracefile\fP is not specified, tracing will be to the console.
.H 2 "udp" " \fIsubcommand\fP"
These commands control the User Datagram Protocol service.
.H 3 "udp status"
Displays the status of all UDP receive queues.
.H 2 "upload" " [\fIfilename\fP]"
Open \fIfilename\fP and send it on the current session as though it were
typed on the terminal. If \fIfilename\fP is not specified the current status
is displayed.
.H 1 "FTP Subcommands"
TO BE WRITTEN.
.H 2 "abort"
Aborts a file transfer operation in progress.
When receiving a file, \fBabort\fP simply resets the data connection.
The next incoming data packet will generate a TCP RST (reset) in response
which will clear the remote server.
When sending a file, \fBabort\fP sends a premature end-of-file.
Note that in both cases \fBabort\fP will leave a partial copy of the file on
the destination machine, which must be removed manually if it is
unwanted. \fBAbort\fP is valid only when a transfer is in progress.
.H 2 "append" " \fIlocal-file\fP [\fIremote-file\fP]"
Copy \fIlocal-file\fP to the end of \fIremote-file\fP.
If \fIremote-file\fP is left unspecified,
the local file name is used in naming the remote file.
.H 2 "ascii"
Set the file transfer type to network ASCII. This is the default type.
See also the \fBtype\fP command.
.H 2 "binary"
Set the file transfer type to BINARY/IMAGE.
See also the \fBtype\fP command.
.H 2 "bye"
Close the connection to the server host.
.H 2 "cd" " \fIremote-directory\fP"
Set the working directory on the server host to \fIremote-directory\fP.
.H 2 "cdup"
Set the working directory on the server host to the parent of the
current remote working directory.
.H 2 "delete" " \fIremote-file\fP"
Delete \fIremote-file\fP. \fIremote-file\fP can be an empty directory.
.H 2 "dir" " [\fIremote-directory\fP [\fIlocal-file\fP]]"
Write a listing of \fIremote-directory\fP to standard output or optionally to
\fIlocal-file\fP.
The listing includes any system-dependent information that the server chooses
to include.
For example, most UNIX systems produce output from the command
\fBls -l\fP (see also \fBnlist\fP).
If neither \fIremote-directory\fP nor \fIlocal-file\fP is specified,
list the remote working directory to standard output.
.H 2 "get" " \fIremote-file\fP [\fIlocal-file\fP]"
Copy \fIremote-file\fP to \fIlocal-file\fP. If \fIlocal-file\fP is unspecified,
ftp uses the specified \fIremote-file\fP name as the \fIlocal-file\fP name.
.H 2 "image"
Set the file transfer type to BINARY/IMAGE.
See also the \fBtype\fP command.
.H 2 "ls" " [\fIremote-directory\fP [\fIlocal-file\fP]]"
An alias for the \fBdir\fP command.
.H 2 "mkdir" " \fIremote-directory\fP"
Create \fIremote-directory\fP.
.H 2 "modtime" " \fIremote-file\fP"
Show the last modification time of \fIremote-file\fP.
.H 2 "nlist" " [\fIremote-directory\fP [\fIlocal-file\fP]]"
Write an abbreviated listing of \fIremote-directory\fP to standard output or optionally to
\fIlocal-file\fP.
If neither \fIremote-directory\fP nor \fIlocal-file\fP is specified,
list the remote working directory to standard output.
.H 2 "password" " \fIpassword\fP"
Supply the \fIpassword\fP required by a remote system to complete
the login procedure.
The \fBpassword\fP command has only to be used if \fIpassword\fP
was not specified with the \fBuser\fP command.
.H 2 "put" " \fIlocal-file\fP [\fIremote-file\fP]"
Copy \fIlocal-file\fP to \fIremote-file\fP. If \fIremote-file\fP is unspecified,
ftp assigns the \fIlocal-file\fP name to the \fIremote-file\fP name.
.H 2 "pwd"
Write the name of the remote working directory to stdout.
.H 2 "quit"
An alias for the \fBbye\fP command.
.H 2 "quote" " \fIftp-command\fP"
Send \fIftp-command\fP, verbatim, to the server host.
.H 2 "recv" " \fIremote-file\fP [\fIlocal-file\fP]"
An alias for the \fBget\fP command.
.H 2 "reget" " \fIremote-file\fP [\fIlocal-file\fP]"
\fBreget\fP acts like \fBget\fP, except that if \fIlocal-file\fP exists and is
smaller than \fIremote-file\fP, \fIlocal-file\fP is presumed to be a
partially transferred copy of \fIremote-file\fP and the transfer is
continued from the apparent point of failure. This command is
useful when transferring very large files over networks that tend
to drop connections.
.H 2 "restart" " \fIoffset\fP"
Restart the immediately following \fBget\fP or \fBput\fP
at the indicated \fIoffset\fP.
.H 2 "rhelp" " [\fIcommand-name\fP]"
Request help from the server host. If \fIcommand-name\fP is specified,
supply it to the server.
.H 2 "rmdir" " \fIremote-directory\fP"
Delete \fIremote-directory\fP. \fIremote-directory\fP must be an empty
directory.
.H 2 "send" " \fIlocal-file\fP [\fIremote-file\fP]"
An alias for the \fBput\fP command.
.H 2 "size" " \fIremote-file\fP"
Show the size of \fIremote-file\fP.
.H 2 "system"
Show the type of operating system running on the remote machine.
.H 2 "type" " [a|b|i|l \fIbytesize\fP]"
Set the FTP file transfer type to either ASCII (type \fBa\fP), or
BINARY/IMAGE (type \fBb\fP or \fBi\fP).
The default type is ASCII.
In ASCII mode, files are sent as varying length lines of text
separated by cr/lf sequences.
In BINARY/IMAGE mode, files are sent exactly as they appear in the file system.
ASCII mode should be used whenever transferring text
between dissimilar systems (e.g., UNIX and MS-DOS) because of their
different end-of-line and/or end-of-file conventions.
When exchanging text files between machines of the same type,
either mode will work, but BINARY/IMAGE mode is usually faster.
Naturally, when exchanging raw binary files (executables, compressed archives,
etc) BINARY/IMAGE mode must be used.
Type \fBl\fP (logical byte size) is used when exchanging binary files
with remote servers having oddball word sizes (e.g., DECSYSTEM-10s and 20s).
Locally it works exactly like BINARY/IMAGE, except that it notifies the
remote system how large the byte size is. \fIbytesize\fP is typically 8.
.H 2 "user" " \fIuser-name\fP [\fIpassword\fP]"
Log into the server host on the current connection, which must
already be open.
.H 1 "Setting Paclen, Maxframe, MTU, MSS and Window"
Many \fBWAMPES\fP users are confused by these parameters and do not know how to
set them properly. This chapter will first review these parameters and
then discuss how to choose values for them. Special emphasis is given to
avoiding interoperability problems that may appear when communicating
with non-\fBWAMPES\fP implementations of AX.25.
.H 2 "AX.25 Parameters"
.H 3 "Paclen"
Paclen limits the size of the data field in an AX.25 I-frame. This
value does NOT include the AX.25 protocol header (source,
destination and digipeater addresses).
.P
Since unconnected-mode (datagram) AX.25 uses UI frames, this parameter
has no effect in unconnected mode.
.P
The default value of \fBpaclen\fP is 256 bytes.
.H 3 "Maxframe"
This parameter controls the number of I-frames that \fBWAMPES\fP may send
on an AX.25 connection before it must stop and wait for an acknowledgement.
Since the AX.25/LAPB sequence number field is 3 bits wide, this number
cannot be larger than 7.
.P
Since unconnected-mode (datagram) AX.25 uses UI frames that do not have
sequence numbers, this parameter does NOT apply to unconnected
mode.
.P
The default value of \fBmaxframe\fP in \fBWAMPES\fP is 7 frames.
.H 2 "IP and TCP Parameters"
.H 3 "MTU"
The MTU (Maximum Transmission Unit) is an interface parameter that
limits the size of the largest IP
datagram that it may handle. IP datagrams routed to
an interface that are larger than its MTU are each split into two or more
\fIfragments\fP.
Each fragment has its own IP header and is handled by the network
as if it were a distinct IP datagram, but when it arrives at
the destination it is held by the IP layer until all of the other fragments
belonging to the original datagram have arrived. Then they are reassembled
back into the complete, original IP datagram.
The minimum acceptable interface MTU is 28
bytes: 20 bytes for the IP (fragment) header, plus 8 bytes of data.
.P
There is no default MTU in \fBWAMPES\fP, it must be explicitly specified for
each interface as part of the \fBattach\fP command.
.H 3 "MSS"
MSS (Maximum Segment Size) is a TCP-level parameter that limits the
amount of data that the remote TCP will send in a single TCP
packet. MSS values are exchanged in the SYN (connection request)
packets that open a TCP connection. In the \fBWAMPES\fP implementation of TCP,
the MSS actually used by TCP is further reduced in order to avoid
fragmentation at the local IP interface. That is, the local TCP asks IP
for the MTU of the interface that will be used to reach the
destination. It then subtracts 40 from the MTU value to allow for the
overhead of the TCP and IP headers. If the result is less than the MSS
received from the remote TCP, it is used instead.
.P
The default value of \fBMSS\fP is 512 bytes.
.H 3 "Window"
This is a TCP-level parameter that controls how much data the local TCP
will allow the remote TCP to send before it must stop and wait for an
acknowledgement. The actual window value used by TCP when deciding how
much more data to send is referred to as the \fIeffective window\fP.
This is the smaller of two values: the window advertised by the remote
TCP minus the unacknowledged data in flight, and the \fIcongestion
window\fP, an automatically computed time-varying estimate of how much
data the network can handle.
.P
The default value of \fBWindow\fP is 2048 bytes.
.H 2 "Discussion"
.H 3 "IP Fragmentation vs AX.25 Segmentation"
IP-level fragmentation often makes it possible to interconnect two
dissimilar networks, but it is best avoided whenever possible.
One reason is that when a single IP fragment is lost, all other fragments
belonging to the same datagram are effectively also lost and
the entire datagram must be retransmitted by the source.
Even without loss, fragments require the allocation of temporary buffer
memory at the destination, and it is never easy
to decide how long to wait for missing fragments before
giving up and discarding those that have already arrived.
A reassembly timer controls this process.
In \fBWAMPES\fP it is (re)initialized with the \fBip rtimer\fP parameter
(default 30 seconds) whenever progress is made in reassembling a datagram
(i.e., a new fragment is received).
It is not necessary that all of the fragments belonging to a datagram
arrive within a single timeout interval, only that the interval between
fragments be less than the timeout.
.P
Most subnetworks that carry IP have MTUs of 576 bytes or more, so
interconnecting them with subnetworks having smaller values can result in
considerable fragmentation. For this reason, IP implementors working with
links or subnets having unusually small packet size limits are encouraged
to use
\fItransparent fragmentation\fP,
that is, to devise schemes to break up large IP
datagrams into a sequence of link or subnet frames that are immediately
reassembled on the other end of the link or subnet into the original, whole IP
datagram without the use of IP-level fragmentation. Such a
scheme is provided in AX.25 Version 2.1. It can break
a large IP or NET/ROM datagram into a series of \fBpaclen\fP-sized
AX.25 segments (not to be confused with TCP segments),
one per AX.25 I-frame, for transmission and reassemble them into
a single datagram at the other end of the link before handing it up to the
IP or NET/ROM module. Unfortunately, the segmentation procedure is a new
feature in AX.25 and is not yet widely implemented,
in fact, \fBWAMPES\fP and \fBNOS\fP are so far
the only known implementations. This creates some interoperability problems
between \fBWAMPES\fP and non-\fBWAMPES\fP nodes, in particular, standard
NET/ROM nodes being used to carry IP datagrams. This problem is discussed
further in the section on setting the MTU.
.H 3 "Setting Paclen"
The more data you put into an AX.25 I frame, the smaller the AX.25
headers are in relation to the total frame size. In other words, by
increasing \fBpaclen\fP, you lower the AX.25 protocol overhead. Also, large
data packets reduce the overhead of keying up a transmitter, and this
can be an important factor with higher speed modems. On the other hand,
large frames make bigger targets for noise and interference. Each link
has an optimum value of \fBpaclen\fP that is best discovered by experiment.
.P
Another thing to remember when setting \fBpaclen\fP is that the AX.25 version
2.0 specification limits it to 256 bytes. Although \fBWAMPES\fP can handle
much larger values, some other AX.25 implementations (including
digipeaters) cannot and this
may cause interoperability problems. Even \fBWAMPES\fP may have trouble with
certain KISS TNCs because of fixed-size buffers. The original KISS TNC
code for the TNC-2 by K3MC can handle frames limited in size only by
the RAM in the TNC, but some other KISS TNCs cannot.
.P
One of the drawbacks of AX.25 is that there is no way for one station to tell
another how large a packet it is willing to accept. This requires the
stations sharing a channel to agree beforehand on a maximum packet size.
TCP is different, as we shall see.
.H 3 "Setting Maxframe"
For best performance on a half-duplex radio channel, \fBmaxframe\fP should
always be set to 1. The reasons are explained in the paper \fILink Level
Protocols Revisited\fP by Brian Lloyd and Phil Karn, which appeared in the
proceedings of the ARRL 5th Computer Networking Conference in 1986.
.H 3 "Setting MTU"
TCP/IP header overhead considerations similar to those of the AX.25 layer
when setting \fBpaclen\fP apply when choosing an MTU. However, certain
subnetwork types have well-established MTUs, and
these should
always be used unless you know what you're doing: 1500 bytes for Ethernet,
and 508 bytes for ARCNET.
The MTU for PPP is automatically negotiated, and defaults to 1500.
Other subnet types, including SLIP and AX.25, are
not as well standardized.
.P
SLIP has no official MTU, but the most common
implementation (for BSD UNIX) uses an MTU of 1006 bytes. Although
\fBWAMPES\fP has no hard wired limit on the size of a received SLIP frame,
this is not true for other systems.
Interoperability problems may therefore result if larger MTUs are used in
\fBWAMPES\fP.
.P
Choosing an MTU for an AX.25 interface is more complex. When the interface
operates in datagram (UI-frame) mode, the \fBpaclen\fP parameter does not
apply. The MTU effectively becomes the \fBpaclen\fP of the link. However,
as mentioned earlier, large packets sent on AX.25 \fIconnections\fP are
automatically segmented into I-frames no larger than \fBpaclen\fP bytes.
Unfortunately, as also mentioned earlier, \fBWAMPES\fP and \fBNOS\fP are so far the only known
implementations of the new AX.25 segmentation procedure. This is fine as long
as all of the NET/ROM nodes along a path are running \fBWAMPES\fP, but since the main
reason \fBWAMPES\fP supports NET/ROM is to allow use of existing NET/ROM networks,
this is unlikely.
.P
So it is usually important to avoid AX.25 segmentation when running IP over
NET/ROM. The way to do this is to make sure that packets larger
than \fBpaclen\fP are never handed to AX.25. A NET/ROM transport header is
5 bytes long and a NET/ROM network header takes 15 bytes, so 20 bytes must
be added to the size of an IP datagram when figuring the size of the AX.25
I-frame data field. If \fBpaclen\fP is 256, this leaves 236 bytes for the IP
datagram. This is the default MTU of the \fBnetrom\fP pseudo-interface, so
as long as \fBpaclen\fP is at least 256 bytes, AX.25 segmentation can't
happen. But if smaller values of \fBpaclen\fP are used, the \fBnetrom\fP MTU
must also be reduced with the \fBifconfig\fP command.
.P
On the other hand, if you're running IP directly on top of AX.25, chances
are all of the nodes are running \fBWAMPES\fP and support AX.25 segmentation.
In this case there is no reason not to use a larger MTU and let
AX.25 segmentation do its thing. If you choose
an MTU on the order of 1000-1500 bytes, you can largely avoid IP-level
fragmentation and reduce TCP/IP-level header overhead on file transfers
to a very low level.
And you are still free to pick whatever \fBpaclen\fP value is
appropriate for the link.
.H 3 "Setting MSS"
The setting of this TCP-level parameter is somewhat less critical than the
IP and AX.25 level parameters already discussed, mainly because it is
automatically lowered according to the MTU of the local interface when a
connection is created. Although this is, strictly speaking, a protocol
layering violation (TCP is not supposed to have any knowledge of the
workings of lower layers) this technique does work well in practice.
However, it can be fooled, for example, if a routing change occurs after the
connection has been opened and the new local interface has a smaller MTU
than the previous one, IP fragmentation may occur in the local system.
.P
The only drawback to setting a large MSS is that it might cause avoidable
fragmentation at some other point within the network path if it includes a
"bottleneck" subnet with an MTU smaller than that of the local interface.
(Unfortunately, there is presently no way to know when this is the case.
There is ongoing work within the Internet Engineering Task Force on a "MTU
Discovery" procedure to determine the largest datagram that may be sent over
a given path without fragmentation, but it is not yet complete.)
Also, since the MSS you specify is sent to the remote system, and not all
other TCPs do the MSS-lowering procedure yet, this might cause the remote
system to generate IP fragments unnecessarily.
.P
On the other hand, a too-small MSS can result in a considerable performance
loss, especially when operating over fast LANs and networks that can handle
larger packets. So the best value for MSS is probably 40 less than the
largest MTU on your system, with the 40-byte margin allowing for the TCP and
IP headers. For example, if you have a SLIP interface with a 1006 byte MTU
and an Ethernet interface with a 1500 byte MTU, set MSS to 1460 bytes. This
allows you to receive maximum-sized Ethernet packets, assuming the path to
your system does not have any bottleneck subnets with smaller MTUs.
.H 3 "Setting Window"
A sliding window protocol like TCP cannot transfer more than one window's
worth of data per round trip time interval. So this TCP-level parameter
controls the ability of the remote TCP to keep a long "pipe" full. That is,
when operating over a path with many hops, offering a large TCP window will
help keep all those hops busy when you're receiving data. On the other hand,
offering too large a window can congest the network if it cannot buffer all
that data. Fortunately, new algorithms for dynamic controlling the
effective TCP flow control window have been developed over the past few
years and are now widely deployed.
\fBWAMPES\fP includes them, and you can watch them
in action with the \fBtcp status\fP \fItcb_addr\fP command.
Look at the \fBcwind\fP (congestion window) value.
.P
In most cases it is safe to set the TCP window to a small integer
multiple of the MSS, (eg. 4 times), or larger if necessary to fully utilize a
high bandwidth*delay product path. One thing to keep in mind, however, is
that advertising a certain TCP window value declares that the system has
that much buffer space available for incoming data.
\fBWAMPES\fP does not actually preallocate this space,
it keeps it in a common pool and may well "overbook" it,
exploiting the fact that many TCP connections are idle for long periods
and gambling that most applications will read incoming data from an active
connection as soon as it arrives, thereby quickly freeing the buffer memory.
However, it is possible to run \fBWAMPES\fP out of memory if excessive TCP window
sizes are advertised and either the applications go to sleep indefinitely
(eg. suspended Telnet sessions) or a lot of out-of-sequence data arrives.
It is wise to keep an eye on the amount of available memory and to decrease
the TCP window size (or limit the number of simultaneous connections) if it
gets too low.
.P
Depending on the channel access method and link level protocol, the use
of a window setting that exceeds the MSS may cause an increase in channel
collisions. In particular, collisions between data packets and returning
acknowledgments during a bulk file transfer
may become common. Although this is, strictly speaking,
not TCP's fault, it is possible to work around the problem at the TCP level
by decreasing the window so that the protocol operates in stop-and-wait mode.
This is done by making the window value equal to the MSS.
.H 2 "Summary"
In most cases, the default values provided by \fBWAMPES\fP for each of these
parameters
will work correctly and give reasonable performance. Only in special
circumstances such as operation over a very poor link or experimentation
with high speed modems should it be necessary to change them.
.TC 1 1 7 0
