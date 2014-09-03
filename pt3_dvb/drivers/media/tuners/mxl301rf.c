/*
 * Sharp VA4M6JC2103 - Earthsoft PT3 ISDB-T tuner MaxLinear CMOS Hybrid TV MxL301RF
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

#include "mxl301rf.h"

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 MxL301RF MaxLinear CMOS Hybrid TV ISDB-T tuner driver");
MODULE_LICENSE("GPL");

struct mxl301rf {
	struct dvb_frontend *fe;
	u8 addr_tuner, idx;
	u32 freq;
};

struct shf_dvbt {
	u32	freq,		/* Channel center frequency @ kHz	*/
		freq_th;	/* Offset frequency threshold @ kHz	*/
	u8	shf_val,	/* Spur shift value			*/
		shf_dir;	/* Spur shift direction			*/
};

static const struct shf_dvbt shf_dvbt_tab[] = {
	{  64500, 500, 0x92, 0x07 },
	{ 191500, 300, 0xe2, 0x07 },
	{ 205500, 500, 0x2c, 0x04 },
	{ 212500, 500, 0x1e, 0x04 },
	{ 226500, 500, 0xd4, 0x07 },
	{  99143, 500, 0x9c, 0x07 },
	{ 173143, 500, 0xd4, 0x07 },
	{ 191143, 300, 0xd4, 0x07 },
	{ 207143, 500, 0xce, 0x07 },
	{ 225143, 500, 0xce, 0x07 },
	{ 243143, 500, 0xd4, 0x07 },
	{ 261143, 500, 0xd4, 0x07 },
	{ 291143, 500, 0xd4, 0x07 },
	{ 339143, 500, 0x2c, 0x04 },
	{ 117143, 500, 0x7a, 0x07 },
	{ 135143, 300, 0x7a, 0x07 },
	{ 153143, 500, 0x01, 0x07 }
};

static const u32 mxl301rf_rf_tab[112] = {
	0x058d3f49, 0x05e8ccc9, 0x06445a49, 0x069fe7c9, 0x06fb7549,
	0x075702c9, 0x07b29049, 0x080e1dc9, 0x0869ab49, 0x08c538c9,
	0x0920c649, 0x097c53c9, 0x09f665c9, 0x0a51f349, 0x0aad80c9,
	0x0b090e49, 0x0b649bc9, 0x0ba1a4c9, 0x0bfd3249, 0x0c58bfc9,
	0x0cb44d49, 0x0d0fdac9, 0x0d6b6849, 0x0dc6f5c9, 0x0e228349,
	0x0e7e10c9, 0x0ed99e49, 0x0f352bc9, 0x0f90b949, 0x0fec46c9,
	0x1047d449, 0x10a361c9, 0x10feef49, 0x115a7cc9, 0x11b60a49,
	0x121197c9, 0x126d2549, 0x12c8b2c9, 0x13244049, 0x137fcdc9,
	0x13db5b49, 0x1436e8c9, 0x14927649, 0x14ee03c9, 0x15499149,
	0x15a51ec9, 0x1600ac49, 0x165c39c9, 0x16b7c749, 0x171354c9,
	0x176ee249, 0x17ca6fc9, 0x1825fd49, 0x18818ac9, 0x18dd1849,
	0x1938a5c9, 0x19943349, 0x19efc0c9, 0x1a4b4e49, 0x1aa6dbc9,
	0x1b026949, 0x1b5df6c9, 0x1bb98449, 0x1c339649, 0x1c8f23c9,
	0x1ceab149, 0x1d463ec9, 0x1da1cc49, 0x1dfd59c9, 0x1e58e749,
	0x1eb474c9, 0x1f100249, 0x1f6b8fc9, 0x1fc71d49, 0x2022aac9,
	0x207e3849, 0x20d9c5c9, 0x21355349, 0x2190e0c9, 0x21ec6e49,
	0x2247fbc9, 0x22a38949, 0x22ff16c9, 0x235aa449, 0x23b631c9,
	0x2411bf49, 0x246d4cc9, 0x24c8da49, 0x252467c9, 0x257ff549,
	0x25db82c9, 0x26371049, 0x26929dc9, 0x26ee2b49, 0x2749b8c9,
	0x27a54649, 0x2800d3c9, 0x285c6149, 0x28b7eec9, 0x29137c49,
	0x296f09c9, 0x29ca9749, 0x2a2624c9, 0x2a81b249, 0x2add3fc9,
	0x2b38cd49, 0x2b945ac9, 0x2befe849, 0x2c4b75c9, 0x2ca70349,
	0x2d0290c9, 0x2d5e1e49,
};
#define MXL301RF_NHK (mxl301rf_rf_tab[77])	/* 日本放送協会 Nippon Hōsō Kyōkai, Japan Broadcasting Corporation */

