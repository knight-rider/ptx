/*
 * Earthsoft PT3 demodulator frontend Toshiba TC90522XBG OFDM(ISDB-T)/8PSK(ISDB-S)
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

#ifndef	__TC90522_H__
#define	__TC90522_H__

#ifndef pr_fmt
 #define pr_fmt(fmt) KBUILD_MODNAME " " fmt
#endif
#include "dvb_frontend.h"

#if IS_ENABLED(CONFIG_DVB_TC90522)
extern struct dvb_frontend *tc90522_attach(struct i2c_adapter *i2c, u8 idx, fe_delivery_system_t type, u8 addr_demod, bool pwr);
#else
static inline struct dvb_frontend *tc90522_attach(struct i2c_adapter *i2c, u8 idx, fe_delivery_system_t type, u8 addr_demod, bool pwr)
{
	pr_warn("%s: driver disabled by Kconfig\n", __func__);
	return NULL;
}
#endif

#endif

