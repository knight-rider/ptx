/*
	Sharp VA4M6JC2103 - Earthsoft PT3 ISDB-T tuner MaxLinear CMOS Hybrid TV MxL301RF

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#include "dvb_frontend.h"
#include "mxl301rf.h"

int mxl301rf_w(struct dvb_frontend *fe, u8 slvadr, const u8 *dat, int len)
{
	struct i2c_client	*d	= fe->demodulator_priv;
	u8			buf[len + 1];
	struct i2c_msg		msg[] = {
		{.addr = d->addr,	.flags = 0,	.buf = buf,	.len = len + 1,},
	};

	buf[0] = slvadr;
	memcpy(buf + 1, dat, len);
	return i2c_transfer(d->adapter, msg, 1) == 1 ? 0 : -EIO;
}

int mxl301rf_w_tuner(struct dvb_frontend *fe, const u8 *dat, int len)
{
	u8 buf[len + 1];

	buf[0] = ((struct i2c_client *)fe->tuner_priv)->addr << 1;
	memcpy(buf + 1, dat, len);
	return mxl301rf_w(fe, 0xFE, buf, len + 1);
}

u8 mxl301rf_r(struct dvb_frontend *fe, u8 regadr)
{
	struct i2c_client	*d	= fe->demodulator_priv,
				*t	= fe->tuner_priv;
	u8	wbuf[]	= {0xFB, regadr},
		rbuf[]	= {0xFE, (t->addr << 1) | 1, 0};
	struct i2c_msg msg[] = {
		{.addr	= d->addr,	.flags	= 0,		.buf	= rbuf,		.len	= 2,},
		{.addr	= d->addr,	.flags	= I2C_M_RD,	.buf	= rbuf + 2,	.len	= 1,},
	};
	mxl301rf_w_tuner(fe, wbuf, sizeof(wbuf));
	return t->addr && (i2c_transfer(d->adapter, msg, 2) == 2) ? rbuf[2] : 0;
}

enum mxl301rf_agc {
	MXL301RF_AGC_AUTO,
	MXL301RF_AGC_MANUAL,
};

int mxl301rf_set_agc(struct dvb_frontend *fe, enum mxl301rf_agc agc)
{
	u8	dat	= agc == MXL301RF_AGC_AUTO ? 0x40 : 0x00,
		imsrst	= 0x01 << 6;
	int	err	= mxl301rf_w(fe, 0x25, &dat, 1);

	dat = 0x4c | (agc == MXL301RF_AGC_AUTO ? 0 : 1);
	return	err				||
		mxl301rf_w(fe, 0x23, &dat, 1)	||
		mxl301rf_w(fe, 0x01, &imsrst, 1);
}

int mxl301rf_sleep(struct dvb_frontend *fe)
{
	u8	buf	= (1 << 7) | (1 << 4),
		dat[]	= {0x01, 0x00, 0x13, 0x00};
	int	err	= mxl301rf_set_agc(fe, MXL301RF_AGC_MANUAL);

	if (err)
		return err;
	mxl301rf_w_tuner(fe, dat, sizeof(dat));
	return mxl301rf_w(fe, 0x03, &buf, 1);
}

int mxl301rf_tune(struct dvb_frontend *fe)
{
	struct shf_dvbt {
		u32	freq,		/* Channel center frequency @ kHz	*/
			freq_th;	/* Offset frequency threshold @ kHz	*/
		u8	shf_val,	/* Spur shift value			*/
			shf_dir;	/* Spur shift direction			*/
	} shf_dvbt_tab[] = {
		{ 64500, 500, 0x92, 0x07},
		{191500, 300, 0xe2, 0x07},
		{205500, 500, 0x2c, 0x04},
		{212500, 500, 0x1e, 0x04},
		{226500, 500, 0xd4, 0x07},
		{ 99143, 500, 0x9c, 0x07},
		{173143, 500, 0xd4, 0x07},
		{191143, 300, 0xd4, 0x07},
		{207143, 500, 0xce, 0x07},
		{225143, 500, 0xce, 0x07},
		{243143, 500, 0xd4, 0x07},
		{261143, 500, 0xd4, 0x07},
		{291143, 500, 0xd4, 0x07},
		{339143, 500, 0x2c, 0x04},
		{117143, 500, 0x7a, 0x07},
		{135143, 300, 0x7a, 0x07},
		{153143, 500, 0x01, 0x07}
	};
	u8 rf_dat[] = {
		0x13, 0x00,	/* abort tune		*/
		0x3b, 0xc0,
		0x3b, 0x80,
		0x10, 0x95,	/* BW			*/
		0x1a, 0x05,
		0x61, 0x00,
		0x62, 0xa0,
		0x11, 0x40,	/* 2 bytes to store RF	*/
		0x12, 0x0e,	/* 2 bytes to store RF	*/
		0x13, 0x01	/* start tune		*/
	};
	const u8 idac[] = {
		0x0d, 0x00,
		0x0c, 0x67,
		0x6f, 0x89,
		0x70, 0x0c,
		0x6f, 0x8a,
		0x70, 0x0e,
		0x6f, 0x8b,
		0x70, 0x10+12,
	};
	u8	dat[20];
	int	err	= mxl301rf_set_agc(fe, MXL301RF_AGC_MANUAL);
	u32	freq	= fe->dtv_property_cache.frequency,
		kHz	= 1000,
		MHz	= 1000000,
		dig_rf	= freq / MHz,
		tmp	= freq % MHz,
		i,
		fdiv	= 1000000;
	unsigned long timeout;

	if (err)
		return err;
	for (i = 0; i < 6; i++) {
		dig_rf <<= 1;
		fdiv /= 2;
		if (tmp > fdiv) {
			tmp -= fdiv;
			dig_rf++;
		}
	}
	if (tmp > 7812)
		dig_rf++;
	rf_dat[2 * 7 + 1]	= (u8)(dig_rf);
	rf_dat[2 * 8 + 1]	= (u8)(dig_rf >> 8);
	for (i = 0; i < ARRAY_SIZE(shf_dvbt_tab); i++) {
		if ((freq >= (shf_dvbt_tab[i].freq - shf_dvbt_tab[i].freq_th) * kHz) &&
				(freq <= (shf_dvbt_tab[i].freq + shf_dvbt_tab[i].freq_th) * kHz)) {
			rf_dat[2 * 5 + 1] = shf_dvbt_tab[i].shf_val;
			rf_dat[2 * 6 + 1] = 0xa0 | shf_dvbt_tab[i].shf_dir;
			break;
		}
	}
	memcpy(dat, rf_dat, sizeof(rf_dat));

	mxl301rf_w_tuner(fe, dat, 14);
	msleep_interruptible(1);
	mxl301rf_w_tuner(fe, dat + 14, 6);
	msleep_interruptible(1);
	dat[0] = 0x1a;
	dat[1] = 0x0d;
	mxl301rf_w_tuner(fe, dat, 2);
	mxl301rf_w_tuner(fe, idac, sizeof(idac));
	timeout = jiffies + msecs_to_jiffies(100);
	while (time_before(jiffies, timeout)) {
		if ((mxl301rf_r(fe, 0x16) & 0x0c) == 0x0c && (mxl301rf_r(fe, 0x16) & 0x03) == 0x03)
			return mxl301rf_set_agc(fe, MXL301RF_AGC_AUTO);
		msleep_interruptible(1);
	}
	return -ETIMEDOUT;
}

