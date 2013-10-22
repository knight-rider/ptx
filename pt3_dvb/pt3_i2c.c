#define PT3_I2C_DATA_OFFSET 2048

bool pt3_i2c_is_clean(PT3_I2C *i2c)
{
	return PT3_SHIFT_MASK(readl(i2c->reg[0] + REG_I2C_R), 3, 1);
}

void pt3_i2c_reset(PT3_I2C *i2c)
{
	writel(1 << 17, i2c->reg[0] + REG_I2C_W);	// 0x00020000
}

static void pt3_i2c_wait(PT3_I2C *i2c, __u32 *data)
{
	__u32 val;

	while (1) {
		val = readl(i2c->reg[0] + REG_I2C_R);
		if (!PT3_SHIFT_MASK(val, 0, 1))	break;
		PT3_WAIT_MS_INT(1);
	}
	if (data) *data = val;
}

void pt3_i2c_copy(PT3_I2C *i2c, PT3_BUS *bus)
{
	__u32 i;
	__u8 *src = &bus->cmds[0];
	void __iomem *dst = i2c->reg[1] + PT3_I2C_DATA_OFFSET + (bus->cmd_addr / 2);

	for (i = 0; i < bus->cmd_pos; i++)
		writeb(src[i], dst + i);
}

int pt3_i2c_run(PT3_I2C *i2c, PT3_BUS *bus, bool copy)
{
	int ret = 0;
	__u32 data, a, i, start_addr = bus->cmd_addr;

	mutex_lock(&i2c->lock);
	if (copy)
		pt3_i2c_copy(i2c, bus);

	pt3_i2c_wait(i2c, &data);
	if (unlikely(start_addr >= (1 << 13)))
		PT3_PRINTK(KERN_DEBUG, "start address is over.\n");
	writel(1 << 16 | start_addr, i2c->reg[0] + REG_I2C_W);	// 0x00010000
	pt3_i2c_wait(i2c, &data);

	if ((a = PT3_SHIFT_MASK(data, 1, 2))) {
		PT3_PRINTK(KERN_DEBUG, "fail i2c run_code ret=0x%x\n", data);
		ret = -EIO;
	}

	for (i = 0; i < bus->read_addr; i++)
		pt3_bus_push_read_data(bus, readb(i2c->reg[1] + PT3_I2C_DATA_OFFSET + i));
	mutex_unlock(&i2c->lock);
	return ret;
}

