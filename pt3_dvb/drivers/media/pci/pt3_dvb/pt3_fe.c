#include "dvb_math.h"
#include "pt3.h"

static u8 pt3_qm_reg_rw[] = {
	0x48, 0x1c, 0xa0, 0x10, 0xbc, 0xc5, 0x20, 0x33,
	0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x00, 0xff, 0xf3, 0x00, 0x2a, 0x64, 0xa6, 0x86,
	0x8c, 0xcf, 0xb8, 0xf1, 0xa8, 0xf2, 0x89, 0x00,
};

void pt3_qm_init_reg_param(struct pt3_qm *qm)
{
	memcpy(qm->reg, pt3_qm_reg_rw, sizeof(pt3_qm_reg_rw));

	qm->adap->freq = 0;
	qm->standby = false;
	qm->wait_time_lpf = 20;
	qm->wait_time_search_fast = 4;
	qm->wait_time_search_normal = 15;
}

static int pt3_qm_write(struct pt3_qm *qm, struct pt3_bus *bus, u8 addr, u8 data)
{
	int ret = pt3_tc_write_tuner(qm->adap, bus, addr, &data, sizeof(data));
	qm->reg[addr] = data;
	return ret;
}

#define PT3_QM_INIT_DUMMY_RESET 0x0c

void pt3_qm_dummy_reset(struct pt3_qm *qm, struct pt3_bus *bus)
{
	pt3_qm_write(qm, bus, 0x01, PT3_QM_INIT_DUMMY_RESET);
	pt3_qm_write(qm, bus, 0x01, PT3_QM_INIT_DUMMY_RESET);
}

static void pt3_qm_sleep(struct pt3_bus *bus, u32 ms)
{
	if (bus)
		pt3_bus_sleep(bus, ms);
	else
		msleep_interruptible(ms);
}

static int pt3_qm_read(struct pt3_qm *qm, struct pt3_bus *bus, u8 addr, u8 *data)
{
	int ret = 0;
	if ((addr == 0x00) || (addr == 0x0d))
		ret = pt3_tc_read_tuner(qm->adap, bus, addr, data);
	return ret;
}

