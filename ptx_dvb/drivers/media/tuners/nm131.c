/*
	Driver for Newport Media tuners NMI131, NMI130 and NMI120

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
*/

#include "dvb_frontend.h"
#include "nm131.h"

bool nm131_w(struct dvb_frontend *fe, u16 slvadr, u32 val, u32 sz)
{
	struct i2c_client	*d	= fe->demodulator_priv;
	u8		buf[]	= {0xFE, 0xCE, slvadr >> 8, slvadr & 0xFF, 0, 0, 0, 0};
	struct i2c_msg	msg[]	= {
		{.addr = d->addr,	.flags = 0,	.buf = buf,	.len = sz + 4,},
	};

	*(u32 *)(buf + 4) = slvadr == 0x36 ? val & 0x7F : val;
	return i2c_transfer(d->adapter, msg, 1) == 1;
}

bool nm131_w8(struct dvb_frontend *fe, u8 slvadr, u8 dat)
{
	struct i2c_client	*d	= fe->demodulator_priv;
	u8		buf[]	= {slvadr, dat};
	struct i2c_msg	msg[]	= {
		{.addr = d->addr,	.flags = 0,	.buf = buf,	.len = 2,},
	};
	return i2c_transfer(d->adapter, msg, 1) == 1;
}

bool nm131_r(struct dvb_frontend *fe, u16 slvadr, u8 *dat, u32 sz)
{
	struct i2c_client	*d	= fe->demodulator_priv;
	u8		rcmd[]	= {0xFE, 0xCF},
			buf[sz];
	struct i2c_msg	msg[]	= {
		{.addr = d->addr,	.flags = 0,		.buf = rcmd,	.len = 2,},
		{.addr = d->addr,	.flags = I2C_M_RD,	.buf = buf,	.len = sz,},
	};
	bool	ret = nm131_w(fe, slvadr, 0, 0) && i2c_transfer(d->adapter, msg, 2) == 2;

	memcpy(dat, buf, sz);
	return ret;
}

