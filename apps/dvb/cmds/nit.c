#include <stdio.h>
#include <string.h>

#include "nitscan.h"


/* DESC_TS_INFO, DESC_SERVICE_LIST, DESC_PARTIAL_RECV share services[] */
/* thus search service_id first and add an entry if not found */
/* return idx of services[] or  -1 if the array overflowed.*/
int get_idx(uint16_t service_id, struct nit *nit)
{
	int i;

	for (i=0; i<nit->ts_info[0].num_service; i++) {
		if (service_id == nit->ts_info[0].services[i].service_id)
			return i;
	}
	if (i == MAX_PROGRAM) return -1;

	nit->ts_info[0].services[i].service_id = service_id;
	nit->ts_info[0].services[i].service_type = 0;
	nit->ts_info[0].services[i].ttype = 0;
	nit->ts_info[0].services[i].partial = 0;
	nit->ts_info[0].services[i].primary = 0;
	nit->ts_info[0].num_service++;

	return i;
}


void set_ts_info(uint8_t *p, struct nit *nit)
{
	int i, j, idx;
	uint8_t *q;
	uint8_t ttype_info, ttype_num_service;
	uint16_t service_id;

	nit->ts_info[0].rc_key_id = p[2];
	nit->ts_info[0].name_len = (p[3] & 0xfc) >> 2;
	nit->ts_info[0].num_ttype = p[3] & 0x03;
	q  = p + 4 + nit->ts_info[0].name_len;
	if (nit->ts_info[0].name_len > MAX_TS_NAME)
		nit->ts_info[0].name_len =  MAX_TS_NAME;

	memcpy(nit->ts_info[0].name, p+4, nit->ts_info[0].name_len);

	dprintf("  ts_info name:%.*s\n",
		nit->ts_info[0].name_len, nit->ts_info[0].name);

	for (i=0; i<nit->ts_info[0].num_ttype; i++) {
		ttype_info = q[0];
		ttype_num_service = q[1];
		dprintf("     ttype:%02hhx(%hhu servs) ",
			ttype_info, ttype_num_service);
		q += 2;
		for(j=0; j<ttype_num_service; j++) {
			service_id = q[0] << 8 | q[1];
			dprintf("[%04hx] ", service_id);
			idx = get_idx(service_id, nit);
			if (idx < 0) {
				continue;
			}
			nit->ts_info[0].services[idx].ttype = ttype_info;

			if (!j)
				nit->ts_info[0].services[idx].primary = 1;

			q += 2;
		}
	}
}


void set_service_list(uint8_t *p, struct nit *nit)
{
	uint8_t *q;
	int i, idx;
	uint16_t service_id;
	uint8_t service_type;

	q = p + 2;
	dprintf("  service_list ");
	for (i=0; i<p[1]; i+=3) {
		service_id = q[0] << 8 | q[1];
		service_type = q[2];
		idx = get_idx(service_id, nit);
		if (idx < 0) {
			dprintf("overflow! ");
			continue;
		}
		nit->ts_info[0].services[idx].service_type = service_type;
		q += 3;
		dprintf("[%04hx](typ:%02hhx) ",	service_id, service_type);
	}
	dprintf("\n");
}


void set_partial_recv(uint8_t *p, struct nit *nit)
{
	uint8_t *q;
	int i, idx;
	uint16_t service_id;
	uint8_t service_type;

	q = p + 2;
	dprintf("  partial_recv_list ");
	for (i=0; i<p[1]; i+=3) {
		service_id = q[0] << 8 | q[1];
		idx = get_idx(service_id, nit);
		if (idx < 0) {
			dprintf("overflow! ");
			continue;
		}
		nit->ts_info[0].services[idx].partial = 1;
		q += 2;
		dprintf("[%04hx] ", service_id);
	}
	dprintf("\n");
}


int doNIT(struct secbuf *sec, void *data)
{
	int ver, len, i;
	int nw_desc_len, ts_desc_len;
	uint16_t nw_id;
	uint8_t *p, *q;
	struct nit *nit = data;

	/* TODO: check CRC?, using crc32 module like dvb_net.c? */
	if (sec->buf[0] != TID_NIT_SELF || sec->buf[1] & 0xf0 != 0xb0 ) {
		dprintf(" bad table header.\n");
		return 2;
	}

	len = (sec->buf[1] & 0x0f) << 8 | sec->buf[2];
	nw_id = sec->buf[3] << 8 | sec->buf[4];
	ver = (sec->buf[5] & 0x3e0) >> 1;
	nw_desc_len = (sec->buf[8]&0x0f)<< 8 | sec->buf[9];

	if (!(sec->buf[5]&0x01) || sec->buf[6] && nw_desc_len
	    || sec->buf[6] && nw_id != nit->nw_id) {
		dprintf(" bad section.\n");
		return 2;
	}

	if (ver != nit->ver) {
		nit->ver = ver;
		nit->next_sec = 0;
	}

	if (sec->buf[6] < nit->next_sec) {
		dprintf(" same section[%02hhx] received.\n", sec->buf[6]);
		return 1;
	} else if (sec->buf[6] > nit->next_sec) {
		dprintf(" section[%02hhx] received, expecting [%02x].\n",
			sec->buf[6], nit->next_sec);
		return 1;
	}
	nit->next_sec++;

	if (!sec->buf[6]) {
		nit->nw_id = nw_id;
		nit->num_ts = 1;
		nit->ts_info[0].num_service = 0;
	}

	p = &sec->buf[10];
	i = 0;
	q = p;
	while (i < nw_desc_len) {
		if (q[0] == DESC_CA_EMM_TS) {
			nit->emm_ts.cas_id = q[2] << 8 | q[3];
			nit->emm_ts.ts_id = q[4] << 8 | q[5];
			nit->emm_ts.orig_nw_id = q[6] << 8 | q[7];
			nit->emm_ts.power_supply_period = q[8];
			break;
		}
		i += q[1] + 2;
		q += q[1] + 2;
	}

	p += nw_desc_len;
	
	p += 2; /* skip ts_loop_len, as it should be fixed to 1 */

	nit->ts_info[0].ts_id = p[0]<<8 | p[1];
	nit->ts_info[0].orig_nw_id = p[2] << 8 | p[3];
	ts_desc_len = (p[4] & 0x0f) << 8 | p[5];
	p += 6;

	dprintf("new NIT nw:%04hx(ver.%02x) ts:%04x desc:%d sec:%hhu/%hhu\n",
		nw_id, ver, nit->ts_info[0].ts_id, ts_desc_len,
		sec->buf[6], sec->buf[7]);

	i = 0;
	while (i < ts_desc_len) {
		switch (p[0]) {
		case DESC_TS_INFO:
			set_ts_info(p, nit);
			break;
			break;
		case DESC_SERVICE_LIST:
			set_service_list(p, nit);
			break;
		case DESC_PARTIAL_RECV:
			set_partial_recv(p, nit);
			break;
		default:
			dprintf("  desc[%02hhx](%hhu)\n", p[0], p[1]);
		}
		i += p[1]+2;
		p += p[1]+2;
	}
	dprintf("\n");
	return 0;
}
