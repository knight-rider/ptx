#include "pt3.h"

#define PT3_I2C_DATA_OFFSET 2048

bool pt3_i2c_is_clean(struct pt3_i2c *i2c)
{
	return PT3_SHIFT_MASK(readl(i2c->reg[0] + REG_I2C_R), 3, 1);
}

void pt3_i2c_reset(struct pt3_i2c *i2c)
{
	writel(1 << 17, i2c->reg[0] + REG_I2C_W);	/* 0x00020000 */
}

static void pt3_i2c_wait(struct pt3_i2c *i2c, u32 *data)
{
	u32 val;

	while (1) {
		val = readl(i2c->reg[0] + REG_I2C_R);
		if (!PT3_SHIFT_MASK(val, 0, 1))
			break;
		msleep_interruptible(1);
	}
	if (data)
		*data = val;
}

void pt3_i2c_copy(struct pt3_i2c *i2c, struct pt3_bus *bus)
{
	u32 i;
	u8 *src = &bus->cmds[0];
	void __iomem *dst = i2c->reg[1] + PT3_I2C_DATA_OFFSET + (bus->cmd_addr / 2);

	for (i = 0; i < bus->cmd_pos; i++)
		writeb(src[i], dst + i);
}

int pt3_i2c_run(struct pt3_i2c *i2c, struct pt3_bus *bus, bool copy)
{
	int ret = 0;
	u32 data, a, i, start_addr = bus->cmd_addr;

	mutex_lock(&i2c->lock);
	if (copy)
		pt3_i2c_copy(i2c, bus);

	pt3_i2c_wait(i2c, &data);
	if (unlikely(start_addr >= (1 << 13)))
		pr_debug("start address is over.\n");
	writel(1 << 16 | start_addr, i2c->reg[0] + REG_I2C_W);
	pt3_i2c_wait(i2c, &data);

	a = PT3_SHIFT_MASK(data, 1, 2);
	if (a) {
		pr_debug("fail i2c run_code ret=0x%x\n", data);
		ret = -EIO;
	}
	for (i = 0; i < bus->read_addr; i++)
		pt3_bus_push_read_data(bus, readb(i2c->reg[1] + PT3_I2C_DATA_OFFSET + i));
	mutex_unlock(&i2c->lock);
	return ret;
}

