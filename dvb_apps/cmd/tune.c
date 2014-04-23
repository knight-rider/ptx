#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/version.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

struct channel {
	int id;
	const char *name;
	unsigned int frequency; // freqno or freq (terrestrial Hz, BSCS110 kHz)
	unsigned int ts_id;	// slotid or tsid
};

static struct channel isdbt_channels[] = {
	{  1,	"NHK東京 総合",	557142857 },
	{  2,	"NHK東京 教育",	551142857 },
	{  3,	"tvk 神奈川",	68 + 128},
	{  4,	"日本テレビ",	545142857 },
	{  5,	"テレビ朝日",	539142857 },
	{  6,	"東京放送",	527142857 },
	{  7,	"テレビ東京",	533142857 },
	{  8,	"フジテレビ",	521142857 },
	{  9,	"MXテレビ",	515142857 },
	{ 12,	"放送大学",	563142857 },
};

static struct channel isdbs_channels[] = {
	{   1, "NHK BS-1",          1318000, 0x40f1 },
	{   2, "NHK BS-2",          1318000, 0x40f1 },
	{   3, "NHK BS-Hi",         1318000, 0x40f2 },
	{   4, "BS日テレ",          1279640, 0x40d0 },
	{   5, "BS朝日",            1049480, 0x4010 },
	{   6, "BS-i",              1049480, 0x4011 },
	{   7, "BSジャパン",        1087840, 0x4031 },
	{   8, "BSフジ",            1279640, 0x40d1 },
	{   9, "WOWOW",             1087840, 0x4030 },
	{  10, "STAR CHANNEL HV",   1202920, 0x4091 },
	{  11, "BS11",              1202920, 0x4090 },
	{  12, "TwellV",            1202920, 0x4092 },

	{  13, "CS 0x7100",	1893000, 0x7100 },
	{  14, "CS 0x7060",	1693000, 0x7060 },

	{ 236, "BSアニマックス",	1164560, 0x4671 },
};

static struct channel *
lookup_channel(int id, struct channel *channels, int nr)
{
	int i;
	struct channel *channel;
	for (i = 0; i < nr; i++) {
		channel = &channels[i];
		if (channel->id == id)
			return channel;
	}
	return NULL;
}

static int search(int adapter_nr, int channel_id)
{
	char file[256];
	int fd;
	struct dvb_frontend_info info;
	struct channel *channel;
	struct dtv_property prop[3];
	struct dtv_properties props;
	int i;
	fe_status_t status;

	sprintf(file, "/dev/dvb/adapter%d/frontend0", adapter_nr);
	if ((fd = open(file, O_RDWR)) < 0) {
		perror("open");
		return -1;
	}
	if (ioctl(fd, FE_GET_INFO, &info) < 0) {
		perror("ioctl FE_GET_INFO");
		goto out;
	}
	if (info.type == FE_QPSK) {
		channel = lookup_channel(channel_id, isdbs_channels,
					 ARRAY_SIZE(isdbs_channels));
	} else if (info.type == FE_OFDM) {
		channel = lookup_channel(channel_id, isdbt_channels,
					 ARRAY_SIZE(isdbt_channels));
	} else {
		fprintf(stderr, "Unknown type of adapter\n");
		goto out;
	}
	if (!channel) {
		fprintf(stderr, "Unknown channel ID\n");
		goto out;
	}

	prop[0].cmd = DTV_FREQUENCY;
	prop[0].u.data = channel->frequency;
	prop[1].cmd = DTV_STREAM_ID;
	prop[1].u.data = channel->ts_id;
	prop[2].cmd = DTV_TUNE;

	props.props = prop;
	props.num = 3;

	if ((ioctl(fd, FE_SET_PROPERTY, &props)) < 0) {
		perror("ioctl FE_SET_PROPERTY");
		goto out;
	}
	for (i = 0; i < 4; i++) {
		if (ioctl(fd, FE_READ_STATUS, &status) < 0) {
			perror("ioctl FE_READ_STATUS");
			goto out;
		}
		if (status & FE_HAS_LOCK) {
			fprintf(stderr, "Successfully tuned to %s .\n",
				channel->name);
			return 0;
		}
		usleep(250 * 1000);
	}
	fprintf(stderr, "Failed to tune to %s (status %02x).\n",
		channel->name, status);
out:
	close(fd);
	return -1;
}

static int track(int adapter_nr)
{
	char file[256];
	int fd;
	struct dmx_pes_filter_params filter;

	filter.pid = 0x2000;
	filter.input = DMX_IN_FRONTEND;
	filter.output = DMX_OUT_TS_TAP;
	filter.pes_type =  DMX_PES_VIDEO;
	filter.flags = DMX_IMMEDIATE_START;

	sprintf(file, "/dev/dvb/adapter%d/demux0", adapter_nr);
	if ((fd = open(file, O_RDWR)) < 0) {
		perror("open");
		return -1;
	}
	if (ioctl(fd, DMX_SET_PES_FILTER, &filter) < 0) {
		perror("ioctl DMX_SET_PES_FILTER");
		close(fd);
		return -1;
	}
	while (1)
		sleep(3);
	/* never returns */
}

int main(int argc, char *argv[]) {
	int adapter_nr, channel_id, fd, ret;

	if (argc <= 2) {
		fprintf(stderr, "Usage: %s adapter_no channel_id\n", argv[0]);
		return 1;
	}
	adapter_nr = strtol(argv[1], NULL, 0);
	channel_id = strtol(argv[2], NULL, 10);

	fd = search(adapter_nr, channel_id);
	if (fd < 0)
		return 1;
	ret = track(adapter_nr);
	close(fd);
	return ret < 0;
}

