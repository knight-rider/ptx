enum pt3s_tune_state {
	PT3S_IDLE,
	PT3S_SET_FREQUENCY,
	PT3S_SET_MODULATION,
	PT3S_CHECK_MODULATION,
	PT3S_SET_TS_ID,
	PT3S_CHECK_TS_ID,
	PT3S_TRACK,
};

struct pt3s_state {
	struct pt3_adapter *adap;
	struct dvb_frontend fe;
	enum pt3s_tune_state tune_state;
};

static int pt3s_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct pt3s_state *state = fe->demodulator_priv;
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

static int pt3s_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static void pt3s_release(struct dvb_frontend *fe)
{
	kfree(fe->demodulator_priv);
}

static int pt3s_init(struct dvb_frontend *fe)
{
	struct pt3s_state *state = fe->demodulator_priv;
	state->tune_state = PT3S_IDLE;
	return pt3_qm_set_sleep(state->adap->qm, false);
}

static int pt3s_sleep(struct dvb_frontend *fe)
{
	struct pt3s_state *state = fe->demodulator_priv;
	return pt3_qm_set_sleep(state->adap->qm, true);
}

u32 pt3s_get_channel(u32 frequency)
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

static int pt3s_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct pt3s_state *state = fe->demodulator_priv;

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

static int pt3s_tune(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct pt3s_state *state = fe->demodulator_priv;
	struct pt3_adapter *adap = state->adap;
	struct tmcc_s *tmcc = &adap->qm->tmcc;
	int i, ret,
	    freq = state->fe.dtv_property_cache.frequency,
	    tsid = state->fe.dtv_property_cache.stream_id,
	    ch = (freq < 1024) ? freq : pt3s_get_channel(freq);	/* consider as channel ID if low */

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

static struct dvb_frontend_ops pt3s_ops = {
	.delsys = { SYS_ISDBS },
	.info = {
		.name = "PT3 ISDB-S",
		.frequency_min = 1,
		.frequency_max = 2150000,
		.frequency_stepsize = 1000,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO | FE_CAN_MULTISTREAM |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},
	.read_snr = pt3s_read_snr,
	.read_status = pt3s_read_status,
	.get_frontend_algo = pt3s_get_frontend_algo,
	.release = pt3s_release,
	.init = pt3s_init,
	.sleep = pt3s_sleep,
	.tune = pt3s_tune,
};

struct dvb_frontend *pt3s_attach(struct pt3_adapter *adap)
{
	struct dvb_frontend *fe;
	struct pt3s_state *state = kzalloc(sizeof(struct pt3s_state), GFP_KERNEL);

	if (!state)
		return NULL;
	state->adap = adap;
	fe = &state->fe;
	memcpy(&fe->ops, &pt3s_ops, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = state;
	return fe;
}

