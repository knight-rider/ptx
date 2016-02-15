/*
 * DVB driver for Earthsoft PT3 ISDB-S/T PCIE bridge Altera Cyclone IV FPGA EP4CGX15BF14C8N
 *
 * Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "dvb_frontend.h"
#include "tc90522.h"
#include "qm1d1c0042.h"
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
	PT3_DMA_DESC_L	= 0x00,	/*	W	DMA descriptor	*/
	PT3_DMA_DESC_H	= 0x04,	/*	W	DMA descriptor	*/
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
	u32		sz,
			pos;
};

struct pt3_adap {
	u32	ts_pos,
		ts_count,
		desc_count;
	void __iomem	*dma_base;
	struct pt3_dma	*ts_info,
			*desc_info;
};

int pt3_i2c_flush(struct pt3_card *c, u32 start_addr)
{
	u32 i2c_wait(void)
	{
		while (1) {
			u32 val = readl(c->bar_reg + PT3_REG_I2C_R);

			if (!(val & 1))					/* sequence stopped */
				return val;
			msleep_interruptible(1);
		}
	}
	i2c_wait();
	writel(1 << 16 | start_addr, c->bar_reg + PT3_REG_I2C_W);	/* 0x00010000 start sequence */
	return i2c_wait() & 0b0110 ? -EIO : 0;				/* ACK status */
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

	writel(lnb ? 0b1111 : 0b1100, c->bar_reg + PT3_REG_SYS_W);
}

int pt3_thread(void *dat)
{
	struct ptx_adap	*adap	= dat;
	struct pt3_adap	*p	= adap->priv;
	struct pt3_dma	*ts;
	u32		i,
			prev;
	size_t		csize,
			remain	= 0;

	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		while (p->ts_info[p->ts_pos].sz > remain) {
			remain = p->ts_info[p->ts_pos].sz;
			mutex_lock(&adap->lock);
			while (remain > 0) {
				for (i = 0; i < 20; i++) {
					struct pt3_dma	*ts;
					u32	next	= p->ts_pos + 1;

					if (next >= p->ts_count)
						next = 0;
					ts = &p->ts_info[next];
					if (ts->dat[ts->pos] == PTX_TS_SYNC)
						break;
					msleep_interruptible(30);
				}
				if (i == 20)
					break;
				prev = p->ts_pos - 1;
				if (prev < 0 || p->ts_count <= prev)
					prev = p->ts_count - 1;
				ts = &p->ts_info[p->ts_pos];
				while (remain > 0) {
					csize = (remain < (ts->sz - ts->pos)) ?
						 remain : (ts->sz - ts->pos);
					dvb_dmx_swfilter_raw(&adap->demux, &ts->dat[ts->pos], csize);
					remain -= csize;
					ts->pos += csize;
					if (ts->pos < ts->sz)
						continue;
					ts->pos = 0;
					ts->dat[ts->pos] = PTX_TS_NOT_SYNC;
					p->ts_pos++;
					if (p->ts_pos >= p->ts_count)
						p->ts_pos = 0;
					break;
				}
			}
			mutex_unlock(&adap->lock);
		}
		if (p->ts_info[p->ts_pos].sz < remain)
			msleep_interruptible(1);
	}
	return 0;
}

void pt3_dma_run(struct ptx_adap *adap, bool ON)
{
	struct pt3_adap	*p		= adap->priv;
	void __iomem	*base		= p->dma_base;
	u64		start_addr	= p->desc_info->adr,
			i;

	if (ON) {
		writel(1 << 18, base + PT3_TS_CTL);	/* reset error count */
		for (i = 0; i < p->ts_count; i++) {
			struct pt3_dma *ts = &p->ts_info[i];

			memset(ts->dat, 0, ts->sz);
			ts->pos = 0;
			*ts->dat = PTX_TS_NOT_SYNC;
		}
		p->ts_pos = 0;
		writel(1 << 1, base + PT3_DMA_CTL);	/* stop DMA */
		writel(start_addr & 0xffffffff, base + PT3_DMA_DESC_L);
		writel(start_addr >> 32, base + PT3_DMA_DESC_H);
		writel(1 << 0, base + PT3_DMA_CTL);	/* start DMA */
	} else {
		writel(1 << 1, base + PT3_DMA_CTL);	/* stop DMA */
		while (1) {
			if (!(readl(base + PT3_STATUS) & 1))
				break;
			msleep_interruptible(1);
		}
	}
}

int pt3_stop_feed(struct dvb_demux_feed *feed)
{
	struct ptx_adap	*adap	= container_of(feed->demux, struct ptx_adap, demux);

	if (adap->kthread) {
		pt3_dma_run(adap, false);
		kthread_stop(adap->kthread);
		adap->kthread = NULL;
	}
	return 0;
}

