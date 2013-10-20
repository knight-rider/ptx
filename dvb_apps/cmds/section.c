#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "nitscan.h"

void clean_si(struct isdbt_si *si)
{
	int i;

	if (si->pat.ver == -1) return;

	for (i=0; i<si->pat.num_prog; i++) {
		if (si->pat.prog[i].pmt) free(si->pat.prog[i].pmt);
		si->pat.prog[i].pmt = NULL;
		if (si->pat.prog[i].eith_p) free(si->pat.prog[i].eith_p);
		si->pat.prog[i].eith_p = NULL;
		if (si->pat.prog[i].eith_f) free(si->pat.prog[i].eith_f);
		si->pat.prog[i].eith_f = NULL;
	}
}


void init_si(struct isdbt_si *si)
{
	int i;

	si->pat.ver = VER_NONE;
	si->pat.ts_id = ID_NONE;
	si->pat.num_prog = 0;
	si->pat.sec.len = 0;
	si->pat.sec.max = sizeof(si->pat.sbuf);
	si->pat.sec.buf = si->pat.sbuf;

	for (i=0; i<MAX_PROGRAM; i++) {
		si->pat.prog[i].prog_id = ID_NONE;
		si->pat.prog[i].pmt_pid = PID_NONE;
		si->pat.prog[i].pmt = NULL;
		si->pat.prog[i].eith_p = NULL;
		si->pat.prog[i].eith_f = NULL;
	}

	si->cat.ver = VER_NONE;
	si->cat.num_cas = 0;
	for (i=0; i<MAX_CA; i++) {
		si->cat.emm[i].cas_id = ID_NONE;
		si->cat.emm[i].ca_pid = PID_NONE;
		si->cat.emm[i].type = 0;
	}
	si->cat.sec.len = 0;
	si->cat.sec.max = sizeof(si->cat.sbuf);
	si->cat.sec.buf = si->cat.sbuf;

	si->nit.ver = VER_NONE;
	si->nit.nw_id = ID_NONE;
	si->nit.num_ts = 0;
	si->nit.ts_info[0].ts_id = ID_NONE;
	si->nit.ts_info[0].orig_nw_id = ID_NONE;
	si->nit.ts_info[0].num_service = 0;
	for (i=0; i<MAX_PROGRAM; i++) {
		si->nit.ts_info[0].services[i].service_id = ID_NONE;
		si->nit.ts_info[0].services[i].service_type = SERVICE_TYPE_DTV;
		si->nit.ts_info[0].services[i].ttype = 0;
		si->nit.ts_info[0].services[i].partial = 0;
		si->nit.ts_info[0].services[i].primary = 0;
	}
	si->nit.ts_info[0].num_ttype = 0;
	si->nit.ts_info[0].name_len = 0;
	si->nit.emm_ts.cas_id = ID_NONE;
	si->nit.emm_ts.ts_id = ID_NONE;
	si->nit.emm_ts.orig_nw_id = ID_NONE;
	si->nit.emm_ts.power_supply_period = 0;

	si->nit.sec.len = 0;
	si->nit.sec.max = sizeof(si->nit.sbuf);
	si->nit.sec.buf = si->nit.sbuf;

	si->tot.day = 0;
	si->tot.time_bcd[0] = 0;
	si->tot.time_bcd[1] = 0;
	si->tot.time_bcd[2] = 0;
	si->tot.sec.len = 0;
	si->tot.sec.max = sizeof(si->tot.sbuf);
	si->tot.sec.buf = si->tot.sbuf;

	si->sdt.ver = VER_NONE;
	si->sdt.ts_id = ID_NONE;
	si->sdt.orig_nw_id = ID_NONE;
	si->sdt.num_service = 0;
	for (i=0; i<MAX_SERVICE; i++) {
		si->sdt.service[i].service_id = ID_NONE;
		si->sdt.service[i].flag = 0;
		si->sdt.service[i].service_type = 0;
		si->sdt.service[i].name_len = 0;
	}
	si->sdt.sec.len = 0;
	si->sdt.sec.max = sizeof(si->sdt.sbuf);
	si->sdt.sec.buf = si->sdt.sbuf;

	si->esec.len = 0;
	si->esec.max = sizeof(si->ebuf);
	si->esec.buf = si->ebuf;

	return;
}


int doSection(uint8_t *buf, struct secbuf *sec,
		 int (*cb)(struct secbuf *, void *), void *data)
{
	uint8_t *b = &buf[4];
	int len = 184;
	int nlen = 0;
	int ret = 0;

	/* check if payload exists */
	if (!(buf[3] & TS_F_PAYLOAD) || (buf[1] & TS_F_TSEI)) return 0;

	/* skip adaptation field */
	if (buf[3] & TS_F_ADAP) {
		len -= buf[4]+1;
		b += buf[4]+1;
	}

	/* check PUSI flag */
	if (buf[1] & TS_F_PUSI) {
		nlen = len - *b - 1;
		len = *b;
		b++;
	}

	/* fill the previous section */
	if (len && sec->len && sec->cc <= 0x0f) {
		sec->cc = (sec->cc + 1) & 0x0f;
		if ((buf[3] & TS_CC_MASK) != sec->cc) {
			sec->cc = 0x20;
			dprintf("broken section data in pid:0x%04hx\n",
				(buf[1] & 0x1f) << 8 | buf[2]);
			return 1;
		} else if (len < sec->len - sec->cur) { /* not the end */
			memcpy(sec->buf + sec->cur, b, len);
			sec->cur += len;
			dprintf("-- %d remains\n", sec->len - sec->cur);
			if (nlen > 0) dprintf("broken section.\n");
		} else {
			memcpy(sec->buf + sec->cur, b, sec->len - sec->cur);
			ret += cb(sec, data);
			if (nlen && (len > sec->len - sec->cur))
				dprintf("illegal section gap.\n");
		}
	}

	/* start  a new section */
	b += len;
	while (nlen>=3) {
		if ( b[0] == 0xff ) break;

		sec->len =  ((b[1] & 0x0f) << 8 ) + b[2] + 3;
		sec->cur = 0;
		sec->cc = buf[3] & 0x0f;
		dprintf("new sec. pid:%04hx tid:%02hhx len:%d cc:%01hhx\n",
			((buf[1]&0x1f)<<8)+buf[2],  b[0], sec->len, sec->cc);
		if (sec->len > sec->max) {
			dprintf("too long(>= %d) section.\n", sec->max);
			sec->len = 0;
			break;
		}
		if (nlen < sec->len) {
			memcpy(sec->buf, b, nlen);
			sec->cur = nlen;
//			dprintf("-- %d more to be read.\n", sec->len - sec->cur);
			break;
		}
		memcpy(sec->buf, b, sec->len);
		ret += cb(sec, data);
		nlen -= sec->len;
		b += sec->len;
	}
	
	return ret;
}
