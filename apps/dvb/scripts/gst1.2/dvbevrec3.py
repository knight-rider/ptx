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
import xdg.BaseDirectory
import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstMpegts', '1.0')
from gi.repository import GObject, Gst, GLib, GstMpegts

# GObject.threads_init()
Gst.init(None)

from optparse import OptionParser

class CLI_Main:

	def __init__(self):
		self.pipeline = Gst.Pipeline()
		dvb = Gst.ElementFactory.make("dvbbasebin", "dvbbasebin")
		tsparse = dvb.get_by_name("mpegtsparse2-0")
		tsparse.set_property("bcas", True)
		# For event-relay recordings to be properly cocatenated,
		#    we need to use SEGMENT_FORMAT_TIME.
		tsparse.set_property("set_timestamps", True)
		valve = Gst.ElementFactory.make("valve", "valve")
		valve.set_property("drop", True)
		fdsink = Gst.ElementFactory.make("fdsink", "fdsink")
		fdsink.set_property("sync", False)
		self.pipeline.add(dvb)
		self.pipeline.add(valve)
		self.pipeline.add(fdsink)
		dvb.link(valve)
		valve.link(fdsink)
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
		self.shrink_pat = False
		self.conffile = None
		self.found_ev = False
		self.len = None
		self.loop = None
		self.watch_pat = False
		bus = self.pipeline.get_bus()
		bus.add_signal_watch()
		bus.connect("message", self.on_message)

	def dprint(self, txt):
		if self.verbose:
			print(time.strftime("%Y-%m-%d %H:%M.%S ") + txt, file=sys.stderr, flush=True)

	def quit(self):
		self.pipeline.set_state(Gst.State.NULL)
		time.sleep(1)
		if self.loop and self.loop.is_running():
			self.loop.quit()

	def on_message(self, bus, message):
		t = message.type
		if t == Gst.MessageType.EOS:
			print(time.ctime() + "Finished by unexpected EOS.",
				file=sys.stderr)
			self.quit()
		elif t == Gst.MessageType.ERROR:
			(err, debug) = message.parse_error()
			print(time.ctime() + "Error: %s" % err, file=sys.stderr)
			self.quit()
		elif t == Gst.MessageType.ELEMENT:
			self.proc_eit(message)

	def is_target(self, present, tm, dur):
		self.dprint("search p/f:%s time:%s dur:%s..." % \
			(present, time.ctime(tm.to_unix()), dur))
		dur_undef = 165 * 3600 + 165 * 60 + 165 # FF:FF:FF as BCD
		if present:
			# if dur is un-defined (extending) or
			#   not ending too soon(60sec), then select this EIT/p.
			# extra 60sec margin is necessary, as this script can/should
			#  run shortly before the target event starts.
			if dur == dur_undef or tm.to_unix() + dur > time.time() + 60:
				return True
		else:
			# if this EIT/f is starting very soon, select this EIT/f
			#  but firstly check if start-time is undef (delaying): 1900-0-0-....
			if tm.get_year() != 1900 and tm.to_unix() < time.time() + 60:
				return True

		return False


	def proc_eit(self, message):
		sec = GstMpegts.message_parse_mpegts_section(message)
		if sec is None:
			return

		st = message.get_structure()
		if self.watch_pat and st.has_name("pat"):
			self.check_pat(sec)
			return

		if not st.has_name("eit"):
			return
		eit = sec.get_eit()
		if eit is None:
			return

		if not (eit.actual_stream and eit.present_following):
			return

		if sec.subtable_extension != self.svc_id:
			if self.ev_id is not None and not self.found_ev:
				self.check_ev_moved(eit, sec.subtable_extension)
			return

		try:
			event = eit.events[0]

			# NOTE: time in EIT message is adjusted to UTC,
			#       in accordance with the DVB standard.
			tm = event.start_time.to_g_date_time()
			dur = event.duration
			if sec.section_number > 1:
				self.dprint("broken EIT. no/bad section-number.");
				return;

			self.dprint("EIT. ID:%s sec:%s ver:%s" %
					(event.event_id,
					 sec.section_number,
					 sec.version_number))
			if self.ev_id is None and \
			   self.is_target(sec.section_number == 0, tm, dur):
				self.ev_id = event.event_id
				self.dprint("selected event:%s ..." % self.ev_id)

			if sec.section_number == 0:
				# "tsparse" filters out the same-versioned table
				self.eit_ver_present = sec.version_number
				self.eit_eid_present = event.event_id
				evname = None
				desc = GstMpegts.find_descriptor(event.descriptors,
				    GstMpegts.DVBDescriptorType.SHORT_EVENT)
				sh = desc.parse_dvb_short_event()
				if sh[0]:
					evname = sh[2]
				if evname is None or evname == "":
					self.eit_name_present = "???"
				else:
					self.eit_name_present = evname
			else:
				dur_undef = 165 * 3600 + 165 * 60 + 165 # FF:FF:FF as BCD
				if dur == dur_undef and tm.get_year() == 1900:
					return
				# "mpegtsparse" filters out the same-versioned table
				#if eit["version-number"] == self.eit_ver_following:
				#	return
				self.eit_ver_following = sec.version_number
				self.eit_eid_following = event.event_id
				evname = None
				desc = GstMpegts.find_descriptor(event.descriptors,
				    GstMpegts.DVBDescriptorType.SHORT_EVENT)
				sh = desc.parse_dvb_short_event()
				if sh[0]:
					evname = sh[2]
				if evname is None or evname == "":
					self.eit_name_following = "???"
				else:
					self.eit_name_following = evname

			# if event is still not identified, then return & wait subsequent EITs
			if self.ev_id is None:
				return

			if sec.section_number == 0 and \
			   self.ev_id == self.eit_eid_present:
				self.found_ev = True
				if self.ev_start is None and tm.get_year() != 1900:
					self.ev_start = tm.to_unix()
				self.valve(False)
				self.tsid = eit.transport_stream_id
				self.check_relay(eit)
				return
			elif sec.section_number == 1 and \
			     self.ev_id == self.eit_eid_following:
				self.found_ev = True
				if self.ev_start is None and tm.get_year() != 1900:
					self.ev_start = tm.to_unix()
				# check if the next event is close enough
				#  but exclude the case for the starting period of unplanned pause.
				#   in this case, the start time is set to the past.
				# (assert the updated EIT is the "following" one)
				now = time.time()
				if self.ev_start and \
				   self.ev_start >= now and self.ev_start - 5 <= now:
					self.valve(False)
				else:
					self.valve(True)
				self.tsid = eit.transport_stream_id
				self.check_relay(eit)
				return
			else:
				# check if in transient state for unplanned pause/break-ins

				# EITp has been updated first before EITf
				#     to a new event (!self.ev_id).
				# Wait for the EITf update and
				#   check if it == ev_id, i.e. pause/break-in.
				if self.recording and sec.section_number == 0 and \
				   self.eit_ver_present != self.eit_ver_following:
					self.valve(True)
					return

				# check if in state for initial waiting
				if not self.found_ev:
					return

				# EITf has been updated before EITp and
				#    self.ev_id != eid_following (new value) &
				#    self.ev_id != eid_present (old value).
				# Wait for the next EITp updated (to be ev_id).
				if sec.section_number == 1 and \
				   self.eit_ver_present != self.eit_ver_following and \
				   self.ev_id != self.eit_eid_present:
					return

				# wait until next EIT (section) update
				if self.ev_id == self.eit_eid_present or \
				   self.ev_id == self.eit_eid_following:
					return

				# check if a relayed-event exists
				if self.found_ev and self.next_eid:
					self.switch_ev()
					return

				self.valve(True)
				self.dprint("Finished recording.")
				self.quit()
				return
		except TypeError:
			self.dprint("broken EIT?. ignoring...");
			return;



	def check_ev_moved(self, eit, sid):
		events = eit.events
		if events is None or len(events) < 1:
			return
		# only EITp/f is fed to this func,
		# so just check if the (1st) event exists
		ev = events[0]
		if ev is None or ev.descriptors is None:
			return
		desc = GstMpegts.find_descriptor(ev.descriptors,
			GstMpegts.ISDBDescriptorType.EVENT_GROUP)
		if desc is None:
			return

		eg = desc.parse_event_group()
		if not eg[0]  or eg[1] is None or len(eg[1].events) < 1:
			return
		if eg[1].group_type != GstMpegts.EventGroupType.MOVED_FROM_INTERNAL:
			return

		for src in eg[1].events:
			if src.service_id == self.svc_id and src.event_id == self.ev_id:
				self.dprint("the evnet:{0}[svc:{1}] moved."
					" switching...".format(src.event_id, src.service_id))
				self.next_svcid = sid
				self.next_eid = ev.event_id
				self.next_tsid = None
				self.switch_ev(self)
				return
		return


	def check_relay(self, eit):
		events = eit.events
		if events is None or len(events) < 1:
			return
		# only EITp/f is fed to this func,
		# so just check if the (1st) event exists
		ev = events[0]
		if ev is None or ev.descriptors is None:
			return
		desc = GstMpegts.find_descriptor(ev.descriptors,
			GstMpegts.ISDBDescriptorType.EVENT_GROUP)
		if desc is None:
			return

		eg = desc.parse_event_group()
		if not eg[0]  or eg[1] is None or len(eg[1].events) < 1:
			return
		if eg[1].group_type == GstMpegts.EventGroupType.RELAYED_TO_INTERNAL or \
		   eg[1].group_type == GstMpegts.EventGroupType.RELAYED_TO:
			if eg[1].events[0] is None:
				self.dprint("broken EIT(ev-grp desc. type:2/4) received.")
				return
			self.next_svcid = eg[1].events[0].service_id
			self.next_eid = eg[1].events[0].event_id
			if eg[1].group_type == GstMpegts.EventGroupType.RELAYED_TO and \
			   eg[1].events[0].transport_stream_id > 0:
				self.next_tsid = eg[1].events[0].transport_stream_id
			else:
				self.next_tsid = None
			self.dprint("will be relayed to ev:{0.next_eid}"
				    "(svc:{0.next_svcid}) ts:{0.next_tsid}".
				    format(self))
		return


	def switch_ev(self):
		self.dprint("relaying to the next event {0.next_eid}"
			    "[{0.next_svcid}] ts:{0.next_tsid}.".format(self));
		self.valve(True)
		self.pipeline.set_state(Gst.State.READY)
		self.pipeline.get_state(Gst.CLOCK_TIME_NONE)

		self.found_ev = False
		self.eit_ver_present = None
		self.eit_eid_present = None
		self.eit_ver_following = None
		self.eit_eid_following = None
		self.ev_id = self.next_eid
		self.next_eid = None
		self.svc_id = self.next_svcid
		self.next_svcid = None
		self.tsid = self.next_tsid
		self.next_tsid = None

		vpad = self.pipeline.get_by_name("valve").get_static_pad("sink")
		dvb = self.pipeline.get_by_name("dvbbasebin")
		if self.shrink_pat:
			dpad = vpad.get_peer()
			dpad.unlink(vpad)
			dvb.release_request_pad(dpad)

		if self.tsid is not None: # relay to another TS
			ch = check_channel(None, self.svc_id, self.conffile)[0]
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
				print(time.ctime() + \
					"Failed to find the channel name for sid:" \
					"{0:d} in channels.conf".format(self.svc_id),
					file=sys.stderr)
				self.quit()
				return
			dvb.set_uri("dvb://%s" % ch)
			dvb.set_property("tune", None)

		dvb.set_property("program-numbers", self.svc_id)

		if self.shrink_pat:
			dpad = dvb.get_request_pad("program_{:d}".
							format(self.svc_id))
			dpad.link(vpad)
		self.pipeline.set_state(Gst.State.PLAYING)
		return

	def check_pat(self, sec):
		if sec is None:
			return
		progs = sec.get_pat()
		if progs is None:
			return

		for p in progs:
			if p.program_number == self.svc_id:
				break;
		else:
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
			self.dprint("Start recording event:%d (title: %s)..." % \
			     (self.ev_id, title))
			      #title.encode(locale.getpreferredencoding(), 'replace')))
			if self.len is not None and self.len > 0:
				GLib.timeout_add_seconds(60 * self.len, self.on_stop)
