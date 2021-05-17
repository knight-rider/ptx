#ifndef _NITSCAN_H_
#define _NITSCAN_H_

#ifdef __cplusplus__
extern "C" {
#endif

#include <stdint.h>

//#define _DEBUG_NITSCAN_ 1
#ifdef _DEBUG_NITSCAN_
#define dprintf(args...) do { fprintf(stderr, args); } while (0)
#else
#define dprintf(args...)
#endif


struct secbuf {
	int max;
	int cur;
	int len;
	int cc;
	uint8_t *buf;
};

struct emm {
	int ver;
	int next_sec;
	struct secbuf sec;
	uint8_t sbuf[4096];
};

struct ca {
	uint16_t cas_id;
#define CA_PID_OFF 0x1fff
	uint16_t ca_pid;
	uint16_t ca_kc_pid;
	uint8_t  type;
	struct emm *emm_sec;
};


struct pmt {
	int ver;
	uint16_t prog_id;
	uint16_t pcr_pid;
	struct ca prog_ecm;

#define MAX_ES 32
	int num_es;
	struct {
#define ES_TYPE_MPEG1 0x01
#define ES_TYPE_MPEG2 0x02
#define ES_TYPE_TXT   0x06
#define ES_TYPE_AAC   0x0F
#define ES_TYPE_H264  0x1B
		uint8_t es_type;
		uint16_t es_pid;
		uint8_t tag; /* link with EIT */
		struct ca es_ecm;
	} es[MAX_ES];

	struct secbuf sec;
	uint8_t sbuf[1024];
};

struct program {
	unsigned int prog_id;
	uint16_t pmt_pid;
	struct pmt *pmt;
	struct eith *eith_p;
	struct eith *eith_f;
};

struct pat {
	int ver;
	uint16_t ts_id;
#define MAX_PROGRAM 8
	int num_prog;
	struct program prog[MAX_PROGRAM];

	struct secbuf sec;
	uint8_t sbuf[376];  /* 188 shoud be enough */
};

struct cat {
	int ver;
	int num_cas;
#define MAX_CA 8
	struct ca emm[MAX_CA];

	struct secbuf sec;
	uint8_t sbuf[1024];
};


struct nit {
	int ver;

	/* section # to be received next. last_sec + 1 if all sec. received */
	int next_sec;
	uint16_t nw_id;
	int num_ts;
	struct ts_info {
		uint16_t ts_id;
		uint16_t orig_nw_id;
		uint8_t rc_key_id;

		int num_service;
		struct servce_list {
			uint16_t service_id;
#define SERVICE_TYPE_DTV    0x01
#define SERVICE_TYPE_DAUDIO 0x02
#define SERVICE_TYPE_DATA   0xC0
#define SERVICE_TYPE_TEMP_V 0xA1
#define SERVICE_TYPE_TEMP_A 0xA2
#define SERVICE_TYPE_TEMP_D 0xA3
#define SERVICE_TYPE_ENGINEERING 0xA4
#define SERVICE_TYPE_BOOKMARK 0xAA
			uint8_t service_type;

			/* ttype = hierarchy info */
			/* 1 hierachy info: TRANS_TYPE | MOD_TYPE | 0x0F */
#define TRANSMISSION_TYPE_A 0x00 /* weakest, high BW */
#define TRANSMISSION_TYPE_B 0x40
#define TRANSMISSION_TYPE_C 0x80 /* strongest, low BW */
#define MOD_64QAM 0x00
#define MOD_16QAM 0x10
#define MOD_QPSK  0x20
			uint8_t ttype;
			/* flags */
			uint8_t partial;
			uint8_t primary;
		} services[MAX_PROGRAM];

		int num_ttype; /* num_hierarchy */

		int name_len;
#define MAX_TS_NAME 20
		char name[MAX_TS_NAME];
		
	} ts_info[1];

	struct emm_ts {
		uint16_t cas_id;
		uint16_t ts_id;
		uint16_t orig_nw_id;
		uint8_t  power_supply_period;
	} emm_ts;

	struct secbuf sec;
	uint8_t sbuf[1024];
};

struct tot {
	int day; /* epoch day 1900/Mar/01  see ARIB STD-B10, Part 2 Annex C. */
	uint8_t time_bcd[3]; /* HHMMSS */

	struct secbuf sec;
	uint8_t sbuf[188];
};

struct sdt {
	int ver;
	int next_sec; /* same as NIT */

	uint16_t ts_id;
	uint16_t orig_nw_id;

	int num_service;
#define MAX_SERVICE MAX_PROGRAM
	struct {
		uint16_t service_id;
#define SDT_F_HEIT 0x10
#define SDT_F_MEIT 0x08
#define SDT_F_LEIT 0x04
#define SDT_F_SCHED 0x02 /* EIT-schedule */
#define SDT_F_PF 0x01  /* EIT-present/following */
#define SDT_F_CA 0x80  /* free CA mode */
		uint8_t flag;

		uint8_t service_type;
		int name_len;
#define MAX_CH_NAME 20
		char name[MAX_CH_NAME];
	} service[MAX_SERVICE];

	struct secbuf sec;
	uint8_t sbuf[1024];
};

struct eith {
	int ver;
	uint16_t service_id;
	uint16_t ts_id;
	uint16_t orig_nw_id;

	uint16_t ev_id;
#define DAY_UNDEFINED ~0
	int start_day;
#define TIME_UNDEFINED 0xffffff
	uint8_t start_time_bcd[3];
	uint8_t duration_bcd[3];
	uint8_t free_ca_mode;

	int name_len;
#define MAX_EV_NAME 96
	char name[MAX_EV_NAME];

	int desc_len;
#define MAX_EV_DESC 192
	char desc[MAX_EV_DESC];

	int num_vcomp;
#define MAX_COMPONENT 16
	struct {
#define COMPONENT_4_3   0x01
#define COMPONENT_16_9  0x03
#define COMPONENT_WIDE  0x04

#define COMPONENT_480I  0x00
#define COMPONENT_480P  0xA0
#define COMPONENT_1080I 0xB0
#define COMPONENT_720P  0xC0
#define COMPONENT_240P  0xD0
		uint8_t type;
		uint8_t tag;

		int name_len;
#define MAX_COMPO_NAME 16
		char name[MAX_COMPO_NAME];
	} v_comp[MAX_COMPONENT];


	int num_acomp;
#define MAX_A_COMPONENT 34
	struct {
#define A_COMPO_MONO_SINGLE 0x01
#define A_COMPO_MONO_DUAL   0x02
#define A_COMPO_STEREO      0x03
#define A_COMPO_3_1         0x07
#define A_COMPO_3_2         0x08
#define A_COMPO_3_2_LFE     0x09
		uint8_t type;
		uint8_t tag;
		uint8_t stream_type; /* fixed to 0x0F: AAC */
#define A_COMPO_ES_MULTI_LING 0x80
#define A_COMPO_MAIN 0x40
#define A_COMPO_QUALITY 0x30
#define A_COMPO_SR_MASK 0x0E
#define A_COMPO_SR_24k  0x06
#define A_COMPO_SR_32k  0x0A
#define A_COMPO_SR_48k  0x0E
		uint8_t flag;
		char lang[3];
		char lang2[3];

		int name_len;
#define MAX_ACOMPO_NAME 33
		char name[MAX_ACOMPO_NAME];
	} a_comp[MAX_A_COMPONENT];
};

struct isdbt_si {
	struct pat pat;
	struct cat cat;
	struct nit nit;
	struct tot tot;
	struct sdt sdt;

	/* PMT, EIT, ECM, EMM are created dynamically. */

	struct secbuf esec;
	uint8_t ebuf[4096];
};

#define TID_PAT 0x00
#define TID_CAT 0x01
#define TID_PMT 0x02
#define TID_NIT_SELF 0x40
#define TID_SDT 0x42
#define TID_EIT_SELF_NEAR 0x4e
#define TID_TDT 0x70
#define TID_ST 0x72
#define TID_TOT 0x73
#define TID_ECM 0x82
#define TID_EMM 0x84

/* TS packet, 2nd Byte */
#define TS_F_TSEI 0x80
#define TS_F_PUSI 0x40
#define TS_F_PRIO 0x20

/* TS packet, 4th Byte */
#define TS_F_ADAP 0x20
#define TS_F_PAYLOAD 0x10
#define TS_CA_NONE 0x40
#define TS_CA_ODD  0x80
#define TS_CA_EVEN 0xC0
#define TS_CA_MASK 0xC0
#define TS_CC_MASK 0x0F

#define PID_PAT 0x0000
#define PID_CAT 0x0001
#define PID_NIT 0x0010
#define PID_SDT 0x0011
#define PID_EITH 0x0012
#define PID_TOT 0x0014
#define PID_NONE 0x2000

/* see ARIB-STD-B10 part1 5.3 */
#define DESC_CA 0x09
#define DESC_SERVICE_LIST 0x41
#define DESC_SERVICE 0x48
#define DESC_EVENT_SHORT 0x4D
#define DESC_EVENT_GROUP 0xD6
#define DESC_COMPONENT_V 0x50
#define DESC_STREAM_ID 0x52
#define DESC_COMPONENT_A 0xC4
#define DESC_CA_EMM_TS 0xCA
#define DESC_TS_INFO 0xCD
#define DESC_PARTIAL_RECV 0xFB


#define VER_NONE -1
#define ID_NONE 0xffff

/* ARIB TR-B14/15 */
#define ID_CAS_ARIB 0x0005
/* ARIB TR-B26 */
#define ID_CAS_ARIB_B 0x000A

#define CA_EMM_TYPE_A 0x01
#define CA_EMM_TYPE_B 0x02

extern void clean_si(struct isdbt_si *si);
extern void init_si(struct isdbt_si *si);

extern int doSection(uint8_t *buf, struct secbuf *sec,
			int (*cb)(struct secbuf *, void *), void *data);

extern int doPAT(struct secbuf *sec, void *pat);
extern int doCAT(struct secbuf *sec, void *cat);
extern int doPMT(struct secbuf *sec, void *prog);
extern int doNIT(struct secbuf *sec, void *nit);
extern int doSDT(struct secbuf *sec, void *sdt);
extern int doTOT(struct secbuf *sec, void *tot);
extern int doEITH(struct secbuf *sec, void *pat);

#ifdef __cplusplus__
}
#endif
#endif
