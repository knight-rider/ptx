/*
 * Defs & procs for PT3 & PX-Q3PE DVB driver
 *
 * Copyright (C) Budi Rachmanto, AreMa Inc. <info@are.ma>
 *
 * This program is distributed in hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef	PTX_COMMON_H
#define PTX_COMMON_H

#include <linux/freezer.h>
#include <linux/kthread.h>
#include <linux/pci.h>
#include "dvb_demux.h"
#include "dvb_frontend.h"
#include "dmxdev.h"

enum ePTX {
	PTX_TS_SIZE	= 188,
	PTX_TS_SYNC	= 0x47,
	PTX_TS_NOT_SYNC	= 0x74,
};

struct ptx_subdev_info {
	enum fe_delivery_system	type;
	u8	demod_addr,	*demod_name,
		tuner_addr,	*tuner_name;
};

struct ptx_card {
	struct ptx_adap		*adap;
	struct mutex		lock;
	struct i2c_adapter	i2c;
	struct pci_dev		*pdev;
	u8	*name,
		adapn;
	bool	lnbON;
	void	*priv,
		(*lnb)(struct ptx_card *card, bool lnb);
	int	(*thread)(void *dat),
		(*dma)(struct ptx_adap *adap, bool ON);
};

struct ptx_adap {
	struct ptx_card		*card;
	bool			ON;
	struct dvb_adapter	dvb;
	struct dvb_demux	demux;
	struct dmxdev		dmxdev;
	struct dvb_frontend	*fe;
	struct task_struct	*kthread;
	void			*priv;
	int	(*fe_sleep)(struct dvb_frontend *),
		(*fe_wakeup)(struct dvb_frontend *);
};

struct ptx_card *ptx_alloc(struct pci_dev *pdev, u8 *name, u8 adapn, u32 sz_card_priv, u32 sz_adap_priv,
			void (*lnb)(struct ptx_card *, bool));
int ptx_sleep(struct dvb_frontend *fe);
int ptx_wakeup(struct dvb_frontend *fe);
int ptx_i2c_add_adapter(struct ptx_card *card, const struct i2c_algorithm *algo);
void ptx_unregister_fe(struct dvb_frontend *fe);
struct dvb_frontend *ptx_register_fe(struct i2c_adapter *i2c, struct dvb_adapter *dvb, const struct ptx_subdev_info *info);
void ptx_unregister_adap(struct ptx_card *card);
int ptx_register_adap(struct ptx_card *card, const struct ptx_subdev_info *info,
			int (*thread)(void *), int (*dma)(struct ptx_adap *, bool));
int ptx_abort(struct pci_dev *pdev, void remover(struct pci_dev *), int err, char *fmt, ...);
u32 ptx_i2c_func(struct i2c_adapter *i2c);

#endif
