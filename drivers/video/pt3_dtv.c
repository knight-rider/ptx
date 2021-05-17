/*******************************************************************************
   Earthsoft PT3 Linux driver

   https://github.com/m-tsudo/pt3 (20210516)

   This program is free software; you can redistribute it and/or modify it
   under the terms and conditions of the GNU General Public License,
   version 3, as published by the Free Software Foundation.

   This program is distributed in the hope it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
   more details.

   The full GNU General Public License is included in this distribution in
   the file called "COPYING".

 *******************************************************************************/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/mutex.h>
#include <linux/uaccess.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
 #include <asm/system.h>
#endif
#include <asm/io.h>
#include <asm/irq.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(4,12,0)
 #include <asm/uaccess.h>
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
typedef struct pm_message {
        int event;
} pm_message_t;
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,23)
 #include <linux/freezer.h>
#else
 #define set_freezable()
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,32)
 #include <linux/sched.h>
#endif
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,38)
 #include <linux/smp_lock.h>
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,8,0)
 #define __devinitdata
 #define __devinit
 #define __devexit
 #define __devexit_p
#endif

#include <linux/kthread.h>
#include <linux/dma-mapping.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/ioctl.h>

// pt3_com.h ///////////////////////////////////////////////////////

#define BIT_SHIFT_MASK(value, shift, mask) (((value) >> (shift)) & (((u64)1<<(mask))-1))

#define		MAX_TUNER			2		//チューナ数
#define		MAX_CHANNEL			4		// チャネル数
#define		FALSE		0
#define		TRUE		1

enum {
	PT3_ISDB_S,
	PT3_ISDB_T,
	PT3_ISDB_MAX,
};

typedef enum {
	PT3_TS_PIN_MODE_NORMAL,
	PT3_TS_PIN_MODE_LOW,
	PT3_TS_PIN_MODE_HIGH,
} PT3_TS_PIN_MODE;

typedef struct _TS_PINS_MODE {
	int clock_data;
	int byte;
	int valid;
} PT3_TS_PINS_MODE;

typedef struct _TS_PINS_LEVEL {
	int clock;
	int data;
	int byte;
	int valid;
} PT3_TS_PINS_LEVEL;

enum LAYER_INDEX {
	LAYER_INDEX_L = 0,
	LAYER_INDEX_H,

	LAYER_INDEX_A = 0,
	LAYER_INDEX_B,
	LAYER_INDEX_C
};

enum LAYER_COUNT {
	LAYER_COUNT_S = LAYER_INDEX_H + 1,
	LAYER_COUNT_T = LAYER_INDEX_C + 1,
};

typedef struct __TMCC_S {
	u32 indicator;
	u32 mode[4];
	u32 slot[4];
	u32 id[8];
	u32 emergency;
	u32 uplink;
	u32 extflag;
	u32 extdata[2];
} TMCC_S;

typedef struct _TMCC_T {
	u32 system;
	u32 indicator;
	u32 emergency;
	u32 partial;
	u32 mode[LAYER_COUNT_T];
	u32 rate[LAYER_COUNT_T];
	u32 interleave[LAYER_COUNT_T];
	u32 segment[LAYER_COUNT_T];
	u32 phase;
	u32 reserved;
} TMCC_T;


typedef enum {
	// エラーなし
	STATUS_OK,

	// 一般的なエラー
	STATUS_GENERAL_ERROR = (1)*0x100,
	STATUS_NOT_IMPLIMENTED,
	STATUS_INVALID_PARAM_ERROR,
	STATUS_OUT_OF_MEMORY_ERROR,
	STATUS_INTERNAL_ERROR,

	// バスクラスのエラー
	STATUS_WDAPI_LOAD_ERROR = (2)*256,	// wdapi1100.dll がロードできない
	STATUS_ALL_DEVICES_MUST_BE_DELETED_ERROR,

	// デバイスクラスのエラー
	STATUS_PCI_BUS_ERROR = (3)*0x100,
	STATUS_CONFIG_REVISION_ERROR,
	STATUS_FPGA_VERSION_ERROR,
	STATUS_PCI_BASE_ADDRESS_ERROR,
	STATUS_FLASH_MEMORY_ERROR,

	STATUS_DCM_LOCK_TIMEOUT_ERROR,
	STATUS_DCM_SHIFT_TIMEOUT_ERROR,

	STATUS_POWER_RESET_ERROR,
	STATUS_I2C_ERROR,
	STATUS_TUNER_IS_SLEEP_ERROR,

	STATUS_PLL_OUT_OF_RANGE_ERROR,
	STATUS_PLL_LOCK_TIMEOUT_ERROR,

	STATUS_VIRTUAL_ALLOC_ERROR,
	STATUS_DMA_ADDRESS_ERROR,
	STATUS_BUFFER_ALREADY_ALLOCATED_ERROR,

	STATUS_DEVICE_IS_ALREADY_OPEN_ERROR,
	STATUS_DEVICE_IS_NOT_OPEN_ERROR,

	STATUS_BUFFER_IS_IN_USE_ERROR,
	STATUS_BUFFER_IS_NOT_ALLOCATED_ERROR,

	STATUS_DEVICE_MUST_BE_CLOSED_ERROR,

	// WinDriver 関連のエラー
	STATUS_WD_DriverName_ERROR = (4)*0x100,

	STATUS_WD_Open_ERROR,
	STATUS_WD_Close_ERROR,

	STATUS_WD_Version_ERROR,
	STATUS_WD_License_ERROR,

	STATUS_WD_PciScanCards_ERROR,

	STATUS_WD_PciConfigDump_ERROR,

	STATUS_WD_PciGetCardInfo_ERROR,
	STATUS_WD_PciGetCardInfo_Bus_ERROR,
	STATUS_WD_PciGetCardInfo_Memory_ERROR,

	STATUS_WD_CardRegister_ERROR,
	STATUS_WD_CardUnregister_ERROR,

	STATUS_WD_CardCleanupSetup_ERROR,

	STATUS_WD_DMALock_ERROR,
	STATUS_WD_DMAUnlock_ERROR,

	STATUS_WD_DMASyncCpu_ERROR,
	STATUS_WD_DMASyncIo_ERROR,

	// ROM
	STATUS_ROM_ERROR = (5)*0x100,
	STATUS_ROM_TIMEOUT
} STATUS;

// pt3_ioctl.h ///////////////////////////////////////////////////////

typedef	struct	_frequency{
	int frequencyno;
	int slot;
} FREQUENCY;

#define		SET_CHANNEL	_IOW(0x8d, 0x01, FREQUENCY)
#define		START_REC	_IO(0x8d, 0x02)
#define		STOP_REC	_IO(0x8d, 0x03)
#define		GET_SIGNAL_STRENGTH	_IOR(0x8d, 0x04, int *)
#define		LNB_ENABLE	_IOW(0x8d, 0x05, int)
#define		LNB_DISABLE	_IO(0x8d, 0x06)
#define		GET_STATUS _IOR(0x8d, 0x07, int *)
#define		SET_TEST_MODE_ON _IO(0x8d, 0x08)
#define		SET_TEST_MODE_OFF _IO(0x8d, 0x09)
#define		GET_TS_ERROR_PACKET_COUNT _IOR(0x8d, 0x0a, unsigned int *)

// pt3_pci.h ///////////////////////////////////////////////////////

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
void * pt3_vzalloc(unsigned long size);
#else
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,2,0)
#include <linux/vmalloc.h>
#endif
#define pt3_vzalloc vzalloc
#endif

extern int debug;
#define PT3_PRINTK(verbose, level, fmt, args...)	{if(verbose <= debug)printk(level "PT3: " fmt, ##args);}

#define REGS_VERSION	0x00	/*	R		Version */
#define REGS_BUS		0x04	/*	R		Bus */
#define REGS_SYSTEM_W	0x08	/*	W		System */
#define REGS_SYSTEM_R	0x0c	/*	R		System */
#define REGS_I2C_W		0x10	/*	W		I2C */
#define REGS_I2C_R		0x14	/*	R		I2C */
#define REGS_RAM_W		0x18	/*	W		RAM */
#define REGS_RAM_R		0x1c	/*	R		RAM */
#define REGS_DMA_DESC_L	0x40	/* + 0x18*	W		DMA */
#define REGS_DMA_DESC_H	0x44	/* + 0x18*	W		DMA */
#define REGS_DMA_CTL	0x48	/* + 0x18*	W		DMA */
#define REGS_TS_CTL		0x4c	/* + 0x18*	W		TS */
#define REGS_STATUS		0x50	/* + 0x18*	R		DMA / FIFO / TS */
#define REGS_TS_ERR		0x54	/* + 0x18*	R		TS */

// pt3_bus.h ///////////////////////////////////////////////////////

#define PT3_BUS_MAX_INST 4096
#define PT3_BUS_INST_ADDR0 4096 + 0
#define PT3_BUS_INST_ADDR1 4096 + 2042

typedef struct _PT3_BUS {
	u32 inst_addr;
	u32 inst_count;
	u32 read_addr;
	u8 tmp_inst;
	u8 insts[PT3_BUS_MAX_INST];
	u32 inst_pos;
	u8 *buf;
	u32 buf_pos;
	u32 buf_size;
} PT3_BUS;

// pt3_i2c.h ///////////////////////////////////////////////////////

typedef struct _PT3_I2C {
	u8 __iomem* bar[2];
	struct mutex lock;
} PT3_I2C;

// pt3_tc.h ///////////////////////////////////////////////////////

#define TC_THROUGH 0xfe

typedef struct _PT3_TC {
	PT3_I2C *i2c;
	u8 tc_addr;
	u8 tuner_addr;
	u8 master_clock_freq;	// MHz
} PT3_TC;

typedef enum {
	PT3_TC_AGC_AUTO,
	PT3_TC_AGC_MANUAL,
} PT3_TC_AGC;

// pt3_qm.h ///////////////////////////////////////////////////////

typedef struct __PT3_QM_PARAM {
	u32 channel_freq;
	u32 crystal_freq;
	int fast_search_mode;
	int standby;
	u32 lpf_wait_time;
	u32 fast_search_wait_time;
	u32 normal_search_wait_time;
} PT3_QM_PARAM;

typedef struct __PT3_QM {
	PT3_I2C *i2c;
	PT3_TC *tc;
	PT3_QM_PARAM param;
	u8 reg[0x20];
	int sleep;
	u32 channel;
	__s32 offset;
} PT3_QM;

// pt3_mx.h ///////////////////////////////////////////////////////

typedef struct _PT3_MX {
	PT3_I2C *i2c;
	PT3_TC *tc;
	u32 freq;
	int sleep;
	u32 channel;
	__s32 offset;
} PT3_MX;

// pt3_dma.h ///////////////////////////////////////////////////////

typedef struct _PT3_DMA_PAGE {
	dma_addr_t addr;
	u8 *data;
	u32 size;
	u32 data_pos;
} PT3_DMA_PAGE;

typedef struct __PT3_DMA {
	PT3_I2C *i2c;
	int real_index;
	int enabled;
	u32 desc_count;
	PT3_DMA_PAGE *desc_info;
	u32 ts_count;
	PT3_DMA_PAGE *ts_info;
	u32 ts_pos;
	struct mutex lock;
} PT3_DMA;

// pt3_bus.c ///////////////////////////////////////////////////////

enum {
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

	// テスト用
	_I_DATA_L_READ = I_DATA_H_READ ^ 0x04
//	_I_DATA_L_ACK0 = I_DATA_H_ACK0 ^ 0x04,
//	_I_DATA_L_ACK1 = I_DATA_H_ACK1 ^ 0x04
};

static void
add_instruction(PT3_BUS *bus, u32 instruction)
{
	if ((bus->inst_count % 2) == 0) {
		bus->tmp_inst = instruction;
	} else {
		bus->tmp_inst |= instruction << 4;
	}

	if (bus->inst_count % 2) {
		bus->insts[bus->inst_pos] = bus->tmp_inst;
		bus->inst_pos++;
		if (bus->inst_pos >= sizeof(bus->insts)) {
			PT3_PRINTK(0, KERN_ERR, "bus instructions is over flow\n");
			bus->inst_pos = 0;
		}
	}
	bus->inst_count++;
}

static u32
datan(PT3_BUS *bus, u32 index, u32 n)
{
	u32 i, data;

	if (unlikely(bus->buf == NULL)) {
		PT3_PRINTK(0, KERN_ERR, "buf is not ready.\n");
		return 0;
	}
	if (unlikely(bus->buf_size < index + n)) {
		PT3_PRINTK(0, KERN_ERR, "buf does not  have enough size. buf_size=%d\n",
				bus->buf_size);
		return 0;
	}

	data = 0;
	for (i = 0; i < n; i++) {
		data <<= 8;
		data |= bus->buf[index + i];
	}

	return data;
}

void
pt3_bus_start(PT3_BUS *bus)
{
	add_instruction(bus, I_DATA_H);
	add_instruction(bus, I_CLOCK_H);
	add_instruction(bus, I_DATA_L);
	add_instruction(bus, I_CLOCK_L);
}

void
pt3_bus_stop(PT3_BUS *bus)
{
	//add_instruction(bus, I_CLOCK_L);
	add_instruction(bus, I_DATA_L);
	add_instruction(bus, I_CLOCK_H);
	add_instruction(bus, I_DATA_H);
}

void
pt3_bus_write(PT3_BUS *bus, const u8 *data, u32 size)
{
	u32 i, j;
	u8 byte;

	for (i = 0; i < size; i++) {
		byte = data[i];
		for (j = 0; j < 8; j++) {
			add_instruction(bus, BIT_SHIFT_MASK(byte, 7 - j, 1) ?
									I_DATA_H_NOP : I_DATA_L_NOP);
		}
		add_instruction(bus, I_DATA_H_ACK0);
	}
}

u32
pt3_bus_read(PT3_BUS *bus, u8 *data, u32 size)
{
	u32 i, j;
	u32 index;

	for (i = 0; i < size; i++) {
		for (j = 0; j < 8; j++) {
			add_instruction(bus, I_DATA_H_READ);
		}

		if (i == (size - 1))
			add_instruction(bus, I_DATA_H_NOP);
		else
			add_instruction(bus, I_DATA_L_NOP);
	}
	index = bus->read_addr;
	bus->read_addr += size;
	if (likely(bus->buf == NULL)) {
		bus->buf = data;
		bus->buf_pos = 0;
		bus->buf_size = size;
	} else
		PT3_PRINTK(0, KERN_ERR, "bus read buff is already exists.\n");

	return index;
}

void
pt3_bus_push_read_data(PT3_BUS *bus, u8 data)
{
	if (unlikely(bus->buf != NULL)) {
		if (bus->buf_pos >= bus->buf_size) {
			PT3_PRINTK(0, KERN_ERR, "buffer over run. pos=%d\n", bus->buf_pos);
			bus->buf_pos = 0;
		}
		bus->buf[bus->buf_pos] = data;
		bus->buf_pos++;
	}
#if 0
	PT3_PRINTK(7, KERN_DEBUG, "bus read data=0x%02x\n", data);
#endif
}

