/*
 * Earthsoft PT3 demodulator frontend Toshiba TC90522XBG OFDM(ISDB-T)/8PSK(ISDB-S)
 *
 * Copyright (C) 2014 Budi Rachmanto, AreMa Inc. <info@are.ma>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "dvb_math.h"
#include "tc90522.h"

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 Toshiba TC90522 OFDM(ISDB-T)/8PSK(ISDB-S) demodulator");
MODULE_LICENSE("GPL");

#define TC90522_PASSTHROUGH 0xfe

enum tc90522_state {
	TC90522_IDLE,
	TC90522_SET_FREQUENCY,
	TC90522_SET_MODULATION,
	TC90522_TRACK,
	TC90522_ABORT,
};

struct tc90522 {
	struct dvb_frontend fe;
	struct i2c_adapter *i2c;
	fe_delivery_system_t type;
	u8 idx, addr_demod;
	s32 offset;
	enum tc90522_state state;
};

int tc90522_write(struct dvb_frontend *fe, const u8 *data, int len)
{
	struct tc90522 *demod = fe->demodulator_priv;
	struct i2c_msg msg[3];
	u8 buf[6];

	if (data) {
		msg[0].addr = demod->addr_demod;
		msg[0].buf = (u8 *)data;
		msg[0].flags = 0;			/* write */
		msg[0].len = len;

		return i2c_transfer(demod->i2c, msg, 1) == 1 ? 0 : -EREMOTEIO;
	} else {
		u8 addr_tuner = (len >> 8) & 0xff,
		   addr_data = len & 0xff;
		if (len >> 16) {			/* read tuner without address */
			buf[0] = TC90522_PASSTHROUGH;
			buf[1] = (addr_tuner << 1) | 1;
			msg[0].buf = buf;
			msg[0].len = 2;
			msg[0].addr = demod->addr_demod;
			msg[0].flags = 0;		/* write */

			msg[1].buf = buf + 2;
			msg[1].len = 1;
			msg[1].addr = demod->addr_demod;
			msg[1].flags = I2C_M_RD;	/* read */

			return i2c_transfer(demod->i2c, msg, 2) == 2 ? buf[2] : -EREMOTEIO;
		} else {				/* read tuner */
			buf[0] = TC90522_PASSTHROUGH;
			buf[1] = addr_tuner << 1;
			buf[2] = addr_data;
			msg[0].buf = buf;
			msg[0].len = 3;
			msg[0].addr = demod->addr_demod;
			msg[0].flags = 0;		/* write */

			buf[3] = TC90522_PASSTHROUGH;
			buf[4] = (addr_tuner << 1) | 1;
			msg[1].buf = buf + 3;
			msg[1].len = 2;
			msg[1].addr = demod->addr_demod;
			msg[1].flags = 0;		/* write */

			msg[2].buf = buf + 5;
			msg[2].len = 1;
			msg[2].addr = demod->addr_demod;
			msg[2].flags = I2C_M_RD;	/* read */

			return i2c_transfer(demod->i2c, msg, 3) == 3 ? buf[5] : -EREMOTEIO;
		}
	}
}

int tc90522_write_data(struct dvb_frontend *fe, u8 addr_data, u8 *data, u8 len)
{
	u8 buf[len + 1];
	buf[0] = addr_data;
	memcpy(buf + 1, data, len);
	return tc90522_write(fe, buf, len + 1);
}

int tc90522_read(struct tc90522 *demod, u8 addr, u8 *buf, u8 buflen)
{
	struct i2c_msg msg[2];
	if (!buf || !buflen)
		return -EINVAL;

	buf[0] = addr;
	msg[0].addr = demod->addr_demod;
	msg[0].flags = 0;			/* write */
	msg[0].buf = buf;
	msg[0].len = 1;

	msg[1].addr = demod->addr_demod;
	msg[1].flags = I2C_M_RD;		/* read */
	msg[1].buf = buf;
	msg[1].len = buflen;

	return i2c_transfer(demod->i2c, msg, 2) == 2 ? 0 : -EREMOTEIO;
}

u32 tc90522_byten(const u8 *data, u32 n)
{
	u32 i, val = 0;

	for (i = 0; i < n; i++) {
		val <<= 8;
		val |= data[i];
	}
	return val;
}

int tc90522_read_id_s(struct tc90522 *demod, u16 *id)
{
	u8 buf[2];
	int ret = tc90522_read(demod, 0xe6, buf, 2);
	if (!ret)
		*id = tc90522_byten(buf, 2);
	return ret;
}

struct tmcc_s {			/* Transmission and Multiplexing Configuration Control */
	u32 mode[4];
	u32 slot[4];
	u32 id[8];
};

