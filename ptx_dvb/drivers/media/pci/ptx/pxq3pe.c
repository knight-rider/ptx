/*
	DVB driver for PLEX PX-Q3PE ISDB-S/T PCIE receiver

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>

	Main components:
	ASIE5606X8	- controller
	TC90522		- 2ch OFDM ISDB-T + 2ch 8PSK ISDB-S demodulator
	TDA20142	- ISDB-S tuner
	NM120		- ISDB-T tuner
*/

#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include "ptx_common.h"
#include "tc90522.h"
#include "tda2014x.h"
#include "nm131.h"

MODULE_AUTHOR(PTX_AUTH);
MODULE_DESCRIPTION("PLEX PX-Q3PE Driver");
MODULE_LICENSE("GPL");

static char	*auth	= PTX_AUTH;
static int	ni,
		nx,
		idx[8]	= {},
		xor[4]	= {};
module_param(auth, charp, 0);
module_param_array(idx, int, &ni, 0);
module_param_array(xor, int, &nx, 0);

static struct pci_device_id pxq3pe_id_table[] = {
	{0x188B, 0x5220, 0x0B06, 0x0002, 0, 0, 0},
	{}
};
MODULE_DEVICE_TABLE(pci, pxq3pe_id_table);

enum ePXQ3PE {
	PKT_NUM		= 312,
	PKT_BUFSZ	= PTX_TS_SIZE * PKT_NUM,

	PXQ3PE_MOD_GPIO		= 0,
	PXQ3PE_MOD_TUNER	= 1,
	PXQ3PE_MOD_STAT		= 2,

	PXQ3PE_IRQ_STAT		= 0x808,
	PXQ3PE_IRQ_CLEAR	= 0x80C,
	PXQ3PE_IRQ_ACTIVE	= 0x814,
	PXQ3PE_IRQ_DISABLE	= 0x818,
	PXQ3PE_IRQ_ENABLE	= 0x81C,

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

	PXQ3PE_MAX_LOOP		= 1000,
};

struct pxq3pe_card {
	void __iomem		*bar;
	struct {
		dma_addr_t	adr;
		u8		*dat;
		u32		sz;
		bool		ON[2];
	} dma;
};

struct pxq3pe_adap {
	u8	tBuf[PKT_BUFSZ],
		*sBuf;
	u32	tBufIdx,
		sBufSize,
		sBufStart,
		sBufStop,
		sBufByteCnt;
};

