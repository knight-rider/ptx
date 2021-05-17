/*
 * dumpeid -p <programID> [-t <TIME>] [-i [-i]] [-w WAIT] < TS-stream
 *  ex1. use with dvb_sched_ev/dvbevrec.py
 *    SID=`convsid -p $CH`		# or CH=`convsid -s $SID`
 *    [ x"$SID" == x ] && exit 1	# or [ x"$CH" == x ] && exit 1
 *    EID=`gst-launch urldecodebin uri="dvb://$CH" caps=video/mpegts ! fdsink | \
 *                  dumpeid -p $SID -t "$TIME" -w 15 2> /dev/null`
 *    [ x"$EID" == x ] || EOPT='-e'
 *    dvb_sched_ev $TIME -s $SID $EOPT $EID -o out.ts
 *
 *  ex2. get eid, title, desc. out of the recoreded program
 *    dumpeid -p 0xABCD -i -i < foo.ts
 *
 * Licence: GPL ver.3
 */
#define _XOPEN_SOURCE
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>

//#define _DEBUG_NITSCAN_ 1
#include "nitscan.h"

extern void
aribstr_to_utf8(char *src, size_t srclen, char *dest, size_t destlen);

// undef val indicated by the 6digit BCD, 0xff:0xff:0xff
#define DURATION_UNDEF (165 * 3600 + 165 * 60 + 165)

static struct {
	long int progid;
	time_t when;
	unsigned long wait;
	int info;
	int others;
} Params = {
	 .progid = 0,
	 .when = 0,
	 .wait = 60,
	 .info = 0,
	 .others = 0,
	};

struct evinfo {
	uint16_t progid;
	uint16_t eid;
	struct tm start;
	int duration;
	struct {
		uint16_t progid;
		uint16_t eid;
	} group_orig; 
	char *title;
	unsigned int title_len;
	char *short_desc;
	unsigned int desc_len;
};

struct evinfo Result = { 0 };

static int
bcd2int(uint8_t v)
{
	return ((v & 0xf0) >> 4) * 10 + (v & 0x0f);
}

static void
mjd2tm(int mjd, struct tm *t)
{
	if (mjd == 0xffff) {
		t->tm_year = 0;
		t->tm_mon = t->tm_mday = 0;
		return;
	}

	t->tm_year = (int)((mjd - 15078.2) / 365.25);
	t->tm_mon = (int)((mjd - 14956.1 - (int) (t->tm_year * 365.25)) / 30.6001);
	t->tm_mday = mjd - 14956 - (int) (t->tm_year * 365.25) - (int) (t->tm_mon * 30.6001);
	if (t->tm_mon == 14 || t->tm_mon == 15) {
		t->tm_year++;
		t->tm_mon = t->tm_mon - 14;
	} else {
		t->tm_mon -= 2;
	}
}


static void
output_event(struct evinfo *ev)
{
	char *buf;
int i;

	if (Params.info == 0) {
		printf("%d\n", ev->eid);
		return;
	}

	printf("serviceID: 0x%04hx\n", ev->progid);
	printf("eventID: %d\n", ev->eid);

	printf("start: %s", asctime(&ev->start));

	printf("duration: ");
	if (ev->duration == DURATION_UNDEF)
		printf("--------\n");
	else
		printf("%02d:%02d:%02d\n",
			ev->duration/ 3600,
			(ev->duration % 3600) / 60,
			(ev->duration % 3600) % 60);


	if (ev->title_len) {
		buf = malloc(4 * ev->title_len);
		if (buf == NULL) {
			perror("Failed to convert the title to UTF-8.");
			return;
		}
		aribstr_to_utf8(ev->title, ev->title_len, buf, 4 * ev->title_len);
		if (*buf) {
			printf("title: %s\n", buf);
//for (i=0; i<ev->title_len; i++) {printf("%02hhx ", ev->title[i]); if (i % 8 == 7) printf("\n");} printf("\n");
		}
		free(buf);
	}

	if (ev->desc_len) {
		buf = malloc(4 * ev->desc_len);
		if (buf == NULL) {
			perror("Failed to convert the description to UTF-8.");
			return;
		}
		aribstr_to_utf8(ev->short_desc, ev->desc_len, buf, 4 * ev->desc_len);
		if (*buf) {
			printf("desc: ------\n");
			printf("%s\n", buf);
//for (i=0; i<ev->desc_len; i++) {printf("%02hhx ", ev->short_desc[i]); if (i % 8 == 7) printf("\n");} printf("\n");
			printf("------------\n");
			free(buf);
		}
	}

	return;
}


