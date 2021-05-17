/*
 * reads from stdin, makes jumped PTS consecutive.
 * example: ./restamp < in-file > out-file
 */
#include <stdio.h>
#include <inttypes.h>
#include <string.h>

#define PTS_MSB (1UL << 32)
#define PTS_MASK (~0x1ffffffffUL)
// 0.5sec for max allowable PTS jump
#define PTS_THRESHOLD (45000 * 300)

// returns (a - b), considering the PTS round over
uint64_t
pts_diff(uint64_t a, uint64_t b)
{
	if (a < b)
		a += (PTS_MSB << 1) * 300;
	return a - b;
}

uint64_t
get_pcr(unsigned char *p)
{
	uint64_t base;
	uint16_t ext;

	base = ((uint64_t)p[0] << 25) | ((uint64_t)p[1] << 17) |
			((uint64_t)p[2] << 9) | ((uint64_t)p[3] << 1) |
			(((uint64_t)p[4] & 0x80) >> 7);
	ext = ((uint16_t)(p[4] & 0x01) << 8) | (uint16_t)p[5];

	return base * 300 + ext;
}

uint64_t
get_pts(unsigned char *p)
{
	return (((uint64_t)(p[0] & 0x0e) << 29) | ((uint64_t)p[1] << 22) |
		((uint64_t)(p[2] & 0xfe) << 14) | ((uint64_t)p[3] << 7) |
		((uint64_t)p[4] >> 1));
}

void
set_pts(unsigned char *p, uint64_t val)
{
	p[0] &= 0xf1;
	p[0] |= (val >> 29) & 0x0e;
	p[1] = (val >> 22) & 0xff;
	p[2] = ((val >> 14) & 0xfe) + 1;
	p[3] = (val >> 7) & 0xff;
	p[4] = ((val << 1) & 0xfe) + 1;
}

void
print_pts(uint64_t pts)
{
	fprintf(stderr, "%#09" PRIx64, pts / 300);
}

