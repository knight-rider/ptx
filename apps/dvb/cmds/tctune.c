/*
	Tune, based on channel list given by tcscan
	(c) Budi Rachmanto <info@are.ma>
*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/dvb/frontend.h>
#include <linux/dvb/dmx.h>

#define DVB_MAX_ADAPTERS 16

int main(int argc, char *argv[]) {
	char	*path,
		linebuf[128],
		chname[32];
	int	ch = -1,
		i,
		fd,
		fe;
	FILE	*fp;
	uint16_t	tsid = 0,
			ch_tune;
	uint32_t	freq;
	enum fe_delivery_system		sys;
	struct dtv_property		fep[3];
	struct dtv_properties		feps = {.props = fep};
	fe_status_t			status;
	struct dmx_pes_filter_params	filter;

	if (argc < 3) {
		fprintf(stderr, "Usage: %s /path/to/channel_list channel_service_id [adapter_no]\n", argv[0]);
		return -1;
	}
	path	= argv[1];
	ch_tune	= atoi(argv[2]);
	if (!(fp = fopen(path, "r"))) {
		fprintf(stderr, "Cannot open %s\n", path);
		return -1;
	}
	while (fgets(linebuf, sizeof(linebuf), fp)) {
		if (*linebuf == 'T') {
			sys = SYS_ISDBT;
			sscanf(linebuf + 2, "%d %d %s", &ch, &freq, chname);
		} else if (*linebuf == 'S') {
			sys = SYS_ISDBS;
			sscanf(linebuf + 2, "%d %d %hx %s", &ch, &freq, &tsid, chname);
		} else
			continue;
		if (ch == ch_tune)
			break;
	}
	fclose(fp);
	if (ch != ch_tune) {
		fprintf(stderr, "CH %d unlisted\n", ch_tune);
		return -1;
	}
	printf("SYS:%c(%d) CH:%d FREQ:%d TSID:%X NAME:%s\n", *linebuf, sys, ch, freq, tsid, chname);

	for (i = argc > 3 ? atoi(argv[3]) : 0; i < DVB_MAX_ADAPTERS; i++) {
		sprintf(linebuf, "/dev/dvb/adapter%d/frontend0", i);
		if ((fe = open(linebuf, O_RDWR)) < 0)
			continue;

		feps.num = 1;
		fep[0].cmd = DTV_ENUM_DELSYS;
		if (ioctl(fe, FE_GET_PROPERTY, &feps) >= 0
			&& fep[0].u.buffer.len
			&& sys == fep[0].u.buffer.data[0]) {
			printf("Using adapter %d\n", i);
			break;
		}
		close(fe);
	}
	if (i == DVB_MAX_ADAPTERS) {
		fprintf(stderr, "Cannot find adapter\n");
		return -1;
	}
	sprintf(linebuf, "/dev/dvb/adapter%d/demux0", i);
	if ((fd = open(linebuf, O_RDWR)) < 0) {
		perror("open");
		goto OUT1;
	}

	fep[0].cmd	= DTV_FREQUENCY;
	fep[0].u.data	= freq;
	fep[1].cmd	= DTV_STREAM_ID;
	fep[1].u.data	= tsid;
	fep[2].cmd	= DTV_TUNE;
	feps.num	= 3;
	if ((ioctl(fe, FE_SET_PROPERTY, &feps)) < 0) {
		perror("ioctl FE_SET_PROPERTY");
		goto OUT2;
	}

	fep[0].cmd	= DTV_STAT_CNR;
	feps.num	= 1;
	for (i = 0; i < 8; i++) {
		if ((ioctl(fe, FE_READ_STATUS, &status) < 0) || (ioctl(fe, FE_GET_PROPERTY, &feps) < 0)) {
			perror("ioctl FE_READ_STATUS / FE_GET_PROPERTY");
			goto OUT2;
		}
		if ((status & FE_HAS_LOCK) && fep[0].u.st.stat[0].svalue) {
			printf("Successfully tuned %f dB\n", ((double)fep[0].u.st.stat[0].svalue)/10000.);
			break;
		}
		usleep(125 * 1000);
	}
	if (i == 8) {
		fprintf(stderr, "Failed to tune, status 0x%02X\n", status);
		goto OUT2;
	}

	filter.pid	= 0x2000;
	filter.input	= DMX_IN_FRONTEND;
	filter.output	= DMX_OUT_TS_TAP;
	filter.pes_type	= DMX_PES_VIDEO;
	filter.flags	= DMX_IMMEDIATE_START;
	if (ioctl(fd, DMX_SET_PES_FILTER, &filter) < 0) {
		perror("ioctl DMX_SET_PES_FILTER");
		goto OUT2;
	}
	while (1)
		sleep(10);	// never returns

OUT2:
	close(fd);
OUT1:
	close(fe);
}