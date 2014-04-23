#!/usr/bin/env python
# track a specified event (== program) and record it.
#   temporal suspend/delay of the event is supported
# if run with -c option, service_id arg can be set dummy(>0xFFFF)
#  In this case, the recording timings can be mistaken with those of the
#   another event which shares the same eventID in the TS.

import calendar
import sys, time
import locale
import os
import os.path
import glib, gobject
gobject.threads_init()
import pygst
pygst.require("0.10")
from optparse import OptionParser

class CLI_Main:

	def __init__(self):
		self.pipeline = gst.Pipeline("pipeline")
		dvb = gst.element_factory_make("dvbbasebin", "dvbbasebin")
		valve = gst.element_factory_make("valve", "valve")
		valve.set_property("drop", True)
		fdsink = gst.element_factory_make("fdsink", "fdsink")
		fdsink.set_property("sync", False)
		self.pipeline.add(dvb, valve, fdsink)
		dvb.get_request_pad("src0")
		gst.element_link_many(dvb, valve, fdsink)
		self.eit_ver_present = None
		self.eit_eid_present = None
		self.eit_ver_following = None
		self.eit_eid_following = None
		self.ev_id = None
		self.next_eid = None
		self.ev_start = None
		self.svc_id = None
		self.next_svcid = None
		self.tsid = None
		self.next_tsid = None
		self.recording = False
		self.verbose = False
		self.found_ev = False
		self.len = None
		self.loop = None
		self.watch_pat = False
		bus = self.pipeline.get_bus()
		bus.add_signal_watch()
		bus.connect("message", self.on_message)

	def dprint(self, txt):
		if self.verbose:
			print >> sys.stderr, time.strftime("%Y-%m-%d %H:%M.%S"), txt

	def quit(self):
		self.pipeline.set_state(gst.STATE_NULL)
		time.sleep(1)
		if self.loop and self.loop.is_running():
			self.loop.quit()

	def on_message(self, bus, message):
		t = message.type
		if t == gst.MESSAGE_EOS:
			print >> sys.stderr, time.ctime(), \
				"Finished by unexpected EOS."
			self.quit()
		elif t == gst.MESSAGE_ERROR:
			(err, debug) = message.parse_error()
			print >> sys.stderr, time.ctime(), "Error: %s" % err
			self.quit()
		elif t == gst.MESSAGE_ELEMENT:
			self.proc_eit(message)

	def is_target(self, present, tm, dur):
		self.dprint("search %s %s %s..." % (present, tm, dur))
		dur_undef = 165 * 3600 + 165 * 60 + 165 # FF:FF:FF as BCD
		if present:
			# if dur is undefined (extending) and
			#    EIT/f has been already recved: eit_eid_folloing != None
			#      and was not set as target: self.ev_id != eit_eid_following
			#  then set this EIT/p as the target
			#    if EIT/f is not recved yet, then defer the decision
			#    until the next EIT/f  by leaving self.ev_id None for now
			if dur == dur_undef and self.eit_eid_following:
				return True

			# if dur is defined (thus end-time is defined) and
			#   not ending too soon(120sec), then select this EIT/p
			# extra 120sec margin is necessary as this script can/should be
			#  run shortly before the target event starts.
			if dur != dur_undef and calendar.timegm(tm) + dur > time.time() + 120 \
			    and self.eit_eid_following:
				return True
		else:
			# if this EIT/f is starting very soon, select this EIT/f
			#  but firstly check if start-time is undef (delaying): 1900-0-0-....
			if tm.tm_year != 1900 and calendar.timegm(tm) < time.time() + 120:
				return True

		return False


	def proc_eit(self, message):
		if message.structure is None:
			return
		eit = message.structure
		if self.watch_pat and eit.get_name() == "pat":
			self.check_pat(eit)
			return

		if eit.get_name() != "eit" or not eit.has_key("section-number"):
			return

		if not (eit["actual-transport-stream"] and eit["present-following"]):
			return

		if eit["service-id"] != self.svc_id:
			if self.ev_id is not None and not self.found_ev:
				self.check_ev_moved(eit)
			return

		eit.get_field_type("events")
		if eit["events"] is None or len(eit["events"]) < 1:
			return
		events = eit["events"]

		# NOTE: time in EIT message is adjusted to UTC,
		#       in accordance with the DVB standard. 
		tm = time.struct_time([
		  events[0]["year"],
		  events[0]["month"],
		  events[0]["day"],
		  events[0]["hour"],
		  events[0]["minute"],
		  events[0]["second"],
		  0, 1, 0])
		dur = events[0]["duration"]
		if not events[0].has_key('name'):
			events[0]["name"] = "???"

		if self.ev_id is None and \
		   self.is_target(eit["section-number"] == 0, tm, dur):
			self.ev_id = events[0]["event-id"]
			self.dprint("selected event:%s ..." % self.ev_id)

		if eit["section-number"] == 0:
			# "mpegtsparse" filters out the same-versioned table
			#if eit["version-number"] == self.eit_ver_present:
			#	return
			self.eit_ver_present = eit["version-number"]
			self.eit_eid_present = events[0]["event-id"]
			self.eit_name_present = events[0]["name"]
		else:
			dur_undef = 165 * 3600 + 165 * 60 + 165 # FF:FF:FF as BCD
			if dur == dur_undef and tm.tm_year == 1900:
				return
			# "mpegtsparse" filters out the same-versioned table
			#if eit["version-number"] == self.eit_ver_following:
			#	return
			self.eit_ver_following = eit["version-number"]
			self.eit_eid_following = events[0]["event-id"]
			self.eit_name_following = events[0]["name"]

		#self.dprint("EIT section:%d eid:%d svcid:%d ver:%d ev_num:%d" %
		#		(eit["section-number"], events[0]["event-id"],
		#		 eit["service-id"], eit["version-number"], len(events)))

		if self.eit_eid_present and self.eit_eid_following and \
		   self.ev_id is None:
			self.ev_id = self.eit_eid_present
			self.dprint("selected event:%s ..." % self.ev_id)

		# if event is still not identified, then return & wait subsequent EITs
		if self.ev_id is None:
			return

		if self.ev_id == self.eit_eid_present:
			self.found_ev = True
			if self.ev_start is None:
				self.ev_start = calendar.timegm(tm)
			self.valve(False)
			self.tsid = eit["transport-stream-id"]
			self.check_relay(eit)
			return
		elif self.ev_id == self.eit_eid_following:
			self.found_ev = True
			if self.ev_start is None and tm.tm_year != 1900:
				self.ev_start = calendar.timegm(tm)
			# check if the next event is close enough
			#  but exclude the case for the starting period of unplanned pause.
			#   in this case, the start time is set to the past.
			# (assert the updated EIT is the "following" one)
			now = time.time()
			if eit["section-number"] == 1 and self.ev_start and \
			   self.ev_start >= now and self.ev_start - 5 <= now:
				self.valve(False)
			else:
				self.valve(True)
			self.tsid = eit["transport-stream-id"]
			self.check_relay(eit)
			return
		else:
			# check if in transient state for unplanned pause/break-ins
			if self.recording and eit["section-number"] == 0 and \
			   self.eit_ver_present != self.eit_ver_following:
				self.valve(True)
				return

			# check if in state for initial waiting
			if not self.found_ev:
				return

			# check if a relayed-event exists
			if self.recording and self.next_eid:
				self.switch_ev()
				return

			self.valve(True)
			self.dprint("Finished recording.")
			self.quit()
			return


	def check_ev_moved(self, eit):
		if not eit.has_key("events") or len(eit["events"]) < 1:
			return
		# only EITp/f is fed to this func,
		# so just check if the (1st) event exists
		ev = eit["events"][0]
		if ev is not None and ev.has_key("descriptors"):
			desc = ev["descriptors"]
		else:
			return

		for d in desc:
			if d[0] != '\xD6' or len(d) < 7:
				continue
			if (ord(d[2]) & 0xf0) >> 4 != 3:
				return
			n = ord(d[2]) & 0x0f
			l = ord(d[1])
			if l + 2 != len(d) or l < 1 + n * 4:
				self.dprint("received a broken EIT.")
				return
			i = 0
			while i < n:
				s = ord(d[3 + i * 4 + 0]) << 8 | ord(d[3 + i * 4 + 1])
				e = ord(d[3 + i * 4 + 2]) << 8 | ord(d[3 + i * 4 + 3])
				if s == self.svc_id and e == self.ev_id:
					self.dprint("the evnet:{0}[0x{1:04x}] moved."
						" switching...".format(e, s))
					self.next_svcid = eit["service-id"]
					self.next_eid = ev["event-id"]
					self.next_tsid = None
					self.switch_ev(self)
					return
				i = i + 1 
			return
		return


	def check_relay(self, eit):
		if not eit.has_key("events") or len(eit["events"]) < 1:
			return
		# only EITp/f is fed to this func,
		# so just check if the (1st) event exists
		ev = eit["events"][0]
		if ev is not None and ev.has_key("descriptors"):
			desc = ev["descriptors"]
		else:
			return

		for d in desc:
			if d[0] != '\xD6' or len(d) < 7:
				continue
			l = ord(d[1])
			t = (ord(d[2]) & 0xf0) >> 4
			n = ord(d[2]) & 0x0f
			if l + 2 != len(d) or l < 1 + n * 4:
				self.dprint("broken EIT(ev-grp desc.) received.")
				return
			if t == 2:
				if n < 1:
					self.dprint("broken EIT(ev-grp desc. type:2) received.")
					return
				self.next_svcid = ord(d[3]) << 8 | ord(d[3 + 1])
				self.next_eid = ord(d[3 + 2]) << 8 | ord(d[3 + 3])
				self.next_tsid = None
			elif t == 4:
				i = 3 + n * 4
				if l + 2 < i + 8:
					self.dprint("broken EIT(ev-grp desc. type:4) received.")
					return
				# todo: check all the destination TS's and
				#       select the receive-able one
				ts = ord(d[i + 2]) << 8 | ord(d[i + 3])
				self.next_tsid = None if self.tsid == ts else ts
				self.next_svcid = ord(d[i + 4]) << 8 | ord(d[i + 5])
				self.next_eid = ord(d[i + 6]) << 8 | ord(d[i + 7])
			return
		return


	def switch_ev(self):
		self.valve(True)
		self.pipeline.set_state(gst.STATE_PAUSED)
		self.dprint("relaying to the next event {0}[0x{1:04x}]." \
				.format(self.next_svcid, self.next_eid));
		self.found_ev = False
		self.eit_ver_present = None
		self.eit_eid_present = None
		self.eit_ver_following = None
		self.eit_eid_following = None
		self.ev_id = self.next_eid;
		self.next_eid = None
		self.svc_id = self.next_svcid
		self.next_svcid = None
		self.tsid = self.next_tsid
		self.next_tsid = None

		if self.tsid is not None: # relay to another TS
			fname = None if options is None else options.confname
			ch = check_channel(None, self.svc_id, fname)[0]
			if ch is None:
				# maybe relayed to a temporary service
				self.watch_pat = True
				# guess the primary permanent service
				if self.tsid & 0xf800 == 0x7800: # terrestrial
					sid = self.svc_id / 8 * 8
				else: # BS
					sid = self.svc_id / 10 * 10
				ch = check_channel(None, sid, fname)[0]
			if ch is None:
				print >> sys.stderr, time.ctime(), \
					"Failed to find the channel name for sid:" \
					"{0:d} in channels.conf".format(self.svc_id)
				self.quit()
				return
			self.pipeline.get_by_name("dvbbasebin").set_uri("dvb://%s" % ch)

		self.pipeline.get_by_name("dvbbasebin").set_property("program-numbers", self.svc_id)
		self.pipeline.set_state(gst.STATE_PLAYING)
		return

	def check_pat(self, pat):
		progs = pat["programs"]
		if progs is None:
			return
		found = False;
		for p in progs:
			if p["program-number"] == self.svc_id:
				found = True;
				break;

		if not found: # not found
			self.valve(True)
			self.dprint("Finished recording due to the end of service.")
			self.quit()
		return


	def valve(self, drop):
		if self.recording != drop:
			return
		self.pipeline.get_by_name("valve").set_property("drop", drop)
		self.recording = not drop
		if drop:
			self.dprint("Paused recording...")
		else:
			if self.ev_id == self.eit_eid_present:
				title = self.eit_name_present 
 			else:
				title = self.eit_name_following
			self.dprint("Start recording evend:%d (title: %s)..." % (self.ev_id, title.encode(locale.getpreferredencoding(), 'replace')))
			if self.len is not None and self.len > 0:
				glib.timeout_add_seconds(60 * self.len, self.on_stop)
