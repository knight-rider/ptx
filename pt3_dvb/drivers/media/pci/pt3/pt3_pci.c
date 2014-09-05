/*
 * DVB driver for Earthsoft PT3 ISDB-S/T PCIE bridge Altera Cyclone IV FPGA EP4CGX15BF14C8N
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

#include "pt3_dma.h"
#include "pt3_i2c.h"
#include "tc90522.h"
#include "qm1d1c0042.h"
#include "mxl301rf.h"

MODULE_AUTHOR("Budi Rachmanto, AreMa Inc. <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 DVB Driver");
MODULE_LICENSE("GPL");

static struct pci_device_id pt3_id_table[] = {
	{ PCI_DEVICE(0x1172, 0x4c15) },
	{ },
};
MODULE_DEVICE_TABLE(pci, pt3_id_table);

static int lnb = 2;
module_param(lnb, int, 0);
MODULE_PARM_DESC(lnb, "LNB level (0:OFF 1:+11V 2:+15V)");

struct pt3_lnb {
	u32 bits;
	char *str;
};

static const struct pt3_lnb pt3_lnb[] = {
	{0b1100,  "0V"},
	{0b1101, "11V"},
	{0b1111, "15V"},
};

struct pt3_cfg {
	fe_delivery_system_t type;
	u8 addr_tuner, addr_demod;
};

static const struct pt3_cfg pt3_cfg[] = {
	{SYS_ISDBS, 0x63, 0b00010001},
	{SYS_ISDBS, 0x60, 0b00010011},
	{SYS_ISDBT, 0x62, 0b00010000},
	{SYS_ISDBT, 0x61, 0b00010010},
};
#define PT3_ADAPN ARRAY_SIZE(pt3_cfg)

int pt3_update_lnb(struct pt3_board *pt3)
{
	u8 i, lnb_eff = 0;

	if (pt3->reset) {
		writel(pt3_lnb[0].bits, pt3->bar_reg + PT3_REG_SYS_W);
		pt3->reset = false;
		pt3->lnb = 0;
	} else {
		struct pt3_adapter *adap;
		for (i = 0; i < PT3_ADAPN; i++) {
			adap = pt3->adap[i];
			dev_dbg(adap->dvb.device, "#%d sleep %d\n", adap->idx, adap->sleep);
			if ((pt3_cfg[i].type == SYS_ISDBS) && (!adap->sleep))
				lnb_eff |= adap->voltage ? adap->voltage : lnb;
		}
		if (unlikely(lnb_eff < 0 || 2 < lnb_eff)) {
			dev_err(&pt3->pdev->dev, "Inconsistent LNB settings\n");
			return -EINVAL;
		}
		if (pt3->lnb != lnb_eff) {
			writel(pt3_lnb[lnb_eff].bits, pt3->bar_reg + PT3_REG_SYS_W);
			pt3->lnb = lnb_eff;
		}
	}
	dev_dbg(&pt3->pdev->dev, "LNB=%s\n", pt3_lnb[lnb_eff].str);
	return 0;
}

int pt3_thread(void *data)
{
	size_t ret;
	struct pt3_adapter *adap = data;

	dev_dbg(adap->dvb.device, "#%d %s sleep %d\n", adap->idx, __func__, adap->sleep);
	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		while ((ret = pt3_dma_copy(adap->dma, &adap->demux)) > 0)
			;
		if (ret < 0) {
			dev_dbg(adap->dvb.device, "#%d fail dma_copy\n", adap->idx);
			msleep_interruptible(1);
		}
	}
	return 0;
}

int pt3_start_feed(struct dvb_demux_feed *feed)
{
	int ret = 0;
	struct pt3_adapter *adap = container_of(feed->demux, struct pt3_adapter, demux);
	dev_dbg(adap->dvb.device, "#%d %s sleep %d\n", adap->idx, __func__, adap->sleep);
	if (!adap->users++) {
		dev_dbg(adap->dvb.device, "#%d %s selected, DMA %s\n",
			adap->idx, (pt3_cfg[adap->idx].type == SYS_ISDBS) ? "S" : "T",
			pt3_dma_get_status(adap->dma) & 1 ? "ON" : "OFF");
		mutex_lock(&adap->lock);
		if (!adap->kthread) {
			adap->kthread = kthread_run(pt3_thread, adap, DRV_NAME "_%d", adap->idx);
			if (IS_ERR(adap->kthread)) {
				ret = PTR_ERR(adap->kthread);
				adap->kthread = NULL;
			} else {
				pt3_dma_set_test_mode(adap->dma, RESET, 0);	/* reset error count */
				pt3_dma_set_enabled(adap->dma, true);
			}
		}
		mutex_unlock(&adap->lock);
		if (ret)
			return ret;
	}
	return 0;
}

