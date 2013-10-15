#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nitscan.h"

void set_short_ev(uint8_t *q, struct eith *eit)
{
	eit->name_len = q[5];
	if (eit->name_len > MAX_EV_NAME)
		eit->name_len = MAX_EV_NAME;
	memcpy(eit->name, q + 6, eit->name_len);

	eit->desc_len = q[6 + q[5]];
	if (eit->desc_len > MAX_EV_DESC)
		eit->desc_len = MAX_EV_DESC;
	memcpy(eit->desc, q+6+q[5], eit->desc_len);

	dprintf("  ev:\"%.*s\":\"%.*s\"\n",
		eit->name_len, eit->name, eit->desc_len, eit->desc);

	return;
}


void set_vcomp(uint8_t *q, struct eith *eit)
{
	int nlen;

	if (eit->num_vcomp >= MAX_COMPONENT) {
		dprintf("  ev vcomp overflowed!\n");
		return;
	}

	eit->v_comp[eit->num_vcomp].type = q[3];
	eit->v_comp[eit->num_vcomp].tag = q[4];

	nlen = q[1] + 2 - 8;
	if (nlen > MAX_COMPO_NAME)
		nlen = MAX_COMPO_NAME;

	eit->v_comp[eit->num_vcomp].name_len = nlen;
	memcpy(eit->v_comp[eit->num_vcomp].name, q + 8, nlen);
	dprintf("    vcomp[%02hhx] type:%02hhx name:\"%.*s\"\n",
		q[4], q[3], nlen, eit->v_comp[eit->num_vcomp].name);

	eit->num_vcomp++;
	return;
} 


void set_acomp(uint8_t *q, struct eith *eit)
{
	int nlen;
	uint8_t *tn;

	if (eit->num_acomp > MAX_A_COMPONENT) {
		dprintf("  ev acomp overflowed!\n");
		return;
	}

	eit->a_comp[eit->num_acomp].type = q[3];
	eit->a_comp[eit->num_acomp].tag = q[4];
	eit->a_comp[eit->num_acomp].stream_type = q[5];
	eit->a_comp[eit->num_acomp].flag = q[7];

	nlen = q[1] + 2 - 11;
	tn = q + 11;
	memcpy(eit->a_comp[eit->num_acomp].lang, q + 8, 3);
	if (eit->a_comp[eit->num_acomp].flag & A_COMPO_ES_MULTI_LING) {
		memcpy(eit->a_comp[eit->num_acomp].lang2, q + 11, 3);
		nlen -= 3;
		tn += 3;
	}

	if (nlen > MAX_ACOMPO_NAME)
		nlen = MAX_ACOMPO_NAME;
	eit->a_comp[eit->num_acomp].name_len = nlen;
	memcpy(eit->a_comp[eit->num_acomp].name, tn, nlen);

	dprintf("    acomp[%02hhx] type:%02hhx strm-type:%02hhx "
		"flag:%02hhx lang:%.3s lang2:%.3s name:\"%.*s\"\n",
		q[4], q[3], q[5], q[7], 
		eit->a_comp[eit->num_acomp].lang,
		eit->a_comp[eit->num_acomp].lang2,
		nlen, eit->a_comp[eit->num_acomp].name);
	eit->num_acomp++;
	return;
} 