static u8 pt3_qm_flag[32] = {
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

static int pt3_qm_set_sleep_mode(struct pt3_qm *qm, struct pt3_bus *bus)
{
	int ret;

	if (qm->standby) {
		qm->reg[0x01] &= (~(1 << 3)) & 0xff;
		qm->reg[0x01] |= 1 << 0;
		qm->reg[0x05] |= 1 << 3;

		ret = pt3_qm_write(qm, bus, 0x05, qm->reg[0x05]);
		if (ret)
			return ret;
		ret = pt3_qm_write(qm, bus, 0x01, qm->reg[0x01]);
		if (ret)
			return ret;
	} else {
		qm->reg[0x01] |= 1 << 3;
		qm->reg[0x01] &= (~(1 << 0)) & 0xff;
		qm->reg[0x05] &= (~(1 << 3)) & 0xff;

		ret = pt3_qm_write(qm, bus, 0x01, qm->reg[0x01]);
		if (ret)
			return ret;
		ret = pt3_qm_write(qm, bus, 0x05, qm->reg[0x05]);
		if (ret)
			return ret;
	}
	return ret;
}

static int pt3_qm_set_search_mode(struct pt3_qm *qm, struct pt3_bus *bus)
{
	qm->reg[3] &= 0xfe;
	return pt3_qm_write(qm, bus, 0x03, qm->reg[3]);
}

int pt3_qm_init(struct pt3_qm *qm, struct pt3_bus *bus)
{
	u8 i_data;
	u32 i;
	int ret;

	/* soft reset on */
	ret = pt3_qm_write(qm, bus, 0x01, PT3_QM_INIT_DUMMY_RESET);
	if (ret)
		return ret;

	pt3_qm_sleep(bus, 1);

	/* soft reset off */
	i_data = qm->reg[0x01] | 0x10;
	ret = pt3_qm_write(qm, bus, 0x01, i_data);
	if (ret)
		return ret;

	/* ID check */
	ret = pt3_qm_read(qm, bus, 0x00, &i_data);
	if (ret)
		return ret;

	if ((bus == NULL) && (i_data != 0x48))
		return -EINVAL;

	/* LPF tuning on */
	pt3_qm_sleep(bus, 1);
	qm->reg[0x0c] |= 0x40;
	ret = pt3_qm_write(qm, bus, 0x0c, qm->reg[0x0c]);
	if (ret)
		return ret;
	pt3_qm_sleep(bus, qm->wait_time_lpf);

	for (i = 0; i < sizeof(pt3_qm_flag); i++)
		if (pt3_qm_flag[i] == 1) {
			ret = pt3_qm_write(qm, bus, i, qm->reg[i]);
			if (ret)
				return ret;
		}
	ret = pt3_qm_set_sleep_mode(qm, bus);
	if (ret)
		return ret;
	return pt3_qm_set_search_mode(qm, bus);
}

int pt3_qm_set_sleep(struct pt3_qm *qm, bool sleep)
{
	qm->standby = sleep;
	if (sleep) {
		int ret = pt3_tc_set_agc_s(qm->adap, PT3_TC_AGC_MANUAL);
		if (ret)
			return ret;
		pt3_qm_set_sleep_mode(qm, NULL);
		pt3_tc_set_sleep_s(qm->adap, NULL, sleep);
	} else {
		pt3_tc_set_sleep_s(qm->adap, NULL, sleep);
		pt3_qm_set_sleep_mode(qm, NULL);
	}
	qm->adap->sleep = sleep;
	return 0;
}

void pt3_qm_get_channel_freq(u32 channel, u32 *number, u32 *freq)
{
	if (channel < 12) {
		*number = 1 + 2 * channel;
		*freq = 104948 + 3836 * channel;
	} else if (channel < 24) {
		channel -= 12;
		*number = 2 + 2 * channel;
		*freq = 161300 + 4000 * channel;
	} else {
		channel -= 24;
		*number = 1 + 2 * channel;
		*freq = 159300 + 4000 * channel;
	}
}

static u32 PT3_QM_FREQ_TABLE[9][3] = {
	{ 2151000, 1, 7 },
	{ 1950000, 1, 6 },
	{ 1800000, 1, 5 },
	{ 1600000, 1, 4 },
	{ 1450000, 1, 3 },
	{ 1250000, 1, 2 },
	{ 1200000, 0, 7 },
	{  975000, 0, 6 },
	{  950000, 0, 0 }
};

static u32 SD_TABLE[24][2][3] = {
	{{0x38fae1, 0x0d, 0x5}, {0x39fae1, 0x0d, 0x5},},
	{{0x3f570a, 0x0e, 0x3}, {0x00570a, 0x0e, 0x3},},
	{{0x05b333, 0x0e, 0x5}, {0x06b333, 0x0e, 0x5},},
	{{0x3c0f5c, 0x0f, 0x4}, {0x3d0f5c, 0x0f, 0x4},},
	{{0x026b85, 0x0f, 0x6}, {0x036b85, 0x0f, 0x6},},
	{{0x38c7ae, 0x10, 0x5}, {0x39c7ae, 0x10, 0x5},},
	{{0x3f23d7, 0x11, 0x3}, {0x0023d7, 0x11, 0x3},},
	{{0x058000, 0x11, 0x5}, {0x068000, 0x11, 0x5},},
	{{0x3bdc28, 0x12, 0x4}, {0x3cdc28, 0x12, 0x4},},
	{{0x023851, 0x12, 0x6}, {0x033851, 0x12, 0x6},},
	{{0x38947a, 0x13, 0x5}, {0x39947a, 0x13, 0x5},},
	{{0x3ef0a3, 0x14, 0x3}, {0x3ff0a3, 0x14, 0x3},},
	{{0x3c8000, 0x16, 0x4}, {0x3d8000, 0x16, 0x4},},
	{{0x048000, 0x16, 0x6}, {0x058000, 0x16, 0x6},},
	{{0x3c8000, 0x17, 0x5}, {0x3d8000, 0x17, 0x5},},
	{{0x048000, 0x18, 0x3}, {0x058000, 0x18, 0x3},},
	{{0x3c8000, 0x18, 0x6}, {0x3d8000, 0x18, 0x6},},
	{{0x048000, 0x19, 0x4}, {0x058000, 0x19, 0x4},},
	{{0x3c8000, 0x1a, 0x3}, {0x3d8000, 0x1a, 0x3},},
	{{0x048000, 0x1a, 0x5}, {0x058000, 0x1a, 0x5},},
	{{0x3c8000, 0x1b, 0x4}, {0x3d8000, 0x1b, 0x4},},
	{{0x048000, 0x1b, 0x6}, {0x058000, 0x1b, 0x6},},
	{{0x3c8000, 0x1c, 0x5}, {0x3d8000, 0x1c, 0x5},},
	{{0x048000, 0x1d, 0x3}, {0x058000, 0x1d, 0x3},},
};

static int pt3_qm_tuning(struct pt3_qm *qm, struct pt3_bus *bus, u32 *sd, u32 channel)
{
	int ret;
	struct pt3_adapter *adap = qm->adap;
	u8 i_data;
	u32 index, i, N, A;

	qm->reg[0x08] &= 0xf0;
	qm->reg[0x08] |= 0x09;

	qm->reg[0x13] &= 0x9f;
	qm->reg[0x13] |= 0x20;

	for (i = 0; i < 8; i++) {
		if ((PT3_QM_FREQ_TABLE[i+1][0] <= adap->freq) && (adap->freq < PT3_QM_FREQ_TABLE[i][0])) {
			i_data = qm->reg[0x02];
			i_data &= 0x0f;
			i_data |= PT3_QM_FREQ_TABLE[i][1] << 7;
			i_data |= PT3_QM_FREQ_TABLE[i][2] << 4;
			pt3_qm_write(qm, bus, 0x02, i_data);
		}
	}

	index = pt3_tc_index(qm->adap);
	*sd = SD_TABLE[channel][index][0];
	N = SD_TABLE[channel][index][1];
	A = SD_TABLE[channel][index][2];

	qm->reg[0x06] &= 0x40;
	qm->reg[0x06] |= N;
	ret = pt3_qm_write(qm, bus, 0x06, qm->reg[0x06]);
	if (ret)
		return ret;

	qm->reg[0x07] &= 0xf0;
	qm->reg[0x07] |= A & 0x0f;
	return pt3_qm_write(qm, bus, 0x07, qm->reg[0x07]);
}

static int pt3_qm_local_lpf_tuning(struct pt3_qm *qm, struct pt3_bus *bus, int lpf, u32 channel)
{
	u8 i_data;
	u32 sd = 0;
	int ret = pt3_qm_tuning(qm, bus, &sd, channel);

	if (ret)
		return ret;
	if (lpf) {
		i_data = qm->reg[0x08] & 0xf0;
		i_data |= 2;
		ret = pt3_qm_write(qm, bus, 0x08, i_data);
	} else
		ret = pt3_qm_write(qm, bus, 0x08, qm->reg[0x08]);
	if (ret)
		return ret;

	qm->reg[0x09] &= 0xc0;
	qm->reg[0x09] |= (sd >> 16) & 0x3f;
	qm->reg[0x0a] = (sd >> 8) & 0xff;
	qm->reg[0x0b] = (sd >> 0) & 0xff;
	ret = pt3_qm_write(qm, bus, 0x09, qm->reg[0x09]);
	if (ret)
		return ret;
	ret = pt3_qm_write(qm, bus, 0x0a, qm->reg[0x0a]);
	if (ret)
		return ret;
	ret = pt3_qm_write(qm, bus, 0x0b, qm->reg[0x0b]);
	if (ret)
		return ret;

	if (lpf) {
		i_data = qm->reg[0x0c];
		i_data &= 0x3f;
		ret = pt3_qm_write(qm, bus, 0x0c, i_data);
		if (ret)
			return ret;
		pt3_qm_sleep(bus, 1);

		i_data = qm->reg[0x0c];
		i_data |= 0xc0;
		ret = pt3_qm_write(qm, bus, 0x0c, i_data);
		if (ret)
			return ret;
		pt3_qm_sleep(bus, qm->wait_time_lpf);
		ret = pt3_qm_write(qm, bus, 0x08, 0x09);
		if (ret)
			return ret;
		ret = pt3_qm_write(qm, bus, 0x13, qm->reg[0x13]);
		if (ret)
			return ret;
	} else {
		ret = pt3_qm_write(qm, bus, 0x13, qm->reg[0x13]);
		if (ret)
			return ret;
		i_data = qm->reg[0x0c];
		i_data &= 0x7f;
		ret = pt3_qm_write(qm, bus, 0x0c, i_data);
		if (ret)
			return ret;
		pt3_qm_sleep(bus, 2);

		i_data = qm->reg[0x0c];
		i_data |= 0x80;
		ret = pt3_qm_write(qm, bus, 0x0c, i_data);
		if (ret)
			return ret;
		if (qm->reg[0x03] & 0x01)
			pt3_qm_sleep(bus, qm->wait_time_search_fast);
		else
			pt3_qm_sleep(bus, qm->wait_time_search_normal);
	}
	return ret;
}

int pt3_qm_get_locked(struct pt3_qm *qm, bool *locked)
{
	int ret = pt3_qm_read(qm, NULL, 0x0d, &qm->reg[0x0d]);
	if (ret)
		return ret;
	if (qm->reg[0x0d] & 0x40)
		*locked = true;
	else
		*locked = false;
	return ret;
}

int pt3_qm_set_frequency(struct pt3_qm *qm, u32 channel)
{
	u32 number, freq, freq_kHz;
	struct timeval begin, now;
	bool locked;
	int ret = pt3_tc_set_agc_s(qm->adap, PT3_TC_AGC_MANUAL);
	if (ret)
		return ret;

	pt3_qm_get_channel_freq(channel, &number, &freq);
	freq_kHz = freq * 10;
	if (pt3_tc_index(qm->adap) == 0)
		freq_kHz -= 500;
	else
		freq_kHz += 500;
	qm->adap->freq = freq_kHz;
	pr_debug("#%d ch %d freq %d kHz\n", qm->adap->idx, channel, freq_kHz);

	ret = pt3_qm_local_lpf_tuning(qm, NULL, 1, channel);
	if (ret)
		return ret;
	do_gettimeofday(&begin);
	while (1) {
		do_gettimeofday(&now);
		ret = pt3_qm_get_locked(qm, &locked);
		if (ret)
			return ret;
		if (locked)
			break;
		if (pt3_tc_time_diff(&begin, &now) >= 100)
			break;
		msleep_interruptible(1);
	}
	pr_debug("#%d qm_get_locked %d ret=0x%x\n", qm->adap->idx, locked, ret);
	if (!locked)
		return -ETIMEDOUT;

	ret = pt3_tc_set_agc_s(qm->adap, PT3_TC_AGC_AUTO);
	if (!ret) {
		qm->adap->channel = channel;
		qm->adap->offset = 0;
	}
	return ret;
}

static struct {
	u32	freq;		/* Channel center frequency @ kHz	*/
	u32	freq_th;	/* Offset frequency threshold @ kHz	*/
	u8	shf_val;	/* Spur shift value			*/
	u8	shf_dir;	/* Spur shift direction			*/
} SHF_DVBT_TAB[] = {
	{  64500, 500, 0x92, 0x07 },
	{ 191500, 300, 0xE2, 0x07 },
	{ 205500, 500, 0x2C, 0x04 },
	{ 212500, 500, 0x1E, 0x04 },
	{ 226500, 500, 0xD4, 0x07 },
	{  99143, 500, 0x9C, 0x07 },
	{ 173143, 500, 0xD4, 0x07 },
	{ 191143, 300, 0xD4, 0x07 },
	{ 207143, 500, 0xCE, 0x07 },
	{ 225143, 500, 0xCE, 0x07 },
	{ 243143, 500, 0xD4, 0x07 },
	{ 261143, 500, 0xD4, 0x07 },
	{ 291143, 500, 0xD4, 0x07 },
	{ 339143, 500, 0x2C, 0x04 },
	{ 117143, 500, 0x7A, 0x07 },
	{ 135143, 300, 0x7A, 0x07 },
	{ 153143, 500, 0x01, 0x07 }
};

static void pt3_mx_rftune(u8 *data, u32 *size, u32 freq)
{
	u32 dig_rf_freq, tmp, frac_divider, kHz, MHz, i;
	u8 rf_data[] = {
		0x13, 0x00,	/* abort tune			*/
		0x3B, 0xC0,
		0x3B, 0x80,
		0x10, 0x95,	/* BW				*/
		0x1A, 0x05,
		0x61, 0x00,
		0x62, 0xA0,
		0x11, 0x40,	/* 2 bytes to store RF freq.	*/
		0x12, 0x0E,	/* 2 bytes to store RF freq.	*/
		0x13, 0x01	/* start tune			*/
	};

	dig_rf_freq = 0;
	tmp = 0;
	frac_divider = 1000000;
	kHz = 1000;
	MHz = 1000000;

	dig_rf_freq = freq / MHz;
	tmp = freq % MHz;

	for (i = 0; i < 6; i++) {
		dig_rf_freq <<= 1;
		frac_divider /= 2;
		if (tmp > frac_divider) {
			tmp -= frac_divider;
			dig_rf_freq++;
		}
	}
	if (tmp > 7812)
		dig_rf_freq++;

	rf_data[2 * (7) + 1] = (u8)(dig_rf_freq);
	rf_data[2 * (8) + 1] = (u8)(dig_rf_freq >> 8);

	for (i = 0; i < sizeof(SHF_DVBT_TAB)/sizeof(*SHF_DVBT_TAB); i++) {
		if ((freq >= (SHF_DVBT_TAB[i].freq - SHF_DVBT_TAB[i].freq_th) * kHz) &&
				(freq <= (SHF_DVBT_TAB[i].freq + SHF_DVBT_TAB[i].freq_th) * kHz)) {
			rf_data[2 * (5) + 1] = SHF_DVBT_TAB[i].shf_val;
			rf_data[2 * (6) + 1] = 0xa0 | SHF_DVBT_TAB[i].shf_dir;
			break;
		}
	}
	memcpy(data, rf_data, sizeof(rf_data));
	*size = sizeof(rf_data);

	pr_debug("mx_rftune freq=%d\n", freq);
}

static void pt3_mx_write(struct pt3_adapter *adap, struct pt3_bus *bus, u8 *data, size_t size)
{
	pt3_tc_write_tuner_without_addr(adap, bus, data, size);
}

static void pt3_mx_standby(struct pt3_adapter *adap)
{
	u8 data[4] = {0x01, 0x00, 0x13, 0x00};
	pt3_mx_write(adap, NULL, data, sizeof(data));
}

static void pt3_mx_set_register(struct pt3_adapter *adap, struct pt3_bus *bus, u8 addr, u8 value)
{
	u8 data[2] = {addr, value};
	pt3_mx_write(adap, bus, data, sizeof(data));
}

static void pt3_mx_idac_setting(struct pt3_adapter *adap, struct pt3_bus *bus)
{
	u8 data[] = {
		0x0D, 0x00,
		0x0C, 0x67,
		0x6F, 0x89,
		0x70, 0x0C,
		0x6F, 0x8A,
		0x70, 0x0E,
		0x6F, 0x8B,
		0x70, 0x10+12,
	};
	pt3_mx_write(adap, bus, data, sizeof(data));
}

static void pt3_mx_tuner_rftune(struct pt3_adapter *adap, struct pt3_bus *bus, u32 freq)
{
	u8 data[100];
	u32 size;

	size = 0;
	adap->freq = freq;
	pt3_mx_rftune(data, &size, freq);
	if (size != 20) {
		pr_debug("fail mx_rftune size = %d\n", size);
		return;
	}
	pt3_mx_write(adap, bus, data, 14);
	msleep_interruptible(1);
	pt3_mx_write(adap, bus, data + 14, 6);
	msleep_interruptible(1);
	pt3_mx_set_register(adap, bus, 0x1a, 0x0d);
	pt3_mx_idac_setting(adap, bus);
}

static void pt3_mx_wakeup(struct pt3_adapter *adap)
{
	u8 data[2] = {0x01, 0x01};

	pt3_mx_write(adap, NULL, data, sizeof(data));
	pt3_mx_tuner_rftune(adap, NULL, adap->freq);
}

static void pt3_mx_set_sleep_mode(struct pt3_adapter *adap, bool sleep)
{
	if (sleep)
		pt3_mx_standby(adap);
	else
		pt3_mx_wakeup(adap);
}

int pt3_mx_set_sleep(struct pt3_adapter *adap, bool sleep)
{
	int ret;

	if (sleep) {
		ret = pt3_tc_set_agc_t(adap, PT3_TC_AGC_MANUAL);
		if (ret)
			return ret;
		pt3_mx_set_sleep_mode(adap, sleep);
		pt3_tc_write_sleep_time(adap, sleep);
	} else {
		pt3_tc_write_sleep_time(adap, sleep);
		pt3_mx_set_sleep_mode(adap, sleep);
	}
	adap->sleep = sleep;
	return 0;
}

static u8 PT3_MX_FREQ_TABLE[][3] = {
	{   2, 0,  3 },
	{  12, 1, 22 },
	{  21, 0, 12 },
	{  62, 1, 63 },
	{ 112, 0, 62 }
};

void pt3_mx_get_channel_frequency(struct pt3_adapter *adap, u32 channel, bool *catv, u32 *number, u32 *freq)
{
	u32 i;
	s32 freq_offset = 0;

	if (12 <= channel)
		freq_offset += 2;
	if (17 <= channel)
		freq_offset -= 2;
	if (63 <= channel)
		freq_offset += 2;
	*freq = 93 + channel * 6 + freq_offset;

	for (i = 0; i < sizeof(PT3_MX_FREQ_TABLE) / sizeof(*PT3_MX_FREQ_TABLE); i++) {
		if (channel <= PT3_MX_FREQ_TABLE[i][0]) {
			*catv = PT3_MX_FREQ_TABLE[i][1] ? true : false;
			*number = channel + PT3_MX_FREQ_TABLE[i][2] - PT3_MX_FREQ_TABLE[i][0];
			break;
		}
	}
}

static u32 RF_TABLE[112] = {
	0x058d3f49, 0x05e8ccc9, 0x06445a49, 0x069fe7c9, 0x06fb7549,
	0x075702c9, 0x07b29049, 0x080e1dc9, 0x0869ab49, 0x08c538c9,
	0x0920c649, 0x097c53c9, 0x09f665c9, 0x0a51f349, 0x0aad80c9,
	0x0b090e49, 0x0b649bc9, 0x0ba1a4c9, 0x0bfd3249, 0x0c58bfc9,
	0x0cb44d49, 0x0d0fdac9, 0x0d6b6849, 0x0dc6f5c9, 0x0e228349,
	0x0e7e10c9, 0x0ed99e49, 0x0f352bc9, 0x0f90b949, 0x0fec46c9,
	0x1047d449, 0x10a361c9, 0x10feef49, 0x115a7cc9, 0x11b60a49,
	0x121197c9, 0x126d2549, 0x12c8b2c9, 0x13244049, 0x137fcdc9,
	0x13db5b49, 0x1436e8c9, 0x14927649, 0x14ee03c9, 0x15499149,
	0x15a51ec9, 0x1600ac49, 0x165c39c9, 0x16b7c749, 0x171354c9,
	0x176ee249, 0x17ca6fc9, 0x1825fd49, 0x18818ac9, 0x18dd1849,
	0x1938a5c9, 0x19943349, 0x19efc0c9, 0x1a4b4e49, 0x1aa6dbc9,
	0x1b026949, 0x1b5df6c9, 0x1bb98449, 0x1c339649, 0x1c8f23c9,
	0x1ceab149, 0x1d463ec9, 0x1da1cc49, 0x1dfd59c9, 0x1e58e749,
	0x1eb474c9, 0x1f100249, 0x1f6b8fc9, 0x1fc71d49, 0x2022aac9,
	0x207e3849, 0x20d9c5c9, 0x21355349, 0x2190e0c9, 0x21ec6e49,
	0x2247fbc9, 0x22a38949, 0x22ff16c9, 0x235aa449, 0x23b631c9,
	0x2411bf49, 0x246d4cc9, 0x24c8da49, 0x252467c9, 0x257ff549,
	0x25db82c9, 0x26371049, 0x26929dc9, 0x26ee2b49, 0x2749b8c9,
	0x27a54649, 0x2800d3c9, 0x285c6149, 0x28b7eec9, 0x29137c49,
	0x296f09c9, 0x29ca9749, 0x2a2624c9, 0x2a81b249, 0x2add3fc9,
	0x2b38cd49, 0x2b945ac9, 0x2befe849, 0x2c4b75c9, 0x2ca70349,
	0x2d0290c9, 0x2d5e1e49,
};

static void pt3_mx_read(struct pt3_adapter *adap, struct pt3_bus *bus, u8 addr, u8 *data)
{
	u8 write[2] = {0xfb, addr};

	pt3_tc_write_tuner_without_addr(adap, bus, write, sizeof(write));
	pt3_tc_read_tuner_without_addr(adap, bus, data);
}

static void pt3_mx_rfsynth_lock_status(struct pt3_adapter *adap, struct pt3_bus *bus, bool *locked)
{
	u8 data;

	*locked = false;
	pt3_mx_read(adap, bus, 0x16, &data);
	data &= 0x0c;
	if (data == 0x0c)
		*locked = true;
}

static void pt3_mx_refsynth_lock_status(struct pt3_adapter *adap, struct pt3_bus *bus, bool *locked)
{
	u8 data;

	*locked = false;
	pt3_mx_read(adap, bus, 0x16, &data);
	data &= 0x03;
	if (data == 0x03)
		*locked = true;
}

bool pt3_mx_locked(struct pt3_adapter *adap)
{
	bool locked1 = false, locked2 = false;
	struct timeval begin, now;

	do_gettimeofday(&begin);
	while (1) {
		do_gettimeofday(&now);
		pt3_mx_rfsynth_lock_status(adap, NULL, &locked1);
		pt3_mx_refsynth_lock_status(adap, NULL, &locked2);
		if (locked1 && locked2)
			break;
		if (pt3_tc_time_diff(&begin, &now) > 1000)
			break;
		msleep_interruptible(1);
	}
	pr_debug("#%d mx locked1=%d locked2=%d\n", adap->idx, locked1, locked2);
	return locked1 && locked2;
}

int pt3_mx_set_frequency(struct pt3_adapter *adap, u32 channel, s32 offset)
{
	bool catv;
	u32 number, freq, real_freq;
	int ret = pt3_tc_set_agc_t(adap, PT3_TC_AGC_MANUAL);

	if (ret)
		return ret;
	pt3_mx_get_channel_frequency(adap, channel, &catv, &number, &freq);
	pr_debug("#%d ch%d%s no%d %dHz\n", adap->idx, channel, catv ? " CATV" : "", number, freq);
	/* real_freq = (7 * freq + 1 + offset) * 1000000.0/7.0; */
	real_freq = RF_TABLE[channel];

	pt3_mx_tuner_rftune(adap, NULL, real_freq);

	return (!pt3_mx_locked(adap)) ? -ETIMEDOUT : pt3_tc_set_agc_t(adap, PT3_TC_AGC_AUTO);
}

/**** ISDB-S ****/

enum pt3_fe_s_tune_state {
	PT3S_IDLE,
	PT3S_SET_FREQUENCY,
	PT3S_SET_MODULATION,
	PT3S_CHECK_MODULATION,
	PT3S_SET_TS_ID,
	PT3S_CHECK_TS_ID,
	PT3S_TRACK,
};

struct pt3_fe_s_state {
	struct pt3_adapter *adap;
	struct dvb_frontend fe;
	enum pt3_fe_s_tune_state tune_state;
};

static int pt3_fe_s_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct pt3_fe_s_state *state = fe->demodulator_priv;
	struct pt3_adapter *adap = state->adap;
	u32 cn = 0;
	s32 x1, x2, x3, x4, x5, y;

	int ret = pt3_tc_read_cn_s(adap, NULL, &cn);
	if (ret)
		return ret;

	cn -= 3000;
	x1 = int_sqrt(cn << 16) * ((15625ll << 21) / 1000000);
	x2 = (s64)x1 * x1 >> 31;
	x3 = (s64)x2 * x1 >> 31;
	x4 = (s64)x2 * x2 >> 31;
	x5 = (s64)x4 * x1 >> 31;

	y = (58857ll << 23) / 1000;
	y -= (s64)x1 * ((89565ll << 24) / 1000) >> 30;
	y += (s64)x2 * ((88977ll << 24) / 1000) >> 28;
	y -= (s64)x3 * ((50259ll << 25) / 1000) >> 27;
	y += (s64)x4 * ((14341ll << 27) / 1000) >> 27;
	y -= (s64)x5 * ((16346ll << 30) / 10000) >> 28;

	*snr = y < 0 ? 0 : y >> 15;
	pr_debug("#%d cn=%d s/n=%d\n", adap->idx, cn, *snr);
	return 0;
}

