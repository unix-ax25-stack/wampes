#! /usr/bin/env python

# BBS/News/Mail Gateway @(#) $Id: bbs.py,v 4.1 2000/03/30 01:51:31 deyke Exp $

import StringIO
import nntplib
import os
import re
import rfc822
import smtplib
import string
import sys
import time

from bbsconf import *

#------------------------------------------------------------------------------

class revision:

	def __init__(self):
		(self.idtag, self.filename, self.number, self.date, self.time, self.author, self.state, self.trailer) = string.split("$Id: bbs.py,v 4.1 2000/03/30 01:51:31 deyke Exp $")

#------------------------------------------------------------------------------

def chomp(line):
	while line and (line[-1] == "\r" or line[-1] == "\n"): line = line[:-1]
	return line

#------------------------------------------------------------------------------

def userof(address):
	p = string.find(address, "@")
	if p < 0: return address
	return address[:p]

#------------------------------------------------------------------------------

def hostof(address):
	p = string.find(address, "@")
	if p < 0: return ""
	return address[p + 1:]

#------------------------------------------------------------------------------

def callvalid(name):
	from string import digits, letters
	if len(name) < 3 or len(name) > 6: return 0
	if name[0] in digits and name[1] in digits: return 0
	if not (name[1] in digits or name[2] in digits): return 0
	if not (name[-1] in letters): return 0
	numdigits = 0
	for char in name:
		if char in digits:
			numdigits = numdigits + 1
			if numdigits > 2: return 0
		elif char in letters:
			pass
		else:
			return 0
	return 1

#------------------------------------------------------------------------------

def userinpath(user, path):
	user = string.lower(user)
	for name in string.split(string.lower(path), "!"):
		if user == string.split(name, ".")[0]: return 1
	return 0

#------------------------------------------------------------------------------

def rfc822_date(gmt):
	return time.strftime("%a, %d %b %Y %H:%M:%S GMT", time.gmtime(gmt))

#------------------------------------------------------------------------------

def lock_file(filename):
	fd = os.open(filename, os.O_CREAT | os.O_RDWR, 0666)
	try:
		import fcntl
		fcntl.flock(fd, fcntl.LOCK_EX)
	except ImportError:
		import msvcrt
		msvcrt.locking(fd, 1, 1024)
	return fd

def unlock_file(fd):
	try:
		import msvcrt
		msvcrt.locking(fd, 0, 1024)
	except ImportError:
		pass
	os.close(fd)

#------------------------------------------------------------------------------

def generate_bid():
	fd = lock_file(BIDLOCKFILE)
	t = int(time.time())
	while t == int(time.time()): time.sleep(0.25)
	unlock_file(fd)
	bid = ""
	for i in range(6):
		bid = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"[t % 36] + bid
		t = t / 36
	shorthostname = string.split(myhostname, ".")[0][:6]
	return string.upper(shorthostname + (6 - len(shorthostname)) * "_" + bid)

#------------------------------------------------------------------------------