bool pxq3pe_i2c_clean(struct ptx_card *card)
{
	struct pxq3pe_card	*c	= card->priv;
	void __iomem		*bar	= c->bar;

	if ((readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F) != 0x10 || readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F00) {
		u32 stat = readl(bar + PXQ3PE_I2C_SW_CTL) | 0x20;

		writel(stat, bar + PXQ3PE_I2C_SW_CTL);
		writel(stat & 0xFFFFFFDF, bar + PXQ3PE_I2C_SW_CTL);
		if ((readl(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F) != 0x10) {
			dev_err(&card->pdev->dev, "%s FIFO error", __func__);
			return false;
		}
	}
	writel(0, bar + PXQ3PE_I2C_CTL_STAT);
	return true;
}

bool pxq3pe_w(struct ptx_card *card, u8 slvadr, u8 regadr, u8 *wdat, u8 bytelen, u8 mode)
{
	struct pxq3pe_card	*c	= card->priv;
	void __iomem		*bar	= c->bar;
	int	i,
		j,
		k;
	u8	i2cCtlByte,
		i2cFifoWSz;

	if (!pxq3pe_i2c_clean(card))
		return false;
	switch (mode) {
	case PXQ3PE_MOD_GPIO:
		i2cCtlByte = 0xC0;
		break;
	case PXQ3PE_MOD_TUNER:
		regadr = 0;
		i2cCtlByte = 0x80;
		break;
	case PXQ3PE_MOD_STAT:
		regadr = 0;
		i2cCtlByte = 0x84;
		break;
	default:
		return false;
	}
	writel((slvadr << 8) + regadr, bar + PXQ3PE_I2C_ADR);
	for (i = 0; i < 16 && i < bytelen; i += 4) {
		udelay(1000);
		writel(*((u32 *)(wdat + i)), bar + PXQ3PE_I2C_FIFO_DATA);
	}
	writew((bytelen << 8) + i2cCtlByte, bar + PXQ3PE_I2C_CTL_STAT);
	for (j = 0; j < PXQ3PE_MAX_LOOP; j++) {
		if (i < bytelen) {
			i2cFifoWSz = readb(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F;
			for (k = 0; bytelen > 16 && k < PXQ3PE_MAX_LOOP && i2cFifoWSz < bytelen - 16; k++) {
				i2cFifoWSz = readb(bar + PXQ3PE_I2C_FIFO_STAT) & 0x1F;
				udelay(1000);
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
	return j < PXQ3PE_MAX_LOOP ? !(readl(bar + PXQ3PE_I2C_CTL_STAT) & 0x280000) : false;
}

bool pxq3pe_r(struct ptx_card *card, u8 slvadr, u8 regadr, u8 *rdat, u8 bytelen, u8 mode)
{
	struct pxq3pe_card	*c	= card->priv;
	void __iomem		*bar	= c->bar;
	u8	i2cCtlByte,
		i2cStat,
		i2cFifoRSz,
		i2cByteCnt;
	int	i		= 0,
		j,
		idx;
	bool	ret		= false;

	if (!pxq3pe_i2c_clean(card))
		return false;
	switch (mode) {
	case PXQ3PE_MOD_GPIO:
		i2cCtlByte = 0xE0;
		break;
	case PXQ3PE_MOD_TUNER:
		regadr = 0;
		i2cCtlByte = 0xA0;
		break;
	default:
		return false;
	}
	writel((slvadr << 8) + regadr, bar + PXQ3PE_I2C_ADR);
	writew(i2cCtlByte + (bytelen << 8), bar + PXQ3PE_I2C_CTL_STAT);
	i2cByteCnt = bytelen;
	j = 0;
	while (j < PXQ3PE_MAX_LOOP) {
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

int pxq3pe_xfr(struct i2c_adapter *i2c, struct i2c_msg *msg, int sz)
{
	struct ptx_card	*card	= i2c_get_adapdata(i2c);
	u8		i;
	bool		ret	= true;

	if (!i2c || !card || !msg)
		return -EINVAL;
	for (i = 0; i < sz && ret; i++, msg++) {
		u8	slvadr	= msg->addr,
			regadr	= msg->len ? *msg->buf : 0,
			mode	= slvadr == PXQ3PE_I2C_ADR_GPIO	? PXQ3PE_MOD_GPIO
				: sz > 1 && i == sz - 2		? PXQ3PE_MOD_STAT
				: PXQ3PE_MOD_TUNER;

		mutex_lock(&card->lock);
		if (msg->flags & I2C_M_RD) {
			u8 buf[sz];

			ret = pxq3pe_r(card, slvadr, regadr, buf, msg->len, mode);
			memcpy(msg->buf, buf, msg->len);
		} else
			ret = pxq3pe_w(card, slvadr, regadr, msg->buf, msg->len, mode);
		mutex_unlock(&card->lock);
	}
	return i;
}

bool pxq3pe_w_gpio2(struct ptx_card *card, u8 dat, u8 mask)
{
	u8 val;

	return	pxq3pe_r(card, PXQ3PE_I2C_ADR_GPIO, 0xB, &val, 1, PXQ3PE_MOD_GPIO)	&&
		(val = (mask & dat) | (val & ~mask), pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 0xB, &val, 1, PXQ3PE_MOD_GPIO));
}

void pxq3pe_w_gpio1(struct ptx_card *card, u8 dat, u8 mask)
{
	struct pxq3pe_card *c = card->priv;

	mask <<= 3;
	writeb((readb(c->bar + 0x890) & ~mask) | ((dat << 3) & mask), c->bar + 0x890);
}

void pxq3pe_w_gpio0(struct ptx_card *card, u8 dat, u8 mask)
{
	struct pxq3pe_card *c = card->priv;

	writeb((-(mask & 1) & 4 & -(dat & 1)) | (readb(c->bar + 0x890) & ~(-(mask & 1) & 4)), c->bar + 0x890);
	writeb((mask & dat) | (readb(c->bar + 0x894) & ~mask), c->bar + 0x894);
}

void pxq3pe_power(struct ptx_card *card, bool ON)
{
	if (ON) {
		pxq3pe_w_gpio0(card, 1, 1);
		pxq3pe_w_gpio0(card, 0, 1);
		pxq3pe_w_gpio0(card, 1, 1);
		pxq3pe_w_gpio1(card, 1, 1);
		pxq3pe_w_gpio1(card, 0, 1);
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
}

irqreturn_t pxq3pe_irq(int irq, void *ctx)
{
	struct ptx_card		*card	= ctx;
	struct pxq3pe_card	*c	= card->priv;
	void __iomem		*bar	= c->bar;
	u32	dmamgmt,
		i,
		irqstat = readl(bar + PXQ3PE_IRQ_STAT);
	bool	ch	= irqstat & 0b0101 ? 0 : 1,
		port	= irqstat & 0b0011 ? 0 : 1;
	u8	*tbuf	= c->dma.dat + PKT_BUFSZ * (port * 2 + ch);

	void pxq3pe_dma_put_stream(struct pxq3pe_adap *p)
	{
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

	if (!(irqstat & 0b1111))
		return IRQ_HANDLED;
	writel(irqstat, bar + PXQ3PE_IRQ_CLEAR);
	dmamgmt = readl(bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	if ((readl(bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * ch + PXQ3PE_DMA_XFR_STAT) & 0x3FFFFF) == PKT_BUFSZ)
		for (i = 0; i < PKT_BUFSZ; i += PTX_TS_SIZE) {
			u8 idx = !port * 4 + (tbuf[i] == 0xC7 ? 0 : tbuf[i] == 0x47 ?
					1 : tbuf[i] == 0x07 ? 2 : tbuf[i] == 0x87 ? 3 : card->adapn);
			struct ptx_adap		*adap	= &card->adap[idx];
			struct pxq3pe_adap	*p	= adap->priv;

			if (idx < card->adapn && adap->ON) {
				tbuf[i] = PTX_TS_SYNC;
				memcpy(&p->tBuf[p->tBufIdx], &tbuf[i], PTX_TS_SIZE);
				p->tBufIdx += PTX_TS_SIZE;
				if (p->tBufIdx >= PKT_BUFSZ) {
					pxq3pe_dma_put_stream(p);
					p->tBufIdx = 0;
				}
			}
		}
	if (c->dma.ON[port])
		writel(dmamgmt | (2 << (ch * 16)), bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	return IRQ_HANDLED;
}

int pxq3pe_thread(void *dat)
{
	struct ptx_adap		*adap	= dat;
	struct pxq3pe_adap	*p	= adap->priv;

	set_freezable();
	while (!kthread_should_stop()) {
		u8	*rbuf	= &p->sBuf[p->sBufStart];
		int	i	= 0,
			j	= 0,
			k,
			sz	= p->sBufSize - p->sBufStart;

		try_to_freeze();
		if (!p->sBufByteCnt) {
			msleep_interruptible(0);
			continue;
		}
		if (sz > p->sBufByteCnt)
			sz = p->sBufByteCnt;
		while (j < sz / PTX_TS_SIZE) {
			j++;
			i += 4;
			while (i < j * PTX_TS_SIZE)
				for (k = 0; k < 8; k++, i++)
					rbuf[i] ^= xor[idx[k]];
		}
		dvb_dmx_swfilter(&adap->demux, rbuf, sz);
		p->sBufStart	= (p->sBufStart + sz) % p->sBufSize;
		p->sBufByteCnt -= sz;
	}
	return 0;
}

int pxq3pe_dma(struct ptx_adap *adap, bool ON)
{
	struct ptx_card		*card	= adap->card;
	struct pxq3pe_card	*c	= card->priv;
	struct pxq3pe_adap	*p	= adap->priv;
	struct i2c_client	*d	= adap->fe->demodulator_priv;
	u8			idx	= (d->addr / 2) & (card->adapn - 1),
				i;
	bool			port	= !(idx & 4);
	u32			val	= 0b0011 << (port * 2);

	if (!ON) {
		for (i = 0; i < card->adapn; i++)
			if (!c->dma.ON[port] || (idx != i && (i & 4) == (idx & 4) && c->dma.ON[port]))
				return 0;

		i = readb(c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
		if ((i & 0b1100) == 4)
			writeb(i & 0xFD, c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
		writeb(0b0011 << (port * 2), c->bar + PXQ3PE_IRQ_DISABLE);
		c->dma.ON[port] = false;
		return 0;
	}

	p->sBufByteCnt	= 0;
	p->sBufStop	= 0;
	p->sBufStart	= 0;
	if (c->dma.ON[port])
		return 0;

	/* SetTSMode */
	i = readb(c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_TSMODE);
	if ((i & 0x80) == 0)
		writeb(i | 0x80, c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_TSMODE);

	/* irq_enable */
	writel(val, c->bar + PXQ3PE_IRQ_ENABLE);
	if (val != (readl(c->bar + PXQ3PE_IRQ_ACTIVE) & val))
		return -EIO;

	/* cfg_dma */
	for (i = 0; i < 2; i++) {
		val		= readl(c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
		writel(c->dma.adr + PKT_BUFSZ * (port * 2 + i),
					c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * i + PXQ3PE_DMA_ADR_LO);
		writel(0,		c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * i + PXQ3PE_DMA_ADR_HI);
		writel(0x11C0E520,	c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_OFFSET_CH * i + PXQ3PE_DMA_CTL);
		writel(val | 3 << (i * 16),
					c->bar + PXQ3PE_DMA_OFFSET_PORT * port + PXQ3PE_DMA_MGMT);
	}
	c->dma.ON[port] = true;
	return 0;
}

void pxq3pe_lnb(struct ptx_card *card, bool lnb)
{
	pxq3pe_w_gpio2(card, lnb ? 0x20 : 0, 0x20);
}

void pxq3pe_remove(struct pci_dev *pdev)
{
	struct ptx_card		*card	= pci_get_drvdata(pdev);
	struct ptx_adap		*adap;
	struct pxq3pe_card	*c;
	u8	regctl = 0,
		i;

	if (!card)
		return;
	c	= card->priv;
	for (i = 0, adap = card->adap; adap->fe && i < card->adapn; i++, adap++) {
		pxq3pe_dma(adap, false);
		ptx_sleep(adap->fe);
	}
	pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 0x80, &regctl, 1, PXQ3PE_MOD_GPIO);
	pxq3pe_power(card, false);

	/* dma_hw_unmap */
	free_irq(pdev->irq, card);
	if (c->dma.dat)
		pci_free_consistent(card->pdev, c->dma.sz, c->dma.dat, c->dma.adr);
	for (i = 0; i < card->adapn; i++) {
		struct ptx_adap		*adap	= &card->adap[i];
		struct pxq3pe_adap	*p	= adap->priv;

		vfree(p->sBuf);
	}
	if (c->bar)
		pci_iounmap(pdev, c->bar);
	ptx_unregister_adap(card);
}

static const struct i2c_algorithm pxq3pe_algo = {
	.functionality	= ptx_i2c_func,
	.master_xfer	= pxq3pe_xfr,
};

static int pxq3pe_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct ptx_subdev_info	pxq3pe_subdev_info[] = {
		{SYS_ISDBT, 0x20, TC90522_MODNAME, 0x10, NM131_MODNAME},
		{SYS_ISDBS, 0x22, TC90522_MODNAME, 0x11, TDA2014X_MODNAME},
		{SYS_ISDBT, 0x24, TC90522_MODNAME, 0x12, NM131_MODNAME},
		{SYS_ISDBS, 0x26, TC90522_MODNAME, 0x13, TDA2014X_MODNAME},
		{SYS_ISDBT, 0x28, TC90522_MODNAME, 0x14, NM131_MODNAME},
		{SYS_ISDBS, 0x2A, TC90522_MODNAME, 0x15, TDA2014X_MODNAME},
		{SYS_ISDBT, 0x2C, TC90522_MODNAME, 0x16, NM131_MODNAME},
		{SYS_ISDBS, 0x2E, TC90522_MODNAME, 0x17, TDA2014X_MODNAME},
	};
	struct ptx_card		*card	= ptx_alloc(pdev, KBUILD_MODNAME, ARRAY_SIZE(pxq3pe_subdev_info),
						sizeof(struct pxq3pe_card), sizeof(struct pxq3pe_adap), pxq3pe_lnb);
	struct pxq3pe_card	*c;
	u8	regctl	= 0xA0,
		i;
	u16	cfg;
	int	err	= !card || pci_read_config_word(pdev, PCI_COMMAND, &cfg);

	if (err)
		return ptx_abort(pdev, pxq3pe_remove, err, "Memory/PCI error, card=%p", card);
	c	= card->priv;
	if (!(cfg & PCI_COMMAND_MASTER)) {
		pci_set_master(pdev);
		pci_read_config_word(pdev, PCI_COMMAND, &cfg);
		if (!(cfg & PCI_COMMAND_MASTER))
			return ptx_abort(pdev, pxq3pe_remove, -EIO, "Bus Mastering is disabled");
	}
	c->bar	= pci_iomap(pdev, 0, 0);
	if (!c->bar)
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "I/O map failed");
	if (ptx_i2c_add_adapter(card, &pxq3pe_algo))
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "Cannot add I2C");

	for (i = 0; i < card->adapn; i++) {
		struct ptx_adap		*adap	= &card->adap[i];
		struct pxq3pe_adap	*p	= adap->priv;

		p->sBufSize	= PTX_TS_SIZE * 100 << 9;
		p->sBuf		= vzalloc(p->sBufSize);
		if (!p->sBuf)
			return ptx_abort(pdev, pxq3pe_remove, -ENOMEM, "No memory for stream buffer");
	}

	/* dma_map */
	if (request_irq(pdev->irq, pxq3pe_irq, IRQF_SHARED, KBUILD_MODNAME, card))
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "IRQ failed");
	c->dma.sz	= PKT_BUFSZ * 4;
	c->dma.dat	= pci_alloc_consistent(card->pdev, c->dma.sz, &c->dma.adr);
	if (!c->dma.dat)
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "DMA mapping failed");

	/* hw_init */
	writeb(readb(c->bar + 0x880) & 0xC0, c->bar + 0x880);
	writel(0x3200C8, c->bar + 0x904);
	writel(0x90,	 c->bar + 0x900);
	writel(0x10000,	 c->bar + 0x880);
	writel(0x0080,	 c->bar + PXQ3PE_DMA_TSMODE);				/* port 0 */
	writel(0x0080,	 c->bar + PXQ3PE_DMA_TSMODE + PXQ3PE_DMA_OFFSET_PORT);	/* port 1 */
	writel(0x0000,	 c->bar + 0x888);
	writel(0x00CF,	 c->bar + 0x894);
	writel(0x8000,	 c->bar + 0x88C);
	writel(0x1004,	 c->bar + 0x890);
	writel(0x0090,	 c->bar + 0x900);
	writel(0x3200C8, c->bar + 0x904);
	pxq3pe_w_gpio0(card, 8, 0xFF);
	pxq3pe_w_gpio1(card, 0, 2);
	pxq3pe_w_gpio1(card, 1, 1);
	pxq3pe_w_gpio0(card, 1, 1);
	pxq3pe_w_gpio0(card, 0, 1);
	pxq3pe_w_gpio0(card, 1, 1);
	for (i = 0; i < 16; i++)
		if (!pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 0x10 + i, auth + i, 1, PXQ3PE_MOD_GPIO))
			break;
	if (i < 16 || !pxq3pe_w(card, PXQ3PE_I2C_ADR_GPIO, 5, &regctl, 1, PXQ3PE_MOD_GPIO))
		return ptx_abort(pdev, pxq3pe_remove, -EIO, "hw_init failed i=%d", i);
	pxq3pe_power(card, true);
	err = ptx_register_adap(card, pxq3pe_subdev_info, pxq3pe_thread, pxq3pe_dma);
	return err ? ptx_abort(pdev, pxq3pe_remove, err, "Unable to register DVB adapter & frontend") : 0;
}

static struct pci_driver pxq3pe_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= pxq3pe_id_table,
	.probe		= pxq3pe_probe,
	.remove		= pxq3pe_remove,
};
module_pci_driver(pxq3pe_driver);

