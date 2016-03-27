/*
	DVB driver for Earthsoft PT3 ISDB-S/T PCIE bridge Altera Cyclone IV FPGA EP4CGX15BF14C8N

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.
*/

#include "tc90522.h"
#include "qm1d1c004x.h"
#include "mxl301rf.h"
#include "ptx_common.h"

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 DVB Driver");
MODULE_LICENSE("GPL");

static struct pci_device_id pt3_id[] = {
	{PCI_DEVICE(0x1172, 0x4c15)},
	{},
};
MODULE_DEVICE_TABLE(pci, pt3_id);

enum ePT3 {
	PT3_REG_VERSION	= 0x00,	/*	R	Version		*/
	PT3_REG_BUS	= 0x04,	/*	R	Bus		*/
	PT3_REG_SYS_W	= 0x08,	/*	W	System		*/
	PT3_REG_SYS_R	= 0x0c,	/*	R	System		*/
	PT3_REG_I2C_W	= 0x10,	/*	W	I2C		*/
	PT3_REG_I2C_R	= 0x14,	/*	R	I2C		*/
	PT3_REG_RAM_W	= 0x18,	/*	W	RAM		*/
	PT3_REG_RAM_R	= 0x1c,	/*	R	RAM		*/
	PT3_DMA_BASE	= 0x40,	/* + 0x18*idx			*/
	PT3_DMA_OFFSET	= 0x18,
	PT3_DMA_DESC	= 0x00,	/*	W	DMA descriptor	*/
	PT3_DMA_CTL	= 0x08,	/*	W	DMA		*/
	PT3_TS_CTL	= 0x0c,	/*	W	TS		*/
	PT3_STATUS	= 0x10,	/*	R	DMA/FIFO/TS	*/
	PT3_TS_ERR	= 0x14,	/*	R	TS		*/

	PT3_I2C_DATA_OFFSET	= 0x800,
	PT3_I2C_START_ADDR	= 0x17fa,

	PT3_PWR_OFF		= 0x00,
	PT3_PWR_AMP_ON		= 0x04,
	PT3_PWR_TUNER_ON	= 0x40,
};

struct pt3_card {
	void __iomem	*bar_reg,
			*bar_mem;
};

struct pt3_dma {
	dma_addr_t	adr;
	u8		*dat;
	u32		sz;
};

struct pt3_adap {
	u32	ts_blk_idx,
		ts_blk_cnt,
		desc_pg_cnt;
	void __iomem	*dma_base;
	struct pt3_dma	*ts_info,
			*desc_info;
};

int pt3_i2c_flush(struct pt3_card *c, u32 start_addr)
{
	u32	val	= 0b0110,
		i	= 999;

	void i2c_wait(void)
	{
		while (1) {
			val = readl(c->bar_reg + PT3_REG_I2C_R);

			if (!(val & 1))						/* sequence stopped */
				return;
			msleep_interruptible(0);
		}
	}

	while ((val & 0b0110) && i--) {						/* I2C bus is dirty */
		i2c_wait();
		writel(1 << 16 | start_addr, c->bar_reg + PT3_REG_I2C_W);	/* 0x00010000 start sequence */
		i2c_wait();
	}
	return val & 0b0110 ? -EIO : 0;						/* ACK status */
}

