#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#ifndef DTV_STREAM_ID
#define DTV_STREAM_ID 42
#endif

static const char *usage = "\n"
	"usage %s [options...] <frequency> [<ts_id>]\n"
	"	-a N	use /dev/dvb/adapterN\n"
	"	-f N	use /dev/dvb/adapter?/frontendN\n"
	"	-d N	use /dev/dvb/adapter?/demuxN\n";

static int cont = 1;

static void sigint_handler(int signo)
{
	cont = 0;
}

int
main(int argc, char **argv)
{
	int opt;
	int adapter = 0, frontend = 0, demux = -1;
	unsigned long freq;
	unsigned long ts_id = 0;

	char fname[32], dname[32];

	struct dtv_property tvp[4];
	struct dtv_properties feprops;

	int fe, fd;
	struct dmx_pes_filter_params dparam;
	struct sigaction sa;
	unsigned long status;

	while ((opt = getopt(argc, argv, "a:f:d:")) != -1) {
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
		default:
			fprintf(stderr, usage, argv[0]);
			return -1;
		};
	}
	if (optind >= argc) {
		fprintf(stderr, usage, argv[0]);
		return -1;
	}
	freq = strtoul(argv[optind++], NULL, 0);
	if (optind < argc)
		ts_id = strtoul(argv[optind], NULL, 0);

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

	fd = open(dname, O_RDWR);
	if (fd < 0) {
		perror(" demuxer open failed.");
		close(fe);
		return -fd;
	}

	feprops.props = tvp;
	if (ts_id > 0) {
		tvp[0].cmd = DTV_VOLTAGE;
		tvp[0].u.data = SEC_VOLTAGE_18;
		feprops.num = 1;
		ioctl(fe, FE_SET_PROPERTY, &feprops);
	}

	tvp[0].cmd = DTV_FREQUENCY;
	tvp[0].u.data = freq;
	if (ts_id > 0) {
		tvp[1].cmd = DTV_STREAM_ID;
		tvp[1].u.data = ts_id;
		tvp[2].cmd = DTV_TUNE;
		tvp[2].u.data = 1;
		feprops.num = 3;
	} else {
		tvp[1].cmd = DTV_TUNE;
		tvp[1].u.data = 1;
		feprops.num = 2;
	}
	fprintf(stderr, "tuning to freq:%u (tsid:%04x)...\n", freq, ts_id);
	if (ioctl(fe, FE_SET_PROPERTY, &feprops) < 0) {
		perror(" FE set channel failed.");
		goto bailout;
	}

	memset(&dparam, 0, sizeof(dparam));
	dparam.pid = 0x2000;
	dparam.input = DMX_IN_FRONTEND;
	dparam.output = DMX_OUT_TS_TAP;
	dparam.pes_type = DMX_PES_OTHER;
	dparam.flags = DMX_IMMEDIATE_START;
	/*
	if (ioctl(fd, DMX_SET_SOURCE, DMX_SOURCE_FRONT0 + frontend) < 0) {
		perror(" failed to set fe on dmx.");
		goto bailout;
	}
	*/
	if (ioctl(fd, DMX_SET_PES_FILTER, &dparam) < 0) {
		perror(" ioctl on demuxer failed.");
		goto bailout;
	}

	sleep(1);
	sa.sa_handler = &sigint_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESETHAND;
	sigaction(SIGINT, &sa, NULL);

	while (cont) {
		status = 0;
		if (ioctl (fe, FE_READ_STATUS, &status) < 0) {
			perror ("FE_READ_STATUS");
			break;
		}
		if (!(status & FE_HAS_LOCK)) {
			fprintf(stderr, " no lock. skipped.\n");
			break;
		}

		if (!cont)
			break;
		sleep(2);
	}

bailout:
	ioctl(fd, DMX_STOP, 1);
	if (ts_id > 0) {
		tvp[0].cmd = DTV_VOLTAGE;
		tvp[0].u.data = SEC_VOLTAGE_OFF;
		feprops.num = 1;
		ioctl(fe, FE_SET_PROPERTY, &feprops);
	}
	close(fd);
	close(fe);
	fprintf(stderr, "finished.\n");
	return 0;
}