static int pt3_fe_s_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static void pt3_fe_s_release(struct dvb_frontend *fe)
{
	kfree(fe->demodulator_priv);
}

static int pt3_fe_s_init(struct dvb_frontend *fe)
{
	struct pt3_fe_s_state *state = fe->demodulator_priv;
	state->tune_state = PT3S_IDLE;
	return pt3_qm_set_sleep(state->adap->qm, false);
}

static int pt3_fe_s_sleep(struct dvb_frontend *fe)
{
	struct pt3_fe_s_state *state = fe->demodulator_priv;
	return pt3_qm_set_sleep(state->adap->qm, true);
}

u32 pt3_fe_s_get_channel(u32 frequency)
{
	u32 freq = frequency / 10,
	    ch0 = (freq - 104948) / 3836, diff0 = freq - (104948 + 3836 * ch0),
	    ch1 = (freq - 161300) / 4000, diff1 = freq - (161300 + 4000 * ch1),
	    ch2 = (freq - 159300) / 4000, diff2 = freq - (159300 + 4000 * ch2),
	    min = diff0 < diff1 ? diff0 : diff1;

	if (diff2 < min)
		return ch2 + 24;
	else if (min == diff1)
		return ch1 + 12;
	else
		return ch0;
}

static int pt3_fe_s_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct pt3_fe_s_state *state = fe->demodulator_priv;

	switch (state->tune_state) {
	case PT3S_IDLE:
	case PT3S_SET_FREQUENCY:
		*status = 0;
		return 0;

	case PT3S_SET_MODULATION:
	case PT3S_CHECK_MODULATION:
		*status |= FE_HAS_SIGNAL;
		return 0;

	case PT3S_SET_TS_ID:
	case PT3S_CHECK_TS_ID:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER;
		return 0;

	case PT3S_TRACK:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;
	}
	BUG();
}

