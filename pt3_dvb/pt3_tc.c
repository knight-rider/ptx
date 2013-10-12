int pt3_tc_write(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 addr, const __u8 *data, __u32 size)
{
	int ret = 0;
	__u8 buf;
	PT3_BUS *p = bus ? bus : vzalloc(sizeof(PT3_BUS));

	if (!p) {
		PT3_PRINTK(KERN_ALERT, "out of memory.\n");
		return -ENOMEM;
	}

	pt3_bus_start(p);
	buf = adap->addr_tc << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, &addr, 1);
	pt3_bus_write(p, data, size);
	pt3_bus_stop(p);

	if (!bus) {
		pt3_bus_end(p);
		ret = pt3_i2c_run(adap->pt3->i2c, p, true);
		vfree(p);
	}
	return ret;
}

static int pt3_tc_write_pskmsrst(PT3_ADAPTER *adap)
{
	__u8 buf = 0x01;
	return pt3_tc_write(adap, NULL, 0x03, &buf, 1);
}

static int pt3_tc_write_imsrst(PT3_ADAPTER *adap)
{
	__u8 buf = 0x01 << 6;
	return pt3_tc_write(adap, NULL, 0x01, &buf, 1);
}

int pt3_tc_init(PT3_ADAPTER *adap)
{
	__u8 buf = 0x10;

	PT3_PRINTK(KERN_INFO, "#%d %s tuner=0x%x tc=0x%x\n", adap->idx, adap->str, adap->addr_tuner, adap->addr_tc);
	if (adap->type == SYS_ISDBS) {
		int ret = pt3_tc_write_pskmsrst(adap);
		return ret ? ret : pt3_tc_write(adap, NULL, 0x1e, &buf, 1);
	} else {
		int ret = pt3_tc_write_imsrst(adap);
		return ret ? ret : pt3_tc_write(adap, NULL, 0x1c, &buf, 1);
	}
}

int pt3_tc_set_powers(PT3_ADAPTER *adap, PT3_BUS *bus, bool tuner, bool amp)
{
	__u8	tuner_power = tuner ? 0x03 : 0x02,
		amp_power = amp ? 0x03 : 0x02,
		data = (tuner_power << 6) | (0x01 << 4) | (amp_power << 2) | 0x01 << 0;
	PT3_PRINTK(KERN_DEBUG, "#%d tuner %s amp %s\n", adap->idx, tuner ? "ON" : "OFF", amp ? "ON" : "OFF");
	return pt3_tc_write(adap, bus, 0x1e, &data, 1);
}

int pt3_tc_set_ts_pins_mode(PT3_ADAPTER *adap, PT3_TS_PINS_MODE *mode)
{
	__u32	clock_data = mode->clock_data,
		byte = mode->byte,
		valid = mode->valid;

	if (clock_data)	clock_data++;
	if (byte)	byte++;
	if (valid)	valid++;
	if (adap->type == SYS_ISDBS) {
		__u8 data[2];
		int ret;
		data[0] = 0x15 | (valid << 6);
		data[1] = 0x04 | (clock_data << 4) | byte;

		if ((ret = pt3_tc_write(adap, NULL, 0x1c, &data[0], 1)))	return ret;
		return pt3_tc_write(adap, NULL, 0x1f, &data[1], 1);
	} else {
		__u8 data = (__u8)(0x01 | (clock_data << 6) | (byte << 4) | (valid << 2)) ;
		return pt3_tc_write(adap, NULL, 0x1d, &data, 1);
	}
}

#define PT3_TC_THROUGH 0xfe
int pt3_tc_write_tuner(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 addr, const __u8 *data, __u32 size)
{
	int ret = 0;
	__u8 buf;
	PT3_BUS *p = bus ? bus : vzalloc(sizeof(PT3_BUS));

	if (!p) {
		PT3_PRINTK(KERN_ALERT, "out of memory.\n");
		return -ENOMEM;
	}

	pt3_bus_start(p);
	buf = adap->addr_tc << 1;
	pt3_bus_write(p, &buf, 1);
	buf = PT3_TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = adap->addr_tuner << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, &addr, 1);
	pt3_bus_write(p, data, size);
	pt3_bus_stop(p);

	if (!bus) {
		pt3_bus_end(p);
		ret = pt3_i2c_run(adap->pt3->i2c, p, true);
		vfree(p);
	}
	return ret;
}

