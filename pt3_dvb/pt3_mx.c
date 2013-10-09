static struct {
	__u32	freq;		// Channel center frequency @ kHz
	__u32	freq_th;	// Offset frequency threshold @ kHz
	__u8	shf_val;	// Spur shift value
	__u8	shf_dir;	// Spur shift direction
} SHF_DVBT_TAB[] = {
	{  64500, 500, 0x92, 0x07 },
	{ 191500, 300, 0xE2, 0x07 },
	{ 205500, 500, 0x2C, 0x04 },
	{ 212500, 500, 0x1E, 0x04 },
	{ 226500, 500, 0xD4, 0x07 },
	{  99143, 500, 0x9C, 0x07 },
	{ 173143, 500, 0xD4, 0x07 },
	{ 191143, 300, 0xD4, 0x07 },
	{ 207143, 500, 0xCE, 0x07 },
	{ 225143, 500, 0xCE, 0x07 },
	{ 243143, 500, 0xD4, 0x07 },
	{ 261143, 500, 0xD4, 0x07 },
	{ 291143, 500, 0xD4, 0x07 },
	{ 339143, 500, 0x2C, 0x04 },
	{ 117143, 500, 0x7A, 0x07 },
	{ 135143, 300, 0x7A, 0x07 },
	{ 153143, 500, 0x01, 0x07 }
};

static void pt3_mx_rftune(__u8 *data, __u32 *size, __u32 freq)
{
	__u32 dig_rf_freq ,temp ,frac_divider, khz, mhz, i;
	__u8 rf_data[] = {
		0x13, 0x00,		// abort tune
		0x3B, 0xC0,
		0x3B, 0x80,
		0x10, 0x95,		// BW
		0x1A, 0x05,
		0x61, 0x00,
		0x62, 0xA0,
		0x11, 0x40,		// 2 bytes to store RF frequency
		0x12, 0x0E,		// 2 bytes to store RF frequency
		0x13, 0x01		// start tune
	};

	dig_rf_freq = 0;
	temp = 0;
	frac_divider = 1000000;
	khz = 1000;
	mhz = 1000000;

	dig_rf_freq = freq / mhz;
	temp = freq % mhz;

	for (i = 0; i < 6; i++) {
		dig_rf_freq <<= 1;
		frac_divider /= 2;
		if (temp > frac_divider) {
			temp -= frac_divider;
			dig_rf_freq++;
		}
	}
	if (temp > 7812)
		dig_rf_freq++;

	rf_data[2 * (7) + 1] = (__u8)(dig_rf_freq);
	rf_data[2 * (8) + 1] = (__u8)(dig_rf_freq >> 8);

	for (i = 0; i < sizeof(SHF_DVBT_TAB)/sizeof(*SHF_DVBT_TAB); i++) {
		if ( (freq >= (SHF_DVBT_TAB[i].freq - SHF_DVBT_TAB[i].freq_th) * khz) &&
				(freq <= (SHF_DVBT_TAB[i].freq + SHF_DVBT_TAB[i].freq_th) * khz) ) {
			rf_data[2 * (5) + 1] = SHF_DVBT_TAB[i].shf_val;
			rf_data[2 * (6) + 1] = 0xa0 | SHF_DVBT_TAB[i].shf_dir;
			break;
		}
	}
	memcpy(data, rf_data, sizeof(rf_data));
	*size = sizeof(rf_data);

	PT3_PRINTK(KERN_DEBUG, "mx_rftune freq=%d\n", freq);
}

static void pt3_mx_write(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 *data, size_t size)
{
	pt3_tc_write_tuner_without_addr(adap, bus, data, size);
}

static void pt3_mx_standby(PT3_ADAPTER *adap)
{
	__u8 data[4] = {0x01, 0x00, 0x13, 0x00};
	pt3_mx_write(adap, NULL, data, sizeof(data));
}

static void pt3_mx_set_register(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 addr, __u8 value)
{
	__u8 data[2] = {addr, value};
	pt3_mx_write(adap, bus, data, sizeof(data));
}

