#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <stdint.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#ifndef DTV_ISDBS_TS_ID
#define DTV_ISDBS_TS_ID 42
#endif

static uint32_t freq;
static uint32_t ts_id;
static int scan_all = 1;
static enum {TER, BS, CS110} mode = TER;
static int volt = 0;

static int cont = 1;

static void stop_loop(int sig)
{
	cont = 0;
	return;
}

static int TS_OFFSET = 0;
static int TS_IDX = 0;
static int CH_IDX = 0;
static int CH_IDX_END = 0;
static FILE *INFILE;

static int
next_ch()
{
	static char buf[256];
	char *p, *q;
	int ch;

	if (!scan_all) {
		if (mode != TER && CH_IDX != 0) {
			if (TS_IDX <= 7)
				goto setparam;

			// scanned all relative tsid.
			// continue to read the next input line
			TS_IDX = 0;
			CH_IDX = 0;
		}

		while (fgets(buf, sizeof(buf), INFILE)) {
			ch = strtoul(buf, &p, 0);
			if (p == buf)
				continue;
			switch (mode) {
			case CS110:
				if (ch < 2 || ch > 24 || ch % 2 != 0) {
					fprintf(stderr, "Invalid channel:%d for CS\n", ch);
					return -1;
				}
				CH_IDX_END = CH_IDX = ch;
				TS_IDX = 0;
				break;

			case BS:
				if (ch < 1 || ch > 23 || ch % 2 == 0) {
					fprintf(stderr, "Invalid channel:%d for BS\n", ch);
					return -1;
				}
				CH_IDX_END = CH_IDX = ch;
				TS_IDX = 0;
				TS_OFFSET = 0;
				break;
			case TER:
				if (ch < 1 || ch > 62) {
					fprintf(stderr, "Invalid channel:%d for TER\n", ch);
					return -1;
				}
				freq = (473 + (ch - 13) * 6) * 1000000 + 142857;
				return 0;
				break;
			}
			if (CH_IDX > 0)
				goto setparam;
			else
				return -1;
		}
		return -1;
	}

setparam:
	switch (mode) {
	case CS110:
		if (TS_IDX > 7) {
			TS_IDX = 0;
			CH_IDX+=2;
		}
		if (CH_IDX > CH_IDX_END)
			return -1;
		freq = 12291000 + (CH_IDX - 2) * 40000 / 2 - 10678000;
		ts_id = (CH_IDX << 4) + TS_IDX;
		if (CH_IDX == 2 || CH_IDX == 8 || CH_IDX == 10)
			ts_id |= 0x6000;
		else
			ts_id |= 0x7000;
		TS_IDX++;
		break;
	case BS:
		if (TS_OFFSET > 3) {
			TS_OFFSET = 0;
			TS_IDX ++;
		}
		if (TS_IDX > 7) {
			TS_IDX = 0;
			CH_IDX += 2;
		}
		if (CH_IDX > CH_IDX_END)
			return -1;
		freq = 11727480 + (CH_IDX - 1) * 38360 / 2 - 10678000;
		ts_id = 0x4000 + (TS_OFFSET << 9) + (CH_IDX << 4) + TS_IDX;
		TS_OFFSET++;
		break;
	case TER:
		if (CH_IDX > CH_IDX_END)
			return -1;
		freq = (473 + (CH_IDX - 13) * 6) * 1000000 + 142857;
		CH_IDX++;
		break;
	default:
		return -1;
	}

	return 0;
}

extern void
aribstr_to_utf8(char *src, size_t srclen, char *dest, size_t destlen);

