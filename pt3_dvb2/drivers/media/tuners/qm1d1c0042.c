/*
 * Sharp VA4M6JC2103 - Earthsoft PT3 ISDB-S tuner driver QM1D1C0042
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

#include "qm1d1c0042.h"

struct qm1d1c0042 {
	struct dvb_frontend *fe;
	u8 addr_tuner, idx, reg[32];
	u32 freq;
	int (*read)(struct dvb_frontend *fe, u8 *buf, int buflen);
};

static const u8 qm1d1c0042_reg_rw[] = {
	0x48, 0x1c, 0xa0, 0x10, 0xbc, 0xc5, 0x20, 0x33,
	0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x00, 0xff, 0xf3, 0x00, 0x2a, 0x64, 0xa6, 0x86,
	0x8c, 0xcf, 0xb8, 0xf1, 0xa8, 0xf2, 0x89, 0x00,
};

/* read via demodulator */
int qm1d1c0042_fe_read(struct dvb_frontend *fe, u8 addr_data, u8 *data)
{
	int ret;
	struct qm1d1c0042 *qm = fe->tuner_priv;
	u8 addr[] = { qm->addr_tuner, addr_data };

	if ((addr_data != 0x00) && (addr_data != 0x0d))
		return -EFAULT;
	ret = qm->read(fe, addr, ARRAY_SIZE(addr));
	if (ret < 0)
		return ret;
	*data = ret;
	return 0;
}

/* write via demodulator */
int qm1d1c0042_fe_write_data(struct dvb_frontend *fe, u8 addr_data, u8 *data, int len)
{
	u8 buf[len + 1];

	buf[0] = addr_data;
	memcpy(buf + 1, data, len);
	return fe->ops.write(fe, buf, len + 1);
}

#define QM1D1C0042_FE_PASSTHROUGH 0xfe

int qm1d1c0042_fe_write_tuner(struct dvb_frontend *fe, u8 *data, int len)
{
	u8 buf[len + 2];

	buf[0] = QM1D1C0042_FE_PASSTHROUGH;
	buf[1] = ((struct qm1d1c0042 *)fe->tuner_priv)->addr_tuner << 1;
	memcpy(buf + 2, data, len);
	return fe->ops.write(fe, buf, len + 2);
}

int qm1d1c0042_write(struct dvb_frontend *fe, u8 addr, u8 data)
{
	struct qm1d1c0042 *qm = fe->tuner_priv;
	u8 buf[] = { addr, data };
	int err = qm1d1c0042_fe_write_tuner(fe, buf, sizeof(buf));

	qm->reg[addr] = buf[1];
	return err;
}

