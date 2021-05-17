/*
	CDEV driver for PLEX PX-Q3PE ISDB-S/T PCIE receiver

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>

	Main components:
	ASIE5606X8	- controller
	TC90522		- 2ch OFDM ISDB-T + 2ch 8PSK ISDB-S demodulator
	TDA20142	- ISDB-S tuner
	NM120		- ISDB-T tuner
*/

#include <linux/pci.h>
#include <linux/interrupt.h>
#include <media/dvb_frontend.h>
#include "ptx_common.h"
#include "tc90522.h"

#define MOD_AUTH "Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>"
MODULE_AUTHOR(MOD_AUTH);
MODULE_DESCRIPTION("PLEX PX-Q3PE Driver");
MODULE_LICENSE("GPL");

static struct pci_device_id pxq3pe_id_table[] = {
	{0x188B, 0x5220, 0x0B06, 0x0002, 0, 0, 0},
	{}
};
MODULE_DEVICE_TABLE(pci, pxq3pe_id_table);

enum eUserCommand {
	START_REC		= 0x8D02,
	STOP_REC		= 0x8D03,
	LNB_DISABLE		= 0x8D06,
	LNB_ENABLE		= 0x40048D05,
	SET_CHANNEL		= 0x40088D01,
	GEN_ENC_SEED		= 0x40088D83,
	MULTI2_ENABLE		= 0x40088D84,
	SET_PROGRAM_ID		= 0x40088D86,
	SET_BCAS_COMMAND	= 0x40088D87,
	GET_SIGNAL_STRENGTH	= 0x80088D04,
	GET_DRV_SUPPORT		= 0x80088D81,
	GET_RANDOM_KEY		= 0x80088D82,
	DECRYP_MULTI_TS		= 0x80088D85,
	GET_BCAS_COMMAND	= 0x80088D88,
};

enum ePXQ3PE {
	PXQ3PE_MAXCARD	= 8,
	PXQ3PE_ADAPN	= 8,
	PKT_BYTES	= 188,
	PKT_BUFLEN	= PKT_BYTES * 312,

	PXQ3PE_MOD_GPIO		= 0,
	PXQ3PE_MOD_TUNER	= 1,
	PXQ3PE_MOD_STAT		= 2,

	PXQ3PE_INT_STAT		= 0x808,
	PXQ3PE_INT_CLEAR	= 0x80C,
	PXQ3PE_INT_ACTIVE	= 0x814,
	PXQ3PE_INT_DISABLE	= 0x818,
	PXQ3PE_INT_ENABLE	= 0x81C,

	PXQ3PE_I2C_ADR_GPIO	= 0x4A,
	PXQ3PE_I2C_CTL_STAT	= 0x940,
	PXQ3PE_I2C_ADR		= 0x944,
	PXQ3PE_I2C_SW_CTL	= 0x948,
	PXQ3PE_I2C_FIFO_STAT	= 0x950,
	PXQ3PE_I2C_FIFO_DATA	= 0x960,

	PXQ3PE_DMA_OFFSET_PORT	= 0x140,
	PXQ3PE_DMA_TSMODE	= 0xA00,
	PXQ3PE_DMA_MGMT		= 0xAE0,
	PXQ3PE_DMA_OFFSET_CH	= 0x10,
	PXQ3PE_DMA_ADR_LO	= 0xAC0,
	PXQ3PE_DMA_ADR_HI	= 0xAC4,
	PXQ3PE_DMA_XFR_STAT	= 0xAC8,
	PXQ3PE_DMA_CTL		= 0xACC,
};

struct pxq3pe_card {
	struct mutex		lock;
	struct i2c_adapter	i2c;
	struct pci_dev		*pdev;
	dev_t			dev;
	void __iomem		*bar;
	char			*name;
	u32			base_minor;
	bool			lnb,
				irq_enabled;
	struct {
		dma_addr_t	adr;
		u8		*dat;
		u32		sz;
		bool		ON[2];
	} dma;
	u8			adapn;
	struct pxq3pe_adap	*adap;
};

struct pxq3pe_adap {
	struct mutex		lock;
	struct pxq3pe_card	*card;
	u8			tBuf[PKT_BUFLEN],
				*sBuf;
	u32			tBufIdx,
				sBufSize,
				sBufStart,
				sBufStop,
				sBufByteCnt,
				minor;
	struct cdev		cdev;
	bool			ON;
	struct dvb_adapter	dvb;
	struct dvb_demux	demux;
	struct dmxdev		dmxdev;
	struct i2c_client	*demod,
				*tuner;
	struct dvb_frontend	fe;
};

struct pxq3pe_card	*gCard[PXQ3PE_MAXCARD];
struct class		*pxq3pe_class;