static void
make_output(unsigned char *sdt, int len)
{
	unsigned char *p;
	uint16_t svc_id;
	uint dlen;
	uint8_t desc_len;
	uint8_t chname[48];

//fprintf(stderr, "parseing SDT(0x%02hhx) tsid:0x%04hx..\n", sdt[0], sdt[3] << 8 | sdt[4]);
	p = sdt + 11;
	while (p < sdt + len - 4) {
		svc_id = p[0] << 8 | p[1];
		p += 3;
		dlen = (p[0] & 0x0F) << 8 | p[1];
		p += 2;
		while (dlen > 0) {
//fprintf(stderr, "Tag:0x%02hhx Len:%hhd ", *p, *(p+1));
			switch (*p) {
			case 0x48:
				/* service-type == digital TV? */
				if (p[2] == 0x01) {
//fprintf(stderr, "--chname-len:%d \n", *(p+4+p[3]));
//{int i; for (i=0; i<14; i++) fprintf(stderr, "0x%02hhx ", *(p+4+p[3]+1+i)); fprintf(stderr,"\n");}
					aribstr_to_utf8(p + 4 + p[3] + 1, *(p + 4 + p[3]),
						chname, sizeof(chname));
					printf("%s:DTV_DELIVERY_SYSTEM=%d",
						chname, (mode != TER)? SYS_ISDBS : SYS_ISDBT);

					if (mode != TER && volt)
						printf("|DTV_VOLTAGE=1");

					printf("|DTV_FREQUENCY=%d", freq);

					if (mode != TER)
						printf("|DTV_ISDBS_TS_ID=0x%04x", ts_id);

					printf(":%d\n", svc_id);
				}
				break;
			}
			dlen -= p[1] + 2;			
			p += p[1] + 2;
		}
	}
}

static const char *usage = "\n"
	"usage %s [options...]\n"
	"	-a N	use /dev/dvb/adapterN\n"
	"	-f N	use /dev/dvb/adapter?/frontendN\n"
	"	-d N	use /dev/dvb/adapter?/demuxN\n"
	"	-w N	max wait for lock [N * 0.1sec], default=5\n"
	"	-l	use channel list to scan (read from stdin)\n"
	"	-p	set LNB power on\n"
	"	-v	include the LNB voltage setting in the output\n"
	"	-b	BS mode\n"
	"	-c	CS110 mode\n";