# for debug
#				glib.timeout_add_seconds(self.len, self.on_stop)
				self.len = None

	def on_stop(self):
		self.valve(True)
		self.dprint("Finished recording by timer.")
		self.quit()
		return False

	def on_timer(self):
		if self.found_ev:
			return False
		print >> sys.stderr, time.ctime(), \
			"Cannot find the event. giving up..."
		self.quit()
		return False

	def set_timer(self, wait):
		if wait is None or wait <= 0:
			wait = 30
		glib.timeout_add_seconds(wait * 60, self.on_timer)


#
# helper func.
#
def check_channel(chname, progid, confname):
	if confname is None:
		if os.environ.has_key('GST_DVB_CHANNELS_CONF'):
			confname = os.environ['GST_DVB_CHANNELS_CONF']
		else:
			confname = os.path.join(os.environ['HOME'],
				".gstreamer-%d.%d" % gst.version()[0:2],
				"dvb-channels.conf")

	with open(confname) as f:
		for line in f:
			if line.startswith('#'):
				continue
			items = line.strip().split(':')
			if len(items) < 3:
				continue
			if chname and chname != items[0]:
				continue
			elif progid and progid != int(items[-1], 0):
				continue
			return (items[0], int(items[-1], 0));

	return (None, None)

#
# start of the main()
#
usage = "usage: %prog [options] {-c CH_NAME | -s SERVICE_ID} -o OUTFILE"

