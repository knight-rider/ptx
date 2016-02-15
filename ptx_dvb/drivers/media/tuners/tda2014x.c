/*
 * Driver for NXP Semiconductors tuner TDA2014x
 *
 * Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
 */

#include <linux/pci.h>
#include "dvb_frontend.h"
#include "tda2014x.h"

struct tda2014x {
	struct i2c_adapter *i2c;
};

int tda2014x_r(struct dvb_frontend *fe, u8 slvadr)
{
	struct tda2014x *t = fe->tuner_priv;
	u8	buf[]	= {0xFE, 0xA8, slvadr},
		rcmd[]	= {0xFE, 0xA9},
		ret	= 0;
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,		.buf = buf,	.len = 3,},
		{.addr = fe->id,	.flags = 0,		.buf = rcmd,	.len = 2,},
		{.addr = fe->id,	.flags = I2C_M_RD,	.buf = &ret,	.len = 1,},
	};
	return i2c_transfer(t->i2c, msg, 3) == 3 ? ret : -EIO;
}

bool tda2014x_r8(struct dvb_frontend *fe, u16 slvadr, u8 start_bit, u8 nbits, u8 *rdat)
{
	u8	mask	= nbits > 7 ? 0xFF : ((1 << nbits) - 1) << start_bit;
	int	val	= tda2014x_r(fe, slvadr);

	if (val < 0)
		return false;
	*rdat = (val & mask) >> start_bit;
	return true;
}

bool tda2014x_w8(struct dvb_frontend *fe, u8 slvadr, u8 dat)
{
	struct tda2014x *t = fe->tuner_priv;
	u8 buf[] = {slvadr, dat};
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = 2,},
	};
	return i2c_transfer(t->i2c, msg, 1) == 1;
}

bool tda2014x_w16(struct dvb_frontend *fe, u16 slvadr, u8 start_bit, u8 nbits, u8 nbytes, bool rmw, u8 access, u16 wdat)
{
	struct tda2014x *t = fe->tuner_priv;
	u16	mask	= nbits > 15 ? 0xFFFF : ((1 << nbits) - 1) << start_bit,
		val	= mask & (wdat << start_bit);
	u8	*wval	= (u8 *)&val,
		i;

	for (i = 0, nbytes = !nbytes ? 1 : nbytes > 2 ? 2 : nbytes; access & 2 && nbytes; i++, nbytes--) {
		u8	buf[]	= {0xFE, 0xA8, slvadr + i, 0};
		int	ret	= tda2014x_r(fe, slvadr + i);
		struct i2c_msg msg[] = {
			{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = 4,},
		};

		if (ret < 0)
			return false;
		if (rmw)
			wval[nbytes - 1] |= ~(mask >> 8 * i) & ret;
		buf[3] = wval[nbytes - 1];
		if (i2c_transfer(t->i2c, msg, 1) != 1)
			return false;
	}
	return true;
}