bool pxq3pe_w(struct pxq3pe_card *card, u8 slvadr, u8 regadr, u8 *wdat, u8 bytelen, u8 mode)
{
	void __iomem	*bar	= card->bar;
	int	i,
		j,
		k;
	u8	i2cCtlByte,
		i2cFifoWSz;

pr_debug("%s slvadr %02x regadr %02x wdat %02x len %d mode %02x", __func__, slvadr, regadr, wdat[0], bytelen, mode);
	if ((readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F) != 0x10 || readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F00)
		return false;
	writel(0, bar + PXQ3PE_I2C_CTL_STAT);
	switch (mode) {
	case PXQ3PE_MOD_GPIO:
		i2cCtlByte = 0xC0;
		break;
	case PXQ3PE_MOD_TUNER:
		slvadr = 2 * slvadr + 0x20;
		regadr = 0;
		i2cCtlByte = 0x80;
		break;
	case PXQ3PE_MOD_STAT:
		slvadr = 2 * slvadr + 0x20;
		regadr = 0;
		i2cCtlByte = 0x84;
		break;
	default:
		return false;
	}
	writel((slvadr << 8) + regadr, bar + PXQ3PE_I2C_ADR);
	for (i = 0; i < 16 && i < bytelen; i += 4) {
		msleep(0);
		writel(*((u32 *)(wdat + i)), bar + PXQ3PE_I2C_FIFO_DATA);
	}
	writew((bytelen << 8) + i2cCtlByte, bar + PXQ3PE_I2C_CTL_STAT);
	for (j = 0; j != 1000; j++) {
		if (i < bytelen) {
			i2cFifoWSz = readb(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F;
			for (k = 0; bytelen > 16 && k < 500 && i2cFifoWSz < bytelen - 16; k++) {
				i2cFifoWSz = readb(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F;
				msleep(0);
			}
			if (i2cFifoWSz & 3)
				continue;
			if (i2cFifoWSz) {
				for (k = i; k < bytelen && k - i < i2cFifoWSz; k += 4)
					writel(*((u32 *)(wdat + k)), bar + PXQ3PE_I2C_FIFO_DATA);
				i = k;
			}
		}
		udelay(10);
		if (readl(bar + PXQ3PE_I2C_CTL_STAT) & 0x400000)
			break;
	}
	return j < 1000 ? !(readl(bar + PXQ3PE_I2C_CTL_STAT) & 0x280000) : false;
}

bool pxq3pe_r(struct pxq3pe_card *card, u8 slvadr, u8 regadr, u8 *rdat, u8 bytelen, u8 mode)
{
	void __iomem	*bar	= card->bar;
	u8	buf[4]		= {0},
		i2cCtlByte,
		i2cStat,
		i2cFifoRSz,
		i2cByteCnt;
	int	i		= 0,
		j,
		idx;
	bool	ret		= false;

pr_debug("%s slvadr %02x regadr %02x rdat %02x len %d mode %02x", __func__, slvadr, regadr, rdat[0], bytelen, mode);
	if ((readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F) != 0x10 || readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F00)
		return false;
	writel(0, bar + PXQ3PE_I2C_CTL_STAT);
	switch (mode) {
	case PXQ3PE_MOD_GPIO:
		i2cCtlByte = 0xE0;
		break;
	case PXQ3PE_MOD_TUNER:
		slvadr = 2 * slvadr + 0x20;
		regadr = 0;
		i2cCtlByte = 0xA0;
		break;
	case PXQ3PE_MOD_STAT:
		*buf = regadr;
		return	pxq3pe_w(card, slvadr, 0, buf, 1, PXQ3PE_MOD_TUNER)	&&
			pxq3pe_r(card, slvadr, 0, rdat, bytelen, PXQ3PE_MOD_TUNER);
	default:
		return false;
	}
	writel((slvadr << 8) + regadr, bar + PXQ3PE_I2C_ADR);
	writew(i2cCtlByte + (bytelen << 8), bar + PXQ3PE_I2C_CTL_STAT);
	i2cByteCnt = bytelen;
	j = 0;
	while (j < 500) {
		udelay(10);
		i2cStat = (readl(bar + PXQ3PE_I2C_CTL_STAT) & 0xFF0000) >> 16;
		if (i2cStat & 0x80) {
			if (i2cStat & 0x28)
				break;
			ret = true;
		}
		i2cFifoRSz = (readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F00) >> 8;
		if (i2cFifoRSz & 3) {
			++j;
			continue;
		}
		for (idx = i; i2cFifoRSz && idx < i2cByteCnt && idx - i < i2cFifoRSz; idx += 4)
			*(u32 *)(rdat + idx) = readl(bar + PXQ3PE_I2C_FIFO_DATA);
		i = idx;
		if (i < bytelen) {
			if (i2cFifoRSz)
				i2cByteCnt -= i2cFifoRSz;
			else
				++j;
			continue;
		}
		i2cStat = (readl(bar + PXQ3PE_I2C_CTL_STAT) & 0xFF0000) >> 16;
		if (i2cStat & 0x80) {
			if (i2cStat & 0x28)
				break;
			ret = true;
			break;
		}
		++j;
	}
	return !(readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F00) && ret;
}

int pxq3pe_i2c_xfr(struct i2c_adapter *i2c, struct i2c_msg *msg, int sz)
{
	struct pxq3pe_card *card = i2c_get_adapdata(i2c);
	u8	i;
	bool	ret	= true;

	if (!i2c || !card || !msg)
		return -EINVAL;
	for (i = 0; i < sz && ret; i++, msg++) {
		u8	regadr	= msg->addr >> 8,
			slvadr	= msg->addr & 0xFF,
			mode	= slvadr == PXQ3PE_I2C_ADR_GPIO	? PXQ3PE_MOD_GPIO
				: slvadr & 0x80			? PXQ3PE_MOD_STAT
				: PXQ3PE_MOD_TUNER;

		mutex_lock(&card->lock);
		if (msg->flags & I2C_M_RD) {
			u8 *buf = kzalloc(sz, GFP_KERNEL);

			if (!buf)
				return -ENOMEM;
			ret = pxq3pe_r(card, slvadr, regadr, buf, msg->len, mode);
			memcpy(msg->buf, buf, msg->len);
			kfree(buf);
		} else
			ret = pxq3pe_w(card, slvadr, regadr, msg->buf, msg->len, mode);
		mutex_unlock(&card->lock);
	}
	return i;
}


struct tc90522 {
	struct dvb_frontend *fe;
	struct i2c_adapter *i2c;
};

bool tc90522_i2c_r_1(struct dvb_frontend *fe, u8 slvadr, u8 *rdat)
{
	struct tc90522 *d = fe->demodulator_priv;
	struct i2c_msg msg[] = {
		{.addr = 0x80 | fe->id,	.flags = 0,		.buf = &slvadr,	.len = 1,},
		{.addr = fe->id,	.flags = I2C_M_RD,	.buf = rdat,	.len = 1,},
	};

	return i2c_transfer(d->i2c, msg, 2) == 2;
}

bool tc90522_i2c_w_tuner(struct dvb_frontend *fe, u8 slvadr, u8 dat)
{
	struct tc90522 *d = fe->demodulator_priv;
	u8 buf[] = {slvadr, dat};
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = 2,},
	};

pr_debug("%s 00 fe=%p slvadr=0x%x dat=0x%x .addr=0x%x", __func__, fe, slvadr, dat, msg[0].addr);
	return i2c_transfer(d->i2c, msg, 1) == 1;
}

bool tc90522_chk_lock(struct dvb_frontend *fe)
{
	u8 tc90522_get_quality(void)
	{
		u8	byte1,
			byte2,
			Data;
		u32	BerReg,
			BER	= 0xFFFFFFFF;

		if (fe->dtv_property_cache.delivery_system == SYS_ISDBS) {	/* PSK */
			if (tc90522_i2c_r_1(fe, 0xC3, &Data)
				&& !(Data & 0x10)
				&& tc90522_i2c_r_1(fe, 0xEB, &Data)) {
				byte2 = Data;
				if (tc90522_i2c_r_1(fe, 0xEC, &Data)) {
					byte1 = Data;
					if (tc90522_i2c_r_1(fe, 0xED, &Data)) {
						BerReg = Data | ((byte1 | (byte2 << 8)) << 8);
						BER = 80 * BerReg;
					}
				}
			}
		} else if (tc90522_i2c_w_tuner(fe, 0xBA, 0) == 1
					 && tc90522_i2c_r_1(fe, 0xB0, &Data)
					 && (Data & 0xF) > 7
					 && tc90522_i2c_r_1(fe, 0xA0, &Data)) {
			byte2 = Data;
			if (tc90522_i2c_r_1(fe, 0xA1, &Data)) {
				byte1 = Data;
				if (tc90522_i2c_r_1(fe, 0xA2, &Data)) {
					BerReg = Data | ((byte1 | (byte2 << 8)) << 8);
					if (tc90522_i2c_r_1(fe, 0xA8, &Data)) {
						byte1 = Data;
						if (tc90522_i2c_r_1(fe, 0xA9, &Data)) {
							u64 CycleReg = Data | (byte1 << 8);
							if (CycleReg)
								BER = 0xEF5A / CycleReg * BerReg;
						}
					}
				}
			}
		}
		return	BER > 1000000	? 5	:
			BER > 500000	? 10	:
			BER > 250000	? 20	:
			BER > 100000	? 30	:
			BER > 50000	? 40	:
			BER > 10000	? 50	:
			BER > 1000	? 60	:
			BER > 100	? 70	:
			BER > 10	? 80	:
			BER ? 90	: 100;
	}
	bool	lock;
	u8	d0 = 0,
		d1 = 0;

	lock =	fe->dtv_property_cache.delivery_system == SYS_ISDBS	?
		(tc90522_i2c_r_1(fe, 0xC3, &d0), !(d0 & 0x10))	&&	/* sat */
		tc90522_get_quality() > 9			:
		(tc90522_i2c_r_1(fe, 0x80, &d0), (d0 & 8) == 0)	&&	/* ter */
		(tc90522_i2c_r_1(fe, 0xB0, &d1), (d1 & 0xF) > 7);
	return lock;
}