static int pt3_fe_s_tune(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct pt3_fe_s_state *state = fe->demodulator_priv;
	struct pt3_adapter *adap = state->adap;
	struct tmcc_s *tmcc = &adap->qm->tmcc;
	int i, ret,
	    freq = state->fe.dtv_property_cache.frequency,
	    tsid = state->fe.dtv_property_cache.stream_id,
	    ch = (freq < 1024) ? freq : pt3_fe_s_get_channel(freq);	/* consider as channel ID if low */

	if (re_tune)
		state->tune_state = PT3S_SET_FREQUENCY;

	switch (state->tune_state) {
	case PT3S_IDLE:
		*delay = 3 * HZ;
		*status = 0;
		return 0;

	case PT3S_SET_FREQUENCY:
		pr_debug("#%d freq %d tsid 0x%x ch %d\n", adap->idx, freq, tsid, ch);
		ret = pt3_qm_set_frequency(adap->qm, ch);
		if (ret)
			return ret;
		adap->channel = ch;
		state->tune_state = PT3S_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case PT3S_SET_MODULATION:
		for (i = 0; i < 1000; i++) {
			ret = pt3_tc_read_tmcc_s(adap, NULL, tmcc);
			if (!ret)
				break;
			msleep_interruptible(1);
		}
		if (ret) {
			pr_debug("fail tc_read_tmcc_s ret=0x%x\n", ret);
			return ret;
		}
		pr_debug("slots=%d,%d,%d,%d mode=%d,%d,%d,%d\n",
				tmcc->slot[0], tmcc->slot[1], tmcc->slot[2], tmcc->slot[3],
				tmcc->mode[0], tmcc->mode[1], tmcc->mode[2], tmcc->mode[3]);
		state->tune_state = PT3S_CHECK_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case PT3S_CHECK_MODULATION:
		pr_debug("tmcc->id=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
				tmcc->id[0], tmcc->id[1], tmcc->id[2], tmcc->id[3],
				tmcc->id[4], tmcc->id[5], tmcc->id[6], tmcc->id[7]);
		for (i = 0; i < sizeof(tmcc->id)/sizeof(tmcc->id[0]); i++) {
			pr_debug("tsid %x i %d tmcc->id %x\n", tsid, i, tmcc->id[i]);
			if (tmcc->id[i] == tsid)
				break;
		}
		if (tsid < sizeof(tmcc->id)/sizeof(tmcc->id[0]))	/* consider as slot# */
			i = tsid;
		if (i == sizeof(tmcc->id)/sizeof(tmcc->id[0])) {
			pr_debug("#%d i%d tsid 0x%x not found\n", adap->idx, i, tsid);
			return -EINVAL;
		}
		adap->offset = i;
		pr_debug("#%d found tsid 0x%x on slot %d\n", adap->idx, tsid, i);
		state->tune_state = PT3S_SET_TS_ID;
		*delay = 0;
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER;
		return 0;

	case PT3S_SET_TS_ID:
		ret = pt3_tc_write_id_s(adap, NULL, (u16)tmcc->id[adap->offset]);
		if (ret) {
			pr_debug("fail set_tmcc_s ret=%d\n", ret);
			return ret;
		}
		state->tune_state = PT3S_CHECK_TS_ID;
		return 0;

	case PT3S_CHECK_TS_ID:
		for (i = 0; i < 1000; i++) {
			u16 short_id;
			ret = pt3_tc_read_id_s(adap, NULL, &short_id);
			if (ret) {
				pr_debug("fail get_id_s ret=%d\n", ret);
				return ret;
			}
			tsid = short_id;
			pr_debug("#%d tsid=0x%x\n", adap->idx, tsid);
			if ((tsid & 0xffff) == tmcc->id[adap->offset])
				break;
			msleep_interruptible(1);
		}
		state->tune_state = PT3S_TRACK;

	case PT3S_TRACK:
		*delay = 3 * HZ;
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;
	}
	BUG();
}

