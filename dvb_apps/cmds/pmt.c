#include <stdio.h>

#include "nitscan.h"

int set_ca(uint8_t *b, struct ca *ca, int filter)
{
	uint16_t id;
	int dlen;

	if (b[0] != DESC_CA) return 1;
	id = (b[2] << 8) + b[3];
	if (id != ID_CAS_ARIB && filter) return 1;
	ca->cas_id = id;
	ca->ca_pid = ((b[4] & 0x1f) << 8) + b[5];
	dlen = (b[4] & 0xc0) >> 6;
	if (b[1] > 4)
		ca->type = b[6];
	else
		ca->type = 0;

	if (dlen >= 3)
		ca->ca_kc_pid = (b[7] & 0x1f) << 8 | b[8];
	else
		ca->ca_kc_pid = 0x1fff;

	return 0;
}


int doPMT(struct secbuf *sec, void *data)
{
	int ver, len, i, num_es, pinfo_len, esinfo_len;
	int skip;
	uint16_t prog_id, pcr_pid;
	uint8_t *b;
	struct pmt *pmt = data;

	/* TODO: verify CRC? */
	if (sec->buf[0] != TID_PMT || sec->buf[1] & 0xf0 != 0xb0) {
		dprintf(" bad table header.\n");
		return 2;
	}

	len = ((sec->buf[1] & 0x0f) << 8) + sec->buf[2] + 3;
	prog_id = (sec->buf[3] << 8) + sec->buf[4];
	ver = (sec->buf[5] & 0x3e0) >> 1;

	if (!(sec->buf[5]&0x01) || sec->buf[6] || sec->buf[7]) {
		dprintf(" bad section.\n");
		return 2;
	}

	if (ver == pmt->ver && prog_id == pmt->prog_id) {
		dprintf(" same table. prog:%04hx %02x\n", prog_id, ver);
		return 1;
	}

	if (ver == pmt->ver && prog_id != pmt->prog_id) {
		dprintf("bad data (prog:%04hx) in PMT table[%04hx].\n",
			prog_id, pmt->prog_id);
		return 2;
	}

	pmt->ver = ver;
	pmt->prog_id = prog_id;
	pmt->pcr_pid = ((sec->buf[8] & 0x1f) << 8) + sec->buf[9];
	pinfo_len = ((sec->buf[10] & 0x0f) << 8) + sec->buf[11];
	b = &sec->buf[12];
	for (i=0; i<pinfo_len; ) {
		switch (b[0]) {
		case DESC_CA:
			set_ca(b, &pmt->prog_ecm, 1);
			dprintf("  prog_ecm type:%04hx, pid:%04hx\n",
				pmt->prog_ecm.cas_id, pmt->prog_ecm.ca_pid);
			break;
		default:
			dprintf("  dsc:%02hhx len:%hhd.\n", b[0], b[1]);
		}
		i += b[1] + 2;
		b += b[1] + 2;
	}

	pmt->num_es = 0;
	while (b < sec->buf + len - 4) {
		skip = 1;
		if (1 || b[0] == ES_TYPE_MPEG2
			 || b[0] == ES_TYPE_AAC
			 || b[0] == ES_TYPE_H264) {
			skip = 0;
			pmt->es[pmt->num_es].es_type = b[0];
			pmt->es[pmt->num_es].es_pid = ((b[1] & 0x1f)<<8) + b[2];
		}
		esinfo_len = (b[3] & 0x0f) + b[4];
		dprintf("  es[%04hx] type:%02hhx len:%d ",
			((b[1]&0x1f)<<8) | b[2],
			b[0], esinfo_len);
		b+= 5;
		if (skip) {
			b += esinfo_len;
			dprintf("\n");
			continue;
		}
		for (i=0; i<esinfo_len;) {
			switch (b[0]) {
			case DESC_CA:
				set_ca(b, &pmt->es[pmt->num_es].es_ecm, 1);
				dprintf("cas:%04hx ca_pid:%04hx kc_pid:%04hx",
					pmt->es[pmt->num_es].es_ecm.cas_id,
					pmt->es[pmt->num_es].es_ecm.ca_pid,
					pmt->es[pmt->num_es].es_ecm.ca_kc_pid);
				break;
			case DESC_STREAM_ID:
				pmt->es[pmt->num_es].tag = b[2];
				dprintf("tag:%02hhx ", b[2]);
				break;
			default:
				dprintf("desc:%02hhx(%hhd) ", b[0], b[1]);
			}
			i += b[1] + 2;
			b += b[1] + 2;
		}

		dprintf("\n");
		pmt->num_es++;
		if (pmt->num_es >= MAX_ES) break;
	}

	dprintf("   end new PMT(%04hx) pcr[%04hx] pinfo:%d es:%d ver:%02x\n",
		prog_id, pmt->pcr_pid, pinfo_len, pmt->num_es, ver);

	return 0;
}

int doCAT(struct secbuf *sec, void *data)
{
	int ver, len, i;
	uint8_t *b;
	struct cat *cat = data;

	if (sec->buf[0] != TID_CAT || sec->buf[1] & 0xf0 != 0xb0) {
		dprintf(" bad table header.\n");
		return 2;
	}

	len = ((sec->buf[1] & 0x0f) << 8) + sec->buf[2] + 3;
	ver = (sec->buf[5] & 0x3e0) >> 1;
	if (ver == cat->ver) {
		dprintf(" same cat table.%02x\n", ver);
		return 1;
	}

	if (!(sec->buf[5]&0x01) || sec->buf[6] || sec->buf[7]) {
		dprintf(" bad section.\n");
		return 2;
	}

	dprintf("new CAT, ver:%02d len:%d ", ver, len);
	cat->ver = ver;
	cat->num_cas = 0;
	b = &sec->buf[8];
	while (b < sec->buf + len - 4) {
		switch (b[0]) {
		case DESC_CA:
			if (cat->num_cas >= MAX_CA) break;
			if ( !set_ca(b, &cat->emm[cat->num_cas], 0) )
				cat->num_cas++;
			dprintf("ca(%04hx,%s)/pid:%04hx kc_ecm:%04hx",
				cat->emm[cat->num_cas].cas_id,
				cat->emm[cat->num_cas].type == CA_EMM_TYPE_A ? "a":"b",
				cat->emm[cat->num_cas].ca_pid,
				cat->emm[cat->num_cas].ca_kc_pid);
			break;
		default:
			dprintf("tag:%02hhx(%hhd) ", b[0], b[1]);
		}
		b += b[1] + 2;
	}
	dprintf("\n");

	return 0;
}