parser = OptionParser(usage=usage)
parser.add_option("-v", "--verbose",
					action="store_true",
					help="output informational messages. [default: False]")

parser.add_option("-c", "--channel",
					metavar="NAME",
					help="channel name of the recording event. "
							" [default: derived from service ID & conf-file]")
parser.add_option("-s", "--serviceid",
					metavar="ID", type="int",
					help="service_ID (program_ID) of the recording event. "
							" [default: derived from channel name & conf-file]")

parser.add_option("-e", "--eventid",
					metavar="ID", type="int",
					help="event_ID which identifies the recording event. "
							" [default: the current event on air]")

parser.add_option("-w", "--wait",
					metavar="TIME", type="int",
					help="max wait (in min.) until the event is found in EIT."
							" [default: 60 if eventID is specified, 3 otherwise]")

parser.add_option("-l", "--length",
					metavar="TIME", type="int",
					help="recording length (in min.) [default: whole event]")

parser.add_option("-o", "--output",
					dest="filename", metavar="FILE",
					help="write output to FILE. [mandatory]")

parser.add_option("-a", "--adapter",
					metavar="NUM", type="int",
					help="use DVB adapter #NUM. [default: 0]")
#parser.add_option("-d", "--demuxer",
#					metavar="NUM", type="int",
#					help="use DVB demuxer #NUM. [default: 0]")
parser.add_option("-f", "--frontend",
					metavar="NUM", type="int",
					help="use DVB frontend #NUM. [default: 0]")

