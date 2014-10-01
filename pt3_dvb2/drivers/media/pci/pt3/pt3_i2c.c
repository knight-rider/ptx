/*
 * I2C handler for Earthsoft PT3 ISDB-S/T PCI-E card DVB driver
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

#include "pt3_i2c.h"

#define PT3_I2C_DATA_OFFSET	2048
#define PT3_I2C_START_ADDR	0x17fa

enum pt3_i2c_cmd {
	I_END,
	I_ADDRESS,
	I_CLOCK_L,
	I_CLOCK_H,
	I_DATA_L,
	I_DATA_H,
	I_RESET,
	I_SLEEP,
	I_DATA_L_NOP  = 0x08,
	I_DATA_H_NOP  = 0x0c,
	I_DATA_H_READ = 0x0d,
	I_DATA_H_ACK0 = 0x0e,
	I_DATA_H_ACK1 = 0x0f,
};

bool pt3_i2c_is_clean(struct pt3_board *pt3)
{
	return (readl(pt3->bar_reg + PT3_REG_I2C_R) >> 3) & 1;
}

void pt3_i2c_reset(struct pt3_board *pt3)
{
	writel(1 << 17, pt3->bar_reg + PT3_REG_I2C_W);			/* 0x00020000 */
}

void pt3_i2c_wait(struct pt3_board *pt3, u32 *status)
{
	u32 val;

	while (1) {
		val = readl(pt3->bar_reg + PT3_REG_I2C_R);
		if (!(val & 1))						/* sequence stopped */
			break;
		msleep_interruptible(1);
	}
	if (status)
		*status = val;						/* I2C register status */
}

void pt3_i2c_mem_write(struct pt3_board *pt3, u8 data)
{
	void __iomem *dst = pt3->bar_mem + PT3_I2C_DATA_OFFSET + pt3->i2c_addr;

	if (pt3->i2c_filled) {
		pt3->i2c_buf |= data << 4;
		writeb(pt3->i2c_buf, dst);
		pt3->i2c_addr++;
	} else
		pt3->i2c_buf = data;
	pt3->i2c_filled ^= true;
}

void pt3_i2c_start(struct pt3_board *pt3)
{
	pt3_i2c_mem_write(pt3, I_DATA_H);
	pt3_i2c_mem_write(pt3, I_CLOCK_H);
	pt3_i2c_mem_write(pt3, I_DATA_L);
	pt3_i2c_mem_write(pt3, I_CLOCK_L);
}

void pt3_i2c_cmd_write(struct pt3_board *pt3, const u8 *data, u32 size)
{
	u32 i, j;
	u8 byte;

	for (i = 0; i < size; i++) {
		byte = data[i];
		for (j = 0; j < 8; j++)
			pt3_i2c_mem_write(pt3, (byte >> (7 - j)) & 1 ? I_DATA_H_NOP : I_DATA_L_NOP);
		pt3_i2c_mem_write(pt3, I_DATA_H_ACK0);
	}
}

void pt3_i2c_cmd_read(struct pt3_board *pt3, u8 *data, u32 size)
{
	u32 i, j;

	for (i = 0; i < size; i++) {
		for (j = 0; j < 8; j++)
			pt3_i2c_mem_write(pt3, I_DATA_H_READ);
		if (i == (size - 1))
			pt3_i2c_mem_write(pt3, I_DATA_H_NOP);
		else
			pt3_i2c_mem_write(pt3, I_DATA_L_NOP);
	}
}

void pt3_i2c_stop(struct pt3_board *pt3)
{
	pt3_i2c_mem_write(pt3, I_DATA_L);
	pt3_i2c_mem_write(pt3, I_CLOCK_H);
	pt3_i2c_mem_write(pt3, I_DATA_H);
}

int pt3_i2c_flush(struct pt3_board *pt3, bool end, u32 start_addr)
{
	u32 status;

	if (end) {
		pt3_i2c_mem_write(pt3, I_END);
		if (pt3->i2c_filled)
			pt3_i2c_mem_write(pt3, I_END);
	}
	pt3_i2c_wait(pt3, &status);
	writel(1 << 16 | start_addr, pt3->bar_reg + PT3_REG_I2C_W);	/* 0x00010000 start sequence */
	pt3_i2c_wait(pt3, &status);
	if (status & 0b0110) {						/* ACK status */
		dev_err(&pt3->i2c.dev, "%s %s failed, status=0x%x\n", pt3->i2c.name, __func__, status);
		return -EIO;
	}
	return 0;
}

u32 pt3_i2c_func(struct i2c_adapter *i2c)
{
	return I2C_FUNC_I2C;
}

int pt3_i2c_xfer(struct i2c_adapter *i2c, struct i2c_msg *msg, int num)
{
	struct pt3_board *pt3 = i2c_get_adapdata(i2c);
	int i, j;

	if (!num)
		return pt3_i2c_flush(pt3, false, PT3_I2C_START_ADDR);
	if ((num < 1) || (num > 3) || !msg || msg[0].flags)		/* always write first */
		return -ENOTSUPP;
	mutex_lock(&pt3->lock);
	pt3->i2c_addr = 0;
	for (i = 0; i < num; i++) {
		u8 byte = (msg[i].addr << 1) | (msg[i].flags & 1);

		pt3_i2c_start(pt3);
		pt3_i2c_cmd_write(pt3, &byte, 1);
		if (msg[i].flags == I2C_M_RD)
			pt3_i2c_cmd_read(pt3, msg[i].buf, msg[i].len);
		else
			pt3_i2c_cmd_write(pt3, msg[i].buf, msg[i].len);
	}
	pt3_i2c_stop(pt3);
	if (pt3_i2c_flush(pt3, true, 0))
		num = -EIO;
	else
		for (i = 1; i < num; i++)
			if (msg[i].flags == I2C_M_RD)
				for (j = 0; j < msg[i].len; j++)
					msg[i].buf[j] = readb(pt3->bar_mem + PT3_I2C_DATA_OFFSET + j);
	mutex_unlock(&pt3->lock);
	return num;
}

static const struct i2c_algorithm pt3_i2c_algo = {
	.functionality = pt3_i2c_func,
	.master_xfer = pt3_i2c_xfer,
};

int pt3_i2c_add_adapter(struct pt3_board *pt3)
{
	struct i2c_adapter *i2c = &pt3->i2c;

	i2c->algo = &pt3_i2c_algo;
	i2c->algo_data = NULL;
	i2c->dev.parent = &pt3->pdev->dev;
	strcpy(i2c->name, PT3_DRVNAME);
	i2c_set_adapdata(i2c, pt3);
	return	i2c_add_adapter(i2c) ||
		(!pt3_i2c_is_clean(pt3) && pt3_i2c_flush(pt3, false, 0));
}

