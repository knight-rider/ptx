#include <stdio.h>
#include <string.h>

#include "nitscan.h"

void set_service_desc(uint8_t *p, struct sdt *sdt)
{
	int i, dlen, nlen;
	uint16_t service_id;
	uint8_t *q;

	if (sdt->num_service >= MAX_SERVICE) {
		dprintf("   overflow!\n");
		return;
	}

	service_id = p[0] << 8 | p[1];
	sdt->service[sdt->num_service].service_id = service_id;
	sdt->service[sdt->num_service].flag = p[2] & 0x1f;
	/* set free_CA_mode at the MSB */
	sdt->service[sdt->num_service].flag |= (p[3] & 0x10)<<3;

	dlen = (p[3] & 0x0f) << 8 | p[4];
	dprintf("   [%04hx] flag:%02hhx dlen:%d ",
		service_id, sdt->service[sdt->num_service].flag, dlen);

	q = p + 5;
	i = 0;
	while (i < dlen) {
		switch (q[0]) {
		case DESC_SERVICE:
			nlen = q[4 + q[3]];
			if (nlen > MAX_CH_NAME)
				nlen = MAX_CH_NAME;

			sdt->service[sdt->num_service].name_len = nlen;
			memcpy(sdt->service[sdt->num_service].name,
			       q + 4 + q[3] + 1, nlen);
			sdt->service[sdt->num_service].service_type = q[2];
			dprintf("\"%.*s\"(type:%02hhx) ", nlen,
				sdt->service[sdt->num_service].name,
				sdt->service[sdt->num_service].service_type);
			break;
		default:
			dprintf("desc[%02hx](%hhu) ",q[0], q[1]);
		}
		q += q[1] + 2;
		i += q[1] + 2;
	}
	dprintf("\n");
	sdt->num_service++;

	return;
}


int doSDT(struct secbuf *sec, void *data)
{
	int ver, len;
	int nw_desc_len, dlen;
	uint16_t ts_id, orig_nw_id;
	uint8_t *p;
	struct sdt *sdt = data;

	/* TODO: check CRC?, using crc32 module like dvb_net.c? */
	if (sec->buf[0] != TID_SDT || sec->buf[1] & 0xf0 != 0xb0 ) {
		dprintf(" bad table header.\n");
		return 2;
	}

	len = (sec->buf[1] & 0x0f) << 8 | sec->buf[2] + 3;
	ts_id = sec->buf[3] << 8 | sec->buf[4];
	ver = (sec->buf[5] & 0x3e0) >> 1;
	orig_nw_id = sec->buf[8] << 8 | sec->buf[9];

	if (!(sec->buf[5]&0x01) || 
	    sec->buf[6] &&
	    (ts_id != sdt->ts_id || orig_nw_id != sdt->orig_nw_id)) {
		dprintf(" bad section.\n");
		return 2;
	}

	if (ver != sdt->ver) {
		sdt->ver = ver;
		sdt->next_sec = 0;
	}

	if (sec->buf[6] < sdt->next_sec) {
		dprintf(" same section[%02hhx] received.\n", sec->buf[6]);
		return 1;
	} else if (sec->buf[6] > sdt->next_sec) {
		dprintf(" section[%02hhx] received, expecting [%02x].\n", sec->buf[6], sdt->next_sec);
		return 1;
	}
	sdt->next_sec++;

	if (!sec->buf[6]) {
		sdt->ts_id = ts_id;
		sdt->orig_nw_id = orig_nw_id;
		sdt->num_service = 0;
	}

	dprintf("new SDT ts:%04hx(ver.%02x) o-nw:%04x len:%d sec:%hhu/%hhu\n",
		ts_id, ver, orig_nw_id, len, sec->buf[6], sec->buf[7]);

	p = &sec->buf[11];
	while (p < sec->buf + len - 4) {
		dlen = (p[3] & 0x0f) << 8 | p[4];
		set_service_desc(p, sdt);
		p += dlen + 5;
	}

	return 0;
}