static struct dvb_frontend_ops pt3_fe_s_ops = {
	.delsys = { SYS_ISDBS },
	.info = {
		.name = "PT3 ISDB-S",
		.frequency_min = 1,
		.frequency_max = 2150000,
		.frequency_stepsize = 1000,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO | FE_CAN_MULTISTREAM |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},
	.read_snr = pt3_fe_s_read_snr,
	.read_status = pt3_fe_s_read_status,
	.get_frontend_algo = pt3_fe_s_get_frontend_algo,
	.release = pt3_fe_s_release,
	.init = pt3_fe_s_init,
	.sleep = pt3_fe_s_sleep,
	.tune = pt3_fe_s_tune,
};

struct dvb_frontend *pt3_fe_s_attach(struct pt3_adapter *adap)
{
	struct dvb_frontend *fe;
	struct pt3_fe_s_state *state = kzalloc(sizeof(struct pt3_fe_s_state), GFP_KERNEL);

	if (!state)
		return NULL;
	state->adap = adap;
	fe = &state->fe;
	memcpy(&fe->ops, &pt3_fe_s_ops, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = state;
	return fe;
}

/**** ISDB-T ****/

enum pt3_fe_t_tune_state {
	PT3T_IDLE,
	PT3T_SET_FREQUENCY,
	PT3T_CHECK_FREQUENCY,
	PT3T_SET_MODULATION,
	PT3T_CHECK_MODULATION,
	PT3T_TRACK,
	PT3T_ABORT,
};

struct pt3_fe_t_state {
	struct pt3_adapter *adap;
	struct dvb_frontend fe;
	enum pt3_fe_t_tune_state tune_state;
};

static int pt3_fe_t_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct pt3_fe_t_state *state = fe->demodulator_priv;
	struct pt3_adapter *adap = state->adap;
	u32 cn = 0;
	s32 x, y;