u32 tc90522_get_cn(struct dvb_frontend *fe)
{
	struct tc90522 *d = fe->demodulator_priv;
	u8	cn[3]	= {0};
	struct i2c_msg msg[] = {
		{.addr = (0x80 | fe->id) + (0xBC << 8),	.flags = I2C_M_RD,	.buf = cn,	.len = 2,},
		{.addr = (0x80 | fe->id) + (0x8B << 8),	.flags = I2C_M_RD,	.buf = cn,	.len = 3,},
	};

	return	!tc90522_chk_lock(fe) ? 0	:
		fe->dtv_property_cache.delivery_system == SYS_ISDBS		?
		(i2c_transfer(d->i2c, msg, 1) == 1 ? (cn[0] << 8) | cn[1] : 0)	:
		(i2c_transfer(d->i2c, msg + 1, 1) == 1 ? (cn[0] << 16) | (cn[1] << 8) | cn[2] : 0);
}

bool pxq3pe_w_gpio2(struct pxq3pe_card *card, u8 dat, u8 mask)
{
	u8	val;
	struct i2c_msg msg[] = {
		{.addr = PXQ3PE_I2C_ADR_GPIO + (0xB << 8),	.flags = I2C_M_RD,	.buf = &val,	.len = 1,},
		{.addr = PXQ3PE_I2C_ADR_GPIO + (0xB << 8),	.flags = 0,		.buf = &val,	.len = 1,},
	};

dev_dbg(&card->pdev->dev, "%s", __func__);
	return	i2c_transfer(&card->i2c, msg, 1) == 1	&&
		(val = (mask & dat) | (val & ~mask), i2c_transfer(&card->i2c, msg + 1, 1) == 1);
}

void pxq3pe_w_gpio1(struct pxq3pe_card *card, u8 dat, u8 mask)
{
	mask <<= 3;
	writeb((readb(card->bar + 0x890) & ~mask) | ((dat << 3) & mask), card->bar + 0x890);
}

void pxq3pe_w_gpio0(struct pxq3pe_card *card, u8 dat, u8 mask)
{
	writeb((-(mask & 1) & 4 & -(dat & 1)) | (readb(card->bar + 0x890) & ~(-(mask & 1) & 4)), card->bar + 0x890);
	writeb((mask & dat) | (readb(card->bar + 0x894) & ~mask), card->bar + 0x894);
}

void tc90522_lnb(struct pxq3pe_card *card)
{
	int	i;
	bool	lnb = false;

	for (i = 0; i < card->adapn; i++)
		if (card->adap[i].fe.dtv_property_cache.delivery_system == SYS_ISDBS && card->adap[i].ON) {
			lnb = true;
			break;
		}
	if (card->lnb != lnb) {
		pxq3pe_w_gpio2(card, lnb ? 0x20 : 0, 0x20);
		card->lnb = lnb;
	}
}

void tc90522_power(struct pxq3pe_card *card, bool bON)
{
dev_dbg(&card->pdev->dev, "%s IN %d", __func__, bON);
	if (bON) {
dev_dbg(&card->pdev->dev, "%s 00", __func__);
		pxq3pe_w_gpio0(card, 1, 1);
		pxq3pe_w_gpio0(card, 0, 1);
		pxq3pe_w_gpio0(card, 1, 1);
dev_dbg(&card->pdev->dev, "%s 01", __func__);
		pxq3pe_w_gpio1(card, 1, 1);
		pxq3pe_w_gpio1(card, 0, 1);
dev_dbg(&card->pdev->dev, "%s 02", __func__);
		pxq3pe_w_gpio2(card, 2, 2);
		pxq3pe_w_gpio2(card, 0, 2);
		pxq3pe_w_gpio2(card, 2, 2);
		pxq3pe_w_gpio2(card, 4, 4);
		pxq3pe_w_gpio2(card, 0, 4);
		pxq3pe_w_gpio2(card, 4, 4);
	} else {
		pxq3pe_w_gpio0(card, 0, 1);
		pxq3pe_w_gpio0(card, 1, 1);
		pxq3pe_w_gpio1(card, 1, 1);
	}
dev_dbg(&card->pdev->dev, "%s OUT %d", __func__, bON);
}

int tc90522_remove(struct i2c_client *c)
{
dev_dbg(&c->dev, "%s\n", __func__);
	kfree(i2c_get_clientdata(c));
	return 0;
}

int tc90522_probe(struct i2c_client *c, const struct i2c_device_id *id)
{
	struct tc90522		*d	= kzalloc(sizeof(struct tc90522), GFP_KERNEL);
	struct dvb_frontend	*fe	= c->dev.platform_data;

	if (!d)
		return -ENOMEM;
	d->fe	= fe;
	d->i2c	= c->adapter;
	fe->demodulator_priv = d;
	i2c_set_clientdata(c, d);

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
/*module_i2c_driver(tc90522_driver); */


#include "tda2014x.h"

struct tda2014x {
	struct dvb_frontend *fe;
	struct i2c_adapter *i2c;
	u64 f_kHz;
};

int tda2014x_r(struct dvb_frontend *fe, u8 slvadr)
{
	struct tda2014x *t = fe->tuner_priv;
	u8	buf[]	= {0xFE, 0xA8, slvadr},
		rcmd[]	= {0xFE, 0xA9},
		ret	= 0;
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,		.buf = buf,	.len = 3,},
		{.addr = 0x80 | fe->id,	.flags = 0,		.buf = rcmd,	.len = 2,},
		{.addr = fe->id,	.flags = I2C_M_RD,	.buf = &ret,	.len = 1,},
	};

	return i2c_transfer(t->i2c, msg, 3) == 3 ? ret : -EREMOTEIO;
}

bool tda2014x_r8(struct dvb_frontend *fe, u16 slvadr, u8 start_bit, u8 nbits, u8 *rdat)
{
	u8	mask	= nbits > 7 ? 0xFF : ((1 << nbits) - 1) << start_bit,
		val	= tda2014x_r(fe, slvadr);

	if (val < 0)
		return false;
	*rdat = (val & mask) >> start_bit;
	return true;
}