int pt3_stop_feed(struct dvb_demux_feed *feed)
{
	struct pt3_adapter *adap = container_of(feed->demux, struct pt3_adapter, demux);
	dev_dbg(adap->dvb.device, "#%d %s sleep %d\n", adap->idx, __func__, adap->sleep);
	if (!--adap->users) {
		mutex_lock(&adap->lock);
		if (adap->kthread) {
			pt3_dma_set_enabled(adap->dma, false);
			dev_dbg(adap->dvb.device, "#%d DMA ts_err packet cnt %d\n",
				adap->idx, pt3_dma_get_ts_error_packet_count(adap->dma));
			kthread_stop(adap->kthread);
			adap->kthread = NULL;
		}
		mutex_unlock(&adap->lock);
		msleep_interruptible(40);
	}
	return 0;
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

struct pt3_adapter *pt3_dvb_register_adapter(struct pt3_board *pt3)
{
	int ret;
	struct dvb_adapter *dvb;
	struct dvb_demux *demux;
	struct dmxdev *dmxdev;
	struct pt3_adapter *adap = kzalloc(sizeof(struct pt3_adapter), GFP_KERNEL);
	if (!adap)
		return ERR_PTR(-ENOMEM);

	adap->pt3 = pt3;
	adap->sleep = true;

	dvb = &adap->dvb;
	dvb->priv = adap;
	ret = dvb_register_adapter(dvb, DRV_NAME, THIS_MODULE, &pt3->pdev->dev, adapter_nr);
	dev_dbg(dvb->device, "adapter%d registered\n", ret);
	if (ret >= 0) {
		demux = &adap->demux;
		demux->dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
		demux->priv = adap;
		demux->feednum = 256;
		demux->filternum = 256;
		demux->start_feed = pt3_start_feed;
		demux->stop_feed = pt3_stop_feed;
		demux->write_to_decoder = NULL;
		ret = dvb_dmx_init(demux);
		if (ret >= 0) {
			dmxdev = &adap->dmxdev;
			dmxdev->filternum = 256;
			dmxdev->demux = &demux->dmx;
			dmxdev->capabilities = 0;
			ret = dvb_dmxdev_init(dmxdev, dvb);
			if (ret >= 0)
				return adap;
			dvb_dmx_release(demux);
		}
		dvb_unregister_adapter(dvb);
	}
	kfree(adap);
	return ERR_PTR(ret);
}

int pt3_sleep(struct dvb_frontend *fe)
{
	struct pt3_adapter *adap = container_of(fe->dvb, struct pt3_adapter, dvb);
	dev_dbg(adap->dvb.device, "#%d %s orig %p\n", adap->idx, __func__, adap->orig_sleep);
	adap->sleep = true;
	pt3_update_lnb(adap->pt3);
	return (adap->orig_sleep) ? adap->orig_sleep(fe) : 0;
}

int pt3_wakeup(struct dvb_frontend *fe)
{
	struct pt3_adapter *adap = container_of(fe->dvb, struct pt3_adapter, dvb);
	dev_dbg(adap->dvb.device, "#%d %s orig %p\n", adap->idx, __func__, adap->orig_init);
	adap->sleep = false;
	pt3_update_lnb(adap->pt3);
	return (adap->orig_init) ? adap->orig_init(fe) : 0;
}

int pt3_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct pt3_adapter *adap = container_of(fe->dvb, struct pt3_adapter, dvb);
	adap->voltage = voltage == SEC_VOLTAGE_18 ? 2 : voltage == SEC_VOLTAGE_13 ? 1 : 0;
	return (adap->orig_voltage) ? adap->orig_voltage(fe, voltage) : 0;
}