int pt3_tc_read_tuner(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 addr, __u8 *data)
{
	int ret = 0;
	__u8 buf;
	size_t rindex;
	PT3_BUS *p;

	if (!(p = bus ? bus : vzalloc(sizeof(PT3_BUS)))) {
		PT3_PRINTK(KERN_ALERT, "out of memory.\n");
		return -ENOMEM;
	}

	pt3_bus_start(p);
	buf = adap->addr_tc << 1;
	pt3_bus_write(p, &buf, 1);
	buf = PT3_TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = adap->addr_tuner << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, &addr, 1);

	pt3_bus_start(p);
	buf = adap->addr_tc << 1;
	pt3_bus_write(p, &buf, 1);
	buf = PT3_TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = (adap->addr_tuner << 1) | 1;
	pt3_bus_write(p, &buf, 1);

	pt3_bus_start(p);
	buf = (adap->addr_tc << 1) | 1;
	pt3_bus_write(p, &buf, 1);
	rindex = pt3_bus_read(p, &buf, 1);
	pt3_bus_stop(p);

	if (!bus) {
		pt3_bus_end(p);
		ret = pt3_i2c_run(adap->pt3->i2c, p, true);
		data[0] = pt3_bus_data1(p, rindex);
		vfree(p);
	}
//#if 0
	PT3_PRINTK(KERN_DEBUG, "#%d read_tuner addr_tc=0x%x addr_tuner=0x%x\n",
		   adap->idx, adap->addr_tc, adap->addr_tuner);
//#endif
	return ret;
}

typedef enum {
	PT3_TC_AGC_AUTO,
	PT3_TC_AGC_MANUAL,
} PT3_TC_AGC;

static __u8 agc_data_s[2] = { 0xb0, 0x30 };

__u32 pt3_tc_index(PT3_ADAPTER *adap)
{
	return PT3_SHIFT_MASK(adap->addr_tc, 1, 1);
}

// ISDB_S
int pt3_tc_set_agc_s(PT3_ADAPTER *adap, PT3_TC_AGC agc)
{
	int ret;
	__u8 data = (agc == PT3_TC_AGC_AUTO) ? 0xff : 0x00;
	if ((ret = pt3_tc_write(adap, NULL, 0x0a, &data, 1)))	return ret;

	data = agc_data_s[pt3_tc_index(adap)];
	data |= (agc == PT3_TC_AGC_AUTO) ? 0x01 : 0x00;
	if ((ret = pt3_tc_write(adap, NULL, 0x10, &data, 1)))	return ret;

	data = (agc == PT3_TC_AGC_AUTO) ? 0x40 : 0x00;
	if ((ret = pt3_tc_write(adap, NULL, 0x11, &data, 1)))	return ret;
	return pt3_tc_write_pskmsrst(adap);
}

int pt3_tc_set_sleep_s(PT3_ADAPTER *adap, PT3_BUS *bus, bool sleep)
{
	__u8 buf = sleep ? 1 : 0;
	return pt3_tc_write(adap, bus, 0x17, &buf, 1);
}

// ISDB_T
int pt3_tc_set_agc_t(PT3_ADAPTER *adap, PT3_TC_AGC agc)
{
	int ret;
	__u8 data = (agc == PT3_TC_AGC_AUTO) ? 0x40 : 0x00;

	if ((ret = pt3_tc_write(adap, NULL, 0x25, &data, 1)))	return ret;

	data = 0x4c;
	data |= (agc == PT3_TC_AGC_AUTO) ? 0x00 : 0x01;
	if ((ret = pt3_tc_write(adap, NULL, 0x23, &data, 1)))	return ret;
	return pt3_tc_write_imsrst(adap);
}

int pt3_tc_write_tuner_without_addr(PT3_ADAPTER *adap, PT3_BUS *bus, const __u8 *data, __u32 size)
{
	int ret = 0;
	__u8 buf;
	PT3_BUS *p = bus ? bus : vzalloc(sizeof(PT3_BUS));
	if (!p) {
		PT3_PRINTK(KERN_ALERT, "out of memory.\n");
		return -ENOMEM;
	}

	pt3_bus_start(p);
	buf = adap->addr_tc << 1;
	pt3_bus_write(p, &buf, 1);
	buf = PT3_TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = adap->addr_tuner << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, data, size);
	pt3_bus_stop(p);

	if (!bus) {
		pt3_bus_end(p);
		ret = pt3_i2c_run(adap->pt3->i2c, p, true);
		vfree(p);
	}
	return ret;
}

int pt3_tc_write_sleep_time(PT3_ADAPTER *adap, int sleep)
{
	__u8 data = (1 << 7) | ((sleep ? 1 : 0) << 4);
	return pt3_tc_write(adap, NULL, 0x03, &data, 1);
}

