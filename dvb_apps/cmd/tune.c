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
	{   1, "NHK BS-1",	1318000, 0x40f1 },
	{   3, "NHK BS-Hi",	1318000, 0x40f2 },
	{   4, "BS日テレ",	1279640, 0x40d0 },
	{   8, "BSフジ",	1279640, 0x40d1 },
	{  10, "★ch HV",	1202920, 0x4091 },
	{  11, "BS11",		1202920, 0x4090 },
	{  12, "TwellV",	1202920, 0x4092 },
	{ 244, "JSPORTS3",	1433080, 0x4751 },

	{ 101, "BS4010 151:BS朝日1 152:BS朝日2 153:BS朝日3",		1049480, 0x4010 },
	{ 102, "BS4011 161-163:BS-TBS",					1049480, 0x4011 },
	{ 103, "BS4030 191:WOWOWプライム",				1087840, 0x4030 },
	{ 104, "BS4031 171:BSジャパン 172:BSジャパン2 173:BSジャパン3",	1087840, 0x4031 },
	{ 105, "BS4450 192:WOWOWライブ",					1126200, 0x4450 },
	{ 106, "BS4451 193:WOWOWシネマ",					1126200, 0x4451 },
	{ 107, "BS4470 201:スターチャンネル2 202:スターチャンネル3",		1164560, 0x4470 },
	{ 108, "BS4671 236:BSアニマックス",				1164560, 0x4671 },
	{ 109, "BS4672 256:ディズニーチャンネル",				1164560, 0x4672 },

	{ 204, "CS7040",	1653000, 0x7040 },
	{ 206, "CS7060 294:ホームドラマ 323:MTV-HD 329:歌謡ポップス 340:ディスカバリー 341:アニマルプラネット 354:CNNj",	1693000, 0x7060},
	{ 212, "CS70c0",	1813000, 0x70c0 },
	{ 216, "CS7100 290:宝塚SKYSTAGE 305:チャンネル銀河 311:AXN海外ドラマ 333:AT-X 343:ナショジオ 353:BBC",		1893000, 0x7100 },
	{ 220, "CS7140",	1973000, 0x7140 },
};

struct channel *lookup_channel(int id, struct channel *channels, int nr)
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

int search(int adapter_nr, int channel_id)
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
	if (info.type == FE_QPSK)
		channel = lookup_channel(channel_id, isdbs_channels, ARRAY_SIZE(isdbs_channels));
	else if (info.type == FE_OFDM)
		channel = lookup_channel(channel_id, isdbt_channels, ARRAY_SIZE(isdbt_channels));
	else {
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

	prop[0].cmd = DTV_STAT_CNR;
	props.num = 1;
	for (i = 0; i < 8; i++) {
		if ((ioctl(fd, FE_READ_STATUS, &status) < 0) || (ioctl(fd, FE_GET_PROPERTY, &props) < 0)) {
			perror("ioctl FE_READ_STATUS / FE_GET_PROPERTY");
			goto out;
		}
		if ((status & FE_HAS_LOCK) && prop[0].u.st.stat[0].svalue) {
			fprintf(stderr, "#%d Successfully tuned to %s, %f dB\n",
				adapter_nr, channel->name, ((double)prop[0].u.st.stat[0].svalue)/10000.);
			return 0;
		}
		usleep(125 * 1000);
	}
	fprintf(stderr, "#%d Failed to tune to %s (status %02x)\n",
		adapter_nr, channel->name, status);
out:
	close(fd);
	return -1;
}

int track(int adapter_nr)
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
	int adapter_nr, channel_id, fd, i, ret;

	if (argc <= 2) {
		fprintf(stderr, "Usage: %s adapter_no channel_id\n", argv[0]);
		fprintf(stderr, "\nSatellite channels:\n");
		for (i = 0; i < ARRAY_SIZE(isdbs_channels); i++)
			fprintf(stderr, "  %d	%s\n", isdbs_channels[i].id, isdbs_channels[i].name);
		fprintf(stderr, "\nTerrestrial channels:\n");
		for (i = 0; i < ARRAY_SIZE(isdbt_channels); i++)
			fprintf(stderr, "  %d	%s\n", isdbt_channels[i].id, isdbt_channels[i].name);
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