void pt3_cleanup_adapter(struct pt3_adapter *adap)
{
	if (!adap)
		return;
	if (adap->kthread)
		kthread_stop(adap->kthread);
	if (adap->fe) {
		dvb_unregister_frontend(adap->fe);
		adap->fe->ops.release(adap->fe);
	}
	if (adap->dma) {
		if (adap->dma->enabled)
			pt3_dma_set_enabled(adap->dma, false);
		pt3_dma_free(adap->dma);
	}
	adap->demux.dmx.close(&adap->demux.dmx);
	dvb_dmxdev_release(&adap->dmxdev);
	dvb_dmx_release(&adap->demux);
	dvb_unregister_adapter(&adap->dvb);
	kfree(adap);
}

void pt3_remove(struct pci_dev *pdev)
{
	int i;
	struct pt3_board *pt3 = pci_get_drvdata(pdev);

	if (pt3) {
		pt3->reset = true;
		pt3_update_lnb(pt3);
		for (i = 0; i < PT3_ADAPN; i++)
			pt3_cleanup_adapter(pt3->adap[i]);
		pt3_i2c_reset(pt3);
		i2c_del_adapter(&pt3->i2c);
		if (pt3->bar_mem)
			iounmap(pt3->bar_mem);
		if (pt3->bar_reg)
			iounmap(pt3->bar_reg);
		pci_release_selected_regions(pdev, pt3->bars);
		kfree(pt3->adap);
		kfree(pt3);
	}
	pci_disable_device(pdev);
}

int pt3_abort(struct pci_dev *pdev, int ret, char *fmt, ...)
{
	va_list ap;
	char *s = NULL;
	int slen;

	va_start(ap, fmt);
	slen = vsnprintf(s, 0, fmt, ap);
	s = vzalloc(slen);
	if (slen > 0 && s) {
		vsnprintf(s, slen, fmt, ap);
		dev_err(&pdev->dev, "%s", s);
		vfree(s);
	}
	va_end(ap);
	pt3_remove(pdev);
	return ret;
}

