#ifndef	__PT3_H__
#define	__PT3_H__

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

#define PT3_SHIFT_MASK(val, shift, mask) (((val) >> (shift)) & (((__u64)1<<(mask))-1))

// register idx
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

#define PT3_MS(x)		msecs_to_jiffies(x)
#define PT3_WAIT_MS_INT(x)	schedule_timeout_interruptible(PT3_MS(x))
#define PT3_WAIT_MS_UNINT(x)	schedule_timeout_uninterruptible(PT3_MS(x))

#define PT3_PRINTK(level, fmt, args...)\
	{if (debug + 48 >= level[1]) printk(DRV_NAME " " level " " fmt, ##args);}

static int lnb = 2;	// LNB OFF:0 +11V:1 +15V:2
module_param(lnb, int, 0);
MODULE_PARM_DESC(lnb, "LNB level (0:OFF 1:+11V 2:+15V)");

int debug = 0;		// 1 normal messages, 0 quiet .. 7 verbose
module_param(debug, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(debug, "debug level (0-7)");

MODULE_AUTHOR("Bud R <knightrider @ are.ma>");
MODULE_DESCRIPTION("Earthsoft PT3 DVB Driver");
MODULE_LICENSE("GPL");

typedef struct file FILE;
typedef struct mutex MUTEX;
typedef struct pci_dev PCI_DEV;
typedef struct pci_device_id PCI_DEV_ID;
typedef struct task_struct TASK_STRUCT;
typedef struct dvb_adapter DVB_ADAPTER;
typedef struct dvb_demux DVB_DEMUX;
typedef struct dvb_demux_feed DVB_DEMUX_FEED;
typedef struct dmxdev DMXDEV;
typedef struct dvb_frontend DVB_FRONTEND;
typedef struct dvb_frontend_ops DVB_FRONTEND_OPS;

static PCI_DEV_ID pt3_id_table[] = {
	{ PCI_DEVICE(ID_VEN_ALTERA, ID_DEV_PT3) },
	{ },
};
MODULE_DEVICE_TABLE(pci, pt3_id_table);

typedef struct {
	dma_addr_t addr;
	__u8 *data;
	__u32 size, data_pos;
} PT3_DMA_PAGE;

typedef struct {
	__u8 __iomem *reg[2];
	MUTEX lock;
} PT3_I2C;

typedef enum {
	LAYER_INDEX_L = 0,
	LAYER_INDEX_H,

	LAYER_INDEX_A = 0,
	LAYER_INDEX_B,
	LAYER_INDEX_C
} LAYER_INDEX;

typedef enum {
	LAYER_COUNT_S = LAYER_INDEX_H + 1,
	LAYER_COUNT_T = LAYER_INDEX_C + 1,
} LAYER_COUNT;

// Transmission and Multiplexing Configuration Control

typedef struct {
	__u32 indicator;
	__u32 mode[4];
	__u32 slot[4];
	__u32 id[8];
	__u32 emergency;
	__u32 uplink;
	__u32 extflag;
} TMCC_S;

typedef struct {
	__u32 system;
	__u32 indicator;
	__u32 emergency;
	__u32 partial;
	__u32 mode[LAYER_COUNT_T];
	__u32 rate[LAYER_COUNT_T];
	__u32 interleave[LAYER_COUNT_T];
	__u32 segment[LAYER_COUNT_T];
} TMCC_T;

typedef struct _PT3_ADAPTER PT3_ADAPTER;

typedef struct {
	PT3_ADAPTER *adap;
	bool enabled;
	__u32 ts_pos, ts_count, desc_count;
	PT3_DMA_PAGE *ts_info, *desc_info;
	MUTEX lock;
} PT3_DMA;

typedef struct {
	PT3_ADAPTER *adap;
	__u8 reg[32];

	// QM PARAM
	bool standby;
	__u32 wait_time_lpf, wait_time_search_fast, wait_time_search_normal;
	TMCC_S tmcc;
} PT3_QM;

typedef struct {
	MUTEX lock;
	bool reset;
	int lnb;

	// PCI & I2C
	PCI_DEV *pdev;
	void __iomem *reg[2];
	int bars;
	PT3_I2C *i2c;

	PT3_ADAPTER *adap[PT3_NR_ADAPS];
} PT3_BOARD;

struct _PT3_ADAPTER {
	MUTEX lock;
	PT3_BOARD *pt3;

	// tuner & DMA
	int idx, init_ch;
	char *str;
	fe_delivery_system_t type;
	bool in_use, sleep;
	__u32 channel;
	__s32 offset;
	__u8 addr_tc, addr_tuner;
	__u32 freq;
	PT3_QM *qm;
	PT3_DMA *dma;
	TASK_STRUCT *kthread;

	// DVB
	DVB_ADAPTER dvb;
	DVB_DEMUX demux;
	int users;
	DMXDEV dmxdev;
	DVB_FRONTEND *fe;
	int (*orig_voltage)(DVB_FRONTEND *fe, fe_sec_voltage_t voltage);
	int (*orig_sleep  )(DVB_FRONTEND *fe                          );
	int (*orig_init   )(DVB_FRONTEND *fe                          );
	fe_sec_voltage_t voltage;
};

struct {
	fe_delivery_system_t type;
	__u8 addr_tuner, addr_tc;
	int init_ch;
	char *str;
} pt3_config[] = {
	{SYS_ISDBS, 0x63, 0b00010001,  0, "ISDB_S"},
	{SYS_ISDBS, 0x60, 0b00010011,  0, "ISDB_S"},
	{SYS_ISDBT, 0x62, 0b00010000, 70, "ISDB_T"},
	{SYS_ISDBT, 0x61, 0b00010010, 71, "ISDB_T"},
};

struct {
	__u32 bits;
	char *str;
} pt3_lnb[] = {
	{0b1100,  "0V"},
	{0b1101, "11V"},
	{0b1111, "15V"},
};

typedef enum {
	PT3_TS_PIN_MODE_NORMAL,
	PT3_TS_PIN_MODE_LOW,
	PT3_TS_PIN_MODE_HIGH,
} PT3_TS_PIN_MODE;

typedef struct {
	PT3_TS_PIN_MODE clock_data, byte, valid;
} PT3_TS_PINS_MODE;

#endif