static const u8 qm1d1c0042_flag[32] = {
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

int qm1d1c0042_write_pskmsrst(struct dvb_frontend *fe)
{
	u8 data = 0x01;

	return qm1d1c0042_fe_write_data(fe, 0x03, &data, 1);
}

enum qm1d1c0042_agc {
	QM1D1C0042_AGC_AUTO,
	QM1D1C0042_AGC_MANUAL,
};

int qm1d1c0042_set_agc(struct dvb_frontend *fe, enum qm1d1c0042_agc agc)
{
	u8 data = (agc == QM1D1C0042_AGC_AUTO) ? 0xff : 0x00;
	int err = qm1d1c0042_fe_write_data(fe, 0x0a, &data, 1);

	if (err)
		return err;
	data = 0xb0 | ((agc == QM1D1C0042_AGC_AUTO) ? 1 : 0);
	err = qm1d1c0042_fe_write_data(fe, 0x10, &data, 1);
	if (err)
		return err;

	data = (agc == QM1D1C0042_AGC_AUTO) ? 0x40 : 0x00;
	return (err = qm1d1c0042_fe_write_data(fe, 0x11, &data, 1)) ? err : qm1d1c0042_write_pskmsrst(fe);
}

int qm1d1c0042_sleep(struct dvb_frontend *fe)
{
	struct qm1d1c0042 *qm = fe->tuner_priv;
	u8 buf = 1;

	dev_dbg(fe->dvb->device, "#%d %s\n", qm->idx, __func__);
	qm->reg[0x01] &= (~(1 << 3)) & 0xff;
	qm->reg[0x01] |= 1 << 0;
	qm->reg[0x05] |= 1 << 3;
	return	qm1d1c0042_set_agc(fe, QM1D1C0042_AGC_MANUAL)	||
		qm1d1c0042_write(fe, 0x05, qm->reg[0x05])	||
		qm1d1c0042_write(fe, 0x01, qm->reg[0x01])	||
		qm1d1c0042_fe_write_data(fe, 0x17, &buf, 1);
}

int qm1d1c0042_wakeup(struct dvb_frontend *fe)
{
	struct qm1d1c0042 *qm = fe->tuner_priv;
	u8 buf = 0;

	dev_dbg(fe->dvb->device, "#%d %s\n", qm->idx, __func__);
	qm->reg[0x01] |= 1 << 3;
	qm->reg[0x01] &= (~(1 << 0)) & 0xff;
	qm->reg[0x05] &= (~(1 << 3)) & 0xff;
	return	qm1d1c0042_fe_write_data(fe, 0x17, &buf, 1)	||
		qm1d1c0042_write(fe, 0x01, qm->reg[0x01])	||
		qm1d1c0042_write(fe, 0x05, qm->reg[0x05]);
}

void qm1d1c0042_get_channel_freq(u32 channel, u32 *number, u32 *freq)
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

static const u32 qm1d1c0042_freq_tab[9][3] = {
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

static const u32 qm1d1c0042_sd_tab[24][3] = {
	{0x38fae1, 0x0d, 0x5},
	{0x3f570a, 0x0e, 0x3},
	{0x05b333, 0x0e, 0x5},
	{0x3c0f5c, 0x0f, 0x4},
	{0x026b85, 0x0f, 0x6},
	{0x38c7ae, 0x10, 0x5},
	{0x3f23d7, 0x11, 0x3},
	{0x058000, 0x11, 0x5},
	{0x3bdc28, 0x12, 0x4},
	{0x023851, 0x12, 0x6},
	{0x38947a, 0x13, 0x5},
	{0x3ef0a3, 0x14, 0x3},
	{0x3c8000, 0x16, 0x4},
	{0x048000, 0x16, 0x6},
	{0x3c8000, 0x17, 0x5},
	{0x048000, 0x18, 0x3},
	{0x3c8000, 0x18, 0x6},
	{0x048000, 0x19, 0x4},
	{0x3c8000, 0x1a, 0x3},
	{0x048000, 0x1a, 0x5},
	{0x3c8000, 0x1b, 0x4},
	{0x048000, 0x1b, 0x6},
	{0x3c8000, 0x1c, 0x5},
	{0x048000, 0x1d, 0x3},
};

static int qm1d1c0042_tuning(struct qm1d1c0042 *qm, u32 *sd, u32 channel)
{
	int ret;
	struct dvb_frontend *fe = qm->fe;
	u8 i_data;
	u32 i, N, A;

	qm->reg[0x08] &= 0xf0;
	qm->reg[0x08] |= 0x09;

	qm->reg[0x13] &= 0x9f;
	qm->reg[0x13] |= 0x20;

	for (i = 0; i < 8; i++) {
		if ((qm1d1c0042_freq_tab[i+1][0] <= qm->freq) && (qm->freq < qm1d1c0042_freq_tab[i][0])) {
			i_data = qm->reg[0x02];
			i_data &= 0x0f;
			i_data |= qm1d1c0042_freq_tab[i][1] << 7;
			i_data |= qm1d1c0042_freq_tab[i][2] << 4;
			qm1d1c0042_write(fe, 0x02, i_data);
		}
	}

	*sd = qm1d1c0042_sd_tab[channel][0];
	N = qm1d1c0042_sd_tab[channel][1];
	A = qm1d1c0042_sd_tab[channel][2];

	qm->reg[0x06] &= 0x40;
	qm->reg[0x06] |= N;
	ret = qm1d1c0042_write(fe, 0x06, qm->reg[0x06]);
	if (ret)
		return ret;

	qm->reg[0x07] &= 0xf0;
	qm->reg[0x07] |= A & 0x0f;
	return qm1d1c0042_write(fe, 0x07, qm->reg[0x07]);
}

static int qm1d1c0042_local_lpf_tuning(struct qm1d1c0042 *qm, u32 channel)
{
	struct dvb_frontend *fe = qm->fe;
	u8 i_data;
	u32 sd = 0;
	int err = qm1d1c0042_tuning(qm, &sd, channel);

	if (err)
		return err;
	i_data = qm->reg[0x08] & 0xf0;
	i_data |= 2;
	err = qm1d1c0042_write(fe, 0x08, i_data);
	if (err)
		return err;

	qm->reg[0x09] &= 0xc0;
	qm->reg[0x09] |= (sd >> 16) & 0x3f;
	qm->reg[0x0a] = (sd >> 8) & 0xff;
	qm->reg[0x0b] = (sd >> 0) & 0xff;
	err =	qm1d1c0042_write(fe, 0x09, qm->reg[0x09])	||
		qm1d1c0042_write(fe, 0x0a, qm->reg[0x0a])	||
		qm1d1c0042_write(qm->fe, 0x0b, qm->reg[0x0b]);
	if (err)
		return err;

	i_data = qm->reg[0x0c];
	i_data &= 0x3f;
	err = qm1d1c0042_write(fe, 0x0c, i_data);
	if (err)
		return err;
	msleep_interruptible(1);

	i_data = qm->reg[0x0c];
	i_data |= 0xc0;
	return	qm1d1c0042_write(fe, 0x0c, i_data)	||
		qm1d1c0042_write(fe, 0x08, 0x09)	||
		qm1d1c0042_write(fe, 0x13, qm->reg[0x13]);
}

int qm1d1c0042_get_locked(struct qm1d1c0042 *qm, bool *locked)
{
	int err = qm1d1c0042_fe_read(qm->fe, 0x0d, &qm->reg[0x0d]);

	if (err)
		return err;
	if (qm->reg[0x0d] & 0x40)
		*locked = true;
	else
		*locked = false;
	return err;
}

u32 qm1d1c0042_freq2ch(u32 frequency)
{
	u32 freq = frequency / 10,
	    ch0 = (freq - 104948) / 3836, diff0 = freq - (104948 + 3836 * ch0),
	    ch1 = (freq - 161300) / 4000, diff1 = freq - (161300 + 4000 * ch1),
	    ch2 = (freq - 159300) / 4000, diff2 = freq - (159300 + 4000 * ch2),
	    min = diff0 < diff1 ? diff0 : diff1;

	if (frequency < 1024)
		return frequency;	/* consider as channel ID if low */
	if (diff2 < min)
		return ch2 + 24;
	if (min == diff1)
		return ch1 + 12;
	return ch0;
}

int qm1d1c0042_set_freq(struct dvb_frontend *fe, u32 frequency)
{
	struct qm1d1c0042 *qm = fe->tuner_priv;
	u32 channel = qm1d1c0042_freq2ch(frequency);
	u32 number, freq;
	bool locked = false;
	unsigned long timeout;
	int err = qm1d1c0042_set_agc(fe, QM1D1C0042_AGC_MANUAL);

	if (err)
		return err;
	qm1d1c0042_get_channel_freq(channel, &number, &freq);
	qm->freq = freq * 10 - 500;
	dev_dbg(fe->dvb->device, "#%d ch %d freq %d kHz\n", qm->idx, channel, qm->freq);

	err = qm1d1c0042_local_lpf_tuning(qm, channel);
	if (err)
		return err;

	timeout = jiffies + msecs_to_jiffies(1000);	/* 1s */
	while (time_before(jiffies, timeout)) {
		err = qm1d1c0042_get_locked(qm, &locked);
		if (err)
			return err;
		if (locked)
			break;
		msleep_interruptible(1);
	}
	dev_dbg(fe->dvb->device, "#%d %s %s\n", qm->idx, __func__, locked ? "LOCKED" : "TIMEOUT");
	return locked ? qm1d1c0042_set_agc(fe, QM1D1C0042_AGC_AUTO) : -ETIMEDOUT;
}

static struct dvb_tuner_ops qm1d1c0042_ops = {
	.info = {
		.frequency_min	= 1,		/* freq under 1024 kHz is handled as channel */
		.frequency_max	= 2150000,	/* kHz */
		.frequency_step	= 1000,		/* = 1 MHz */
	},
	.set_frequency = qm1d1c0042_set_freq,
	.sleep = qm1d1c0042_sleep,
	.init = qm1d1c0042_wakeup,
};

int qm1d1c0042_remove(struct i2c_client *client)
{
	struct dvb_frontend *fe = i2c_get_clientdata(client);

	dev_dbg(&client->dev, "%s\n", __func__);
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

int qm1d1c0042_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	u8 d[] = { 0x10, 0x15, 0x04 };
	struct dvb_frontend *fe = client->dev.platform_data;
	struct qm1d1c0042 *qm = kzalloc(sizeof(struct qm1d1c0042), GFP_KERNEL);

	if (!qm)
		return -ENOMEM;
	fe->tuner_priv = qm;
	qm->fe = fe;
	qm->idx = !(client->addr & 1);
	qm->addr_tuner = client->addr;
	qm->read = fe->ops.tuner_ops.calc_regs;
	memcpy(&fe->ops.tuner_ops, &qm1d1c0042_ops, sizeof(struct dvb_tuner_ops));
	memcpy(qm->reg, qm1d1c0042_reg_rw, sizeof(qm1d1c0042_reg_rw));
	qm->freq = 0;
	i2c_set_clientdata(client, fe);
	return	qm1d1c0042_fe_write_data(fe, 0x1e, d,   1)	||
		qm1d1c0042_fe_write_data(fe, 0x1c, d+1, 1)	||
		qm1d1c0042_fe_write_data(fe, 0x1f, d+2, 1);
}

static struct i2c_device_id qm1d1c0042_id_table[] = {
	{ QM1D1C0042_DRVNAME, 0 },
	{ },
};
MODULE_DEVICE_TABLE(i2c, qm1d1c0042_id_table);

static struct i2c_driver qm1d1c0042_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= qm1d1c0042_id_table->name,
	},
	.probe		= qm1d1c0042_probe,
	.remove		= qm1d1c0042_remove,
	.id_table	= qm1d1c0042_id_table,
};
module_i2c_driver(qm1d1c0042_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 QM1D1C0042 ISDB-S tuner driver");
MODULE_LICENSE("GPL");

