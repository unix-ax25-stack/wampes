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
.PF "^Bridge Reference Manual^-\\\\nP-^Version 990918" \" Page footer
.\"
.S 30
.ce
\fBBridge Reference Manual\fP
.ce
Version 990918
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
.nr Ej 0 \" No new page for all headers
.H 1 "The bridge utility"
Usage: /tcp/util/bridge [-a] [-f \fIfailures\fP]
.SP
\fBbridge\fP allows multiple \fBnet\fP programs to communicate with each other.
.SP
One common problem for packet users is only having one AX.25 callsign
while using WAMPES. You have to run another copy of the \fBnet\fP program
if you want a second callsign for connects. Many people achieve this by
using a tty/pty pair to connect one \fBnet\fP to another, which isn't a very
nice method.
.SP
An easier solution is \fBbridge\fP.
.SP
\fBbridge\fP listens on a host tcp socket, and relays received packets like
a digipeater.
.SP
\fBbridge\fP may be started with the following, optional switches:
.SP
The \fB-a\fP switch relays received packets to all connections, even back to
the connection a packet was received from. This is useful
for certain forms of input stress testing.
.SP
The \fB-f\fP \fIfailures\fP switch (0 <= \fIfailures\fP <= 100) specifies
the percentage of packets dropped by \fBbridge\fP.
This is useful to test retry behavior.
The default is 0 (no packets are dropped).
.SP
\fBbridge\fP doesn't run in the background by itself, so don't forget to add
an '&' if you start it somewhere in your system startup files.
.SP
Once \fBbridge\fP is running the interface in \fBnet\fP is attached with this
command:
.SP
.DS I
.ft CW
# open 127.0.0.1, port 4713 where bridge is listening:
attach asy 7f000001 4713 kissui br0 0 256 19200
#
.ft P
.DE
.SP
The interface is named \fBbr0\fP here, but you can choose any name you like.
.SP
You can use the same attach command for multiple \fBnet\fP programs. Don't
forget to start each \fBnet\fP with
.SP
.DS I
.ft CW
net -g net.rc.\fIxxx\fP
.ft P
.DE
.SP
to prevent unlinking of the unix domain sockets, and using a different net.rc.
.TC 1 1 7 0