bool tda2014x_w16(struct dvb_frontend *fe, u16 slvadr, u8 start_bit, u8 nbits, u8 nbytes, bool rmw, u8 access, u16 wdat)
{
	struct tda2014x *t = fe->tuner_priv;
	u16	mask	= nbits > 15 ? 0xFFFF : ((1 << nbits) - 1) << start_bit,
		val	= mask & (wdat << start_bit);
	u8	*wval	= (u8 *)&val,
		i;

	for (i = 0, nbytes = !nbytes ? 1 : nbytes > 2 ? 2 : nbytes; access & 2 && nbytes; i++, nbytes--) {
		u8	buf[]	= {0xFE, 0xA8, slvadr + i, 0},
			ret	= tda2014x_r(fe, slvadr + i);
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
	struct tda2014x *t = fe->tuner_priv;
	bool	bDoublerEnable[]		= {false, true, true, true, true},
		bDcc1Enable[]			= {false, true, true, true, true},
		bDcc2Enable[]			= {false, true, true, true, true},
		bPpfEnable[]			= {false, true, true, true, true},
		bDiv1ConfigInDivideBy3[]	= {false, true, false, true, false},
		bDiv2ConfigInDivideBy3[]	= {false, true, true, false, false},
		bSelectDivideBy4Or5Or6Or7Path[]	= {false, true, true, true, true},
		bSelectDivideBy8Path[]		= {true, false, false, false, false},
		bInputMuxEnable;
	u8	PredividerRatio,
		val;
	u64	f_kHz = fe->dtv_property_cache.frequency,
		ResLsb,
		Premain,
		ulCalcPrecision = 1000000,
		kint,
		Nint,
		ulR,
		DsmFracInReg,
		DsmIntInReg,
		v15;
	int	ePllRefClkRatio,
		i = f_kHz <= 1075000 ? 0 : f_kHz <= 1228000 ? 1 : f_kHz <= 1433000 ? 2 : f_kHz <= 1720000 ? 3 : 4;

	if (t->f_kHz == f_kHz)
		return 0;
	if (f_kHz > fe->ops.tuner_ops.info.frequency_max_hz)
		return -ERANGE;

	/* GetLoConfig */
	if (!tda2014x_r8(fe, 0x25, 3, 1, &val))
		return -EIO;
	bInputMuxEnable = val;

	/* SetLoConfig */
	val = tda2014x_w16(fe, 0x22, 0, 8, 0, 0, 6,
		(bDoublerEnable[i] << 7) | (bDcc1Enable[i] << 6) | (bDcc2Enable[i] << 5) | 0b11110 | bPpfEnable[i]) &&
	tda2014x_r8(fe, 0x23, 0, 8, &val) &&
	tda2014x_w16(fe, 0x23, 0, 8, 0, 0, 6, (bDiv1ConfigInDivideBy3[i] << 7) | (bDiv2ConfigInDivideBy3[i] << 5) |
		(bSelectDivideBy4Or5Or6Or7Path[i] << 3) | (bSelectDivideBy8Path[i] << 2) | (val & 0b1010011)) &&
	tda2014x_w16(fe, 0x25, 3, 1, 0, 1, 6, bInputMuxEnable);

	ResLsb = (8 - i) * f_kHz * 1000 / 27;	/* Xtal 27 MHz */
	kint = ResLsb;
	v15 = ResLsb / 1000000;
	ulR = 1;
	Premain = 2;
	Nint = v15 * ulR / Premain;
	if (Nint < 131) {
		Premain = 1;
		Nint = v15 * ulR / Premain;
		if (Nint > 251) {
			ulR = 3;
			Premain = 4;
			goto LABEL_36;
		}
		if (Nint < 131) {
			ulR = 3;
			Premain = 2;
			goto LABEL_36;
		}
	} else if (Nint > 251) {
		Premain = 4;
		Nint = v15 * ulR / Premain;
		if (Nint > 251) {
			ulR = 3;
			Premain = 4;
		}
LABEL_36:
		Nint = v15 * ulR / Premain;
		if (Nint < 131 || Nint > 251)
			return -ERANGE;
	}
	switch (100 * ulR / Premain) {
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
	kint		= (kint / 10) * 10;
	ePllRefClkRatio	= ulR == 2 ? 1 : ulR == 3 ? 2 : 0;
	PredividerRatio	= Premain == 2 ? 0 : 1;
	DsmIntInReg	= kint / 1000000;
	DsmFracInReg	= kint - 1000000 * DsmIntInReg;
	for (i = 0; i < 16; i++) {
		DsmFracInReg *= 2;
		if (DsmFracInReg > 0xFFFFFFF && i != 15) {
			DsmFracInReg /= 10;
			ulCalcPrecision /= 10;
		}
	}
	t->f_kHz = f_kHz;
	return	-EIO * !(tda2014x_w16(fe, 3, 6, 2, 0, 1, 6, ePllRefClkRatio)	&&

		/* SetPllDividerConfig */
		tda2014x_w16(fe, 0x1A, 5, 1, 0, 1, 6, PredividerRatio)				&&
		tda2014x_w16(fe, 0x1E, 0, 8, 0, 0, 6, DsmIntInReg - 128)			&&
		tda2014x_w16(fe, 0x1F, 0, 0x10, 2, 0, 6, DsmFracInReg / ulCalcPrecision)	&&

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
		tda2014x_w16(fe, 6, 0, 8, 0, 0, 6, (val & 0x48) | 0b10100010)	&&
		tda2014x_r8(fe, 9, 0, 8, &val)					&&
		tda2014x_w16(fe, 9, 0, 8, 0, 0, 6, 0b10110000 | (val & 3))	&&
		tda2014x_w16(fe, 0xA, 5, 3, 0, 1, 6, 3)				&&
		tda2014x_w16(fe, 0xC, 4, 4, 0, 1, 6, 3));
}

static struct dvb_tuner_ops tda2014x_ops = {
	.info = {
		.frequency_min_hz	= 1,		/* freq under 1024 kHz is handled as ch */
		.frequency_max_hz	= 3000000,	/* kHz */
		.frequency_step_hz	= 1000,		/* = 1 MHz */
	},
	.set_params = tda2014x_tune,
};

int tda2014x_remove(struct i2c_client *c)
{
dev_dbg(&c->dev, "%s\n", __func__);
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
	t->fe	= fe;
	t->i2c	= c->adapter;
	fe->tuner_priv = t;
	memcpy(&fe->ops.tuner_ops, &tda2014x_ops, sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(c, t);
	fe->dtv_property_cache.frequency = 1318000;

	val = tc90522_i2c_w_tuner(fe, 0x13, 0)	&&	/* sat */
	tc90522_i2c_w_tuner(fe, 0x15, 0)	&&
	tc90522_i2c_w_tuner(fe, 0x17, 0)	&&
	tc90522_i2c_w_tuner(fe, 0x1C, 0)	&&
	tc90522_i2c_w_tuner(fe, 0x1D, 0)	&&
	tc90522_i2c_w_tuner(fe, 0x1F, 0)	&&
	(tc90522_i2c_w_tuner(fe, 7, 0x31)	,
	tc90522_i2c_w_tuner(fe, 8, 0x77)	,
	tc90522_i2c_w_tuner(fe, 4, 2))		&&

	/* SetPowerMode */
	tda2014x_r8(fe, 2, 0, 8, &val)				&&
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
	.driver.name	= tda2014x_id->name,
	.probe		= tda2014x_probe,
	.remove		= tda2014x_remove,
	.id_table	= tda2014x_id,
};
/*module_i2c_driver(tda2014x_driver); */

#include "nm131.h"

struct nm131 {
	struct dvb_frontend *fe;
	struct i2c_adapter *i2c;
	u32 Hz;
};

bool nm131_w(struct dvb_frontend *fe, u16 slvadr, u32 val, u32 sz)
{
	struct nm131 *t = fe->tuner_priv;
	u8	buf[]	= {0xFE, 0xCE, slvadr >> 8, slvadr & 0xFF, 0, 0, 0, 0};
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = sz + 4,},
	};

	*(u32 *)(buf + 4) = slvadr == 0x36 ? val & 0x7F : val;
	return i2c_transfer(t->i2c, msg, 1) == 1;
}

bool nm131_r(struct dvb_frontend *fe, u16 slvadr, u8 *dat, u32 sz)
{
	struct nm131 *t = fe->tuner_priv;
	u8	rcmd[]	= {0xFE, 0xCF},
		*buf	= kzalloc(sz, GFP_KERNEL);
	struct i2c_msg msg[] = {
		{.addr = 0x80 | fe->id,	.flags = 0,		.buf = rcmd,	.len = 2,},
		{.addr = fe->id,	.flags = I2C_M_RD,	.buf = buf,	.len = sz,},
	};
	bool	ret;

	if (!buf)
		return false;
	ret = nm131_w(fe, slvadr, 0, 0) && i2c_transfer(t->i2c, msg, 2) == 2;
	memcpy(dat, buf, sz);
	kfree(buf);
	return ret;
}

int nm131_tune(struct dvb_frontend *fe)
{
	struct nm131 *t = fe->tuner_priv;
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

	if (t->Hz == tune_rf)
		return 0;
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

	/* demod_config */
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
	t->Hz = tune_rf;
	return 0;
}

static struct dvb_tuner_ops nm131_ops = {
	.info = {
		.frequency_min_hz	= 1,		/* freq under 90 MHz is handled as channel */
		.frequency_max_hz	= 770000000,	/* Hz */
		.frequency_step_hz	= 142857,
	},
	.set_params = nm131_tune,
};

int nm131_remove(struct i2c_client *c)
{
dev_dbg(&c->dev, "%s\n", __func__);
	kfree(i2c_get_clientdata(c));
	return 0;
}

int nm131_probe(struct i2c_client *c, const struct i2c_device_id *id)
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
	}
	const tnr_bb_defaults_lut[2] = {
		{356, 2048},	{448, 764156359}
	};
	u8			i;
	struct nm131		*t	= kzalloc(sizeof(struct nm131), GFP_KERNEL);
	struct dvb_frontend	*fe	= c->dev.platform_data;

	if (!t)
		return -ENOMEM;
	t->fe	= fe;
	t->i2c	= c->adapter;
	fe->tuner_priv = t;
	memcpy(&fe->ops.tuner_ops, &nm131_ops, sizeof(struct dvb_tuner_ops));
	i2c_set_clientdata(c, t);

	i = tc90522_i2c_w_tuner(fe, 0xB0, 0xA0)	&&	/* ter */
	tc90522_i2c_w_tuner(fe, 0xB2, 0x3D)	&&
	tc90522_i2c_w_tuner(fe, 0xB3, 0x25)	&&
	tc90522_i2c_w_tuner(fe, 0xB4, 0x8B)	&&
	tc90522_i2c_w_tuner(fe, 0xB5, 0x4B)	&&
	tc90522_i2c_w_tuner(fe, 0xB6, 0x3F)	&&
	tc90522_i2c_w_tuner(fe, 0xB7, 0xFF)	&&
	tc90522_i2c_w_tuner(fe, 0xB8, 0xC0)	&&
	tc90522_i2c_w_tuner(fe, 3, 0)		&&
	tc90522_i2c_w_tuner(fe, 0x1D, 0)	&&
	tc90522_i2c_w_tuner(fe, 0x1F, 0)	&&
	(tc90522_i2c_w_tuner(fe, 0xE, 0x77)	,
	tc90522_i2c_w_tuner(fe, 0xF, 0x13)	,
	tc90522_i2c_w_tuner(fe, 0x75, 2));

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
	.remove		= nm131_remove,
	.id_table	= nm131_id,
};
/*module_i2c_driver(nm131_driver); */


