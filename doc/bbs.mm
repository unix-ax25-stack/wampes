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
.PF "^BBS Reference Manual^-\\\\nP-^Version 950919" \" Page footer
.\"
.S 30
.ce
\fBBBS Reference Manual\fP
.ce
Version 950919
.S
.SP 2
.S 15
.ce
Dieter Deyke, DK5SG/N0PRA
.ce
deyke@fc.hp.com
.S
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
.H 1 "Flow of incoming BBS traffic"
.PS
reset
down
REPLY: box "REPLY"
move
SEND: box "SEND"
move
MYBBS: box "MYBBS"
right
arrow from right of SEND
ROUTE: box "route" "mail"
arrow
SEND_TO_MAIL_OR_NEWS: box "send to" "mail/news"
arrow from right of REPLY to 1/3 <ROUTE.nw,ROUTE.sw>
arrow from right of MYBBS to 1/3 <ROUTE.sw,ROUTE.nw>
arrow right up from 1/3 <SEND_TO_MAIL_OR_NEWS.ne,SEND_TO_MAIL_OR_NEWS.se>
arrow right down from 1/3 <SEND_TO_MAIL_OR_NEWS.se,SEND_TO_MAIL_OR_NEWS.ne>
.PE
.TC 1 1 7 0