int pt3_i2c_xfr(struct i2c_adapter *i2c, struct i2c_msg *msg, int sz)
{
	enum pt3_i2c_cmd {
		I_END,
		I_ADDRESS,
		I_CLOCK_L,
		I_CLOCK_H,
		I_DATA_L,
		I_DATA_H,
		I_RESET,
		I_SLEEP,
		I_DATA_L_NOP	= 0x08,
		I_DATA_H_NOP	= 0x0c,
		I_DATA_H_READ	= 0x0d,
		I_DATA_H_ACK0	= 0x0e,
	};
	struct ptx_card *card	= i2c_get_adapdata(i2c);
	struct pt3_card *c	= card->priv;
	u32	offset		= 0;
	u8	buf;
	bool	filled		= false;

	void i2c_shoot(u8 dat)
	{
		if (filled) {
			buf |= dat << 4;
			writeb(buf, c->bar_mem + PT3_I2C_DATA_OFFSET + offset);
			offset++;
		} else
			buf = dat;
		filled ^= true;
	}

	void i2c_w(const u8 *dat, u32 size)
	{
		u32 i, j;

		for (i = 0; i < size; i++) {
			for (j = 0; j < 8; j++)
				i2c_shoot((dat[i] >> (7 - j)) & 1 ? I_DATA_H_NOP : I_DATA_L_NOP);
			i2c_shoot(I_DATA_H_ACK0);
		}
	}

	void i2c_r(u32 size)
	{
		u32 i, j;

		for (i = 0; i < size; i++) {
			for (j = 0; j < 8; j++)
				i2c_shoot(I_DATA_H_READ);
			if (i == (size - 1))
				i2c_shoot(I_DATA_H_NOP);
			else
				i2c_shoot(I_DATA_L_NOP);
		}
	}
	int i, j;

	if (sz < 1 || sz > 3 || !msg || msg[0].flags)		/* always write first */
		return -ENOTSUPP;
	mutex_lock(&card->lock);
	for (i = 0; i < sz; i++) {
		u8 byte = (msg[i].addr << 1) | (msg[i].flags & 1);

		/* start */
		i2c_shoot(I_DATA_H);
		i2c_shoot(I_CLOCK_H);
		i2c_shoot(I_DATA_L);
		i2c_shoot(I_CLOCK_L);
		i2c_w(&byte, 1);
		if (msg[i].flags == I2C_M_RD)
			i2c_r(msg[i].len);
		else
			i2c_w(msg[i].buf, msg[i].len);
	}

	/* stop */
	i2c_shoot(I_DATA_L);
	i2c_shoot(I_CLOCK_H);
	i2c_shoot(I_DATA_H);
	i2c_shoot(I_END);
	if (filled)
		i2c_shoot(I_END);
	if (pt3_i2c_flush(c, 0))
		sz = -EIO;
	else
		for (i = 1; i < sz; i++)
			if (msg[i].flags == I2C_M_RD)
				for (j = 0; j < msg[i].len; j++)
					msg[i].buf[j] = readb(c->bar_mem + PT3_I2C_DATA_OFFSET + j);
	mutex_unlock(&card->lock);
	return sz;
}

static const struct i2c_algorithm pt3_i2c_algo = {
	.functionality	= ptx_i2c_func,
	.master_xfer	= pt3_i2c_xfr,
};

void pt3_lnb(struct ptx_card *card, bool lnb)
{
	struct pt3_card *c = card->priv;

	writel(lnb ? 15 : 12, c->bar_reg + PT3_REG_SYS_W);
}

int pt3_power(struct dvb_frontend *fe, u8 pwr)
{
	struct i2c_client	*d	= fe->demodulator_priv;
	u8		buf[]	= {0x1E, pwr | 0b10011001};
	struct i2c_msg	msg[]	= {
		{.addr = d->addr,	.flags = 0,	.buf = buf,	.len = 2,},
	};

	return i2c_transfer(d->adapter, msg, 1) == 1 ? 0 : -EIO;
}

int pt3_dma_run(struct ptx_adap *adap, bool ON)
{
	struct pt3_adap	*p	= adap->priv;
	void __iomem	*base	= p->dma_base;
	int		i	= 999;

	if (ON) {
		for (i = 0; i < p->ts_blk_cnt; i++)		/* 17 */
			*p->ts_info[i].dat	= PTX_TS_NOT_SYNC;
		p->ts_blk_idx = 0;
		writel(2, base + PT3_DMA_CTL);			/* stop DMA */
		writeq(p->desc_info->adr, base + PT3_DMA_DESC);
		writel(1, base + PT3_DMA_CTL);			/* start DMA */
	} else {
		writel(2, base + PT3_DMA_CTL);			/* stop DMA */
		while (i--) {
			if (!(readl(base + PT3_STATUS) & 1))
				break;
			msleep_interruptible(0);
		}
	}
	return i ? 0 : -ETIMEDOUT;
}

