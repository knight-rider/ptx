#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>

#include <linux/dvb/dmx.h>
#include <linux/dvb/frontend.h>

#include "nitscan.h"

static volatile sig_atomic_t cont = 1;

static void stop_loop(int sig)
{
	cont = 0;
}

static const char *get_pestype_str(uint8_t ptype)
{
	switch (ptype) {
	case 0x01:
		return "MPEG1";
	case 0x02:
		return "MPEG2";
	case 0x06:
		return "Subttl";
	case 0x0D:
		return "Data";
	case 0x0F:
		return "AAC";
	case 0x1B:
		return "H264";
	}
	return "Unk.";
}

static const char *get_svctype_str(uint8_t stype)
{
	switch (stype) {
	case 0x01:
		return "digiTV";
	case 0x02:
		return "digiRADIO";
	case 0xC0:
		return "Data";
	case 0xA1:
		return "tempo.Video";
	case 0xA2:
		return "tempo.Audio";
	case 0xA3:
		return "tempo.Data";
	case 0xA4:
		return "Engineering";
	case 0xAA:
		return "BookmarkList";
	}
	return "Unk";
}

static int dump_si(struct isdbt_si *si)
{
	int i, j;
	int rc = 0;
	char pname[8], vpid[48], apid[32];
	struct pmt *pmt;
	char *vp, *ap;

	if (si->pat.ver == -1) {
		fprintf(stderr, " PAT not recived.\n");
		return -1;
	}

	if (si->nit.ver != -1)
		rc = si->nit.ts_info[0].rc_key_id;

	fprintf(stderr, "PAT: v%d ts-id:[%04hx], %d programs.\n",
		si->pat.ver, si->pat.ts_id, si->pat.num_prog);

	for (i=0; i<si->pat.num_prog; i++) {
		pmt = si->pat.prog[i].pmt;
		fprintf(stderr, "  PMT: prog[%04hx] pid[%04hx]",
			si->pat.prog[i].prog_id, si->pat.prog[i].pmt_pid);

		if (!pmt || pmt->ver == -1) {
			fprintf(stderr, " not recieved.\n");
			continue;
		}

		fprintf(stderr, " v%d pcr[%04hx]", pmt->ver, pmt->pcr_pid);
		if (pmt->prog_ecm.ca_pid == CA_PID_OFF)
			fprintf(stderr, " ca[----]");
		else
			fprintf(stderr, " ca[%04hx]", pmt->prog_ecm.ca_pid);

		fprintf(stderr, " #PES: %d\n", pmt->num_es);
		if (!pmt->num_es) {
			fprintf(stderr, "\n");
			continue;
		}

		if (si->nit.ver != -1) {
			for (j=0; j<si->nit.ts_info[0].num_service; j++) {
				if (pmt->prog_id == si->nit.ts_info[0].services[j].service_id)
					break;
			}

			if (j < si->nit.ts_info[0].num_service) {
				fprintf(stderr, "        rc-key:%02hhu svc-type:%s %s%strans-type:0x%02hhx", 
					si->nit.ts_info[0].rc_key_id,
					get_svctype_str(si->nit.ts_info[0].services[j].service_type),
					si->nit.ts_info[0].services[j].partial ?
					"partial ":"",
					si->nit.ts_info[0].services[j].primary ?
					"primary ":"",
					si->nit.ts_info[0].services[j].ttype);
			}
		}

		if (si->sdt.ver != -1) {
			for (j=0; j<si->sdt.num_service; j++) {
				if (pmt->prog_id == si->sdt.service[j].service_id)
					break;
			}

			if (j < si->sdt.num_service) {
				fprintf(stderr, " svc-flag:%02hhx", 
					si->sdt.service[j].flag);
			}
		}
		fprintf(stderr, "\n");

		if (rc)
			snprintf(pname, sizeof(pname), "%02d%1d", rc, i);
		else
			snprintf(pname, sizeof(pname), "[%04hx]", pmt->prog_id);

		vp = vpid;
		ap = apid;
		for (j=0; j<pmt->num_es; j++) {
			fprintf(stderr,
			        "        PES[%04hx]: type:%s tag:%02hhx",
				pmt->es[j].es_pid,
				get_pestype_str(pmt->es[j].es_type),
			        pmt->es[j].tag);
			if (pmt->es[j].es_ecm.ca_pid != 0)
				fprintf(stderr, "  ecm:%04hx",
				        pmt->es[j].es_ecm.ca_pid);
			fprintf(stderr, "\n");
		}
	}

	if (si->nit.ver != -1) {
		fprintf(stderr, "NIT emm-TS[%04hx] power:%hhd\n",
			si->nit.emm_ts.ts_id, 
			si->nit.emm_ts.power_supply_period);
		
	}

	if (si->cat.ver != -1 && si->cat.num_cas) {
		fprintf(stderr,	"CAT v%hhd %d cas. "
			"[0]casid:%04hx EMM[%04hx] type:%02hhx\n",
			si->cat.num_cas, si->cat.emm[0].cas_id,
			si->cat.emm[0].ca_pid, si->cat.emm[0].type);
	}
	return 0;
}