int
main(int argc, char **argv)
{
	int c, i;
	int force;
	unsigned char buf[188];  // TS packet buffer
	unsigned long long pos = 0; // byte position of the input stream

	uint64_t offset;  // current PTS offset in 27MHz
	struct {
		uint64_t val; // previously received PTS/PCR in 27MHz
		unsigned int step;  // duration (in 27MHz) of AU
	} prev_pcr, prev_pes[51];  // PES-id: 0xbd .. 0xef

	uint16_t pid, pcr_pid;
	uint64_t base;
	unsigned int ext;
	uint64_t newpts, diff;

	unsigned char *p;
	unsigned char st;

	static const unsigned int fr_steps[] = {
		1, // forbidden
		27000000UL * 1001 / 24000, // 23.976fps
		27000000 / 24,  // 24fps
		27000000 / 25,
		27000000UL * 1001 / 30000,
		27000000 / 30,
		27000000 / 50,
		27000000UL * 1001 / 60000,
		27000000 / 60,
		1, 1, 1, 1, 1, 1
	};
	// duration of an aac frame (1024 sample)
	static const unsigned int sr_steps[] = {
		27000000UL * 1024 / 96000,  // 96kHz
		27000000UL * 1024 / 88200,
		27000000UL * 1024 / 64000,
		27000000UL * 1024 / 48000,
		27000000UL * 1024 / 44100,
		27000000UL * 1024 / 32000,
		27000000UL * 1024 / 24000,
		27000000UL * 1024 / 22050,
		27000000UL * 1024 / 16000,
		27000000UL * 1024 / 12000,
		27000000UL * 1024 / 11025,
		27000000UL * 1024 / 8000,
		1, 1, 1, 1
	};

	force = 0;
	if (argc > 0 && !strncmp("-f", argv[0], 2))
		force = 1;

	// init
 	// start PTS with margin: 2sec
#define START_PTS 54000000
	offset = 0; // will be corrected later
	prev_pcr.val = START_PTS;
	//prev_pcr.step = 0;

	for (i = 3; i < 35; i++) {
		prev_pes[i].step = 1920 * 300; // 90000 * 1024 / 48k = 1920
		prev_pes[i].val = START_PTS - prev_pes[i].step;
	}
	for (i = 35; i < 51; i++) {
		prev_pes[i].step = 3003 * 300; // 90000 / 29.97 = 3003
		prev_pes[i].val = START_PTS - prev_pes[i].step;
	}
	pcr_pid = 0x2000;

	buf[0] = 0x47;
	for(;;) {
		i = 0;
		while ((c = getchar()) != -1 && c != '\x47') {
			if (!(i++ % 10)) fprintf(stderr, ".");
			pos++;
		}
		if (i) fprintf(stderr, "\n");

		if (c != '\x47') break;

		if (fread(buf + 1, sizeof(buf) - 1, 1, stdin) < 1)
			 break;
		pos += sizeof(buf);

		pid = ((buf[1] & 0x1f) << 8) + buf[2];
		if (pid < 0x30 || pid == 0x1fff || (buf[1] & 0x80))
			goto next;

		/* PCR in adaptation field */
		if ((buf[3] & 0x20) && buf[4] >= 7 && (buf[5] & 0x10)) {
			if (pcr_pid != 0x2000 && pid != pcr_pid && !force) {
				fprintf(stderr, "WARNING... "
					"multiple PCR streams were found.\n"
					"This program cannot handle multiple"
					" programs(or PCRs) correctly.\n"
					"Run with the \"-f\" option if you"
					" want to force processing.\n");
				goto bailout;
			}

			pcr_pid = pid;
			if (buf[4] >= 12 && (buf[5] & 0x08))
				base = get_pcr(buf + 13);
			else
				base = get_pcr(buf + 6);

			newpts = pts_diff(base, offset);
			// diff := (base - offset) - prev_pcr.val
			diff = pts_diff(newpts, prev_pcr.val);
			if (diff > PTS_THRESHOLD &&
			    pts_diff(prev_pcr.val, newpts) > PTS_THRESHOLD) {
				// PTS jump!!
				fprintf(stderr,	"PCR jump from:");
				print_pts(prev_pcr.val);
				fprintf(stderr,	", diff:");
				print_pts(diff);
				fprintf(stderr,	", orig:");
				print_pts(base);
				fprintf(stderr,	"\n");

				newpts = prev_pcr.val + 4500 * 300;
				offset = pts_diff(base, newpts);
				fprintf(stderr, "new offset:");
				print_pts(offset);
				fprintf(stderr, " at pos:%#llx\n", pos - sizeof(buf));
			}
			prev_pcr.val = newpts;

			// modify PCR in the TS packet
			base = newpts / 300;
			ext = newpts % 300;
			buf[6] = (base & 0x1fe000000UL) >> 25;
			buf[7] = (base & 0x1fe0000UL) >> 17;
			buf[8] = (base & 0x1fe00UL) >> 9;
			buf[9] = (base & 0x1feUL) >> 1;
			buf[10] = (base & 0x1UL) << 7;
			buf[10] += 0x3e + ((ext & 0x100) >> 8);
			buf[11] = ext & 0xff;
		}

		if (!(buf[1] & 0x40) || !(buf[3] & 0x10))
			goto next;
		p = buf + 4;
		if (buf[3] & 0x20)
			p += (*p) + 1;

		if (p + 14 >= buf + sizeof(buf)) {
			fprintf(stderr, "--- %#04hx\n", pid);
			goto next;
		}
		if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01) {
			goto next;
		}

		st = p[3];
		if (st != 0xbd && (st < 0xbf || st > 0xef))
			goto next;
		st -= 0xbd;

		// check DTS first
		if (p[7] & 0x40) {
			// has DTS in (Video) PES packet header
			base = get_pts(p + 14);
			base *= 300;
			newpts = pts_diff(base, offset);

			// check jump only when MPEG2 Video PES 
			diff = pts_diff(newpts, prev_pes[st].val);
			if (st >= 3 &&
			    diff > 2 * PTS_THRESHOLD &&
			    pts_diff(prev_pes[st].val, newpts) > 2 * PTS_THRESHOLD) {
				// change offset!
				fprintf(stderr,	"DTS jump from:");
				print_pts(prev_pes[st].val);
				fprintf(stderr,	", diff:");
				print_pts(diff);
				fprintf(stderr,	", orig:");
				print_pts(base);
				fprintf(stderr,	"\n");

				newpts = prev_pes[st].val + prev_pes[st].step;
				offset = pts_diff(base, newpts);
				fprintf(stderr, "new offset[%#02hhx]:", st + 0xbd);
				print_pts(offset);
				fprintf(stderr, " at pos:%#llx\n", pos - sizeof(buf));
			}

			// modify DTS in the TS packet
			base = newpts / 300;
			set_pts(p + 14, base);
			prev_pes[st].val = newpts;

			// get framerate and set prev_pes[].step for Video PES
			if (st >= 35) {
				for (i = 9 + p[8]; p + i + 7 < buf + sizeof(buf);) {
					if (p[i + 1] != 0)									i += 2;
					else if (p[i + 2] > 1)
						i += 3;
					else if (p[i + 2] == 0)
						i ++;
					else if (p[i] != 0 || p[i + 3] != 0xb3)
						i += 3;
					else {
						prev_pes[st].step =
							fr_steps[p[i + 7] & 0x0f];
						break;
					}
				}
			}
		}

		// check PTS, update offset only if audio PES
		if (p[7] & 0x80) {
			base = get_pts(p + 9);
			base *= 300;
			newpts = pts_diff(base, offset);

			// check jump only when DTS is not specified
			diff = pts_diff(newpts, prev_pes[st].val);
			if (st >= 3 && !(p[7] & 0x40) &&
			    diff > 2 * PTS_THRESHOLD &&
			    pts_diff(prev_pes[st].val, newpts) > 2 * PTS_THRESHOLD) {
				// change offset!
				fprintf(stderr,	"PTS jump from:");
				print_pts(prev_pes[st].val);
				fprintf(stderr,	", diff:");
				print_pts(diff);
				fprintf(stderr,	", orig:");
				print_pts(base);
				fprintf(stderr,	"\n");

				newpts = prev_pes[st].val + prev_pes[st].step;
				offset = pts_diff(base, newpts);
				fprintf(stderr, "new offset[%#02hhx]:", st + 0xbd);
				print_pts(offset);
				fprintf(stderr, " at pos:%#llx\n", pos - sizeof(buf));
			}

			// modify PTS in the TS packet
			base = newpts / 300;
			set_pts(p + 9, base);
			prev_pes[st].val = newpts;

			// get framerate and set prev_pes[].step for AAC PES
			if (st >= 3 && st < 35 &&
			    p[9 + p[8]] == 0xff && (p[10 + p[8]] & 0xf6) == 0xf0) { 
				prev_pes[st].step =
					sr_steps[(p[11 + p[8]] & 0x3c) >> 2];
			} else if (st >= 35 && !(p[7] & 0x40)) {
				for (i = 9 + p[8]; p + i + 7 < buf + sizeof(buf);) {
					if (p[i + 1] != 0)									i += 2;
					else if (p[i + 2] > 1)
						i += 3;
					else if (p[i + 2] == 0)
						i ++;
					else if (p[i] != 0 || p[i + 3] != 0xb3)
						i += 3;
					else {
						prev_pes[st].step =
							fr_steps[p[i + 7] & 0x0f];
						break;
					}
				}
                        }
		}

		// check ESCR
		if (!(p[7] & 0x20))
			goto next;

		p += 9 + ((p[7] & 0x80) ? 5 : 0) + ((p[7] & 0x40) ? 5 : 0);
		if (p + 6 >= buf + sizeof(buf)) {
			fprintf(stderr, "--- %#04hx\n", pid);
			goto next;
		}

		base = ((p[0] &0x38) << 27) | ((p[0] & 0x03) << 28) |
			(p[1] << 20) | ((p[2] & 0xf8) << 12) |
			((p[2] & 0x03) << 13) | (p[3] << 5) |
			((p[4] & 0xf8) >> 3);
		ext = ((p[4] & 0x03) << 7) | (p[5] >> 1);
		base = base * 300 + ext;

		newpts = pts_diff(base, offset);

		// modify ESCR
		base = newpts / 300;
		ext = newpts % 300;
		p[0] = (base << 29) & 0x38;
		p[0] |= 0x04;
		p[0] |= (base << 28) & 0x03;
		p[1] = (base << 20) & 0xff;
		p[2] = (base << 12) & 0xf8;
		p[2] |= 0x04;
		p[2] |= (base << 13) & 0x03;
		p[3] = (base << 5) & 0xff;
		p[4] = (base << 3) & 0xf8;
		p[4] |= 0x04;
		p[4] |= (ext >> 7) & 0x03;
		p[5] = ((ext << 1) & 0xfe) + 1;

next:
		if (fwrite(buf, sizeof(buf), 1, stdout) < 1) {
			perror("failed to output.");
			goto bailout;
		}
	}

	fprintf(stderr, "process finished. (%#llx bytes)\n", pos);
	return 0;

bailout:
	fprintf(stderr, "aborted at %#llx\n", pos);
	return 1;
}