int mxl301rf_freq(int freq)
{
	if (freq >= 90000000)
		return freq;					/* real_freq Hz	*/
	if (freq > 255)
		return MXL301RF_NHK;
	if (freq > 127)
		return mxl301rf_rf_tab[freq - 128];		/* freqno (IO#)	*/
	if (freq > 63) {					/* CATV		*/
		freq -= 64;
		if (freq > 22)
			return mxl301rf_rf_tab[freq - 1];	/* C23-C62	*/
		if (freq > 12)
			return mxl301rf_rf_tab[freq - 10];	/* C13-C22	*/
		return MXL301RF_NHK;
	}
	if (freq > 62)
		return MXL301RF_NHK;
	if (freq > 12)
		return mxl301rf_rf_tab[freq + 50];		/* 13-62	*/
	if (freq >  3)
		return mxl301rf_rf_tab[freq +  9];		/*  4-12	*/
	if (freq)
		return mxl301rf_rf_tab[freq -  1];		/*  1-3		*/
	return MXL301RF_NHK;
}

void mxl301rf_rftune(u8 *data, u32 *size, u32 freq)
{
	u32 dig_rf_freq, tmp, frac_divider, kHz, MHz, i;
	u8 rf_data[] = {
		0x13, 0x00,	/* abort tune			*/
		0x3b, 0xc0,
		0x3b, 0x80,
		0x10, 0x95,	/* BW				*/
		0x1a, 0x05,
		0x61, 0x00,
		0x62, 0xa0,
		0x11, 0x40,	/* 2 bytes to store RF freq.	*/
		0x12, 0x0e,	/* 2 bytes to store RF freq.	*/
		0x13, 0x01	/* start tune			*/
	};

	freq = mxl301rf_freq(freq);
	dig_rf_freq = 0;
	tmp = 0;
	frac_divider = 1000000;
	kHz = 1000;
	MHz = 1000000;

	dig_rf_freq = freq / MHz;
	tmp = freq % MHz;

	for (i = 0; i < 6; i++) {
		dig_rf_freq <<= 1;
		frac_divider /= 2;
		if (tmp > frac_divider) {
			tmp -= frac_divider;
			dig_rf_freq++;
		}
	}
	if (tmp > 7812)
		dig_rf_freq++;

	rf_data[2 * (7) + 1] = (u8)(dig_rf_freq);
	rf_data[2 * (8) + 1] = (u8)(dig_rf_freq >> 8);

	for (i = 0; i < ARRAY_SIZE(shf_dvbt_tab); i++) {
		if ((freq >= (shf_dvbt_tab[i].freq - shf_dvbt_tab[i].freq_th) * kHz) &&
				(freq <= (shf_dvbt_tab[i].freq + shf_dvbt_tab[i].freq_th) * kHz)) {
			rf_data[2 * (5) + 1] = shf_dvbt_tab[i].shf_val;
			rf_data[2 * (6) + 1] = 0xa0 | shf_dvbt_tab[i].shf_dir;
			break;
		}
	}
	memcpy(data, rf_data, sizeof(rf_data));
	*size = sizeof(rf_data);

	pr_debug("mx_rftune freq=%d\n", freq);
}

