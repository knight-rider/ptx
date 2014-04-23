#!/usr/bin/env python
import os
import os.path
from optparse import OptionParser
import xdg.BaseDirectory

gst_ver = (1, 0)

def check_channel(chname, progid, confname):
	if confname is None:
		if os.environ.has_key('GST_DVB_CHANNELS_CONF'):
			confname = os.environ['GST_DVB_CHANNELS_CONF']
		else:
			confname = os.path.join(xdg.BaseDirectory.xdg_config_home,
				"gstreamer-%d.%d" % gst_ver,
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


if __name__ == "__main__":
	usage = "usage: %prog [options] {-c CH_NAME | -s SERVICE_ID}"
	parser = OptionParser(usage=usage)
	parser.add_option("-c", "--channel",
					metavar="NAME",
					help="channel name of the recording event. "
					" [default: derived from service ID & conf-file]")
	parser.add_option("-s", "--serviceid",
					metavar="ID", type="int",
					help="service_ID (program_ID) of the recording event. "
					" [default: derived from channel name & conf-file]")
	parser.add_option("--conf",
					metavar="FILE",
					help="path to the channel config file. "
					" [default:~/.config/gstreamer-%d.%d/"
					"dvb-channels.conf]" % gst_ver)
	(options, args) = parser.parse_args()

	if options.channel is None and options.serviceid is None:
		parser.error("either -s or -c must be specified.");

	(ch_name, sid) = check_channel(options.channel, options.serviceid, options.conf)
	if ch_name is None or sid is None:
		parser.error("(channel:%s, service_id:%s) is not a valid combination."
			% (options.channel, options.serviceid))

	if options.channel:
		print sid
	else:
		print ch_name