static const char *usage = "\n"
	"usage %s [-c] < TS-data-file\n";


int main(int argc, char **argv)
{

	int fd, cfd;
	struct dvb_frontend_parameters param;
	struct dmx_pes_filter_params flt;
	fe_status_t stat;
	struct sigaction act;
	struct itimerval it, itz;
	struct isdbt_si si;
	int opt, i, c, ret;
	size_t len;
	uint8_t buf[188];
	uint8_t *p;
	uint16_t pid;
	int retry;
	int wait_cat;

	wait_cat = 0;
	while ((opt = getopt(argc, argv, "hc")) != -1) {
		switch (opt) {
		case 'c':
			wait_cat = 1;
			break;
		default:
			fprintf(stderr, usage, argv[0]);
			return -1;
		};
	}

	sigemptyset(&act.sa_mask);
	act.sa_handler = stop_loop;
	act.sa_flags = 0;
	if (sigaction(SIGINT, &act, NULL) || sigaction(SIGALRM, &act, NULL))
		return 1;

	itz.it_interval.tv_sec = 0;
	itz.it_interval.tv_usec = 0;
	itz.it_value.tv_sec = 0;
	itz.it_value.tv_usec = 0;

	c = 0;

	clean_si(&si);
	init_si(&si);

	it.it_interval.tv_sec = 0; /* one shot */
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = 10;
	it.it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &it, NULL)) {
		perror("setitimer");
		return -1;
	}

	while (cont) {
		i = 0;
		while (cont && fread(buf, sizeof(buf[0]), 1, stdin) == 1 &&
				buf[0] != '\x47') {
			if (!(i++ % 10))
				fprintf(stderr, ".");
		}
		if (i)
			fprintf(stderr, "\n");

		if (buf[0] != '\x47') {
			continue;
		}
 
		p = &buf[1];
		retry = 0;
		do {
			len = fread(p, sizeof(buf[0]), buf + 188 - p, stdin);
			if (len < buf + 188 - p) {
				if (!feof(stdin))
					perror(" read failed.");
				else
					fprintf(stderr, "EOF.\n");
				goto bailout;
			}
			p += len;
		} while (p < buf + 188);

		if (!(buf[3] & TS_F_PAYLOAD)) continue;

		pid = ((buf[1] & 0x1f) << 8) + buf[2];
		if (pid == PID_PAT) {
			doSection(buf, &si.pat.sec, doPAT, &si.pat);
			continue;
		} else if (pid == PID_CAT) {
			doSection(buf, &si.cat.sec, doCAT, &si.cat);
			continue;
		} else if (pid == PID_NIT) {
			doSection(buf, &si.nit.sec, doNIT, &si.nit);
			continue;
		} else if (pid == PID_SDT) {
			doSection(buf, &si.sdt.sec, doSDT, &si.sdt);
			continue;
		} else if (pid == PID_TOT) {
			doSection(buf, &si.tot.sec, doTOT, &si.tot);
			continue;
		} else if (pid == PID_EITH) {
			doSection(buf, &si.esec, doEITH, &si.pat);
			continue;
		}

		for (i=0; si.pat.ver != -1 && i<si.pat.num_prog; i++) {
			if (!si.pat.prog[i].pmt)
				continue;

			if (pid == si.pat.prog[i].pmt_pid) {
				doSection(buf, &si.pat.prog[i].pmt->sec,
					  doPMT, si.pat.prog[i].pmt);
				break;
			}
		}

		if (si.pat.ver != -1 && si.nit.ver != -1 &&
		    si.sdt.ver != -1 && (!wait_cat || si.cat.num_cas > 0)) {
			for (i=0; i<si.pat.num_prog; i++)
				if (si.pat.prog[i].pmt->ver == -1)
					break;
			if (i == si.pat.num_prog)
				break;
		}
	}

bailout:
	ret = dump_si(&si);
	
	return ret;
}