int
main(int argc, char **argv)
{
	int opt;
	int adapter = 0, frontend = 0, demux = -1;
	char fname[32], dname[32];
	int wtime = 5;
	int lnb = 0;

	struct dtv_property tvp[4];
	struct dtv_properties feprops;
	struct dmx_sct_filter_params sdtfilter;
	fe_status_t status;

	int fe, fd;
	struct pollfd pfd;

	struct sigaction act;

	int i, ret;
	unsigned char sdt[1024];
	int len;

	while ((opt = getopt(argc, argv, "a:f:d:w:lpvbc")) != -1) {
		switch (opt) {
		case 'a':
			adapter = strtoul(optarg, NULL, 0);
			break;
		case 'f':
			frontend = strtoul(optarg, NULL, 0);
			break;
		case 'd':
			demux = strtoul(optarg, NULL, 0);
			break;
		case 'w':
			wtime = strtoul(optarg, NULL, 0);
			if (wtime < 1)
				wtime = 5;
			break;
		case 'l':
			scan_all = 0;
			break;
		case 'p':
			lnb = 1;
			break;
		case 'v':
			volt = 1;
			break;
		case 'b':
			mode = BS;
			break;
		case 'c':
			mode = CS110;
			break;
		default:
			fprintf(stderr, usage, argv[0]);
			return -1;
		};
	}
	snprintf(fname, sizeof(fname),
		"/dev/dvb/adapter%i/frontend%i", adapter, frontend);

	if (demux == -1)
		demux = frontend;
	snprintf(dname, sizeof(dname),
		"/dev/dvb/adapter%i/demux%i", adapter, demux);


	fe = open(fname, O_RDWR);
	if (fe < 0) {
		perror(" FE open failed.");
		return -fe;
	}
	if (ioctl(fe, FE_SET_FRONTEND_TUNE_MODE, FE_TUNE_MODE_ONESHOT)) {
		fprintf(stderr, "FE: setting tune-mode failed.\n");
		return -1;
	}

	fd = open(dname, O_RDONLY);
	if (fd < 0) {
		perror(" demuxer open failed.");
		close(fe);
		return -fd;
	}
	memset(&sdtfilter, 0, sizeof(sdtfilter));
	sdtfilter.pid = 0x11;
	sdtfilter.filter.filter[0] = 0x42;
	sdtfilter.filter.mask[0] = 0xFF;
	if (mode != TER) {
		// mask & mode for TS_ID filtering.
		// Note the strange indices of 1 and 2,
		//    not the offset (3, 4) of ts_id field in the SDT section,
		//    because dvb-core/dmxdev.c strangely requires to skip
		//    the 2nd & 3rd bytes of the section in section filters.
		sdtfilter.filter.mask[1] = 0xFF;
		sdtfilter.filter.mask[2] = 0xFF;
	}
	sdtfilter.flags = DMX_IMMEDIATE_START;
	pfd.fd = fd;
	pfd.events = POLLIN;

	if (!scan_all) {
		INFILE = stdin;
	} else if (mode == BS) {
		CH_IDX = 1;
		CH_IDX_END = 23;
	} else if (mode == CS110) {
		CH_IDX = 2;
		CH_IDX_END = 24;
	}else {
		CH_IDX = 13;
		CH_IDX_END = 62;
	}

	feprops.props = tvp;
	sigemptyset(&act.sa_mask);
	act.sa_handler = stop_loop;
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	if (lnb) {
		tvp[0].cmd = DTV_VOLTAGE;
		tvp[0].u.data = SEC_VOLTAGE_18;
		feprops.num = 1;
		ioctl(fe, FE_SET_PROPERTY, &feprops);
	}

	cont = 1;
	while (cont && !next_ch()) {
		ioctl(fd, DMX_STOP, 1);
		usleep( 500 * 1000);
		tvp[0].cmd = DTV_FREQUENCY;
		tvp[0].u.data = freq;
		if (mode != TER) {
			tvp[1].cmd = DTV_ISDBS_TS_ID;
			tvp[1].u.data = ts_id;
			tvp[2].cmd = DTV_TUNE;
			tvp[2].u.data = 1;
			feprops.num = 3;
		} else {
			tvp[1].cmd = DTV_TUNE;
			tvp[1].u.data = 1;
			feprops.num = 2;
		}
		fprintf(stderr, "trying freq:%u (tsid:%04x)...", freq, ts_id);
		if (ioctl(fe, FE_SET_PROPERTY, &feprops) < 0) {
			fprintf(stderr, " FE set channel failed.\n");
			continue;
		}
		status = 0;
		for (i = 0; i < wtime; i++) {
			usleep (100 * 1000);
			if (ioctl (fe, FE_READ_STATUS, &status) < 0) {
				perror ("FE_READ_STATUS");
				break;
			}
			if (status & FE_HAS_LOCK)
				break;
		}
		if (!(status & FE_HAS_LOCK)) {
			fprintf(stderr, " no lock. skipped.\n");
			continue;
		}
		fprintf(stderr, "locked..");

		if (mode != TER) {
			sdtfilter.filter.filter[1] = (ts_id & 0xFF00) >> 8;
			sdtfilter.filter.filter[2] = ts_id  & 0x00FF;
		}

		if (ioctl(fd, DMX_SET_FILTER, &sdtfilter) < 0) {
			perror("failed to request SDT");
			continue;
		}

		if (poll(&pfd, 1, 1000 * wtime) <= 0) {
			fprintf(stderr, "timeout/failed to get SDT.\n");
			continue;
		}
		if (!(pfd.revents & POLLIN)) {
			fprintf(stderr, "failed to get SDT.\n");
			continue;
		}
		ret = read(pfd.fd, sdt, 3);
		len = ((sdt[1] & 0x0F) << 8) + sdt[2];
		if (ret != 3 || len > 1021 || len < 17) {
			fprintf(stderr, "revcieved broken SDT(ret:%d len:%d).\n", ret, len);
			continue;
		}
		if (read(pfd.fd, sdt + 3, len) < len) {
			fprintf(stderr, "received incomplete SDT.\n");
			continue;
		}
		make_output(sdt, len + 3);
		fprintf(stderr, "ok.\n");
	}
	
bailout:
	if (lnb) {
		tvp[0].cmd = DTV_VOLTAGE;
		tvp[0].u.data = SEC_VOLTAGE_OFF;
		feprops.num = 1;
		ioctl(fe, FE_SET_PROPERTY, &feprops);
	}
	close(fd);
	close(fe);
	return 0;
}