u8
pt3_bus_data1(PT3_BUS *bus, u32 index)
{
	return (u8)datan(bus, index, 1);
}

void
pt3_bus_sleep(PT3_BUS *bus, u32 ms)
{
	u32 i;
	for (i = 0; i< ms; i++)
		add_instruction(bus, I_SLEEP);
}

void
pt3_bus_end(PT3_BUS *bus)
{
	add_instruction(bus, I_END);

	if (bus->inst_count % 2)
		add_instruction(bus, I_END);
}

void
pt3_bus_reset(PT3_BUS *bus)
{
	add_instruction(bus, I_RESET);
}

void
free_pt3_bus(PT3_BUS *bus)
{
	vfree(bus);
}

PT3_BUS *
create_pt3_bus(void)
{
	PT3_BUS *bus;

	bus = pt3_vzalloc(sizeof(PT3_BUS));
	if (bus == NULL)
		goto fail;

	bus->inst_addr = 0;
	bus->read_addr = 0;
	bus->inst_count = 0;
	bus->tmp_inst = 0;
	bus->inst_pos = 0;
	bus->buf = NULL;

	return bus;
fail:
	if (bus != NULL)
		free_pt3_bus(bus);
	return NULL;
}

// pt3_i2c.c ///////////////////////////////////////////////////////

#define DATA_OFFSET 2048

static void
wait(PT3_I2C *i2c, u32 *data)
{
	u32 val;

	while (1) {
		val = readl(i2c->bar[0] + REGS_I2C_R);
		if (!BIT_SHIFT_MASK(val, 0, 1))
			break;
		schedule_timeout_interruptible(msecs_to_jiffies(1));
	}

	if (data != NULL)
		*data = val;
}

static STATUS
run_code(PT3_I2C *i2c, u32 start_addr, u32 *ack)
{
	u32 data, a;

	wait(i2c, &data);

	if (unlikely(start_addr >= (1 << 13)))
		PT3_PRINTK(0, KERN_DEBUG, "start address is over.\n");

	writel(1 << 16 | start_addr, i2c->bar[0] + REGS_I2C_W);
#if 0
	PT3_PRINTK(7, KERN_DEBUG, "run i2c start_addr=0x%x\n", start_addr);
#endif

	wait(i2c, &data);

	a = BIT_SHIFT_MASK(data, 1, 2);
	if (ack != NULL)
		*ack = a;
	if (a)
		PT3_PRINTK(0, KERN_DEBUG, "fail i2c run_code status 0x%x\n", data);

	return BIT_SHIFT_MASK(data, 1, 2) ? STATUS_I2C_ERROR : STATUS_OK;
}

void
pt3_i2c_copy(PT3_I2C *i2c, PT3_BUS *bus)
{
	void __iomem *dst;
	u8 *src;
	u32 i;

	src = &bus->insts[0];
	dst = i2c->bar[1] + DATA_OFFSET + (bus->inst_addr / 2);

#if 0
	PT3_PRINTK(7, KERN_DEBUG, "PT3 : i2c_copy. base=%p dst=%p src=%p size=%d\n",
						i2c->bar[1], dst, src, bus->inst_pos);
#endif

#if 1
	for (i = 0; i < bus->inst_pos; i++) {
		writeb(src[i], dst + i);
	}
#else
	memcpy(dst, src, bus->inst_pos);
#endif
}

STATUS
pt3_i2c_run(PT3_I2C *i2c, PT3_BUS *bus, u32 *ack, int copy)
{
	STATUS status;
	u32 rsize, i;

	mutex_lock(&i2c->lock);

	if (copy) {
		pt3_i2c_copy(i2c, bus);
	}

	status = run_code(i2c, bus->inst_addr, ack);

	rsize = bus->read_addr;

	for (i = 0; i < rsize; i++) {
		pt3_bus_push_read_data(bus, readb(i2c->bar[1] + DATA_OFFSET + i));
	}
#if 0
	if (rsize > 0) {
		for (i = 1; i < 10; i++) {
			PT3_PRINTK(7, KERN_DEBUG, "bus_read_data + %d = 0x%x inst = 0x%x\n",
					i, readb(i2c->bar[1] + DATA_OFFSET + i),
					bus->insts[i]);
		}
	}
#endif

	mutex_unlock(&i2c->lock);

	return status;
}

int
pt3_i2c_is_clean(PT3_I2C *i2c)
{
	u32 val;

	val = readl(i2c->bar[0] + REGS_I2C_R);

	return BIT_SHIFT_MASK(val, 3, 1);
}

void
pt3_i2c_reset(PT3_I2C *i2c)
{
	writel(1 << 17, i2c->bar[0] + REGS_I2C_W);
}

void
free_pt3_i2c(PT3_I2C *i2c)
{
	vfree(i2c);
}

PT3_I2C *
create_pt3_i2c(u8 __iomem *bar[])
{
	PT3_I2C *i2c;

	i2c = pt3_vzalloc(sizeof(PT3_I2C));
	if (i2c == NULL)
		goto fail;

	mutex_init(&i2c->lock);
	i2c->bar[0] = bar[0];
	i2c->bar[1] = bar[1];

	return i2c;
fail:
	if (i2c != NULL)
		free_pt3_i2c(i2c);
	return NULL;
}

// pt3_tc.c ///////////////////////////////////////////////////////

static u32
byten(const u8 *data, u32 n)
{
	u32 value, i;

	value = 0;
	for (i = 0; i < n; i++) {
		value <<= 8;
		value |= data[i];
	}

	return value;
}

static u16
byte2(const u8 *data)
{
	return (u16)byten(data, 2);
}

static u32
byte3(const u8 *data)
{
	return byten(data, 3);
}

u8
pt3_tc_address(u32 pin, int isdb, u32 index)
{
	u8 isdb2 = (isdb == PT3_ISDB_S) ? 1 : 0;
	return (u8)(1 << 4 | pin << 2 | index << 1 | isdb2);
}

STATUS
pt3_tc_write(PT3_TC *tc, PT3_BUS *bus, u8 addr, const u8 *data, u32 size)
{
	STATUS status;
	u8 buf;
	PT3_BUS *p;

	p = bus ? bus : create_pt3_bus();
	if (p == NULL) {
		PT3_PRINTK(0, KERN_ERR, "out of memory.\n");
		return STATUS_OUT_OF_MEMORY_ERROR;
	}

	pt3_bus_start(p);
	buf = tc->tc_addr << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, &addr, 1);
	pt3_bus_write(p, data, size);
	pt3_bus_stop(p);

	if (bus) {
		status = STATUS_OK;
	} else {
		pt3_bus_end(p);
		status =  pt3_i2c_run(tc->i2c, p, NULL, 1);
	}

	if (!bus)
		free_pt3_bus(p);

	return status;
}

static STATUS
tc_read(PT3_TC *tc, PT3_BUS *bus, u8 addr, u8 *data, u32 size)
{
	STATUS status;
	u8 *buf    = kzalloc(size, GFP_KERNEL);
	u32 i, rindex;
	PT3_BUS *p;

	p = bus ? bus : create_pt3_bus();
	if (p == NULL) {
		PT3_PRINTK(0, KERN_ERR, "out of memory.\n");
		return STATUS_OUT_OF_MEMORY_ERROR;
	}

	pt3_bus_start(p);
	buf[0] = tc->tc_addr << 1;
	pt3_bus_write(p, &buf[0], 1);
	pt3_bus_write(p, &addr, 1);

	pt3_bus_start(p);
	buf[0] = tc->tc_addr << 1 | 1;
	pt3_bus_write(p, &buf[0], 1);
	rindex = pt3_bus_read(p, &buf[0], size);
	pt3_bus_stop(p);

	if (bus) {
		status = STATUS_OK;
	} else {
		pt3_bus_end(p);
		status = pt3_i2c_run(tc->i2c, p, NULL, 1);
		for (i = 0; i < size; i++)
			data[i] = pt3_bus_data1(p, rindex + i);
	}

	if (!bus)
		free_pt3_bus(p);

	kfree(buf);
	return status;
}

STATUS
pt3_tc_read_tuner_without_addr(PT3_TC *tc, PT3_BUS *bus, u8 *data, u32 size)
{
	STATUS status;
	u8 *buf    = kzalloc(size, GFP_KERNEL);
	u32 i;
	u32 rindex;
	PT3_BUS *p;

	memset(buf, 0, size);

	p = bus ? bus : create_pt3_bus();
	if (p == NULL) {
		PT3_PRINTK(0, KERN_ERR, "out of memory.\n");
		return STATUS_OUT_OF_MEMORY_ERROR;
	}

	pt3_bus_start(p);
	buf[0] = tc->tc_addr << 1;
	pt3_bus_write(p, &buf[0], 1);
	buf[0] = TC_THROUGH;
	pt3_bus_write(p, &buf[0], 1);
	buf[0] = (tc->tuner_addr << 1) | 0x01;
	pt3_bus_write(p, &buf[0], 1);

	pt3_bus_start(p);
	buf[0] = (tc->tc_addr << 1) | 0x01;
	pt3_bus_write(p, &buf[0], 1);
	rindex = pt3_bus_read(p, &buf[0], size);
	pt3_bus_stop(p);

	if (bus) {
		status = STATUS_OK;
	} else {
		pt3_bus_end(p);
		status = pt3_i2c_run(tc->i2c, p, NULL, 1);
		for (i = 0; i < size; i++)
			data[i] = pt3_bus_data1(p, rindex + i);
	}

	if (!bus)
		free_pt3_bus(p);

#if 0
	PT3_PRINTK(7, KERN_DEBUG, "read_tuner_without tc_addr=0x%x tuner_addr=0x%x\n",
			tc->tc_addr, tc->tuner_addr);
#endif

	kfree(buf);
	return status;
}

STATUS
pt3_tc_write_tuner_without_addr(PT3_TC *tc, PT3_BUS *bus, const u8 *data, u32 size)
{
	STATUS status;
	u8 buf;
	PT3_BUS *p;

	p = bus ? bus : create_pt3_bus();
	if (p == NULL) {
		PT3_PRINTK(0, KERN_ERR, "out of memory.\n");
		return STATUS_OUT_OF_MEMORY_ERROR;
	}

	pt3_bus_start(p);
	buf = tc->tc_addr << 1;
	pt3_bus_write(p, &buf, 1);
	buf = TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = tc->tuner_addr << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, data, size);
	pt3_bus_stop(p);

	if (bus)
		status = STATUS_OK;
	else {
		pt3_bus_end(p);
		status = pt3_i2c_run(tc->i2c, p, NULL, 1);
	}

	if (!bus)
		free_pt3_bus(p);

	return status;
}

STATUS
pt3_tc_read_tuner(PT3_TC *tc, PT3_BUS *bus, u8 addr, u8 *data, u32 size)
{
	STATUS status;
	u8 *buf    = kzalloc(size, GFP_KERNEL);
	u32 i;
	size_t rindex;
	PT3_BUS *p;

	memset(buf, 0, size);

	p = bus ? bus : create_pt3_bus();
	if (p == NULL) {
		PT3_PRINTK(0, KERN_ERR, "out of memory.\n");
		return STATUS_OUT_OF_MEMORY_ERROR;
	}

	pt3_bus_start(p);
	buf[0] = tc->tc_addr << 1;
	pt3_bus_write(p, &buf[0], 1);
	buf[0] = TC_THROUGH;
	pt3_bus_write(p, &buf[0], 1);
	buf[0] = tc->tuner_addr << 1;
	pt3_bus_write(p, &buf[0], 1);
	pt3_bus_write(p, &addr, 1);

	pt3_bus_start(p);
	buf[0] = tc->tc_addr << 1;
	pt3_bus_write(p, &buf[0], 1);
	buf[0] = TC_THROUGH;
	pt3_bus_write(p, &buf[0], 1);
	buf[0] = (tc->tuner_addr << 1) | 1;
	pt3_bus_write(p, &buf[0], 1);

	pt3_bus_start(p);
	buf[0] = (tc->tc_addr << 1) | 1;
	pt3_bus_write(p, &buf[0], 1);
	rindex = pt3_bus_read(p, &buf[0], size);
	pt3_bus_stop(p);

	if (bus) {
		status = STATUS_OK;
	} else {
		pt3_bus_end(p);
		status = pt3_i2c_run(tc->i2c, p, NULL, 1);
		for (i = 0; i < size; i++)
			data[i] = pt3_bus_data1(p, rindex + i);
	}

	if (!bus)
		free_pt3_bus(p);

#if 0
	PT3_PRINTK(7, KERN_DEBUG, "read_tuner tc_addr=0x%x tuner_addr=0x%x\n",
			tc->tc_addr, tc->tuner_addr);
#endif

	kfree(buf);
	return status;
}

STATUS
pt3_tc_write_tuner(PT3_TC *tc, PT3_BUS *bus, u8 addr, const u8 *data, u32 size)
{
	STATUS status;
	u8 buf;
	PT3_BUS *p;

	p = bus ? bus : create_pt3_bus();
	if (p == NULL) {
		PT3_PRINTK(0, KERN_ERR, "out of memory.\n");
		return STATUS_OUT_OF_MEMORY_ERROR;
	}

	pt3_bus_start(p);
	buf = tc->tc_addr << 1;
	pt3_bus_write(p, &buf, 1);
	buf = TC_THROUGH;
	pt3_bus_write(p, &buf, 1);
	buf = tc->tuner_addr << 1;
	pt3_bus_write(p, &buf, 1);
	pt3_bus_write(p, &addr, 1);
	pt3_bus_write(p, data, size);
	pt3_bus_stop(p);

	if (bus)
		status = STATUS_OK;
	else {
		pt3_bus_end(p);
		status = pt3_i2c_run(tc->i2c, p, NULL, 1);
	}

	if (!bus)
		free_pt3_bus(p);

	return status;
}

/* TC_S */

static STATUS
write_pskmsrst(PT3_TC *tc, PT3_BUS *bus)
{
	u8 buf;

	buf = 0x01;
	return pt3_tc_write(tc, bus, 0x03, &buf, 1);
}

STATUS
pt3_tc_init_s(PT3_TC *tc, PT3_BUS *bus)
{
	STATUS status;
	u8 buf;

	status = write_pskmsrst(tc, bus);
	if (status)
		return status;
	buf = 0x10;
	status = pt3_tc_write(tc, bus, 0x1e, &buf, 1);
	if (status)
		return status;

	return status;
}

/* TC_T */

static STATUS
write_imsrst(PT3_TC *tc, PT3_BUS *bus)
{
	u8 buf;

	buf = 0x01 << 6;
	return pt3_tc_write(tc, bus, 0x01, &buf, 1);
}

STATUS
pt3_tc_init_t(PT3_TC *tc, PT3_BUS *bus)
{
	STATUS status;
	u8 buf;

	status = write_imsrst(tc, bus);
	if (status)
		return status;
	buf = 0x10;
	status = pt3_tc_write(tc, bus, 0x1c, &buf, 1);
	if (status)
		return status;

	return status;
}