def forward_news():
	try:
		(response, curr_date, curr_time) = nntp_channel.date()
	except:
		gmt = time.gmtime(time.time() - 60)
		curr_date = "%02d%02d%02d" % (gmt[0] % 100, gmt[1], gmt[2])
		curr_time = "%02d%02d%02d" % (gmt[3], gmt[4], gmt[5])
	try:
		match = re.match(r"(\d\d\d\d\d\d) (\d\d\d\d\d\d)", open(NEWNEWSFILE + user).readline())
		(last_date, last_time) = match.groups()
	except:
		gmt = time.gmtime(time.time() - 7 * 24 * 60 * 60)
		last_date = "%02d%02d%02d" % (gmt[0] % 100, gmt[1], gmt[2])
		last_time = "%02d%02d%02d" % (gmt[3], gmt[4], gmt[5])
	(response, articles) = nntp_channel.newnews("*", last_date, last_time)
	for article in articles:
		(response, number, id, list) = nntp_channel.article(article)
		msgfile = StringIO.StringIO(string.join(list, "\n") + "\n")
		message = rfc822.Message(msgfile)
		if message.has_key("Path") and userinpath(user, message["Path"]): continue
		to = ""
		if message.has_key("Newsgroups"):
			for group in string.split(message["Newsgroups"], ","):
				if group[:9] == "ampr.bbs.":
					to = group[9:]
					if message.has_key("Distribution"):
						to = to + "@" + string.split(message["Distribution"], ",")[0]
					break
			messagetype = "B"
		elif message.has_key("To"):
			(fullname, to) = message.getaddrlist("To")[0]
			messagetype = "P"
		if not to: continue
		if message.has_key("X-BBS-Msg-Type"): messagetype = message["X-BBS-Msg-Type"]
		if message.has_key("From"): (fullname, frm) = message.getaddrlist("From")[0]
		else: frm = ""
		##### Special: translate "deyke" to "dk5sg" ????????
		bid = ""
		if message.has_key("Bulletin-ID"): bid = rfc822.unquote(message["Bulletin-ID"])
		elif message.has_key("BBS-Message-ID"): bid = rfc822.unquote(message["BBS-Message-ID"])
		elif message.has_key("Message-ID"):
			match = re.match(r"<(.*)\.bbs\.net>", message["Message-ID"])
			if match: bid = match.groups()[0]
		if not bid:
			if messagetype == "B": continue
			bid = generate_bid()
		if bid[0] == "$": bid = bid[1:]
		if message.has_key("Lifetime"): lifetime = message["Lifetime"]
		else: lifetime = ""
		if message.has_key("Subject"): subject = message["Subject"]
		else: subject = ""
		print "S" + messagetype,
		print userof(to),
		if hostof(to): print "@", hostof(to),
		if frm: print "<", userof(frm),
		if bid: print "$" + bid,
		if lifetime: print "#", lifetime,
		if string.lower(userof(to)) == "e" or string.lower(userof(to)) == "m":
			print subject,
		else:
			print
			response = raw_input()
			if not response or string.lower(response[0]) != "o": continue
			print subject
			for line in message.headers:
				if line[:4] == "X-R:": print "R:" + string.strip(line[4:])
			for line in msgfile.readlines(): print chomp(line)
		print "\032"
		line = raw_input()
		if string.find(line, ">") < 0: sys.exit()

		### Handle MIME messages

	open(NEWNEWSFILE + user, "w").write(curr_date + " " + curr_time + "\n")

#------------------------------------------------------------------------------

def parse_args(cmdlist, cmdchar):
	value = ""
	if cmdlist and cmdlist[0][0] == cmdchar and cmdlist[0] != cmdchar:
		value = cmdlist[0][1:]
		del cmdlist[0]
	elif len(cmdlist) >= 2 and cmdlist[0] == cmdchar:
		value = cmdlist[1]
		del cmdlist[0:2]
	return value

#------------------------------------------------------------------------------

def ssid_command(cmdlist):
	pass # Ignore for now

#------------------------------------------------------------------------------

def forward_command(cmdlist):
	fd = lock_file(FWDLOCKFILE + user)
	forward_news() ########### Needs some loop
	unlock_file(fd)
	######### Print "F" and return if there was some forward
	sys.exit()

#------------------------------------------------------------------------------

