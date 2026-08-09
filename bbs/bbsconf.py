# BBS/News/Mail Gateway Configuration

import os

TCPDIR = os.environ.get("TCPDIR", "/tcp")

myhostname = "dk5sg.#bw.deu.eu"
mylocation = "Fort Collins"

nntphost = "deyke1.fc.hp.com"
nntpport = 119

smtphost = "deyke1.fc.hp.com"
smtpport = 25

debuglevel = 2 # Must be 0 for normal operation

LOCKDIR = TCPDIR + "/locks"
BIDLOCKFILE = LOCKDIR + "/bbs.bid"
FWDLOCKFILE = LOCKDIR + "/bbs.fwd." # Append name of host
NEWNEWSFILE = LOCKDIR + "/bbs.new." # Append name of host
