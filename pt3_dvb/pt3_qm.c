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
	PT3_PRINTK(KERN_DEBUG, "#%d ch %d freq %d kHz\n", qm->adap->idx, channel, freq_kHz);

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
	PT3_PRINTK(KERN_DEBUG, "#%d qm_get_locked %d ret=0x%x\n", qm->adap->idx, locked, ret);
	if (!locked)
		return -ETIMEDOUT;

	ret = pt3_tc_set_agc_s(qm->adap, PT3_TC_AGC_AUTO);
	if (!ret) {
		qm->adap->channel = channel;
		qm->adap->offset = 0;
	}
	return ret;
}