/* write via demodulator */
int mxl301rf_fe_write_data(struct dvb_frontend *fe, u8 addr_data, const u8 *data, int len)
{
	u8 buf[len + 1];

	buf[0] = addr_data;
	memcpy(buf + 1, data, len);
	return fe->ops.write(fe, buf, len + 1);
}

#define MXL301RF_FE_PASSTHROUGH 0xfe

int mxl301rf_fe_write_tuner(struct dvb_frontend *fe, const u8 *data, int len)
{
	u8 buf[len + 2];

	buf[0] = MXL301RF_FE_PASSTHROUGH;
	buf[1] = ((struct mxl301rf *)fe->tuner_priv)->addr_tuner << 1;
	memcpy(buf + 2, data, len);
	return fe->ops.write(fe, buf, len + 2);
}

/* read via demodulator */
void mxl301rf_fe_read(struct dvb_frontend *fe, u8 addr, u8 *data)
{
	const u8 wbuf[2] = {0xfb, addr};
	int ret;

	mxl301rf_fe_write_tuner(fe, wbuf, sizeof(wbuf));
	ret = fe->ops.write(fe, NULL, (1 << 16) | (((struct mxl301rf *)fe->tuner_priv)->addr_tuner << 8) | addr);
	if (ret >= 0)
		*data = ret;
}

void mxl301rf_idac_setting(struct dvb_frontend *fe)
{
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
	mxl301rf_fe_write_tuner(fe, idac, sizeof(idac));
}

void mxl301rf_set_register(struct dvb_frontend *fe, u8 addr, u8 value)
{
	const u8 data[2] = {addr, value};
	mxl301rf_fe_write_tuner(fe, data, sizeof(data));
}

int mxl301rf_write_imsrst(struct dvb_frontend *fe)
{
	u8 data = 0x01 << 6;
	return mxl301rf_fe_write_data(fe, 0x01, &data, 1);
}

enum mxl301rf_agc {
	MXL301RF_AGC_AUTO,
	MXL301RF_AGC_MANUAL,
};

int mxl301rf_set_agc(struct dvb_frontend *fe, enum mxl301rf_agc agc)
{
	u8 data = (agc == MXL301RF_AGC_AUTO) ? 0x40 : 0x00;
	int ret = mxl301rf_fe_write_data(fe, 0x25, &data, 1);
	if (ret)
		return ret;

	data = 0x4c | ((agc == MXL301RF_AGC_AUTO) ? 0x00 : 0x01);
	return	mxl301rf_fe_write_data(fe, 0x23, &data, 1) ||
		mxl301rf_write_imsrst(fe);
}

int mxl301rf_sleep(struct dvb_frontend *fe)
{
	u8 buf = (1 << 7) | (1 << 4);
	const u8 data[4] = {0x01, 0x00, 0x13, 0x00};
	int err = mxl301rf_set_agc(fe, MXL301RF_AGC_MANUAL);
	if (err)
		return err;
	mxl301rf_fe_write_tuner(fe, data, sizeof(data));
	return mxl301rf_fe_write_data(fe, 0x03, &buf, 1);
}

static const u8 mxl301rf_freq_tab[][3] = {
	{   2, 0,  3 },
	{  12, 1, 22 },
	{  21, 0, 12 },
	{  62, 1, 63 },
	{ 112, 0, 62 }
};

bool mxl301rf_rfsynth_locked(struct dvb_frontend *fe)
{
	u8 data;

	mxl301rf_fe_read(fe, 0x16, &data);
	return (data & 0x0c) == 0x0c;
}

bool mxl301rf_refsynth_locked(struct dvb_frontend *fe)
{
	u8 data;

	mxl301rf_fe_read(fe, 0x16, &data);
	return (data & 0x03) == 0x03;
}

bool mxl301rf_locked(struct dvb_frontend *fe)
{
	bool locked1 = false, locked2 = false;
	unsigned long timeout = jiffies + msecs_to_jiffies(100);

	while (time_before(jiffies, timeout)) {
		locked1 = mxl301rf_rfsynth_locked(fe);
		locked2 = mxl301rf_refsynth_locked(fe);
		if (locked1 && locked2)
			break;
		msleep_interruptible(1);
	}
	pr_debug("#%d %s lock1=%d lock2=%d\n", ((struct mxl301rf *)fe->tuner_priv)->idx, __func__, locked1, locked2);
	return locked1 && locked2 ? !mxl301rf_set_agc(fe, MXL301RF_AGC_AUTO) : false;
}