bool pxq3pe_tune(struct pxq3pe_adap *p, int fno, int slot)
{
	struct dvb_frontend *fe = &p->fe;
	u8	slvadr,
		tsid_lo,
		tsid_hi;
	u16	tsid[8];
	int	i;

	if (fe->dtv_property_cache.delivery_system == SYS_ISDBS) {
		u32 f_kHz = fno < 12 ? 11727480 + fno * 38360 : 12291000 + (fno - 12) * 40000;

		fe->dtv_property_cache.frequency = f_kHz - 10678000;
		i = tc90522_i2c_w_tuner(fe, 0xA, 0)	&&
		tc90522_i2c_w_tuner(fe, 0x10, 0xB0)	&&
		tc90522_i2c_w_tuner(fe, 0x11, 2)	&&
		tc90522_i2c_w_tuner(fe, 3, 1)		&&
		!tda2014x_tune(fe)			&&
		tc90522_i2c_w_tuner(fe, 0xA, 0xFF)	&&
		tc90522_i2c_w_tuner(fe, 0x10, 0xB2)	&&
		tc90522_i2c_w_tuner(fe, 0x11, 0)	&&
		tc90522_i2c_w_tuner(fe, 3, 1);
		msleep(150);	/* min 150ms */
		for (slvadr = 0xCE, i = 0; slvadr < 0xDE && i < ARRAY_SIZE(tsid); slvadr += 2, i++) {
			if (!tc90522_i2c_r_1(fe, slvadr, &tsid_hi) || !tc90522_i2c_r_1(fe, slvadr + 1, &tsid_lo))
				break;
			tsid[i] = (tsid_hi << 8) | tsid_lo;
dev_dbg(&p->card->pdev->dev, "%s TSID %04X", __func__, tsid[i]);
		}
		if (tsid[slot]) {
			i = tc90522_i2c_w_tuner(fe, 0x8F, tsid[slot] >> 8) &&
				tc90522_i2c_w_tuner(fe, 0x90, tsid[slot] & 0xFF);
		}
	} else {
		u32 f_MHz = fno > 112 ? 557 : 93 + 6 * fno + (fno < 12 ? 0 : fno < 17 ? 2 : fno < 63 ? 0 : 2);

		fe->dtv_property_cache.frequency = f_MHz * 1000000 + 142857;
		i = tc90522_i2c_w_tuner(fe, 1, 0x50)	&&
		tc90522_i2c_w_tuner(fe, 0x47, 0x30)	&&
		tc90522_i2c_w_tuner(fe, 0x25, 0)	&&
		tc90522_i2c_w_tuner(fe, 0x20, 0)	&&
		tc90522_i2c_w_tuner(fe, 0x23, 0x4D)	&&
		!nm131_tune(fe)				&&
		tc90522_i2c_w_tuner(fe, 0x23, 0x4C)	&&
		tc90522_i2c_w_tuner(fe, 1, 0x50)	&&
		tc90522_i2c_w_tuner(fe, 0x71, 1)	&&
		tc90522_i2c_w_tuner(fe, 0x72, 0x24);
	}
	for (i = 0; i < 500; i++) {
		if (tc90522_chk_lock(fe))
			return true;
		msleep_interruptible(10);
	}
	return false;
}

