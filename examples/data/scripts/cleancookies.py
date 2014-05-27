#!/usr/bin/python2
# vim: fileencoding=utf-8

# This program is free software. It comes without any warranty, to
# the extent permitted by applicable law. You can redistribute it
# and/or modify it under the terms of the Do What The Fuck You Want
# To Public License, Version 2, as published by Sam Hocevar. See
# http://sam.zoy.org/wtfpl/COPYING for more details.

import getopt
import re
import sys
import time

ver = "0.1"
copyright = "Paweł Zuzelski <pawelz@pld-linux.org>"

# Functions
def version():
	print "cleancookies v. " + ver + " Copyright (c) " + copyright

def usage():
	print
	version()
	print
	print "cleancookies [-w FILE | -b FILE] [-c COOKIES]"
	print
	print "       -w FILE    - read whitelisted domains from FILE"
	print "       -b FILE    - read blacklisted domains from FILE"
	print "       -c COOKIES - read cookies from COOKIES file instead of stdin"
	print "       -h         - print this help"
	print "       -V         - print version"
	print
	print "If -w or -b option is specified, cleancookies treats each line of FILE as regular expression that cookie domain is matched against."
	print
	print "COOKIES file (or stabndard input) should be cookies.txt file as used by mozilla and reuzbl-cookie-daemon."
	print
	print "EXAMPLE: add following script to your ~/.xsession, or register it in crontab:"
	print
	print "tmp=$(mktemp)"
	print "wl=${XDG_CONFIG_HOME:-$HOME/.config}/reuzbl/whitelist"
	print "ck=${XDG_DATA_HOME:-$HOME/.config}/reuzbl/cookies.txt"
	print "cleancookies -w $wl -c $ck > $tmp"
	print "mv $tmp $ck"
	print "killall reuzbl-cookie-daemon && reuzbl-cookie-daemon -v start"
	print


# Parse cmdline options
try:
	opts, args = getopt.getopt(sys.argv[1:], "w:b:c:hV")
except getopt.GetoptError, err:
	print str(err)
	usage()
	sys.exit(2)

# Some global variables
mode = None
ckfile = sys.stdin
whitelist = []

for o, a in opts:
	if o == "-V":
		version()
		sys.exit()
	if o == "-h":
		usage()
		sys.exit()
	if o == "-w":
		mode = "w"
		lfile = a
	elif o == "-b":
		mode = "b"
		lfile = a
	elif o == "-c":
		ckfile = open(a, "r")

if mode:
	with open(lfile, "r") as file:
		for line in file:
			whitelist.append(line[:-1])

r = re.compile("|".join(whitelist))

class CookieLineIsComment(Exception):
	pass

class Cookie:
	"""Class representing cookie object"""

	def __init__(self, s):
		"""Initialize cookie object from cookies.txt line"""

		if len(s) < 5:
			raise CookieLineIsComment

		if s[0] == " ":
			raise CookieLineIsComment

		if s[0] == "#":
			raise CookieLineIsComment

		self.line = s[:-1]
		f = self.line.split("\t")

		self.domain = f[0]
		self.expire = f[4]
		self.key = f[5]
		self.value = f[6]
	
	def expired(self):
		"""Returns True if cookie is expired, False otherwise"""

		# If cookie has no expiration time set, return False
		if self.expire == "":
			return False

		# If cookie is expired, return True
		if int(self.expire) < int(time.strftime("%s")):
			return True

		# Otherwise return False
		return False

	def __str__(self):
		"""Returns string representation of cookie object (i.e. cookies.txt line)"""
		return self.line
	
for line in ckfile:
	# Parse cookie
	try:
		c = Cookie(line)
	except CookieLineIsComment:
		print line[:-1]
		continue

	# Remove expired cookies
	if c.expired():
		continue
	
	# Check blacklist/whitelist
	if mode:
		m = r.search(c.domain)
		if (m and mode == "w") or ((not m) and mode == "b"):
			print c
		continue

	# Finally print cookie
	print c
