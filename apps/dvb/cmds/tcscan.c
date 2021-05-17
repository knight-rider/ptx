/*
	Scan active channels, only usable with included TC90522 driver
	(c) Budi Rachmanto <info@are.ma>
*/
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <poll.h>
#include <string.h>
#include <sys/ioctl.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

bool cont = true;
void stop_loop(int sig)
{
	cont = false;
	return;
}

extern void aribstr_to_utf8(uint8_t *src, size_t srclen, uint8_t *dest, size_t destlen);

void make_output(uint8_t *sdt, int len, enum fe_delivery_system delsys,uint32_t freq)
{
	uint8_t		chname[48],
			*p = sdt + 11;

	while (p < sdt + len - 4) {
		uint16_t	service_id	= p[0] << 8 | p[1],
				dlen		= (p[3] & 0x0F) << 8 | p[4];
		p += 5;
		while (dlen > 0) {
			if (*p == 0x48 && p[2] == 0x01) {
				printf("%c %05d %d", delsys == SYS_ISDBT ? 'T' : 'S', service_id, freq);
				if (delsys == SYS_ISDBS)
					printf(" %04X", sdt[3] << 8 | sdt[4]);	// TSID
				aribstr_to_utf8(p + 4 + p[3] + 1, *(p + 4 + p[3]), chname, sizeof(chname));
				printf(" %s\n", chname);
			}
			dlen	-= p[1] + 2;
			p	+= p[1] + 2;
		}
	}
}

uint32_t ch2kHz(uint32_t ch)				// BS/CS110 base freq 10678000 kHz
{
	if (ch < 12)
		return 1049480 + 38360 * ch;		/* 00-11 BS	right	odd	*/
	else if (ch < 23)
		return 1068660 + 38360 * (ch - 12);	/* 12-22 BS	left	even	*/
	else if (ch < 35)
		return 1613000 + 40000 * (ch - 23);	/* 23-34 CS110	right	even	*/
	return 1553000 + 40000 * (ch - 35);		/* 35-47 CS110	left	odd	*/
}

uint32_t ch2Hz(uint32_t ch)
{
	return	(ch > 112 ? 557 : 93 + 6 * ch + (ch < 12 ? 0 : ch < 17 ? 2 : ch < 63 ? 0 : 2)) * 1000000 + 142857;
}

uint32_t getfreq(uint16_t *tsid, enum fe_delivery_system delsys)
{
	static uint8_t	ch	= 0,
			id	= 0;
	uint32_t	freq	= 0;

	if (delsys == SYS_ISDBS) {
		if (ch > 34)
			return 0;
		if (ch == 12)
			ch = 23;
		freq = ch2kHz(ch);
		*tsid = id;
		id++;
		if (id == 8){
			id = 0;
			ch++;
		}
	} else {
		if (ch > 62)
			return 0;
		if (!ch)
			ch = 13;
		freq = ch2Hz(ch + 50);
		*tsid = ch;
		ch++;
	}

	return freq;
}

int main(int argc, char **argv)
{
	char fname[32], dname[32];
	uint8_t sdt[1024];
	int	fe, fd, i, wtime = 5,
		ret, len,
		adapno = 0;
	struct pollfd pfd;
	struct dtv_property		fep[3];
	struct dtv_properties		feps = {.props = fep};
	enum fe_delivery_system		delsys;
	struct dmx_sct_filter_params	sdtfilter;
	struct sigaction		act;
	uint32_t	freq;
	uint16_t	tsid;

	if (argc < 2) {
		fprintf(stderr, "usage: %s adapter_no\n", argv[0]);
		return -1;
	}
	adapno = atoi(argv[1]);

	snprintf(fname, sizeof(fname), "/dev/dvb/adapter%i/frontend0", adapno);
	if ((fe = open(fname, O_RDWR)) < 0) {
		perror("FE open failed.");
		return -fe;
	}

	feps.num = 1;
	fep[0].cmd = DTV_ENUM_DELSYS;
	if (ioctl(fe, FE_GET_PROPERTY, &feps) < 0 || !fep[0].u.buffer.len) {
		perror("FE_GET_PROPERTY");
		goto OUT1;
	}
	delsys = fep[0].u.buffer.data[0];
	if ((delsys != SYS_ISDBS) && (delsys != SYS_ISDBT)) {
		perror("Unknown type of adapter");
		goto OUT1;
	}

	snprintf(dname, sizeof(dname), "/dev/dvb/adapter%i/demux0", adapno);
	if ((fd = open(dname, O_RDONLY)) < 0) {
		perror("demux open failed.");
		goto OUT1;
	}
	pfd.fd		= fd;
	pfd.events	= POLLIN;
	memset(&sdtfilter, 0, sizeof(sdtfilter));
	sdtfilter.pid			= 0x11;
	sdtfilter.filter.filter[0]	= 0x42;
	sdtfilter.filter.mask[0]	= 0xFF;
	sdtfilter.flags			= DMX_IMMEDIATE_START;

	sigemptyset(&act.sa_mask);
	act.sa_handler = stop_loop;
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	if (delsys == SYS_ISDBS) {
		fep[0].cmd = DTV_VOLTAGE;
		fep[0].u.data = SEC_VOLTAGE_18;
		feps.num = 1;
		ioctl(fe, FE_SET_PROPERTY, &feps);
	}

	cont = true;
	while (cont && (freq = getfreq(&tsid, delsys))) {
		fe_status_t status = 0;

		ioctl(fd, DMX_STOP, 1);
		usleep(500 * 1000);
		fep[0].cmd = DTV_FREQUENCY;
		fep[0].u.data = freq;
		if (delsys == SYS_ISDBS) {
			fep[1].cmd = DTV_STREAM_ID;
			fep[1].u.data = tsid;
			fep[2].cmd = DTV_TUNE;
			fep[2].u.data = 1;
			feps.num = 3;
		} else {
			fep[1].cmd = DTV_TUNE;
			fep[1].u.data = 1;
			feps.num = 2;
		}
		fprintf(stderr, "trying freq:%u (id:%d)...", freq, tsid);
		if (ioctl(fe, FE_SET_PROPERTY, &feps) < 0) {
			fprintf(stderr, " FE set channel failed.\n");
			continue;
		}
		for (i = 0; i < wtime; i++) {
			usleep(1000);
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
		fprintf(stderr, " locked:\n");

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
			fprintf(stderr, "received broken SDT(ret:%d len:%d).\n", ret, len);
			continue;
		}
		if (read(pfd.fd, sdt + 3, len) < len) {
			fprintf(stderr, "received incomplete SDT.\n");
			continue;
		}
		make_output(sdt, len + 3, delsys, freq);
		fprintf(stderr, "ok.\n");
	}

	if (delsys == SYS_ISDBS) {
		fep[0].cmd = DTV_VOLTAGE;
		fep[0].u.data = SEC_VOLTAGE_OFF;
		feps.num = 1;
		ioctl(fe, FE_SET_PROPERTY, &feps);
	}
	close(fd);
OUT1:
	close(fe);
}