STATUS
pt3_tc_set_powers(PT3_TC *tc, PT3_BUS *bus, int tuner, int amp)
{
	STATUS status;
	u8 tuner_power = tuner ? 0x03 : 0x02;
	u8 amp_power = amp ? 0x03 : 0x02;

	u8 data = (tuner_power << 6) | (0x01 << 4) | (amp_power << 2) | 0x01 << 0;

	status = pt3_tc_write(tc, bus, 0x1e, &data, 1);

	return status;
}

u32
pt3_tc_index(PT3_TC *tc)
{
	return BIT_SHIFT_MASK(tc->tc_addr, 1, 1);
}

static u8 agc_data_s[2] = { 0xb0, 0x30 };
STATUS
pt3_tc_set_agc_s(PT3_TC *tc, PT3_BUS *bus, PT3_TC_AGC agc)
{
	STATUS status;
	u8 data;

	data = (agc == PT3_TC_AGC_AUTO) ? 0xff : 0x00;
	status = pt3_tc_write(tc, bus, 0x0a, &data, 1);
	if (status)
		return status;

	data = agc_data_s[pt3_tc_index(tc)];
	data |= (agc == PT3_TC_AGC_AUTO) ? 0x01 : 0x00;
	status = pt3_tc_write(tc, bus, 0x10, &data, 1);
	if (status)
		return status;

	data = (agc == PT3_TC_AGC_AUTO) ? 0x40 : 0x00;
	status = pt3_tc_write(tc, bus, 0x11, &data, 1);
	if (status)
		return status;

	status = write_pskmsrst(tc, bus);

	return status;
}

STATUS
pt3_tc_set_agc_t(PT3_TC *tc, PT3_BUS *bus, PT3_TC_AGC agc)
{
	STATUS status;
	u8 data;

	data = (agc == PT3_TC_AGC_AUTO) ? 64 : 0;
	status = pt3_tc_write(tc, bus, 0x25, &data, 1);
	if (status)
		return status;

	data = 0x4c;
	data |= (agc == PT3_TC_AGC_AUTO) ? 0x00 : 0x01;
	status = pt3_tc_write(tc, bus, 0x23, &data, 1);
	if (status)
		return status;

	status = write_imsrst(tc, bus);

	return status;
}

STATUS
pt3_tc_set_sleep_s(PT3_TC *tc, PT3_BUS *bus, int sleep)
{
	STATUS status;
	u8 buf;

	buf = sleep ? 1 : 0;
	status = pt3_tc_write(tc, bus, 0x17, &buf, 1);

	return status;
}

STATUS
pt3_tc_set_ts_pins_mode_s(PT3_TC *tc, PT3_BUS *bus, PT3_TS_PINS_MODE *mode)
{
	u32 clock_data, byte, valid;
	u8 data[2];
	STATUS status;

	clock_data = mode->clock_data;
	byte = mode->byte;
	valid = mode->valid;

	if (clock_data)
		clock_data++;
	if (byte)
		byte++;
	if (valid)
		valid++;

	data[0] = 0x15 | (valid << 6);
	data[1] = 0x04 | (clock_data << 4) | byte;

	status = pt3_tc_write(tc, bus, 0x1c, &data[0], 1);
	if (status)
		return status;
	status = pt3_tc_write(tc, bus, 0x1f, &data[1], 1);
	if (status)
		return status;

	return status;
}

STATUS
pt3_tc_set_ts_pins_mode_t(PT3_TC *tc, PT3_BUS *bus, PT3_TS_PINS_MODE *mode)
{
	u32 clock_data, byte, valid;
	u8 data;
	STATUS status;

	clock_data = mode->clock_data;
	byte = mode->byte;
	valid = mode->valid;

	if (clock_data)
		clock_data++;
	if (byte)
		byte++;
	if (valid)
		valid++;

	data = (u8)(0x01 | (clock_data << 6) | (byte << 4) | (valid << 2)) ;
	status = pt3_tc_write(tc, bus, 0x1d, &data, 1);

	return status;
}

STATUS
pt3_tc_read_retryov_tmunvld_fulock(PT3_TC *tc, PT3_BUS *bus, int *retryov, int *tmunvld, int *fulock)
{
	STATUS status;
	u8 data;

	status = tc_read(tc, bus, 0x80, &data, 1);
	if (status)
		return status;

	*retryov = BIT_SHIFT_MASK(data, 7, 1) ? 1 : 0;
	*tmunvld = BIT_SHIFT_MASK(data, 5, 1) ? 1 : 0;
	*fulock = BIT_SHIFT_MASK(data, 3, 1) ? 1 : 0;

	return status;
}

STATUS
pt3_tc_read_tmcc_s(PT3_TC *tc, PT3_BUS *bus, TMCC_S *tmcc)
{
	enum {
		BASE = 0xc5,
		SIZE = 0xe5 - BASE + 1
	};
	STATUS status;
	u8 data[SIZE];
	u32 i, byte_offset, bit_offset;

	status = tc_read(tc, bus, 0xc3, data, 1);
	if (status)
		return status;
	if (BIT_SHIFT_MASK(data[0], 4, 1))
		return STATUS_GENERAL_ERROR;

	status = tc_read(tc, bus, 0xce, data, 2);
	if (status)
		return status;
	if (byte2(data) == 0)
		return STATUS_GENERAL_ERROR;

	status = tc_read(tc, bus, 0xc3, data, 1);
	if (status)
		return status;
	tmcc->emergency = BIT_SHIFT_MASK(data[0], 2, 1);
	tmcc->extflag   = BIT_SHIFT_MASK(data[0], 1, 1);

	status = tc_read(tc, bus, 0xc5, data, SIZE);
	if (status)
		return status;
	tmcc->indicator = BIT_SHIFT_MASK(data[0xc5 - BASE], 3, 5);
	tmcc->uplink    = BIT_SHIFT_MASK(data[0xc7 - BASE], 0, 4);

	for (i = 0; i < 4; i++) {
		byte_offset = i / 2;
		bit_offset = (i % 2) ? 0 : 4;
		tmcc->mode[i] = BIT_SHIFT_MASK(data[0xc8 + byte_offset - BASE], bit_offset, 4);
		tmcc->slot[i] = BIT_SHIFT_MASK(data[0xca + i - BASE], 0, 6);
	}

	for (i = 0; i < 8; i++)
		tmcc->id[i] = byte2(data + 0xce + i * 2 - BASE);

	return status;
}

STATUS
pt3_tc_read_tmcc_t(PT3_TC *tc, PT3_BUS *bus, TMCC_T *tmcc)
{
	STATUS status;
	u8 data[8];
	u32 interleave0h, interleave0l, segment1h, segment1l;

	status = tc_read(tc, bus, 0xb2+0, &data[0], 4);
	if (status)
		return status;
	status = tc_read(tc, bus, 0xb2+4, &data[4], 4);
	if (status)
		return status;

	tmcc->system    = BIT_SHIFT_MASK(data[0], 6, 2);
	tmcc->indicator = BIT_SHIFT_MASK(data[0], 2, 4);
	tmcc->emergency = BIT_SHIFT_MASK(data[0], 1, 1);
	tmcc->partial   = BIT_SHIFT_MASK(data[0], 0, 1);

	tmcc->mode[0] = BIT_SHIFT_MASK(data[1], 5, 3);
	tmcc->mode[1] = BIT_SHIFT_MASK(data[2], 0, 3);
	tmcc->mode[2] = BIT_SHIFT_MASK(data[4], 3, 3);

	tmcc->rate[0] = BIT_SHIFT_MASK(data[1], 2, 3);
	tmcc->rate[1] = BIT_SHIFT_MASK(data[3], 5, 3);
	tmcc->rate[2] = BIT_SHIFT_MASK(data[4], 0, 3);

	interleave0h = BIT_SHIFT_MASK(data[1], 0, 2);
	interleave0l = BIT_SHIFT_MASK(data[2], 7, 1);

	tmcc->interleave[0] = interleave0h << 1 | interleave0l << 0;
	tmcc->interleave[1] = BIT_SHIFT_MASK(data[3], 2, 3);
	tmcc->interleave[2] = BIT_SHIFT_MASK(data[5], 5, 3);

	segment1h = BIT_SHIFT_MASK(data[3], 0, 2);
	segment1l = BIT_SHIFT_MASK(data[4], 6, 2);

	tmcc->segment[0] = BIT_SHIFT_MASK(data[2], 3, 4);
	tmcc->segment[1] = segment1h << 2 | segment1l << 0;
	tmcc->segment[2] = BIT_SHIFT_MASK(data[5], 1, 4);

	return status;
}

STATUS
pt3_tc_write_id_s(PT3_TC *tc, PT3_BUS *bus, u16 id)
{
	STATUS status;
	u8 data[2] = { id >> 8, (u8)id };

	status = pt3_tc_write(tc, bus, 0x8f, data, sizeof(data));

	return status;
}

STATUS
pt3_tc_read_id_s(PT3_TC *tc, PT3_BUS *bus, u16 *id)
{
	STATUS status;
	u8 data[2];

	status = tc_read(tc, bus, 0xe6, data, sizeof(data));
	if (status)
		return status;

	*id = byte2(data);

	return status;
}

STATUS
pt3_tc_write_slptim(PT3_TC *tc, PT3_BUS *bus, int sleep)
{
	STATUS status;
	u8 data;

	data = (1 << 7) | ((sleep ? 1 :0) <<4);
	status = pt3_tc_write(tc, bus, 0x03, &data, 1);

	return status;
}

STATUS
pt3_tc_read_agc_s(PT3_TC *tc, PT3_BUS *bus, u8 *agc)
{
	STATUS status;
	u8 data;

	status = tc_read(tc, bus, 0xba, &data, 1);
	if (status)
		return status;

	*agc = data & 0x7f;

	return status;
}

STATUS
pt3_tc_read_ifagc_dt(PT3_TC *tc, PT3_BUS *bus, u8 *ifagc_dt)
{
	STATUS status;

	status = tc_read(tc, bus, 0x82, ifagc_dt, 1);
	if (status)
		return status;

	return status;
}

STATUS
pt3_tc_read_cn_s(PT3_TC *tc, PT3_BUS *bus, u32 *cn)
{
	STATUS status;
	u8 data[2];

	status = tc_read(tc, bus, 0xbc, data, sizeof(data));
	if (status)
		return status;

	*cn = byte2(data);

	return status;
}

STATUS
pt3_tc_read_cndat_t(PT3_TC *tc, PT3_BUS *bus, u32 *cn)
{
	STATUS status;
	u8 data[3];

	status = tc_read(tc, bus, 0x8b, data, sizeof(data));
	if (status)
		return status;

	*cn = byte3(data);

	return status;
}

PT3_TC *
create_pt3_tc(PT3_I2C *i2c, u8 tc_addr, u8 tuner_addr)
{
	PT3_TC *tc;

	tc = NULL;

	tc = pt3_vzalloc(sizeof(PT3_TC));
	if (tc == NULL)
		goto fail;

	tc->i2c = i2c;
	tc->tc_addr = tc_addr;
	tc->tuner_addr = tuner_addr;
	tc->master_clock_freq = 78;

	return tc;
fail:
	if (tc != NULL)
		vfree(tc);
	return NULL;
}

void
free_pt3_tc(PT3_TC *tc)
{
	vfree(tc);
}

// pt3_qm.c ///////////////////////////////////////////////////////

#define INIT_DUMMY_RESET 0x0c

/* TUNER_S */
void
pt3_qm_get_channel_freq(u32 channel, int *bs, u32 *number, u32 *freq)
{
	if (channel < 12) {
		*bs = 1;
		*number = 1 + 2 * channel;
		*freq = 104948 + 3836 * channel;
	} else if (channel < 24) {
		channel -= 12;
		*bs = 0;
		*number = 2 + 2 * channel;
		*freq = 161300 + 4000 * channel;
	} else {
		channel -= 24;
		*bs = 0;
		*number = 1 + 2 * channel;
		*freq = 159300 + 4000 * channel;
	}
}

/* QM */
static u8 rw_reg[0x20] = {
	0x48, 0x1c, 0xa0, 0x10, 0xbc, 0xc5, 0x20, 0x33,
	0x06, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x00, 0xff, 0xf3, 0x00, 0x2a, 0x64, 0xa6, 0x86,
	0x8c, 0xcf, 0xb8, 0xf1, 0xa8, 0xf2, 0x89, 0x00,
};

static u8 flag[0x20] = {
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0,
	0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};

static STATUS
qm_write(PT3_QM *qm, PT3_BUS *bus, u8 addr, u8 data)
{
	STATUS ret;
	ret = pt3_tc_write_tuner(qm->tc, bus, addr, &data, sizeof(data));
	qm->reg[addr] = data;
	return ret;
}

static STATUS
qm_read(PT3_QM *qm, PT3_BUS *bus, u8 addr, u8 *data)
{
	STATUS status;
	if ((addr == 0x00 ) || (addr == 0x0d)) {
		status = pt3_tc_read_tuner(qm->tc, bus, addr, data, 1);
#if 0
		if (!bus)
			PT3_PRINTK(7, KERN_DEBUG "qm_read addr=0x%02x data=0x%02x\n", addr, *data);
#endif
	} else
		status = STATUS_OK;

	return status;
}

static void
qm_sleep(PT3_QM *qm, PT3_BUS *bus, u32 ms)
{
	if (bus)
		pt3_bus_sleep(bus, ms);
	else
		schedule_timeout_interruptible(msecs_to_jiffies(ms));
}

static STATUS
qm_set_sleep_mode(PT3_QM *qm, PT3_BUS *bus)
{
	STATUS status;
	PT3_QM_PARAM *param;

	param = &qm->param;

	if (param->standby) {
		qm->reg[0x01] &= (~(1 << 3)) & 0xff;
		qm->reg[0x01] |= 1 << 0;
		qm->reg[0x05] |= 1 << 3;

		status = qm_write(qm, bus, 0x05, qm->reg[0x05]);
		if (status)
			return status;
		status = qm_write(qm, bus, 0x01, qm->reg[0x01]);
		if (status)
			return status;
	} else {
		qm->reg[0x01] |= 1 <<3;
		qm->reg[0x01] &= (~(1 << 0)) & 0xff;
		qm->reg[0x05] &= (~(1 << 3)) & 0xff;

		status = qm_write(qm, bus, 0x01, qm->reg[0x01]);
		if (status)
			return status;
		status = qm_write(qm, bus, 0x05, qm->reg[0x05]);
		if (status)
			return status;
	}

	return status;
}

static STATUS
qm_set_search_mode(PT3_QM *qm, PT3_BUS *bus)
{
	STATUS status;
	PT3_QM_PARAM *param;

	param = &qm->param;

	if (param->fast_search_mode) {
		qm->reg[0x03] |= 0x01;
		status = qm_write(qm, bus, 0x03, qm->reg[0x03]);
		if (status)
			return status;
	} else {
		qm->reg[0x03] &= 0xfe;
		status = qm_write(qm, bus, 0x03, qm->reg[0x03]);
		if (status)
			return status;
	}

	return status;
}

