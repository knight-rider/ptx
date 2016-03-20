/*
	Sharp VA4M6JC2103 QM1D1C004x ISDB-S tuner driver

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>

	CHIP		VER.	CARD
	QM1D1C0042	0x48	Earthsoft PT3
	QM1D1C0045	0x58
	QM1D1C0045_2	0x68	PLEX PX-BCUD

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#include "dvb_frontend.h"
#include "qm1d1c004x.h"

struct qm1d1c004x {
	u8 reg[32];
};

bool qm1d1c004x_r(struct dvb_frontend *fe, u8 slvadr, u8 *dat)
{
	struct i2c_client	*d	= fe->demodulator_priv,
				*t	= fe->tuner_priv;
	u8		buf[]	= {0xFE, t->addr << 1, slvadr, 0xFE, (t->addr << 1) | 1, 0};
	struct i2c_msg	msg[]	= {
		{.addr = d->addr,	.flags = 0,		.buf = buf,	.len = 3,},
		{.addr = d->addr,	.flags = 0,		.buf = buf + 3,	.len = 2,},
		{.addr = d->addr,	.flags = I2C_M_RD,	.buf = buf + 5,	.len = 1,},
	};
	bool	ret = i2c_transfer(d->adapter, msg, 3) == 3;

	*dat = buf[5];
	return ret;
}

int qm1d1c004x_w(struct dvb_frontend *fe, u8 slvadr, u8 *dat, int len)
{
	struct i2c_client	*d	= fe->demodulator_priv;
	u8		buf[len + 1];
	struct i2c_msg	msg[] = {
		{.addr = d->addr,	.flags = 0,	.buf = buf,	.len = len + 1,},
	};

	buf[0] = slvadr;
	memcpy(buf + 1, dat, len);
	return i2c_transfer(d->adapter, msg, 1) == 1 ? 0 : -EIO;
}

int qm1d1c004x_w_tuner(struct dvb_frontend *fe, u8 adr, u8 dat)
{
	struct i2c_client	*t	= fe->tuner_priv;
	struct qm1d1c004x	*q	= i2c_get_clientdata(t);
	u8			buf[]	= {t->addr << 1, adr, dat};
	int			err	= qm1d1c004x_w(fe, 0xFE, buf, 3);

	q->reg[adr] = dat;
	return err;
}

enum qm1d1c004x_agc {
	QM1D1C004X_AGC_AUTO,
	QM1D1C004X_AGC_MANUAL,
};

int qm1d1c004x_set_agc(struct dvb_frontend *fe, enum qm1d1c004x_agc agc)
{
	u8	dat		= (agc == QM1D1C004X_AGC_AUTO) ? 0xff : 0x00,
		pskmsrst	= 0x01;
	int	err		= qm1d1c004x_w(fe, 0x0a, &dat, 1);

	if (err)
		return err;
	dat = 0xb0 | (agc == QM1D1C004X_AGC_AUTO ? 1 : 0);
	err = qm1d1c004x_w(fe, 0x10, &dat, 1);
	if (err)
		return err;
	dat = (agc == QM1D1C004X_AGC_AUTO) ? 0x40 : 0x00;
	return	(err = qm1d1c004x_w(fe, 0x11, &dat, 1)) ?
		err : qm1d1c004x_w(fe, 0x03, &pskmsrst, 1);
}

int qm1d1c004x_sleep(struct dvb_frontend *fe)
{
	u8	buf	= 1,
		*reg	= ((struct qm1d1c004x *)fe->tuner_priv)->reg;

	reg[0x01] &= (~(1 << 3)) & 0xff;
	reg[0x01] |= 1 << 0;
	reg[0x05] |= 1 << 3;
	return	qm1d1c004x_set_agc(fe, QM1D1C004X_AGC_MANUAL)	||
		qm1d1c004x_w_tuner(fe, 0x05, reg[0x05])		||
		qm1d1c004x_w_tuner(fe, 0x01, reg[0x01])		||
		qm1d1c004x_w(fe, 0x17, &buf, 1);
}

int qm1d1c004x_wakeup(struct dvb_frontend *fe)
{
	u8	regs[][32] = {
			{	/* QM1D1C0042	Earthsoft PT3	*/
			0x48, 0x1c, 0xa0, 0x10, 0xbc, 0xc5, 0x20, 0x33,	0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
			0x00, 0xff, 0xf3, 0x00, 0x2a, 0x64, 0xa6, 0x86,	0x8c, 0xcf, 0xb8, 0xf1, 0xa8, 0xf2, 0x89, 0x00,
			}, {	/* QM1D1C0045	untested!	*/
			0x58, 0x1C, 0xC0, 0x10, 0xBC, 0xC1, 0x15, 0x34, 0x06, 0x3e, 0x00, 0x00, 0x43, 0x00, 0x00, 0x00,
			0x11, 0xFF, 0xF3, 0x00, 0x3E, 0x25, 0x5C, 0xD6, 0x55, 0x8F, 0x95, 0xF6, 0x36, 0xF2, 0x09, 0x00,
			}, {	/* QM1D1C0045_2	PLEX PX-BCUD	*/
			0x68, 0x1c, 0xc0, 0x10, 0xbc, 0xc1, 0x11, 0x33,	0x03, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
			0x00, 0xff, 0xf3, 0x00, 0x3f, 0x25, 0x5c, 0xd6,	0x55, 0xcf, 0x95, 0xf6, 0x36, 0xf2, 0x09, 0x00,
			}
		},
		*reg	= ((struct qm1d1c004x *)i2c_get_clientdata(fe->tuner_priv))->reg,
		dat	= 0,
		i;

	for (i = 0; i < ARRAY_SIZE(regs); i++) {
		if (!qm1d1c004x_r(fe, 0, &dat))
			return -EIO;
		if (dat == regs[i][0])
			break;
	}
	if (i == ARRAY_SIZE(regs))
		return -ENOTSUPP;
	memcpy(reg, regs[i], 32);
	reg[0x01] |= 1 << 3;
	reg[0x01] &= (~(1 << 0)) & 0xff;
	reg[0x05] &= (~(1 << 3)) & 0xff;
	dat = 0;
	return	qm1d1c004x_w(fe, 0x17, &dat, 1)		||
		qm1d1c004x_w_tuner(fe, 0x01, reg[0x01])	||
		qm1d1c004x_w_tuner(fe, 0x05, reg[0x05]);
}