# for debug
#				GLib.timeout_add_seconds(self.len, self.on_stop)
				self.len = None

	def on_stop(self):
		self.valve(True)
		self.dprint("Finished recording by timer.")
		self.quit()
		return False

	def on_timer(self):
		if self.found_ev:
			return False
		print(time.ctime() + "Cannot find the event. giving up...",
			file=sys.stderr)
		self.quit()
		return False

	def set_timer(self, wait):
		if wait is None or wait <= 0:
			wait = 30
		GLib.timeout_add_seconds(wait * 60, self.on_timer)

	def reconnect(self):
		dvb = self.pipeline.get_by_name("dvbbasebin")
		vpad = self.pipeline.get_by_name("valve").get_static_pad("sink")
		dpad = vpad.get_peer().unlink(vpad)
		dpad = dvb.get_request_pad("program_{:d}".format(self.svc_id))
		dpad.link(vpad)
		self.dprint("Using dvbbasebin.program_{:d}".format(self.svc_id))

#
# helper func.
#
def check_channel(chname, progid, confname):
	if confname is None:
		if os.environ.get('GST_DVB_CHANNELS_CONF'):
			confname = os.environ['GST_DVB_CHANNELS_CONF']
		else:
			confname = os.path.join (xdg.BaseDirectory.xdg_config_home,
				"gstreamer-%d.0" % Gst.version()[0],
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
					" [default:~/.config/gstreamer-%d.0/"
					"dvb-channels.conf]" % Gst.version()[0])