int tda2014x_tune(struct dvb_frontend *fe)
{
	bool	bDoublerEnable[]		= {false, true, true, true, true},
		bDcc1Enable[]			= {false, true, true, true, true},
		bDcc2Enable[]			= {false, true, true, true, true},
		bPpfEnable[]			= {false, true, true, true, true},
		bDiv1ConfigInDivideBy3[]	= {false, true, false, true, false},
		bDiv2ConfigInDivideBy3[]	= {false, true, true, false, false},
		bSelectDivideBy4Or5Or6Or7Path[]	= {false, true, true, true, true},
		bSelectDivideBy8Path[]		= {true, false, false, false, false},
		bInputMuxEnable;
	u64	kHz = fe->dtv_property_cache.frequency,
		ResLsb,
		kint,
		Nint,
		v15;
	enum {
		TDA2014X_LNA_GAIN_7dB	= 0x0,
		TDA2014X_LNA_GAIN_10dB	= 0x1,
		TDA2014X_LNA_GAIN_13dB	= 0x2,		/* default */
		TDA2014X_LNA_GAIN_18dB	= 0x3,
		TDA2014X_LNA_GAIN_NEGATIVE_11dB = 0x4,

		TDA2014X_LPT_GAIN_NEGATIVE_8dB	= 0x0,
		TDA2014X_LPT_GAIN_NEGATIVE_10dB	= 0x1,	/* default */
		TDA2014X_LPT_GAIN_NEGATIVE_14dB	= 0x2,
		TDA2014X_LPT_GAIN_NEGATIVE_16dB	= 0x3,

		TDA2014X_AMPOUT_15DB	= 0x0,
		TDA2014X_AMPOUT_18DB	= 0x1,
		TDA2014X_AMPOUT_18DB_b	= 0x2,
		TDA2014X_AMPOUT_21DB	= 0x3,		/* default */
		TDA2014X_AMPOUT_24DB	= 0x6,		/* ok too */
		TDA2014X_AMPOUT_27DB	= 0x7,
		TDA2014X_AMPOUT_30DB	= 0xE,
		TDA2014X_AMPOUT_33DB	= 0xF,
	};
	u8	val,
		R,
		Premain,
		i	= kHz <= 1075000 ? 0 : kHz <= 1228000 ? 1 : kHz <= 1433000 ? 2 : kHz <= 1720000 ? 3 : 4,
		lna	= TDA2014X_LNA_GAIN_13dB,
		gain	= (lna == TDA2014X_LNA_GAIN_18dB) | ((lna & 3) << 4) | (TDA2014X_LPT_GAIN_NEGATIVE_10dB << 1),
		ampout	= TDA2014X_AMPOUT_21DB;

	if (!(tda2014x_w8(fe, 0xA, 0)		&&
		tda2014x_w8(fe, 0x10, 0xB0)	&&
		tda2014x_w8(fe, 0x11, 2)	&&
		tda2014x_w8(fe, 3, 1)		&&

		/* GetLoConfig */
		tda2014x_r8(fe, 0x25, 3, 1, &val)))
		return -EIO;
	bInputMuxEnable = val;

	/* SetLoConfig */
	tda2014x_w16(fe, 0x22, 0, 8, 0, 0, 6,
		(bDoublerEnable[i] << 7) | (bDcc1Enable[i] << 6) | (bDcc2Enable[i] << 5) | 0b11110 | bPpfEnable[i]) &&
	tda2014x_r8(fe, 0x23, 0, 8, &val) &&
	tda2014x_w16(fe, 0x23, 0, 8, 0, 0, 6, (bDiv1ConfigInDivideBy3[i] << 7) | (bDiv2ConfigInDivideBy3[i] << 5) |
		(bSelectDivideBy4Or5Or6Or7Path[i] << 3) | (bSelectDivideBy8Path[i] << 2) | (val & 0b1010011)) &&
	tda2014x_w16(fe, 0x25, 3, 1, 0, 1, 6, bInputMuxEnable);

	ResLsb = (8 - i) * kHz * 39;	/* Xtal 27 MHz */
	kint = ResLsb;
	v15 = ResLsb / 0x100000;
	R = 1;
	Premain = 1;
	Nint = v15 * R >> Premain;
	if (Nint < 131) {
		Premain = 0;
		Nint = v15 * R >> Premain;
		if (Nint > 251) {
			R = 3;
			Premain = 2;
			goto LABEL_36;
		}
		if (Nint < 131) {
			R = 3;
			Premain = 1;
			goto LABEL_36;
		}
	} else if (Nint > 251) {
		Premain = 2;
		Nint = v15 * R >> Premain;
		if (Nint > 251) {
			R = 3;
			Premain = 2;
		}
LABEL_36:
		Nint = v15 * R >> Premain;
		if (Nint < 131 || Nint > 251)
			return -ERANGE;
	}
	switch (100 * R >> Premain) {
	case 25:
		kint = ResLsb / 4;
		break;
	case 50:
		kint = ResLsb / 2;
		break;
	case 75:
		kint = ResLsb / 2 + ResLsb / 4;
		break;
	case 100:
		break;
	case 150:
		kint = ResLsb / 2 + ResLsb;
		break;
	default:
		return -ERANGE;
	}
	return	!(tda2014x_w16(fe, 3, 6, 2, 0, 1, 6, R - 1)			&&

		/* SetPllDividerConfig */
		tda2014x_w16(fe, 0x1A, 5, 1, 0, 1, 6, Premain == 1 ? 0 : 1)	&&
		tda2014x_w16(fe, 0x1E, 0, 8, 0, 0, 6, kint / 0x100000)		&&
		tda2014x_w16(fe, 0x1F, 0, 16, 2, 0, 6, kint % 0x100000)		&&

		/* ProgramVcoChannelChange */
		tda2014x_r8(fe, 0x12, 0, 8, &val)				&&
		tda2014x_w16(fe, 0x12, 0, 8, 0, 0, 6, (val & 0x7F) | 0x40)	&&
		tda2014x_w16(fe, 0x13, 0, 2, 0, 1, 6, 2)			&&
		tda2014x_w16(fe, 0x13, 7, 1, 0, 1, 6, 0)			&&
		tda2014x_w16(fe, 0x13, 5, 1, 0, 1, 6, 1)			&&
		tda2014x_w16(fe, 0x13, 5, 1, 0, 1, 6, 1)			&&
		tda2014x_w16(fe, 0x13, 7, 1, 0, 1, 6, 0)			&&
		tda2014x_w16(fe, 0x13, 4, 1, 0, 1, 6, 1)			&&
		((tda2014x_r8(fe, 0x15, 4, 1, &val) && val == 1)		||
		(tda2014x_r8(fe, 0x15, 4, 1, &val) && val == 1))		&&
		tda2014x_w16(fe, 0x13, 4, 1, 0, 1, 6, 0)			&&
		tda2014x_r8(fe, 0x12, 0, 8, &val)				&&
		tda2014x_w16(fe, 0x12, 0, 8, 0, 0, 6, val & 0x7F)		&&

		/* SetFilterBandwidth */
		tda2014x_w16(fe, 0xA, 0, 4, 0, 1, 6, 0xA)	&&
		tda2014x_w16(fe, 0xB, 1, 7, 0, 1, 6, 0x7C)	&&

		/* SetGainConfig */
		tda2014x_r8(fe, 6, 0, 8, &val)					&&
		tda2014x_w16(fe, 6, 0, 8, 0, 0, 6, (val & 0x48) | 0x80 | gain)	&&
		tda2014x_r8(fe, 9, 0, 8, &val)					&&
		tda2014x_w16(fe, 9, 0, 8, 0, 0, 6, 0b10110000 | (val & 3))	&&
		tda2014x_w16(fe, 0xA, 5, 3, 0, 1, 6, 3)				&&
		tda2014x_w16(fe, 0xC, 4, 4, 0, 1, 6, ampout)			&&

		tda2014x_w8(fe, 0xA, 0xFF)	&&
		tda2014x_w8(fe, 0x10, 0xB2)	&&
		tda2014x_w8(fe, 0x11, 0)	&&
		tda2014x_w8(fe, 3, 1)) * -EIO;
}

