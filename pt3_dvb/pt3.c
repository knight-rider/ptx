#include "pt3.h"
#include "pt3_bus.c"
#include "pt3_i2c.c"
#include "pt3_tc.c"
#include "pt3_qm.c"
#include "pt3_mx.c"
#include "pt3_dma.c"
#include "pt3_fe.c"

MODULE_AUTHOR("Budi Rachmanto <knightrider(@)are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 DVB Driver");
MODULE_LICENSE("GPL");

static int pt3_set_frequency(struct pt3_adapter *adap, u32 channel, s32 offset)
{
	int ret;

	pr_debug("#%d %s set_freq channel=%d offset=%d\n", adap->idx, adap->str, channel, offset);

	if (adap->type == SYS_ISDBS)
		ret = pt3_qm_set_frequency(adap->qm, channel);
	else
		ret = pt3_mx_set_frequency(adap, channel, offset);
	return ret;
}

static int pt3_set_tuner_sleep(struct pt3_adapter *adap, bool sleep)
{
	int ret;

	pr_debug("#%d %p %s %s\n", adap->idx, adap, adap->str, sleep ? "Sleep" : "Wakeup");

	if (adap->type == SYS_ISDBS)
		ret = pt3_qm_set_sleep(adap->qm, sleep);
	else
		ret = pt3_mx_set_sleep(adap, sleep);
	msleep_interruptible(10);
	return ret;
}

static int pt3_update_lnb(struct pt3_board *pt3)
{
	u8 i, lnb_eff = 0;

	if (pt3->reset) {
		writel(pt3_lnb[0].bits, pt3->reg[0] + REG_SYSTEM_W);
		pt3->reset = false;
		pt3->lnb = 0;
	} else {
		struct pt3_adapter *adap;
		mutex_lock(&pt3->lock);
		for (i = 0; i < PT3_NR_ADAPS; i++) {
			adap = pt3->adap[i];
			pr_debug("#%d in_use %d sleep %d\n", adap->idx, adap->in_use, adap->sleep);
			if ((adap->type == SYS_ISDBS) && (!adap->sleep)) {
				lnb_eff |= adap->voltage == SEC_VOLTAGE_13 ? 1
					:  adap->voltage == SEC_VOLTAGE_18 ? 2
					:  lnb;
			}
		}
		mutex_unlock(&pt3->lock);
		if (unlikely(lnb_eff < 0 || 2 < lnb_eff)) {
			pr_debug("Inconsistent LNB settings\n");
			return -EINVAL;
		}
		if (pt3->lnb != lnb_eff) {
			writel(pt3_lnb[lnb_eff].bits, pt3->reg[0] + REG_SYSTEM_W);
			pt3->lnb = lnb_eff;
		}
	}
	pr_debug("LNB=%s\n", pt3_lnb[lnb_eff].str);
	return 0;
}

int pt3_thread(void *data)
{
	size_t ret;
	struct pt3_adapter *adap = data;
	loff_t ppos = 0;

	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		while ((ret = pt3_dma_copy(adap->dma, &adap->demux, &ppos)) > 0)
			;
		if (ret < 0) {
			pr_debug("#%d fail dma_copy\n", adap->idx);
			msleep_interruptible(1);
		}
	}
	return 0;
}

static int pt3_start_polling(struct pt3_adapter *adap)
{
	int ret = 0;

	mutex_lock(&adap->lock);
	if (!adap->kthread) {
		adap->kthread = kthread_run(pt3_thread, adap, DRV_NAME "_%d", adap->idx);
		if (IS_ERR(adap->kthread)) {
			ret = PTR_ERR(adap->kthread);
			adap->kthread = NULL;
		} else {
			pt3_dma_set_test_mode(adap->dma, RESET, 0);	/* reset_error_count */
			pt3_dma_set_enabled(adap->dma, true);
		}
	}
	mutex_unlock(&adap->lock);
	return ret;
}

