#!/usr/bin/env python
import sys
import os.path
import glib
import gi
import gi.module
gi.require_version('Gst', '1.0')
from gi.repository import GObject, Gst, GLib

def proc_msg(message):
	eit = message.get_structure()
	if eit is None or eit.get_name() != 'eit':
		return
	events = eit.get_value('events')
	if events is None:
		sys.stderr.write('******* no events included.\n')
		return
	for i in range(0, events.len()):
		title = events.index(i).get_string('name')
		if False and eit.get_boolean('present-following')[1] and \
		   eit.get_boolean('actual-transport-stream')[1] and \
		   eit.get_uint('section-number')[1] == 0 and \
		   title is not None:
			print title
			print events.index(i).get_string('description')

		items = events.index(i).get_value('extended-items')
		if items is None:
			# sys.stderr.write('======== no ext. event desc.\n')
			continue
		for j in range(0, items.len()):
			print items.index(j).get_string('description'), ':'
			print items.index(j).get_string('text')
			print

def on_message(bus, message):
	t = message.type
	if t == Gst.MessageType.ERROR or t == Gst.MessageType.EOS:
		pipeline.set_state(Gst.State.NULL)
		loop.quit()
	elif t == Gst.MessageType.ELEMENT:
		proc_msg(message)

def on_pad_added(src, pad, sink):
	pad.link(sink.get_static_pad('sink'))

GObject.threads_init()
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

loop = glib.MainLoop()
pipeline.set_state(Gst.State.PLAYING)
try:
	loop.run()
except:
	pipeline.set_state(Gst.State.NULL)
	loop.quit()

