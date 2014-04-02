/*
 * DVB driver for Earthsoft PT3 ISDB-S/T PCI-E card
 *
 * Copyright (C) 2013 Budi Rachmanto, AreMa Inc. <info@are.ma>
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

#ifndef	__PT3_DMA_H__
#define	__PT3_DMA_H__

#include "pt3_common.h"

struct pt3_dma_page {
	dma_addr_t addr;
	u8 *data;
	u32 size, data_pos;
};

enum pt3_dma_mode {
	USE_LFSR = 1 << 16,
	REVERSE  = 1 << 17,
	RESET    = 1 << 18,
};

struct pt3_dma {
	struct pt3_adapter *adap;
	bool enabled;
	u32 ts_pos, ts_count, desc_count;
	struct pt3_dma_page *ts_info, *desc_info;
	struct mutex lock;
};

ssize_t pt3_dma_copy(struct pt3_dma *dma, struct dvb_demux *demux);
struct pt3_dma *pt3_dma_create(struct pt3_adapter *adap);
void pt3_dma_free(struct pt3_dma *dma);
u32 pt3_dma_get_status(struct pt3_dma *dma);
u32 pt3_dma_get_ts_error_packet_count(struct pt3_dma *dma);
void pt3_dma_set_enabled(struct pt3_dma *dma, bool enabled);
void pt3_dma_set_test_mode(struct pt3_dma *dma, enum pt3_dma_mode mode, u16 initval);

#endif