static u32 SD_TABLE[24][2][3] = {
	{{0x38fae1, 0xd, 0x5},{0x39fae1, 0xd, 0x5},},
	{{0x3f570a, 0xe, 0x3},{0x570a, 0xe, 0x3},},
	{{0x5b333, 0xe, 0x5},{0x6b333, 0xe, 0x5},},
	{{0x3c0f5c, 0xf, 0x4},{0x3d0f5c, 0xf, 0x4},},
	{{0x26b85, 0xf, 0x6},{0x36b85, 0xf, 0x6},},
	{{0x38c7ae, 0x10, 0x5},{0x39c7ae, 0x10, 0x5},},
	{{0x3f23d7, 0x11, 0x3},{0x23d7, 0x11, 0x3},},
	{{0x58000, 0x11, 0x5},{0x68000, 0x11, 0x5},},
	{{0x3bdc28, 0x12, 0x4},{0x3cdc28, 0x12, 0x4},},
	{{0x23851, 0x12, 0x6},{0x33851, 0x12, 0x6},},
	{{0x38947a, 0x13, 0x5},{0x39947a, 0x13, 0x5},},
	{{0x3ef0a3, 0x14, 0x3},{0x3ff0a3, 0x14, 0x3},},
	{{0x3c8000, 0x16, 0x4},{0x3d8000, 0x16, 0x4},},
	{{0x48000, 0x16, 0x6},{0x58000, 0x16, 0x6},},
	{{0x3c8000, 0x17, 0x5},{0x3d8000, 0x17, 0x5},},
	{{0x48000, 0x18, 0x3},{0x58000, 0x18, 0x3},},
	{{0x3c8000, 0x18, 0x6},{0x3d8000, 0x18, 0x6},},
	{{0x48000, 0x19, 0x4},{0x58000, 0x19, 0x4},},
	{{0x3c8000, 0x1a, 0x3},{0x3d8000, 0x1a, 0x3},},
	{{0x48000, 0x1a, 0x5},{0x58000, 0x1a, 0x5},},
	{{0x3c8000, 0x1b, 0x4},{0x3d8000, 0x1b, 0x4},},
	{{0x48000, 0x1b, 0x6},{0x58000, 0x1b, 0x6},},
	{{0x3c8000, 0x1c, 0x5},{0x3d8000, 0x1c, 0x5},},
	{{0x48000, 0x1d, 0x3},{0x58000, 0x1d, 0x3},},
};

static STATUS
qm_tuning(PT3_QM *qm, PT3_BUS *bus, u32 *sd, u32 channel)
{
static u32 FREQ_TABLE[9][3] = {
	{ 2151000, 1, 7 },
	{ 1950000, 1, 6 },
	{ 1800000, 1, 5 },
	{ 1600000, 1, 4 },
	{ 1450000, 1, 3 },
	{ 1250000, 1, 2 },
	{ 1200000, 0, 7 },
	{  975000, 0, 6 },
	{  950000, 0, 0 }
};

	STATUS status;
	PT3_QM_PARAM *param = &qm->param;
	u8 i_data;
	u32 index, i, N, A;
	// u32 a;
	//double M, b;	// double

	qm->reg[0x08] &= 0xf0;
	qm->reg[0x08] |= 0x09;

	qm->reg[0x13] &= 0x9f;
	qm->reg[0x13] |= 0x20;

	for (i = 0; i < 8; i++) {
		if ((FREQ_TABLE[i+1][0] <= param->channel_freq) &&
				(param->channel_freq < FREQ_TABLE[i][0])) {
			i_data = qm->reg[0x02];
			i_data &= 0x0f;
			i_data |= FREQ_TABLE[i][1] << 7;
			i_data |= FREQ_TABLE[i][2] << 4;
			status = qm_write(qm, bus, 0x02, i_data);
		}
	}

#if 0
	//M = (double)(param->channel_freq) / (double)(param->crystal_freq);
	M = param->channel_freq / param->crystal_freq;
	//a = (__s32)(M + 0.5);
	a = (__s32)((M * 10 + 5) / 10);
	b = M - a;

	N = (a - 12) >> 2;
	A = a - 4 * (N + 1) - 5;

	if (0 <= b)
		//*sd = (u32)(pow(2, 20.) * b);
		*sd = (u32)((2^20) * b);
	else
		//*sd = (u32)(pow(2, 20.) * b + (1 << 22);
		*sd = (u32)((2 ^ 20) * b + (1 << 22));
#else
	index = pt3_tc_index(qm->tc);
	*sd = SD_TABLE[channel][index][0];
	N = SD_TABLE[channel][index][1];
	A = SD_TABLE[channel][index][2];
#endif
	qm->reg[0x06] &= 0x40;
	qm->reg[0x06] |= N;
	status = qm_write(qm, bus, 0x06, qm->reg[0x06]);
	if (status)
		return status;

	qm->reg[0x07] &= 0xf0;
	qm->reg[0x07] |= A & 0x0f;
	status = qm_write(qm, bus, 0x07, qm->reg[0x07]);
	if (status)
		return status;

	return status;
}

static STATUS
qm_local_lpf_tuning(PT3_QM *qm, PT3_BUS *bus, int lpf, u32 channel)
{
	PT3_QM_PARAM *param = &qm->param;
	u8 i_data;
	u32 sd;
	STATUS status;

	sd = 0;
	status = qm_tuning(qm, bus, &sd, channel);
	if (status)
		return status;

	if (lpf) {
		i_data = qm->reg[0x08] & 0xf0;
		i_data |= 2;
		status = qm_write(qm, bus, 0x08, i_data);
	} else {
		status = qm_write(qm, bus, 0x08, qm->reg[0x08]);
	}
	if (status)
		return status;

	qm->reg[0x09] &= 0xc0;
	qm->reg[0x09] |= (sd >> 16) & 0x3f;
	qm->reg[0x0a] = (sd >> 8) & 0xff;
	qm->reg[0x0b] = (sd >> 0) & 0xff;
	status = qm_write(qm, bus, 0x09, qm->reg[0x09]);
	if (status)
		return status;
	status = qm_write(qm, bus, 0x0a, qm->reg[0x0a]);
	if (status)
		return status;
	status = qm_write(qm, bus, 0x0b, qm->reg[0x0b]);
	if (status)
		return status;

	if (!lpf) {
		status = qm_write(qm, bus, 0x13, qm->reg[0x13]);
		if (status)
			return status;
	}

	if (lpf) {
		i_data = qm->reg[0x0c];
		i_data &= 0x3f;
		status = qm_write(qm, bus, 0x0c, i_data);
		if (status)
			return status;
		qm_sleep(qm, bus, 1);

		i_data = qm->reg[0x0c];
		i_data |= 0xc0;
		status = qm_write(qm, bus, 0x0c, i_data);
		if (status)
			return status;
	} else {
		i_data = qm->reg[0x0c];
		i_data &= 0x7f;
		status = qm_write(qm, bus, 0x0c, i_data);
		if (status)
			return status;
		qm_sleep(qm, bus, 2);	// 1024usec

		i_data = qm->reg[0x0c];
		i_data |= 0x80;
		status = qm_write(qm, bus, 0x0c, i_data);
		if (status)
			return status;
	}

	if (lpf) {
		qm_sleep(qm, bus, param->lpf_wait_time);
	} else {
		if (qm->reg[0x03] & 0x01) {
			qm_sleep(qm, bus, param->fast_search_wait_time);
		} else {
			qm_sleep(qm, bus, param->normal_search_wait_time);
		}
	}

	if (lpf) {
		status = qm_write(qm, bus, 0x08, 0x09);
		if (status)
			return status;
		status = qm_write(qm, bus, 0x13, qm->reg[0x13]);
		if (status)
			return status;
	}

	return status;
}

static u8 qm_address[MAX_TUNER] = { 0x63, 0x60 };

u8
pt3_qm_address(u32 index)
{
	return qm_address[index];
}

STATUS
pt3_qm_set_sleep(PT3_QM *qm, int sleep)
{
	STATUS status;
	PT3_TS_PIN_MODE mode;

	mode = sleep ? PT3_TS_PIN_MODE_LOW : PT3_TS_PIN_MODE_NORMAL;
	qm->param.standby = sleep;

	if (sleep) {
		status = pt3_tc_set_agc_s(qm->tc, NULL, PT3_TC_AGC_MANUAL);
		if (status)
			return status;
		qm_set_sleep_mode(qm, NULL);
		pt3_tc_set_sleep_s(qm->tc, NULL, sleep);
	} else {
		pt3_tc_set_sleep_s(qm->tc, NULL, sleep);
		qm_set_sleep_mode(qm, NULL);
	}

	qm->sleep = sleep;

	return STATUS_OK;
}

void
pt3_qm_dummy_reset(PT3_QM *qm, PT3_BUS *bus)
{
	qm_write(qm, bus, 0x01, INIT_DUMMY_RESET);
	qm_write(qm, bus, 0x01, INIT_DUMMY_RESET);
}

void
pt3_qm_init_reg_param(PT3_QM *qm)
{
	memcpy(qm->reg, rw_reg, sizeof(rw_reg));

	qm->param.channel_freq = 0;
	qm->param.crystal_freq = 16000;
	qm->param.fast_search_mode = 0;
	qm->param.standby = 0;
	qm->param.lpf_wait_time = 20;
	qm->param.fast_search_wait_time = 4;
	qm->param.normal_search_wait_time = 15;
}

STATUS
pt3_qm_init(PT3_QM *qm, PT3_BUS *bus)
{
	u8 i_data;
	u32 i;
	STATUS status;

	// soft reset on
	status = qm_write(qm, bus, 0x01, INIT_DUMMY_RESET);
	if (status)
		return status;

	qm_sleep(qm, bus, 1);

	// soft reset off
	i_data = qm->reg[0x01];
	i_data |= 0x10;
	status = qm_write(qm, bus, 0x01, i_data);
	if (status)
		return status;

	// ID check
	status = qm_read(qm, bus, 0x00, &i_data);
	if (status)
		return status;

	if ((bus == NULL) && (i_data != 0x48))
		return STATUS_INVALID_PARAM_ERROR;

	// LPF tuning on
	qm_sleep(qm, bus, 1);
	qm->reg[0x0c] |= 0x40;
	status = qm_write(qm, bus, 0x0c, qm->reg[0x0c]);
	if (status)
		return status;
	qm_sleep(qm, bus, qm->param.lpf_wait_time);

	for (i = 0; i < sizeof(flag); i++) {
		if (flag[i] == 1) {
			status = qm_write(qm, bus, i, qm->reg[i]);
			if (status)
				return status;
		}
	}

	status = qm_set_sleep_mode(qm, bus);
	if (status)
		return status;

	status = qm_set_search_mode(qm, bus);
	if (status)
		return status;

	return status;
}

STATUS
pt3_qm_get_locked(PT3_QM *qm, PT3_BUS *bus, int *locked)
{
	STATUS status;

	status = qm_read(qm, bus, 0x0d, &qm->reg[0x0d]);
	if (status)
		return status;

	if (qm->reg[0x0d] & 0x40)
		*locked = 1;
	else
		*locked = 0;

	return status;
}

STATUS
pt3_qm_set_frequency(PT3_QM *qm, u32 channel, __s32 offset)
{
	STATUS status;
	int bs, locked;
	u32 number, freq, freq_khz;
    ktime_t begin, now;

	status = pt3_tc_set_agc_s(qm->tc, NULL, PT3_TC_AGC_MANUAL);
	if (status)
		return status;

	pt3_qm_get_channel_freq(channel, &bs, &number, &freq);
	freq_khz = freq * 10 + offset;
	if (pt3_tc_index(qm->tc) == 0)
		freq_khz -= 500;
	else
		freq_khz += 500;
	qm->param.channel_freq = freq_khz;
	//PT3_PRINTK(7, KERN_DEBUG "frequency %d Khz\n", freq_khz);


	status = qm_local_lpf_tuning(qm, NULL, 1, channel);
	if (status)
		return status;

    begin = ktime_get();
	while (1) {
        now = ktime_get();

		status = pt3_qm_get_locked(qm, NULL, &locked);
		if (status)
			return status;
		if (locked)
			break;

		if (ktime_to_ns(ktime_sub(now, begin)) >= 100 * NSEC_PER_USEC)
			break;

		schedule_timeout_interruptible(msecs_to_jiffies(1));
	}
	// PT3_PRINTK(7, KERN_DEBUG "qm_get_locked %d status=0x%x\n", locked, status);
	if (!locked)
		return STATUS_PLL_LOCK_TIMEOUT_ERROR;

	status = pt3_tc_set_agc_s(qm->tc, NULL, PT3_TC_AGC_AUTO);
	if (status)
		return status;

	qm->channel = channel;
	qm->offset = offset;

	return status;
}

PT3_QM *
create_pt3_qm(PT3_I2C *i2c, PT3_TC *tc)
{
	PT3_QM *qm;

	qm = NULL;

	qm = pt3_vzalloc(sizeof(PT3_QM));
	if (qm == NULL)
		goto fail;

	qm->i2c = i2c;
	qm->tc = tc;
	qm->sleep = 1;

	return qm;
fail:
	if (qm != NULL)
		vfree(qm);
	return NULL;
}

void
free_pt3_qm(PT3_QM *qm)
{
	vfree(qm);
}

// pt3_mx.c ///////////////////////////////////////////////////////

typedef struct _SHF_TYPE {
	u32	freq;		// Channel center frequency
	u32	freq_th;	// Offset frequency threshold
	u8	shf_val;	// Spur shift value
	u8	shf_dir;	// Spur shift direction
} SHF_TYPE;

