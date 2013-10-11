#include "dvb_math.h"

typedef enum {
	PT3T_IDLE,
	PT3T_SET_FREQUENCY,
	PT3T_CHECK_FREQUENCY,
	PT3T_SET_MODULATION,
	PT3T_CHECK_MODULATION,
	PT3T_TRACK,
	PT3T_ABORT,
} PT3T_TUNE_STATE;

typedef struct {
	PT3_ADAPTER *adap;
	DVB_FRONTEND fe;
	PT3T_TUNE_STATE tune_state;
} PT3T_STATE;

static int pt3t_read_snr(DVB_FRONTEND *fe, u16 *snr) // signal to noise ratio
{
	PT3T_STATE *state = fe->demodulator_priv;
	PT3_ADAPTER *adap = state->adap;
	u32 cn = 0;
	s32 x, y;

	int ret = pt3_tc_read_cndat_t(adap, NULL, &cn);
	if (ret) return ret;

	x = 10 * (intlog10(0x540000 * 100 / cn) - (2 << 24));
	y = (24ll << 46) / 1000000;
	y = ((s64)y * x >> 30) - (16ll << 40) / 10000;
	y = ((s64)y * x >> 29) + (398ll << 35) / 10000;
	y = ((s64)y * x >> 30) + (5491ll << 29) / 10000;
	y = ((s64)y * x >> 30) + (30965ll << 23) / 10000;
	*snr = y >> 15;
	PT3_PRINTK(KERN_INFO, "#%d CN=%d S/N=%d\n", adap->idx, cn, *snr);
	return 0;
}

static int pt3t_get_frontend_algo(DVB_FRONTEND *fe)
{
	return DVBFE_ALGO_HW;
}

static void pt3t_release(DVB_FRONTEND *fe)
{
	kfree(fe->demodulator_priv);
}

static int pt3t_init(DVB_FRONTEND *fe)
{
	PT3T_STATE *state = fe->demodulator_priv;
	state->tune_state = PT3T_IDLE;
//	return pt3_set_tuner_sleep(state->adap, false);
	return pt3_mx_set_sleep(state->adap, false);
}

static int pt3t_sleep(DVB_FRONTEND *fe)
{
	PT3T_STATE *state = fe->demodulator_priv;
//	int ret = pt3_set_frequency(state->adap, state->adap->init_ch, 0);
//	return (ret < 0) ? ret : pt3_set_tuner_sleep(state->adap, true);
	return pt3_mx_set_sleep(state->adap, true);
}

static int pt3t_get_tmcc(PT3_ADAPTER *adap, TMCC_T *tmcc)
{
	int b = 0, retryov, tmunvld, fulock;

	if (unlikely(!tmcc)) return -EINVAL;
	while (1) {
		pt3_tc_read_retryov_tmunvld_fulock(adap, NULL, &retryov, &tmunvld, &fulock);
		if (!fulock) {
			b = 1;
			break;
		} else {
			if (retryov)
				break;
		}
		PT3_WAIT_MS_INT(1);	
	}
	if (likely(b))
		pt3_tc_read_tmcc_t(adap, NULL, tmcc);
	return b ? 0 : -EBADMSG;
}

static int pt3t_read_status(DVB_FRONTEND *fe, fe_status_t *status)
{
	PT3T_STATE *state = fe->demodulator_priv;

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

#define NHK (REAL_TABLE[77])
int pt3t_freq(int freq)
{
	if (freq > 255) return freq;			// real_freq
	if (freq > 127) return REAL_TABLE[freq - 128];	// freqno (io_no)
	if (freq > 63) {				// CATV
		freq -= 64;
		if (freq > 22) return REAL_TABLE[freq - 1];	// C23-C62
		if (freq > 12) return REAL_TABLE[freq - 10];	// C13-C22
		return NHK;
	}
	if (freq > 62) return NHK;
	if (freq > 12) return REAL_TABLE[freq + 50];	// 13-62
	if (freq >  3) return REAL_TABLE[freq +  9];	//  4-12
	if (freq)      return REAL_TABLE[freq -  1];	//  1-3
	return NHK;
}

static int pt3t_tune(DVB_FRONTEND *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	TMCC_T tmcc_t;
	int ret, i;
	PT3T_STATE *state = fe->demodulator_priv;

	if (re_tune) state->tune_state = PT3T_SET_FREQUENCY;

	switch (state->tune_state) {
	case PT3T_IDLE:
		*delay = 3 * HZ;
		*status = 0;
		return 0;

	case PT3T_SET_FREQUENCY:
		//pt3_set_frequency(state->adap,77,0); // NHK
		if ((ret = pt3_tc_set_agc_t(state->adap, PT3_TC_AGC_MANUAL)))
			return ret;
		pt3_mx_tuner_rftune(state->adap, NULL, pt3t_freq(state->fe.dtv_property_cache.frequency));
		state->tune_state = PT3T_CHECK_FREQUENCY;
		*delay = 0;
		*status = 0;
		return 0;

	case PT3T_CHECK_FREQUENCY:
		if (!pt3_mx_locked(state->adap)) {
			*delay = PT3_MS(1);
			*status = 0;
			return 0;
		}
		state->tune_state = PT3T_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case PT3T_SET_MODULATION:
		if ((ret = pt3_tc_set_agc_t(state->adap, PT3_TC_AGC_AUTO)))
			return ret;
		state->tune_state = PT3T_CHECK_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case PT3T_CHECK_MODULATION:
		for (i = 0; i < 1000; i++) {
			if (!(ret = pt3t_get_tmcc(state->adap, &tmcc_t)))
				break;
			PT3_WAIT_MS_INT(2);
		}
		if (ret) {
			PT3_PRINTK(KERN_ALERT, "#%d fail get_tmcc_t ret=0x%x\n", state->adap->idx, ret);
				state->tune_state = PT3T_ABORT;
				*delay = 3 * HZ;
				return 0;				
		}
		PT3_WAIT_MS_INT(50);					// wait for fill buffer
		pt3_dma_set_test_mode(state->adap->dma, RESET, 0);	// reset_error_count
		state->tune_state = PT3T_TRACK;
		// fall through

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

static DVB_FRONTEND_OPS pt3t_ops = {
	.delsys = { SYS_ISDBT },
	.info = {
		.name = "PT3 ISDB-T",
		.frequency_min = 1,//90000000,
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

DVB_FRONTEND *pt3t_attach(PT3_ADAPTER *adap)
{
	DVB_FRONTEND *fe;
	PT3T_STATE *state = kzalloc(sizeof(PT3T_STATE), GFP_KERNEL);

	if (!state) return NULL;
	state->adap = adap;
	fe = &state->fe;
	memcpy(&fe->ops, &pt3t_ops, sizeof(DVB_FRONTEND_OPS));
	fe->demodulator_priv = state;
	return fe;
}

