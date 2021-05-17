#!/usr/bin/env python
import sys
import os
import os.path
import gi
gi.require_version('Gst', '1.0')
gi.require_version('GstMpegts', '1.0')
from gi.repository import GLib, GObject, Gst, GstMpegts

verbose = os.environ.setdefault('DEBUG', 0)

def proc_msg(message):
	st = message.get_structure()
	if st is None or not st.has_name('eit'):
		return

	try:
		sec = GstMpegts.message_parse_mpegts_section(message)
		eit = sec.get_eit()
		events = eit.events
		if verbose and eit.actual_stream and eit.present_following:
			print("EIT p/f:{} ver:{} eid:{} sid:{}".format(
			sec.section_number, sec.version_number,
			eit.events[0].event_id, sec.subtable_extension))
	except (AttributeError, KeyError):
		sys.stderr.write('******* no events included.\n')
		return

	for ev in events:
		d = GstMpegts.find_descriptor(ev.descriptors,
					      GstMpegts.DVBDescriptorType.SHORT_EVENT)
		try:
			(title, txt) = d.parse_dvb_short_event()[2:4]

			if eit.present_following and \
			   eit.actual_stream and \
			   sec.section_number == 0 and \
			   title is not None:
				print(title)
				print(txt)
				print()
		except (AttributeError, TypeError):
			pass

		d = GstMpegts.find_descriptor(ev.descriptors,
					      GstMpegts.DVBDescriptorType.EXTENDED_EVENT)
		try:
			for desc in d.parse_dvb_extended_event():
				for item in desc.items:
					print(item.item_descripton + ':')
					print(item.item)
					print()
		except AttributeError:
			pass


def on_message(bus, message):
	t = message.type
	if t == Gst.MessageType.ERROR or t == Gst.MessageType.EOS:
		pipeline.set_state(Gst.State.NULL)
		loop.quit()
	elif t == Gst.MessageType.ELEMENT:
		proc_msg(message)

def on_pad_added(src, pad, sink):
	pad.link(sink.get_static_pad('sink'))

# GObject.threads_init()
Gst.init(None)

if len(sys.argv) < 2:
	sys.stderr.write('Usage: %s {dvb://... | stdin:// | [file://]<FILE>}\n' % os.path.basename(sys.argv[0]))
	sys.exit(1)

pipeline = Gst.Pipeline()
sink = Gst.ElementFactory.make('fakesink', None)
pipeline.add(sink)
parser = Gst.ElementFactory.make('tsparse', None)

if sys.argv[1].startswith('dvb://'):
	src = Gst.ElementFactory.make('uridecodebin', None)
	src.set_property('uri', sys.argv[1])
	src.set_property('caps', Gst.Caps.from_string('video/mpegts, systemstream=true'))
	pipeline.add(src)
	src.connect('pad-added', on_pad_added, sink)
elif sys.argv[1].startswith('stdin://'):
	src = Gst.ElementFactory.make('fdsrc', None)
	pipeline.add(src)
	pipeline.add(parser)
	src.link(parser)
	parser.link(sink)
else:
	src = Gst.ElementFactory.make('filesrc', None)
	if sys.argv[1].startswith('file://'):
		src.set_property('location', sys.argv[1][7:])
	else:
		src.set_property('location', sys.argv[1])
	pipeline.add(src)
	pipeline.add(parser)
	src.link(parser)
	parser.link(sink)

bus = pipeline.get_bus()
bus.add_signal_watch()
bus.connect('message', on_message)

loop = GLib.MainLoop()
pipeline.set_state(Gst.State.PLAYING)
try:
	loop.run()
except:
	pipeline.set_state(Gst.State.NULL)
	loop.quit()

