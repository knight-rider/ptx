#include "dvb_math.h"

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