__u32 pt3_tc_time_diff(struct timeval *st, struct timeval *et)
{
	__u32 diff = ((et->tv_sec - st->tv_sec) * 1000000 + (et->tv_usec - st->tv_usec)) / 1000;
#if 0
	PT3_PRINTK(KERN_DEBUG, "time diff = %d\n", diff);
#endif
	return diff;
}

int pt3_tc_read_tuner_without_addr(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 *data)
{
	int ret = 0;
	__u8 buf;
	__u32 rindex;
	PT3_BUS *p = bus ? bus : vzalloc(sizeof(PT3_BUS));

	if (!p) {
		PT3_PRINTK(KERN_ALERT, "out of memory.\n");
		return -ENOMEM;
	}

	pt3_bus_start(p);
	buf = adap->addr_tc << 1;
	pt3_bus_write(p, &buf, 1);
	buf = PT3_TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = (adap->addr_tuner << 1) | 0x01;
	pt3_bus_write(p, &buf, 1);

	pt3_bus_start(p);
	buf = (adap->addr_tc << 1) | 0x01;
	pt3_bus_write(p, &buf, 1);
	rindex = pt3_bus_read(p, &buf, 1);
	pt3_bus_stop(p);

	if (!bus) {
		pt3_bus_end(p);
		ret = pt3_i2c_run(adap->pt3->i2c, p, true);
		data[0] = pt3_bus_data1(p, rindex);
		vfree(p);
	}
#if 0
	PT3_PRINTK(KERN_DEBUG, "read_tuner_without addr_tc=0x%x addr_tuner=0x%x\n", adap->addr_tc, adap->addr_tuner);
#endif
	return ret;
}

static int pt3_tc_read(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 addr, __u8 *data, __u32 size)
{
	int ret = 0;
	__u8 buf[size];
	__u32 i, rindex;
	PT3_BUS *p = bus ? bus : vzalloc(sizeof(PT3_BUS));
	if (!p) {
		PT3_PRINTK(KERN_ALERT, "out of memory.\n");
		return -ENOMEM;
	}

	pt3_bus_start(p);
	buf[0] = adap->addr_tc << 1;
	pt3_bus_write(p, &buf[0], 1);
	pt3_bus_write(p, &addr, 1);

	pt3_bus_start(p);
	buf[0] = adap->addr_tc << 1 | 1;
	pt3_bus_write(p, &buf[0], 1);
	rindex = pt3_bus_read(p, &buf[0], size);
	pt3_bus_stop(p);

	if (!bus) {
		pt3_bus_end(p);
		ret = pt3_i2c_run(adap->pt3->i2c, p, true);
		for (i = 0; i < size; i++)
			data[i] = pt3_bus_data1(p, rindex + i);
		vfree(p);
	}
	return ret;
}

static __u32 pt3_tc_byten(const __u8 *data, __u32 n)
{
	__u32 i, value = 0;

	for (i = 0; i < n; i++) {
		value <<= 8;
		value |= data[i];
	}
	return value;
}

int pt3_tc_read_cn_s(PT3_ADAPTER *adap, PT3_BUS *bus, __u32 *cn)
{
	__u8 data[2];
	int ret = pt3_tc_read(adap, bus, 0xbc, data, sizeof(data));
	if (!ret) *cn = pt3_tc_byten(data,2);
	return ret;
}

int pt3_tc_read_cndat_t(PT3_ADAPTER *adap, PT3_BUS *bus, __u32 *cn)
{
	__u8 data[3];
	int ret = pt3_tc_read(adap, bus, 0x8b, data, sizeof(data));
	if (!ret) *cn = pt3_tc_byten(data,3);
	return ret;
}

int pt3_tc_read_retryov_tmunvld_fulock(PT3_ADAPTER *adap, PT3_BUS *bus, int *retryov, int *tmunvld, int *fulock)
{
	__u8 data;
	int ret = pt3_tc_read(adap, bus, 0x80, &data, 1);
	if (!ret) {
		*retryov = PT3_SHIFT_MASK(data, 7, 1) ? 1 : 0;
		*tmunvld = PT3_SHIFT_MASK(data, 5, 1) ? 1 : 0;
		*fulock  = PT3_SHIFT_MASK(data, 3, 1) ? 1 : 0;
	}
	return ret;
}