int mxl301rf_wakeup(struct dvb_frontend *fe)
{
	u8	buf	= (1 << 7) | (0 << 4),
		dat[2]	= {0x01, 0x01};
	int	err	= mxl301rf_w(fe, 0x03, &buf, 1);

	if (err)
		return err;
	mxl301rf_w_tuner(fe, dat, sizeof(dat));
	return 0;
}

int mxl301rf_probe(struct i2c_client *t, const struct i2c_device_id *id)
{
	struct dvb_frontend	*fe	= t->dev.platform_data;
	u8			d[]	= {0x10, 0x01};

	fe->ops.tuner_ops.set_params	= mxl301rf_tune;
	fe->ops.tuner_ops.sleep		= mxl301rf_sleep;
	fe->ops.tuner_ops.init		= mxl301rf_wakeup;
	return	mxl301rf_w(fe, 0x1c, d, 1)	||
		mxl301rf_w(fe, 0x1d, d+1, 1);
}

static struct i2c_device_id mxl301rf_id[] = {
	{MXL301RF_MODNAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, mxl301rf_id);

static struct i2c_driver mxl301rf_driver = {
	.driver.name	= mxl301rf_id->name,
	.probe		= mxl301rf_probe,
	.id_table	= mxl301rf_id,
};
module_i2c_driver(mxl301rf_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 MxL301RF MaxLinear CMOS Hybrid TV ISDB-T tuner driver");
MODULE_LICENSE("GPL");

