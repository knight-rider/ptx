#include "pt3.h"
#include "pt3_bus.c"
#include "pt3_i2c.c"
#include "pt3_tc.c"
#include "pt3_qm.c"
#include "pt3_mx.c"
#include "pt3_dma.c"
#include "pt3s.c"
#include "pt3t.c"

static int pt3_set_frequency(PT3_ADAPTER *adap, __u32 channel, __s32 offset)
{
	int ret;

	PT3_PRINTK(KERN_DEBUG, "#%d %s set_freq channel=%d offset=%d\n", adap->idx, adap->str, channel, offset);

	if (adap->type == SYS_ISDBS)
		ret = pt3_qm_set_frequency(adap->qm, channel);
	else
		ret = pt3_mx_set_frequency(adap, channel, offset);
	return ret;
}

static int pt3_set_tuner_sleep(PT3_ADAPTER *adap, bool sleep)
{
	int ret;

	PT3_PRINTK(KERN_INFO, "#%d %p %s %s\n", adap->idx, adap, adap->str, sleep ? "Sleep" : "Wakeup");

	if (adap->type == SYS_ISDBS) {
		ret = pt3_qm_set_sleep(adap->qm, sleep);
	} else {
		ret = pt3_mx_set_sleep(adap, sleep);
	}
	PT3_WAIT_MS_INT(10);
	return ret;
}

static int pt3_update_lnb(PT3_BOARD *pt3)
{
	u8 i, nup = 0, lnb_eff = 0;

	if (pt3->reset) {
		writel(pt3_lnb[0].bits, pt3->reg[0] + REG_SYSTEM_W);
		pt3->reset = false;
	} else {
		PT3_ADAPTER *adap;
		mutex_lock(&pt3->lock);
		for (i = 0; i < PT3_NR_ADAPS; i++) {
			adap = pt3->adap[i];
			PT3_PRINTK(KERN_DEBUG, "#%d in_use %d sleep %d\n", adap->idx, adap->in_use, adap->sleep);
			if ((adap->type == SYS_ISDBS) && (!adap->sleep)) {
				nup++;
				lnb_eff |= adap->voltage == SEC_VOLTAGE_13 ? 1 :
				           adap->voltage == SEC_VOLTAGE_18 ? 2 :
				           lnb;
			}
		}
		mutex_unlock(&pt3->lock);
		if (unlikely(lnb_eff < 0 || 2 < lnb_eff)) {
			PT3_PRINTK(KERN_ALERT, "Inconsistent LNB settings\n");
			return -EINVAL;
			}
		if (nup) writel(pt3_lnb[lnb_eff].bits, pt3->reg[0] + REG_SYSTEM_W);
	}
	PT3_PRINTK(KERN_INFO, "LNB = %s\n", pt3_lnb[lnb_eff].str);
	return 0;
}

int pt3_thread(void *data)
{
	size_t ret;
	PT3_ADAPTER *adap = data;
	loff_t ppos = 0;

	set_freezable();
	while (!kthread_should_stop()) {
		try_to_freeze();
		while ((ret = pt3_dma_copy(adap->dma, &adap->demux, &ppos)) > 0);
		if (ret < 0) {
			PT3_PRINTK(KERN_INFO, "#%d fail dma_copy\n", adap->idx);
			PT3_WAIT_MS_INT(1);
		}
	}
	return 0;
}

static int pt3_start_polling(PT3_ADAPTER *adap)
{
	int ret = 0;

	mutex_lock(&adap->lock);
	if (!adap->kthread) {
		adap->kthread = kthread_run(pt3_thread, adap, DRV_NAME "_%d", adap->idx);
		if (IS_ERR(adap->kthread)) {
			ret = PTR_ERR(adap->kthread);
			adap->kthread = NULL;
		} else {
			pt3_dma_set_test_mode(adap->dma, RESET, 0);	// reset_error_count
			pt3_dma_set_enabled(adap->dma, true);
		}
	}
	mutex_unlock(&adap->lock);
	return ret;
}