static void pt3_mx_idac_setting(PT3_ADAPTER *adap, PT3_BUS *bus)
{
	__u8 data[] = {
		0x0D, 0x00,
		0x0C, 0x67,
		0x6F, 0x89,
		0x70, 0x0C,
		0x6F, 0x8A,
		0x70, 0x0E,
		0x6F, 0x8B,
		0x70, 0x10+12,
	};
	pt3_mx_write(adap, bus, data, sizeof(data));
}

static void pt3_mx_tuner_rftune(PT3_ADAPTER *adap, PT3_BUS *bus, __u32 freq)
{
	__u8 data[100];
	__u32 size;

	size = 0;
	adap->freq = freq;
	pt3_mx_rftune(data, &size, freq);
	if (size != 20) {
		PT3_PRINTK(KERN_ALERT, "fail mx_rftune size = %d\n", size);
		return;
	}
	pt3_mx_write(adap, bus, data, 14);
	PT3_WAIT_MS_INT(1);
	pt3_mx_write(adap, bus, data + 14, 6);
	PT3_WAIT_MS_INT(1);
	PT3_WAIT_MS_INT(30);
	pt3_mx_set_register(adap, bus, 0x1a, 0x0d);
	pt3_mx_idac_setting(adap, bus);
}

static void pt3_mx_wakeup(PT3_ADAPTER *adap)
{
	__u8 data[2] = {0x01, 0x01};

	pt3_mx_write(adap, NULL, data, sizeof(data));
	pt3_mx_tuner_rftune(adap, NULL, adap->freq);
}

static void pt3_mx_set_sleep_mode(PT3_ADAPTER *adap, bool sleep)
{
	if (sleep)	pt3_mx_standby(adap);
	else		pt3_mx_wakeup(adap);
}

int pt3_mx_set_sleep(PT3_ADAPTER *adap, bool sleep)
{
	int ret;

	if (sleep) {
		if ((ret = pt3_tc_set_agc_t(adap, PT3_TC_AGC_MANUAL)))
			return ret;
		pt3_mx_set_sleep_mode(adap, sleep);
		pt3_tc_write_sleep_time(adap, sleep);
	} else {
		pt3_tc_write_sleep_time(adap, sleep);
		pt3_mx_set_sleep_mode(adap, sleep);
	}
	adap->sleep = sleep;
	return 0;
}

static __u8 PT3_MX_FREQ_TABLE[][3] = {
	{   2, 0,  3 },
	{  12, 1, 22 },
	{  21, 0, 12 },
	{  62, 1, 63 },
	{ 112, 0, 62 }
};

void pt3_mx_get_channel_frequency(PT3_ADAPTER *adap, __u32 channel, bool *catv, __u32 *number, __u32 *freq)
{
	__u32 i;
	__s32 freq_offset = 0;

	if (12 <= channel)	freq_offset += 2;
	if (17 <= channel)	freq_offset -= 2;
	if (63 <= channel)	freq_offset += 2;
	*freq = 93 + channel * 6 + freq_offset;

	for (i = 0; i < sizeof(PT3_MX_FREQ_TABLE) / sizeof(*PT3_MX_FREQ_TABLE); i++) {
		if (channel <= PT3_MX_FREQ_TABLE[i][0]) {
			*catv = PT3_MX_FREQ_TABLE[i][1] ? true : false;
			*number = channel + PT3_MX_FREQ_TABLE[i][2] - PT3_MX_FREQ_TABLE[i][0];
			break;
		}
	}
}

