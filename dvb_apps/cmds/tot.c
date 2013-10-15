#include <stdio.h>

#include "nitscan.h"

int doTOT(struct secbuf *sec, void *data)
{
	struct tot *tot = data;

	/* TODO: check CRC?, using crc32 module like dvb_net.c? */
	if (sec->buf[0] != TID_TOT || sec->buf[1] & 0xf0 != 0x30 ) {
		dprintf(" bad table header.\n");
		return 2;
	}

	tot->day = sec->buf[3] << 8 | sec->buf[4];
	tot->time_bcd[0] = sec->buf[5];
	tot->time_bcd[1] = sec->buf[6];
	tot->time_bcd[2] = sec->buf[7];
	dprintf(" new TOT, time %1hhd%1hhd:%1hhd%1hhd:%1hhd%1hhd\n",
		(sec->buf[5]&0xf0)>>8, sec->buf[5]&0x0f,
		(sec->buf[6]&0xf0)>>8, sec->buf[6]&0x0f,
		(sec->buf[7]&0xf0)>>8, sec->buf[7]&0x0f);
	return 0;
}