def send_command(cmdlist):
	cmd = cmdlist[0]
	del cmdlist[0]
	if len(cmd) >= 2 and string.lower(cmd[1]) != "e": messagetype = string.upper(cmd[1])
	else: messagetype = ""
	got_control_z = cmdlist and cmdlist[-1] == "\032"
	if got_control_z: del cmdlist[-1]
	if not cmdlist: raise RuntimeError, "Missing destination in SEND command"
	to = cmdlist[0]
	del cmdlist[0]
	host = parse_args(cmdlist, "@")
	if host: to = to + "@" + host
	frm = parse_args(cmdlist, "<")
	if frm:
		host = parse_args(cmdlist, "@")
		if host: frm = frm + "@" + host
	else:
		frm = user
	bid = parse_args(cmdlist, "$")
	if not bid: bid = generate_bid()
	mid = "<" + bid + "@bbs.net>"
	try: lifetime = int(parse_args(cmdlist, "#"))
	except: lifetime = ""
	if lifetime and lifetime < 1: lifetime = ""
	if lifetime: expires = int(time.time()) + int(lifetime) * 24 * 60 * 60
	else: expires = ""
	subject = string.join(cmdlist)
	host = hostof(to)
	usemail = not host or string.find(host, ".") >= 0 or callvalid(host)
	if usemail:
		response = smtp_channel.verify(to)[0]
		ok = (200 <= response <= 299)
	else:
		try:
			nntp_channel.stat(mid)
			ok = 0
		except nntplib.error_temp:
			ok = 1
	if not ok:
		if not got_control_z: print "No"
		return
	body = []
	if not got_control_z:
		print "OK"
		if not subject: subject = raw_input()
		while 1:
			try: line = raw_input()
			except EOFError: line = "\032"
			if line == "\032": break
			body.append(line)
	if not hostof(frm):
		host = myhostname
		for line in body:
			match = re.match(r"R:.*@:*(\S+)", line)
			if not match: break
			host = match.groups()[0]
		frm = frm + "@" + host
	date = int(time.time())
	for line in body:
		match = re.match(r"R:(\d\d)(\d\d)(\d\d)/(\d\d)(\d\d)", line)
		if not match: break
		date = int(time.mktime(map(int, match.groups()) + [0, 0, 0, 0]) - time.timezone)
	msg = ""
	msg = msg + "From: " + frm + "\n"
	if subject and string.lower(userof(to)) == "e":
		cancel_bid = "<" + subject + ".bbs.net>"
		msg = msg + "Control: cancel " + cancel_bid + "\n"
		msg = msg + "Subject: cmsg cancel " + cancel_bid + "\n"
	else:
		msg = msg + "Subject: " + subject + "\n"
	msg = msg + "Date: " + rfc822_date(date) + "\n"
	if expires: msg = msg + "Expires: " + rfc822_date(expires) + "\n"
	msg = msg + "Message-ID: " + mid + "\n"
	msg = msg + "Bulletin-ID: <" + bid + ">\n"
	if lifetime: msg = msg + "Lifetime: " + str(lifetime) + "\n"
	if messagetype: msg = msg + "X-BBS-Msg-Type: " + messagetype + "\n"
	gmt = time.gmtime(time.time())
	line = "X-R: %02d%02d%02d/%02d%02dZ @:%s [%s] WAMPES%s $:%s" % (gmt[0] % 100, gmt[1], gmt[2], gmt[3], gmt[4], string.upper(myhostname), mylocation, revision().number, bid)
	msg = msg + line + "\n"
	if not body: body.append("")
	inheader = 1
	for line in body:
		if inheader:
			if line[:2] == "R:":
				msg = msg + "X-R: " + line[2] + "\n"
			else:
				inheader = 0
				msg = msg + "\n" + line + "\n"
		else:
			msg = msg + line + "\n"
	if usemail:
		msg = "To: " + to + "\n" + msg
		try: smtp_channel.sendmail(frm, to, msg)
		except smtplib.SMTPException: pass
	else:
		path = ""
		for line in body:
			match = re.match(r"R:.*@:*(\S+)", line)
			if not match: break
			host = match.groups()[0]
			if path: path = path + "!" + host
			else: path = host
		if not path: path = "not-for-mail"
		msg = "Path: " + path + "\n" + msg
		if hostof(to): msg = "Distribution: " + hostof(to) + "\n" + msg
		msg = "Newsgroups: " + "ampr.bbs." + userof(to) + "\n" + msg
		try: ihave_channel.ihave(mid, StringIO.StringIO(msg))
		except nntplib.error_reply: pass

#------------------------------------------------------------------------------

def command_loop():
	while 1:
		print ">"
		comand_line = raw_input()
		if not comand_line: sys.exit() # End of input
		cmdlist = string.split(comand_line)
		if not cmdlist: continue # Empty command line
		cmdchar = string.lower(cmdlist[0][0])
		if cmdchar == "[": ssid_command(cmdlist)
		elif cmdchar == "f": forward_command(cmdlist)
		elif cmdchar == "s": send_command(cmdlist)
		else: raise RuntimeError, "Unknown command: " + cmdlist[0]

#------------------------------------------------------------------------------

import getpass ### Should be done better !!!!!!!!!!!!!!
user = getpass.getuser() ### Should be done better !!!!!!!!!!!!!!

nntp_channel = nntplib.NNTP(nntphost, nntpport)
nntp_channel.set_debuglevel(debuglevel)
try: nntp_channel.shortcmd("mode reader")
except: pass

ihave_channel = nntplib.NNTP(nntphost, nntpport)
ihave_channel.set_debuglevel(debuglevel)

smtp_channel = smtplib.SMTP(smtphost, smtpport)
smtp_channel.set_debuglevel(debuglevel)

print "[WAMPES-%s-DHM$]" % (revision().number)
command_loop()