static __u32 REAL_TABLE[112] = {
	0x058d3f49,0x05e8ccc9,0x06445a49,0x069fe7c9,0x06fb7549,
	0x075702c9,0x07b29049,0x080e1dc9,0x0869ab49,0x08c538c9,
	0x0920c649,0x097c53c9,0x09f665c9,0x0a51f349,0x0aad80c9,
	0x0b090e49,0x0b649bc9,0x0ba1a4c9,0x0bfd3249,0x0c58bfc9,
	0x0cb44d49,0x0d0fdac9,0x0d6b6849,0x0dc6f5c9,0x0e228349,
	0x0e7e10c9,0x0ed99e49,0x0f352bc9,0x0f90b949,0x0fec46c9,
	0x1047d449,0x10a361c9,0x10feef49,0x115a7cc9,0x11b60a49,
	0x121197c9,0x126d2549,0x12c8b2c9,0x13244049,0x137fcdc9,
	0x13db5b49,0x1436e8c9,0x14927649,0x14ee03c9,0x15499149,
	0x15a51ec9,0x1600ac49,0x165c39c9,0x16b7c749,0x171354c9,
	0x176ee249,0x17ca6fc9,0x1825fd49,0x18818ac9,0x18dd1849,
	0x1938a5c9,0x19943349,0x19efc0c9,0x1a4b4e49,0x1aa6dbc9,
	0x1b026949,0x1b5df6c9,0x1bb98449,0x1c339649,0x1c8f23c9,
	0x1ceab149,0x1d463ec9,0x1da1cc49,0x1dfd59c9,0x1e58e749,
	0x1eb474c9,0x1f100249,0x1f6b8fc9,0x1fc71d49,0x2022aac9,
	0x207e3849,0x20d9c5c9,0x21355349,0x2190e0c9,0x21ec6e49,
	0x2247fbc9,0x22a38949,0x22ff16c9,0x235aa449,0x23b631c9,
	0x2411bf49,0x246d4cc9,0x24c8da49,0x252467c9,0x257ff549,
	0x25db82c9,0x26371049,0x26929dc9,0x26ee2b49,0x2749b8c9,
	0x27a54649,0x2800d3c9,0x285c6149,0x28b7eec9,0x29137c49,
	0x296f09c9,0x29ca9749,0x2a2624c9,0x2a81b249,0x2add3fc9,
	0x2b38cd49,0x2b945ac9,0x2befe849,0x2c4b75c9,0x2ca70349,
	0x2d0290c9,0x2d5e1e49,
};

static void pt3_mx_read(PT3_ADAPTER *adap, PT3_BUS *bus, __u8 addr, __u8 *data)
{
	__u8 write[2] = {0xfb, addr};

	pt3_tc_write_tuner_without_addr(adap, bus, write, sizeof(write));
	pt3_tc_read_tuner_without_addr(adap, bus, data);
}

static void pt3_mx_rfsynth_lock_status(PT3_ADAPTER *adap, PT3_BUS *bus, bool *locked)
{
	__u8 data;

	*locked = false;
	pt3_mx_read(adap, bus, 0x16, &data);
	data &= 0x0c;
	if (data == 0x0c)
		*locked = true;
}

static void pt3_mx_refsynth_lock_status(PT3_ADAPTER *adap, PT3_BUS *bus, bool *locked)
{
	__u8 data;

	*locked = false;
	pt3_mx_read(adap, bus, 0x16, &data);
	data &= 0x03;
	if (data == 0x03)
		*locked = true;
}

bool pt3_mx_locked(PT3_ADAPTER *adap)
{
	bool locked1 = false, locked2 = false;
	struct timeval begin, now;

	do_gettimeofday(&begin);
	while (1) {
		do_gettimeofday(&now);
		pt3_mx_rfsynth_lock_status(adap, NULL, &locked1);
		pt3_mx_refsynth_lock_status(adap, NULL, &locked2);
		if (locked1 && locked2)
			break;
		if (pt3_tc_time_diff(&begin, &now) > 1000)
			break;
		PT3_WAIT_MS_INT(1);
	}
#if 0
	PT3_PRINTK(KERN_DEBUG, "mx locked1=%d locked2=%d\n", locked1, locked2);
#endif
	return locked1 && locked2;
}

int pt3_mx_set_frequency(PT3_ADAPTER *adap, __u32 channel, __s32 offset)
{
	int ret;
	bool catv;
	__u32 number, freq, real_freq;

	if ((ret = pt3_tc_set_agc_t(adap, PT3_TC_AGC_MANUAL)))
		return ret;
	pt3_mx_get_channel_frequency(adap, channel, &catv, &number, &freq);
	//real_freq = (7 * freq + 1 + offset) * 1000000.0/7.0;
	real_freq = REAL_TABLE[channel];

	pt3_mx_tuner_rftune(adap, NULL, real_freq);

	return (!pt3_mx_locked(adap)) ? -ETIMEDOUT : pt3_tc_set_agc_t(adap, PT3_TC_AGC_AUTO);
}