static struct dvb_tuner_ops tda2014x_ops = {
	.set_params = tda2014x_tune,
};

int tda2014x_remove(struct i2c_client *c)
{
	kfree(i2c_get_clientdata(c));
	return 0;
}

int tda2014x_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	u8			val	= 0;
	struct tda2014x		*t	= kzalloc(sizeof(struct tda2014x), GFP_KERNEL);
	struct dvb_frontend	*fe	= c->dev.platform_data;

	if (!t)
		return -ENOMEM;
	t->i2c	= c->adapter;
	fe->tuner_priv = t;
	memcpy(&fe->ops.tuner_ops, &tda2014x_ops, sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(c, t);
	fe->dtv_property_cache.frequency = 1318000;

	tda2014x_w8(fe, 0x13, 0)	&&
	tda2014x_w8(fe, 0x15, 0)	&&
	tda2014x_w8(fe, 0x17, 0)	&&
	tda2014x_w8(fe, 0x1C, 0)	&&
	tda2014x_w8(fe, 0x1D, 0)	&&
	tda2014x_w8(fe, 0x1F, 0)	&&
	(tda2014x_w8(fe, 7, 0x31), tda2014x_w8(fe, 8, 0x77), tda2014x_w8(fe, 4, 2))	&&

	/* SetPowerMode */
	tda2014x_r8(fe, 2, 0, 8, &val)					&&
	tda2014x_w16(fe, 2, 0, 8, 0, 0, 6, val | 0x81)			&&
	tda2014x_r8(fe, 6, 0, 8, &val)					&&
	tda2014x_w16(fe, 6, 0, 8, 0, 0, 6, (val | 0x39) & 0x7F)		&&
	tda2014x_r8(fe, 7, 0, 8, &val)					&&
	tda2014x_w16(fe, 7, 0, 8, 0, 0, 6, val | 0xAE)			&&
	tda2014x_r8(fe, 0xF, 0, 8, &val)				&&
	tda2014x_w16(fe, 0xF, 0, 8, 0, 0, 6, val | 0x80)		&&
	tda2014x_r8(fe, 0x18, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x18, 0, 8, 0, 0, 6, val & 0x7F)		&&
	tda2014x_r8(fe, 0x1A, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x1A, 0, 8, 0, 0, 6, val | 0xC0)		&&
	tda2014x_w16(fe, 0x22, 0, 8, 0, 0, 6, 0xFF)			&&
	tda2014x_r8(fe, 0x23, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x23, 0, 8, 0, 0, 6, val & 0xFE)		&&
	tda2014x_r8(fe, 0x25, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x25, 0, 8, 0, 0, 6, val | 8)			&&
	tda2014x_r8(fe, 0x27, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x27, 0, 8, 0, 0, 6, (val | 0xC0) & 0xDF)	&&
	tda2014x_r8(fe, 0x24, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x24, 0, 8, 0, 0, 6, (val | 4) & 0xCF)		&&
	tda2014x_r8(fe, 0xD, 0, 8, &val)				&&
	tda2014x_w16(fe, 0xD, 0, 8, 0, 0, 6, val & 0xDF)		&&
	tda2014x_r8(fe, 9, 0, 8, &val)					&&
	tda2014x_w16(fe, 9, 0, 8, 0, 0, 6, (val | 0xB0) & 0xB1)		&&
	tda2014x_r8(fe, 0xA, 0, 8, &val)				&&
	tda2014x_w16(fe, 0xA, 0, 8, 0, 0, 6, (val | 0x6F) & 0x7F)	&&
	tda2014x_r8(fe, 0xB, 0, 8, &val)				&&
	tda2014x_w16(fe, 0xB, 0, 8, 0, 0, 6, (val | 0x7A) & 0x7B)	&&
	tda2014x_w16(fe, 0xC, 0, 8, 0, 0, 6, 0)				&&
	tda2014x_w16(fe, 0x19, 0, 8, 0, 0, 6, 0xFA)			&&
	tda2014x_r8(fe, 0x1B, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x1B, 0, 8, 0, 0, 6, val & 0x7F)		&&
	tda2014x_r8(fe, 0x21, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x21, 0, 8, 0, 0, 6, val | 0x40)		&&
	tda2014x_r8(fe, 0x10, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x10, 0, 8, 0, 0, 6, (val | 0x90) & 0xBF)	&&
	tda2014x_r8(fe, 0x14, 0, 8, &val)				&&
	tda2014x_w16(fe, 0x14, 0, 8, 0, 0, 6, (val | 0x20) & 0xEF)	&&

	/* ProgramPllPor */
	tda2014x_w16(fe, 0x1A, 6, 1, 0, 1, 6, 1)	&&
	tda2014x_w16(fe, 0x18, 0, 1, 0, 1, 6, 1)	&&
	tda2014x_w16(fe, 0x18, 7, 1, 0, 1, 6, 1)	&&
	tda2014x_w16(fe, 0x1B, 7, 1, 0, 1, 6, 1)	&&
	tda2014x_w16(fe, 0x18, 0, 1, 0, 1, 6, 0)	&&

	/* ProgramVcoPor */
	tda2014x_r8(fe, 0xF, 0, 8, &val)						&&
	(val = (val & 0x1F) | 0x80, tda2014x_w16(fe, 0xF, 0, 8, 0, 0, 6, val))		&&
	tda2014x_r8(fe, 0x13, 0, 8, &val)						&&
	(val = (val & 0xFFFFFFCF) | 0x20, tda2014x_w16(fe, 0x13, 0, 8, 0, 0, 6, val))	&&
	tda2014x_r8(fe, 0x12, 0, 8, &val)				&&
	(val |= 0xC0, tda2014x_w16(fe, 0x12, 0, 8, 0, 0, 6, val))	&&
	tda2014x_w16(fe, 0x10, 5, 1, 0, 1, 6, 1)			&&
	tda2014x_w16(fe, 0x10, 5, 1, 0, 1, 6, 1)			&&
	tda2014x_w16(fe, 0xF, 5, 1, 0, 1, 6, 1)				&&
	tda2014x_r8(fe, 0x11, 4, 1, &val)				&&
	(val || tda2014x_r8(fe, 0x11, 4, 1, &val))			&&
	(val || tda2014x_r8(fe, 0x11, 4, 1, &val))			&&
	val								&&
	tda2014x_r8(fe, 0x10, 0, 4, &val)				&&
	tda2014x_w16(fe, 0xF, 0, 4, 0, 1, 6, val)			&&
	tda2014x_w16(fe, 0xF, 6, 1, 0, 1, 6, 1)				&&
	tda2014x_w16(fe, 0xF, 5, 1, 0, 1, 6, 0)				&&
	tda2014x_r8(fe, 0x12, 0, 8, &val)				&&
	(val &= 0x7F, tda2014x_w16(fe, 0x12, 0, 8, 0, 0, 6, val))	&&
	tda2014x_w16(fe, 0xD, 5, 2, 0, 1, 6, 1)				&&

	/* EnableLoopThrough */
	tda2014x_r8(fe, 6, 0, 8, &val)			&&
	tda2014x_w16(fe, 6, 0, 8, 0, 0, 6, (val & 0xF7) | 8);

	return tda2014x_tune(fe);
}

static struct i2c_device_id tda2014x_id[] = {
	{TDA2014X_MODNAME, 0},
	{},
};
MODULE_DEVICE_TABLE(i2c, tda2014x_id);

static struct i2c_driver tda2014x_driver = {
	.driver = {
		.owner	= THIS_MODULE,
		.name	= tda2014x_id->name,
	},
	.probe		= tda2014x_probe,
	.remove		= tda2014x_remove,
	.id_table	= tda2014x_id,
};
module_i2c_driver(tda2014x_driver);

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <info@are.ma>");
MODULE_DESCRIPTION("Driver for NXP Semiconductors tuner TDA2014x");
MODULE_LICENSE("GPL");
