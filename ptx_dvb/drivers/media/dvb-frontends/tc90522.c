/*
 * Toshiba TC90522XBG 2ch OFDM(ISDB-T) + 2ch 8PSK(ISDB-S) demodulator
 *
 * Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
 *
 * Supported cards:
 *	Earthsoft PT3
 *	PLEX PX-Q3PE
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pci.h>
#include "dvb_math.h"
#include "dvb_frontend.h"
#include "tc90522.h"

struct tc90522 {
	struct i2c_adapter *i2c;
	enum fe_status stat;
};

bool tc90522_r(struct dvb_frontend *fe, u8 slvadr, u8 *buf, u8 len)
{
	struct tc90522 *d = fe->demodulator_priv;
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,		.buf = &slvadr,	.len = 1,},
		{.addr = fe->id,	.flags = I2C_M_RD,	.buf = buf,	.len = len,},
	};
	return i2c_transfer(d->i2c, msg, 2) == 2;
}

bool tc90522_w(struct dvb_frontend *fe, u8 slvadr, u8 dat)
{
	struct tc90522 *d = fe->demodulator_priv;
	u8 buf[] = {slvadr, dat};
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = 2,},
	};
	return i2c_transfer(d->i2c, msg, 1) == 1;
}

u64 tc90522_n2int(const u8 *data, u8 n)		/* convert n_bytes data from stream (network byte order) to integer */
{						/* can't use <arpa/inet.h>'s ntoh*() as sometimes n = 3,5,...       */
	u32 i, val = 0;

	for (i = 0; i < n; i++) {
		val <<= 8;
		val |= data[i];
	}
	return val;
}

int tc90522_cn_raw(struct dvb_frontend *fe, u16 *raw)	/* for DVBv3 compatibility	*/
{
	u8	buf[3],
		len	= fe->dtv_property_cache.delivery_system == SYS_ISDBS ? 2 : 3,
		adr	= fe->dtv_property_cache.delivery_system == SYS_ISDBS ? 0xbc : 0x8b;
	bool	ok	= tc90522_r(fe, adr, buf, len);
	int	cn	= tc90522_n2int(buf, len);

	if (!ok)
		return -EIO;
	*raw = cn;
	return cn;
}