parser.add_option("--shrink_pat",
					action="store_true",
					help="rewrite PAT to contain just one program. [default: False]")

(options, args) = parser.parse_args()
mainclass = None
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
				os.makedirs(d, 0o755)
			f = open(options.filename, 'wb')
			mainclass.pipeline.get_by_name("fdsink").set_property("fd", f.fileno())
		except:
			parser.error("file:%s cannot be opened for writing", options.filename)
	mainclass.conffile = options.filename

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
			options.wait = 5

	if options.eventid and (ev_id < 0 or ev_id >= 0x10000):
		parser.error("invalid event_id:%d." % ev_id)
	mainclass.ev_id = ev_id

	(ch_name, sid) = check_channel(options.channel, options.serviceid, options.conf)
	if ch_name is None or sid is None:
		parser.error("(channel:%s, service_id:%s) is not a valid combination."
			% (options.channel, options.serviceid))
	mainclass.svc_id = sid
	mainclass.shrink_pat = options.shrink_pat
	if options.shrink_pat:
		mainclass.reconnect()
	mainclass.pipeline.get_by_name("dvbbasebin").set_uri("dvb://%s" % ch_name)

	loop = GLib.MainLoop()
	mainclass.loop = loop
	mainclass.pipeline.set_state(Gst.State.PLAYING)
	mainclass.dprint("Waiting for the EIT (ID:%s) of ch:%s to be received..." % (ev_id, ch_name))
	mainclass.set_timer(options.wait)
	loop.run()
finally:
	if mainclass:
		mainclass.quit()