int pt3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pt3_board *pt3;
	struct pt3_adapter *adap;
	const struct pt3_cfg *cfg = pt3_cfg;
	struct dvb_frontend *fe[PT3_ADAPN];
	int i, err, bars = pci_select_bars(pdev, IORESOURCE_MEM);

	err = pci_enable_device(pdev)					||
		pci_set_dma_mask(pdev, DMA_BIT_MASK(64))		||
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64))	||
		pci_read_config_dword(pdev, PCI_CLASS_REVISION, &i)	||
		pci_request_selected_regions(pdev, bars, DRV_NAME);
	if (err)
		return pt3_abort(pdev, err, "PCI/DMA error\n");
	if ((i & 0xFF) != 1)
		return pt3_abort(pdev, -EINVAL, "Revision 0x%x is not supported\n", i & 0xFF);

	pci_set_master(pdev);
	err = pci_save_state(pdev);
	if (err)
		return pt3_abort(pdev, err, "Failed pci_save_state\n");
	pt3 = kzalloc(sizeof(struct pt3_board), GFP_KERNEL);
	if (!pt3)
		return pt3_abort(pdev, -ENOMEM, "struct pt3_board out of memory\n");
	pt3->adap = kzalloc(PT3_ADAPN * sizeof(struct pt3_adapter *), GFP_KERNEL);
	if (!pt3->adap)
		return pt3_abort(pdev, -ENOMEM, "No memory for *adap\n");

	pt3->bars = bars;
	pt3->pdev = pdev;
	pci_set_drvdata(pdev, pt3);
	pt3->bar_reg = pci_ioremap_bar(pdev, 0);
	pt3->bar_mem = pci_ioremap_bar(pdev, 2);
	if (!pt3->bar_reg || !pt3->bar_mem)
		return pt3_abort(pdev, -EIO, "Failed pci_ioremap_bar\n");

	err = readl(pt3->bar_reg + PT3_REG_VERSION);
	i = ((err >> 24) & 0xFF);
	if (i != 3)
		return pt3_abort(pdev, -EIO, "ID=0x%x, not a PT3\n", i);
	i = ((err >>  8) & 0xFF);
	if (i != 4)
		return pt3_abort(pdev, -EIO, "FPGA version 0x%x is not supported\n", i);
	err = pt3_i2c_add_adapter(pt3);
	if (err < 0)
		return pt3_abort(pdev, err, "Cannot add I2C\n");
	mutex_init(&pt3->lock);

	for (i = 0; i < PT3_ADAPN; i++) {
		adap = pt3_dvb_register_adapter(pt3);
		if (IS_ERR(adap))
			return pt3_abort(pdev, PTR_ERR(adap), "Failed pt3_dvb_register_adapter\n");
		adap->idx = i;
		adap->dma = pt3_dma_create(adap);
		if (!adap->dma)
			return pt3_abort(pdev, -ENOMEM, "Failed pt3_dma_create\n");
		pt3->adap[i] = adap;
		adap->sleep = true;
		mutex_init(&adap->lock);
	}

	for (i = 0; i < PT3_ADAPN; i++) {
		fe[i] = tc90522_attach(&pt3->i2c, cfg[i].type, cfg[i].addr_demod, i + 1 == PT3_ADAPN);
		if (!fe[i] || (cfg[i].type == SYS_ISDBS ?
			qm1d1c0042_attach(fe[i], cfg[i].addr_tuner) : mxl301rf_attach(fe[i], cfg[i].addr_tuner))) {
			while (i--)
				fe[i]->ops.release(fe[i]);
			return pt3_abort(pdev, -ENOMEM, "Cannot attach frontend\n");
		}
	}

	for (i = 0; i < PT3_ADAPN; i++) {
		struct pt3_adapter *adap = pt3->adap[i];
		dev_dbg(&pdev->dev, "#%d %s\n", i, __func__);

		adap->orig_voltage	= fe[i]->ops.set_voltage;
		adap->orig_sleep	= fe[i]->ops.sleep;
		adap->orig_init		= fe[i]->ops.init;
		fe[i]->ops.set_voltage	= pt3_set_voltage;
		fe[i]->ops.sleep	= pt3_sleep;
		fe[i]->ops.init		= pt3_wakeup;
		fe[i]->dvb		= &adap->dvb;
		if ((adap->orig_init(fe[i]) && adap->orig_init(fe[i]) && adap->orig_init(fe[i])) ||
			adap->orig_sleep(fe[i]) || dvb_register_frontend(&adap->dvb, fe[i])) {
			while (i--)
				dvb_unregister_frontend(fe[i]);
			for (i = 0; i < PT3_ADAPN; i++) {
				fe[i]->ops.release(fe[i]);
				adap->fe = NULL;
			}
			return pt3_abort(pdev, -EREMOTEIO, "Cannot register frontend\n");
		}
		adap->fe = fe[i];
	}
	pt3->reset = true;
	pt3_update_lnb(pt3);
	return 0;
}

static struct pci_driver pt3_driver = {
	.name		= DRV_NAME,
	.probe		= pt3_probe,
	.remove		= pt3_remove,
	.id_table	= pt3_id_table,
};

module_pci_driver(pt3_driver);

