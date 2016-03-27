/*
	Common procedures for PT3, PX-Q3PE, and other DVB drivers

	Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
*/

#include "ptx_common.h"

void ptx_lnb(struct ptx_card *card)
{
	struct ptx_adap	*adap;
	int	i;
	bool	lnb = false;

	for (i = 0, adap = card->adap; adap->fe && i < card->adapn; i++, adap++)
		if (adap->fe->dtv_property_cache.delivery_system == SYS_ISDBS && adap->ON) {
			lnb = true;
			break;
	}
	if (card->lnbON != lnb) {
		card->lnb(card, lnb);
		card->lnbON = lnb;
	}
}

int ptx_sleep(struct dvb_frontend *fe)
{
	struct ptx_adap	*adap	= container_of(fe->dvb, struct ptx_adap, dvb);

	adap->ON = false;
	ptx_lnb(adap->card);
	return adap->fe_sleep ? adap->fe_sleep(fe) : 0;
}

int ptx_wakeup(struct dvb_frontend *fe)
{
	struct ptx_adap	*adap	= container_of(fe->dvb, struct ptx_adap, dvb);

	adap->ON = true;
	ptx_lnb(adap->card);
	return adap->fe_wakeup ? adap->fe_wakeup(fe) : 0;
}

int ptx_stop_feed(struct dvb_demux_feed *feed)
{
	struct ptx_adap	*adap	= container_of(feed->demux, struct ptx_adap, demux);

	adap->card->dma(adap, false);
	if (adap->kthread)
		kthread_stop(adap->kthread);
	return 0;
}

int ptx_start_feed(struct dvb_demux_feed *feed)
{
	struct ptx_adap	*adap	= container_of(feed->demux, struct ptx_adap, demux);

	if (adap->card->thread)
		adap->kthread = kthread_run(adap->card->thread, adap, "%s_%d%c", adap->dvb.name, adap->dvb.num,
					adap->fe->dtv_property_cache.delivery_system == SYS_ISDBS ? 's' : 't');
	return IS_ERR(adap->kthread) ? PTR_ERR(adap->kthread) : adap->card->dma(adap, true);
}

struct ptx_card *ptx_alloc(struct pci_dev *pdev, u8 *name, u8 adapn, u32 sz_card_priv, u32 sz_adap_priv,
			void (*lnb)(struct ptx_card *, bool))
{
	u8 i;
	struct ptx_card *card = kzalloc(sizeof(struct ptx_card) + sz_card_priv + adapn *
					(sizeof(struct ptx_adap) + sz_adap_priv), GFP_KERNEL);
	if (!card)
		return NULL;
	card->priv	= sz_card_priv ? &card[1] : NULL;
	card->adap	= (struct ptx_adap *)((u8 *)&card[1] + sz_card_priv);
	card->pdev	= pdev;
	card->adapn	= adapn;
	card->name	= name;
	card->lnbON	= true;
	card->lnb	= lnb;
	for (i = 0; i < card->adapn; i++) {
		struct ptx_adap *p = &card->adap[i];

		p->card	= card;
		p->priv	= sz_adap_priv ? (u8 *)&card->adap[card->adapn] + i * sz_adap_priv : NULL;
	}
	if (pci_enable_device(pdev)					||
		pci_set_dma_mask(pdev, DMA_BIT_MASK(32))		||
		pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(32))	||
		pci_request_regions(pdev, name)) {
		kfree(card);
		return NULL;
	}
	pci_set_drvdata(pdev, card);
	return card;
}

int ptx_i2c_add_adapter(struct ptx_card *card, const struct i2c_algorithm *algo)
{
	struct i2c_adapter *i2c = &card->i2c;

	i2c->algo	= algo;
	i2c->dev.parent	= &card->pdev->dev;
	strcpy(i2c->name, card->name);
	i2c_set_adapdata(i2c, card);
	mutex_init(&card->lock);
	return	i2c_add_adapter(i2c);
}

void ptx_unregister_subdev(struct i2c_client *c)
{
	if (!c)
		return;
	if (c->dev.driver)
		module_put(c->dev.driver->owner);
	i2c_unregister_device(c);
}

struct i2c_client *ptx_register_subdev(struct i2c_adapter *i2c, struct dvb_frontend *fe, u16 adr, char *name)
{
	struct i2c_client	*c;
	struct i2c_board_info	info = {
		.platform_data	= fe,
		.addr		= adr,
	};

	strlcpy(info.type, name, I2C_NAME_SIZE);
	request_module("%s", info.type);
	c = i2c_new_device(i2c, &info);
	if (!c)
		return NULL;
	if (c->dev.driver && try_module_get(c->dev.driver->owner))
		return c;
	ptx_unregister_subdev(c);
	return NULL;
}