int nm131_tune(struct dvb_frontend *fe)
{
	struct vhf_filter_cutoff_codes_t {
		u32	Hz;
		u8	val8_0x08,
			val8_0x09;
	} const vhf_filter_cutoff_codes[] = {
		{45000000, 167, 58},	{55000000, 151, 57},	{65000000, 100, 54},	{75000000, 83, 53},	{85000000, 82, 53},
		{95000000, 65, 52},	{105000000, 64, 52},	{115000000, 64, 52},	{125000000, 0, 0}
	};
	const u8	v45[]		= {0, 1, 2, 3, 4, 6, 9, 12},
			ACI_audio_lut	= 0,
			aci_lut		= 1;
	const u32	lo_freq_lut[]	= {0, 0, 434000000, 237000000, 214000000, 118000000, 79000000, 53000000},
			*v11,
			adec_ddfs_fq	= 126217,
			ddfs_lut	= 0;
	u8		rf_reg_0x05	= 0x87,
			v15,
			val;
	int		i;
	u32		tune_rf		= fe->dtv_property_cache.frequency,
			clk_off_f	= tune_rf,
			xo		= 24000,
			lofreq,
			rf,
			v14,
			v32;
	bool		done		= false;

	if (!(nm131_w8(fe, 1, 0x50)		&&
		nm131_w8(fe, 0x47, 0x30)	&&
		nm131_w8(fe, 0x25, 0)		&&
		nm131_w8(fe, 0x20, 0)		&&
		nm131_w8(fe, 0x23, 0x4D)))
		return -EIO;
	while (1) {
		rf = clk_off_f;
		val = rf > 120400000 ? 0x85 : 5;
		if (rf_reg_0x05 != val) {
			nm131_w(fe, 0x05, val, 1);
			rf_reg_0x05 = val;
		}
		lofreq = rf;
		v11 = &lo_freq_lut[6];
		val = 6;
		if (lofreq > 53000000) {
			do {
				if (*v11 >= lofreq)
					break;
				--val;
				--v11;
			} while (val != 1);
		} else
			val = 7;
		i = 0;
		do {
			if (lofreq > vhf_filter_cutoff_codes[i].Hz && lofreq <= vhf_filter_cutoff_codes[i + 1].Hz)
				break;
			++i;
		} while (i != 8);
		nm131_w(fe, 8, vhf_filter_cutoff_codes[i].val8_0x08, 1);
		nm131_w(fe, 9, vhf_filter_cutoff_codes[i].val8_0x09, 1);
		v14 = lofreq / 1000 * 8 * v45[val];
		nm131_r(fe, 0x21, &v15, 1);
		v15 &= 3;
		xo = v15 == 2 ? xo * 2 : v15 == 3 ? xo >> 1 : xo;
		v32 = v14 / xo;
		if (!((v14 % xo * (0x80000000 / xo) >> 12) & 0x7FFFF) || done)
			break;
		clk_off_f += 1000;
		done = true;
	}
	xo = (v14 % xo * (0x80000000 / xo) >> 12) & 0x7FFFF;
	clk_off_f = v14;
	v14 /= 216000;
	if (v14 > 31)
		v14 = 31;
	if (v14 < 16)
		v14 = 16;
	nm131_w(fe, 1, (u16)v32 >> 1, 1);
	nm131_w(fe, 2, (v32 & 1) | 2 * xo, 1);
	nm131_w(fe, 3, xo >> 7, 1);
	nm131_w(fe, 4, (xo >> 15) | 16 * v14, 1);
	nm131_r(fe, 0x1D, &v15, 1);
	nm131_w(fe, 0x1D, 32 * val | (v15 & 0x1F), 1);
	if (lofreq < 300000000) {
		nm131_w(fe, 0x25, 0x78, 1);
		nm131_w(fe, 0x27, 0x7F, 1);
		nm131_w(fe, 0x29, 0x7F, 1);
		nm131_w(fe, 0x2E, 0x12, 1);
	} else {
		nm131_w(fe, 0x25, 0xF4, 1);
		nm131_w(fe, 0x27, 0xEF, 1);
		nm131_w(fe, 0x29, 0x4F, 1);
		nm131_w(fe, 0x2E, 0x34, 1);
	}
	nm131_w(fe, 0x36, lofreq < 150000000 ? 0x54 : 0x7C, 1);
	nm131_w(fe, 0x37, lofreq < 155000000 ? 0x84 : lofreq < 300000000 ? 0x9C : 0x84, 1);
	clk_off_f = (clk_off_f << 9) / v14 - 110592000;

	nm131_w(fe, 0x164, tune_rf < 300000000 ? 0x600 : 0x500, 4);
	rf = clk_off_f / 6750 + 16384;
	nm131_w(fe, 0x230, (adec_ddfs_fq << 15) / rf | 0x80000, 4);
	nm131_w(fe, 0x250, ACI_audio_lut, 4);
	nm131_w(fe, 0x27C, 0x1010, 4);
	nm131_w(fe, 0x1BC, (ddfs_lut << 14) / rf, 4);
	nm131_r(fe, 0x21C, (u8 *)(&v32), 4);
	nm131_w(fe, 0x21C, (((v32 & 0xFFC00000) | 524288000 / ((clk_off_f >> 14) + 6750)) & 0xC7BFFFFE) | 0x8000000, 4);
	nm131_r(fe, 0x234, (u8 *)(&v32), 4);
	nm131_w(fe, 0x234, v32 & 0xCFC00000, 4);
	nm131_w(fe, 0x210, ((864 * (clk_off_f >> 5) - 1308983296) / 216000 & 0xFFFFFFF0) | 3, 4);
	nm131_r(fe, 0x104, (u8 *)(&v32), 4);
	v32 = ((((clk_off_f > 3686396 ? 2 : clk_off_f >= 1843193 ? 1 : 0) << 16) | (((((((v32 & 0x87FFFFC0) | 0x10000011)
		& 0xFFFF87FF) | (aci_lut << 11)) & 0xFFFF7FFF) | 0x8000) & 0xFFF0FFFF)) & 0xFC0FFFFF) | 0xA00000;
	nm131_w(fe, 0x104, v32, 4);
	v32 = (v32 & 0xFFFFFFEF) | 0x20000020;
	nm131_w(fe, 0x104, v32, 4);
	nm131_r(fe, 0x328, (u8 *)(&v14), 4);
	if (v14) {
		v32 &= 0xFFFFFFDF;
		nm131_w(fe, 0x104, v32, 4);
		nm131_w(fe, 0x104, v32 | 0x20, 4);
	}
	return	nm131_w8(fe, 0x23, 0x4C)	&&
		nm131_w8(fe, 1, 0x50)		&&
		nm131_w8(fe, 0x71, 1)		&&
		nm131_w8(fe, 0x72, 0x24)	?
		0 : -EIO;
}

