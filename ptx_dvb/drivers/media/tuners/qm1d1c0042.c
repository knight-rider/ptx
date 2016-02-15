/*
 * Sharp VA4M6JC2103 - Earthsoft PT3 ISDB-S tuner driver QM1D1C0042
 *
 * Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "dvb_frontend.h"
#include "qm1d1c0042.h"

struct qm1d1c0042 {
	struct i2c_adapter *i2c;
	u8 adr_tuner, reg[32];
};

bool qm1d1c0042_r(struct dvb_frontend *fe, u8 slvadr, u8 *dat)
{
	struct qm1d1c0042 *t = fe->tuner_priv;
	u8		buf[]	= {0xFE, t->adr_tuner << 1, slvadr, 0xFE, (t->adr_tuner << 1) | 1, 0};
	struct i2c_msg	msg[]	= {
		{.addr = fe->id,	.flags = 0,		.buf = buf,	.len = 3,},
		{.addr = fe->id,	.flags = 0,		.buf = buf + 3,	.len = 2,},
		{.addr = fe->id,	.flags = I2C_M_RD,	.buf = buf + 5,	.len = 1,},
	};
	bool	ret = i2c_transfer(t->i2c, msg, 3) == 3;

	*dat = buf[5];
	return ret;
}

int qm1d1c0042_w(struct dvb_frontend *fe, u8 slvadr, u8 *dat, int len)
{
	u8 buf[len + 1];
	struct qm1d1c0042 *t = fe->tuner_priv;
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = len + 1,},
	};

	buf[0] = slvadr;
	memcpy(buf + 1, dat, len);
	return i2c_transfer(t->i2c, msg, 1) == 1 ? 0 : -EIO;
}

int qm1d1c0042_w_tuner(struct dvb_frontend *fe, u8 adr, u8 dat)
{
	struct qm1d1c0042 *t = fe->tuner_priv;
	u8 buf[] = {t->adr_tuner << 1, adr, dat};
	int err = qm1d1c0042_w(fe, 0xFE, buf, 3);

	t->reg[adr] = dat;
	return err;
}

enum qm1d1c0042_agc {
	QM1D1C0042_AGC_AUTO,
	QM1D1C0042_AGC_MANUAL,
};

int qm1d1c0042_set_agc(struct dvb_frontend *fe, enum qm1d1c0042_agc agc)
{
	u8	dat		= (agc == QM1D1C0042_AGC_AUTO) ? 0xff : 0x00,
		pskmsrst	= 0x01;
	int	err		= qm1d1c0042_w(fe, 0x0a, &dat, 1);

	if (err)
		return err;
	dat = 0xb0 | (agc == QM1D1C0042_AGC_AUTO ? 1 : 0);
	err = qm1d1c0042_w(fe, 0x10, &dat, 1);
	if (err)
		return err;
	dat = (agc == QM1D1C0042_AGC_AUTO) ? 0x40 : 0x00;
	return	(err = qm1d1c0042_w(fe, 0x11, &dat, 1)) ?
		err : qm1d1c0042_w(fe, 0x03, &pskmsrst, 1);
}

int qm1d1c0042_sleep(struct dvb_frontend *fe)
{
	struct qm1d1c0042 *t = fe->tuner_priv;
	u8 buf = 1;

	t->reg[0x01] &= (~(1 << 3)) & 0xff;
	t->reg[0x01] |= 1 << 0;
	t->reg[0x05] |= 1 << 3;
	return	qm1d1c0042_set_agc(fe, QM1D1C0042_AGC_MANUAL)	||
		qm1d1c0042_w_tuner(fe, 0x05, t->reg[0x05])	||
		qm1d1c0042_w_tuner(fe, 0x01, t->reg[0x01])	||
		qm1d1c0042_w(fe, 0x17, &buf, 1);
}

int qm1d1c0042_wakeup(struct dvb_frontend *fe)
{
	struct qm1d1c0042 *t = fe->tuner_priv;
	u8 buf = 0;

	t->reg[0x01] |= 1 << 3;
	t->reg[0x01] &= (~(1 << 0)) & 0xff;
	t->reg[0x05] &= (~(1 << 3)) & 0xff;
	return	qm1d1c0042_w(fe, 0x17, &buf, 1)			||
		qm1d1c0042_w_tuner(fe, 0x01, t->reg[0x01])	||
		qm1d1c0042_w_tuner(fe, 0x05, t->reg[0x05]);
}

int qm1d1c0042_tune(struct dvb_frontend *fe)
{
	u32 freq2ch(u32 kHz)
	{
		u32 freq = kHz / 10,
		    ch0 = (freq - 104948) / 3836, diff0 = freq - (104948 + 3836 * ch0),
		    ch1 = (freq - 161300) / 4000, diff1 = freq - (161300 + 4000 * ch1),
		    ch2 = (freq - 159300) / 4000, diff2 = freq - (159300 + 4000 * ch2),
		    min = diff0 < diff1 ? diff0 : diff1;

		if (diff2 < min)
			return ch2 + 24;
		if (min == diff1)
			return ch1 + 12;
		return ch0;
	}

	struct qm1d1c0042	*t	= fe->tuner_priv;
	u32	fgap_tab[9][3]	= {
		{2151000, 1, 7},	{1950000, 1, 6},	{1800000, 1, 5},
		{1600000, 1, 4},	{1450000, 1, 3},	{1250000, 1, 2},
		{1200000, 0, 7},	{ 975000, 0, 6},	{ 950000, 0, 0}
	};
	u32	sd_tab[24][3]	= {
		{0x38fae1, 0x0d, 0x5},	{0x3f570a, 0x0e, 0x3},	{0x05b333, 0x0e, 0x5},	{0x3c0f5c, 0x0f, 0x4},
		{0x026b85, 0x0f, 0x6},	{0x38c7ae, 0x10, 0x5},	{0x3f23d7, 0x11, 0x3},	{0x058000, 0x11, 0x5},
		{0x3bdc28, 0x12, 0x4},	{0x023851, 0x12, 0x6},	{0x38947a, 0x13, 0x5},	{0x3ef0a3, 0x14, 0x3},
		{0x3c8000, 0x16, 0x4},	{0x048000, 0x16, 0x6},	{0x3c8000, 0x17, 0x5},	{0x048000, 0x18, 0x3},
		{0x3c8000, 0x18, 0x6},	{0x048000, 0x19, 0x4},	{0x3c8000, 0x1a, 0x3},	{0x048000, 0x1a, 0x5},
		{0x3c8000, 0x1b, 0x4},	{0x048000, 0x1b, 0x6},	{0x3c8000, 0x1c, 0x5},	{0x048000, 0x1d, 0x3},
	};
	int	err	= qm1d1c0042_set_agc(fe, QM1D1C0042_AGC_MANUAL);
	u32	kHz	= fe->dtv_property_cache.frequency,
		ch	= freq2ch(kHz),
		N	= sd_tab[ch][1],
		A	= sd_tab[ch][2],
		sd	= 0,
		i;

	if (err || ch >= 24)
		return -EIO;
	t->reg[0x08] &= 0xf0;
	t->reg[0x08] |= 0x09;
	t->reg[0x13] &= 0x9f;
	t->reg[0x13] |= 0x20;
	for (i = 0; i < 8; i++)
		if ((fgap_tab[i+1][0] <= kHz - 500) && (kHz - 500 < fgap_tab[i][0]))
			qm1d1c0042_w_tuner(fe, 0x02, (t->reg[0x02] & 0x0f) | fgap_tab[i][1] << 7 | fgap_tab[i][2] << 4);
	sd = sd_tab[ch][0];
	t->reg[0x06] &= 0x40;
	t->reg[0x06] |= N;
	t->reg[0x07] &= 0xf0;
	t->reg[0x07] |= A & 0x0f;
	err =	qm1d1c0042_w_tuner(fe, 0x06, t->reg[0x06])	||
		qm1d1c0042_w_tuner(fe, 0x07, t->reg[0x07])	||
		qm1d1c0042_w_tuner(fe, 0x08, (t->reg[0x08] & 0xf0) | 2);
	if (err)
		return err;
	t->reg[0x09] &= 0xc0;
	t->reg[0x09] |= (sd >> 16) & 0x3f;
	t->reg[0x0a] = (sd >> 8) & 0xff;
	t->reg[0x0b] = (sd >> 0) & 0xff;
	err =	qm1d1c0042_w_tuner(fe, 0x09, t->reg[0x09])	||
		qm1d1c0042_w_tuner(fe, 0x0a, t->reg[0x0a])	||
		qm1d1c0042_w_tuner(fe, 0x0b, t->reg[0x0b])	||
		qm1d1c0042_w_tuner(fe, 0x0c, t->reg[0x0c] & 0x3f);
	if (err)
		return err;
	msleep_interruptible(1);
	err =	qm1d1c0042_w_tuner(fe, 0x0c, t->reg[0x0c] | 0xc0)	||
		qm1d1c0042_w_tuner(fe, 0x08, 0x09)			||
		qm1d1c0042_w_tuner(fe, 0x13, t->reg[0x13]);
	if (err)
		return err;
	for (i = 0; i < 500; i++) {
		if (!qm1d1c0042_r(fe, 0x0d, &t->reg[0x0d]))
			return -EIO;
		if (t->reg[0x0d] & 0x40)	/* locked */
			return qm1d1c0042_set_agc(fe, QM1D1C0042_AGC_AUTO);
		msleep_interruptible(1);
	}
	return -ETIMEDOUT;
}

