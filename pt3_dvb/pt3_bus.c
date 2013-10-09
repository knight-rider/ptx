#define PT3_BUS_CMD_MAX   4096
#define PT3_BUS_CMD_ADDR0 4096 + 0
#define PT3_BUS_CMD_ADDR1 4096 + 2042

typedef struct {
	__u32 read_addr, cmd_addr, cmd_count, cmd_pos, buf_pos, buf_size;
	__u8 cmd_tmp, cmds[PT3_BUS_CMD_MAX], *buf;
} PT3_BUS;

typedef enum {
	I_END,
	I_ADDRESS,
	I_CLOCK_L,
	I_CLOCK_H,
	I_DATA_L,
	I_DATA_H,
	I_RESET,
	I_SLEEP,	// Sleep 1ms
	I_DATA_L_NOP  = 0x08,
	I_DATA_H_NOP  = 0x0c,
	I_DATA_H_READ = 0x0d,
	I_DATA_H_ACK0 = 0x0e,
	I_DATA_H_ACK1 = 0x0f,
} PT3_BUS_CMD;

static void pt3_bus_add_cmd(PT3_BUS *bus, PT3_BUS_CMD cmd)
{
	if ((bus->cmd_count % 2) == 0) {
		bus->cmd_tmp = cmd;
	} else {
		bus->cmd_tmp |= cmd << 4;
	}

	if (bus->cmd_count % 2) {
		bus->cmds[bus->cmd_pos] = bus->cmd_tmp;
		bus->cmd_pos++;
		if (bus->cmd_pos >= sizeof(bus->cmds)) {
			PT3_PRINTK(KERN_ALERT, "bus->cmds is overflow\n");
			bus->cmd_pos = 0;
		}
	}
	bus->cmd_count++;
}

__u8 pt3_bus_data1(PT3_BUS *bus, __u32 index)
{
	if (unlikely(!bus->buf)) {
		PT3_PRINTK(KERN_ALERT, "buf is not ready.\n");
		return 0;
	}
	if (unlikely(bus->buf_size < index + 1)) {
		PT3_PRINTK(KERN_ALERT, "buf does not have enough size. buf_size=%d\n",
				bus->buf_size);
		return 0;
	}

	return bus->buf[index];
}

void pt3_bus_start(PT3_BUS *bus)
{
	pt3_bus_add_cmd(bus, I_DATA_H);
	pt3_bus_add_cmd(bus, I_CLOCK_H);
	pt3_bus_add_cmd(bus, I_DATA_L);
	pt3_bus_add_cmd(bus, I_CLOCK_L);
}

void pt3_bus_stop(PT3_BUS *bus)
{
	pt3_bus_add_cmd(bus, I_DATA_L);
	pt3_bus_add_cmd(bus, I_CLOCK_H);
	pt3_bus_add_cmd(bus, I_DATA_H);
}

void pt3_bus_write(PT3_BUS *bus, const __u8 *data, __u32 size)
{
	__u32 i, j;
	__u8 byte;

	for (i = 0; i < size; i++) {
		byte = data[i];
		for (j = 0; j < 8; j++) {
			pt3_bus_add_cmd(bus, PT3_SHIFT_MASK(byte, 7 - j, 1) ? I_DATA_H_NOP : I_DATA_L_NOP);
		}
		pt3_bus_add_cmd(bus, I_DATA_H_ACK0);
	}
}

__u32 pt3_bus_read(PT3_BUS *bus, __u8 *data, __u32 size)
{
	__u32 i, j;
	__u32 index;

	for (i = 0; i < size; i++) {
		for (j = 0; j < 8; j++) {
			pt3_bus_add_cmd(bus, I_DATA_H_READ);
		}

		if (i == (size - 1))
			pt3_bus_add_cmd(bus, I_DATA_H_NOP);
		else
			pt3_bus_add_cmd(bus, I_DATA_L_NOP);
	}
	index = bus->read_addr;
	bus->read_addr += size;
	if (likely(bus->buf == NULL)) {
		bus->buf = data;
		bus->buf_pos = 0;
		bus->buf_size = size;
	} else
		PT3_PRINTK(KERN_ALERT, "bus read buf already exists.\n");

	return index;
}

void pt3_bus_push_read_data(PT3_BUS *bus, __u8 data)
{
	if (unlikely(bus->buf)) {
		if (bus->buf_pos >= bus->buf_size) {
			PT3_PRINTK(KERN_ALERT, "buffer over run. pos=%d\n", bus->buf_pos);
			bus->buf_pos = 0;
		}
		bus->buf[bus->buf_pos] = data;
		bus->buf_pos++;
	}
#if 0
	PT3_PRINTK(KERN_DEBUG, "bus read data=0x%02x\n", data);
#endif
}

void pt3_bus_sleep(PT3_BUS *bus, __u32 ms)
{
	__u32 i;
	for (i = 0; i< ms; i++)
		pt3_bus_add_cmd(bus, I_SLEEP);
}

void pt3_bus_end(PT3_BUS *bus)
{
	pt3_bus_add_cmd(bus, I_END);

	if (bus->cmd_count % 2)
		pt3_bus_add_cmd(bus, I_END);
}

void pt3_bus_reset(PT3_BUS *bus)
{
	pt3_bus_add_cmd(bus, I_RESET);
}