int nm131_probe(struct i2c_client *t, const struct i2c_device_id *id)
{
	struct tnr_rf_reg_t {
		u8 slvadr;
		u8 val;
	} const
	tnr_rf_defaults_lut[] = {
		{6, 72},	{7, 64},	{10, 235},	{11, 17},	{12, 16},	{13, 136},
		{16, 4},	{17, 48},	{18, 48},	{21, 170},	{22, 3},	{23, 128},
		{24, 103},	{25, 212},	{26, 68},	{28, 16},	{29, 238},	{30, 153},
		{33, 197},	{34, 145},	{36, 1},	{43, 145},	{45, 1},	{47, 128},
		{49, 0},	{51, 0},	{56, 0},	{57, 47},	{58, 0},	{59, 0}
	},
	nm120_rf_defaults_lut[] = {
		{14, 69},	{27, 14},	{35, 255},	{38, 130},	{40, 0},
		{48, 223},	{50, 223},	{52, 104},	{53, 24}
	};
	struct tnr_bb_reg_t {
		u16 slvadr;
		u32 val;
	} const
	tnr_bb_defaults_lut[2] = {
		{356, 2048},	{448, 764156359}
	};
	u8			i;
	struct dvb_frontend	*fe	= t->dev.platform_data;

	fe->ops.tuner_ops.set_params	= nm131_tune;
	if (nm131_w8(fe, 0xB0, 0xA0)		&&
		nm131_w8(fe, 0xB2, 0x3D)	&&
		nm131_w8(fe, 0xB3, 0x25)	&&
		nm131_w8(fe, 0xB4, 0x8B)	&&
		nm131_w8(fe, 0xB5, 0x4B)	&&
		nm131_w8(fe, 0xB6, 0x3F)	&&
		nm131_w8(fe, 0xB7, 0xFF)	&&
		nm131_w8(fe, 0xB8, 0xC0)	&&
		nm131_w8(fe, 3, 0)		&&
		nm131_w8(fe, 0x1D, 0)		&&
		nm131_w8(fe, 0x1F, 0)) {
		nm131_w8(fe, 0xE, 0x77);
		nm131_w8(fe, 0xF, 0x13);
		nm131_w8(fe, 0x75, 2);
	}
	for (i = 0; i < ARRAY_SIZE(tnr_rf_defaults_lut); i++)
		nm131_w(fe, tnr_rf_defaults_lut[i].slvadr, tnr_rf_defaults_lut[i].val, 1);
	nm131_r(fe, 0x36, &i, 1);
	nm131_w(fe, 0x36, i & 0x7F, 1);	/* no LDO bypass */
	nm131_w(fe, tnr_bb_defaults_lut[0].slvadr, tnr_bb_defaults_lut[0].val, 4);
	nm131_w(fe, tnr_bb_defaults_lut[1].slvadr, tnr_bb_defaults_lut[1].val, 4);
	for (i = 0; i < ARRAY_SIZE(nm120_rf_defaults_lut); i++)
		nm131_w(fe, nm120_rf_defaults_lut[i].slvadr, nm120_rf_defaults_lut[i].val, 1);
	nm131_w(fe, 0xA, 0xFB, 1);	/* ltgain */
	return 0;
}

static struct i2c_device_id nm131_id[] = {
	{NM131_MODNAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, nm131_id);

static struct i2c_driver nm131_driver = {
	.driver.name	= nm131_id->name,
	.probe		= nm131_probe,
	.id_table	= nm131_id,
};
module_i2c_driver(nm131_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <info@are.ma>");
MODULE_DESCRIPTION("Driver for Newport Media tuners NMI131, NMI130 and NMI120");
MODULE_LICENSE("GPL");
