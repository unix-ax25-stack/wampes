.\"
.\" Format this manual with:
.\"
.\" groff -t -p -mgm manual.mm
.\"
.PH "" \" Page header
.ds HF 3 3 3 3 3 3 3 \" All headers bold
.ds HP +2 +2 +2 +2 +2 +2 +2 \" All headers 2 points bigger
.nr Cl 7 \" Max level of header for table of contents
.nr Ej 1 \" Level 1 headers on new page
.nr Hb 7 \" Break after all headers
.nr Hs 7 \" Empty line after all headers
.nr Hy 1 \" Hyphenation on
.\"
.PF "^WAMPES Reference Manual^-\\\\nP-^Version 950729" \" Page footer
.\"
.S 30
.ce
\fBWAMPES Reference Manual\fP
.ce
Version 950729
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
.nr Cl 7 \" Max level of header for table of contents
.H 1 "Credits"
This manual is based in part on publications authored by
.DS I
Phil Karn, KA9Q
Bdale Garbee, N3EUA
Gerard van der Grinten, PA0GRI
.DE
.nr Cl 2 \" Max level of header for table of contents
.H 1 "The /tcp/net Program"
The executable file \fB/tcp/net\fP
(further called \fBWAMPES\fP) provides Internet (TCP/IP),
NET/ROM and AX.25
facilities. Because it has an internal multi-tasking system,
\fBWAMPES\fP can act simultaneously as a client, a server and a packet switch
for all three sets of protocols. That is, while a local user accesses remote
services, the system can also provide those same services to remote users
while also switching IP, NET/ROM and AX.25 packets and frames between other
client and server nodes.
.P
The keyboard and display is used by the local operator to control both host
and gateway level functions, for which a number of commands are provided.
.H 2 "Startup"
.DS I
\fB/tcp/net [-g] [-v]\fP [\fIstartup file\fP]
.DE
When \fBWAMPES\fP is executed without arguments,
it attempts to open the file \fB/tcp/net.rc\fP.
If it exists, it is read and executed as though its contents
were typed on the console as commands.
This feature is useful for configuring network addresses,
attaching communication interfaces, and starting the various services.
.P
The following command-line options are accepted:
.VL 20 2
.LI \fB-g\fP
The \fB-g\fP option causes \fBWAMPES\fP to run in \fBdebug\fP mode.
In \fBdebug\fP mode, \fBWAMPES\fP will NOT:
.BL 5 1
.LI
load or save the ARP table, the IP routing table, or the AX.25 routing table
from or to a disc file
.LI
unlink any UNIX domain sockets
.LI
enable the 120 seconds watch dog timer
.LI
change its UNIX scheduling priority
.LI
check the files \fB/tcp/net\fP and \fB/tcp/net.rc\fP for modifications
.LE
.LI \fB-v\fP
The \fB-v\fP option allows the user to view command execution during
the startup of \fBWAMPES\fP.
It echoes the commands read from the startup file before they are executed.
This is a nice help if \fBWAMPES\fP stops (hangs) during initialization.
.LE 1
After all command-line options, the name of an alternate startup file may
be specified. This file is then opened and read instead
of \fB/tcp/net.rc\fP.
.H 2 "Environment variables"
The following environment variables are read by \fBWAMPES\fP:
.VL 20 2
.LI \fBTZ\fP
The TZ variable should be set to the local timezone. Default is TZ=MEZ-1MESZ.
This is used in various time stamps.
.LE
.nr Cl 7 \" Max level of header for table of contents
.H 1 "Console modes"
The console may be in one of two modes: \fBcommand mode\fP or
\fBconverse mode\fP.
In \fBcommand mode\fP the prompt \fIhostname\fP> is displayed and any of the
commands described in the \fBCommands\fP chapter may be entered.
In \fBconverse mode\fP
keyboard input is processed according to the current session.
.P
Sessions come in many types: Telnet, FTP, AX25,
Finger, and NETROM.
.P
In a Telnet, AX25, or NETROM
session keyboard input is sent to the
remote system, and any output from the remote system is displayed on the
console. In a FTP session keyboard input has to consist of
known local commands
(see the \fBFTP Subcommands\fP chapter).
A Finger session is used to peek at a
remote system for its users (and what they are doing on some UNIX systems).
.P
The keyboard also has \fBcooked\fP and \fBraw\fP states.
In \fBcooked\fP state input
is line-at-a-time. The user may use the editing keys described below
to edit the line.
Hitting either Return or Linefeed passes the
complete line to the application.
In \fBraw\fP mode each character is
immediately passed to the application as it is typed.
The keyboard is always in \fBcooked\fP state in command mode.
It is also \fBcooked\fP in converse mode on an AX25, FTP, or NETROM session.
In a Telnet session it depends on
whether the remote end has issued (and the local end has
accepted) the Telnet WILL ECHO option (see the \fBecho\fP command).
.P
To escape back to \fBcommand mode\fP
the user must enter the \fIescape\fP character, which is by
default Control-] (0x1d, ASCII GS). Note that this is distinct from the
ASCII character of the same name. The escape character can be changed (see
the \fBescape\fP command).
Setting the escape character to an unreachable code renders a system inescapable
and the user hung in a session.
.P
The following editing keys are available:
.VL 20 2 1
.LI Control-A
.LI Home
.LI Shift-LeftArrow
Move cursor to start of line.
.SP
.LI Escape\ b
Move cursor backward one word.
.SP
.LI Control-B
.LI LeftArrow
Move cursor backward one character.
.SP
.LI Control-F
.LI RightArrow
Move cursor forward one character.
.SP
.LI Escape\ f
Move cursor forward one word.
.SP
.LI Control-E
.LI Shift-Home
.LI Shift-RightArrow
Move cursor to end of line.
.SP
.LI Control-W
.LI Escape\ Backspace
.LI Escape\ DEL
Delete previous word.
.SP
.LI Control-H
.LI Backspace
.LI DEL
Delete previous character.
.SP
.LI Escape\ d
Delete current word.
.SP
.LI Control-D
.LI DeleteChar
Delete current character.
.SP
.LI Control-K
.LI ClearLine
Delete from cursor to end of line.
.SP
.LI Control-U
.LI Control-X
.LI DeleteLine
Delete entire line.
.SP
.LI Control-V
Escape next character. Editing characters
can be entered in a command line or in
a search string if preceded by a Control-V.
Control-V removes the
next character's editing features (if any).
.SP
.LI Control-L
Linefeed and print line.
.SP
.LI Control-P
.LI Prev
.LI UpArrow
Fetch previous command.
Each time Control-P is entered,
the next previous command in the history list is accessed.
.SP
.LI Control-N
.LI Next
.LI DownArrow
Fetch next command.
Each time Control-N is entered,
the next command in the history list is accessed.
.SP
.LI Control-R\fIstring\fP
Search the history list for a previous command line
containing \fIstring\fP.
\fIstring\fP is terminated by a Return or Linefeed.
.SP
.LI Control-J
.LI Control-M
.LI Enter
.LI Return
Append Return+Linefeed, then execute line.
.SP
.LI Control-T
Execute line without appending Return+Linefeed to it.
.LE
.nr Cl 2 \" Max level of header for table of contents
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
parameters. Giving a \fB?\fP in place of the subcommand will also
generate the message. This is useful when the command word alone is a
valid command. If a command takes an optional value parameter, issuing
the command without the parameter generally displays the current value
of the variable. Exceptions to this rule are noted in the individual
command descriptions.
.P
Two or more parameters separated by vertical bar(s) denote a choice
between the specified values. Optional parameters are shown enclosed in
[brackets], and a parameter shown as \fIparameter\fP should be
replaced with an actual value or string. For example, the notation
\fIhostid\fP denotes an actual host or gateway, which may be specified in
one of two ways: as a numeric IP address in dotted decimal notation
(eg. 44.0.0.1), or as a symbolic name stored in the domain name database.
.P
All commands and many subcommands may be abbreviated. You only need
type enough of a command's name to distinguish it from others that begin
with the same series of letters. Parameters, however, must be typed in
full.
.P
FTP subcommands (eg. put, get, dir, etc) are recognized only in
converse mode with the appropriate FTP session, they are not recognized
in command mode (see the \fBFTP Subcommands\fP chapter).
.P
A word beginning with \fB#\fP causes that word and all the following
characters on the same line to be ignored.
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
For each IP address entry the subnet type (eg. \fBax25\fP, \fBnetrom\fP),
link address, and time to expiration is shown.
If the link address is currently unknown,
the number of IP datagrams awaiting resolution is also shown.
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
Use this feature with great care.
.H 2 "asystat" " [\fIinterface\fP ...]"
Display statistics on the specified or all
attached asynchronous communications interfaces.
The display for each interface consists of three lines:
.BL
.LI
The first line shows the interface name,
the state (\fBUP\fP or \fBDOWN\fP),
and the speed in bits per second.
.LI
The second line shows receiver (RX) event
counts: the total number of read system calls, received
characters, and the receiver high water mark. The receiver high water
mark is the maximum number of characters ever read from the
device during a single read system call. This is useful for
monitoring system interrupt latency margins as it shows how
close the port hardware has come to overflowing due to the
inability of the CPU to respond in time.
The high water mark is reset to 0 after \fBasystat\fP has
displayed its value.
.LI
The third line shows transmit (TX) statistics, including the total
number of write system calls and transmitted characters.
.LE
.H 2 "attach" " \fItype\fP [\fItype specific options\fP]"
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
.P
If any I/O error is encountered reading or writing the interface device file,
the interface will be marked \fBDOWN\fP.
Use the \fBparam\fP \fIname\fP \fBUp\fP command to re-enable the interface.
.H 3 "attach asy \fIip-addr\fP" " \fIport\fP \fIencapsulation\fP \fIname\fP 0 \fImtu\fP \fIspeed\fP"
Configure and attach a UNIX TCP connection based interface to the system.
This is very similar to the asynchronous communications interface described above,
but instead of talking directly to a hardware device file, this interface type
will open a UNIX TCP connection to \fIip-addr\fP and \fIport\fP.
The primary use of this interface type is to talk to some TNC which is
connected to the system via the LAN.
\fIip-addr\fP is the destination IP address,
and has to be specified as one hexadecimal number.
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
\fIspeed\fP must be specified, but is not used.
.P
If any I/O error is encountered reading or writing the UNIX TCP connection,
the interface will be marked \fBDOWN\fP.
Use the \fBparam\fP \fIname\fP \fBUp\fP command to re-enable the interface.
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
This creates an IP point-to-point link between \fBWAMPES\fP and UNIX,
by creating a new interface named \fIname\fP on the \fBWAMPES\fP side,
and by creating a new interface named \fBni?\fP
with IP address \fIip-addr\fP
and netmask \fInetmask\fP
on the UNIX side.
It also establishes a UNIX routing table entry
directing traffic for \fBWAMPES\fP to the newly created interface.
\fIip-addr\fP and \fInetmask\fP have to be specified
as numeric IP addresses in dotted decimal notation (eg. 44.0.0.1),
or as symbolic names stored in the domain name database.
\fInetmask\fP defaults to 255.0.0.0 if not specified.
Example:
.DS I
.ft CW
attach ni ni dk5sg-u # ip to host ip
.ft P
.DE
.P
Currently \fBattach ni\fP is available on HP-UX systems only.
.H 2 "ax25" " \fIsubcommand\fP"
These commands control the AX.25 service.
.H 3 "ax25 blimit" " [\fIlimit\fP]"
Display or set the AX.25 retransmission backoff limit. Normally each
successive AX.25 retransmission is delayed by a factor of 1.25 compared to
the previous interval, this is called \fBexponential backoff\fP.
When the number of retries reaches the \fBblimit\fP setting the backoff
is held at its current value, and is not increased anymore.
Note that this is applicable only to actual AX.25 connections, UI frames
will never be retransmitted by the AX.25 layer.
The default is 16.
.H 3 "ax25 destlist" " [\fIinterface\fP]"
Display the AX.25 "destination" list.
Each address seen in the destination field
of an AX.25 frame is displayed (most recent first),
along with the time since it was last referenced.
The time since the same address was last seen in the source field
of an AX.25 frame on the same interface is also shown.
If the address has never been seen in the source field of a frame,
then this field is left blank.
(This indicates that the destination is either a multicast address or a "hidden station".)
If \fIinterface\fP is given, only the list for that interface is displayed.
.H 3 "ax25 digipeat" " [0|1|2]"
Display or set the digipeat mode. The default is 2. MORE TO BE WRITTEN.
.H 3 "ax25 flush"
Clear the AX.25 "heard" and "destination" lists
(see \fBax25 heard\fP and \fBax25 destlist\fP).
.H 3 "ax25 heard" " [\fIinterface\fP]"
Display the AX.25 "heard" list.
For each interface that is configured to use AX.25,
a list of all addresses heard through that interface is shown,
along with a count of the number of packets heard from each station
and the interval, in days:hr:min:sec format, since each station was last heard.
The list is sorted in most-recently-heard order.
The local station appears first in the listing,
the packet count actually reflects the number of packets transmitted.
This count will be correct whether or not the modem monitors
its own transmissions.
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
unacknowledged at any time on AX.25 connections. This number cannot
be greater than 7.
Note that the maximum outstanding frame count only works with
virtual connections, UI frames are not affected.
The default is 7 frames.
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
.H 3 "ax25 mycall" " [\fIax25_addr\fP]"
Display or set the default local AX.25 address. The standard format is used,
eg. KA9Q-0 or WB6RQN-5.
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
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
.H 3 "ax25 pthresh" " [\fIsize\fP]"
Display or set the poll threshold to be used for new AX.25 Version 2
connections. The poll threshold controls retransmission behavior as
follows. If the oldest unacknowledged I frame size is less than the poll
threshold, it will be sent with the poll (P) bit set if a timeout occurs.
If the oldest unacknowledged I frame size is equal to or greater than the
threshold, then a RR or RNR frame, as appropriate, with the poll bit set
will be sent if a timeout occurs.
.P
The idea behind the poll threshold is that the extra time needed to send a
"small" I frame instead of a supervisory frame when polling after a timeout
is small, and since there is a good chance the I frame will have to be sent
anyway (i.e., if it were lost previously) then you might as well send it as
the poll. But if the I frame is large, send a supervisory (RR/RNR) poll
instead to determine first if retransmitting the oldest unacknowledged
I frame is necessary, the timeout might have been caused by a lost
acknowledgement. This is obviously a tradeoff, so experiment with the
poll threshold setting. The default is 64 bytes.
.H 3 "ax25 reset" " \fIaxcb_addr\fP"
Delete the AX.25 control block at the specified address.
The control block address can be found with the \fBax25 status\fP command.
.H 3 "ax25 retry" " [\fIcount\fP]"
Limit the number of successive unsuccessful transmission attempts on
new AX.25 connections. If this limit is exceeded, the connection
is abandoned and all queued data is deleted.
A \fIcount\fP of 0 allows unlimited transmission attempts.
The default is 10 tries.
.H 3 "ax25 route" " [stat]"
Display the AX.25 routing table that
specifies the interface and digipeaters to be used in reaching a given station.
MORE TO BE WRITTEN.
.H 4 "ax25 route add" " [permanent] \fIinterface\fP default|\fIax25_addr\fP [\fIdigipeater\fP ...]"
Add an entry to the AX.25 routing table. An automatic \fBax25 route add\fP
is executed if digipeaters are specified in an AX.25 \fBconnect\fP
command, or if a connection is received from a remote station.
Such automatic routing table entries won't override locally
created \fBpermanent\fP entries, however. MORE TO BE WRITTEN.
.H 4 "ax25 route list" " [\fIax25_addr\fP ...]"
TO BE WRITTEN.
.H 3 "ax25 status" " [\fIaxcb_addr\fP]"
Without an argument, display a one-line summary of each AX.25 control block.
If the address of a particular control block is specified, the contents of
that control block are shown in more detail.
.H 3 "ax25 t1" " [\fImilliseconds\fP]"
Display or set the AX.25 retransmission timer.
The default is 5000 milliseconds (5 seconds). MORE TO BE WRITTEN.
.H 3 "ax25 t2" " [\fImilliseconds\fP]"
The default is 300 milliseconds.
MORE TO BE WRITTEN.
.H 3 "ax25 t3" " [\fImilliseconds\fP]"
Display or set the AX.25 "keep alive" timer.
The default is 900000 milliseconds (15 minutes).
.H 3 "ax25 t4" " [\fImilliseconds\fP]"
The default is 60000 milliseconds (1 minute).
MORE TO BE WRITTEN.
.H 3 "ax25 version" " [1|2]"
Display or set the version of the AX.25 protocol to attempt to use on
new connections. The default is 2 (the version
that uses the poll/final bits).
.H 3 "ax25 window" " [\fIsize\fP]"
Set the number of bytes that can be pending on an AX.25 receive queue
beyond which I frames will be answered with RNR (Receiver Not Ready)
responses. This presently applies only to suspended interactive AX.25
sessions, since incoming I frames containing network (IP, NET/ROM) packets
are always processed immediately and are not placed on the receive queue.
However, when an AX.25 connection carries both interactive
and network packet traffic, a RNR generated because of
backlogged interactive traffic will also stop network
packet traffic.
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
current session. On an AX.25 session this command initiates a
disconnect. On a FTP or Telnet session this command sends a
FIN (i.e., initiates a close) on the session's TCP connection.
This is an alternative to asking the remote server to initiate a
close (QUIT to FTP, or the logout command appropriate for the
remote system in the case of Telnet). When either FTP or Telnet
sees the incoming half of a TCP connection close, it
automatically responds by closing the outgoing half of the
connection. \fBclose\fP is more graceful than the \fBreset\fP command, in
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
by their AX.25 Protocol IDs.
.H 2 "delete" " \fIfilename\fP ..."
Remove the specified files from the file system.
.H 2 "disconnect" " [\fIsession#\fP]"
An alias for the \fBclose\fP command (for the benefit of AX.25 users).
.H 2 "domain" " \fIsubcommand\fP"
These commands control the Domain Name Service (DNS).
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
Display or set the flag controlling
the use of the UNIX functions gethostbyname
and gethostbyaddr.
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
also performed, see the \fBConsole modes\fP chapter.
.P
When communicating from keyboard to keyboard the standard local
echo mode is used, so the setting of this parameter has no
effect. However, many timesharing systems (eg. UNIX) prefer to
do their own echoing of typed input. (This makes screen editors
work right, among other things.) Such systems send a Telnet
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
.H 2 "escape" " [\fIcharacter\fP]"
Display or set the current command-mode escape character.
To enter a control character from the keyboard it has
to be prefixed by Control-V.
The default is Control-] (0x1d, ASCII GS).
.H 2 "exit"
Exit (terminate) \fBWAMPES\fP.
.H 2 "finger" " [\fIuser\fP]@\fIhostid\fP"
Issue a network finger request for user \fIuser\fP at host \fIhostid\fP. If only
\fB@\fP\fIhostid\fP is given, all users on that host are identified.
.H 2 "fkey" " \fIkey#\fP \fItext\fP"
Set the value for a programmable key on the keyboard.
To enter a control character from the keyboard it has
to be prefixed by Control-V.
\fIText\fP has to be enclosed in double quotes if it
contains white space.
.H 2 "flexnet" " \fIsubcommand\fP"
These commands control the FLEXNET service.
.H 3 "flexnet dest" " [\fIax25_addr\fP]"
Display all known, or the specified, FLEXNET destination,
together with the list of neighbors
through which the destination can be reached.
The number in parentheses after each neighbor is the propagation delay
to the destination through this neighbor.
The neighbor list is sorted by this delay,
the best neighbor
(the one which actually will be used)
is listed first.
The delay is measured in 100 millisecond units,
a value of zero is to be taken as infinity.
.H 3 "flexnet destdebug"
Display all known FLEXNET destinations,
together with the list of all neighbors.
Two numbers are printed in parentheses after each neighbor.
The \fBD\fPelay value is the propagation delay to the destination
through this neighbor.
The \fBL\fPast value is the delay our node sent to this neighbor,
telling it the propagation delay to the destination through our node.
The delays are measured in 100 millisecond units,
a value of zero is to be taken as infinity.
.H 3 "flexnet link"
Without an argument, display the FLEXNET link table,
which contains all known FLEXNET neighbors.
The fields are as follows:
.VL 20 2
.LI \fBCall\fP
The call sign and SSID range of this neighbor.
.LI \fBRemote\fP
The propagation delay to this neighbor as measured by the neighbor.
.LI \fBLocal\fP
The propagation delay to this neighbor as measured by the our node.
.LI \fBSmooth\fP
The smoothed, average propagation delay to this neighbor.
.LI \fBP\fP
If this column contains a \fBP\fP,
then the link was created manually,
and is \fBpermanent\fP.
A permanent link can only be removed
with the \fBflexnet link delete\fP command.
.LI \fBT\fP
The state of the routing token encoded as follows:
.VL 10 2 1
.LI \fBN\fP
Our node does not have the token, and did not requested it.
.LI \fBW\fP
Our node does not have the token, but did requested it.
.LI \fBY\fP
Our node has the token.
.LE
.LI \fBState\fP
The state of the AX.25 link to this neighbor.
.LE
.H 4 "flexnet link add" " \fIax25_addr\fP"
Add a permanent entry to the FLEXNET link table.
.H 4 "flexnet link delete" " \fIax25_addr\fP"
Remove the specified entry from the FLEXNET link table.
.H 3 "flexnet query" " \fIax25_addr\fP"
Display the path to the specified FLEXNET destination. Call signs shown
in capital letters along the path support the FLEXNET protocol,
those in lower-case letters do not.
.H 2 "ftp" " \fIhostid\fP [\fIport\fP]"
Open a FTP control channel to the specified remote host
and enter converse mode on the new session.
If \fIport\fP is given that port is used. Default port is 21.
Responses from the remote server are displayed directly on the screen.
See the \fBFTP Subcommands\fP chapter for descriptions of the commands
available in a FTP session.
.H 2 "hostname" " [\fIhostname\fP]"
Display or set the local host's name. By convention this should
be the same as the host's primary domain name. This string is
used in greeting messages of various network
servers, and in the command line prompt.
Note that \fBhostname\fP does NOT set the system's IP address.
.P
If \fIhostname\fP is the same as the name of an attached interface,
\fIhostname\fP will be substituted by the canonical host name
which corresponds to the IP address of that interface.
.H 2 "icmp" " \fIsubcommand\fP"
These commands control the Internet Control Message Protocol (ICMP) service.
.H 3 "icmp echo" " [on|off]"
Display or set the flag controlling the asynchronous display of
ICMP Echo Reply packets. This flag must be \fBon\fP for one-shot
pings to work (see the \fBping\fP command). The default is \fBon\fP.
.H 3 "icmp status"
Display statistics about the Internet Control Message Protocol
(ICMP), including the number of ICMP messages of each type sent
and received.
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
.H 3 "ifconfig \fIinterface\fP broadcast" " \fIhostid\fP"
Set the broadcast address of \fIinterface\fP to \fIhostid\fP.
This is related to the \fBnetmask\fP subcommand.
See also the \fBarp\fP command.
.H 3 "ifconfig \fIinterface\fP encapsulation" " \fIencapsulation\fP"
Set the encapsulation for \fIinterface\fP to \fIencapsulation\fP.
\fIEncapsulation\fP may be one of:
.VL 20 2
.LI \fBnone\fP
No encapsulation.
.LI \fBslip\fP
Serial Line Internet Protocol.
Encapsulates IP datagrams directly in SLIP frames without a link
header. This is for operation on point-to-point lines and is compatible
with 4.2BSD UNIX SLIP.
.LI \fBvjslip\fP
Compressed SLIP.
.LI \fBax25ui\fP,\ \fBax25i\fP
Similar to \fBslip\fP, except that an AX.25 header
is added to the datagram before SLIP encoding.
.LI \fBkissui\fP,\ \fBkissi\fP
Similar to \fBslip\fP, except that an AX.25 header
and a KISS TNC control header
is added to the datagram before SLIP encoding.
.LI \fBnetrom\fP
Adds a NET/ROM network header to the datagram.
.LI \fBnrs\fP
Adds an AX.25 header,
then encodes the frame using
the NET/ROM asynchronous framing technique
for communication with a local NET/ROM TNC.
.LE 1
For AX.25 based encapsulations UI frames (datagram mode) will be used
if any one of the following conditions is true,
otherwise I frames (virtual circuit mode) will be used:
.BL 5 1
.LI
the "low delay" bit is set in the IP type-of-service field.
.LI
the "reliability" bit is NOT set in the IP type-of-service field,
and encapsulation is \fBax25ui\fP, \fBkissui\fP, or \fBnrs\fP.
.LI
the destination is the broadcast address
(this is helpful when broadcasting on an interface
that uses \fBax25i\fP or \fBkissi\fP encapsulation).
.LE
.H 3 "ifconfig \fIinterface\fP forward" " \fIinterface2\fP"
When a forward is defined, all output for \fIinterface\fP is redirected to
\fIinterface2\fP. To remove the forward, set \fIinterface2\fP to \fIinterface\fP.
.H 3 "ifconfig \fIinterface\fP ipaddress" " \fIhostid\fP"
Set the IP address to \fIhostid\fP for this interface. This might be necessary
when a system acts as a gateway.
See also the \fBip address\fP command.
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
Example:
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
Set the transmit queue limit
(maximum number of packets waiting in the transmit queue).
If set to 0 the transmit queue is unlimited.
.H 2 "ip" " \fIsubcommand\fP"
These commands control the Internet Protocol (IP) service.
.H 3 "ip address" " [\fIhostid\fP]"
Display or set the default local IP address. This command must be given before
an \fBattach\fP command if it is to be used as the default IP address for
that interface.
.H 3 "ip rtimer" " [\fIseconds\fP]"
Display or set the IP fragment reassembly timeout. The default is 30 seconds.
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
Without an argument, display the IP filter table,
which allows or denies IP packets to be sent to a destination.
.P
The default is to allow IP packets to be sent to any destination.
Use the \fBipfilter allow\fP and \fBipfilter deny\fP commands
to extend the table.
Entries listed earlier take precedence over entries listed later.
.H 3 "ipfilter allow|deny" " \fIhostid\fP[/\fIbits\fP] [to \fIhostid\fP[/\fIbits\fP]]"
This command (re)defines the rights for a range of IP addresses.
The optional /\fIbits\fP suffix to \fIhostid\fP specifies how
many leading bits in \fIhostid\fP are to be considered significant.
If not specified, 32 bits (i.e., full significance) is
assumed. With this option, a single \fIhostid\fP/\fIbits\fP
specification may refer to
many hosts all sharing a common bit string prefix in their IP addresses.
For example, ARPA Class A, B and C networks would use suffixes of /8,
/16 and /24 respectively. The command
.DS I
.ft CW
ipfilter allow 44/8
.ft P
.DE
causes any IP address beginning with "44" in the first 8 bits to be
allowed, the remaining 24 bits are "don't-cares".
.P
If two \fIhostid\fPs are specified, those two IP addresses and all
IP addresses in between are allowed or denied.
For example:
.DS I
.ft CW
ipfilter allow 44.1.2.0 to 44.1.2.255
.ft P
.DE
is equivalent to
.DS I
.ft CW
ipfilter allow 44.1.2/24
.ft P
.DE
In case one or both of the \fIhostid\fPs has a /\fIbits\fP suffix,
the range of IP addresses allowed or denied
is from the lowest to the highest IP address.
For example:
.DS I
.ft CW
ipfilter allow 44.1/16 to 44.3/16
.ft P
.DE
is equivalent to
.DS I
.ft CW
ipfilter allow 44.1.0.0 to 44.3.255.255
.ft P
.DE
The \fBipfilter\fP command tries to combine multiple \fBallow\fP and \fBdeny\fP
commands into as few IP filter table entries as possible.
Because the table initially allows everything,
the first \fBipfilter\fP command must be \fBdeny\fP to have any effect.
To only allow certain IP addresses use something like
the following command sequence:
.DS I
.ft CW
ipfilter deny 0/0 # default is deny
ipfilter allow 127.0.0.1 # loopback
ipfilter allow 44.128.4/24
ipfilter allow 44.130/16
ipfilter allow 44.142/16
ipfilter allow 44.143/16
ipfilter allow ke0gb
ipfilter allow winfree.n3eua
.ft P
.DE
.H 2 "kick" " [\fIsession#\fP]"
Kick all control blocks associated with a session.
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
Display or set the flag controlling
the automatic login of users. If automatic login
is enabled the user name is derived from the incoming AX.25
call, the NET/ROM user name, or the IP host name. If automatic
login is disabled the user has to supply the user name at the
login: prompt.
The default is \fBon\fP.
.H 3 "login create" " [on|off]"
Display or set the flag controlling
the automatic creation of user accounts (entries
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
.BL 5 1
.LI
/home
.LI
/u
.LI
/users
.LI
/usr/people
.LE 1
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
Display or set the flag controlling
heap debugging. If heap debugging is turned on, all
memory blocks returned by malloc will be filled with the value 0xdd, and
all memory blocks freed will be filled with the value 0xbb. This makes
it easier to track uninitialized and freed memory reads.
The default is \fBoff\fP.
.H 3 "memory freelist"
Display the number of all currently free memory blocks. Different block
sizes (rounded up to the next power of two) are counted separately.
.H 3 "memory merge" " [on|off]"
Display or set the flag controlling
the attempt to merge adjacent free memory blocks into
bigger blocks.
The default is \fBon\fP.
.H 3 "memory sizes"
Display the total number (counted since program startup) of all memory
allocation requests. Requests for different block sizes (rounded up to
the next power of two) are counted separately.
.H 3 "memory status"
Display a summary of storage allocator statistics:
.BL
.LI
The first line
shows the total size of the heap, the amount of heap
memory available in bytes and as a percentage of the total heap size,
and the number of sbrk system calls used to increase the heap size.
.LI
The second line shows the total number of calls to allocate and free blocks
of memory, the difference of these two values (i.e., the number of allocated
blocks outstanding), the number of allocation requests that were denied
due to lack of memory, and the number of calls to free that attempted to
free garbage
(eg. by freeing the same block twice or freeing a garbled pointer).
.LI
The third line shows the total number of splits
(breaking a big heap block into two small blocks)
and merges
(combining two adjacent small blocks into one big block),
and the difference of these two values.
.LI
The fourth line shows the number of calls to pushdown,
and the number of calls to pushdown which resulted in a call to malloc.
.LE
.H 2 "mkdir" " \fIdirname\fP"
Create the specified directory.
.H 2 "netrom" " \fIsubcommand\fP"
These commands control the NET/ROM service.
.H 3 "netrom broadcast" " [\fIinterface\fP \fIax25_addr\fP [\fIdigipeater\fP ...]]"
Without an argument,
display the NET/ROM broadcast table,
which lists all interfaces and destination addresses
to be used if the auto-update broadcast interval timer fires
(see \fBnetrom parms 7\fP).
If \fIinterface\fP and \fIax25_addr\fP (and optional \fIdigipeater\fPs)
are specified, add a new entry to the table.
The usual destination address for NET/ROM broadcasts is NODES.
Note that there is currently no way to delete an entry from the table
(short of restarting \fBWAMPES\fP).
Example:
.DS I
.ft CW
netrom broadcast tnc0 NODES
.ft P
.DE
.H 3 "netrom connect" " \fInode\fP [\fIuser\fP]"
Initiate a NETROM session to the specified NET/ROM \fInode\fP.
If \fIuser\fP is not specified, the value of \fBax25 mycall\fP
will be used as user identification.
The de-facto presentation standard format is used,
in that each packet holds one line of
text, terminated by a carriage return.
.H 3 "netrom ident" " [\fIident\fP]"
Display or set the NET/ROM \fBident\fP (also sometimes called "alias").
The \fBident\fP is only used in outgoing broadcasts,
it is NOT possible to connect \fBWAMPES\fP
using the \fBident\fP as destination address.
.H 3 "netrom kick" " \fInrcb_addr\fP"
If there is unacknowledged data on the send queue of the specified NET/ROM
control block, this command forces an immediate retransmission.
The control block address can be found with the \fBnetrom status\fP command.
.H 3 "netrom links" " [\fInode\fP [\fInode2\fP \fIquality\fP [permanent]]]"
Display or change the NET/ROM links table,
which controls the NET/ROM routing.
A link is the direct connection of two nodes,
without any intermediate nodes.
Without arguments, all known links are shown.
If \fInode\fP is given,
all known links originating from \fInode\fP are shown.
The fields are as follows:
.VL 20 2
.LI \fBFrom\fP
The name of the originating node for this link.
.LI \fBTo\fP
The name of the destination node for this link.
.LI \fBLevel\fP
The level (confidence in the correctness of information) of this link.
Levels are coded as follows:
.VL 10 2 1
.LI \fB1\fP
information created locally
.LI \fB2\fP
information reported by neighbor
.LI \fB3\fP
information reported by neighbor of neighbor
.LE
.LI \fBQuality\fP
The quality of this link.
.LI \fBAge\fP
The age (seconds since last update) of this link.
If this field is empty, the link is marked \fBpermanent\fP.
.LE
.P
If \fInode\fP, \fInode2\fP, and \fIquality\fP are given,
a link between \fInode\fP and \fInode2\fP with quality \fIquality\fP
is put into the table, and also marked \fBpermanent\fP if so specified.
.H 3 "netrom nodes" " [\fInode\fP]"
Display information about all known, or the specified, NET/ROM node.
The fields are as follows:
.VL 20 2
.LI \fBNode\fP
The official name of this node.
.LI \fBIdent\fP
The ident (alias) of this node.
.LI \fBNeighbor\fP
The neighbor used to reach this node.
.LI \fBLevel\fP
The level (confidence in the correctness of information) of this node.
Levels are coded as follows:
.VL 10 2 1
.LI \fB0\fP
myself
.LI \fB1\fP
neighbor
.LI \fB2\fP
neighbor of neighbor
.LI \fB3\fP
reported by neighbor of neighbor
.LI \fB999\fP
unreachable
.LE
.LI \fBQuality\fP
The quality of the path to this node.
.LE
.H 3 "netrom parms" " [\fIparm#\fP [\fIvalue\fP]]"
Display or set NET/ROM parameters.
The following parameters are available:
.P
.TS
center box tab(;) ;
cB | cB | cB | cB | cB | cB
r | l | r | r | r | l.
Parm;Description;Min;Max;Dflt;Used
_
1;Maximum destination list entries;1;400;400;No
2;Worst quality for auto-updates;0;255;0;No
3;Channel 0 (HDLC) quality;0;255;192;Yes
4;Channel 1 (RS232) quality;0;255;255;No
5;Obsolescence count initializer (0=off);0;255;3;Yes
6;Obsolescence count min to be broadcast;0;255;0;No
7;Auto-update broadcast interval (sec, 0=off);0;65535;1800;Yes
8;Network 'time-to-live' initializer;1;255;16;Yes
9;Transport timeout (sec);5;600;60;Yes
10;Transport maximum tries;1;127;5;Yes
11;Transport acknowledge delay (ms);1;60000;1;No
12;Transport busy delay (sec);1;1000;180;Yes
13;Transport requested window size (frames);1;127;8;Yes
14;Congestion control threshold (frames);1;127;8;Yes
15;No-activity timeout (sec, 0=off);0;65535;1800;Yes
16;Persistance;0;255;64;No
17;Slot time (10msec increments);0;127;10;No
18;Link T1 timeout 'FRACK' (ms);1;MAXINT;5000;Yes
19;Link TX window size 'MAXFRAME' (frames);1;7;7;Yes
20;Link maximum tries (0=forever);0;127;10;Yes
21;Link T2 timeout (ms);1;MAXINT;1;Yes
22;Link T3 timeout (ms);0;MAXINT;900000;Yes
23;AX.25 digipeating (0=off 1=dumb 2=s&f);0;2;2;Yes
24;Validate callsigns (0=off 1=on);0;1;0;No
25;Station ID beacons (0=off 1=after 2=every);0;2;0;No
26;CQ UI frames (0=off 1=on);0;1;0;No
.TE
.H 3 "netrom reset" " \fInrcb_addr\fP"
Delete the NET/ROM control block at the specified address.
The control block address can be found with the \fBnetrom status\fP command.
.H 3 "netrom status" " [\fInrcb_addr\fP]"
Without an argument, display a one-line summary of each NET/ROM control block.
If the address of a particular control block is specified, the contents of
that control block are shown in more detail.
.H 2 "nrstat"
Display statistics on all attached NET/ROM serial interfaces.
.H 2 "param" " \fIinterface\fP [\fIname\fP|\fInumber\fP [\fIvalue\fP]]"
Invoke a device-specific control routine.
The following parameter names are recognized by the \fBparam\fP command,
but not all are supported by each device type.
Most commands deal only with half-duplex packet radio interfaces.
.TS
center box tab(;) ;
cB | cB | cB
l | n | l.
Name;Number;Meaning
_
Data;0;
TxDelay;1;Transmit keyup delay
Persist;2;P-persistence
SlotTime;3;Persistence slot time
TxTail;4;Transmit done holdup delay
FullDup;5;Enable/disable full duplex
Hardware;6;Hardware specific command
TxMute;7;Experimental transmit mute command
DTR;8;Control Data Terminal Ready (DTR) signal to modem
RTS;9;Control Request to Send (RTS) signal to modem
Speed;10;Line speed
EndDelay;11;
Group;12;
Idle;13;
Min;14;
MaxKey;15;
Wait;16;
Down;129;Drop modem control lines
Up;130;Raise modem control lines
Blind;131;
Return;255;Return a KISS TNC to command mode
.TE
.SP
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
param ax0 TxDelay 255
.ft P
.DE
will set the keyup timer
(type field = 1) on the KISS TNC configured as ax0 to 2.55
seconds (255 x .01 sec). On all asy interfaces (slip, ax25, kiss,
nrs) the \fBparam\fP \fIinterface\fP \fBSpeed\fP command allows the baud rate to
be read or set.
.P
The implementation of this command for the various interface
drivers is incomplete and subject to change.
.H 2 "ping" " [\fIsubcommand\fP]"
Without an argument,
display the Ping table,
which lists statistics on all active repetitive pings.
.H 3 "ping clear"
Stop all active repetitive pings, and clear the Ping table.
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
If \fIinterval\fP is specified,
pings will be repeated at the specified interval (in seconds)
until the \fBping clear\fP command is issued,
otherwise a single, "one shot" ping is done.
Responses to one-shot pings appear asynchronously on the command screen,
while responses from repetitive pings are stored in the Ping table.
.H 2 "ps"
Display all current processes in the system. The fields are as follows:
.VL 20 2
.LI \fBPID\fP
The process ID (the address of the process control block).
.LI \fBSP\fP
The current value of the process stack pointer.
.LI \fBstksize\fP
The size of the stack allocated to the process, measured in 16 bit words.
.LI \fBmaxstk\fP
The apparent peak stack utilization of this process,
measured in 16 bit words.
This is done in a somewhat heuristic fashion, so the numbers should be treated
as approximate. If this number reaches or exceeds the \fBstksize\fP figure,
the system is almost certain to crash,
and \fBWAMPES\fP should be recompiled
to give the process a larger allocation when it is started.
.LI \fBevent\fP
The event this process is waiting for, if it is not runnable.
.LI \fBfl\fP
The process status flags. There are two:
W (Waiting for event) and S (suspended - not currently used).
The W flag indicates that the process is waiting for an event.
Note that although there may be several runnable processes at any time
(shown in the \fBps\fP listing as those without the W flag)
only one process is actually running at any one instant.
.LI \fBin\fP
Always zero.
.LI \fBout\fP
Always zero.
.LI \fBname\fP
The name of the process.
.LE
.H 2 "record" " [off|\fIfilename\fP]"
Append to \fIfilename\fP all data received on the current session.
Data sent on the current session is also written into the file
except for Telnet sessions in remote echo mode.
The command \fBrecord off\fP stops recording and closes the file.
If no argument is specified the current status is displayed.
.H 2 "remote" " [-p \fIport\fP] [-k \fIkey\fP] [-a \fIkickaddr\fP] \fIhostid\fP exit|reset|kick"
Send a UDP packet to the specified host commanding it
to exit the \fBWAMPES\fP or \fBNOS\fP program, reset the processor,
or force a retransmission on TCP connections. For this
command to be accepted, the remote system must be running the \fBremote\fP
server, and the port number used by the local \fBremote\fP command must match
the port number used by the remote server.
If the port numbers do not match, or if the remote server is not running
on the target system, the command packet is ignored. Even if the
command is accepted there is no acknowledgement.
.P
If the \fB-p\fP option is not specified, port 1234 will be used.
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
The password is set on the server with the \fB-s\fP option,
and it is specified to the client with the \fB-k\fP option.
If no password is set with the \fB-s\fP option,
then the \fBexit\fP and \fBreset\fP subcommands are disabled.
.H 2 "rename" " \fIoldfilename\fP \fInewfilename\fP"
Rename \fIoldfilename\fP to \fInewfilename\fP.
.H 2 "repeat" " [\fImilliseconds\fP] \fIcommand\fP [\fIarguments\fP ...]"
Execute \fIcommand\fP every \fImilliseconds\fP,
or once every second if \fImilliseconds\fP is not specified.
Before each iteration the screen is cleared.
\fBrepeat\fP is terminated if any keyboard input is made,
or if \fIcommand\fP returns an error.
Example:
.DS I
.ft CW
repeat 2000 tcp status
.ft P
.DE
executes "tcp status" every two seconds.
.H 2 "reset" " [\fIsession#\fP]"
Reset the specified session, if no argument is given, reset the current
session. This command should be used with caution since it does not
reliably inform the remote end that the connection no longer exists. In
TCP a reset (RST) message will be automatically generated should the remote
TCP send anything after a local \fBreset\fP has been done. In AX.25 the DM
message performs a similar role. Both are used to get rid of a lingering
half-open connection after a remote system has crashed.
.H 2 "rip" " \fIsubcommand\fP"
These commands control the Routing Information Protocol (RIP) service.
.H 3 "rip accept" " \fIhostid\fP"
Remove the specified host from the RIP filter table, allowing future
broadcasts from that host to be accepted.
.H 3 "rip add" " \fIhostid\fP \fIseconds\fP [\fIflags\fP]"
Add an entry to the RIP broadcast table. The IP routing table will be sent
to \fIhostid\fP every interval of \fIseconds\fP. If
\fIflags\fP is specified as 1, then "split horizon" processing will
be performed
for this destination. That is, any IP routing table entries pointing to the
interface that will be used to send this update will be removed from the
update. If split horizon processing is not specified, then all routing
table entries except those marked "private" will be sent in each update.
Private entries are never sent in RIP packets.
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
broadcast address convention is used (eg. 44.255.255.255),
then chances are you already have the necessary IP routing table entry, but
unusual subnet or cluster-addressed networks may require special attention.
However, an \fBarp add\fP command will be required to translate this address to
the appropriate link level broadcast address. For example:
.DS I
.ft CW
arp add 44.255.255.255 ax25 QST-0
.ft P
.DE
for an AX.25 packet radio channel.
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
.TS
center tab(;) ;
lB lB lB lB lB lB lB lB
l l l l l l l l.
Dest;Len;Interface;Gateway;Metric;P;Timer;Use
1.2.3.4;32;ethernet0;128.96.1.2;1;0;0;0
1.2.3;24;ethernet0;128.96.1.2;1;0;0;0
.TE
.SP
then the first entry would be deleted as redundant since packets sent to
1.2.3.4 will still be routed correctly by the second entry. Note that the
relative metrics of the entries are ignored.
The default is \fBoff\fP.
.H 3 "rip refuse" " \fIhostid\fP"
Refuse to accept RIP updates from the specified host by adding the
host to the RIP filter table. It may be later removed with the
\fBrip accept\fP command.
.H 3 "rip request" " \fIhostid\fP"
Send a RIP Request packet to the specified host, causing it to reply
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
not local, the gateway's hostid should also be specified. If the interface
is a point-to-point link, then \fIgateway_hostid\fP may be omitted even if the
target is non-local because this field is only used to determine the
gateway's link level address, if any. If the destination is directly
reachable, \fIgateway_hostid\fP is also unnecessary since the destination
address is used to determine the link address.
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
routed via sl0 to 44.64.0.2, the remaining 24 bits are "don't-cares".
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
until their time-to-live (TTL) fields expire. Routing loops for
specific addresses can also be created, but this is less likely to occur
accidentally.
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
# The station with IP address 44.0.0.10 is on the local AX.25 channel.
route add 44.0.0.10 ax0
.ft P
.DE
.DS I
.ft CW
# The local Ethernet has an ARPA Class-C address assignment.
# Route all IP addresses beginning with 192.4.8 to it.
route add 192.4.8/24 ec0
.ft P
.DE
.DS I
.ft CW
# Route all default traffic to the gateway on the local Ethernet
# with IP address 192.4.8.1.
route add default ec0 192.4.8.1
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
Display the IP routing table entry which will be used to route to \fIhostid\fP.
.H 2 "session" " [\fIsession#\fP]"
Without arguments, display the list of current sessions,
including session number, remote TCP, AX.25, or NET/ROM address
and the address of the TCP, AX.25, or NET/ROM control block.
An asterisk (*) is shown next to the current session.
Entering a session number as an argument to the \fBsession\fP
command will put you in converse mode with that session.
.H 2 "shell" " \fIshell_command_line\fP"
Suspend \fBWAMPES\fP and execute a subshell (/bin/sh).
When the subshell exits, \fBWAMPES\fP resumes.
Background activity (FTP servers, etc) is also suspended
while the subshell executes.
.H 2 "smtp" " \fIsubcommand\fP"
These commands control the Simple Message Transport Protocol (SMTP) service
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
These commands control the Simple Network Time Protocol (SNTP) service.
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
adjtime system call will be used to slow down or to accelerate
the clock, otherwise the clock will be stepped using the
settimeofday system call.
The default is 1 second.
.H 3 "sntp sys"
Display the sntp server configuration.
.H 4 "sntp sys leap" " [\fIleap\fP]"
Display or set the sntp server leap indicator. This is a
two-bit code warning of an impending leap second to be
inserted/deleted in the last minute of the current day, coded as
follows:
.VL 20 2 1
.LI \fB0\fP
no warning
.LI \fB1\fP
last minute has 61 seconds
.LI \fB2\fP
last minute has 59 seconds)
.LI \fB3\fP
alarm condition (clock not synchronized)
.LE 1
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
.SP
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
.VL 20 2 1
.LI \fB0\fP
unspecified or unavailable
.LI \fB1\fP
primary reference (e.g., radio clock)
.LI \fB2\fP-\fB15\fP
secondary reference (via NTP or SNTP)
.LI \fB16\fP-\fB255\fP
reserved
.LE 1
The default is 1.
.H 3 "sntp trace" " [on|off]"
Display or set the flag controlling
sntp tracing.
The default is \fBoff\fP.
.H 2 "source" " \fIfilename\fP"
Read subsequent commands from \fIfilename\fP until EOF, then resume reading
commands from the previous stream. This can be used to keep routing statements
in a separate file, which can be read at some point in \fBnet.rc\fP.
.H 2 "start" " \fIserver\fP [\fIarguments\fP]"
Start the specified server, allowing remote connection requests.
.H 3 "start ax25"
Start the AX.25 Login server.
See also the \fBax25\fP and \fBlogin\fP commands.
.H 3 "start discard" " [\fIport\fP]"
Start the TCP DISCARD server.
Default \fIport\fP is 9.
See also RFC863.
.H 3 "start domain" " [\fIport\fP]"
Start the TCP/UDP DOMAIN server.
Default \fIport\fP is 53.
See also the \fBdomain\fP command, RFC1034, and RFC1035.
.H 3 "start echo" " [\fIport\fP]"
Start the TCP ECHO server.
Default \fIport\fP is 7.
See also RFC862.
.H 3 "start ftp" " [\fIport\fP]"
Start the TCP FTP server.
Default \fIport\fP is 21.
See also the \fBftp\fP command.
.H 3 "start tcpgate" " \fIport\fP [\fIhost\fP[:\fIservice\fP]]"
Start a \fBWAMPES\fP TCP to UNIX TCP gateway.
MORE TO BE WRITTEN.
.H 3 "start netrom"
Start the NET/ROM Login server.
See also the \fBnetrom\fP and \fBlogin\fP commands.
.H 3 "start rip"
Start the UDP RIP server on port 520.
See also the \fBrip\fP command and RFC1058.
.H 3 "start sntp"
Start the UDP SNTP server on port 123.
See also the \fBsntp\fP command and RFC1361.
.H 3 "start telnet" " [\fIport\fP]"
Start the TCP TELNET server.
Default \fIport\fP is 23.
See also the \fBlogin\fP command.
.H 3 "start time"
Start the UDP TIME server on port 37.
See also RFC868.
.H 3 "start remote" " [\fIport\fP]"
Start the UDP REMOTE server.
Default \fIport\fP is 1234.
See also the \fBremote\fP command.
.H 2 "status"
Display a one-line summary of each AX.25, NET/ROM, TCP, and UDP control block.
.H 2 "stop" " \fIserver\fP"
Stop the specified server, rejecting any further remote connect
requests. Existing connections are allowed to complete normally.
See also the \fBstart\fP command.
.P
\fIserver\fP has to be one of:
.BL 5 1
.LI
\fBax25\fP
.LI
\fBdiscard\fP
.LI
\fBdomain\fP
.LI
\fBecho\fP
.LI
\fBftp\fP
.LI
\fBnetrom\fP
.LI
\fBrip\fP
.LI
\fBsntp\fP
.LI
\fBtelnet\fP
.LI
\fBtime\fP
.LI
\fBremote\fP
.LE
.H 2 "tcp" " \fIsubcommand\fP"
These commands control the Transmission Control Protocol (TCP) service.
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
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
.H 3 "tcp reset" " \fItcb_addr\fP"
Delete the TCP control block at the specified address.
The control block address can be found with the \fBtcp status\fP command.
.H 3 "tcp rtt" " \fItcb_addr\fP \fImilliseconds\fP"
Replaces the automatically computed round trip time in the specified
TCP control block with the rtt in milliseconds.
This command is useful to speed up recovery from a series of lost packets
since it provides a manual bypass around the
normal backoff retransmission timing mechanisms.
.H 3 "tcp status" " [\fItcb_addr\fP]"
Without arguments, display several TCP-level statistics, plus a summary of
all existing TCP connections, including TCP control block address,
send and receive queue sizes, local and remote sockets, and connection state.
If \fItcb_addr\fP is specified, a more detailed dump of the specified
TCP control block is generated, including send and receive sequence numbers
and timer information.
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
See the \fBSetting Paclen, Maxframe, MTU, MSS and Window\fP chapter
for more information.
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
.VL 20 2 1
.LI \fB0001\fP
Trace output packets
.LI \fB0010\fP
Trace input packets
.SP
.LI \fB0000\fP
Decode headers
.LI \fB0100\fP
Print data (but not headers) in ASCII
.LI \fB0200\fP
Print headers and data in HEX and ASCII
.SP
.LI \fB1000\fP
Trace only packets addressed to me, suppress broadcasts
.LI \fB2000\fP
Print all packet bytes (input and output), no interpretation
.LE 1
Instead of defining the trace flags numerically one of the following
subcommands may be given:
.VL 20 2 1
.LI \fB-ascii\fP
Decode headers only
.LI \fB-broadcast\fP
Disable trace of broadcasts
.LI \fB-hex\fP
Print data (but not headers) in ASCII
.LI \fB-input\fP
Disable input trace
.LI \fB-output\fP
Disable output trace
.LI \fB-raw\fP
Disable raw packet dumps
.LI \fBascii\fP
Print data (but not headers) in ASCII
.LI \fBbroadcast\fP
Enable trace of broadcasts
.LI \fBhex\fP
Print headers and data in HEX and ASCII
.LI \fBinput\fP
Enable input trace
.LI \fBoff\fP
Turn off all trace output
.LI \fBoutput\fP
Enable output trace
.LI \fBraw\fP
Enable raw packet dumps
.LE 1
If \fItracefile\fP is not specified, tracing will be to the console.
.H 2 "udp" " \fIsubcommand\fP"
These commands control the User Datagram Protocol (UDP) service.
.H 3 "udp status"
Display several UDP-level statistics, plus a summary of
all existing UDP control blocks.
.H 2 "upload" " [stop|\fIfilename\fP]"
Open \fIfilename\fP and send it on the current session as though it were
typed on the terminal.
The command \fBupload stop\fP stops uploading and closes the file.
If no argument is specified the current status is displayed.
.nr Cl 2 \" Max level of header for table of contents
.H 1 "FTP Subcommands"
This section describes the commands recognized in converse mode
with a FTP session.
.P
All commands may be abbreviated.
You only need type enough of a command's name to distinguish it from others
that begin with the same series of letters.
Parameters, however, must be typed in full.
.H 2 "abort"
Abort a file transfer operation in progress.
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
Write the name of the remote working directory to standard output.
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
.nr Cl 7 \" Max level of header for table of contents
.H 1 "Setting Paclen, Maxframe, MTU, MSS and Window"
Many \fBWAMPES\fP users are confused by these parameters and do not know how to
set them properly. This chapter will first review these parameters and
then discuss how to choose values for them. Special emphasis is given to
avoiding interoperability problems that may appear when communicating
with non-\fBWAMPES\fP implementations of AX.25.
.H 2 "AX.25 Parameters"
.H 3 "Paclen"
Paclen limits the size of the data field in an AX.25 I frame. This
value does NOT include the AX.25 protocol header (source,
destination and digipeater addresses).
.P
Since unconnected-mode (datagram) AX.25 uses UI frames, this parameter
has no effect in unconnected mode.
.P
The default value of \fBpaclen\fP is 256 bytes.
.H 3 "Maxframe"
This parameter controls the number of I frames that \fBWAMPES\fP may send
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
TCP minus the unacknowledged data in flight, and the \fIcongestion window\fP,
an automatically computed time-varying estimate of how much
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
one per AX.25 I frame, for transmission and reassemble them into
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
always be set to 1. The reasons are explained in the paper
\fILink Level Protocols Revisited\fP by Brian Lloyd and Phil Karn,
which appeared in the
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
operates in datagram (UI frame) mode, the \fBpaclen\fP parameter does not
apply. The MTU effectively becomes the \fBpaclen\fP of the link. However,
as mentioned earlier, large packets sent on AX.25 \fIconnections\fP are
automatically segmented into I frames no larger than \fBpaclen\fP bytes.
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
I frame data field. If \fBpaclen\fP is 256, this leaves 236 bytes for the IP
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