int set_eith(struct eith **eitp, uint8_t *buf)
{
	int i, dlen;
	uint8_t *p, *q;
	struct eith *eit = *eitp;

	int ver = (buf[5] & 0x3e0) >> 1;
	int len = (buf[1] & 0x0f) << 8 | buf[2] + 3;

	if (!eit) {
		eit = malloc(sizeof(struct eith));
		if (!eit) {
			dprintf("  malloc failed.\n");
			return 2;
		}
		*eitp = eit;
		eit->ver = -1;
		eit->ev_id = -1;
		eit->start_day = DAY_UNDEFINED;
		eit->start_time_bcd[0] = 0xff;
		eit->start_time_bcd[1] = 0xff;
		eit->start_time_bcd[2] = 0xff;
		eit->duration_bcd[0] = 0xff;
		eit->duration_bcd[1] = 0xff;
		eit->duration_bcd[2] = 0xff;
		eit->free_ca_mode = 0;
		eit->name_len = 0;
		eit->desc_len = 0;
	}

	if (eit->ver == ver) {
		dprintf(" same EIT-%s[%04hx] %02x\n",
			(buf[6])? "f":"p", buf[3]<<8 | buf[4], ver);
		return 1;
	}

	eit->ver = ver;
	eit->service_id = buf[3]<<8 | buf[4];
	eit->ts_id = buf[8]<<8 | buf[9];
	eit->orig_nw_id = buf[10]<<8 | buf[11];
	eit->num_vcomp = 0;
	eit->num_acomp = 0;
	dprintf("new EIT-%s(ver.%02x) ts:%04hx [%04hx] o-nw:%04hx\n",
		(buf[6])? "f":"p", ver, eit->ts_id,
		eit->service_id, eit->orig_nw_id);

	p = buf + 14;
	while (p < buf + len - 4) {
		eit->ev_id = p[0]<<8 | p[1];
		eit->start_day = p[2] << 8 | p[3];
		memcpy(eit->start_time_bcd, p + 4, 3);
		memcpy(eit->duration_bcd, p + 7, 3);
		eit->free_ca_mode = (p[10] & 0x10)>>4;
		dlen = (p[10] & 0x0f)<<8 | p[11];
		dprintf("  Event:id[%04hx] start[%d%d:%d%d:%d%d] "
			"dur[%d%d:%d%d:%d%d] ca:%s\n",
			eit->ev_id,
			(eit->start_time_bcd[0]&0xf0)>>4, 
			eit->start_time_bcd[0]&0x0f,
			(eit->start_time_bcd[1]&0xf0)>>4, 
			eit->start_time_bcd[1]&0x0f,
			(eit->start_time_bcd[2]&0xf0)>>4, 
			eit->start_time_bcd[2]&0x0f,
			(eit->duration_bcd[0]&0xf0)>>4, 
			eit->duration_bcd[0]&0x0f,
			(eit->duration_bcd[1]&0xf0)>>4,
			eit->duration_bcd[1]&0x0f,
			(eit->duration_bcd[2]&0xf0)>>4,
			eit->duration_bcd[2]&0x0f,
			eit->free_ca_mode? "on":"off");

		p += 12;
		i = 0;
		q = p;
		while (i < dlen) {
			switch (q[0]) {
			case DESC_EVENT_SHORT:
				set_short_ev(q, eit);
				break;

			case DESC_COMPONENT_V:
				set_vcomp(q, eit);
				break;

			case DESC_COMPONENT_A:
				set_acomp(q, eit);
				break;
			default:
				dprintf("  desc[%02hhx](%hhu)\n", q[0], q[1]);
				break;
			}
			i += q[1] + 2;
			q += q[1] + 2;
		}
		p += dlen;
	}
	return 0;
}


int doEITH(struct secbuf *sec, void *data)
{
	int ret, i;
	uint16_t service_id, ts_id, orig_nw_id;
	struct pat *pat = data;

	if (pat->ver == -1) {
		dprintf("  PAT must be received before EIT.\n");
		return 1;
	}

	/* TODO: check CRC?, using crc32 module like dvb_net.c? */
	if (sec->buf[0] != TID_EIT_SELF_NEAR ) {
		dprintf("bad table header[%02hhx] %02hhx\n", 
			sec->buf[0], sec->buf[1]);
		return 2;
	}

	service_id = sec->buf[3] << 8 | sec->buf[4];

	/* check cur/next flag, sec #, last sec # */
	if (!(sec->buf[5]&0x01) || sec->buf[7] != 1 || sec->buf[12] != 1
		|| sec->buf[13] != sec->buf[0]) {
		dprintf("bad section.\n");
		return 2;
	}

	ts_id = sec->buf[8] << 8 | sec->buf[9];
	orig_nw_id = sec->buf[10] << 8 | sec->buf[11];

	if (ts_id != pat->ts_id) {
		dprintf("  bad ts_id:[%04hx] found\n", ts_id);
		return 2;
	}

	for (i=0; i<pat->num_prog; i++) {
		if (service_id != pat->prog[i].prog_id)
			continue;

		if (!sec->buf[6]) { /* EITH-present */
			ret = set_eith(&pat->prog[i].eith_p, sec->buf);
		} else { /* EITH-following */
			ret = set_eith(&pat->prog[i].eith_f, sec->buf);
		}
		return ret;
	}
	
	dprintf("  prog[%04hx] not found in PAT.\n", service_id);
	return 1;
}