static struct dvb_tuner_ops qm1d1c0042_ops = {
	.set_params	= qm1d1c0042_tune,
	.sleep		= qm1d1c0042_sleep,
	.init		= qm1d1c0042_wakeup,
};

int qm1d1c0042_remove(struct i2c_client *c)
{
	kfree(i2c_get_clientdata(c));
	return 0;
}

int qm1d1c0042_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	u8 reg_rw[] = {
		0x48, 0x1c, 0xa0, 0x10, 0xbc, 0xc5, 0x20, 0x33,	0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
		0x00, 0xff, 0xf3, 0x00, 0x2a, 0x64, 0xa6, 0x86,	0x8c, 0xcf, 0xb8, 0xf1, 0xa8, 0xf2, 0x89, 0x00,
	};
	u8	d[] = {0x10, 0x15, 0x04};
	struct dvb_frontend	*fe	= c->dev.platform_data;
	struct qm1d1c0042	*t	= kzalloc(sizeof(struct qm1d1c0042), GFP_KERNEL);

	if (!t)
		return -ENOMEM;
	fe->tuner_priv	= t;
	t->i2c		= c->adapter;
	t->adr_tuner	= c->addr;
	memcpy(&fe->ops.tuner_ops, &qm1d1c0042_ops, sizeof(struct dvb_tuner_ops));
	memcpy(t->reg, reg_rw, sizeof(reg_rw));
	i2c_set_clientdata(c, t);
	return	qm1d1c0042_w(fe, 0x1e, d,   1)	||
		qm1d1c0042_w(fe, 0x1c, d+1, 1)	||
		qm1d1c0042_w(fe, 0x1f, d+2, 1);
}

static struct i2c_device_id qm1d1c0042_id[] = {
	{QM1D1C0042_MODNAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, qm1d1c0042_id);

static struct i2c_driver qm1d1c0042_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= qm1d1c0042_id->name,
	},
	.probe		= qm1d1c0042_probe,
	.remove		= qm1d1c0042_remove,
	.id_table	= qm1d1c0042_id,
};
module_i2c_driver(qm1d1c0042_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 QM1D1C0042 ISDB-S tuner driver");
MODULE_LICENSE("GPL");