int pt3_start_feed(struct dvb_demux_feed *feed)
{
	int		err	= 0;
	struct ptx_adap	*adap	= container_of(feed->demux, struct ptx_adap, demux);

	if (!adap->kthread) {
		adap->kthread = kthread_run(pt3_thread, adap, "%s_%d", adap->card->name, adap->fe.id);
		if (IS_ERR(adap->kthread)) {
			err = PTR_ERR(adap->kthread);
			adap->kthread = NULL;
		} else
			pt3_dma_run(adap, true);
	}
	return err;
}

void pt3_dma_free(struct ptx_adap *adap)
{
	struct pt3_adap	*p	= adap->priv;
	struct pt3_dma	*page;
	u32 i;

	if (p->ts_info) {
		for (i = 0; i < p->ts_count; i++) {
			page = &p->ts_info[i];
			if (page->dat)
				pci_free_consistent(adap->card->pdev, page->sz, page->dat, page->adr);
		}
		kfree(p->ts_info);
	}
	if (p->desc_info) {
		for (i = 0; i < p->desc_count; i++) {
			page = &p->desc_info[i];
			if (page->dat)
				pci_free_consistent(adap->card->pdev, page->sz, page->dat, page->adr);
		}
		kfree(p->desc_info);
	}
}

int pt3_power(struct dvb_frontend *fe, u8 pwr)
{
	struct ptx_adap	*adap	= container_of(fe, struct ptx_adap, fe);
	u8		buf[]	= {0x1e, pwr | 0b10011001};
	struct i2c_msg msg[] = {
		{.addr = fe->id,	.flags = 0,	.buf = buf,	.len = 2,},
	};

	return i2c_transfer(&adap->card->i2c, msg, 1) == 1 ? 0 : -EIO;
}

void pt3_remove(struct pci_dev *pdev)
{
	struct ptx_card	*card	= pci_get_drvdata(pdev);
	struct pt3_card	*c	= card->priv;
	struct ptx_adap *adap	= card->adap;
	int i;

	if (!card)
		return;
	for (i = 0; i < card->adapn; i++, adap++) {
		pt3_dma_run(adap, false);
		pt3_dma_free(adap);
		ptx_sleep(&adap->fe);
		pt3_power(&adap->fe, PT3_PWR_OFF);
	}
	ptx_unregister_adap_fe(card);
	if (c->bar_reg) {
		writel(1 << 17, c->bar_reg + PT3_REG_I2C_W);		/* i2c_reset */
		iounmap(c->bar_reg);
	}
	if (c->bar_mem)
		iounmap(c->bar_mem);
}