int tc90522_read_tmcc_s(struct tc90522 *demod, struct tmcc_s *tmcc)
{
	enum {
		BASE = 0xc5,
		SIZE = 0xe5 - BASE + 1
	};
	u8 data[SIZE];
	u32 i, byte_offset, bit_offset;

	int err = tc90522_read(demod, 0xc3, data, 1)	||
		((data[0] >> 4) & 1)			||
		tc90522_read(demod, 0xce, data, 2)	||
		(tc90522_byten(data, 2) == 0)		||
		tc90522_read(demod, 0xc3, data, 1)	||
		tc90522_read(demod, 0xc5, data, SIZE);
	if (err)
		return err;
	for (i = 0; i < 4; i++) {
		byte_offset = i >> 1;
		bit_offset = (i & 1) ? 0 : 4;
		tmcc->mode[i] = (data[0xc8 + byte_offset - BASE] >> bit_offset) & 0b00001111;
		tmcc->slot[i] = (data[0xca + i           - BASE] >>          0) & 0b00111111;
	}
	for (i = 0; i < 8; i++)
		tmcc->id[i] = tc90522_byten(data + 0xce + i * 2 - BASE, 2);
	return 0;
}

enum tc90522_pwr {
	TC90522_PWR_OFF		= 0x00,
	TC90522_PWR_AMP_ON	= 0x04,
	TC90522_PWR_TUNER_ON	= 0x40,
};

static enum tc90522_pwr tc90522_pwr = TC90522_PWR_OFF;

int tc90522_set_powers(struct tc90522 *demod, enum tc90522_pwr pwr)
{
	u8 data = pwr | 0b10011001;
	pr_debug("#%d tuner %s amp %s\n", demod->idx, pwr & TC90522_PWR_TUNER_ON ? "ON" : "OFF", pwr & TC90522_PWR_AMP_ON ? "ON" : "OFF");
	tc90522_pwr = pwr;
	return tc90522_write_data(&demod->fe, 0x1e, &data, 1);
}

/* dvb_frontend_ops */
int tc90522_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

int tc90522_sleep(struct dvb_frontend *fe)
{
	struct tc90522 *demod = fe->demodulator_priv;
	pr_debug("#%d %s %s\n", demod->idx, __func__, demod->type == SYS_ISDBS ? "S" : "T");
	return fe->ops.tuner_ops.sleep(fe);
}

int tc90522_wakeup(struct dvb_frontend *fe)
{
	struct tc90522 *demod = fe->demodulator_priv;
	pr_debug("#%d %s %s 0x%x\n", demod->idx, __func__, demod->type == SYS_ISDBS ? "S" : "T", tc90522_pwr);

	if (!tc90522_pwr)
		return	tc90522_set_powers(demod, TC90522_PWR_TUNER_ON)	||
			i2c_transfer(demod->i2c, NULL, 0)		||
			tc90522_set_powers(demod, TC90522_PWR_TUNER_ON | TC90522_PWR_AMP_ON);
	demod->state = TC90522_IDLE;
	return fe->ops.tuner_ops.init(fe);
}

void tc90522_release(struct dvb_frontend *fe)
{
	struct tc90522 *demod = fe->demodulator_priv;
	pr_debug("#%d %s\n", demod->idx, __func__);

	if (tc90522_pwr)
		tc90522_set_powers(demod, TC90522_PWR_OFF);
	tc90522_sleep(fe);
	fe->ops.tuner_ops.release(fe);
	kfree(demod);
}

s64 tc90522_get_cn_raw(struct tc90522 *demod)
{
	u8 buf[3], buflen = demod->type == SYS_ISDBS ? 2 : 3, addr = demod->type == SYS_ISDBS ? 0xbc : 0x8b;
	int err = tc90522_read(demod, addr, buf, buflen);
	return err < 0 ? err : tc90522_byten(buf, buflen);
}

s64 tc90522_get_cn_s(s64 raw)	/* @ .0001 dB */
{
	s64 x, y;

	raw -= 3000;
	if (raw < 0)
		raw = 0;
	x = int_sqrt(raw << 20);
	y = 16346ll * x - (143410ll << 16);
	y = ((x * y) >> 16) + (502590ll << 16);
	y = ((x * y) >> 16) - (889770ll << 16);
	y = ((x * y) >> 16) + (895650ll << 16);
	y = (588570ll << 16) - ((x * y) >> 16);
	return y < 0 ? 0 : y >> 16;
}

s64 tc90522_get_cn_t(s64 raw)	/* @ .0001 dB */
{
	s64 x, y;
	if (!raw)
		return 0;
	x = (1130911733ll - 10ll * intlog10(raw)) >> 2;
	y = (6ll * x / 25ll) - (16ll << 22);
	y = ((x * y) >> 22) + (398ll << 22);
	y = ((x * y) >> 22) + (5491ll << 22);
	y = ((x * y) >> 22) + (30965ll << 22);
	return y >> 22;
}

