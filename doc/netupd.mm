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
.PF "^Net Update Reference Manual^-\\\\nP-^Version 981206" \" Page footer
.\"
.S 30
.ce
\fBNet Update Reference Manual\fP
.ce
Version 981206
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
.nr Ej 0 \" No new page for all headers
.H 1 "Server"
.AL
.LI
The following external UNIX programs (supporting the specified
options) have to be installed in either /bin, /usr/bin,
/usr/local/bin, or /usr/contrib/bin:
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
netupds stream tcp nowait root /tcp/util/netupds netupds [-s \fIsourcedir\fP] [-n \fIuser\fP]
.ft P
.DE
Options:
.P
.VL 20 2
.LI "\fB-s\fP \fIsourcedir\fP"
Serve files from directory \fIsourcedir\fP. If not specified files
will be served from directory \fB/tcp\fP.
.LI "\fB-n\fP \fIuser\fP"
Send notification mail to \fIuser\fP. If not specified mail will be
send to \fBroot\fP.
.LE
.LI
Reconfigure inetd using the following UNIX command:
.DS I
.ft CW
inetd -c
.ft P
.DE
.LI
Add the following line to /tcp/net.rc:
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
The following external UNIX programs (supporting the specified
options) have to be installed in either /bin, /usr/bin,
/usr/local/bin, or /usr/contrib/bin:
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
/tcp/util/netupdc [-s \fIsourcedir\fP] [-w \fIwampesdir\fP] [-u] [-m] \fIserver\fP \fIclient\fP
.ft P
.DE
Options:
.VL 20 2
.LI "\fB-s\fP \fIsourcedir\fP"
Update files in directory \fIsourcedir\fP. If not specified files will
be updated in directory \fB/tcp\fP.
.LI "\fB-w\fP \fIwampesdir\fP"
Directory where \fBWAMPES\fP is installed. If not specified
\fBWAMPES\fP is expected in directory \fB/tcp\fP. Not used when
option \fB-u\fP is specified.
.LI \fB-u\fP
Use \fBUnix\fP networking to connect to \fIserver\fP. If not specified
\fBWAMPES\fP will be used to connect to \fIserver\fP.
.LI \fB-m\fP
Do \fBnot\fP start the \fBmake\fP program after the update.
.LI \fIserver\fP
Hostname of the net update server. If not specified this defaults to
\fBdb0sao\fP.
.LI \fIclient\fP
Hostname of this client. If not specified this defaults to the first
part (up to the first '.' character) of the client hostname (as
returned by the UNIX hostname command). Please note that \fIclient\fP
may only consist of letters, digits, and the '-' character.
.LE
.LI
If you wish you can execute netupdc automatically from cron by putting
the command from 2.2 into your crontab. Example crontab entry:
.DS I
.ft CW
0 8 * * * exec /tcp/util/netupdc db0sao dk5sg
.ft P
.DE
.LE
.TC 1 1 7 0