int mxl301rf_tuner_rftune(struct dvb_frontend *fe, u32 freq)
{
	struct mxl301rf *mx = fe->tuner_priv;
	u8 data[100];
	u32 size = 0;
	int err = mxl301rf_set_agc(fe, MXL301RF_AGC_MANUAL);
	if (err)
		return err;

	mx->freq = freq;
	mxl301rf_rftune(data, &size, freq);
	if (size != 20) {
		pr_debug("fail mx_rftune size = %d\n", size);
		return -EINVAL;
	}
	mxl301rf_fe_write_tuner(fe, data, 14);
	msleep_interruptible(1);
	mxl301rf_fe_write_tuner(fe, data + 14, 6);
	msleep_interruptible(1);
	mxl301rf_set_register(fe, 0x1a, 0x0d);
	mxl301rf_idac_setting(fe);

	return mxl301rf_locked(fe) ? 0 : -ETIMEDOUT;
}

int mxl301rf_wakeup(struct dvb_frontend *fe)
{
	struct mxl301rf *mx = fe->tuner_priv;
	int err;
	u8 buf = (1 << 7) | (0 << 4);
	const u8 data[2] = {0x01, 0x01};

	err = mxl301rf_fe_write_data(fe, 0x03, &buf, 1);
	if (err)
		return err;
	mxl301rf_fe_write_tuner(fe, data, sizeof(data));
	mxl301rf_tuner_rftune(fe, mx->freq);
	return 0;
}

void mxl301rf_ch2freq(u32 channel, bool *catv, u32 *number, u32 *freq)
{
	u32 i;
	s32 freq_offset = 0;

	if (12 <= channel)
		freq_offset += 2;
	if (17 <= channel)
		freq_offset -= 2;
	if (63 <= channel)
		freq_offset += 2;
	*freq = 93 + channel * 6 + freq_offset;

	for (i = 0; i < ARRAY_SIZE(mxl301rf_freq_tab); i++) {
		if (channel <= mxl301rf_freq_tab[i][0]) {
			*catv = mxl301rf_freq_tab[i][1] ? true : false;
			*number = channel + mxl301rf_freq_tab[i][2] - mxl301rf_freq_tab[i][0];
			break;
		}
	}
}

int mxl301rf_release(struct dvb_frontend *fe)
{
	kfree(fe->tuner_priv);
	fe->tuner_priv = NULL;
	return 0;
}

static struct dvb_tuner_ops mxl301rf_ops = {
	.info = {
		.frequency_min	= 1,		/* actually 90 MHz, freq below that is handled as ch */
		.frequency_max	= 770000000,	/* Hz */
		.frequency_step	= 142857,
	},
	.set_frequency = mxl301rf_tuner_rftune,
	.sleep = mxl301rf_sleep,
	.init = mxl301rf_wakeup,
	.release = mxl301rf_release,
};

int mxl301rf_attach(struct dvb_frontend *fe, u8 idx, u8 addr_tuner)
{
	u8 d[] = { 0x10, 0x01 };
	struct mxl301rf *mx = kzalloc(sizeof(struct mxl301rf), GFP_KERNEL);
	if (!mx)
		return -ENOMEM;
	fe->tuner_priv = mx;
	mx->fe = fe;
	mx->idx = idx;
	mx->addr_tuner = addr_tuner;
	memcpy(&fe->ops.tuner_ops, &mxl301rf_ops, sizeof(struct dvb_tuner_ops));

	return	mxl301rf_fe_write_data(fe, 0x1c, d, 1)	||
		mxl301rf_fe_write_data(fe, 0x1d, d+1, 1);
}
EXPORT_SYMBOL(mxl301rf_attach);