int pt3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct dma_desc {
		u64 page_addr;
		u32 page_size;
		u64 next_desc;
	} __packed;
	enum {
		DMA_MAX_DESCS	= 204,
		DMA_PAGE_SIZE	= DMA_MAX_DESCS * sizeof(struct dma_desc),
		DMA_BLOCK_COUNT	= 17,
		DMA_BLOCK_SIZE	= DMA_PAGE_SIZE * 47,
		DMA_TS_BUF_SIZE	= DMA_BLOCK_SIZE * DMA_BLOCK_COUNT,
	};
	struct ptx_subdev_info	pt3_subdev_info[] = {
		{SYS_ISDBS, 0b00010001, TC90522_MODNAME, 0x63, QM1D1C0042_MODNAME},
		{SYS_ISDBS, 0b00010011, TC90522_MODNAME, 0x60, QM1D1C0042_MODNAME},
		{SYS_ISDBT, 0b00010000, TC90522_MODNAME, 0x62, MXL301RF_MODNAME},
		{SYS_ISDBT, 0b00010010, TC90522_MODNAME, 0x61, MXL301RF_MODNAME},
	};
	struct ptx_card	*card	= ptx_alloc(pdev, KBUILD_MODNAME, ARRAY_SIZE(pt3_subdev_info),
					sizeof(struct pt3_card), sizeof(struct pt3_adap), pt3_lnb);
	struct pt3_card	*c	= card->priv;
	struct ptx_adap	*adap;

	bool dma_create(void)
	{
		struct pt3_adap	*p		= adap->priv;
		struct pt3_dma	*descinfo;
		struct dma_desc	*prev		= NULL,
				*curr;
		u32		i,
				j,
				desc_remain	= 0,
				desc_info_pos	= 0;
		u64		desc_addr;

		p->ts_count	= DMA_BLOCK_COUNT;
		p->ts_info	= kcalloc(p->ts_count, sizeof(struct pt3_dma), GFP_KERNEL);
		p->desc_count	= 1 + (DMA_TS_BUF_SIZE / DMA_PAGE_SIZE - 1) / DMA_MAX_DESCS;
		p->desc_info	= kcalloc(p->desc_count, sizeof(struct pt3_dma), GFP_KERNEL);
		if (!p->ts_info || !p->desc_info)
			return false;
		for (i = 0; i < p->ts_count; i++) {
			p->ts_info[i].sz	= DMA_BLOCK_SIZE;
			p->ts_info[i].pos	= 0;
			p->ts_info[i].dat	= pci_alloc_consistent(adap->card->pdev, p->ts_info[i].sz, &p->ts_info[i].adr);
			if (!p->ts_info[i].dat)
				return false;
		}
		for (i = 0; i < p->desc_count; i++) {
			p->desc_info[i].sz	= DMA_PAGE_SIZE;
			p->desc_info[i].pos	= 0;
			p->desc_info[i].dat	= pci_alloc_consistent(adap->card->pdev, p->desc_info[i].sz, &p->desc_info[i].adr);
			if (!p->desc_info[i].dat)
				return false;
		}
		for (i = 0; i < p->ts_count; i++)
			for (j = 0; j < p->ts_info[i].sz / DMA_PAGE_SIZE; j++) {
			if (desc_remain < sizeof(struct dma_desc)) {
				descinfo	= &p->desc_info[desc_info_pos];
				descinfo->pos	= 0;
				curr		= (struct dma_desc *)&descinfo->dat[descinfo->pos];
				desc_addr	= descinfo->adr;
				desc_remain	= descinfo->sz;
				desc_info_pos++;
			}
			if (prev)
				prev->next_desc = desc_addr | 0b10;
			curr->page_addr = 0b111 | (p->ts_info[i].adr + DMA_PAGE_SIZE * j);
			curr->page_size = 0b111 | DMA_PAGE_SIZE;
			curr->next_desc = 0b10;

			prev		= curr;
			descinfo->pos	+= sizeof(struct dma_desc);
			curr		= (struct dma_desc *)&descinfo->dat[descinfo->pos];
			desc_addr	+= sizeof(struct dma_desc);
			desc_remain	-= sizeof(struct dma_desc);
		}
		prev->next_desc = p->desc_info->adr | 0b10;
		return true;
	}

	bool i2c_is_clean(void)
	{
		return (readl(c->bar_reg + PT3_REG_I2C_R) >> 3) & 1;
	}
	u8	i;
	int	err	= !card || pci_read_config_byte(pdev, PCI_CLASS_REVISION, &i);

	if (err)
		return ptx_abort(pdev, pt3_remove, err, "PCI/DMA/memory error");
	if (i != 1)
		return ptx_abort(pdev, pt3_remove, -EINVAL, "Revision 0x%X is not supported", i);
	pci_set_master(pdev);
	c->bar_reg	= pci_ioremap_bar(pdev, 0);
	c->bar_mem	= pci_ioremap_bar(pdev, 2);
	if (!c->bar_reg || !c->bar_mem)
		return ptx_abort(pdev, pt3_remove, -EIO, "Failed pci_ioremap_bar");
	err = readl(c->bar_reg + PT3_REG_VERSION);
	i = ((err >> 24) & 0xFF);
	if (i != 3)
		return ptx_abort(pdev, pt3_remove, -EIO, "ID=0x%X, not a PT3", i);
	i = ((err >>  8) & 0xFF);
	if (i != 4)
		return ptx_abort(pdev, pt3_remove, -EIO, "FPGA version 0x%X is not supported", i);
	if (ptx_i2c_add_adapter(card, &pt3_i2c_algo) || (!i2c_is_clean() && pt3_i2c_flush(c, 0)))
		return ptx_abort(pdev, pt3_remove, err, "Cannot add I2C");
	for (i = 0, adap = card->adap; i < card->adapn; i++, adap++) {
		struct dvb_frontend	*fe	= &adap->fe;
		struct pt3_adap		*p	= adap->priv;

		fe->id		= pt3_subdev_info[i].demod_addr;
		p->dma_base	= c->bar_reg + PT3_DMA_BASE + PT3_DMA_OFFSET * i;
		if (!dma_create())
			return ptx_abort(pdev, pt3_remove, -ENOMEM, "Failed dma_create");
	}
	err =	ptx_register_adap_fe(card, pt3_subdev_info, pt3_start_feed, pt3_stop_feed)	||
		pt3_power(&card->adap[i - 1].fe, PT3_PWR_TUNER_ON)				||
		pt3_i2c_flush(c, PT3_I2C_START_ADDR)						||
		pt3_power(&card->adap[i - 1].fe, PT3_PWR_TUNER_ON | PT3_PWR_AMP_ON);
	return err ? ptx_abort(pdev, pt3_remove, err, "Unable to register DVB adapter/frontend") : 0;
}

static struct pci_driver pt3_driver = {
	.name		= KBUILD_MODNAME,
	.id_table	= pt3_id,
	.probe		= pt3_probe,
	.remove		= pt3_remove,
};
module_pci_driver(pt3_driver);