static void pt3_stop_polling(PT3_ADAPTER *adap)
{
	mutex_lock(&adap->lock);
	if (adap->kthread) {
		pt3_dma_set_enabled(adap->dma, false);
		PT3_PRINTK(KERN_INFO, "#%d DMA ts_err packet cnt %d\n",
			adap->idx, pt3_dma_get_ts_error_packet_count(adap->dma));
		kthread_stop(adap->kthread);
		adap->kthread = NULL;
	}
	mutex_unlock(&adap->lock);
}

static int pt3_start_feed(DVB_DEMUX_FEED *feed)
{
	int ret;
	PT3_ADAPTER *adap = container_of(feed->demux, PT3_ADAPTER, demux);
	if (!adap->users++) {
		if (adap->in_use) {
			PT3_PRINTK(KERN_DEBUG, "device is already used\n");
			return -EIO;
		}
		PT3_PRINTK(KERN_DEBUG, "#%d %s selected, DMA %s\n",
			adap->idx, adap->str, pt3_dma_get_status(adap->dma) & 1 ? "ON" : "OFF");
		adap->in_use = true;
		if ((ret = pt3_start_polling(adap))) return ret;
	}
	return 0;
}

static int pt3_stop_feed(DVB_DEMUX_FEED *feed)
{
	PT3_ADAPTER *adap = container_of(feed->demux, PT3_ADAPTER, demux);
	if (!--adap->users) {
		pt3_stop_polling(adap);
		adap->in_use = false;
		PT3_WAIT_MS_INT(40);
	}
	return 0;
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adapter_nr);

static PT3_ADAPTER *pt3_alloc_adapter(PT3_BOARD *pt3)
{
	PT3_ADAPTER *adap;
	DVB_ADAPTER *dvb;
	DVB_DEMUX *demux;
	DMXDEV *dmxdev;
	int ret;

	if (!(adap = kzalloc(sizeof(PT3_ADAPTER), GFP_KERNEL))) {
		ret = -ENOMEM;
		goto err;
	}
	adap->pt3 = pt3;
	adap->voltage = SEC_VOLTAGE_OFF;
	adap->sleep = true;

	dvb = &adap->dvb;
	dvb->priv = adap;
	if ((ret = dvb_register_adapter(dvb, DRV_NAME, THIS_MODULE, &pt3->pdev->dev, adapter_nr)) < 0)
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

static int pt3_tuner_init_s(PT3_I2C *i2c, PT3_ADAPTER *adap)
{
	int ret;
	PT3_BUS *bus;

	pt3_qm_init_reg_param(adap->qm);

	if (!(bus = vzalloc(sizeof(PT3_BUS))))
		return -ENOMEM;
	pt3_qm_dummy_reset(adap->qm, bus);
	pt3_bus_end(bus);
	ret = pt3_i2c_run(i2c, bus, true);
	vfree(bus);
	if (ret) {
		PT3_PRINTK(KERN_DEBUG, "fail pt3_tuner_init_s dummy reset ret=%d\n", ret);
		return ret;
	}

	if (!(bus = vzalloc(sizeof(PT3_BUS))))
		return -ENOMEM;
	if ((ret = pt3_qm_init(adap->qm, bus))) {
		vfree(bus);
		return ret;
	}
	pt3_bus_end(bus);
	ret = pt3_i2c_run(i2c, bus, true);
	vfree(bus);
	if (ret) {
		PT3_PRINTK(KERN_DEBUG, "fail pt3_tuner_init_s qm init ret=%d\n", ret);
		return ret;
	}
	return ret;
}

static int pt3_tuner_power_on(PT3_BOARD *pt3, PT3_BUS *bus)
{
	int ret, i, j;
	PT3_TS_PINS_MODE pins;

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		ret = pt3_tc_init(pt3->adap[i]);
		PT3_PRINTK(KERN_INFO, "#%d tc_init ret=%d\n", i, ret);
	}
	if ((ret = pt3_tc_set_powers(pt3->adap[PT3_NR_ADAPS-1], NULL, true, false))) {
		PT3_PRINTK(KERN_DEBUG, "fail set powers.\n");
		goto last;
	}

	pins.clock_data = PT3_TS_PIN_MODE_NORMAL;
	pins.byte = PT3_TS_PIN_MODE_NORMAL;
	pins.valid = PT3_TS_PIN_MODE_NORMAL;

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		ret = pt3_tc_set_ts_pins_mode(pt3->adap[i], &pins);
		if (ret) PT3_PRINTK(KERN_INFO, "#%d %s fail set ts pins mode ret=%d\n", i, pt3->adap[i]->str, ret);
	}
	PT3_WAIT_MS_INT(1);

	for (i = 0; i < PT3_NR_ADAPS; i++) if (pt3->adap[i]->type == SYS_ISDBS) {
		for (j = 0; j < 10; j++) {
			if (j) PT3_PRINTK(KERN_INFO, "retry pt3_tuner_init_s\n");
			if (!(ret = pt3_tuner_init_s(pt3->i2c, pt3->adap[i])))
				break;
			PT3_WAIT_MS_INT(1);
		}
		if (ret) {
			PT3_PRINTK(KERN_INFO, "fail pt3_tuner_init_s %d ret=0x%x\n", i, ret);
			goto last;
		}
	}
	if (unlikely(bus->cmd_addr < 4096))
		pt3_i2c_copy(pt3->i2c, bus);

	bus->cmd_addr = PT3_BUS_CMD_ADDR1;
	if ((ret = pt3_i2c_run(pt3->i2c, bus, false))) {
		PT3_PRINTK(KERN_INFO, "failed cmd_addr=0x%x ret=0x%x\n", PT3_BUS_CMD_ADDR1, ret);
		goto last;
	}
	if ((ret = pt3_tc_set_powers(pt3->adap[PT3_NR_ADAPS-1], NULL, true, true))) {
		PT3_PRINTK(KERN_INFO, "fail tc_set_powers,\n");
		goto last;
	}