int qm1d1c004x_tune(struct dvb_frontend *fe)
{
	u32	fgap_tab[9][3]	= {
		{2151000, 1, 7},	{1950000, 1, 6},	{1800000, 1, 5},
		{1600000, 1, 4},	{1450000, 1, 3},	{1250000, 1, 2},
		{1200000, 0, 7},	{ 975000, 0, 6},	{ 950000, 0, 0}
	};
	u8	*reg	= ((struct qm1d1c004x *)fe->tuner_priv)->reg;
	u32	kHz	= fe->dtv_property_cache.frequency - 500,
		XtalkHz	= 16000,
		i	= ((kHz + XtalkHz / 2) / XtalkHz) * XtalkHz;
	s64	b	= kHz - i;
	u8	N	= i / (4 * XtalkHz) - 3,
		A	= (i / XtalkHz) - 4 * (N + 1) - 5;
	int	sd	= b < 0 ? (0x100000 / XtalkHz) * b + 0x400000 : (0x100000 / XtalkHz) * b,
		err	= qm1d1c004x_set_agc(fe, QM1D1C004X_AGC_MANUAL);

	if (err)
		return -EIO;

	/* div2/vco_band */
	for (i = 0; i < 8; i++)
		if ((fgap_tab[i+1][0] <= kHz) && (kHz < fgap_tab[i][0]))
			qm1d1c004x_w_tuner(fe, 0x02, (reg[0x02] & 0x0f) | fgap_tab[i][1] << 7 | fgap_tab[i][2] << 4);

	reg[0x06] &= 0x40;
	reg[0x06] |= N;
	reg[0x07] &= 0xf0;
	reg[0x07] |= A & 0x0f;

	/* LPF */
	reg[0x08] &= 0xf0;
	reg[0x08] |= 0x09;
	reg[0x13] &= 0x9f;
	reg[0x13] |= 0x20;
	err =	qm1d1c004x_w_tuner(fe, 0x06, reg[0x06])	||
		qm1d1c004x_w_tuner(fe, 0x07, reg[0x07])	||
		qm1d1c004x_w_tuner(fe, 0x08, (reg[0x08] & 0xf0) | 2);
	if (err)
		return err;
	reg[0x09] &= 0xc0;
	reg[0x09] |= (sd >> 16) & 0x3f;
	reg[0x0a] = (sd >> 8) & 0xff;
	reg[0x0b] = (sd >> 0) & 0xff;
	err =	qm1d1c004x_w_tuner(fe, 0x09, reg[0x09])	||
		qm1d1c004x_w_tuner(fe, 0x0a, reg[0x0a])	||
		qm1d1c004x_w_tuner(fe, 0x0b, reg[0x0b])	||
		qm1d1c004x_w_tuner(fe, 0x0c, reg[0x0c] & 0x3f);
	if (err)
		return err;
	msleep_interruptible(1);
	err =	qm1d1c004x_w_tuner(fe, 0x0c, reg[0x0c] | 0xc0)	||
		qm1d1c004x_w_tuner(fe, 0x08, 0x09)		||
		qm1d1c004x_w_tuner(fe, 0x13, reg[0x13]);
	if (err)
		return err;
	for (i = 0; i < 500; i++) {
		if (!qm1d1c004x_r(fe, 0x0d, &reg[0x0d]))
			return -EIO;
		if (reg[0x0d] & 0x40)	/* locked */
			return qm1d1c004x_set_agc(fe, QM1D1C004X_AGC_AUTO);
		msleep_interruptible(1);
	}
	return -ETIMEDOUT;
}

int qm1d1c004x_remove(struct i2c_client *t)
{
	kfree(i2c_get_clientdata(t));
	return 0;
}

int qm1d1c004x_probe(struct i2c_client *t, const struct i2c_device_id *id)
{
	struct dvb_frontend	*fe	= t->dev.platform_data;
	struct qm1d1c004x	*q	= kzalloc(sizeof(struct qm1d1c004x), GFP_KERNEL);
	u8			d[]	= {0x10, 0x15, 0x04};

	if (!q)
		return -ENOMEM;
	i2c_set_clientdata(t, q);
	fe->ops.tuner_ops.set_params	= qm1d1c004x_tune;
	fe->ops.tuner_ops.sleep		= qm1d1c004x_sleep;
	fe->ops.tuner_ops.init		= qm1d1c004x_wakeup;
	return	qm1d1c004x_w(fe, 0x1e, d,   1)	||
		qm1d1c004x_w(fe, 0x1c, d+1, 1)	||
		qm1d1c004x_w(fe, 0x1f, d+2, 1);
}

static struct i2c_device_id qm1d1c004x_id[] = {
	{QM1D1C004X_MODNAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, qm1d1c004x_id);

static struct i2c_driver qm1d1c004x_driver = {
	.driver.name	= qm1d1c004x_id->name,
	.probe		= qm1d1c004x_probe,
	.remove		= qm1d1c004x_remove,
	.id_table	= qm1d1c004x_id,
};
module_i2c_driver(qm1d1c004x_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 QM1D1C004X ISDB-S tuner driver");
MODULE_LICENSE("GPL");

