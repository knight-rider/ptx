/*
 * read from stdin, find each packet, print its header info to stdout.
 * example: ./ptsdump < in-file | lv
 */
#define _LARGEFILE64_SOURCE
#include <inttypes.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

void print_pts(uint64_t base, int ret)
{
	int i;

	printf("0x%09" PRIX64, base);

	printf(" %02lu:%02d:%02d.%03d",
		base / (90000 * 3600),
		(base / (90000 * 60)) % 60,
		(base / 90000) % 60,
		(base / 90) % 1000);
	if (ret)
		printf("\n");
}

int main(int argc, char **argv)
{
	int c, i;
	unsigned char buf[188];

	uint16_t pid;
	uint64_t base;
	uint16_t ext;
	uint8_t *p;
	unsigned char st;
	off64_t ofs = 0;

	for(;;) {
		i = 0;
		while ((c = getchar()) != -1 && c != '\x47') {
			ofs++;
			if (!(i++ % 10)) printf(".");
		}
		if (i) printf("\n");

		if (c != '\x47') return (1);

		if (fread(buf + 1, 1, sizeof(buf) - 1, stdin) < sizeof(buf) - 1)
			 return (0);
		ofs += sizeof(buf);
		pid = ((buf[1] & 0x1f) << 8) + buf[2];
		if (pid < 16 || pid == 0x1fff)
			continue;
		if ((buf[3] & 0x20) && buf[4] >= 7 && (buf[5] & 0x10)) {
//printf("[%02hhX %02hhX %02hhX %02hhX %02hhX] ", buf[6], buf[7], buf[8], buf[9], buf[10]);
			base = ((uint64_t)buf[6] << 25) | ((uint64_t)buf[7] << 17) |
				((uint64_t)buf[8] << 9) | ((uint64_t)buf[9] << 1) |
				((uint64_t)(buf[10] & 0x80) >> 7);
			ext = ((uint16_t)(buf[10] & 0x01) << 8) | (uint16_t)buf[11];
			printf("0x%04hX PCR ", pid);
			print_pts(base, 0);
			if (buf[4] >= 12 && (buf[5] & 0x08)) {
				printf("  OPCR ");
				base = ((uint64_t)buf[12] << 25) |
					((uint64_t)buf[13] << 17) |
					((uint64_t)buf[14] << 9) |
					((uint64_t)buf[15] << 1) |
					((uint64_t)(buf[16] & 0x80) >> 7);
				print_pts(base, 0);
			}
			printf("    @%08llX\n", ofs - sizeof(buf));
		}

		if (!(buf[1] & 0x40) || !(buf[3] & 0x10) || (buf[1] & 0x80))
			continue;
		p = buf + 4;
		if (buf[3] & 0x20)
			p += (*p) + 1;
		if (p + 14 >= buf + sizeof(buf)) {
			printf("--- %#04hX\n", pid);
			continue;
		}
		if (p[0] != 0x00 || p[1] != 0x00 || p[2] != 0x01) {
			continue;
		}
		st = p[3];
		if (p[7] & 0x40) {
			if (p + 19 > buf + sizeof(buf)) {
				printf("--- 0x%04hX\n", pid);
				continue;
			}
			base = ((uint64_t)(p[14] & 0x0e) << 29) | ((uint64_t)p[15] << 22) |
				((uint64_t)(p[16] & 0xfe) << 14) | ((uint64_t)p[17] << 7) |
				((uint64_t)p[18] >> 1);
			printf("  0x%02hhX ", st);
			printf("DTS ");
			print_pts(base, 0);
		}

		if (p[7] & 0x80) {
			base = ((uint64_t)(p[9] & 0x0e) << 29) | ((uint64_t)p[10] << 22) |
				((uint64_t)(p[11] & 0xfe) << 14) | ((uint64_t)p[12] << 7) |
				((uint64_t)p[13] >> 1);
			if (!(p[7] & 0x40))
				printf("  0x%02hhX", st);
			printf(" PTS ");
			print_pts(base, 0);
		}

		if (!(p[7] & 0x20)) {
			printf("    @%08llX\n", ofs - sizeof(buf));
			continue;
		}
		p += 9 + ((p[7] & 0x80) ? 5 : 0) + ((p[7] & 0x40) ? 5 : 0);
		if (p + 6 >= buf + sizeof(buf)) {
			printf("--- 0x%04hX\n", pid);
			continue;
		}
		base = ((uint64_t)(p[0] &0x38) << 27) | ((uint64_t)(p[0] & 0x03) << 28) |
			((uint64_t)p[1] << 20) | ((uint64_t)(p[2] & 0xf8) << 12) |
			((uint64_t)(p[2] & 0x03) << 13) | ((uint64_t)p[3] << 5) |
			((uint64_t)(p[4] & 0xf8) >> 3);
		ext = ((uint16_t)(p[4] & 0x03) << 7) | ((uint16_t)p[5] >> 1);
		printf(" ESCR:0x%02hhX ", st);
		print_pts(base, 1);
	}
	return 0;
}
