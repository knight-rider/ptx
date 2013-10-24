#include "dvb_math.h"

enum pt3t_tune_state {
	PT3T_IDLE,
	PT3T_SET_FREQUENCY,
	PT3T_CHECK_FREQUENCY,
	PT3T_SET_MODULATION,
	PT3T_CHECK_MODULATION,
	PT3T_TRACK,
	PT3T_ABORT,
};

struct pt3t_state {
	struct pt3_adapter *adap;
	struct dvb_frontend fe;
	enum pt3t_tune_state tune_state;
};

static int pt3t_read_snr(struct dvb_frontend *fe, u16 *snr)
{
	struct pt3t_state *state = fe->demodulator_priv;
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

static int pt3t_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

static void pt3t_release(struct dvb_frontend *fe)
{
	kfree(fe->demodulator_priv);
}

static int pt3t_init(struct dvb_frontend *fe)
{
	struct pt3t_state *state = fe->demodulator_priv;
	state->tune_state = PT3T_IDLE;
	return pt3_mx_set_sleep(state->adap, false);
}

static int pt3t_sleep(struct dvb_frontend *fe)
{
	struct pt3t_state *state = fe->demodulator_priv;
	return pt3_mx_set_sleep(state->adap, true);
}

static int pt3t_get_tmcc(struct pt3_adapter *adap, struct tmcc_t *tmcc)
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

static int pt3t_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct pt3t_state *state = fe->demodulator_priv;

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
int pt3t_freq(int freq)
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

static int pt3t_tune(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct pt3t_state *state = fe->demodulator_priv;
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
		pt3_mx_tuner_rftune(state->adap, NULL, pt3t_freq(state->fe.dtv_property_cache.frequency));
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
			ret = pt3t_get_tmcc(state->adap, &tmcc_t);
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

static struct dvb_frontend_ops pt3t_ops = {
	.delsys = { SYS_ISDBT },
	.info = {
		.name = "PT3 ISDB-T",
		.frequency_min = 1,
		.frequency_max = 770000000,
		.frequency_stepsize = 142857,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},
	.read_snr = pt3t_read_snr,
	.get_frontend_algo = pt3t_get_frontend_algo,
	.release = pt3t_release,
	.init = pt3t_init,
	.sleep = pt3t_sleep,
	.read_status = pt3t_read_status,
	.tune = pt3t_tune,
};

struct dvb_frontend *pt3t_attach(struct pt3_adapter *adap)
{
	struct dvb_frontend *fe;
	struct pt3t_state *state = kzalloc(sizeof(struct pt3t_state), GFP_KERNEL);
	if (!state)
		return NULL;
	state->adap = adap;
	fe = &state->fe;
	memcpy(&fe->ops, &pt3t_ops, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = state;
	return fe;
}