parser.add_option("--conf",
					metavar="FILE",
					help="path to the channel config file. "
					" [default:~/.gstreamer-%d.%d/dvb-channels.conf]"
#							 % gst.version()[0:2])
							 % (0, 10))

(options, args) = parser.parse_args()
import gst

try:
	mainclass = CLI_Main()

	if options.channel is None and options.serviceid is None:
		parser.error("at least -c or -s option must be specified.")

	mainclass.verbose = options.verbose
	mainclass.len = options.length

	if options.filename:
		try:
			d = os.path.dirname(options.filename);
			if d != "" and not os.path.isdir(d):
				os.makedirs(d, 0755)
			f = open(options.filename, 'wb')
			mainclass.pipeline.get_by_name("fdsink").set_property("fd", f.fileno())
		except:
			parser.error("file:%s cannot be opened for writing", options.filename)

	if options.adapter:
		mainclass.pipeline.get_by_name("dvbbasebin").set_property("adapter", options.adapter)
	if options.frontend:
		mainclass.pipeline.get_by_name("dvbbasebin").set_property("frontend", options.frontend)

	if options.eventid:
		ev_id = options.eventid
		if options.wait is None:
			options.wait = 60
	else:
		ev_id = None
		if options.wait is None:
			options.wait = 3

	if options.eventid and ev_id < 0 or ev_id >= 0x10000:
		parser.error("invalid event_id:%d." % ev_id)
	mainclass.ev_id = ev_id

	(ch_name, sid) = check_channel(options.channel, options.serviceid, options.conf)
	if ch_name is None or sid is None:
		parser.error("(channel:%s, service_id:%s) is not a valid combination."
			% (options.channel, options.serviceid))
	mainclass.svc_id = sid
	mainclass.pipeline.get_by_name("dvbbasebin").set_uri("dvb://%s" % ch_name)

	loop = glib.MainLoop()
	mainclass.loop = loop
	mainclass.pipeline.set_state(gst.STATE_PLAYING)
	mainclass.dprint("Waiting for the EIT (ID:%s) of ch:%s to be received..." % (ev_id, ch_name))
	mainclass.set_timer(options.wait)
	loop.run()
finally:
	if mainclass:
		mainclass.quit()