last:
	return ret;
}

static int pt3_tuner_init_all(PT3_BOARD *pt3)
{
	int ret, i;
	PT3_I2C *i2c = pt3->i2c;
	PT3_BUS *bus = vzalloc(sizeof(PT3_BUS));

	if (!bus) return -ENOMEM;
	pt3_bus_end(bus);
	bus->cmd_addr = PT3_BUS_CMD_ADDR0;

	if (!pt3_i2c_is_clean(i2c)) {
		PT3_PRINTK(KERN_INFO, "cleanup I2C bus\n");
		if ((ret = pt3_i2c_run(i2c, bus, false)))
			goto last;
		PT3_WAIT_MS_INT(10);
	}
	if ((ret = pt3_tuner_power_on(pt3, bus)))
		goto last;
	PT3_PRINTK(KERN_DEBUG, "tuner_power_on\n");
	
	for (i = 0; i < PT3_NR_ADAPS; i++) {
		PT3_ADAPTER *adap = pt3->adap[i];
		if ((ret = pt3_set_tuner_sleep(adap, false)))
			goto last;
		if ((ret = pt3_set_frequency(adap, adap->init_ch, 0)))
			PT3_PRINTK(KERN_DEBUG, "fail set_frequency, ret=%d\n", ret);
		if ((ret = pt3_set_tuner_sleep(adap, true)))
			goto last;
	}
last:
	vfree(bus);
	return ret;
}