static void pt3_stop_polling(struct pt3_adapter *adap)
{
	mutex_lock(&adap->lock);
	if (adap->kthread) {
		pt3_dma_set_enabled(adap->dma, false);
		pr_debug("#%d DMA ts_err packet cnt %d\n",
			adap->idx, pt3_dma_get_ts_error_packet_count(adap->dma));
		kthread_stop(adap->kthread);
		adap->kthread = NULL;
	}
	mutex_unlock(&adap->lock);
}

static int pt3_start_feed(struct dvb_demux_feed *feed)
{
	int ret;
	struct pt3_adapter *adap = container_of(feed->demux, struct pt3_adapter, demux);
	if (!adap->users++) {
		if (adap->in_use) {
			pr_debug("device is already used\n");
			return -EIO;
		}
		pr_debug("#%d %s selected, DMA %s\n",
			adap->idx, adap->str, pt3_dma_get_status(adap->dma) & 1 ? "ON" : "OFF");
		adap->in_use = true;
		ret = pt3_start_polling(adap);
		if (ret)
			return ret;
	}
	return 0;
}

static int pt3_stop_feed(struct dvb_demux_feed *feed)
{
	struct pt3_adapter *adap = container_of(feed->demux, struct pt3_adapter, demux);
	if (!--adap->users) {
		pt3_stop_polling(adap);
		adap->in_use = false;
		msleep_interruptible(40);
	}
	return 0;
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static struct pt3_adapter *pt3_alloc_adapter(struct pt3_board *pt3)
{
	int ret;
	struct dvb_adapter *dvb;
	struct dvb_demux *demux;
	struct dmxdev *dmxdev;
	struct pt3_adapter *adap = kzalloc(sizeof(struct pt3_adapter), GFP_KERNEL);

	if (!adap) {
		ret = -ENOMEM;
		goto err;
	}
	adap->pt3 = pt3;
	adap->voltage = SEC_VOLTAGE_OFF;
	adap->sleep = true;

	dvb = &adap->dvb;
	dvb->priv = adap;
	ret = dvb_register_adapter(dvb, DRV_NAME, THIS_MODULE, &pt3->pdev->dev, adapter_nr);
	if (ret < 0)
		goto err_kfree;

	demux = &adap->demux;
	demux->dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
	demux->priv = adap;
	demux->feednum = 256;
	demux->filternum = 256;
	demux->start_feed = pt3_start_feed;
	demux->stop_feed = pt3_stop_feed;
	demux->write_to_decoder = NULL;
	ret = dvb_dmx_init(demux);
	if (ret < 0)
		goto err_unregister_adapter;

	dmxdev = &adap->dmxdev;
	dmxdev->filternum = 256;
	dmxdev->demux = &demux->dmx;
	dmxdev->capabilities = 0;
	ret = dvb_dmxdev_init(dmxdev, dvb);
	if (ret < 0)
		goto err_dmx_release;

	return adap;

err_dmx_release:
	dvb_dmx_release(demux);
err_unregister_adapter:
	dvb_unregister_adapter(dvb);
err_kfree:
	kfree(adap);
err:
	return ERR_PTR(ret);
}

static int pt3_tuner_init_s(struct pt3_i2c *i2c, struct pt3_adapter *adap)
{
	int ret;
	struct pt3_bus *bus = vzalloc(sizeof(struct pt3_bus));

	if (!bus)
		return -ENOMEM;
	pt3_qm_init_reg_param(adap->qm);
	pt3_qm_dummy_reset(adap->qm, bus);
	pt3_bus_end(bus);
	ret = pt3_i2c_run(i2c, bus, true);
	vfree(bus);
	if (ret) {
		pr_debug("fail pt3_tuner_init_s dummy reset ret=%d\n", ret);
		return ret;
	}

	bus = vzalloc(sizeof(struct pt3_bus));
	if (!bus)
		return -ENOMEM;
	ret = pt3_qm_init(adap->qm, bus);
	if (ret) {
		vfree(bus);
		return ret;
	}
	pt3_bus_end(bus);
	ret = pt3_i2c_run(i2c, bus, true);
	vfree(bus);
	if (ret) {
		pr_debug("fail pt3_tuner_init_s qm init ret=%d\n", ret);
		return ret;
	}
	return ret;
}

static int pt3_tuner_power_on(struct pt3_board *pt3, struct pt3_bus *bus)
{
	int ret, i, j;
	struct pt3_ts_pins_mode pins;

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		ret = pt3_tc_init(pt3->adap[i]);
		pr_debug("#%d tc_init ret=%d\n", i, ret);
	}
	ret = pt3_tc_set_powers(pt3->adap[PT3_NR_ADAPS-1], NULL, true, false);
	if (ret) {
		pr_debug("fail set powers.\n");
		goto last;
	}

	pins.clock_data = PT3_TS_PIN_MODE_NORMAL;
	pins.byte       = PT3_TS_PIN_MODE_NORMAL;
	pins.valid      = PT3_TS_PIN_MODE_NORMAL;

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		ret = pt3_tc_set_ts_pins_mode(pt3->adap[i], &pins);
		if (ret)
			pr_debug("#%d %s fail set ts pins mode ret=%d\n", i, pt3->adap[i]->str, ret);
	}
	msleep_interruptible(1);

	for (i = 0; i < PT3_NR_ADAPS; i++)
		if (pt3->adap[i]->type == SYS_ISDBS) {
			for (j = 0; j < 10; j++) {
				if (j)
					pr_debug("retry pt3_tuner_init_s\n");
				ret = pt3_tuner_init_s(pt3->i2c, pt3->adap[i]);
				if (!ret)
					break;
				msleep_interruptible(1);
			}
			if (ret) {
				pr_debug("fail pt3_tuner_init_s %d ret=0x%x\n", i, ret);
				goto last;
			}
		}
	if (unlikely(bus->cmd_addr < 4096))
		pt3_i2c_copy(pt3->i2c, bus);

	bus->cmd_addr = PT3_BUS_CMD_ADDR1;
	ret = pt3_i2c_run(pt3->i2c, bus, false);
	if (ret) {
		pr_debug("failed cmd_addr=0x%x ret=0x%x\n", PT3_BUS_CMD_ADDR1, ret);
		goto last;
	}
	ret = pt3_tc_set_powers(pt3->adap[PT3_NR_ADAPS-1], NULL, true, true);
	if (ret) {
		pr_debug("fail tc_set_powers,\n");
		goto last;
	}