int tc90522_read_signal_strength(struct dvb_frontend *fe, u16 *cn)	/* raw C/N */
{
	struct tc90522 *demod = fe->demodulator_priv;
	s64 ret = tc90522_get_cn_raw(demod);
	*cn = ret < 0 ? 0 : ret;
	pr_debug("CN %d (%lld dB)\n", (int)*cn, demod->type == SYS_ISDBS ? (long long int)tc90522_get_cn_s(*cn) : (long long int)tc90522_get_cn_t(*cn));
	return ret < 0 ? ret : 0;
}

int tc90522_read_status(struct dvb_frontend *fe, fe_status_t *status)
{
	struct tc90522 *demod = fe->demodulator_priv;
	struct dtv_frontend_properties *c = &fe->dtv_property_cache;
	s64 ret = tc90522_get_cn_raw(demod),
	    raw = ret < 0 ? 0 : ret;

	switch (demod->state) {
	case TC90522_IDLE:
	case TC90522_SET_FREQUENCY:
		*status = 0;
		break;

	case TC90522_SET_MODULATION:
	case TC90522_ABORT:
		*status |= FE_HAS_SIGNAL;
		break;

	case TC90522_TRACK:
		*status |= FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		break;
	}

	c->cnr.stat[0].svalue = demod->type == SYS_ISDBS ? tc90522_get_cn_s(raw) : tc90522_get_cn_t(raw);
	c->cnr.stat[0].scale = FE_SCALE_DECIBEL;
	return ret < 0 ? ret : 0;
}

/**** ISDB-S ****/
int tc90522_write_id_s(struct dvb_frontend *fe, u16 id)
{
	u8 data[2] = { id >> 8, (u8)id };
	return tc90522_write_data(fe, 0x8f, data, sizeof(data));
}

int tc90522_tune_s(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct tc90522 *demod = fe->demodulator_priv;
	struct tmcc_s tmcc;
	int i, ret,
	    freq = fe->dtv_property_cache.frequency,
	    tsid = fe->dtv_property_cache.stream_id;

	if (re_tune)
		demod->state = TC90522_SET_FREQUENCY;

	switch (demod->state) {
	case TC90522_IDLE:
		*delay = msecs_to_jiffies(3000);
		*status = 0;
		return 0;

	case TC90522_SET_FREQUENCY:
		pr_debug("#%d tsid 0x%x freq %d\n", demod->idx, tsid, freq);
		ret = fe->ops.tuner_ops.set_frequency(fe, freq);
		if (ret)
			return ret;
		demod->offset = 0;
		demod->state = TC90522_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case TC90522_SET_MODULATION:
		for (i = 0; i < 1000; i++) {
			ret = tc90522_read_tmcc_s(demod, &tmcc);
			if (!ret)
				break;
			msleep_interruptible(1);
		}
		if (ret) {
			pr_debug("fail tc_read_tmcc_s ret=0x%x\n", ret);
			demod->state = TC90522_ABORT;
			*delay = msecs_to_jiffies(1000);
			return ret;
		}
		pr_debug("slots=%d,%d,%d,%d mode=%d,%d,%d,%d tmcc.id=0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
				tmcc.slot[0], tmcc.slot[1], tmcc.slot[2], tmcc.slot[3],
				tmcc.mode[0], tmcc.mode[1], tmcc.mode[2], tmcc.mode[3],
				tmcc.id[0], tmcc.id[1], tmcc.id[2], tmcc.id[3],
				tmcc.id[4], tmcc.id[5], tmcc.id[6], tmcc.id[7]);
		for (i = 0; i < ARRAY_SIZE(tmcc.id); i++) {
			pr_debug("tsid %x i %d tmcc.id %x\n", tsid, i, tmcc.id[i]);
			if (tmcc.id[i] == tsid)
				break;
		}
		if (tsid < ARRAY_SIZE(tmcc.id))		/* treat as slot# */
			i = tsid;
		if (i == ARRAY_SIZE(tmcc.id)) {
			pr_debug("#%d i%d tsid 0x%x not found\n", demod->idx, i, tsid);
			return -EINVAL;
		}
		demod->offset = i;
		pr_debug("#%d found tsid 0x%x on slot %d\n", demod->idx, tsid, i);
		ret = tc90522_write_id_s(fe, (u16)tmcc.id[demod->offset]);
		if (ret) {
			pr_debug("fail set_tmcc_s ret=%d\n", ret);
			return ret;
		}
		for (i = 0; i < 1000; i++) {
			u16 short_id;
			ret = tc90522_read_id_s(demod, &short_id);
			if (ret) {
				pr_debug("fail get_id_s ret=%d\n", ret);
				return ret;
			}
			tsid = short_id;
			pr_debug("#%d tsid=0x%x\n", demod->idx, tsid);
			if ((tsid & 0xffff) == tmcc.id[demod->offset])
				break;
			msleep_interruptible(1);
		}
		demod->state = TC90522_TRACK;
		/* fallthrough */

	case TC90522_TRACK:
		*delay = msecs_to_jiffies(3000);
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;

	case TC90522_ABORT:
		*delay = msecs_to_jiffies(3000);
		*status = FE_HAS_SIGNAL;
		return 0;
	}
	return -ERANGE;
}