static void pt3_cleanup_adapters(PT3_BOARD *pt3)
{
	int i;
	PT3_ADAPTER *adap;
	for (i = 0; i < PT3_NR_ADAPS; i++) if ((adap = pt3->adap[i])) {
		if (adap->kthread) kthread_stop(adap->kthread);
		if (adap->fe) dvb_unregister_frontend(adap->fe);
		if (!adap->sleep) pt3_set_tuner_sleep(adap, true);
		if (adap->qm) vfree(adap->qm);
		if (adap->dma) {
			if (adap->dma->enabled) pt3_dma_set_enabled(adap->dma, false);
			pt3_dma_free(adap->dma);
		}
		adap->demux.dmx.close(&adap->demux.dmx);
		dvb_dmxdev_release(&adap->dmxdev);
		dvb_dmx_release(&adap->demux);
		dvb_unregister_adapter(&adap->dvb);
		kfree(adap);
	}
}

static int pt3_fe_set_voltage(DVB_FRONTEND *fe, fe_sec_voltage_t voltage)
{
	PT3_ADAPTER *adap = container_of(fe->dvb, PT3_ADAPTER, dvb);
	adap->voltage = voltage;
	pt3_update_lnb(adap->pt3);
	return (adap->orig_voltage) ? adap->orig_voltage(fe, voltage) : 0;
}

static int pt3_fe_sleep(DVB_FRONTEND *fe)
{
	PT3_ADAPTER *adap = container_of(fe->dvb, PT3_ADAPTER, dvb);
	adap->sleep = true;
	pt3_update_lnb(adap->pt3);
	return (adap->orig_sleep) ? adap->orig_sleep(fe) : 0;
}

static int pt3_fe_wakeup(DVB_FRONTEND *fe)
{
	PT3_ADAPTER *adap = container_of(fe->dvb, PT3_ADAPTER, dvb);
	adap->sleep = false;
	pt3_update_lnb(adap->pt3);
	PT3_WAIT_MS_UNINT(1);
	return (adap->orig_init) ? adap->orig_init(fe) : 0;
}

static int pt3_init_frontend(PT3_ADAPTER *adap, DVB_FRONTEND *fe)
{
	int ret = 0;

	adap->orig_voltage  = fe->ops.set_voltage;
	adap->orig_sleep    = fe->ops.sleep;
	adap->orig_init     = fe->ops.init;
	fe->ops.set_voltage = pt3_fe_set_voltage;
	fe->ops.sleep       = pt3_fe_sleep;
	fe->ops.init        = pt3_fe_wakeup;

	if ((ret = dvb_register_frontend(&adap->dvb, fe)) >= 0) adap->fe = fe;
	return ret;
}

static int pt3_init_frontends(PT3_BOARD *pt3)
{
	DVB_FRONTEND *fe[PT3_NR_ADAPS];
	int i, ret;

	for (i = 0; i < PT3_NR_ADAPS; i++) if (pt3->adap[i]->type == SYS_ISDBS) {
		if (!(fe[i] = pt3s_attach(pt3->adap[i]))) break;
	} else {
		if (!(fe[i] = pt3t_attach(pt3->adap[i]))) break;		
	}
	if (i < PT3_NR_ADAPS) {
		while (i--) fe[i]->ops.release(fe[i]);
		return -ENOMEM;
	}
	for (i = 0; i < PT3_NR_ADAPS; i++)
		if ((ret = pt3_init_frontend(pt3->adap[i], fe[i])) < 0)	{
			while(i--) dvb_unregister_frontend(fe[i]);
			for (i = 0; i < PT3_NR_ADAPS; i++) fe[i]->ops.release(fe[i]);
			return ret;
		}
	return 0;
}

