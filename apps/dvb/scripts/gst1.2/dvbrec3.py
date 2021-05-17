#!/usr/bin/env python
# record a TS stream from a DVB device
#

import os, os.path
import sys, time
import threading
import xdg.BaseDirectory
import gi
gi.require_version('Gst', '1.0')
from gi.repository import GObject, Gst, GLib

# GObject.threads_init()
Gst.init(None)

from optparse import OptionParser

class CLI_Main:

	def __init__(self):
		self.pipeline = Gst.Pipeline()
		dvb = Gst.ElementFactory.make("dvbbasebin", "dvbbasebin")
		fdsink = Gst.ElementFactory.make("fdsink", "fdsink")
		fdsink.set_property("sync", False)
		self.pipeline.add(dvb)
		self.pipeline.add(fdsink)
		# dvb.get_request_pad("src0")
		dvb.link(fdsink)
		tsparse = dvb.get_by_name("mpegtsparse2-0")
		tsparse.set_property("bcas", True)
		self.verbose = False
		self.start_time = None
		bus = self.pipeline.get_bus()
		bus.add_signal_watch()
		bus.connect("message", self.on_message)

	def on_message(self, bus, message):
		t = message.type
		if t == Gst.MessageType.EOS:
			print("Finished by unexpected EOS.", file=sys.stderr)
			self.quit()
		elif t == Gst.MessageType.ERROR:
			err, debug = message.parse_error()
			print("Error: %s" % err, file=sys.stderr)
			self.quit()

	def dprint(self, txt):
		if self.verbose:
			print(txt, file=sys.stderr)

	def quit(self):
		self.pipeline.set_state(Gst.State.NULL)
		self.playmode = False

	def on_stop_timer(self):
		self.dprint("Stopped the recording.")
		self.quit()
		return False

	def rec(self):
		self.pipeline.set_state(Gst.State.PLAYING)
		GLib.timeout_add_seconds(self.len, self.on_stop_timer)
		self.dprint("Started to record (for %d [sec.]) ..." % self.len)
		return False

	def reconnect(self):
		dvb = self.pipeline.get_by_name("dvbbasebin")
		fdsink = self.pipeline.get_by_name("fdsink")
		dvb.unlink(fdsink)
		dpad = dvb.get_request_pad("program_{:d}".format(self.svc_id))
		fpad = fdsink.get_static_pad("sink")
		dpad.link(fpad)
		self.dprint("Using dvbbasebin.program_{:d}".format(self.svc_id))

	def main(self):
		self.playmode = True
		if self.start_time is None:
			self.rec()
		else:
			now = time.time()
			if self.start_time - now > 3600 * 6:
				print("start time is too ditant future. quiting...", file=sys.stderr)
				self.playmode = False
			elif now - self.start_time > 3600 * 4:
				print("start time is too distant past. quiting...", file=sys.stderr)
				self.playmode = False
			elif now - self.start_time > -1:
				self.dprint("already past the start time.")
				self.rec()
			else:
				GLib.timeout_add_seconds(int(self.start_time - now), self.rec)
				self.dprint("Waiting about %d sec. to start recording..."
					% int(self.start_time - now))

		while self.playmode:
			time.sleep(1)
		self.loop.quit()

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
usage = "usage: %prog [options] {-c CH_NAME | -s SERVICE_ID} [-l length]"

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

parser.add_option("-l", "--length",
					metavar="TIME", type="int",
					help="recording length (in min.) [default: 60]")

parser.add_option("-o", "--output",
					dest="filename", metavar="FILE",
					help="write output to FILE. [default: stdout]")

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

parser.add_option("-t", "--start",
					metavar="TIME",
					help="start recording at TIME in localtime. [default: now]  "
							"Format of TIME: %Y-%m-%dT%H:%M:%S")

parser.add_option("--shrink_pat",
					action="store_true",
					help="rewrite PAT to contain just one program. [default: False]")

(options, args) = parser.parse_args()
mainclass = None
ch_name = None
try:
	mainclass = CLI_Main()
	mainclass.verbose = options.verbose

	if options.channel is None and options.serviceid is None:
		parser.error("at least either -c or -s option must be specified.")

	if options.filename:
		try:
			d = os.path.dirname(options.filename);
			if d != "" and not os.path.isdir(d):
				os.makedirs(d, 0o755)
			f = open(options.filename, 'w')
			mainclass.pipeline.get_by_name("fdsink").set_property("fd", f.fileno())
		except:
			parser.error("option -o has unwritable filename: %s" % options.filename)

	if options.adapter:
		mainclass.pipeline.get_by_name("dvbbasebin").set_property("adapter", options.adapter)
	if options.frontend:
		mainclass.pipeline.get_by_name("dvbbasebin").set_property("frontend", options.frontend)

	if options.start:
		mainclass.start_time = time.mktime(
			time.strptime(options.start, "%Y-%m-%dT%H:%M:%S"))

	if options.length is not None and options.length <= 0:
		parser.error("invalid value [%d] for -l option." % options.length)
	if options.length is None:
		mainclass.len = 60 * 60
	else:
		mainclass.len = options.length * 60

	(ch_name, sid) = check_channel(options.channel, options.serviceid, options.conf)
	if ch_name is None or sid is None:
		parser.error("(channel:%s, service_id:%s) is not a valid combination."
			% (options.channel, options.serviceid))
	mainclass.svc_id = sid
	if options.shrink_pat:
		mainclass.reconnect()
	mainclass.pipeline.get_by_name("dvbbasebin").set_uri("dvb://%s" % ch_name)

	loop = GLib.MainLoop()
	mainclass.loop = loop
	threading.Thread(target=mainclass.main).start()
	loop.run()
except:
	if mainclass:
		mainclass.quit()
	if ch_name is not None:
		print("dvb://%s" % ch_name, file=sys.stderr)
	print("aborted by:%s, %s" % sys.exc_info()[0:2], file=sys.stderr)