int pt3_tc_read_tmcc_t(PT3_ADAPTER *adap, PT3_BUS *bus, TMCC_T *tmcc)
{
	int ret;
	__u8 data[8];
	__u32 interleave0h, interleave0l, segment1h, segment1l;

	if ((ret = pt3_tc_read(adap, bus, 0xb2+0, &data[0], 4)))	return ret;
	if ((ret = pt3_tc_read(adap, bus, 0xb2+4, &data[4], 4)))	return ret;

	tmcc->system    = PT3_SHIFT_MASK(data[0], 6, 2);
	tmcc->indicator = PT3_SHIFT_MASK(data[0], 2, 4);
	tmcc->emergency = PT3_SHIFT_MASK(data[0], 1, 1);
	tmcc->partial   = PT3_SHIFT_MASK(data[0], 0, 1);

	tmcc->mode[0] = PT3_SHIFT_MASK(data[1], 5, 3);
	tmcc->mode[1] = PT3_SHIFT_MASK(data[2], 0, 3);
	tmcc->mode[2] = PT3_SHIFT_MASK(data[4], 3, 3);

	tmcc->rate[0] = PT3_SHIFT_MASK(data[1], 2, 3);
	tmcc->rate[1] = PT3_SHIFT_MASK(data[3], 5, 3);
	tmcc->rate[2] = PT3_SHIFT_MASK(data[4], 0, 3);

	interleave0h = PT3_SHIFT_MASK(data[1], 0, 2);
	interleave0l = PT3_SHIFT_MASK(data[2], 7, 1);

	tmcc->interleave[0] = interleave0h << 1 | interleave0l << 0;
	tmcc->interleave[1] = PT3_SHIFT_MASK(data[3], 2, 3);
	tmcc->interleave[2] = PT3_SHIFT_MASK(data[5], 5, 3);

	segment1h = PT3_SHIFT_MASK(data[3], 0, 2);
	segment1l = PT3_SHIFT_MASK(data[4], 6, 2);

	tmcc->segment[0] = PT3_SHIFT_MASK(data[2], 3, 4);
	tmcc->segment[1] = segment1h << 2 | segment1l << 0;
	tmcc->segment[2] = PT3_SHIFT_MASK(data[5], 1, 4);

	return ret;
}

int pt3_tc_read_tmcc_s(PT3_ADAPTER *adap, PT3_BUS *bus, TMCC_S *tmcc)
{
	enum {
		BASE = 0xc5,
		SIZE = 0xe5 - BASE + 1
	};
	int ret;
	__u8 data[SIZE];
	__u32 i, byte_offset, bit_offset;

	if ((ret = pt3_tc_read(adap, bus, 0xc3, data, 1)))	return ret;
	if (PT3_SHIFT_MASK(data[0], 4, 1))			return -EBADMSG;
	if ((ret = pt3_tc_read(adap, bus, 0xce, data, 2)))	return ret;
	if (pt3_tc_byten(data,2) == 0)				return -EBADMSG;
	if ((ret = pt3_tc_read(adap, bus, 0xc3, data, 1)))	return ret;
	tmcc->emergency = PT3_SHIFT_MASK(data[0], 2, 1);
	tmcc->extflag   = PT3_SHIFT_MASK(data[0], 1, 1);

	if ((ret = pt3_tc_read(adap, bus, 0xc5, data, SIZE)))	return ret;
	tmcc->indicator = PT3_SHIFT_MASK(data[0xc5 - BASE], 3, 5);
	tmcc->uplink    = PT3_SHIFT_MASK(data[0xc7 - BASE], 0, 4);

	for (i = 0; i < 4; i++) {
		byte_offset = i / 2;
		bit_offset = (i % 2) ? 0 : 4;
		tmcc->mode[i] = PT3_SHIFT_MASK(data[0xc8 + byte_offset - BASE], bit_offset, 4);
		tmcc->slot[i] = PT3_SHIFT_MASK(data[0xca + i - BASE], 0, 6);
	}
	for (i = 0; i < 8; i++)
		tmcc->id[i] = pt3_tc_byten(data + 0xce + i * 2 - BASE, 2);
	return ret;
}

int pt3_tc_write_id_s(PT3_ADAPTER *adap, PT3_BUS *bus, __u16 id)
{
	__u8 data[2] = { id >> 8, (__u8)id };
	return pt3_tc_write(adap, bus, 0x8f, data, sizeof(data));
}

int pt3_tc_read_id_s(PT3_ADAPTER *adap, PT3_BUS *bus, __u16 *id)
{
	__u8 data[2];
	int ret = pt3_tc_read(adap, bus, 0xe6, data, sizeof(data));
	if (!ret) *id = pt3_tc_byten(data,2);
	return ret;
}