	int ret = pt3_tc_read_cndat_t(adap, NULL, &cn);
	if (ret)
		return ret;

	x = 10 * (intlog10(0x540000 * 100 / cn) - (2 << 24));
	y = (24ll << 46) / 1000000;
	y = ((s64)y * x >> 30) - (16ll << 40) / 10000;
	y = ((s64)y * x >> 29) + (398ll << 35) / 10000;
	y = ((s64)y * x >> 30) + (5491ll << 29) / 10000;
	y = ((s64)y * x >> 30) + (30965ll << 23) / 10000;
	*snr = y >> 15;
	pr_debug("#%d CN=%d S/N=%d\n", adap->idx, cn, *snr);
	return 0;
}

static int pt3_fe_t_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static void pt3_fe_t_release(struct dvb_frontend *fe)
{
	kfree(fe->demodulator_priv);
}

static int pt3_fe_t_init(struct dvb_frontend *fe)
{
	struct pt3_fe_t_state *state = fe->demodulator_priv;
	state->tune_state = PT3T_IDLE;
	return pt3_mx_set_sleep(state->adap, false);
}

static int pt3_fe_t_sleep(struct dvb_frontend *fe)
{
	struct pt3_fe_t_state *state = fe->demodulator_priv;
	return pt3_mx_set_sleep(state->adap, true);
}