last:
	return ret;
}

static int pt3_tuner_init_all(struct pt3_board *pt3)
{
	int ret, i;
	struct pt3_i2c *i2c = pt3->i2c;
	struct pt3_bus *bus = vzalloc(sizeof(struct pt3_bus));

	if (!bus)
		return -ENOMEM;
	pt3_bus_end(bus);
	bus->cmd_addr = PT3_BUS_CMD_ADDR0;

	if (!pt3_i2c_is_clean(i2c)) {
		pr_debug("cleanup I2C bus\n");
		ret = pt3_i2c_run(i2c, bus, false);
		if (ret)
			goto last;
		msleep_interruptible(10);
	}
	ret = pt3_tuner_power_on(pt3, bus);
	if (ret)
		goto last;
	pr_debug("tuner_power_on\n");

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		struct pt3_adapter *adap = pt3->adap[i];
		ret = pt3_set_tuner_sleep(adap, false);
		if (ret)
			goto last;
		ret = pt3_set_frequency(adap, adap->init_ch, 0);
		if (ret)
			pr_debug("fail set_frequency, ret=%d\n", ret);
		ret = pt3_set_tuner_sleep(adap, true);
		if (ret)
			goto last;
	}
last:
	vfree(bus);
	return ret;
}

static void pt3_cleanup_adapter(struct pt3_adapter *adap)
{
	if (!adap)
		return;
	if (adap->kthread)
		kthread_stop(adap->kthread);
	if (adap->fe)
		dvb_unregister_frontend(adap->fe);
	if (!adap->sleep)
		pt3_set_tuner_sleep(adap, true);
	if (adap->qm)
		vfree(adap->qm);
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

static int pt3_fe_set_voltage(struct dvb_frontend *fe, fe_sec_voltage_t voltage)
{
	struct pt3_adapter *adap = container_of(fe->dvb, struct pt3_adapter, dvb);
	adap->voltage = voltage;
	return (adap->orig_voltage) ? adap->orig_voltage(fe, voltage) : 0;
}

static int pt3_fe_sleep(struct dvb_frontend *fe)
{
	struct pt3_adapter *adap = container_of(fe->dvb, struct pt3_adapter, dvb);
	adap->sleep = true;
	pt3_update_lnb(adap->pt3);
	return (adap->orig_sleep) ? adap->orig_sleep(fe) : 0;
}

static int pt3_fe_wakeup(struct dvb_frontend *fe)
{
	struct pt3_adapter *adap = container_of(fe->dvb, struct pt3_adapter, dvb);
	adap->sleep = false;
	pt3_update_lnb(adap->pt3);
	return (adap->orig_init) ? adap->orig_init(fe) : 0;
}

static int pt3_init_frontends(struct pt3_board *pt3)
{
	struct dvb_frontend *fe[PT3_NR_ADAPS];
	int i, ret;

	for (i = 0; i < PT3_NR_ADAPS; i++)
		if (pt3->adap[i]->type == SYS_ISDBS) {
			fe[i] = pt3_fe_s_attach(pt3->adap[i]);
			if (!fe[i])
				break;
		} else {
			fe[i] = pt3_fe_t_attach(pt3->adap[i]);
			if (!fe[i])
				break;
		}
	if (i < PT3_NR_ADAPS) {
		while (i--)
			fe[i]->ops.release(fe[i]);
		return -ENOMEM;
	}
	for (i = 0; i < PT3_NR_ADAPS; i++) {
		struct pt3_adapter *adap = pt3->adap[i];

		adap->orig_voltage     = fe[i]->ops.set_voltage;
		adap->orig_sleep       = fe[i]->ops.sleep;
		adap->orig_init        = fe[i]->ops.init;
		fe[i]->ops.set_voltage = pt3_fe_set_voltage;
		fe[i]->ops.sleep       = pt3_fe_sleep;
		fe[i]->ops.init        = pt3_fe_wakeup;

		ret = dvb_register_frontend(&adap->dvb, fe[i]);
		if (ret >= 0)
			adap->fe = fe[i];
		else {
			while (i--)
				dvb_unregister_frontend(fe[i]);
			for (i = 0; i < PT3_NR_ADAPS; i++)
				fe[i]->ops.release(fe[i]);
			return ret;
		}
	}
	return 0;
}

static void pt3_remove(struct pci_dev *pdev)
{
	int i;
	struct pt3_board *pt3 = pci_get_drvdata(pdev);

	if (pt3) {
		pt3->reset = true;
		pt3_update_lnb(pt3);
		if (pt3->i2c) {
			if (pt3->adap[PT3_NR_ADAPS-1])
				pt3_tc_set_powers(pt3->adap[PT3_NR_ADAPS-1], NULL, false, false);
			pt3_i2c_reset(pt3->i2c);
			vfree(pt3->i2c);
		}
		for (i = 0; i < PT3_NR_ADAPS; i++)
			pt3_cleanup_adapter(pt3->adap[i]);
		if (pt3->reg[1])
			iounmap(pt3->reg[1]);
		if (pt3->reg[0])
			iounmap(pt3->reg[0]);
		pci_release_selected_regions(pdev, pt3->bars);
		kfree(pt3);
	}
	pci_disable_device(pdev);
}

static int pt3_abort(struct pci_dev *pdev, int ret, char *fmt, ...)
{
	va_list ap;
	char *s = NULL;
	int slen;

	va_start(ap, fmt);
	slen = vsnprintf(s, 0, fmt, ap);
	s = vzalloc(slen);
	if (slen > 0 && s) {
		vsnprintf(s, slen, fmt, ap);
		dev_alert(&pdev->dev, "%s", s);
		vfree(s);
	}
	va_end(ap);
	pt3_remove(pdev);
	return ret;
}

static int pt3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	struct pt3_board *pt3;
	struct pt3_adapter *adap;
	int i, ret, bars = pci_select_bars(pdev, IORESOURCE_MEM);

	ret = pci_enable_device(pdev);
	if (ret < 0)
		return pt3_abort(pdev, ret, "PCI device unusable\n");
	ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (ret)
		return pt3_abort(pdev, ret, "DMA mask error\n");
	pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &i);
	if ((i & 0xFF) != 1)
		return pt3_abort(pdev, ret, "Revision 0x%x is not supported\n", i & 0xFF);
	ret = pci_request_selected_regions(pdev, bars, DRV_NAME);
	if (ret < 0)
		return pt3_abort(pdev, ret, "Could not request regions\n");

	pci_set_master(pdev);
	ret = pci_save_state(pdev);
	if (ret)
		return pt3_abort(pdev, ret, "Failed pci_save_state\n");
	pt3 = kzalloc(sizeof(struct pt3_board), GFP_KERNEL);
	if (!pt3)
		return pt3_abort(pdev, -ENOMEM, "struct pt3_board out of memory\n");

	pt3->bars = bars;
	pt3->pdev = pdev;
	pci_set_drvdata(pdev, pt3);
	pt3->reg[0] = pci_ioremap_bar(pdev, 0);
	pt3->reg[1] = pci_ioremap_bar(pdev, 2);
	if (!pt3->reg[0] || !pt3->reg[1])
		return pt3_abort(pdev, -EIO, "Failed pci_ioremap_bar\n");

	ret = readl(pt3->reg[0] + REG_VERSION);
	i = ((ret >> 24) & 0xFF);
	if (i != 3)
		return pt3_abort(pdev, -EIO, "ID=0x%x, not a PT3\n", i);
	i = ((ret >>  8) & 0xFF);
	if (i != 4)
		return pt3_abort(pdev, -EIO, "FPGA version 0x%x is not supported\n", i);
	mutex_init(&pt3->lock);

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		pt3->adap[i] = NULL;
		adap = pt3_alloc_adapter(pt3);
		if (IS_ERR(adap))
			return pt3_abort(pdev, PTR_ERR(adap), "Failed pt3_alloc_adapter\n");
		adap->dma = pt3_dma_create(adap);
		if (!adap->dma)
			return pt3_abort(pdev, -ENOMEM, "Failed pt3_dma_create\n");
		mutex_init(&adap->lock);
		adap->idx = i;
		pt3->adap[i] = adap;
		adap->type       = pt3_config[i].type;
		adap->addr_tuner = pt3_config[i].addr_tuner;
		adap->addr_tc    = pt3_config[i].addr_tc;
		adap->init_ch    = pt3_config[i].init_ch;
		adap->str        = pt3_config[i].str;
		if (adap->type == SYS_ISDBS) {
			adap->qm = vzalloc(sizeof(struct pt3_qm));
			if (!adap->qm)
				return pt3_abort(pdev, -ENOMEM, "QM out of memory\n");
			adap->qm->adap = adap;
		}
		adap->sleep = true;
	}
	pt3->reset = true;
	pt3_update_lnb(pt3);

	pt3->i2c = vzalloc(sizeof(struct pt3_i2c));
	if (!pt3->i2c)
		return pt3_abort(pdev, -ENOMEM, "Cannot allocate I2C\n");
	mutex_init(&pt3->i2c->lock);
	pt3->i2c->reg[0] = pt3->reg[0];
	pt3->i2c->reg[1] = pt3->reg[1];

	ret = pt3_tuner_init_all(pt3);
	if (ret)
		return pt3_abort(pdev, ret, "Failed pt3_tuner_init_all\n");
	ret = pt3_init_frontends(pt3);
	if (ret < 0)
		return pt3_abort(pdev, ret, "Failed pt3_init_frontends\n");
	return ret;
}

static struct pci_driver pt3_driver = {
	.name		= DRV_NAME,
	.probe		= pt3_probe,
	.remove		= pt3_remove,
	.id_table	= pt3_id_table,
};

module_pci_driver(pt3_driver);