static void pt3_remove(struct pci_dev *pdev)
{
	PT3_BOARD *pt3 = pci_get_drvdata(pdev);

	if (pt3) {
		pt3->reset = true;
		pt3_update_lnb(pt3);
		if (pt3->i2c) {
			if (pt3->adap[PT3_NR_ADAPS-1])
				pt3_tc_set_powers(pt3->adap[PT3_NR_ADAPS-1], NULL, false, false);
			pt3_i2c_reset(pt3->i2c);
			vfree(pt3->i2c);
		}
		pt3_cleanup_adapters(pt3);
		if (pt3->reg[1]) iounmap(pt3->reg[1]);
		if (pt3->reg[0]) iounmap(pt3->reg[0]);
		pci_release_selected_regions(pdev, pt3->bars);
		kfree(pt3);
	}
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int pt3_abort(PCI_DEV *pdev, int ret, char *fmt, ...)
{
	va_list ap;
	char *s = NULL;
	int slen;

	va_start(ap,fmt);
	if ((slen = vsnprintf(s,0,fmt,ap)) > 0) if ((s = vzalloc(slen))) {
		vsnprintf(s,slen,fmt,ap);
		dev_printk(KERN_ALERT, &pdev->dev, "%s", s);
		vfree(s);
	}
	va_end(ap);
	pt3_remove(pdev);
	return ret;
}

static int pt3_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	PT3_BOARD *pt3;
	PT3_ADAPTER *adap;
	int i, ret;

	int bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if ((ret = pci_enable_device(pdev)) < 0)
		return pt3_abort(pdev, ret, "PCI device unusable\n");
	if ((ret = pci_set_dma_mask(pdev, DMA_BIT_MASK(64))))
		return pt3_abort(pdev, ret, "DMA mask error\n");
	pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &i);
	if ((i & 0xFF) != 1)
		return pt3_abort(pdev, ret, "Revision 0x%x is not supported\n", i & 0xFF);
	if ((ret = pci_request_selected_regions(pdev, bars, DRV_NAME)) < 0)
		return pt3_abort(pdev, ret, "Could not request regions\n");

	pci_set_master(pdev);
	if ((ret = pci_save_state(pdev)))
		return pt3_abort(pdev, ret, "Failed pci_save_state\n");
	if (!(pt3 = kzalloc(sizeof(PT3_BOARD), GFP_KERNEL)))
		return pt3_abort(pdev, -ENOMEM, "PT3_BOARD out of memory\n");

	pt3->bars = bars;
	pt3->pdev = pdev;
	pci_set_drvdata(pdev, pt3);
	if (  !(pt3->reg[0] = pci_ioremap_bar(pdev, 0))
	   || !(pt3->reg[1] = pci_ioremap_bar(pdev, 2)) )
		return pt3_abort(pdev, -EIO, "Failed pci_ioremap_bar\n");

	ret = readl(pt3->reg[0] + REG_VERSION);
	if ((i = ((ret >> 24) & 0xFF)) != 3)
		return pt3_abort(pdev, -EIO, "ID=0x%x, not a PT3\n", i);
	if ((i = ((ret >>  8) & 0xFF)) != 0x04)
		return pt3_abort(pdev, -EIO, "FPGA version 0x%x is not supported\n", i);
	mutex_init(&pt3->lock);

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		pt3->adap[i] = NULL;
		if (IS_ERR(adap = pt3_alloc_adapter(pt3)))
			return pt3_abort(pdev, PTR_ERR(adap), "Failed pt3_alloc_adapter\n");
		if (!(adap->dma = pt3_dma_create(adap)))
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
			if (!(adap->qm = vzalloc(sizeof(PT3_QM))))
				return pt3_abort(pdev, -ENOMEM, "QM out of memory\n");
			adap->qm->adap = adap;
		}
		adap->sleep = true;
	}
	pt3->reset = true;
	pt3_update_lnb(pt3);

	if (!(pt3->i2c = vzalloc(sizeof(PT3_I2C))))
		return pt3_abort(pdev, -ENOMEM, "Cannot allocate I2C\n");
	mutex_init(&pt3->i2c->lock);
	pt3->i2c->reg[0] = pt3->reg[0];
	pt3->i2c->reg[1] = pt3->reg[1];

	if ((ret = pt3_tuner_init_all(pt3)))
		return pt3_abort(pdev, ret, "Failed pt3_tuner_init_all\n");
	if ((ret = pt3_init_frontends(pt3))<0)
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