int pt3_thread(void *dat)
{
	struct ptx_adap	*adap	= dat;
	struct pt3_adap	*p	= adap->priv;
	struct pt3_dma	*ts;

	set_freezable();
	while (!kthread_should_stop()) {
		u32 next = (p->ts_blk_idx + 1) % p->ts_blk_cnt;

		try_to_freeze();
		ts = p->ts_info + next;
		if (*ts->dat != PTX_TS_SYNC) {		/* wait until 1 TS block is full */
			schedule_timeout_interruptible(0);
			continue;
		}
		ts = p->ts_info + p->ts_blk_idx;
		dvb_dmx_swfilter_packets(&adap->demux, ts->dat, ts->sz / PTX_TS_SIZE);
		*ts->dat	= PTX_TS_NOT_SYNC;	/* mark as read */
		p->ts_blk_idx	= next;
	}
	return 0;
}

void pt3_remove(struct pci_dev *pdev)
{
	struct ptx_card	*card	= pci_get_drvdata(pdev);
	struct pt3_card	*c;
	struct ptx_adap	*adap;
	int		i;

	if (!card)
		return;
	c	= card->priv;
	adap	= card->adap;
	for (i = 0; i < card->adapn; i++, adap++) {
		struct pt3_adap	*p	= adap->priv;
		struct pt3_dma	*page;
		u32		j;

		pt3_dma_run(adap, false);
		if (p->ts_info) {
			for (j = 0; j < p->ts_blk_cnt; j++) {
				page = &p->ts_info[j];
				if (page->dat)
					pci_free_consistent(adap->card->pdev, page->sz, page->dat, page->adr);
			}
			kfree(p->ts_info);
		}
		if (p->desc_info) {
			for (j = 0; j < p->desc_pg_cnt; j++) {
				page = &p->desc_info[j];
				if (page->dat)
					pci_free_consistent(adap->card->pdev, page->sz, page->dat, page->adr);
			}
			kfree(p->desc_info);
		}
		if (adap->fe) {
			ptx_sleep(adap->fe);
			pt3_power(adap->fe, PT3_PWR_OFF);
		}
	}
	ptx_unregister_adap(card);
	if (c->bar_reg)
		iounmap(c->bar_reg);
	if (c->bar_mem)
		iounmap(c->bar_mem);
}