irqreturn_t pxq3pe_irq(int irq, void *ctx)
{
	struct pxq3pe_card	*card	= ctx;
	void __iomem		*bar	= card->bar;
	u32	dmamgmt,
		i,
		intstat = readl(bar + PXQ3PE_INT_STAT);
	bool	ch	= intstat & 0b0101 ? 0 : 1,
		port	= intstat & 0b0011 ? 0 : 1;
	u8	*tbuf	= card->dma.dat + PKT_BUFLEN * (port * 2 + ch);

	void pxq3pe_dma_put_stream(struct pxq3pe_adap *p) {
		u8	*src	= p->tBuf;
		u32	len	= p->tBufIdx,
			savesz	= len <= p->sBufSize - p->sBufStop ? len : p->sBufSize - p->sBufStop,
			remain	= len - savesz;

		memcpy(&p->sBuf[p->sBufStop], src, savesz);
		if (remain)
			memcpy(p->sBuf, &src[savesz], remain);
		p->sBufStop = (p->sBufStop + len) % p->sBufSize;
		if (p->sBufByteCnt == p->sBufSize)
			p->sBufStart = p->sBufStop;
		else {
			if (p->sBufSize >= p->sBufByteCnt + len)
				p->sBufByteCnt += len;
			else {
				p->sBufStart = p->sBufStop;
				p->sBufByteCnt = p->sBufSize;
			}
		}
	}

	if (!(intstat & 0b1111))
		return IRQ_HANDLED;
	writel(intstat, bar + PXQ3PE_INT_CLEAR);
	dmamgmt = readl(bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	if ((readl(bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * ch + PXQ3PE_DMA_XFR_STAT) & 0x3FFFFF) == PKT_BUFLEN)
		for (i = 0; i < PKT_BUFLEN; i += PKT_BYTES) {
		u8 i2cadr = !port * 4 + (tbuf[i] == 0xC7 ? 0 : tbuf[i] == 0x47 ?
					1 : tbuf[i] == 0x07 ? 2 : tbuf[i] == 0x87 ? 3 : card->adapn);
		struct pxq3pe_adap *p = &card->adap[i2cadr];

		if (i2cadr < card->adapn) {
			tbuf[i] = PTX_TS_SYNC;
			memcpy(&p->tBuf[p->tBufIdx], &tbuf[i], PKT_BYTES);
			p->tBufIdx += PKT_BYTES;
			if (p->tBufIdx >= PKT_BUFLEN) {
				pxq3pe_dma_put_stream(p);
				p->tBufIdx = 0;
			}
		}
	}
	if (card->dma.ON[port])
		writel(dmamgmt | (2 << (ch * 16)), bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);

	return IRQ_HANDLED;
}

bool pxq3pe_dma_start(struct pxq3pe_adap *p)
{
	u8	i2cadr	= p->fe.id,
		i;
	bool	port	= !(i2cadr & 4);
	u32	val	= 0b0011 << (port * 2);
	struct pxq3pe_card	*card	= p->card;

	p->sBufByteCnt	= 0;
	p->sBufStop	= 0;
	p->sBufStart	= 0;
	if (card->dma.ON[port])
		return true;

	/* SetTSMode */
	i = readb(card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_TSMODE);
	if ((i & 0x80) == 0)
		writeb(i | 0x80, card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_TSMODE);

	/* irq_enable */
	writel(val, card->bar + PXQ3PE_INT_ENABLE);
	if (val != (readl(card->bar + PXQ3PE_INT_ACTIVE) & val))
		return false;

	/* cfg_dma */
	for (i = 0; i < 2; i++) {
		val = readl(		card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
		writel(card->dma.adr + PKT_BUFLEN * (port * 2 + i),
					card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * i + PXQ3PE_DMA_ADR_LO);
		writel(0,		card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * i + PXQ3PE_DMA_ADR_HI);
		writel(0x11C0E520,	card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * i + PXQ3PE_DMA_CTL);
		writel(val | 3 << (i * 16),
					card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	}
	card->dma.ON[port] = true;
	return true;
}

void pxq3pe_dma_stop(struct pxq3pe_adap *p)
{
	struct pxq3pe_card	*card	= p->card;
	u8			i2cadr	= p->fe.id,
				i;
	bool			port	= !(i2cadr & 4);

	for (i = 0; i < card->adapn; i++)
		if (!card->dma.ON[port] || (i2cadr != i && (i & 4) == (i2cadr & 4) && card->dma.ON[port]))
			return;

	/* cancel_dma */
	i = readb(card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	if ((i & 0b1100) == 4)
		writeb(i & 0xFD, card->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	writeb(0b0011 << (port * 2), card->bar + PXQ3PE_INT_DISABLE);
	card->dma.ON[port] = false;
}

ssize_t pxq3pe_read(struct file *file, char *out, size_t maxlen, loff_t *ppos)
{
	size_t			rlen	= (maxlen / PKT_BYTES) * PKT_BYTES;
	struct pxq3pe_adap	*p	= file->private_data;
	u8			*rbuf	= (u8 *)kzalloc(rlen, GFP_ATOMIC),
				xor[]	= {0x2F, 0x46, 0x56, 0xE3};
	int			sz	= p->sBufSize - p->sBufStart,
				i	= 0,
				j	= 0;

	if (file && out && rbuf && p->sBufByteCnt >= rlen) {
		mutex_lock(&p->lock);
		if (rlen <= sz)
			memcpy(rbuf, &p->sBuf[p->sBufStart], rlen);
		else {
			memcpy(rbuf, &p->sBuf[p->sBufStart], sz);
			memcpy(rbuf + sz, p->sBuf, rlen - sz);
		}
		p->sBufStart = (p->sBufStart + rlen) % p->sBufSize;
		p->sBufByteCnt -= rlen;
		mutex_unlock(&p->lock);
		while (j < rlen / PKT_BYTES) {
			j++;
			i += 4;
			while (i < j * PKT_BYTES) {
				rbuf[i] ^= xor[0]; i++;
				rbuf[i] ^= xor[0]; i++;
				rbuf[i] ^= xor[3]; i++;
				rbuf[i] ^= xor[1]; i++;
				rbuf[i] ^= xor[0]; i++;
				rbuf[i] ^= xor[2]; i++;
				rbuf[i] ^= xor[1]; i++;
				rbuf[i] ^= xor[2]; i++;
			}
		}
		sz = copy_to_user(out, rbuf, rlen);
	} else
		rlen = 0;
	kfree(rbuf);
	return rlen;
}

long pxq3pe_ioctl(struct file *file, enum eUserCommand cmd, unsigned long arg0)
{
	u32	val;
	struct sFREQUENCY {
		int fno;
		int slot;
	}	freq;
	struct pxq3pe_adap	*p	= file->private_data;
	void			*arg	= (void *)arg0;
	struct dvb_frontend	*fe	= &p->fe;

	switch (cmd) {
	case GEN_ENC_SEED:
	case MULTI2_ENABLE:
	case SET_PROGRAM_ID:
	case SET_BCAS_COMMAND:
	case GET_RANDOM_KEY:
	case DECRYP_MULTI_TS:
	case GET_BCAS_COMMAND:
	case LNB_DISABLE:
	case LNB_ENABLE:
		return 0;
	case GET_DRV_SUPPORT:
		val = 0x43;
		val = copy_to_user(arg, &val, 1);
		return 0;
	case START_REC:
		pxq3pe_dma_start(p);
		return 0;
	case STOP_REC:
		pxq3pe_dma_stop(p);
		return 0;
	case GET_SIGNAL_STRENGTH:
		val = tc90522_get_cn(fe);
		val = copy_to_user(arg, &val, 4);
		return 0;
	case SET_CHANNEL:
		val = copy_from_user(&freq, arg, sizeof(freq));
		if (pxq3pe_tune(p, freq.fno, freq.slot))
			return 0;
	}
	return -EINVAL;
}

int pxq3pe_open(struct inode *inode, struct file *file)
{
	int	major	= imajor(inode),
		minor	= iminor(inode),
		i,
		j;

	for (i = 0; inode && file && i < PXQ3PE_MAXCARD; i++) {
		struct pxq3pe_card *card = gCard[i];

		if (!card)
			continue;
		if (MAJOR(card->dev) == major && card->base_minor <= minor && minor < card->base_minor + card->adapn) {
			for (j = 0; j < card->adapn; j++) {
				struct pxq3pe_adap *p = &card->adap[j];

				if (p->minor == minor) {
					if (p->ON) {
						dev_err(&card->pdev->dev, "%s device in use", __func__);
						return -EIO;
					}
					file->private_data = p;
					p->ON = true;
					tc90522_lnb(card);
					return 0;
				}
			}
		}
	}
	return -EIO;
}

int pxq3pe_release(struct inode *inode, struct file *file)
{
	struct pxq3pe_adap	*p	= file->private_data;

	if (!inode || !file)
		return -EIO;
	p->ON = false;
	tc90522_lnb(p->card);
	return 0;
}

static const struct file_operations pxq3pe_fops = {
	.owner		= THIS_MODULE,
	.llseek		= no_llseek,
	.unlocked_ioctl	= pxq3pe_ioctl,
	.compat_ioctl	= pxq3pe_ioctl,
	.read		= pxq3pe_read,
	.open		= pxq3pe_open,
	.release	= pxq3pe_release,
};

void pxq3pe_remove(struct pci_dev *pdev)
{
	struct pxq3pe_card	*card	= pci_get_drvdata(pdev);
	u8	i,
		WtEncCtlReg = 0;

	if (!card)
		return;
	for (i = 0; i < card->adapn && card->adap; i++) {
		pxq3pe_dma_stop(&card->adap[i]);
		card->adap[i].ON = false;
		cdev_del(&card->adap[i].cdev);
		device_destroy(pxq3pe_class, MKDEV(MAJOR(card->dev), MINOR(card->dev) + i));
	}
	pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 0x80, &WtEncCtlReg, 1, PXQ3PE_MOD_GPIO);
	tc90522_lnb(card);
	tc90522_power(card, false);
	unregister_chrdev_region(card->dev, card->adapn);

	/* dma_hw_unmap */
	if (card->irq_enabled)
		free_irq(pdev->irq, card);
	if (card->dma.dat) {
		dma_free_attrs(&pdev->dev, card->dma.sz, card->dma.dat, card->dma.adr, 0);
	}
	for (i = 0; card->adap && i < card->adapn; i++) {
		struct pxq3pe_adap	*p	= &card->adap[i];

		if (p->tuner) {
			if (i & 1)
				tda2014x_driver.remove(p->tuner);
			else
				nm131_driver.remove(p->tuner);
			kfree(p->tuner);
		}
		if (p->demod) {
			tc90522_driver.remove(p->demod);
			kfree(p->demod);
		}
		vfree(p->sBuf);
	}
	kfree(card->adap);
	if (card->bar)
		pci_iounmap(pdev, card->bar);
	i2c_del_adapter(&card->i2c);
	for (i = 0; i < PXQ3PE_MAXCARD; i++)
		if (gCard[i] == card)
			gCard[i] = NULL;
	kfree(card);
	pci_release_regions(pdev);
	pci_set_drvdata(pdev, NULL);
	pci_disable_device(pdev);
dev_dbg(&pdev->dev, "%s", __func__);
}

static const struct i2c_algorithm pxq3pe_i2c_algo = {
	.functionality = ptx_i2c_func,
	.master_xfer = pxq3pe_i2c_xfr,
};

static int pxq3pe_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct pxq3pe_card	*card	= kzalloc(sizeof(struct pxq3pe_card), GFP_KERNEL);
	struct device		*dev	= &pdev->dev;
	struct i2c_adapter	*i2c	= &card->i2c;
	u8	i,
		v8,
		cardno;
	u16	cfg;
	int	err =	!card							||
			pci_enable_device(pdev)					||
			pci_set_dma_mask(pdev, DMA_BIT_MASK(32))		||
			pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))	||
			pci_read_config_word(pdev, PCI_COMMAND, &cfg)		||
			pci_request_regions(pdev, KBUILD_MODNAME);

	if (err)
		return ptx_abort(pdev, pxq3pe_remove, err, "Memory/PCI/DMA err, card=%p", card);
	card->pdev	= pdev;
	pci_set_drvdata(pdev, card);
	if (!(cfg & PCI_COMMAND_MASTER)) {
		pci_set_master(pdev);
		pci_read_config_word(pdev, PCI_COMMAND, &cfg);
		if (!(cfg & PCI_COMMAND_MASTER))
			return ptx_abort(pdev, pxq3pe_remove, -EIO, "Bus Mastering is not enabled");
	}
	card->bar	= pci_iomap(pdev, 0, 0);
	card->adapn	= PXQ3PE_ADAPN;
	card->name	= KBUILD_MODNAME;
	if (!card->bar)
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "I/O map failed");
	if (alloc_chrdev_region(&card->dev, 0, card->adapn, KBUILD_MODNAME) < 0)
		return ptx_abort(pdev, pxq3pe_remove, -ENOMEM, "alloc_chrdev_region failed");
	card->base_minor = MINOR(card->dev);
	i2c->algo = &pxq3pe_i2c_algo;
	i2c->dev.parent = &card->pdev->dev;
	strcpy(i2c->name, KBUILD_MODNAME);
	i2c_set_adapdata(i2c, card);
	if (i2c_add_adapter(i2c))
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "Cannot add I2C");

	for (i = 0; i < PXQ3PE_MAXCARD && gCard[i]; i++);
	if (i == PXQ3PE_MAXCARD)
		return ptx_abort(pdev, pxq3pe_remove, -ERANGE, "Too many cards");
	gCard[i]	= card;
	cardno		= i;
	card->adap	= kcalloc(card->adapn, sizeof(struct pxq3pe_adap), GFP_KERNEL);
	if (!card->adap)
		return ptx_abort(pdev, pxq3pe_remove, -ENOMEM, "No memory for pxq3pe_adap");
	mutex_init(&card->lock);

	for (i = 0; i < card->adapn; i++) {
		struct pxq3pe_adap	*p	= &card->adap[i];
		struct dvb_frontend	*fe	= &p->fe;

		fe->dtv_property_cache.delivery_system = i & 1 ? SYS_ISDBS : SYS_ISDBT;
		fe->id		= i;
		fe->dvb		= &p->dvb;
		p->sBufSize	= PKT_BYTES * 100 << 9;
		p->sBuf		= vzalloc(p->sBufSize);
		if (!p->sBuf)
			break;
		p->card		= card;
		p->minor	= card->base_minor + i;
		cdev_init(&p->cdev, &pxq3pe_fops);
		p->cdev.owner	= THIS_MODULE;
		cdev_add(&p->cdev, MKDEV(MAJOR(card->dev), p->minor), 1);
		device_create(pxq3pe_class,
				NULL,
				MKDEV(MAJOR(card->dev), p->minor),
				NULL,
				"%s%x%x%c",
				KBUILD_MODNAME,
				cardno,
				i,
				i & 1 ? 's': 't');
		mutex_init(&p->lock);
	}
	if (i < card->adapn)
		return ptx_abort(pdev, pxq3pe_remove, -ENOMEM, "No memory for stream buffer");

	/* IRQ & DMA map */
	if (request_irq(pdev->irq, pxq3pe_irq, IRQF_SHARED, KBUILD_MODNAME, card))
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "IRQ failed");
	card->irq_enabled = true;
	card->dma.sz	= PKT_BUFLEN * 4;
	card->dma.dat	= dma_alloc_attrs(dev, card->dma.sz, &card->dma.adr, GFP_KERNEL, 0);
	if (!card->dma.dat)
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "DMA mapping failed");

	/* hw_init */
	writeb(readb(card->bar + 0x880) & 0xC0, card->bar + 0x880);
	writel(0x3200C8, card->bar + 0x904);
	writel(0x90, card->bar + 0x900);
	writel(0x10000, card->bar + 0x0880);
	writel(0x0080, card->bar + 0x0A00);
	writel(0x0080, card->bar + 0x0B40);
	writel(0x0000, card->bar + 0x0888);
	writel(0x00CF, card->bar + 0x0894);
	writel(0x8000, card->bar + 0x088C);
	writel(0x1004, card->bar + 0x0890);
	writel(0x0090, card->bar + 0x0900);
	writel(0x3200C8, card->bar + 0x0904);
	pxq3pe_w_gpio0(card, 8, 0xFF);
	pxq3pe_w_gpio1(card, 0, 2);
	pxq3pe_w_gpio1(card, 1, 1);
	pxq3pe_w_gpio0(card, 1, 1);
	pxq3pe_w_gpio0(card, 0, 1);
	pxq3pe_w_gpio0(card, 1, 1);
	for (i = 0; i < 16; i++)
		if (!pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 0x10 + i, MOD_AUTH + i, 1, PXQ3PE_MOD_GPIO))
			break;
	v8 = 0xA0;
	if (i < 16 || !pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 5, &v8, 1, PXQ3PE_MOD_GPIO))
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "pxq3pe_hw_init failed");
dev_dbg(&pdev->dev, "%s 00", __func__);
	tc90522_power(card, true);
