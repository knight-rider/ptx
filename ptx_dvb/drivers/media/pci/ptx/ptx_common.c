/*
 * Common procedures for PT3 & PX-Q3PE DVB driver
 *
 * Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
 */

#include <linux/pci.h>
#include "dvb_frontend.h"
#include "ptx_common.h"

void ptx_lnb(struct ptx_card *card)
{
	int	i;
	bool	lnb = false;

	for (i = 0; i < card->adapn; i++)
		if (card->adap[i].fe.dtv_property_cache.delivery_system == SYS_ISDBS && card->adap[i].ON) {
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
	struct ptx_adap	*adap	= container_of(fe, struct ptx_adap, fe);

	adap->ON = false;
	ptx_lnb(adap->card);
	return adap->fe_sleep ? adap->fe_sleep(fe) : 0;
}

int ptx_wakeup(struct dvb_frontend *fe)
{
	struct ptx_adap	*adap	= container_of(fe, struct ptx_adap, fe);

	adap->ON = true;
	ptx_lnb(adap->card);
	return adap->fe_wakeup ? adap->fe_wakeup(fe) : 0;
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

struct i2c_client *ptx_register_subdev(struct i2c_adapter *i2c, void *dat, u16 adr, char *type)
{
	struct i2c_client	*c;
	struct i2c_board_info	info = {
		.platform_data	= dat,
		.addr		= adr,
	};

	strlcpy(info.type, type, I2C_NAME_SIZE);
	request_module("%s", info.type);
	c = i2c_new_device(i2c, &info);
	if (c) {
		if (c->dev.driver && try_module_get(c->dev.driver->owner))
			return c;
		i2c_unregister_device(c);
	}
	return NULL;
}

void ptx_unregister_adap_fe(struct ptx_card *card)
{
	int		i	= card->adapn - 1;
	struct ptx_adap	*adap	= card->adap + i;

	for (; i >= 0; i--, adap--) {
		if (adap->fe.frontend_priv)
			dvb_unregister_frontend(&adap->fe);
		if (adap->fe.ops.release)
			adap->fe.ops.release(&adap->fe);
		ptx_unregister_subdev(adap->tuner);
		ptx_unregister_subdev(adap->demod);
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
int ptx_register_adap_fe(struct ptx_card *card, const struct ptx_subdev_info *info,
			int (*start)(struct dvb_demux_feed *), int (*stop)(struct dvb_demux_feed *))
{
	struct ptx_adap	*adap;
	u8	i;
	int	err;

	for (i = 0, adap = card->adap; i < card->adapn; i++, adap++) {
		struct dvb_adapter	*dvb	= &adap->dvb;
		struct dvb_demux	*demux	= &adap->demux;
		struct dmxdev		*dmxdev	= &adap->dmxdev;
		struct dvb_frontend	*fe	= &adap->fe;

		if (dvb_register_adapter(dvb, card->name, THIS_MODULE, &card->pdev->dev, adap_no) < 0)
			return -ENFILE;
		demux->feednum		= 1;
		demux->filternum	= 1;
		demux->start_feed	= start;
		demux->stop_feed	= stop;
		if (dvb_dmx_init(demux) < 0)
			return -ENOMEM;
		dmxdev->filternum	= 1;
		dmxdev->demux		= &demux->dmx;
		err = dvb_dmxdev_init(dmxdev, dvb);
		if (err)
			return err;
		fe->dtv_property_cache.delivery_system	= info[i].type;
		fe->dvb	= &adap->dvb;
		adap->demod = ptx_register_subdev(&card->i2c, &adap->fe, info[i].demod_addr, info[i].demod_name);
		adap->tuner = ptx_register_subdev(&card->i2c, &adap->fe, info[i].tuner_addr, info[i].tuner_name);
		if (!adap->demod || !adap->tuner)
			return -ENFILE;
		adap->fe_sleep		= adap->fe.ops.sleep;
		adap->fe_wakeup		= adap->fe.ops.init;
		adap->fe.ops.sleep	= ptx_sleep;
		adap->fe.ops.init	= ptx_wakeup;
		adap->fe.dvb		= &adap->dvb;
		if (dvb_register_frontend(&adap->dvb, &adap->fe))
			return -EIO;
		ptx_sleep(&adap->fe);
		mutex_init(&adap->lock);
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
	s	= vzalloc(slen);
	if (s) {
		vsnprintf(s, slen, fmt, ap);
		dev_err(&pdev->dev, "%s", s);
		vfree(s);
	}
	va_end(ap);
	remover(pdev);
	return err;
}

u32 ptx_i2c_func(struct i2c_adapter *i2c)
{
	return I2C_FUNC_I2C | I2C_FUNC_NOSTART;
}