int pt3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct ptx_adap	*adap;
	struct pt3_card	*c;
	struct ptx_subdev_info	pt3_subdev_info[] = {
		{SYS_ISDBS, 0b00010001, TC90522_MODNAME, 0x63, QM1D1C004X_MODNAME},
		{SYS_ISDBS, 0b00010011, TC90522_MODNAME, 0x60, QM1D1C004X_MODNAME},
		{SYS_ISDBT, 0b00010000, TC90522_MODNAME, 0x62, MXL301RF_MODNAME},
		{SYS_ISDBT, 0b00010010, TC90522_MODNAME, 0x61, MXL301RF_MODNAME},
	};
	struct ptx_card	*card	= ptx_alloc(pdev, KBUILD_MODNAME, ARRAY_SIZE(pt3_subdev_info),
					sizeof(struct pt3_card), sizeof(struct pt3_adap), pt3_lnb);

	bool dma_create(struct pt3_adap	*p)
	{
		struct dma_desc {
			u64 page_addr;
			u32 page_size;
			u64 next_desc;
		} __packed;		/* 20B */
		enum {
			DESC_SZ		= sizeof(struct dma_desc),		/* 20B	*/
			DESC_MAX	= 4096 / DESC_SZ,			/* 204	*/
			DESC_PAGE_SZ	= DESC_MAX * DESC_SZ,			/* 4080	*/
			TS_PAGE_CNT	= PTX_TS_SIZE / 4,			/* 47	*/
			TS_BLOCK_CNT	= 17,
		};
		struct pt3_dma	*descinfo;
		struct dma_desc	*prev		= NULL,
				*curr;
		u32		i,
				j,
				desc_todo	= 0,
				desc_pg_idx	= 0;
		u64		desc_addr;

		p->ts_blk_cnt	= TS_BLOCK_CNT;							/* 17	*/
		p->desc_pg_cnt	= roundup(TS_PAGE_CNT * p->ts_blk_cnt, DESC_MAX);		/* 4	*/
		p->ts_info	= kcalloc(p->ts_blk_cnt, sizeof(struct pt3_dma), GFP_KERNEL);
		p->desc_info	= kcalloc(p->desc_pg_cnt, sizeof(struct pt3_dma), GFP_KERNEL);
		if (!p->ts_info || !p->desc_info)
			return false;
		for (i = 0; i < p->desc_pg_cnt; i++) {						/* 4	*/
			p->desc_info[i].sz	= DESC_PAGE_SZ;					/* 4080B, max 204 * 4 = 816 descs */
			p->desc_info[i].dat	= pci_alloc_consistent(card->pdev, p->desc_info[i].sz, &p->desc_info[i].adr);
			if (!p->desc_info[i].dat)
				return false;
			memset(p->desc_info[i].dat, 0, p->desc_info[i].sz);
		}
		for (i = 0; i < p->ts_blk_cnt; i++) {						/* 17	*/
			p->ts_info[i].sz	= DESC_PAGE_SZ * TS_PAGE_CNT;			/* 1020 pkts, 4080 * 47 = 191760B, total 3259920B */
			p->ts_info[i].dat	= pci_alloc_consistent(card->pdev, p->ts_info[i].sz, &p->ts_info[i].adr);
			if (!p->ts_info[i].dat)
				return false;
			for (j = 0; j < TS_PAGE_CNT; j++) {					/* 47, total 47 * 17 = 799 pages */
				if (!desc_todo) {						/* 20	*/
					descinfo	= p->desc_info + desc_pg_idx;		/* jump to next desc_pg */
					curr		= (struct dma_desc *)descinfo->dat;
					desc_addr	= descinfo->adr;
					desc_todo	= DESC_MAX;				/* 204	*/
					desc_pg_idx++;
				}
				if (prev)
					prev->next_desc = desc_addr;
				curr->page_addr = p->ts_info[i].adr + DESC_PAGE_SZ * j;
				curr->page_size = DESC_PAGE_SZ;
				curr->next_desc = p->desc_info->adr;				/* circular link */
				prev		= curr;
				curr++;
				desc_addr	+= DESC_SZ;
				desc_todo--;
			}
		}
		return true;
	}

	u8	i;
	int	ret	= !card || pci_read_config_byte(pdev, PCI_CLASS_REVISION, &i);

	if (ret)
		return ptx_abort(pdev, pt3_remove, ret, "PCI/DMA/memory error");
	if (i != 1)
		return ptx_abort(pdev, pt3_remove, -ENOTSUPP, "PCI Rev%d not supported", i);
	pci_set_master(pdev);
	c		= card->priv;
	c->bar_reg	= pci_ioremap_bar(pdev, 0);
	c->bar_mem	= pci_ioremap_bar(pdev, 2);
	if (!c->bar_reg || !c->bar_mem)
		return ptx_abort(pdev, pt3_remove, -EIO, "Failed pci_ioremap_bar");
	ret = (readl(c->bar_reg + PT3_REG_VERSION) >> 8) & 0xFF00FF;
	if (ret != 0x030004)
		return ptx_abort(pdev, pt3_remove, -ENOTSUPP, "PT%d FPGA v%d not supported", ret >> 16, ret & 0xFF);
	for (i = 0, adap = card->adap; i < card->adapn; i++, adap++) {
		struct pt3_adap	*p	= adap->priv;

		p->dma_base	= c->bar_reg + PT3_DMA_BASE + PT3_DMA_OFFSET * i;
		if (!dma_create(p))
			return ptx_abort(pdev, pt3_remove, -ENOMEM, "Failed dma_create");
	}
	adap--;
	ret =	ptx_i2c_add_adapter(card, &pt3_i2c_algo)				||
		pt3_i2c_flush(c, 0)							||
		ptx_register_adap(card, pt3_subdev_info, pt3_thread, pt3_dma_run)	||
		pt3_power(adap->fe, PT3_PWR_TUNER_ON)					||
		pt3_i2c_flush(c, PT3_I2C_START_ADDR)					||
		pt3_power(adap->fe, PT3_PWR_TUNER_ON | PT3_PWR_AMP_ON);
	return	ret ?
		ptx_abort(pdev, pt3_remove, ret, "Unable to register I2C/DVB adapter/frontend") :
		0;
}

static struct pci_driver pt3_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= pt3_id,
	.probe		= pt3_probe,
	.remove		= pt3_remove,
};
module_pci_driver(pt3_driver);