static SHF_TYPE SHF_DVBT_TAB[] = {
	// { Freq(kHz),Offset(kHz), Val,	Dir,	type},
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

static u8 mx_address[MAX_TUNER] = { 0x62, 0x61 };

static void
mx_write(PT3_MX *mx, PT3_BUS *bus, u8 *data, size_t size)
{
	pt3_tc_write_tuner_without_addr(mx->tc, bus, data, size);
}

static void
mx_read(PT3_MX *mx, PT3_BUS *bus, u8 addr, u8 *data)
{
	u8 write[2];
	write[0] = 0xfb;
	write[1] = addr;

	pt3_tc_write_tuner_without_addr(mx->tc, bus, write, sizeof(write));
	pt3_tc_read_tuner_without_addr(mx->tc, bus, data, 1);
}

static void
mx_get_register(PT3_MX *mx, PT3_BUS *bus, u8 addr, u8 *data)
{
	mx_read(mx, bus, addr, data);
}

static void
mx_rftune(u8 *data, u32 *size, u32 freq)
{
	u32 dig_rf_freq ,temp ,frac_divider, khz, mhz, i;
	u8 rf_data[] = {
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

	rf_data[2 * (7) + 1] = (u8)(dig_rf_freq);
	rf_data[2 * (8) + 1] = (u8)(dig_rf_freq >> 8);

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
}

static void
mx_set_register(PT3_MX *mx, PT3_BUS *bus, u8 addr, u8 value)
{
	u8 data[2];

	data[0] = addr;
	data[1] = value;

	mx_write(mx, bus, data, sizeof(data));
}

static void
mx_idac_setting(PT3_MX *mx, PT3_BUS *bus)
{
	u8 data[] = {
		0x0D, 0x00,
		0x0C, 0x67,
		0x6F, 0x89,
		0x70, 0x0C,
		0x6F, 0x8A,
		0x70, 0x0E,
		0x6F, 0x8B,
		0x70, 0x10+12,
	};

	mx_write(mx, bus, data, sizeof(data));
}

static void
mx_tuner_rftune(PT3_MX *mx, PT3_BUS *bus, u32 freq)
{
	u8 data[100];
	u32 size;

	size = 0;
	mx->freq = freq;

	mx_rftune(data, &size, freq);

	if (size != 20) {
		PT3_PRINTK(0, KERN_ERR, "fail mx_rftune size = %d\n", size);
		return;
	}

	mx_write(mx, bus, data, 14);

	schedule_timeout_interruptible(msecs_to_jiffies(1));

	mx_write(mx, bus, data + 14, 6);

	schedule_timeout_interruptible(msecs_to_jiffies(1));
	schedule_timeout_interruptible(msecs_to_jiffies(30));

	mx_set_register(mx, bus, 0x1a, 0x0d);

	mx_idac_setting(mx, bus);
}

static void
mx_standby(PT3_MX *mx, PT3_BUS *bus)
{
	u8 data[4];

	data[0] = 0x01;
	data[1] = 0x00;
	data[2] = 0x13;
	data[3] = 0x00;

	mx_write(mx, bus, data, sizeof(data));
}

static void
mx_wakeup(PT3_MX *mx, PT3_BUS *bus)
{
	u8 data[2];

	data[0] = 0x01;
	data[1] = 0x01;

	mx_write(mx, bus, data, sizeof(data));

	mx_tuner_rftune(mx, bus, mx->freq);
}

static STATUS
mx_set_sleep_mode(PT3_MX *mx, PT3_BUS *bus, int sleep)
{
	STATUS status;
	status = 0;

	if (sleep) {
		mx_standby(mx, bus);
	} else {
		mx_wakeup(mx, bus);
	}

	return status;
}

void
pt3_mx_get_channel_frequency(PT3_MX *mx, u32 channel, int *catv, u32 *number, u32 *freq)
{
static u8 FREQ_TABLE[][3] = {
	{   2,  0,   3 },
	{  12,  1,  22 },
	{  21,  0,  12 },
	{  62,  1,  63 },
	{ 112,  0,  62 }
};

	u32 i;
	__s32 freq_offset = 0;

	if (12 <= channel)
		freq_offset += 2;
	if (17 <= channel)
		freq_offset -= 2;
	if (63 <= channel)
		freq_offset += 2;
	*freq = 93 + channel * 6 + freq_offset;

	for (i = 0; i < sizeof(FREQ_TABLE) / sizeof(*FREQ_TABLE); i++) {
		if (channel <= FREQ_TABLE[i][0]) {
			*catv = FREQ_TABLE[i][1] ? 1: 0;
			*number = channel + FREQ_TABLE[i][2] - FREQ_TABLE[i][0];
			break;
		}
	}
}

static void
mx_set_frequency(PT3_MX *mx, PT3_BUS *bus, u32 freq)
{
	mx_tuner_rftune(mx, bus, freq);
}

static void
mx_rfsynth_lock_status(PT3_MX *mx, PT3_BUS *bus, int *locked)
{
	u8 data;

	*locked = 0;

	mx_get_register(mx, bus, 0x16, &data);

	data &= 0x0c;
	if (data == 0x0c)
		*locked = 1;
}

static void
mx_refsynth_lock_status(PT3_MX *mx, PT3_BUS *bus, int *locked)
{
	u8 data;

	*locked = 0;

	mx_get_register(mx, bus, 0x16, &data);

	data &= 0x03;
	if (data == 0x03)
		*locked = 1;
}

u8
pt3_mx_address(u32 index)
{
	return mx_address[index];
}

STATUS
pt3_mx_set_sleep(PT3_MX *mx, int sleep)
{
	STATUS status;

	if (sleep) {
		status = pt3_tc_set_agc_t(mx->tc, NULL, PT3_TC_AGC_MANUAL);
		if (status)
			return status;
		mx_set_sleep_mode(mx, NULL, sleep);
		pt3_tc_write_slptim(mx->tc, NULL, sleep);
	} else {
		pt3_tc_write_slptim(mx->tc, NULL, sleep);
		mx_set_sleep_mode(mx, NULL, sleep);
	}

	mx->sleep = sleep;

	return STATUS_OK;
}

STATUS
pt3_mx_get_locked1(PT3_MX *mx, PT3_BUS *bus, int *locked)
{
	mx_rfsynth_lock_status(mx, bus, locked);

	return STATUS_OK;
}

STATUS
pt3_mx_get_locked2(PT3_MX *mx, PT3_BUS *bus, int *locked)
{
	mx_refsynth_lock_status(mx, bus, locked);

	return STATUS_OK;
}

static u32 REAL_TABLE[112] = {
	0x58d3f49,0x5e8ccc9,0x6445a49,0x69fe7c9,0x6fb7549,
	0x75702c9,0x7b29049,0x80e1dc9,0x869ab49,0x8c538c9,
	0x920c649,0x97c53c9,0x9f665c9,0xa51f349,0xaad80c9,
	0xb090e49,0xb649bc9,0xba1a4c9,0xbfd3249,0xc58bfc9,
	0xcb44d49,0xd0fdac9,0xd6b6849,0xdc6f5c9,0xe228349,
	0xe7e10c9,0xed99e49,0xf352bc9,0xf90b949,0xfec46c9,
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

STATUS
pt3_mx_set_frequency(PT3_MX *mx, u32 channel, __s32 offset)
{
	STATUS status;
	int catv, locked1, locked2;
	u32 number, freq;
	u32 real_freq;
    ktime_t begin, now;

	status = pt3_tc_set_agc_t(mx->tc, NULL, PT3_TC_AGC_MANUAL);
	if (status)
		return status;

	pt3_mx_get_channel_frequency(mx, channel, &catv, &number, &freq);

	//real_freq = (7 * freq + 1 + offset) * 1000000.0 /7.0;
	real_freq = REAL_TABLE[channel];

	mx_set_frequency(mx, NULL, real_freq);

    begin = ktime_get();
	locked1 = locked2 = 0;
	while (1) {
        now = ktime_get();
		pt3_mx_get_locked1(mx, NULL, &locked1);
		pt3_mx_get_locked2(mx, NULL, &locked2);

		if (locked1 && locked2)
			break;
		if (ktime_to_ns(ktime_sub(now, begin)) > 1000 * NSEC_PER_USEC)
			break;

		schedule_timeout_interruptible(msecs_to_jiffies(1));
	}
#if 0
	PT3_PRINTK(7, KERN_DEBUG, "mx_get_locked1 %d locked2 %d\n", locked1, locked2);
#endif
	if (!(locked1 && locked2))
		return STATUS_PLL_LOCK_TIMEOUT_ERROR;

	status = pt3_tc_set_agc_t(mx->tc, NULL, PT3_TC_AGC_AUTO);
	if (status)
		return status;

	return status;
}

PT3_MX *
create_pt3_mx(PT3_I2C *i2c, PT3_TC *tc)
{
	PT3_MX *mx;

	mx = NULL;

	mx = pt3_vzalloc(sizeof(PT3_MX));
	if (mx == NULL)
		goto fail;

	mx->i2c = i2c;
	mx->tc = tc;
	mx->sleep = 1;

	return mx;
fail:
	if (mx != NULL)
		vfree(mx);
	return NULL;
}

void
free_pt3_mx(PT3_MX *mx)
{
	vfree(mx);
}

// pt3_dma.c ///////////////////////////////////////////////////////

#define DMA_DESC_SIZE		20
#define DMA_PAGE_SIZE		4096
#define MAX_DESCS			204		/* 4096 / 20 */
#if 1
#define PAGE_BLOCK_COUNT	(17)
#define PAGE_BLOCK_SIZE		(DMA_PAGE_SIZE * 47)
#else
#define PAGE_BLOCK_COUNT	(32)
#define PAGE_BLOCK_SIZE		(DMA_PAGE_SIZE * 47 * 8)
#endif
#define DMA_TS_BUF_SIZE		(PAGE_BLOCK_SIZE * PAGE_BLOCK_COUNT)
#define NOT_SYNC_BYTE		0x74

static u32
gray2binary(u32 gray, u32 bit)
{
	u32 binary, i, j, k;

	binary = 0;
	for (i = 0; i < bit; i++) {
		k = 0;
		for (j = i; j < bit; j++) {
			k = k ^ BIT_SHIFT_MASK(gray, j, 1);
		}
		binary |= k << i;
	}

	return binary;
}

static void
dma_link_descriptor(u64 next_addr, u8 *desc)
{
	(*(u64 *)(desc + 12)) = next_addr | 2;
}

static void
dma_write_descriptor(u64 ts_addr, u32 size, u64 next_addr, u8 *desc)
{
	(*(u64 *)(desc +  0)) = ts_addr   | 7;
	(*(u32 *)(desc +  8)) = size      | 7;
	(*(u64 *)(desc + 12)) = next_addr | 2;
}

void
pt3_dma_build_page_descriptor(PT3_DMA *dma, int loop)
{
	PT3_DMA_PAGE *desc_info, *ts_info;
	u64 ts_addr, desc_addr;
	u32 i, j, ts_size, desc_remain, ts_info_pos, desc_info_pos;
	u8 *prev, *curr;

	if (unlikely(dma == NULL)) {
		PT3_PRINTK(1, KERN_ERR, "dma build page descriptor needs DMA\n");
		return;
	}
#if 0
	PT3_PRINTK(7, KERN_DEBUG, "build page descriptor ts_count=%d ts_size=0x%x desc_count=%d desc_size=0x%x\n",
			dma->ts_count, dma->ts_info[0].size, dma->desc_count, dma->desc_info[0].size);
#endif

	desc_info_pos = ts_info_pos = 0;
	desc_info = &dma->desc_info[desc_info_pos];
#if 1
	if (unlikely(desc_info == NULL)) {
		PT3_PRINTK(0, KERN_ERR, "dma maybe failed allocate desc_info %d\n",
				desc_info_pos);
		return;
	}
#endif
	desc_addr = desc_info->addr;
	desc_remain = desc_info->size;
	desc_info->data_pos = 0;
	curr = &desc_info->data[desc_info->data_pos];
	prev = NULL;
#if 1
	if (unlikely(curr == NULL)) {
		PT3_PRINTK(0, KERN_ERR, "dma maybe failed allocate desc_info->data %d\n",
				desc_info_pos);
		return;
	}
#endif
	desc_info_pos++;

	for (i = 0; i < dma->ts_count; i++) {
#if 1
		if (unlikely(dma->ts_count <= ts_info_pos)) {
			PT3_PRINTK(0, KERN_ERR, "ts_info overflow max=%d curr=%d\n",
					dma->ts_count, ts_info_pos);
			return;
		}
#endif
		ts_info = &dma->ts_info[ts_info_pos];
#if 1
		if (unlikely(ts_info == NULL)) {
			PT3_PRINTK(0, KERN_ERR, "dma maybe failed allocate ts_info %d\n",
					ts_info_pos);
			return;
		}
#endif
		ts_addr = ts_info->addr;
		ts_size = ts_info->size;
		ts_info_pos++;
		// PT3_PRINTK(7, KERN_DEBUG, "ts_info addr=0x%llx size=0x%x\n", ts_addr, ts_size);
#if 1
		if (unlikely(ts_info == NULL)) {
			PT3_PRINTK(0, KERN_ERR, "dma maybe failed allocate ts_info %d\n",
					ts_info_pos);
			return;
		}
#endif
		for (j = 0; j < ts_size / DMA_PAGE_SIZE; j++) {
			if (desc_remain < DMA_DESC_SIZE) {
#if 1
				if (unlikely(dma->desc_count <= desc_info_pos)) {
					PT3_PRINTK(0, KERN_ERR, "desc_info overflow max=%d curr=%d\n",
							dma->desc_count, desc_info_pos);
					return;
				}
#endif
				desc_info = &dma->desc_info[desc_info_pos];
				desc_info->data_pos = 0;
				curr = &desc_info->data[desc_info->data_pos];
#if 1
				if (unlikely(curr == NULL)) {
					PT3_PRINTK(0, KERN_ERR, "dma maybe failed allocate desc_info->data %d\n",
							desc_info_pos);
					return;
				}
				/*
				PT3_PRINTK(7, KERN_DEBUG, "desc_info_pos=%d ts_addr=0x%llx remain=%d\n",
						desc_info_pos, ts_addr, desc_remain);
				*/
#endif
				desc_addr = desc_info->addr;
				desc_remain = desc_info->size;
				desc_info_pos++;
			}
			if (prev != NULL) {
				dma_link_descriptor(desc_addr, prev);
			}
			dma_write_descriptor(ts_addr, DMA_PAGE_SIZE, 0, curr);
#if 0
			PT3_PRINTK(7, KERN_DEBUG, "dma write desc ts_addr=0x%llx desc_info_pos=%d\n",
						ts_addr, desc_info_pos);
#endif
			ts_addr += DMA_PAGE_SIZE;

			prev = curr;
			desc_info->data_pos += DMA_DESC_SIZE;
			if (unlikely(desc_info->size <= desc_info->data_pos)) {
				PT3_PRINTK(0, KERN_ERR, "dma desc_info data overflow.\n");
				return;
			}
			curr = &desc_info->data[desc_info->data_pos];
			desc_addr += DMA_DESC_SIZE;
			desc_remain -= DMA_DESC_SIZE;
		}
	}

	if (prev != NULL) {
		if (loop)
			dma_link_descriptor(dma->desc_info->addr, prev);
		else
			dma_link_descriptor(1, prev);
	}
}

void __iomem *
get_base_addr(PT3_DMA *dma)
{
	return dma->i2c->bar[0] + REGS_DMA_DESC_L + (0x18 * dma->real_index);
}

void
pt3_dma_set_test_mode(PT3_DMA *dma, int test, u16 init, int not, int reset)
{
	void __iomem *base;
	u32 data;

	base = get_base_addr(dma);
	data = (reset ? 1: 0) << 18 | (not ? 1 : 0) << 17 | (test ? 1 : 0) << 16 | init;

	PT3_PRINTK(7, KERN_DEBUG, "set_test_mode base=%p data=0x%04d\n",
			base, data);

	writel(data, base + 0x0c);
}

void
pt3_dma_reset(PT3_DMA *dma)
{
	PT3_DMA_PAGE *page;
	u32 i;

	for (i = 0; i < dma->ts_count; i++) {
		page = &dma->ts_info[i];
		memset(page->data, 0, page->size);
		page->data_pos = 0;
		*page->data = NOT_SYNC_BYTE;
	}
	dma->ts_pos = 0;
}

void
pt3_dma_set_enabled(PT3_DMA *dma, int enabled)
{
	void __iomem *base;
	u32 data;
	u64 start_addr;

	base = get_base_addr(dma);
	start_addr = dma->desc_info->addr;

	if (enabled) {
		PT3_PRINTK(7, KERN_DEBUG, "enable dma real_index=%d start_addr=%llx\n",
				dma->real_index, start_addr);
		pt3_dma_reset(dma);
		writel( 1 << 1, base + 0x08);
		writel(BIT_SHIFT_MASK(start_addr,  0, 32), base + 0x0);
		writel(BIT_SHIFT_MASK(start_addr, 32, 32), base + 0x4);
		PT3_PRINTK(7, KERN_DEBUG, "set descriptor address low %llx\n",
				BIT_SHIFT_MASK(start_addr,  0, 32));
		PT3_PRINTK(7, KERN_DEBUG, "set descriptor address heigh %llx\n",
				BIT_SHIFT_MASK(start_addr, 32, 32));
		writel( 1 << 0, base + 0x08);
	} else {
		PT3_PRINTK(7, KERN_DEBUG, "disable dma real_index=%d\n", dma->real_index);
		writel(1 << 1, base + 0x08);
		while (1) {
			data = readl(base + 0x10);
			if (!BIT_SHIFT_MASK(data, 0, 1))
				break;
			schedule_timeout_interruptible(msecs_to_jiffies(1));
		}
	}
	dma->enabled = enabled;
}

int
pt3_dma_ready(PT3_DMA *dma)
{
	u32 next;
	PT3_DMA_PAGE *page;
	u8 *p;

	next = dma->ts_pos + 1;
	if (next >= dma->ts_count)
		next = 0;

	page = &dma->ts_info[next];
	p = &page->data[page->data_pos];

	if (*p == 0x47)
		return 1;
	if (*p == NOT_SYNC_BYTE)
		return 0;

	PT3_PRINTK(0, KERN_DEBUG, "invalid sync byte value=0x%02x ts_pos=%d data_pos=%d curr=0x%02x\n",
			*p, next, page->data_pos, dma->ts_info[dma->ts_pos].data[0]);

	return 0;
}

ssize_t
pt3_dma_copy(PT3_DMA *dma, char __user *buf, size_t size, loff_t *ppos, int look_ready)
{
	int ready;
	PT3_DMA_PAGE *page;
	size_t csize, remain;
	u32 lp;
	u32 prev;

	mutex_lock(&dma->lock);

	PT3_PRINTK(7, KERN_DEBUG, "dma_copy ts_pos=0x%x data_pos=0x%x\n",
				dma->ts_pos, dma->ts_info[dma->ts_pos].data_pos);

	remain = size;
	for (;;) {
		if (likely(look_ready)) {
			for (lp = 0; lp < 20; lp++) {
				ready = pt3_dma_ready(dma);
				if (ready)
					break;
				schedule_timeout_interruptible(msecs_to_jiffies(30));
			}
			if (!ready)
				goto last;
			prev = dma->ts_pos - 1;
			if (prev < 0 || dma->ts_count <= prev)
				prev = dma->ts_count - 1;
			if (dma->ts_info[prev].data[0] != NOT_SYNC_BYTE)
				PT3_PRINTK(7, KERN_INFO, "dma buffer overflow. prev=%d data=0x%x\n",
						prev, dma->ts_info[prev].data[0]);
		}
		page = &dma->ts_info[dma->ts_pos];
		for (;;) {
			if ((page->size - page->data_pos) > remain) {
				csize = remain;
			} else {
				csize = (page->size - page->data_pos);
			}
			if (copy_to_user(&buf[size - remain], &page->data[page->data_pos], csize)) {
				mutex_unlock(&dma->lock);
				return -EFAULT;
			}
			*ppos += csize;
			remain -= csize;
			page->data_pos += csize;
			if (page->data_pos >= page->size) {
				page->data_pos = 0;
				page->data[page->data_pos] = NOT_SYNC_BYTE;
				dma->ts_pos++;
				if (dma->ts_pos >= dma->ts_count)
					dma->ts_pos = 0;
				break;
			}
			if (remain <= 0)
				goto last;
		}
		// schedule_timeout_interruptible(msecs_to_jiffies(0));
	}
last:
	mutex_unlock(&dma->lock);

	return size - remain;
}

u32
pt3_dma_get_ts_error_packet_count(PT3_DMA *dma)
{
	void __iomem *base;
	u32 gray;

	base = get_base_addr(dma);

	gray = readl(base + 0x14);

	return gray2binary(gray, 32);
}

u32
pt3_dma_get_status(PT3_DMA *dma)
{
	void __iomem *base;
	u32 status;

	base = get_base_addr(dma);

	status = readl(base + 0x10);

	return status;
}

void
free_pt3_dma(struct pci_dev *hwdev, PT3_DMA *dma)
{
	PT3_DMA_PAGE *page;
	u32 i;
	if (dma->ts_info != NULL) {
		for (i = 0; i < dma->ts_count; i++) {
			page = &dma->ts_info[i];
			if (page->size != 0)
				pci_free_consistent(hwdev, page->size, page->data, page->addr);
		}
		kfree(dma->ts_info);
	}
	if (dma->desc_info != NULL) {
		for (i = 0; i < dma->desc_count; i++) {
			page = &dma->desc_info[i];
			if (page->size != 0)
				pci_free_consistent(hwdev, page->size, page->data, page->addr);
		}
		kfree(dma->desc_info);
	}
	kfree(dma);
}

PT3_DMA *
create_pt3_dma(struct pci_dev *hwdev, PT3_I2C *i2c, int real_index)
{
	PT3_DMA *dma;
	PT3_DMA_PAGE *page;
	u32 i;

	dma = kzalloc(sizeof(PT3_DMA), GFP_KERNEL);
	if (dma == NULL) {
		PT3_PRINTK(0, KERN_ERR, "fail allocate PT3_DMA\n");
		goto fail;
	}

	dma->enabled = 0;
	dma->i2c = i2c;
	dma->real_index = real_index;
	mutex_init(&dma->lock);

	dma->ts_count = PAGE_BLOCK_COUNT;
	dma->ts_info = kzalloc(sizeof(PT3_DMA_PAGE) * dma->ts_count, GFP_KERNEL);
	if (dma->ts_info == NULL) {
		PT3_PRINTK(0, KERN_ERR, "fail allocate PT3_DMA_PAGE\n");
		goto fail;
	}
	for (i = 0; i < dma->ts_count; i++) {
		page = &dma->ts_info[i];
		page->size = PAGE_BLOCK_SIZE;
		page->data_pos = 0;
		page->data = pci_alloc_consistent(hwdev, page->size, &page->addr);
		if (page->data == NULL) {
			PT3_PRINTK(0, KERN_ERR, "fail allocate consistent. %d\n", i);
			goto fail;
		}
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate TS buffer.\n");

	dma->desc_count = (DMA_TS_BUF_SIZE / (DMA_PAGE_SIZE) + MAX_DESCS - 1) / MAX_DESCS;
	dma->desc_info = kzalloc(sizeof(PT3_DMA_PAGE) * dma->desc_count, GFP_KERNEL);
	if (dma->desc_info == NULL) {
		PT3_PRINTK(0, KERN_ERR, "fail allocate PT3_DMA_PAGE\n");
		goto fail;
	}
	for (i = 0; i < dma->desc_count; i++) {
		page = &dma->desc_info[i];
		page->size = DMA_PAGE_SIZE;
		page->data_pos = 0;
		page->data = pci_alloc_consistent(hwdev, page->size, &page->addr);
		if (page->data == NULL) {
			PT3_PRINTK(0, KERN_ERR, "fail allocate consistent. %d\n", i);
			goto fail;
		}
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate Descriptor buffer.\n");
	pt3_dma_build_page_descriptor(dma, 1);
	PT3_PRINTK(7, KERN_DEBUG, "set page descriptor.\n");
#if 0
	dma_check_page_descriptor(dma);
#endif

	return dma;
fail:
	if (dma != NULL)
		free_pt3_dma(hwdev, dma);
	return NULL;
}

// pt3_pci.c ///////////////////////////////////////////////////////

#ifndef pt3_vzalloc
void *
pt3_vzalloc(unsigned long size)
{
	void *p = vmalloc(size);
	if (p)
		memset(p, 0, size);
	return p;
}
#endif

#define DRV_NAME	"pt3_cdev"
#define DRV_VERSION	"0.0.1"
#define DRV_RELDATE	"20210516"

// These identify the driver base version and may not be removed.
static char version[] __devinitdata = DRV_NAME " " DRV_VERSION " " DRV_RELDATE "\n";

MODULE_AUTHOR("anyone");
MODULE_DESCRIPTION("PCI Earthsoft PT3 driver");
MODULE_LICENSE("GPL");

int debug = 0;		// 1 normal messages, 0 quiet .. 7 verbose
static int lnb = 0;	// LNB OFF:0 +11V:1 +15V:2
static int lnb_force = 0; // Force enable LNB

module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(lnb, int, 0);
module_param(lnb_force, int, 0);
MODULE_PARM_DESC(debug, "debug lvel (0-7)");
MODULE_PARM_DESC(lnb, "LNB level (0:OFF 1:+11V 2:+15V)");
MODULE_PARM_DESC(lnb_force, "Force enable LNB");

#define VENDOR_ALTERA 0x1172
#define PCI_PT3_ID    0x4c15

static struct pci_device_id pt3_pci_tbl[] = {
	{ VENDOR_ALTERA, PCI_PT3_ID, PCI_ANY_ID, PCI_ANY_ID, 0, 0, 0 },
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, pt3_pci_tbl);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
static	DEFINE_MUTEX(pt3_biglock);
#endif

#define DRV_CLASS	"ptx"
#define DEV_NAME	"pt3video"
#define MAX_PCI_DEVICE 128		// 最大64枚

typedef struct _PT3_VERSION {
	u8		ptn;
	u8 		regs;
	u8		fpga;
} PT3_VERSION;

typedef struct _PT3_SYSTEM {
	u8		dma_descriptor_page_size;
	u8		can_transport_ts;
} PT3_SYSTEM;

typedef struct _PT3_TUNER {
	int tuner_no;
	PT3_TC *tc_s;
	PT3_TC *tc_t;
	PT3_QM *qm;
	PT3_MX *mx;
} PT3_TUNER;

typedef struct _PT3_CHANNEL PT3_CHANNEL;

typedef struct	_pt3_device{
	int bars;
	u8 __iomem* hw_addr[2];
	struct mutex	lock ;
	dev_t		dev ;
	int		card_number;
	u32		base_minor ;
	struct	cdev	cdev[MAX_CHANNEL];
	PT3_VERSION	version;
	PT3_SYSTEM	system;
	PT3_I2C	*i2c;
	PT3_TUNER	tuner[MAX_TUNER];
	PT3_CHANNEL	*channel[MAX_CHANNEL];
} PT3_DEVICE;

struct _PT3_CHANNEL {
	u32		valid ;
	u32		minor;
	PT3_TUNER	*tuner;
	int		type ;
	struct mutex	lock ;
	PT3_DEVICE	*ptr ;
	PT3_I2C	*i2c;
	PT3_DMA	*dma;
};

static int real_channel[MAX_CHANNEL] = {0, 1, 2, 3};
static int channel_type[MAX_CHANNEL] = {PT3_ISDB_S, PT3_ISDB_S, PT3_ISDB_T, PT3_ISDB_T};

static	PT3_DEVICE	*device[MAX_PCI_DEVICE];
static struct class	*pt3video_class;

static int
check_fpga_version(PT3_DEVICE *dev_conf)
{
	u32 val = readl(dev_conf->hw_addr[0] + REGS_VERSION);

	dev_conf->version.ptn  = ((val >> 24) & 0xFF);
	dev_conf->version.regs = ((val >> 16) & 0xFF);
	dev_conf->version.fpga = ((val >>  8) & 0xFF);
	if (dev_conf->version.ptn != 3) {
		PT3_PRINTK(0, KERN_ERR, "Not a PT3\n");
		return -1;
	}
	PT3_PRINTK(7, KERN_INFO, "PT%d found\n", dev_conf->version.ptn);

	if (dev_conf->version.fpga != 0x04) {
		PT3_PRINTK(0, KERN_ERR, "FPGA version 0x%x is not supported\n",
				dev_conf->version.fpga);
		return -1;
	}

	val = readl(dev_conf->hw_addr[0] + REGS_SYSTEM_R);
	dev_conf->system.can_transport_ts = ((val >> 5) & 0x01);
	dev_conf->system.dma_descriptor_page_size = (val & 0x1F);

	return 0;
}

#if 0
static int
get_tuner_status(int isdb, PT3_TUNER *tuner)
{
	int sleep;

	sleep = 1;
	switch (isdb) {
	case PT3_ISDB_S :
		sleep = tuner->qm->sleep;
		break;
	case PT3_ISDB_T :
		sleep = tuner->mx->sleep;
		break;
	}
	return sleep ? 1 : 0;
}
#endif

static STATUS
set_id_s(PT3_TUNER *tuner, u32 id)
{
	return pt3_tc_write_id_s(tuner->tc_s, NULL, (u16)id);
}

static STATUS
get_id_s(PT3_TUNER *tuner, u32 *id)
{
	STATUS status;
	u16 short_id;

	if (unlikely(id == NULL))
		return STATUS_INVALID_PARAM_ERROR;

	status = pt3_tc_read_id_s(tuner->tc_s, NULL, &short_id);
	if (status)
		return status;

	*id = short_id;

	return status;
}

static STATUS
get_tmcc_s(PT3_TUNER *tuner, TMCC_S *tmcc)
{
	if (unlikely(tmcc == NULL))
		return STATUS_INVALID_PARAM_ERROR;

	return pt3_tc_read_tmcc_s(tuner->tc_s, NULL, tmcc);
}

static STATUS
get_tmcc_t(PT3_TUNER *tuner, TMCC_T *tmcc)
{
	int b, retryov, tmunvld, fulock;

	if (unlikely(tmcc == NULL))
		return STATUS_INVALID_PARAM_ERROR;

	b = 0;
	while (1) {
		pt3_tc_read_retryov_tmunvld_fulock(tuner->tc_t, NULL, &retryov, &tmunvld, &fulock);
		if (!fulock) {
			b = 1;
			break;
		} else {
			if (retryov)
				break;
		}
		schedule_timeout_interruptible(msecs_to_jiffies(1));
	}

	if (likely(b))
		pt3_tc_read_tmcc_t(tuner->tc_t, NULL, tmcc);

	return b ? STATUS_OK : STATUS_GENERAL_ERROR;
}

static u32 LNB_SETTINGS[] = {
	(1 << 3 | 0 << 1) | (1 << 2 | 0 << 9),	// 0v
	(1 << 3 | 0 << 1) | (1 << 2 | 1 << 0),	// 12v
	(1 << 3 | 1 << 1) | (1 << 2 | 1 << 0),	// 15v
};

static STATUS
set_lnb(PT3_DEVICE *dev_conf, int lnb)
{
	if (unlikely(lnb < 0 || 2 < lnb))
		return STATUS_INVALID_PARAM_ERROR;
	writel(LNB_SETTINGS[lnb], dev_conf->hw_addr[0] + REGS_SYSTEM_W);
	return STATUS_OK;
}

static STATUS
set_frequency(int isdb, PT3_TUNER *tuner, u32 channel, __s32 offset)
{
	STATUS status;

	PT3_PRINTK(7, KERN_DEBUG, "set_freq isdb=%d tuner_no=%d channel=%d offset=%d\n",
			isdb, tuner->tuner_no, channel, offset);

	switch (isdb) {
	case PT3_ISDB_S :
		status = pt3_qm_set_frequency(tuner->qm, channel, 0);
		break;
	case PT3_ISDB_T :
		status = pt3_mx_set_frequency(tuner->mx, channel, offset);
		break;
	default :
		status = STATUS_INVALID_PARAM_ERROR;
	}

	return status;
}

static STATUS
set_tuner_sleep(int isdb, PT3_TUNER *tuner, int sleep)
{
	STATUS status;

	switch (isdb) {
	case PT3_ISDB_S :
		PT3_PRINTK(1, KERN_INFO, "TUNER %p ISDB_S %s\n", tuner,
					(sleep) ? "Sleep" : "Wakeup");
		status = pt3_qm_set_sleep(tuner->qm, sleep);
		break;
	case PT3_ISDB_T :
		PT3_PRINTK(1, KERN_INFO, "TUNER %p ISDB_T %s\n", tuner,
					(sleep) ? "Sleep" : "Wakeup");
		status = pt3_mx_set_sleep(tuner->mx, sleep);
		break;
	default :
		status = STATUS_INVALID_PARAM_ERROR;
	}
	schedule_timeout_interruptible(msecs_to_jiffies(50));

	return status;
}

static STATUS
init_tuner(PT3_I2C *i2c, PT3_TUNER *tuner)
{
	STATUS status;
	PT3_BUS *bus;

	pt3_qm_init_reg_param(tuner->qm);
	{
		bus = create_pt3_bus();
		if (bus == NULL)
			return STATUS_OUT_OF_MEMORY_ERROR;
		pt3_qm_dummy_reset(tuner->qm, bus);
		pt3_bus_end(bus);
		status = pt3_i2c_run(i2c, bus, NULL, 1);
		free_pt3_bus(bus);
		if (status) {
			PT3_PRINTK(7, KERN_DEBUG, "fail init_tuner dummy reset. status=0x%x\n", status);
			return status;
		}
	}

	{
		bus = create_pt3_bus();
		if (bus == NULL)
			return STATUS_OUT_OF_MEMORY_ERROR;
		status = pt3_qm_init(tuner->qm, bus);
		if (status) {
			free_pt3_bus(bus);
			return status;
		}
		pt3_bus_end(bus);
		status = pt3_i2c_run(i2c, bus, NULL, 1);
		free_pt3_bus(bus);
		if (status) {
			PT3_PRINTK(7, KERN_DEBUG, "fail init_tuner qm init. status=0x%x\n", status);
			return status;
		}
	}

	return status;
}

static STATUS
tuner_power_on(PT3_DEVICE *dev_conf, PT3_BUS *bus)
{
	STATUS status;
	int i, j;
	PT3_TS_PINS_MODE pins;

	PT3_TUNER *tuner;

	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &dev_conf->tuner[i];
		status = pt3_tc_init_s(tuner->tc_s, NULL);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "tc_init_s[%d] status=0x%x\n", i, status);
	}
	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &dev_conf->tuner[i];
		status = pt3_tc_init_t(tuner->tc_t, NULL);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "tc_init_t[%d] status=0x%x\n", i, status);
	}

	tuner = &dev_conf->tuner[1];
	status = pt3_tc_set_powers(tuner->tc_t, NULL, 1, 0);
	if (status) {
		PT3_PRINTK(7, KERN_DEBUG, "fail set powers.\n");
		goto last;
	}

	pins.clock_data = PT3_TS_PIN_MODE_NORMAL;
	pins.byte = PT3_TS_PIN_MODE_NORMAL;
	pins.valid = PT3_TS_PIN_MODE_NORMAL;

	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &dev_conf->tuner[i];
		status = pt3_tc_set_ts_pins_mode_s(tuner->tc_s, NULL, &pins);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "fail set ts pins mode s [%d] status=0x%x\n", i, status);
	}
	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &dev_conf->tuner[i];
		status = pt3_tc_set_ts_pins_mode_t(tuner->tc_t, NULL, &pins);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "fail set ts pins mode t [%d] status=0x%x\n", i, status);
	}

	schedule_timeout_interruptible(msecs_to_jiffies(1));

	for (i = 0; i < MAX_TUNER; i++) {
		for (j = 0; j < 10; j++) {
			if (j != 0)
				PT3_PRINTK(0, KERN_INFO, "retry init_tuner\n");
			status = init_tuner(dev_conf->i2c, &dev_conf->tuner[i]);
			if (!status)
				break;
			schedule_timeout_interruptible(msecs_to_jiffies(1));
		}
		if (status) {
			PT3_PRINTK(7, KERN_INFO, "fail init_tuner %d status=0x%x\n", i, status);
			goto last;
		}
	}

	if (unlikely(bus->inst_addr < 4096))
		pt3_i2c_copy(dev_conf->i2c, bus);

	bus->inst_addr = PT3_BUS_INST_ADDR1;
	status = pt3_i2c_run(dev_conf->i2c, bus, NULL, 0);
	if (status) {
		PT3_PRINTK(7, KERN_INFO, "failed inst_addr=0x%x status=0x%x\n",
				PT3_BUS_INST_ADDR1, status);
		goto last;
	}

	tuner = &dev_conf->tuner[1];
	status = pt3_tc_set_powers(tuner->tc_t, NULL, 1, 1);
	if (status) {
		 PT3_PRINTK(7, KERN_INFO, "fail tc_set_powers,\n");
		goto last;
	}