int tc90522_status(struct dvb_frontend *fe, enum fe_status *stat)
{
	struct tc90522			*d = fe->demodulator_priv;
	struct dtv_frontend_properties	*c = &fe->dtv_property_cache;
	u16	v16;
	s64	raw	= tc90522_cn_raw(fe, &v16),
		x,
		y;

	s64 cn_s(void)	/* @ .0001 dB */
	{
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

	s64 cn_t(void)	/* @ .0001 dB */
	{
		if (!raw)
			return 0;
		x = (1130911733ll - 10ll * intlog10(raw)) >> 2;
		y = (x >> 2) - (x >> 6) + (x >> 8) + (x >> 9) - (x >> 10) + (x >> 11) + (x >> 12) - (16ll << 22);
		y = ((x * y) >> 22) + (398ll << 22);
		y = ((x * y) >> 22) + (5491ll << 22);
		y = ((x * y) >> 22) + (30965ll << 22);
		return y >> 22;
	}

	c->cnr.len		= 1;
	c->cnr.stat[0].svalue	= fe->dtv_property_cache.delivery_system == SYS_ISDBS ? cn_s() : cn_t();
	c->cnr.stat[0].scale	= FE_SCALE_DECIBEL;
	*stat = d->stat;
	return d->stat;
}

int tc90522_get_frontend_algo(struct dvb_frontend *fe)
{
	return DVBFE_ALGO_HW;
}

int tc90522_tune(struct dvb_frontend *fe, bool retune, u32 mode_flags, u32 *delay, enum fe_status *stat)
{
	u32 fno2kHz(u32 fno)
	{
		u32 chno;

		if (fno < 12) {
			chno = 1 + 2 * fno;		/* BS		*/
			return 1049480 + 38360 * fno;
		} else if (fno < 24) {
			fno -= 12;
			chno = 2 + 2 * fno;		/* CS110 right	*/
			return 1613000 + 40000 * fno;
		}
		fno -= 24;
		chno = 1 + 2 * fno;			/* CS110 left	*/
		return 1593000 + 40000 * fno;
	}

	void s_kHz(u32 *f)
	{
		*f =	*f > 3000000 ? fno2kHz(14)	:	/* CNN etc.	*/
			*f >= 1049480 ? *f		:
			*f > 48 ? fno2kHz(4)		:	/* BS11 etc.	*/
			fno2kHz(*f - 1);
	}

	u32 fno2Hz(u32 fno)
	{
		return	(fno > 112 ? 557 : 93 + 6 * fno + (fno < 12 ? 0 : fno < 17 ? 2 : fno < 63 ? 0 : 2)) * 1000000 + 142857;
	}

	void t_Hz(u32 *f)
	{
		*f =	*f >= 90000000	? *f			:	/* real_freq Hz	*/
			*f > 255	? fno2Hz(77)		:	/* NHK		*/
			*f > 127	? fno2Hz(*f - 128)	:	/* freqno (IO#)	*/
			*f > 63	? (*f -= 64,				/* CATV		*/
				*f > 22	? fno2Hz(*f - 1)	:	/* C23-C62	*/
				*f > 12	? fno2Hz(*f - 10)	:	/* C13-C22	*/
				fno2Hz(77))			:
			*f > 62	? fno2Hz(77)			:
			*f > 12	? fno2Hz(*f + 50)		:	/* 13-62	*/
			*f > 3	? fno2Hz(*f +  9)		:	/*  4-12	*/
			*f		? fno2Hz(*f -  1)	:	/*  1-3		*/
			fno2Hz(77);
	}
	struct tc90522	*d		= fe->demodulator_priv;
	u16		stream_id	= fe->dtv_property_cache.stream_id,
			i		= 0xFFFF;
	u8		data[16];

	if (!retune)		/* once is enough */
		return 0;
	d->stat = 0;
	if (fe->dtv_property_cache.delivery_system == SYS_ISDBT)
		goto ISDBT;

	s_kHz(&fe->dtv_property_cache.frequency);
	if (fe->ops.tuner_ops.set_params(fe))
		return -EIO;
	while (i--) {
		if	((tc90522_r(fe, 0xC3, data, 1), !(data[0] & 0x10))	&&	/* locked	*/
			(tc90522_r(fe, 0xCE, data, 2), *(u16 *)data != 0)	&&	/* valid TSID	*/
			tc90522_r(fe, 0xC3, data, 1)				&&
			tc90522_r(fe, 0xCE, data, 16))
			break;
		msleep_interruptible(1);
	}
	if (!i)
		return -ETIMEDOUT;
	for (i = 0; i < 8; i++) {
		u16 tsid = tc90522_n2int(data + i*2, 2);

		if ((tsid == stream_id || stream_id == i)	&&
			tc90522_w(fe, 0x8F, tsid >> 8)		&&
			tc90522_w(fe, 0x90, tsid & 0xFF)	&&
			tc90522_r(fe, 0xE6, data, 2)		&&
			tc90522_n2int(data, 2) == tsid)
			goto LOCK;
	}
	goto ERR;
ISDBT:
	t_Hz(&fe->dtv_property_cache.frequency);
	if (fe->ops.tuner_ops.set_params(fe))
		return -EIO;
	while (i--) {
		bool	retryov,
			lock0,
			lock1;
		if (!tc90522_r(fe, 0x80, data, 1) || !tc90522_r(fe, 0xB0, data + 1, 1))
			break;
		retryov	= data[0] & 0b10000000 ? true : false;
		lock0	= data[0] & 0b00001000 ? false : true;
		lock1	= data[1] & 0b00001000 ? true : false;
		if (lock0 && lock1) {
LOCK:
			d->stat = FE_HAS_SIGNAL | FE_HAS_CARRIER | FE_HAS_LOCK;
			*stat = d->stat;
			return 0;
		}
		if (retryov)
			break;
		msleep_interruptible(1);
	}
ERR:
	*stat = d->stat;
	return -ETIMEDOUT;
}

static struct dvb_frontend_ops tc90522_ops = {
	.info = {
		.name = TC90522_MODNAME,
		.caps = FE_CAN_INVERSION_AUTO | FE_CAN_FEC_AUTO | FE_CAN_QAM_AUTO | FE_CAN_MULTISTREAM |
			FE_CAN_TRANSMISSION_MODE_AUTO | FE_CAN_GUARD_INTERVAL_AUTO | FE_CAN_HIERARCHY_AUTO,
		.frequency_min	= 1,
		.frequency_max	= 770000000,
	},
	.get_frontend_algo = tc90522_get_frontend_algo,
	.read_snr	= tc90522_cn_raw,
	.read_status	= tc90522_status,
	.tune		= tc90522_tune,
};

int tc90522_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	struct tc90522		*d	= kzalloc(sizeof(struct tc90522), GFP_KERNEL);
	struct dvb_frontend	*fe	= c->dev.platform_data;

	if (!d)
		return -ENOMEM;
	d->i2c			= c->adapter;
	fe->demodulator_priv	= d;
	i2c_set_clientdata(c, d);
	memcpy(&fe->ops, &tc90522_ops, sizeof(struct dvb_frontend_ops));
	fe->ops.delsys[0] = fe->dtv_property_cache.delivery_system;
	return 0;
}

int tc90522_remove(struct i2c_client *c)
{
	kfree(i2c_get_clientdata(c));
	return 0;
}

static struct i2c_device_id tc90522_id[] = {
	{TC90522_MODNAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, tc90522_id);

static struct i2c_driver tc90522_driver = {
	.driver.name	= tc90522_id->name,
	.probe		= tc90522_probe,
	.remove		= tc90522_remove,
	.id_table	= tc90522_id,
};
module_i2c_driver(tc90522_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Toshiba TC90522 8PSK(ISDB-S)/OFDM(ISDB-T) quad demodulator");
MODULE_LICENSE("GPL");

