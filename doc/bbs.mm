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
.PF "^BBS Reference Manual^-\\\\nP-^Version 951107" \" Page footer
.\"
.S 30
.ce
\fBBBS Reference Manual\fP
.ce
Version 951107
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
.nr Cl 2 \" Max level of header for table of contents
.H 1 "Start"
To start the BBS you must type 'bbs' after you have connected to the
system.
If your home directory contains the file '.bbsrc' it's contents will be
executed automatically. This allows the customization of the
\fBPROMPT\fP text, or the automatic execution of (for example)
\fBALIAS\fP, \fBSUBSCRIBE\fP, or \fBDIR\fP commands.
.H 1 "Command Input"
A command consists of a command name and optional arguments. Commands
are not case sensitive, and may be abbreviated. If the command
arguments contain spaces, they must be enclosed in either single or
double quotes. The maximum length of a command line is 2000 characters,
and the maximum number of arguments is 250.
.P
You can type more than one command on one line by separating the
commands with a ; (semicolon) character.
.H 1 "Interrupting the Output"
The output may be stopped by typing the 'interrupt' character. Normally
this character is defined to be DEL (ASCII character 127), however this
may be changed using the Unix command "stty intr" before starting the
BBS.
.H 1 "Commands"
.H 2 "ALIAS" " [\fIname\fP [\fItext\fP]]"
The \fBALIAS\fP command shows or sets command aliases. After a command
line is scanned, it is parsed into distinct commands and the first word
of each command, left-to-right, is checked to see if it has an alias.
If it does, it is replaced by the text which is the alias for that
command. To detect looping, the number of alias substitutions on a
single line is restricted to 20.
.P
To unset an alias make \fItext\fP the empty string (eg "").
.P
The following aliases are compiled into the BBS but can be unset or
redefined:
.P
.TS
tab(~) ;
l l.
BYE~"QUIT"
CHECK~"DIR"
ERASE~"DELETE"
EXIT~"QUIT"
KILL~"DELETE"
MAIL~"SHELL mailx"
MORE~"PIPE more"
NEWNEWS~"RELOAD; DIR"
PRINT~"READ"
RESPOND~"REPLY"
SB~"SEND"
SKIPGROUP~"MARK *; LIST"
SP~"SEND"
STATUS~"VERSION"
TYPE~"READ"
>~"WRITE"
?~"HELP"
!~"SHELL"
|~"PIPE"
.TE
.H 2 "DELETE" " \fIrange ...\fP"
The \fBDELETE\fP command deletes the specified articles from the current
group. Only articles written by the user may be deleted. See also
\fBRanges\fP.
.H 2 "DIR" " [\fIpattern ...\fP]"
The \fBDIR\fP command with no arguments shows all subscribed-to groups
containing one or more unread articles. If \fBDIR\fP is followed by one
or more patterns it shows all groups matching those patterns, counting
all articles (read or unread). See also \fBPatterns\fP and the
\fBSUBSCRIBE\fP and \fBUNSUBSCRIBE\fP commands.
.H 2 "DISCONNECT"
The \fBDISCONNECT\fP command ends the current BBS session, and then
tries to terminate the connection. If the BBS was started using a Unix
shell with job control (eg /bin/csh or /bin/ksh), \fBDISCONNECT\fP will
not be able to terminate the connection. In this case the user has to
enter 'exit' to the shell in order to disconnect.
.H 2 "GROUP" " [\fIgroup\fP]"
The \fBGROUP\fP command shows or sets the current group.
.H 2 "HELP" " [\fIcommand\fP|\fBALL\fP]"
The \fBHELP\fP command with no arguments prints a short list of all
possible BBS commands, \fBHELP\fP followed by \fIcommand\fP prints
help specific to that command, \fBHELP\fP \fBALL\fP prints the complete BBS
manual.
.H 2 "HEADERS" " [\fIflags\fP]"
The \fBHEADERS\fP command shows or sets the flags controlling the output
of article header lines by the \fBREAD\fP, \fBWRITE\fP, and \fBPIPE\fP
commands. \fIflags\fP contains one character for each header line to be
output:
.P
.TS
tab(;) ;
l l.
B;Bulletin-ID:
D;Date:
E;Expires:
F;From:
L;Lifetime:
M;Msg#:
P;Path:
R;R:
S;Subject:
T;To:
.TE
.P
The default for \fIflags\fP is "DFS".
.H 2 "INFO"
The \fBINFO\fP command prints the contents of the file
"/usr/local/lib/station.data", which may contain station information.
.H 2 "LIST" " [\fIpattern ...\fP]"
The \fBLIST\fP command with no arguments lists all unread articles in
the current group. If the current group does not contain any unread
articles, \fBLIST\fP will enter the next subscribed-to group containing
unread articles, make that the current group, and list all unread
articles in this new group. The \fBLIST\fP command followed by one or
more patterns shows all articles (read or unread) matching those
patterns. The patterns are matched against the Subject and From fields.
See also \fBPatterns\fP.
.H 2 "MARK" " [\fIrange ...\fP]"
The \fBMARK\fP command marks the current or the specified articles in
the current group as being read. See also \fBRanges\fP.
.H 2 "MAXAGE" " [\fImaxage\fP]"
The \fBMAXAGE\fP command shows or sets the maximum age of unread
articles. Whenever a group is listed with the \fBLIST\fP command, all
articles older than \fImaxage\fP days are automatically marked as being
read. To disable this feature set \fImaxage\fP to some large number (eg
99999). The default for \fImaxage\fP is 7 (days).
.H 2 "MYBBS" " \fImailbox\fP"
The \fBMYBBS\fP command stores \fImailbox\fP as the users home
mailbox. All incoming messages to this user will be
forwarded to this home mailbox. This \fBMYBBS\fP information will be
automatically transferred to other BBS systems.
.H 2 "PIPE" " \fIunix-command\fP [\fIrange ...\fP]"
The \fBPIPE\fP command with no \fIrange\fP arguments pipes the next
unread article in the current group to \fIunix-command\fP. If the
current group does not contain any unread articles, \fBPIPE\fP will
enter the next subscribed-to group containing unread articles, make that
the current group, and pipe the first unread article in this new group.
The \fBPIPE\fP command followed by one or more \fIranges\fP pipes the
specified articles in the current group. See also \fBRanges\fP.
.H 2 "PROMPT" " [\fIprompt-string\fP]"
The \fBPROMPT\fP command shows or sets the text that the BBS transmits
to indicate that it is ready for more user input. If
\fIprompt-string\fP contains space characters, it must be enclosed in
either single or double quotes. The following special character
sequences are recognized:
.P
.TS
tab(;) ;
l l.
\ec;current article number
\ed;current date
\eh;BBS hostname
\en;newline character
\et;current time
\eu;user name
\ew;current group
\exxx;character with octal code xxx
\e\e;backslash character
.TE
.P
The default \fIprompt-string\fP is "\ew:\ec > ".
.H 2 "QUIT"
The \fBQUIT\fP command exits the BBS program.
.H 2 "READ" " [\fIrange ...\fP]"
The \fBREAD\fP command with no \fIrange\fP arguments reads the next
unread article in the current group. If the current group does not
contain any unread articles, \fBREAD\fP will enter the next
subscribed-to group containing unread articles, make that the current
group, and read the first unread article in this new group. The
\fBREAD\fP command followed by one or more \fIranges\fP reads the
specified articles in the current group. See also \fBRanges\fP.
.H 2 "RELOAD"
The \fBRELOAD\fP command checks for new groups and articles.
.H 2 "REPLY" " [ALL] [\fIarticle-number\fP]"
The \fBREPLY\fP command sends a response to the current or the specified
article in the current group. If \fBALL\fP is used, the response will
be delivered to all recipients of the original message, otherwise it
will be sent to the author only.
.H 2 "SEND" " \fIrecipient\fP|\fIgroup\fP [@ \fImailbox\fP|\fIdistribution\fP] [< \fIsender\fP] [$\fIbulletin-id\fP] [# \fIlifetime\fP]"
The \fBSEND\fP command sends a personal mail to
\fIrecipient\fP@\fImailbox\fP, or posts an article to \fIgroup\fP with
distribution \fIdistribution\fP. Mail messages and articles are
forwarded to other BBS systems if necessary. Terminate message entry
with a line containing just ^Z (control Z), /EX, or ***END.
.H 2 "SHELL" " [\fIunix-command\fP]"
The \fBSHELL\fP command starts a Unix shell process. If a Unix command
is specified with the shell command, only this command will be executed,
and the BBS will be re-started when the command has completed. If the
Unix command contain spaces, it must be enclosed in either single or
double quotes.
.H 2 "SUBSCRIBE" " [\fIpattern ...\fP]"
The \fBSUBSCRIBE\fP command with no arguments subscribes to the current
group. If \fBSUBSCRIBE\fP is followed by one or more patterns it
subscribes to all groups matching those patterns. See also
\fBPatterns\fP.
.H 2 "UNMARK" " [\fIrange ...\fP]"
The \fBUNMARK\fP command marks the current or the specified articles in
the current group as NOT being read. See also \fBRanges\fP.
.H 2 "UNSUBSCRIBE" " [\fIpattern ...\fP]"
The \fBUNSUBSCRIBE\fP command with no arguments unsubscribes from the
current group. If \fBUNSUBSCRIBE\fP is followed by one or more patterns
it unsubscribes from all groups matching those patterns. See also
\fBPatterns\fP.
.H 2 "VERSION"
The \fBVERSION\fP command shows the current BBS version number, and some
status information.
.H 2 "WRITE" " \fIfilename\fP [\fIrange ...\fP]"
The \fBWRITE\fP command with no \fIrange\fP arguments writes (appends)
the next unread article in the current group to \fIfilename\fP. If the
current group does not contain any unread articles, \fBWRITE\fP will
enter the next subscribed-to group containing unread articles, make that
the current group, and write the first unread article in this new group.
The \fBWRITE\fP command followed by one or more \fIranges\fP writes the
specified articles in the current group. See also \fBRanges\fP.
.H 1 "Patterns"
Some BBS commands take patterns as arguments. The syntax of patterns is:
.P
.TS
tab(;) ;
l l.
*;Matches any string, including the null string.
?;Matches any single character.
[...];Matches any one of the enclosed characters.
;A pair of characters separated by - matches any
;character lexically between the pair, inclusive.
;The NOT operator ! can be specified immediately
;following the left bracket to match any single
;character not enclosed in the brackets.
\e;Removes any special meaning of the following character.
;Any other character matches itself.
.TE
.P
All pattern matches are case insensitive.
.H 1 "Ranges"
Some BBS commands take ranges as arguments. The syntax of ranges is:
.P
.TS
tab(;) ;
l l.
*;All articles.
-;All articles.
\fInumber\fP;The article numbered \fInumber\fP.
\fInumber\fP-;All articles from \fInumber\fP to the highest possible number.
-\fInumber\fP;All articles from the lowest possible number to \fInumber\fP.
\fInumber\fP-\fInumber\fP;All articles from the \fInumber\fP to \fInumber\fP.
.TE
.H 1 "S&F data flow"
.PS
reset
right
box invis "incoming" "BBS traffic"
arrow
BBSIN: box "bbs"
arrow "bulletins" above
INN: box "inn"
arrow
NEWSSPOOL: circle "news" "spool"
arrow
BBSOUT: box "bbs"
arrow
box invis "outgoing" "BBS traffic"
move to top of BBSOUT
up
move
UDBM: box "udbm"
arrow from NEWSSPOOL \
	to 1/3 of the way between UDBM.sw and UDBM.nw \
	chop circlerad chop 0
up
arrow <-> from top of INN
box invis "other news" "systems"
down
arrow <- from bottom of INN
GW: box "mail to" "news gw"
arrow <-
SENDMAIL: box "sendmail"
arrow <->
box invis "other mail" "systems"
left
arrow <- from left of SENDMAIL
NETIN: box "net"
arrow <-
box invis "incoming" "SMTP traffic"
right
arrow from right of SENDMAIL
MAILSPOOL: circle "mail" "spool"
arrow
NETOUT: box "net"
arrow
box invis "outgoing" "SMTP traffic"
arrow from 1/3 of the way between BBSIN.se and BBSIN.ne \
	to 1/3 of the way between SENDMAIL.nw and SENDMAIL.sw \
	"personal" "mail"
arrow from MAILSPOOL \
	to 1/3 of the way between BBSOUT.sw and BBSOUT.nw \
	chop circlerad chop 0
.PE
.TC 1 1 7 0