dev_dbg(&pdev->dev, "%s 01", __func__);
	for (i = 0; i < card->adapn; i++) {
		struct pxq3pe_adap	*p	= &card->adap[i];
		struct i2c_board_info	info	= {};

		info.platform_data	= &p->fe;
		info.addr		= PXQ3PE_I2C_ADR_GPIO;	/* should not be zero! */
		strlcpy(info.type, TC90522_MODNAME, I2C_NAME_SIZE);
		p->demod = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
		if (!p->demod)
			return ptx_abort(pdev, pxq3pe_remove, -ENODEV, "#%d Cannot register I2C demod", i);
		p->demod->dev.platform_data	= &p->fe;
		p->demod->adapter		= i2c;
dev_dbg(&pdev->dev, "%s %d probe DEMOD", __func__, i);
		tc90522_driver.probe(p->demod, NULL);

		p->tuner = kzalloc(sizeof(struct i2c_client), GFP_KERNEL);
		if (!p->tuner)
			return ptx_abort(pdev, pxq3pe_remove, -ENODEV, "#%d Cannot register I2C tuner", i);
		p->tuner->dev.platform_data	= &p->fe;
		p->tuner->adapter		= i2c;
dev_dbg(&pdev->dev, "%s %d probe TUNER", __func__, i);
		if (i & 1)
			tda2014x_driver.probe(p->tuner, NULL);
		else
			nm131_driver.probe(p->tuner, NULL);
dev_dbg(&pdev->dev, "%s %d end", __func__, i);
	}
	card->lnb = true;
	tc90522_lnb(card);
	return err;
}

static struct pci_driver pxq3pe_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= pxq3pe_id_table,
	.probe		= pxq3pe_probe,
	.remove		= pxq3pe_remove,
};

static int pxq3pe_init(void)
{
	int i;

	pxq3pe_class = class_create(THIS_MODULE, KBUILD_MODNAME);
	if (IS_ERR(pxq3pe_class))
		return PTR_ERR(pxq3pe_class);
	for (i = 0; i < PXQ3PE_MAXCARD; i++)
		gCard[i] = NULL;
	return pci_register_driver(&pxq3pe_driver);
}

static void pxq3pe_exit(void)
{
	pci_unregister_driver(&pxq3pe_driver);
	class_destroy(pxq3pe_class);
}

module_init(pxq3pe_init);
module_exit(pxq3pe_exit);