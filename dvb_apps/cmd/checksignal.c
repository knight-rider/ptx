#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>
#include <linux/dvb/audio.h>
#include <linux/dvb/version.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include <float.h>

#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

#include "readchannels.c"

struct channel isdbt_channels[] = {
	{  1,	"NHK東京 総合",	557142857 },
	{  2,	"NHK東京 教育",	551142857 },
	{  3,	"tvk 神奈川",	68 + 128},
	{  4,	"日本テレビ",	545142857 },
	{  5,	"テレビ朝日",	539142857 },
	{  6,	"東京放送",	527142857 },
	{  7,	"テレビ東京",	533142857 },
	{  8,	"フジテレビ",	521142857 },
	{  9,	"MXテレビ",	525142857 },
	{ 12,	"放送大学",	563142857 },
/*
	{  23, "TSCテレビせとうち",  503142857 },
	{   9, "RNC西日本テレビ",    515142857},
	{  11, "RSKテレビ",          521142857},
	{  35, "OHK",                557142857},
	{  25, "瀬戸内海放送",       575142857},
	{   3, "NHK総合",            587142857},
	{   5, "NHKEテレ",           665142857},
*/
};

struct channel isdbs_channels[] = {
{101,"NHKBS1",1318000,0x40f1},
{102,"NHKBS1",1318000,0x40f1},
{103,"NHKBSプレミアム",1318000,0x40f2},
{104,"NHKBSプレミアム",1318000,0x40f2},
{141,"BS日テレ",1279640,0x40d0},
{142,"BS日テレ",1279640,0x40d0},
{143,"BS日テレ",1279640,0x40d0},
{151,"BS朝日1",1049480,0x4010},
{152,"BS朝日2",1049480,0x4010},
{153,"BS朝日3",1049480,0x4010},
{161,"BS−TBS",1049480,0x4011},
{162,"BS−TBS",1049480,0x4011},
{163,"BS−TBS",1049480,0x4011},
{171,"BSジャパン",1087840,0x4031},
{172,"BSジャパン2",1087840,0x4031},
{173,"BSジャパン3",1087840,0x4031},
{181,"BSフジ・181",1279640,0x40d1},
{182,"BSフジ・182",1279640,0x40d1},
{183,"BSフジ・183",1279640,0x40d1},
{191,"WOWOWプライム",1087840,0x4030},
{192,"WOWOWライブ",1126200,0x4450},
{193,"WOWOWシネマ",1126200,0x4451},
{200,"スター・チャンネル1",1202920,0x4091},
{201,"スター・チャンネル2",1164560,0x4470},
{202,"スター・チャンネル3",1164560,0x4470},
{211,"BS11",1202920,0x4090},
{222,"TwellV",1202920,0x4092},
{231,"放送大学BS1",1241280,0x46b2},
{232,"放送大学BS2",1241280,0x46b2},
{233,"放送大学BS3",1241280,0x46b2},
{234,"グリーンチャンネル",1394720,0x4730},
{236,"BSアニマックス",1164560,0x4671},
{238,"FOXbs238",1241280,0x46b0},
{241,"BSスカパー!",1241280,0x46b1},
{242,"J　SPORTS　1",1394720,0x4731},
{243,"J　SPORTS　2",1394720,0x4732},
{244,"J　SPORTS　3",1433080,0x4751},
{245,"J　SPORTS　4",1433080,0x4752},
{251,"BS釣りビジョン",1471440,0x4770},
{252,"IMAGICA　BS",1433080,0x4750},
{255,"BS日本映画専門ch",1471440,0x4771},
{256,"ディズニーチャンネル",1164560,0x4672},
{258,"Dlife",1471440,0x4772},
{291,"ＮＨＫ総合１・東京",1356360,0x4310},
{292,"ＮＨＫＥテレ東京",1356360,0x4310},
{294,"日テレ１",1356360,0x4311},
{295,"テレビ朝日",1356360,0x4311},
{296,"ＴＢＳ１",1356360,0x4311},
{297,"テレビ東京１",1356360,0x4311},
{298,"フジテレビ",1356360,0x4310},
};

struct channel isdbs_channels_cs[] = {
{55,"ショップチャンネル",1733000,0x6080},
{161,"QVC",2013000,0x7160},
{221,"東映チャンネル",1693000,0x7060},
{222,"衛星劇場",1693000,0x7060},
{223,"チャンネルNECO",1693000,0x7060},
{227,"ザ・シネマ",1653000,0x7040},
{240,"ムービープラスHD",1933000,0x7120},
{250,"sky・Aスポーツ+",1693000,0x7060},
{254,"GAORA",1693000,0x7060},
{257,"日テレG+　HD",2053000,0x7180},
{262,"ゴルフネットHD",1933000,0x7120},
{290,"SKY　STAGE",1893000,0x7100},
{292,"時代劇専門チャンネル",1693000,0x7060},
{293,"ファミリー劇場",2013000,0x7160},
{294,"ホームドラマCH",1653000,0x7040},
{300,"日テレプラス",2053000,0x7180},
{301,"TBSチャンネル",2013000,0x7160},
{303,"テレ朝チャンネル",1813000,0x70c0},
{305,"チャンネル銀河",1893000,0x7100},
{307,"フジテレビONE",1973000,0x7140},
{308,"フジテレビTWO",1973000,0x7140},
{309,"フジテレビNEXT",1973000,0x7140},
{310,"スーパードラマ",1893000,0x7100},
{311,"AXN",1893000,0x7100},
{312,"FOX",1653000,0x7040},
{314,"女性ch/LaLa",1933000,0x7120},
{315,"FOXプラス",2053000,0x7180},
{321,"スペシャプラス",2053000,0x7180},
{322,"スペースシャワーTV",1653000,0x7040},
{323,"MTV",1813000,0x70c0},
{324,"ミュージック・エア",1813000,0x70c0},
{325,"エムオン!",2013000,0x7160},
{331,"カートゥーン",1653000,0x7040},
{333,"AT−X",1893000,0x7100},
{334,"ディズニーXD",1653000,0x7040},
{335,"キッズステーション",1733000,0x6080},
{340,"ディスカバリー",1693000,0x7060},
{341,"動物ch/アニプラ",1693000,0x7060},
{342,"ヒストリーチャンネル",1893000,0x7100},
{343,"ナショジオチャンネル",2013000,0x7160},
{350,"日テレNEWS24",2053000,0x7180},
{351,"TBSニュースバード",2013000,0x7160},
{352,"朝日ニュースター",1813000,0x70c0},
{353,"BBCワールド",1813000,0x70c0},
{354,"CNNj",1813000,0x70c0},
{362,"旅チャンネル",2053000,0x7180},
{800,"スカチャン0",1773000,0x60a0},
{801,"スカチャン1",1773000,0x60a0},
{802,"スカチャン2",1773000,0x60a0},
{805,"スカチャン3",1773000,0x60a0},
};

