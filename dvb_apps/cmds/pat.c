#include <stdio.h>
#include <stdlib.h>

#include "nitscan.h"

int doPAT(struct secbuf *sec, void *data)
{
	int ver, len, pnum, i;
	uint16_t ts_id, prog_id, pid;
	uint8_t *p;
	struct pmt *pmt = NULL;
	struct pat *pat = data;

	/* TODO: check CRC?, using crc32 module like dvb_net.c? */
	if (sec->buf[0] != TID_PAT || sec->buf[1] & 0xf0 != 0xb0 ) {
		dprintf(" bad table header.\n");
		return 2;
	}

	len = (sec->buf[1] & 0x0f) << 8 | sec->buf[2];
	ts_id = sec->buf[3] << 8 | sec->buf[4];
	ver = (sec->buf[5] & 0x3e0) >> 1;
	pnum = (len - 9) / 4;

	/* check cur/next flag, sec #, last sec # */
	if (!(sec->buf[5]&0x01) || sec->buf[6] || sec->buf[7]) {
		dprintf(" bad section.\n");
		return 2;
	}

	if (ts_id == pat->ts_id && ver == pat->ver) {
		dprintf(" same table. tsid:%04hx %02x\n", ts_id, ver);
		return 1;
	}

	dprintf("new pat for ts:%04hx, ver:%02x, prog:%d\n", ts_id, ver, pnum);

	pat->ver = ver;
	pat->ts_id = ts_id;

	/* release old PMTs */
	for(i=0; i<pat->num_prog; i++) {
		if (pat->prog[i].pmt) free(pat->prog[i].pmt);
		if (pat->prog[i].eith_p) free(pat->prog[i].eith_p);
		if (pat->prog[i].eith_f) free(pat->prog[i].eith_f);
	}

	pat->num_prog = 0;
	p = &sec->buf[8];
	for(i=0; i<pnum; i++) {
		prog_id = (p[0] << 8) + p[1];
		pid = ((p[2] << 8) + p[3]) & 0x1fff;
		if (prog_id) {
			pat->prog[pat->num_prog].prog_id = prog_id;
			pat->prog[pat->num_prog].pmt_pid = pid;
			pmt = malloc(sizeof(struct pmt));
			if (!pmt) {
				dprintf("memory alloc failed.\n");
			}
			pmt->ver = VER_NONE;
			pmt->sec.buf = pmt->sbuf;
			pmt->sec.len = 0;
			pmt->sec.max = sizeof(pmt->sbuf);
			pmt->prog_ecm.ca_pid = CA_PID_OFF;

			pat->prog[pat->num_prog].pmt = pmt;
			pat->prog[pat->num_prog].eith_p = NULL;
			pat->prog[pat->num_prog].eith_f = NULL;
			pat->num_prog++;
			dprintf("  prog:%04hx, pmt_pid:%04hx\n", prog_id, pid);
		}
		p += 4;
	}

	return 0;
}