static int checkEIT(struct secbuf *sec, void *dummy)
{
	uint8_t *p;
	uint8_t tid;
	int len;
	uint16_t progid;
	int sect;
	int dlen;

	p = sec->buf;
	tid = p[0];
	// skip if TID is invalid
	if (Params.others == 0) {
		if (!(tid  == 0x4e || tid == 0x50 || tid == 0x51))
			return 0;
	} else {
		// exclude EIT_schedule_extended
		if (tid < 0x4e || tid > 0x67 || (tid > 0x57 && tid < 0x60))
			return 0;
	}

	len = (p[1] & 0x0f) << 8 | p[2] + 3;
	if (len < 18) {
		dprintf("broken (too short) EITp/f.\n");
		return 1;
	}

	progid = p[3] << 8 | p[4];
/*
	// filter by progid, but considering event-grouping in EIT_schedule(tid >= 0x50).
	if (Params.progid != progid && (Params.info == 0 || tid == 0x4e))
		return 0;
 */

	sect = p[6];
	// when==0 -> search from just EIT_present(tid:4e, sec:0) 
	if (Params.when == 0 && !(tid <= 0x4f && sect == 0))
		return 0;

	dlen = 0;
	for (p += 14; p + 12 < sec->buf + len - 4; p += dlen) {
		struct evinfo ev;
		int mjd;
		time_t e_begin, e_end;
		uint8_t *q;

		if (Params.progid == progid) {
			ev.progid = progid;
			ev.eid = p[0] << 8 | p[1];
			ev.group_orig.progid = 0;
			ev.group_orig.eid = 0;
		} else {
			ev.progid = 0;
			ev.eid = 0;
			ev.group_orig.progid = progid;
			ev.group_orig.eid = p[0] << 8 | p[1];
		}

		mjd = p[2] << 8 | p[3];
		mjd2tm(mjd, &ev.start);

		ev.start.tm_hour = bcd2int(p[4]);
		ev.start.tm_min = bcd2int(p[5]);
		ev.start.tm_sec = bcd2int(p[6]);
		ev.start.tm_isdst = 0;
		ev.duration = bcd2int(p[7]) * 3600 + bcd2int(p[8]) * 60 + bcd2int(p[9]);
		ev.title = NULL;
		ev.title_len = 0;
		ev.short_desc = NULL;
		ev.desc_len = 0;

		dlen = (p[10] & 0x0f)<<8 | p[11];
		p +=12;

		// check if this event contains Params.when...

		// temporary event insertion/delay/extention ...
		// if start time is undef ...
		if (ev.start.tm_hour == 165) // can be true only when 4e,4f/1
			return 0;

		e_begin = mktime(&ev.start);
		if (e_begin < 0) {
			fprintf(stderr, "Bad time data found in EIT.\n");
			continue;
		}

		if (Params.when == 0) // ->  (tid==0x4e or 0x4f && sect==0)
			goto info;

		if (tid <= 0x4f && Params.progid == progid && Params.when < e_begin) {
			fprintf(stderr, "The program already finished in the past.\n");
			exit(0);
		}

		// if duration is undef...  [but !(4e,4f/0 && when==0)]
		if (ev.duration == DURATION_UNDEF)
			return 0;

		// normal case. both start-time & duration defined (& when != 0)
		e_end = e_begin + ev.duration - 1;
		if (Params.when < e_begin)
			return 0;
		if (Params.when > e_end)
			continue;

		// time matched. check for event-group, info ...
info:
//fprintf(stderr, "ev svc:0x%04hx id:0x%04hx o-svc:0x%04hx o-id:0x%04hx dur:%d start:%s",
//	ev.progid, ev.eid, ev.group_orig.progid, ev.group_orig.eid,
//	ev.duration, asctime(&ev.start));
		if (Params.progid == progid && Params.info == 0)
			goto output;

		// check descriptors
		for (q = p; q + 2 < p + dlen; q += q[1] + 2) {
			if (q[0] == DESC_EVENT_GROUP && q[1] > 0 && (q[2] & 0xf0) == 0x10) {
				int i;
				uint16_t e = 0, s = 0;
				for (i = 0; i < (q[2] & 0x0f); i++) {
					s = q[3 + 4 * i + 0] << 8 | q[3 + 4 * i + 1];
					e = q[3 + 4 * i + 2] << 8 | q[3 + 4 * i + 3];
					if (s == Params.progid)
						break;
				}
				if (i == (q[2] & 0x0f)) { // not found
					if (Params.info > 1)
						ev.progid = 0;
					break;
				}
				ev.eid = e;
				ev.progid = s;
				if (!Params.info || ev.title)
					break;
			} else if (q[0] == DESC_EVENT_SHORT && Params.info) {
				ev.title_len = q[5];
				if (ev.title_len == 0)
					break;
				ev.title = q + 6;

				ev.desc_len = q[6 + ev.title_len ];
				if (ev.desc_len == 0)
					break;
				ev.short_desc = q + 7 + ev.title_len;
				if (ev.progid != 0)
					break;
			}
		}

output:
		if (ev.progid != 0) {
			Result = ev;
			Result.title_len = 0;
			Result.desc_len = 0;
			if (Params.info < 2 || ev.title) {
				output_event(&ev);
				exit(0);
			}
		}
		break;
	}

	return 0;
}