last:
	return status;
}

static STATUS
init_all_tuner(PT3_DEVICE *dev_conf)
{
	STATUS status;
	int i, j, channel;
	PT3_I2C *i2c = dev_conf->i2c;
	PT3_BUS *bus = create_pt3_bus();

	if (bus == NULL)
		return STATUS_OUT_OF_MEMORY_ERROR;

	pt3_bus_end(bus);
	bus->inst_addr = PT3_BUS_INST_ADDR0;

	if (!pt3_i2c_is_clean(i2c)) {
		PT3_PRINTK(0, KERN_INFO, "cleanup I2C bus.\n");
		status = pt3_i2c_run(i2c, bus, NULL, 0);
		if (status)
			goto last;
		schedule_timeout_interruptible(msecs_to_jiffies(10));
	}

	status = tuner_power_on(dev_conf, bus);
	if (status)
		goto last;
	PT3_PRINTK(7, KERN_DEBUG, "tuner_power_on\n");

	for (i = 0; i < MAX_TUNER; i++) {
		for (j = 0; j < PT3_ISDB_MAX; j++) {
			if (j == PT3_ISDB_S)
				channel = 0;
			else
				channel = (i == 0) ? 70 : 71;
			status = set_tuner_sleep(j, &dev_conf->tuner[i], 0);
			if (status)
				goto last;
			status = set_frequency(j, &dev_conf->tuner[i], channel, 0);
			if (status) {
				PT3_PRINTK(0, KERN_DEBUG, "fail set_frequency. status=0x%x\n", status);
			}
			status = set_tuner_sleep(j, &dev_conf->tuner[i], 1);
			if (status)
				goto last;
		}
	}
last:
	free_pt3_bus(bus);
	return status;
}