static int pt3_fe_t_get_tmcc(struct pt3_adapter *adap, struct tmcc_t *tmcc)
{
	int b = 0, retryov, tmunvld, fulock;

	if (unlikely(!tmcc))
		return -EINVAL;
	while (1) {
		pt3_tc_read_retryov_tmunvld_fulock(adap, NULL, &retryov, &tmunvld, &fulock);
		if (!fulock) {
			b = 1;
			break;
		} else {
			if (retryov)
				break;
		}
		msleep_interruptible(1);
	}
	if (likely(b))
		pt3_tc_read_tmcc_t(adap, NULL, tmcc);
	return b ? 0 : -EBADMSG;
}

static int pt3_fe_t_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct pt3_fe_t_state *state = fe->demodulator_priv;

	switch (state->tune_state) {
	case PT3T_IDLE:
	case PT3T_SET_FREQUENCY:
	case PT3T_CHECK_FREQUENCY:
		*status = 0;
		return 0;

	case PT3T_SET_MODULATION:
	case PT3T_CHECK_MODULATION:
	case PT3T_ABORT:
		*status |= FE_HAS_SIGNAL;
		return 0;

	case PT3T_TRACK:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;
	}
	BUG();
}

#define NHK (RF_TABLE[77])
int pt3_fe_t_freq(int freq)
{
	if (freq >= 90000000)
		return freq;				/* real_freq	*/
	if (freq > 255)
		return NHK;
	if (freq > 127)
		return RF_TABLE[freq - 128];		/* freqno (IO#)	*/
	if (freq > 63) {				/* CATV		*/
		freq -= 64;
		if (freq > 22)
			return RF_TABLE[freq - 1];	/* C23-C62	*/
		if (freq > 12)
			return RF_TABLE[freq - 10];	/* C13-C22	*/
		return NHK;
	}
	if (freq > 62)
		return NHK;
	if (freq > 12)
		return RF_TABLE[freq + 50];		/* 13-62	*/
	if (freq >  3)
		return RF_TABLE[freq +  9];		/*  4-12	*/
	if (freq)
		return RF_TABLE[freq -  1];		/*  1-3		*/
	return NHK;
}