static struct dvb_frontend_ops tc90522_ops_s = {
	.delsys = { SYS_ISDBS },
	.info = {
		.name = "TC90522 ISDB-S",
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO | FE_CAN_MULTISTREAM |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},
	.init = tc90522_wakeup,
	.sleep = tc90522_sleep,
	.release = tc90522_release,
	.write = tc90522_write,
	.get_frontend_algo = tc90522_get_frontend_algo,
	.read_signal_strength = tc90522_read_signal_strength,
	.read_status = tc90522_read_status,
	.tune = tc90522_tune_s,
};

/**** ISDB-T ****/
int tc90522_get_tmcc_t(struct tc90522 *demod)
{
	u8 buf;
	bool b = false, retryov, fulock;

	while (1) {
		if (tc90522_read(demod, 0x80, &buf, 1))
			return -EBADMSG;
		retryov = buf & 0b10000000 ? true : false;
		fulock  = buf & 0b00001000 ? true : false;
		if (!fulock) {
			b = true;
			break;
		} else {
			if (retryov)
				break;
		}
		msleep_interruptible(1);
	}
	return b ? 0 : -EBADMSG;
}

int tc90522_tune_t(struct dvb_frontend *fe, bool re_tune, unsigned int mode_flags, unsigned int *delay, fe_status_t *status)
{
	struct tc90522 *demod = fe->demodulator_priv;
	int ret, i;

	if (re_tune)
		demod->state = TC90522_SET_FREQUENCY;

	switch (demod->state) {
	case TC90522_IDLE:
		*delay = msecs_to_jiffies(3000);
		*status = 0;
		return 0;

	case TC90522_SET_FREQUENCY:
		if (fe->ops.tuner_ops.set_frequency(fe, fe->dtv_property_cache.frequency)) {
			*delay = msecs_to_jiffies(1000);
			*status = 0;
			return 0;
		}
		demod->state = TC90522_SET_MODULATION;
		*delay = 0;
		*status = FE_HAS_SIGNAL;
		return 0;

	case TC90522_SET_MODULATION:
		for (i = 0; i < 1000; i++) {
			ret = tc90522_get_tmcc_t(demod);
			if (!ret)
				break;
			msleep_interruptible(2);
		}
		if (ret) {
			pr_debug("#%d fail get_tmcc_t ret=%d\n", demod->idx, ret);
				demod->state = TC90522_ABORT;
				*delay = msecs_to_jiffies(1000);
				return 0;
		}
		demod->state = TC90522_TRACK;
		/* fallthrough */

	case TC90522_TRACK:
		*delay = msecs_to_jiffies(3000);
		*status = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
		return 0;

	case TC90522_ABORT:
		*delay = msecs_to_jiffies(3000);
		*status = FE_HAS_SIGNAL;
		return 0;
	}
	return -ERANGE;
}

static struct dvb_frontend_ops tc90522_ops_t = {
	.delsys = { SYS_ISDBT },
	.info = {
		.name = "TC90522 ISDB-T",
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
	},
	.init = tc90522_wakeup,
	.sleep = tc90522_sleep,
	.release = tc90522_release,
	.write = tc90522_write,
	.get_frontend_algo = tc90522_get_frontend_algo,
	.read_signal_strength = tc90522_read_signal_strength,
	.read_status = tc90522_read_status,
	.tune = tc90522_tune_t,
};

/**** Common ****/
struct dvb_frontend *tc90522_attach(struct i2c_adapter *i2c, u8 idx, fe_delivery_system_t type, u8 addr_demod)
{
	struct dvb_frontend *fe;
	struct tc90522 *demod = kzalloc(sizeof(struct tc90522), GFP_KERNEL);
	if (!demod)
		return NULL;

	demod->i2c	= i2c;
	demod->idx	= idx;
	demod->type	= type;
	demod->addr_demod = addr_demod;
	fe = &demod->fe;
	memcpy(&fe->ops, (demod->type == SYS_ISDBS) ? &tc90522_ops_s : &tc90522_ops_t, sizeof(struct dvb_frontend_ops));
	fe->demodulator_priv = demod;
	return fe;
}
EXPORT_SYMBOL(tc90522_attach);

