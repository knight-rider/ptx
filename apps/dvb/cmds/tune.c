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
	unsigned int frequency; // freqno or freq (terrestrial: Hz, BS/CS110: kHz)
	unsigned int ts_id;	// slotid or tsid
};

static struct channel isdbt_channels[] = {
	{  1,	"NHK総合 東京",	557142857},
	{  2,	"NHK教育 東京",	551142857},
	{  3,	"tvk 神奈川",	68 +  128},
	{  4,	"日本テレビ",	545142857},
	{  5,	"テレビ朝日",	539142857},
	{  6,	"TBS 東京放送",	527142857},
	{  7,	"テレビ東京",	533142857},
	{  8,	"フジテレビ",	521142857},
	{  9,	"TOKYO MX",	515142857},
	{ 10,	"テレ玉",	485142857},
	{ 11,	"J:COMテレビ",	473142857},
	{ 12,	"J:COMチャンネル",509142857},
//	{ 12,	"tvk",		503142857},
//	{ 12,	"放送大学",	563142857},
};

static struct channel isdbs_channels[] = {
	{  1, "NHK BS-1",	1318000, 0x40f1},
	{  3, "NHK BS-Hi",	1318000, 0x40f2},
	{  4, "BS日テレ",	1279640, 0x40d0},
	{  8, "BSフジ",		1279640, 0x40d1},
	{ 10, "★ch HV",		1202920, 0x4091},
	{ 11, "BS11",		1202920, 0x4090},
	{ 12, "TwellV",		1202920, 0x4092},

	{101, "BS4010 151:BS朝日1 152:BS朝日2 153:BS朝日3",		1049480, 0x4010},
	{102, "BS4011 161-163:BS-TBS",					1049480, 0x4011},
	{103, "BS4030 191:WOWOWプライム",					1087840, 0x4030},
	{104, "BS4031 171:BSジャパン 172:BSジャパン2 173:BSジャパン3",	1087840, 0x4031},
	{105, "BS4450 192:WOWOWライブ",					1126200, 0x4450},
	{106, "BS4451 193:WOWOWシネマ",					1126200, 0x4451},
	{107, "BS4470 201:スターチャンネル2 202:スターチャンネル3",		1164560, 0x4470},
	{108, "BS4671 236:BSアニマックス",				1279640, 0x46d2},
	{109, "BS4672 256:ディズニーch",					1087840, 0x4632},
	{119, "BS4731 242:JSPORTS1",					1394720, 0x4731},
	{120, "BS4732 243:JSPORTS2",					1394720, 0x4732},
	{121, "BS4751 244:JSPORTS3",					1433080, 0x4751},
	{122, "BS4752 245:JSPORTS4",					1433080, 0x4752},

	{204, "CS7040",												1653000, 0x7040},
	{206, "CS7060 294:ホームドラマ 323:MTV-HD 329:歌謡ポップス 340:ディスカバリー 341:アニマルプラネット 354:CNNj",	1693000, 0x7060},
	{212, "CS70c0 254:GAORA 325:エムオン!HD 330:キッズステーション",						1813000, 0x70c0},
	{216, "CS7100 290:宝塚SKYSTAGE 305:チャンネル銀河 311:AXN海外ドラマ 333:AT-X 343:ナショジオ 353:BBC",		1893000, 0x7100},
	{220, "CS7140",												1973000, 0x7140},
};
/*
BS朝日1:1049480|0x4010:151
BS朝日2:1049480|0x4010:152
BS朝日3:1049480|0x4010:153
WOWOWプライム:1087840|0x4030:191
WOWOWライブ:1126200|0x4450:192
WOWOWシネマ:1126200|0x4451:193
BS11イレブン:1202920|0x4090:211
BSスカパー!:1241280|0x46b1:241
BS日テレ:1279640|0x40d0:141
BS日テレ:1279640|0x40d0:142
BS日テレ:1279640|0x40d0:143
NHKBS1:1318000|0x40f1:101
NHKBS1:1318000|0x40f1:102
グリーンチャンネル:1394720|0x4730:234
シネフィルWOWOW:1433080|0x4750:252
J　SPORTS　4:1433080|0x4752:245
BS釣りビジョン:1471440|0x4770:251

TBSチャンネル1:1613000|0x6020:296
テレ朝チャンネル1:1613000|0x6020:298
テレ朝チャンネル2:1613000|0x6020:299
ディズニージュニア:1613000|0x6020:339
スカイA:1653000|0x7040:250
時代劇専門ch:1653000|0x7040:292
エンタメ〜テレ:1653000|0x7040:301
MTV:1653000|0x7040:323
ホームドラマCH:1693000|0x7060:294
ミュージック・エア:1693000|0x7060:324
歌謡ポップス:1693000|0x7060:329
カートゥーン:1693000|0x7060:331
ディスカバリー:1693000|0x7060:340
アニマルプラネット:1693000|0x7060:341
CNNj:1693000|0x7060:354
囲碁・将棋チャンネル:1693000|0x7060:363
ショップチャンネル:1733000|0x6080:55
東映チャンネル:1733000|0x6080:218
Mnet:1733000|0x6080:318
日テレNEWS24:1733000|0x6080:349
衛星劇場:1773000|0x60a0:219
KBS　World:1773000|0x60a0:317
スポーツライブ+:1773000|0x60a0:800
スカチャン1:1773000|0x60a0:801
GAORA:1813000|0x70c0:254
エムオン!:1813000|0x70c0:325
キッズステーション:1813000|0x70c0:330
ナショジオ:1813000|0x70c0:343
ザ・シネマ:1853000|0x70e0:227
ファミリー劇場:1853000|0x70e0:293
スーパー!ドラマTV:1853000|0x70e0:310
ヒストリーチャンネル:1853000|0x70e0:342
SKY　STAGE:1893000|0x7100:290
AXN　海外ドラマ:1893000|0x7100:311
AXNミステリー:1893000|0x7100:316
スペシャプラス:1893000|0x7100:321
AT−X:1893000|0x7100:333
BBCワールド:1893000|0x7100:353
ムービープラス:1933000|0x7120:240
ゴルフネットワーク:1933000|0x7120:262
銀河◆歴ドラ・サスペ:1933000|0x7120:305
女性ch/LaLa:1933000|0x7120:314
フジテレビONE:1973000|0x7140:307
フジテレビTWO:1973000|0x7140:308
フジテレビNEXT:1973000|0x7140:309
スペースシャワーTV:1973000|0x7140:322
QVC:2013000|0x7160:161
TBSチャンネル2:2013000|0x7160:297
FOX:2013000|0x7160:312
TBS　NEWS:2013000|0x7160:351
映画・chNECO:2053000|0x7180:223
日テレジータス:2053000|0x7180:257
MONDO　TV:2053000|0x7180:295
日テレプラス:2053000|0x7180:300
*/
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
	fprintf(stderr, "#%d Failed to tune %s, status 0x%02X\n",
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