struct channel *
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

int search(int adapter_nr, int cs, int channel_id)
{
	char file[256];
	int fd;
	struct dvb_frontend_info info;
	struct channel *channel;
	struct dtv_property prop[4];
	struct dtv_properties props;
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
		if( cs ){
			struct channel *chs = isdbs_channels_cs;
			int count = ARRAY_SIZE(isdbs_channels_cs);
			read_channels_tune( "/CS", &chs, &count );
			channel = lookup_channel(channel_id, chs, count);
		}else{
			struct channel *chs = isdbs_channels;
			int count = ARRAY_SIZE(isdbs_channels);
			read_channels_tune( "/BS", &chs, &count );
			channel = lookup_channel(channel_id, chs, count);
		}
	} else if (info.type == FE_OFDM) {
		channel = lookup_channel(channel_id, isdbt_channels,
					 ARRAY_SIZE(isdbt_channels));
	} else {
		fprintf(stderr, "Unknown type of adapter\n");
		goto out;
	}
	if (channel == NULL) {
		fprintf(stderr, "Unknown id of channel\n");
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

	unsigned short raw=0, prev = 0xffff;
	double cnr=0, cnrmax=0, cnrmin=DBL_MAX, P;

	prop[3].cmd = DTV_STAT_CNR;
	props.props = &prop[3];
	props.num = 1;
	while (1){
		usleep(100 * 1000);
		if ((ioctl(fd, FE_READ_STATUS, &status) < 0) || (ioctl(fd, FE_GET_PROPERTY, &props) < 0)) {
			perror("ioctl FE_READ_STATUS / FE_GET_PROPERTY");
			break;
		} else if ((status & FE_HAS_LOCK) && (prop[3].u.st.len))
			printf("\rDVBv5 CN %f (dB) ", ((double)prop[3].u.st.stat[0].svalue)/10000.);

		if (ioctl(fd, FE_READ_SIGNAL_STRENGTH, &raw) < 0) {
			perror("ioctl FE_READ_SIGNAL_STRENGTH");
			break;
		} else {
			if (!raw)
				continue;
			if (prev == 0xffff)
				prev = raw;
			if (info.type == FE_OFDM) {
				P = log10(5505024/(double)raw) * 10;
				cnr = .000024 * P * P * P * P - .0016 * P * P * P + .0398 * P * P + .5491 * P + 3.0965;
			} else {
				P = raw - 3000;
				if (P < 0)
					P = 0;
				P = sqrt(P) / 64;
				cnr = -1.6346*P*P*P*P*P + 14.341*P*P*P*P - 50.259*P*P*P + 88.977*P*P - 89.565*P + 58.857;
				if (cnr < 0)
					cnr = 0;
			}
			if (cnr > cnrmax) cnrmax = cnr;
			if (cnr < cnrmin) cnrmin = cnr;
			printf("DVBv3 raw %d cnr %f min %f max %f", raw, cnr, cnrmin, cnrmax);
			prev = cnr;
		}
	}
	fprintf(stderr, "Failed to tune to %s (status %02x).\n",
		channel->name, status);
out:
	close(fd);
	return -1;
}

int track(int fefd, int adapter_nr)
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

	/* never returns */
	return 0;
}

int main(int argc, char *argv[]) {
	int adapter_nr, channel_id, cs = 0, fd, ret;

	if (argc <= 2) {
		fprintf(stderr, "Usage: %s adapter_nr channel_id\n", argv[0]);
		return 1;
	}

	adapter_nr = strtol(argv[1], NULL, 0);
	if( strlen(argv[2])>2 && strncmp(argv[2],"CS",2)==0 ){
		channel_id = strtol(argv[2]+2, NULL, 10);
		cs = 1;
	}else if( strlen(argv[2])>2 && strncmp(argv[2],"BS",2)==0 ){
		channel_id = strtol(argv[2]+2, NULL, 10);
	}else if( strlen(argv[2])>2 && strncmp(argv[2],"GR",2)==0 ){
		channel_id = strtol(argv[2]+2, NULL, 10);
	}else{
		channel_id = strtol(argv[2], NULL, 10);
	}

	fd = search(adapter_nr, cs, channel_id);
	if (fd < 0)
		return 1;

	ret = track(fd, adapter_nr);
	close(fd);

	return ret < 0;
}
