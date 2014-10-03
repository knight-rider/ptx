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

#define TC90522_DRVNAME "tc90522"

struct tc90522_config {
	fe_delivery_system_t	type;	/* IN	SYS_ISDBS or SYS_ISDBT */
	bool			pwr;	/* IN	set only once after all demods initialized */
	struct dvb_frontend	*fe;	/* OUT	allocated frontend */
};

#endif