static int pt3_fe_t_tune(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct pt3_fe_t_state *state = fe->demodulator_priv;
	struct tmcc_t tmcc_t;
	int ret, i;

	if (re_tune)
		state->tune_state = PT3T_SET_FREQUENCY;

	switch (state->tune_state) {
	case PT3T_IDLE:
		*delay = 3 * HZ;
		*status = 0;
		return 0;

	case PT3T_SET_FREQUENCY:
		ret = pt3_tc_set_agc_t(state->adap, PT3_TC_AGC_MANUAL);
		if (ret)
			return ret;
		pt3_mx_tuner_rftune(state->adap, NULL, pt3_fe_t_freq(state->fe.dtv_property_cache.frequency));
		state->tune_state = PT3T_CHECK_FREQUENCY;
		*delay = 0;
		*status = 0;
		return 0;

	case PT3T_CHECK_FREQUENCY:
		if (!pt3_mx_locked(state->adap)) {
			*delay = HZ;
			*status = 0;
			return 0;
		}
		state->tune_state = PT3T_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case PT3T_SET_MODULATION:
		ret = pt3_tc_set_agc_t(state->adap, PT3_TC_AGC_AUTO);
		if (ret)
			return ret;
		state->tune_state = PT3T_CHECK_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case PT3T_CHECK_MODULATION:
		for (i = 0; i < 1000; i++) {
			ret = pt3_fe_t_get_tmcc(state->adap, &tmcc_t);
			if (!ret)
				break;
			msleep_interruptible(2);
		}
		if (ret) {
			pr_debug("#%d fail get_tmcc_t ret=%d\n", state->adap->idx, ret);
				state->tune_state = PT3T_ABORT;
				*delay = HZ;
				return 0;
		}
		state->tune_state = PT3T_TRACK;

	case PT3T_TRACK:
		*delay = 3 * HZ;
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;

	case PT3T_ABORT:
		*delay = 3 * HZ;
		*status = FE_HAS_SIGNAL;
		return 0;
	}
	BUG();
}

static struct dvb_frontend_ops pt3_fe_t_ops = {
	.delsys = { SYS_ISDBT },
	.info = {
		.name = "PT3 ISDB-T",
		.frequency_min = 1,
		.frequency_max = 770000000,
		.frequency_stepsize = 142857,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},
	.read_snr = pt3_fe_t_read_snr,
	.read_status = pt3_fe_t_read_status,
	.get_frontend_algo = pt3_fe_t_get_frontend_algo,
	.release = pt3_fe_t_release,
	.init = pt3_fe_t_init,
	.sleep = pt3_fe_t_sleep,
	.tune = pt3_fe_t_tune,
};

struct dvb_frontend *pt3_fe_t_attach(struct pt3_adapter *adap)
{
	struct dvb_frontend *fe;
	struct pt3_fe_t_state *state = kzalloc(sizeof(struct pt3_fe_t_state), GFP_KERNEL);
	if (!state)
		return NULL;
	state->adap = adap;
	fe = &state->fe;
	memcpy(&fe->ops, &pt3_fe_t_ops, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = state;
	return fe;
}

