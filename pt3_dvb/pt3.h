#ifndef	__PT3_H__
#define	__PT3_H__

#define pr_fmt(fmt) KBUILD_MODNAME " " fmt

#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_frontend.h"

#define DRV_NAME "pt3_dvb"
#define ID_VEN_ALTERA	0x1172
#define ID_DEV_PT3	0x4c15

#define PT3_NR_ADAPS 4
#define PT3_SHIFT_MASK(val, shift, mask) (((val) >> (shift)) & (((u64)1<<(mask))-1))

/* register idx */
#define REG_VERSION	0x00	/*	R	Version		*/
#define REG_BUS		0x04	/*	R	Bus		*/
#define REG_SYSTEM_W	0x08	/*	W	System		*/
#define REG_SYSTEM_R	0x0c	/*	R	System		*/
#define REG_I2C_W 	0x10	/*	W	I2C		*/
#define REG_I2C_R 	0x14	/*	R	I2C		*/
#define REG_RAM_W 	0x18	/*	W	RAM		*/
#define REG_RAM_R 	0x1c	/*	R	RAM		*/
#define REG_BASE  	0x40	/* + 0x18*idx			*/
#define REG_DMA_DESC_L	0x00	/*	W	DMA		*/
#define REG_DMA_DESC_H	0x04	/*	W	DMA		*/
#define REG_DMA_CTL	0x08	/*	W	DMA		*/
#define REG_TS_CTL	0x0c	/*	W	TS		*/
#define REG_STATUS	0x10	/*	R	DMA/FIFO/TS	*/
#define REG_TS_ERR	0x14	/*	R	TS		*/

static int lnb = 2;	/* used if not set by frontend / the value is invalid */
module_param(lnb, int, 0);
MODULE_PARM_DESC(lnb, "LNB level (0:OFF 1:+11V 2:+15V)");

static DEFINE_PCI_DEVICE_TABLE(pt3_id_table) = {
	{ PCI_DEVICE(ID_VEN_ALTERA, ID_DEV_PT3) },
	{ },
};
MODULE_DEVICE_TABLE(pci, pt3_id_table);

struct pt3_dma_page {
	dma_addr_t addr;
	u8 *data;
	u32 size, data_pos;
};

struct pt3_i2c {
	u8 __iomem *reg[2];
	struct mutex lock;
};

enum {
	LAYER_INDEX_L = 0,
	LAYER_INDEX_H,

	LAYER_INDEX_A = 0,
	LAYER_INDEX_B,
	LAYER_INDEX_C
};

enum {
	LAYER_COUNT_S = LAYER_INDEX_H + 1,
	LAYER_COUNT_T = LAYER_INDEX_C + 1,
};

/* Transmission and Multiplexing Configuration Control */

struct tmcc_s {
	u32 indicator;
	u32 mode[4];
	u32 slot[4];
	u32 id[8];
	u32 emergency;
	u32 uplink;
	u32 extflag;
};

struct tmcc_t {
	u32 system;
	u32 indicator;
	u32 emergency;
	u32 partial;
	u32 mode[LAYER_COUNT_T];
	u32 rate[LAYER_COUNT_T];
	u32 interleave[LAYER_COUNT_T];
	u32 segment[LAYER_COUNT_T];
};

struct pt3_adapter;

struct pt3_dma {
	struct pt3_adapter *adap;
	bool enabled;
	u32 ts_pos, ts_count, desc_count;
	struct pt3_dma_page *ts_info, *desc_info;
	struct mutex lock;
};

struct pt3_qm {
	struct pt3_adapter *adap;
	u8 reg[32];

	bool standby;
	u32 wait_time_lpf, wait_time_search_fast, wait_time_search_normal;
	struct tmcc_s tmcc;
};

struct pt3_board {
	struct mutex lock;
	bool reset;
	int lnb;

	struct pci_dev *pdev;
	void __iomem *reg[2];
	int bars;
	struct pt3_i2c *i2c;

	struct pt3_adapter *adap[PT3_NR_ADAPS];
};

struct pt3_adapter {
	struct mutex lock;
	struct pt3_board *pt3;

	int idx, init_ch;
	char *str;
	fe_delivery_system_t type;
	bool in_use, sleep;
	u32 channel;
	s32 offset;
	u8 addr_tc, addr_tuner;
	u32 freq;
	struct pt3_qm *qm;
	struct pt3_dma *dma;
	struct task_struct *kthread;

	struct dvb_adapter dvb;
	struct dvb_demux demux;
	int users;
	struct dmxdev dmxdev;
	struct dvb_frontend *fe;
	int (*orig_voltage)(struct dvb_frontend *fe, fe_sec_voltage_t voltage);
	int (*orig_sleep  )(struct dvb_frontend *fe                          );
	int (*orig_init   )(struct dvb_frontend *fe                          );
	fe_sec_voltage_t voltage;
};

struct {
	fe_delivery_system_t type;
	u8 addr_tuner, addr_tc;
	int init_ch;
	char *str;
} pt3_config[] = {
	{SYS_ISDBS, 0x63, 0b00010001,  0, "ISDB-S"},
	{SYS_ISDBS, 0x60, 0b00010011,  0, "ISDB-S"},
	{SYS_ISDBT, 0x62, 0b00010000, 70, "ISDB-T"},
	{SYS_ISDBT, 0x61, 0b00010010, 71, "ISDB-T"},
};

struct {
	u32 bits;
	char *str;
} pt3_lnb[] = {
	{0b1100,  "0V"},
	{0b1101, "11V"},
	{0b1111, "15V"},
};

enum pt3_ts_pin_mode {
	PT3_TS_PIN_MODE_NORMAL,
	PT3_TS_PIN_MODE_LOW,
	PT3_TS_PIN_MODE_HIGH,
};

struct pt3_ts_pins_mode {
	enum pt3_ts_pin_mode clock_data, byte, valid;
};

#endif