void ptx_unregister_fe(struct dvb_frontend *fe)
{
	if (!fe)
		return;
	if (fe->frontend_priv)
		dvb_unregister_frontend(fe);
	ptx_unregister_subdev(fe->tuner_priv);
	ptx_unregister_subdev(fe->demodulator_priv);
	kfree(fe);
}

struct dvb_frontend *ptx_register_fe(struct i2c_adapter *i2c, struct dvb_adapter *dvb, const struct ptx_subdev_info *info)
{
	struct dvb_frontend *fe = kzalloc(sizeof(struct dvb_frontend), GFP_KERNEL);

	if (!fe)
		return	NULL;
	fe->demodulator_priv	= ptx_register_subdev(i2c, fe, info->demod_addr, info->demod_name);
	fe->tuner_priv		= ptx_register_subdev(i2c, fe, info->tuner_addr, info->tuner_name);
	if (info->type)
		fe->ops.delsys[0] = info->type;
	if (!fe->demodulator_priv || !fe->tuner_priv || (dvb && dvb_register_frontend(dvb, fe))) {
		ptx_unregister_fe(fe);
		return	NULL;
	}
	return fe;
}

void ptx_unregister_adap(struct ptx_card *card)
{
	int		i	= card->adapn - 1;
	struct ptx_adap	*adap	= card->adap + i;

	for (; i >= 0; i--, adap--) {
		ptx_unregister_fe(adap->fe);
		if (adap->demux.dmx.close)
			adap->demux.dmx.close(&adap->demux.dmx);
		if (adap->dmxdev.filter)
			dvb_dmxdev_release(&adap->dmxdev);
		if (adap->demux.cnt_storage)
			dvb_dmx_release(&adap->demux);
		if (adap->dvb.name)
			dvb_unregister_adapter(&adap->dvb);
	}
	i2c_del_adapter(&card->i2c);
	pci_release_regions(card->pdev);
	pci_set_drvdata(card->pdev, NULL);
	pci_disable_device(card->pdev);
	kfree(card);
}

DVB_DEFINE_MOD_OPT_ADAPTER_NR(adap_no);
int ptx_register_adap(struct ptx_card *card, const struct ptx_subdev_info *info,
			int (*thread)(void *), int (*dma)(struct ptx_adap *, bool))
{
	struct ptx_adap	*adap;
	u8	i;
	int	err;

	card->thread	= thread;
	card->dma	= dma;
	for (i = 0, adap = card->adap; i < card->adapn; i++, adap++) {
		struct dvb_adapter	*dvb	= &adap->dvb;
		struct dvb_demux	*demux	= &adap->demux;
		struct dmxdev		*dmxdev	= &adap->dmxdev;

		if (dvb_register_adapter(dvb, card->name, THIS_MODULE, &card->pdev->dev, adap_no) < 0)
			return -ENFILE;
		demux->dmx.capabilities = DMX_TS_FILTERING | DMX_SECTION_FILTERING;
		demux->feednum		= 1;
		demux->filternum	= 1;
		demux->start_feed	= ptx_start_feed;
		demux->stop_feed	= ptx_stop_feed;
		if (dvb_dmx_init(demux) < 0)
			return -ENOMEM;
		dmxdev->filternum	= 1;
		dmxdev->demux		= &demux->dmx;
		err			= dvb_dmxdev_init(dmxdev, dvb);
		if (err)
			return err;
		adap->fe		= ptx_register_fe(&adap->card->i2c, &adap->dvb, &info[i]);
		if (!adap->fe)
			return -ENOMEM;
		adap->fe_sleep		= adap->fe->ops.sleep;
		adap->fe_wakeup		= adap->fe->ops.init;
		adap->fe->ops.sleep	= ptx_sleep;
		adap->fe->ops.init	= ptx_wakeup;
		ptx_sleep(adap->fe);
	}
	return 0;
}

int ptx_abort(struct pci_dev *pdev, void remover(struct pci_dev *), int err, char *fmt, ...)
{
	va_list	ap;
	char	*s = NULL;
	int	slen;

	va_start(ap, fmt);
	slen	= vsnprintf(s, 0, fmt, ap) + 1;
	s	= kzalloc(slen, GFP_ATOMIC);
	if (s) {
		vsnprintf(s, slen, fmt, ap);
		dev_err(&pdev->dev, "%s", s);
		kfree(s);
	}
	va_end(ap);
	remover(pdev);
	return err;
}

u32 ptx_i2c_func(struct i2c_adapter *i2c)
{
	return I2C_FUNC_I2C | I2C_FUNC_NOSTART;
}


