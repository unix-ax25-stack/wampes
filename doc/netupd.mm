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
.PF "^Net Update Reference Manual^-\\\\nP-^Version 950907" \" Page footer
.\"
.S 30
.ce
\fBNet Update Reference Manual\fP
.ce
Version 950907
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
.nr Ej 0 \" No new page for all headers
.H 1 "Prerequisites"
.AL
.LI
You need at least netupds/netupdc version 1.21 or higher
on both client and server machine before you can start.
Use
.DS I
.ft CW
what /tcp/util/netupd?
.ft P
.DE
or
.DS I
.ft CW
ident /tcp/util/netupd?
.ft P
.DE
to verify.
.LE
.H 1 "Server"
.AL
.LI
The following external UNIX programs (supporting the specified options)
have to be installed in either
/bin, /usr/bin, /usr/local/bin, or /usr/contrib/bin:
.BL
.LI
diff -n
.LI
gzip -9
.LI
compress
.LE
.LI
Add the following line to /etc/services:
.DS I
.ft CW
netupds 4715/tcp
.ft P
.DE
.LI
Add the following line to /etc/inetd.conf:
.DS I
.ft CW
netupds stream tcp nowait root /tcp/util/netupds netupds
.ft P
.DE
.LI
Reconfigure inetd using the following UNIX command:
.DS I
.ft CW
inetd -c
.ft P
.DE
.LI
Add the following line to /tcp/net.rc or /tcp/net.rc.local:
.DS I
.ft CW
start tcpgate netupds
.ft P
.DE
.LI
Restart \fBWAMPES\fP or execute the following command in \fBWAMPES\fP:
.DS I
.ft CW
start tcpgate netupds
.ft P
.DE
.LE
.H 1 "Client"
.AL
.LI
The following external UNIX programs (supporting the specified options)
have to be installed in either
/bin, /usr/bin, /usr/local/bin, or /usr/contrib/bin:
.BL
.LI
gzip -d
.LI
compress -d
.LE
.LI
Execute the following UNIX command:
.DS I
.ft CW
/tcp/util/netupdc \fIserver\fP \fIclient\fP
.ft P
.DE
where \fIserver\fP is the hostname of the server, and \fIclient\fP
is the hostname of the client. Please note that \fIclient\fP
may only consist of letters, digits, and the '-' character.
.SP
If \fIclient\fP is not specified, it defaults to the first part
(up to the first '.' character) of the client hostname (as
returned by the UNIX hostname command).
.SP
If \fIserver\fP is not specified, it defaults to "db0sao".
.LI
If you wish you can execute netupdc automatically from cron
by putting the command from 3.2 into your crontab.
Example crontab entry:
.DS I
.ft CW
0 8 * * * exec /tcp/util/netupdc db0sao dk5sg
.ft P
.DE
.LE
.TC 1 1 7 0
