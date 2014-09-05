/*
 * Sharp VA4M6JC2103 - Earthsoft PT3 ISDB-S tuner driver QM1D1C0042
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

#ifndef __QM1D1C0042_H__
#define __QM1D1C0042_H__

#include "dvb_frontend.h"

#if IS_ENABLED(CONFIG_MEDIA_TUNER_QM1D1C0042)
extern int qm1d1c0042_attach(struct dvb_frontend *fe, u8 addr_tuner);
#else
static inline int qm1d1c0042_attach(struct dvb_frontend *fe, u8 addr_tuner)
{
	dev_warn(fe->dvb->device, "%s: driver disabled by Kconfig\n", __func__);
	return 0;
}
#endif

#endif