static STATUS
get_cn_agc(PT3_CHANNEL *channel, u32 *cn, u32 *curr_agc, u32 *max_agc)
{
	STATUS status;
	PT3_TUNER *tuner = channel->tuner;
	u8 byte_agc;

	switch (channel->type) {
	case PT3_ISDB_S:
		status = pt3_tc_read_cn_s(tuner->tc_s, NULL, cn);
		if (status)
			return status;
		status = pt3_tc_read_agc_s(tuner->tc_s, NULL, &byte_agc);
		if (status)
			return status;
		*curr_agc = byte_agc;
		*max_agc = 127;
		break;
	case PT3_ISDB_T:
		status = pt3_tc_read_cndat_t(tuner->tc_t, NULL, cn);
		if (status)
			return status;
		status = pt3_tc_read_ifagc_dt(tuner->tc_t, NULL, &byte_agc);
		if (status)
			return status;
		*curr_agc = byte_agc;
		*max_agc = 255;
		break;
	default:
		*cn = 0;
		*curr_agc = 0;
		*max_agc = 0;
	}
	PT3_PRINTK(7, KERN_INFO, "cn=0x%x\n", *cn);
	PT3_PRINTK(7, KERN_INFO, "agc=0x%x\n", *curr_agc);

	return STATUS_OK;
}

static STATUS
SetChannel(PT3_CHANNEL *channel, FREQUENCY *freq)
{
	TMCC_S tmcc_s;
	TMCC_T tmcc_t;
	u32 i, tsid;

	STATUS status = set_frequency(channel->type, channel->tuner, freq->frequencyno, freq->slot);
	if (status)
		return status;

	switch (channel->type) {
	case PT3_ISDB_S :
		for (i = 0; i < 1000; i++) {
			schedule_timeout_interruptible(msecs_to_jiffies(1));
			status = get_tmcc_s(channel->tuner, &tmcc_s);
			if (!status)
				break;
		}
		if (status) {
			PT3_PRINTK(1, KERN_ERR, "fail get_tmcc_s status=0x%x\n", status);
			return status;
		}
		PT3_PRINTK(7, KERN_DEBUG, "tmcc_s.id = 0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x,0x%x\n",
				tmcc_s.id[0], tmcc_s.id[1], tmcc_s.id[2], tmcc_s.id[3],
				tmcc_s.id[4], tmcc_s.id[5], tmcc_s.id[6], tmcc_s.id[7]);
		status = set_id_s(channel->tuner, tmcc_s.id[freq->slot]);
		if (status) {
			PT3_PRINTK(1, KERN_ERR, "fail set_tmcc_s status=0x%x\n", status);
			return status;
		}
		for (i = 0; i < 1000; i++) {
			status = get_id_s(channel->tuner, &tsid);
			if (status) {
				PT3_PRINTK(1, KERN_ERR, "fail get_id_s status=0x%x\n", status);
				return status;
			}
			PT3_PRINTK(7, KERN_DEBUG, "tsid=0x%x\n", tsid);
			if ((tsid & 0xffff) == tmcc_s.id[freq->slot]) {
				// wait for fill buffer
				schedule_timeout_interruptible(msecs_to_jiffies(100));
				// reset_error_count
				pt3_dma_set_test_mode(channel->dma, 0, 0, 0, 1);
				return STATUS_OK;
			}
			schedule_timeout_interruptible(msecs_to_jiffies(2));
		}
		break;
	case PT3_ISDB_T :
		for (i = 0; i < 1000; i++) {
			status = get_tmcc_t(channel->tuner, &tmcc_t);
			if (!status)
				break;
			schedule_timeout_interruptible(msecs_to_jiffies(2));
		}
		if (status) {
			PT3_PRINTK(1, KERN_ERR, "fail get_tmcc_t status=0x%x\n", status);
			return status;
		}
		// wait for fill buffer
		schedule_timeout_interruptible(msecs_to_jiffies(200));
		// reset_error_count
		pt3_dma_set_test_mode(channel->dma, 0, 0, 0, 1);
		return status;
		break;
	}

	return STATUS_INVALID_PARAM_ERROR;
}

static int
pt3_open(struct inode *inode, struct file *file)
{
	int major = imajor(inode);
	int minor = iminor(inode);
	int lp, lp2;
	PT3_CHANNEL *channel;

	for (lp = 0; lp < MAX_PCI_DEVICE; lp++) {
		if (device[lp] == NULL) {
			PT3_PRINTK(1, KERN_DEBUG, "device does not exist\n");
			return -EIO;
		}

		if (MAJOR(device[lp]->dev) == major &&
			device[lp]->base_minor <= minor &&
			device[lp]->base_minor + MAX_CHANNEL > minor) {

			mutex_lock(&device[lp]->lock);
			for (lp2 = 0; lp2 < MAX_CHANNEL; lp2++) {
				channel = device[lp]->channel[lp2];
				if (channel->minor == minor) {
					if (channel->valid) {
						mutex_unlock(&device[lp]->lock);
						PT3_PRINTK(1, KERN_DEBUG, "device is already used.\n");
						return -EIO;
					}
					PT3_PRINTK(7, KERN_DEBUG, "selected tuner_no=%d type=%d\n",
							channel->tuner->tuner_no, channel->type);

					set_tuner_sleep(channel->type, channel->tuner, 0);
					schedule_timeout_interruptible(msecs_to_jiffies(100));

					channel->valid = 1;
					file->private_data = channel;

					mutex_unlock(&device[lp]->lock);

					return 0;
				}
			}
			mutex_unlock(&device[lp]->lock);
		}
	}

	return -EIO;
}

static int
pt3_release(struct inode *inode, struct file *file)
{
	PT3_CHANNEL *channel = file->private_data;

	mutex_lock(&channel->ptr->lock);
	channel->valid = 0;
	pt3_dma_set_enabled(channel->dma, 0);
	mutex_unlock(&channel->ptr->lock);

	if (debug > 0)
		PT3_PRINTK(0, KERN_INFO, "(%d:%d) error count %d\n",
				imajor(inode), iminor(inode),
				pt3_dma_get_ts_error_packet_count(channel->dma));
	set_tuner_sleep(channel->type, channel->tuner, 1);
	schedule_timeout_interruptible(msecs_to_jiffies(50));

	return 0;
}

static int dma_look_ready[MAX_CHANNEL] = {1, 1, 1, 1};
static ssize_t
pt3_read(struct file *file, char __user *buf, size_t cnt, loff_t * ppos)
{
	size_t rcnt;
	PT3_CHANNEL *channel;

	channel = file->private_data;

	rcnt = pt3_dma_copy(channel->dma, buf, cnt, ppos,
						dma_look_ready[channel->dma->real_index]);
	if (rcnt < 0) {
		PT3_PRINTK(1, KERN_INFO, "fail copy_to_user.\n");
		return -EFAULT;
	}

	return rcnt;
}

static int
count_used_bs_tuners(PT3_DEVICE *device)
{
	int count, i;
	count = 0;

	for (i = 0; i < MAX_CHANNEL; i++) {
		if (device && device->channel[i] &&
			device->channel[i]->type == PT3_ISDB_S &&
			device->channel[i]->valid)
			count++;
	}

	PT3_PRINTK(1, KERN_INFO, "used bs tuners on %p = %d\n", device, count);

	return count;
}

