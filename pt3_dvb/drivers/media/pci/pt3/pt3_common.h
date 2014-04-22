/*
 * DVB driver for Earthsoft PT3 ISDB-S/T PCI-E card
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

#ifndef	__PT3_COMMON_H__
#define	__PT3_COMMON_H__

#define pr_fmt(fmt) KBUILD_MODNAME " " fmt

#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_frontend.h"

#define DRV_NAME "pt3_dvb"

/* register idx */
#define PT3_REG_VERSION	0x00	/*	R	Version		*/
#define PT3_REG_BUS	0x04	/*	R	Bus		*/
#define PT3_REG_SYS_W	0x08	/*	W	System		*/
#define PT3_REG_SYS_R	0x0c	/*	R	System		*/
#define PT3_REG_I2C_W	0x10	/*	W	I2C		*/
#define PT3_REG_I2C_R	0x14	/*	R	I2C		*/
#define PT3_REG_RAM_W	0x18	/*	W	RAM		*/
#define PT3_REG_RAM_R	0x1c	/*	R	RAM		*/
#define PT3_REG_BASE	0x40	/* + 0x18*idx			*/
#define PT3_REG_DMA_D_L	0x00	/*	W	DMA descriptor	*/
#define PT3_REG_DMA_D_H	0x04	/*	W	DMA descriptor	*/
#define PT3_REG_DMA_CTL	0x08	/*	W	DMA		*/
#define PT3_REG_TS_CTL	0x0c	/*	W	TS		*/
#define PT3_REG_STATUS	0x10	/*	R	DMA/FIFO/TS	*/
#define PT3_REG_TS_ERR	0x14	/*	R	TS		*/

struct pt3_adapter;

struct pt3_board {
	struct mutex lock;
	int lnb;
	bool reset;

	struct pci_dev *pdev;
	int bars;
	void __iomem *bar_reg, *bar_mem;
	struct i2c_adapter i2c;
	u8 i2c_buf;
	u32 i2c_addr;
	bool i2c_filled;

	struct pt3_adapter **adap;
};

struct pt3_adapter {
	struct mutex lock;
	struct pt3_board *pt3;

	u8 idx;
	bool sleep;
	struct pt3_dma *dma;
	struct task_struct *kthread;
	struct dvb_adapter dvb;
	struct dvb_demux demux;
	struct dmxdev dmxdev;
	int users, voltage;

	struct dvb_frontend *fe;
	int (*orig_voltage)(struct dvb_frontend *fe, fe_sec_voltage_t voltage);
	int (*orig_sleep)(struct dvb_frontend *fe);
	int (*orig_init)(struct dvb_frontend *fe);
};

#endif