static void stop_loop(int sig)
{
	if (Params.info && Result.progid)
		output_event(&Result);
	else
		fprintf(stderr, "stopped by timer.\n");
	exit(0);
}

static void get_next_packet(uint8_t *buf)
{
	int i, j;
	size_t len;
	uint8_t *p;

	j = 0;
	do {
		len = fread(buf, sizeof(buf[0]), 188, stdin);
		if (len < 1) {
			fprintf(stderr, "failed to sync.\n");
			exit(1);
		}

		for (i = 0; buf[i] != 0x47 && i < len; i++)
			if (!(j++ % 10))
				dprintf(".");
	} while (i == len);

	if (i > 0) {
		len -= i;
		memmove(buf, buf + i, len);
	}

	if (j)
		dprintf("\n");

	for (p = buf + len; p < buf + 188; p += len) {
		len = fread(p, sizeof(buf[0]), buf + 188 - p, stdin);
		if (ferror(stdin)) {
			perror(" read failed.");
			exit(1);
		} else if (feof(stdin)) {
			fprintf(stderr, "finished by EOF.\n");
			exit(1);
		}
	}
}

static const char *usage = "\n"
	"usage %s -p ID [options] < TS-data-file\n"
	"	-p ID	program_ID(servie_ID) where EIT is searched\n"
	"	-o	search EIT_others as well\n"
	"	-t TIME	find the event at TIME (default: 0/first-found one)\n"
	"	-w SEC 	stop searching after SEC [sec.] at max. (default: 60)\n"
	"	-i	output event info as well (default: no, just eventID)\n"
	"	-i -i	search event info further (for a grouped event)\n";


int main(int argc, char **argv)
{
	struct sigaction act;
	struct itimerval it, itz;
	int opt;
	uint8_t buf[188];
	uint16_t pid;
	struct tm tbegin = { 0 };
	struct secbuf esec;
	uint8_t esbuf[4096];


	while ((opt = getopt(argc, argv, "p:ot:w:ih")) != -1) {
		switch (opt) {
		case 'p':
			Params.progid = strtol(optarg, NULL, 0);
			if (Params.progid <= 0 || Params.progid > 0xffff) {
				fprintf(stderr, "progid  must be within [0...0xffff].\n");
				return 2;
			}
			break;
		case 't':
			if (strptime(optarg, " %H:%M %Y-%m-%d ", &tbegin) != NULL)
				Params.when = mktime(&tbegin);
			else {
				time(&Params.when);
				localtime_r(&Params.when, &tbegin);
				tbegin.tm_sec = 0;
				if (!strptime(optarg, " %H:%M ", &tbegin))
					Params.when = -1;
				else
					Params.when = mktime(&tbegin);
				if (Params.when > 0 && Params.when < time(NULL))
					Params.when += 3600 * 24;
			}
			if (Params.when == -1) {
				fprintf(stderr, "invalid time format for \"-t\". use \"HH:MM [YYYY-mm-dd]\".\n");
				return 2;
			}
fprintf(stderr, "search for EIT on %s", ctime(&Params.when));
			Params.when += 30; // margin for wrong EIT start-time
			break;
		case 'w':
			Params.wait = strtol(optarg, NULL, 0);
			if (Params.wait <= 0) {
				fprintf(stderr, "-w must be > 0.\n");
				return 2;
			}
			if (Params.wait > 180)
				Params.wait = 180;
			break;
		case 'i':
			Params.info++;
			break;
		case 'o':
			Params.others = 1;
			break;
		case 'h':
		default:
			fprintf(stderr, usage, argv[0]);
			return 2;
		};
	}
	if (Params.progid == 0) {
		fprintf(stderr, "-p option is required.\n");
		return 2;
	}

	esec.len = 0;
	esec.max = sizeof(esbuf);
	esec.buf = esbuf;


	sigemptyset(&act.sa_mask);
	act.sa_handler = stop_loop;
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);
	sigaction(SIGALRM, &act, NULL);

	itz.it_interval.tv_sec = 0;
	itz.it_interval.tv_usec = 0;
	itz.it_value.tv_sec = 0;
	itz.it_value.tv_usec = 0;

	it.it_interval.tv_sec = 0; /* one shot */
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = Params.wait;
	it.it_value.tv_usec = 0;
	if (setitimer(ITIMER_REAL, &it, NULL) < 0) {
		perror("setitimer");
		exit(1);
	}

	buf[0] = 0; // mark as not synced initially
	while (1) {
		get_next_packet(buf);
		if (!(buf[3] & TS_F_PAYLOAD) || (buf[1] & TS_F_TSEI))
			continue;

		pid = ((buf[1] & 0x1f) << 8) + buf[2];
		if (pid == PID_EITH)
			doSection(buf, &esec, checkEIT, NULL);
	}

	return 0;
}