static long
pt3_do_ioctl(struct file  *file, unsigned int cmd, unsigned long arg0)
{
	PT3_CHANNEL *channel;
	FREQUENCY freq;
	int status, signal, curr_agc, max_agc, lnb_eff, lnb_usr;
	unsigned int count;
	unsigned long dummy;
	char *voltage[] = {"0V", "11V", "15V"};
	void *arg;

	channel = file->private_data;
	arg = (void *)arg0;

	switch (cmd) {
	case SET_CHANNEL:
		dummy = copy_from_user(&freq, arg, sizeof(FREQUENCY));
		status = SetChannel(channel, &freq);
		return -status;
	case START_REC:
		pt3_dma_set_enabled(channel->dma, 1);
		return 0;
	case STOP_REC:
		pt3_dma_set_enabled(channel->dma, 0);
		return 0;
	case GET_SIGNAL_STRENGTH:
		status = get_cn_agc(channel, &signal, &curr_agc, &max_agc);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "fail get signal strength status=0x%x\n", status);
		dummy = copy_to_user(arg, &signal, sizeof(int));
		return 0;
	case LNB_ENABLE:
		count = count_used_bs_tuners(channel->ptr);
		if (count <= 1 && !lnb_force) {
			lnb_usr = (int)arg0;
			lnb_eff = lnb_usr ? lnb_usr : lnb;
			set_lnb(channel->ptr, lnb_eff);
			PT3_PRINTK(1, KERN_INFO, "LNB on %s\n", voltage[lnb_eff]);
		}
		return 0;
	case LNB_DISABLE:
		count = count_used_bs_tuners(channel->ptr);
		if (count <= 1 && !lnb_force) {
			set_lnb(channel->ptr, 0);
			PT3_PRINTK(1, KERN_INFO, "LNB off\n");
		}
		return 0;
	case GET_STATUS:
		status = (int)pt3_dma_get_status(channel->dma);
		dummy = copy_to_user(arg, &status, sizeof(int));
		return 0;
	case SET_TEST_MODE_ON:
		pt3_dma_build_page_descriptor(channel->dma, 0);
		PT3_PRINTK(7, KERN_DEBUG, "rebuild dma descriptor.\n");
		status = (1 + channel->dma->real_index) * 12345;
		pt3_dma_set_test_mode(channel->dma, 1, (u16)status, 0, 0);
		PT3_PRINTK(7, KERN_DEBUG, "set test mode.\n");
		schedule_timeout_interruptible(msecs_to_jiffies(10));
		pt3_dma_set_enabled(channel->dma, 1);
		schedule_timeout_interruptible(msecs_to_jiffies(10));
		while (1) {
			status = (int)pt3_dma_get_status(channel->dma);
			PT3_PRINTK(7, KERN_DEBUG, "status = 0x%x\n", status);
			if ((status & 0x01) == 0)
				break;
			if ((status >> 24) != 0x47)
				break;
			schedule_timeout_interruptible(msecs_to_jiffies(1));
		}
		dma_look_ready[channel->dma->real_index] = 0;
		return 0;
	case SET_TEST_MODE_OFF:
		dma_look_ready[channel->dma->real_index] = 1;
		pt3_dma_set_enabled(channel->dma, 0);
		pt3_dma_set_test_mode(channel->dma, 0, 0, 0, 1);
		pt3_dma_build_page_descriptor(channel->dma, 1);
		return 0;
	case GET_TS_ERROR_PACKET_COUNT:
		count = (int)pt3_dma_get_ts_error_packet_count(channel->dma);
		dummy = copy_to_user(arg, &count, sizeof(unsigned int));
		return 0;
	}
	return -EINVAL;
}

static long
pt3_unlocked_ioctl(struct file  *file, unsigned int cmd, unsigned long arg0)
{
	long ret;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
	if(mutex_lock_interruptible(&pt3_biglock))
		return -EINTR ;
#else
	lock_kernel();
#endif

	ret = pt3_do_ioctl(file, cmd, arg0);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,39)
	mutex_unlock(&pt3_biglock);
#else
	unlock_kernel();
#endif

	return ret;
}

static long
pt3_compat_ioctl(struct file  *file, unsigned int cmd, unsigned long arg0)
{
	/* should do 32bit <-> 64bit conversion here? --yaz */
	return (long)pt3_unlocked_ioctl(file, cmd, arg0);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
static int
pt3_ioctl(struct inode *inode, struct file  *file, unsigned int cmd, unsigned long arg0)
{
	return (int)pt3_do_ioctl(file, cmd, arg0);
}
#endif

static const struct file_operations pt3_fops = {
	.owner		=	THIS_MODULE,
	.open		=	pt3_open,
	.release	=	pt3_release,
	.read		=	pt3_read,
	.llseek	=	no_llseek,
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,36)
	.ioctl		=	pt3_ioctl,
#else
	.unlocked_ioctl	=	pt3_unlocked_ioctl,
	.compat_ioctl		=	pt3_compat_ioctl,
#endif
};

static int __devinit
pt3_pci_init_one (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		rc ;
	int		lp ;
	int		minor ;
	int		bars;
	u32		class_revision ;
	PT3_DEVICE	*dev_conf ;
	PT3_TUNER	*tuner;
	PT3_CHANNEL	*channel;

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	rc = pci_enable_device(pdev);
	if (rc)
		return rc;

	rc =pci_request_selected_regions(pdev, bars, DRV_NAME);
	if (rc)
		goto out_err_pci;

	pci_set_master(pdev);
	PT3_PRINTK(0, KERN_INFO, "Bus Mastering Enabled.\n");
	rc = pci_save_state(pdev);
	if (rc)
		goto out_err_reg;

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class_revision);
	if ((class_revision & 0xFF) != 1) {
		PT3_PRINTK(0, KERN_ERR, "Revision %x is not supported\n",
				(class_revision & 0xFF));
		goto out_err_reg;
	}
	PT3_PRINTK(7, KERN_DEBUG, "Revision 0x%x passed\n", class_revision & 0xff);

	dev_conf = kzalloc(sizeof(PT3_DEVICE), GFP_KERNEL);
	if(!dev_conf){
		PT3_PRINTK(0, KERN_ERR, "out of memory!\n");
		goto out_err_reg;
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate PT3_DEVICE.\n");

	// PCIアドレスをマップする
	dev_conf->bars = bars;
	dev_conf->hw_addr[0] = pci_ioremap_bar(pdev, 0);
	if (!dev_conf->hw_addr[0])
		goto out_err_fpga;
	dev_conf->hw_addr[1] = pci_ioremap_bar(pdev, 2);
	if (!dev_conf->hw_addr[1])
		goto out_err_fpga;

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	} else {
		PT3_PRINTK(0, KERN_ERR, "DMA MASK ERROR\n");
		goto out_err_fpga;
	}

	if(check_fpga_version(dev_conf)){
		goto out_err_fpga;
	}
	mutex_init(&dev_conf->lock);
	dev_conf->i2c = create_pt3_i2c(dev_conf->hw_addr);
	if (dev_conf->i2c == NULL) {
		PT3_PRINTK(0, KERN_ERR, "cannot allocate i2c.\n");
		goto out_err_fpga;
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate PT3_I2C.\n");
	set_lnb(dev_conf, lnb_force ? lnb : 0);
	// Tuner
	for (lp = 0; lp < MAX_TUNER; lp++) {
		u8 tc_addr, tuner_addr;
		u32 pin;

		tuner = &dev_conf->tuner[lp];
		tuner->tuner_no = lp;
		pin = 0;
		tc_addr = pt3_tc_address(pin, PT3_ISDB_S, lp);
		tuner_addr = pt3_qm_address(lp);

		tuner->tc_s = create_pt3_tc(dev_conf->i2c, tc_addr, tuner_addr);
		tuner->qm   = create_pt3_qm(dev_conf->i2c, tuner->tc_s);

		tc_addr = pt3_tc_address(pin, PT3_ISDB_T, lp);
		tuner_addr = pt3_mx_address(lp);

		tuner->tc_t = create_pt3_tc(dev_conf->i2c, tc_addr, tuner_addr);
		tuner->mx   = create_pt3_mx(dev_conf->i2c, tuner->tc_t);
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate tuners.\n");

	rc = init_all_tuner(dev_conf);
	if (rc) {
		PT3_PRINTK(0, KERN_ERR, "fail init_all_tuner. 0x%x\n", rc);
		goto out_err_i2c;
	}

	for(lp = 0 ; lp < MAX_PCI_DEVICE ; lp++){
		PT3_PRINTK(0, KERN_INFO, "device[%d]=%p\n", lp, device[lp]);
		if(device[lp] == NULL){
			device[lp] = dev_conf ;
			dev_conf->card_number = lp;
			break ;
		}
	}

	rc =alloc_chrdev_region(&dev_conf->dev, 0, MAX_CHANNEL, DEV_NAME);
	if (rc < 0)
		goto out_err_i2c;
	minor = MINOR(dev_conf->dev) ;
	dev_conf->base_minor = minor ;
	for (lp = 0; lp < MAX_CHANNEL; lp++) {
		cdev_init(&dev_conf->cdev[lp], &pt3_fops);
		dev_conf->cdev[lp].owner = THIS_MODULE;
		rc = cdev_add(&dev_conf->cdev[lp],
			MKDEV(MAJOR(dev_conf->dev), (MINOR(dev_conf->dev) + lp)), 1);
		if (rc < 0) {
			PT3_PRINTK(0, KERN_ERR, "fail cdev_add.\n");
		}

		channel = kzalloc(sizeof(PT3_CHANNEL), GFP_KERNEL);
		if (channel == NULL) {
			PT3_PRINTK(0, KERN_ERR, "out of memory!\n");
			goto out_err_dma;
		}

		channel->dma = create_pt3_dma(pdev, dev_conf->i2c, real_channel[lp]);
		if (channel->dma == NULL) {
			PT3_PRINTK(0, KERN_ERR, "fail create dma.\n");
			kfree(channel);
			goto out_err_dma;
		}

		mutex_init(&channel->lock);
		channel->minor = MINOR(dev_conf->dev) + lp;
		channel->tuner = &dev_conf->tuner[real_channel[lp] & 1];
		channel->type  = channel_type[lp];
		channel->ptr   = dev_conf;
		channel->i2c   = dev_conf->i2c;

		dev_conf->channel[lp] = channel;

		PT3_PRINTK(0, KERN_INFO, "card_number=%d channel=%d\n",
					dev_conf->card_number, real_channel[lp]);
		device_create(pt3video_class,
				NULL,
				MKDEV(MAJOR(dev_conf->dev), (MINOR(dev_conf->dev) + lp)),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
				NULL,
#endif
				DEV_NAME "%u",
				MINOR(dev_conf->dev) + lp + (dev_conf->card_number * MAX_CHANNEL));
	}

	pci_set_drvdata(pdev, dev_conf);
	return 0;

out_err_dma:
	for (lp = 0; lp < MAX_CHANNEL; lp++) {
		if (dev_conf->channel[lp] != NULL) {
			if (dev_conf->channel[lp]->dma != NULL)
				free_pt3_dma(pdev, dev_conf->channel[lp]->dma);
			kfree(dev_conf->channel[lp]);
			device_destroy(pt3video_class,
					MKDEV(MAJOR(dev_conf->dev), (MINOR(dev_conf->dev) + lp)));
		}
	}
out_err_i2c:
	for (lp = 0; lp < MAX_TUNER; lp++) {
		tuner = &dev_conf->tuner[lp];
		free_pt3_tc(tuner->tc_s);
		free_pt3_qm(tuner->qm);
		free_pt3_tc(tuner->tc_t);
		free_pt3_mx(tuner->mx);
	}
	free_pt3_i2c(dev_conf->i2c);
out_err_fpga:
	if (dev_conf->hw_addr[0])
		iounmap(dev_conf->hw_addr[0]);
	if (dev_conf->hw_addr[1])
		iounmap(dev_conf->hw_addr[1]);
	kfree(dev_conf);
out_err_reg:
	pci_release_selected_regions(pdev, bars);
out_err_pci:
	pci_disable_device(pdev);
	return -EIO;
}

static void __devexit
pt3_pci_remove_one(struct pci_dev *pdev)
{
	u32		lp;
	PT3_TUNER	*tuner;
	PT3_CHANNEL	*channel;
	PT3_DEVICE	*dev_conf = (PT3_DEVICE *)pci_get_drvdata(pdev);

	if(dev_conf){
		for (lp = 0; lp < MAX_CHANNEL; lp++) {
			channel = dev_conf->channel[lp];
			if (channel->dma->enabled)
				pt3_dma_set_enabled(channel->dma, 0);
			set_tuner_sleep(channel->type, channel->tuner, 1);
		}
		set_lnb(dev_conf, 0);
		for (lp = 0; lp < MAX_TUNER; lp++) {
			tuner = &dev_conf->tuner[lp];

			if (tuner->tc_s != NULL) {
				free_pt3_tc(tuner->tc_s);
			}
			if (tuner->tc_t != NULL) {
				pt3_tc_set_powers(tuner->tc_t, NULL, 0, 0);
				free_pt3_tc(tuner->tc_t);
			}
			if (tuner->qm != NULL)
				free_pt3_qm(tuner->qm);
			if (tuner->mx != NULL)
				free_pt3_mx(tuner->mx);
		}
		for (lp = 0; lp < MAX_CHANNEL; lp++) {
			if (dev_conf->channel[lp] != NULL) {
				cdev_del(&dev_conf->cdev[lp]);
				if (dev_conf->channel[lp]->dma != NULL)
					free_pt3_dma(pdev, dev_conf->channel[lp]->dma);
				kfree(dev_conf->channel[lp]);
			}
			device_destroy(pt3video_class,
						MKDEV(MAJOR(dev_conf->dev), (MINOR(dev_conf->dev) + lp)));
		}
		pt3_i2c_reset(dev_conf->i2c);
		free_pt3_i2c(dev_conf->i2c);

		unregister_chrdev_region(dev_conf->dev, MAX_CHANNEL);
		if (dev_conf->hw_addr[0])
			iounmap(dev_conf->hw_addr[0]);
		if (dev_conf->hw_addr[1])
			iounmap(dev_conf->hw_addr[1]);
		pci_release_selected_regions(pdev, dev_conf->bars);
		device[dev_conf->card_number] = NULL;
		kfree(dev_conf);
		PT3_PRINTK(0, KERN_DEBUG, "free PT3 DEVICE.\n");
	}
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
static int
pt3_pci_suspend (struct pci_dev *pdev, pm_message_t state)
{
	return 0;
}

static int
pt3_pci_resume (struct pci_dev *pdev)
{
	return 0;
}
#endif /* CONFIG_PM */

static struct pci_driver pt3_driver = {
	.name		= DRV_NAME,
	.probe		= pt3_pci_init_one,
	.remove	= __devexit_p(pt3_pci_remove_one),
	.id_table	= pt3_pci_tbl,
#ifdef CONFIG_PM
	.suspend	= pt3_pci_suspend,
	.resume	= pt3_pci_resume,
#endif /* CONFIG_PM */
};

static int __init
pt3_pci_init(void)
{
	PT3_PRINTK(0, KERN_INFO, "%s", version);
	pt3video_class = class_create(THIS_MODULE, DRV_CLASS);
	if (IS_ERR(pt3video_class))
		return PTR_ERR(pt3video_class);
	return pci_register_driver(&pt3_driver);
}

static void __exit
pt3_pci_cleanup(void)
{
	pci_unregister_driver(&pt3_driver);
	class_destroy(pt3video_class);
}

module_init(pt3_pci_init);
module_exit(pt3_pci_cleanup);