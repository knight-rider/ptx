#ifndef	__PT3_H__
#define	__PT3_H__

#define pr_fmt(fmt) KBUILD_MODNAME " " fmt

#include <linux/pci.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include "dvb_demux.h"
#include "dmxdev.h"
#include "dvb_frontend.h"

#define PT3_NR_ADAPS 4
#define PT3_SHIFT_MASK(val, shift, mask) (((val) >> (shift)) & (((u64)1<<(mask))-1))

/* register idx */
#define REG_VERSION	0x00	/*	R	Version		*/
#define REG_BUS		0x04	/*	R	Bus		*/
#define REG_SYSTEM_W	0x08	/*	W	System		*/
#define REG_SYSTEM_R	0x0c	/*	R	System		*/
#define REG_I2C_W	0x10	/*	W	I2C		*/
#define REG_I2C_R	0x14	/*	R	I2C		*/
#define REG_RAM_W	0x18	/*	W	RAM		*/
#define REG_RAM_R	0x1c	/*	R	RAM		*/
#define REG_BASE	0x40	/* + 0x18*idx			*/
#define REG_DMA_DESC_L	0x00	/*	W	DMA		*/
#define REG_DMA_DESC_H	0x04	/*	W	DMA		*/
#define REG_DMA_CTL	0x08	/*	W	DMA		*/
#define REG_TS_CTL	0x0c	/*	W	TS		*/
#define REG_STATUS	0x10	/*	R	DMA/FIFO/TS	*/
#define REG_TS_ERR	0x14	/*	R	TS		*/

static int lnb = 2;	/* used if not set by frontend / the value is invalid */
module_param(lnb, int, 0);
MODULE_PARM_DESC(lnb, "LNB level (0:OFF 1:+11V 2:+15V)");

/* Transmission and Multiplexing Configuration Control */

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

struct pt3_i2c {
	u8 __iomem *reg[2];
	struct mutex lock;
};

struct pt3_dma_page {
	dma_addr_t addr;
	u8 *data;
	u32 size, data_pos;
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
	int *dec;
	struct dvb_adapter dvb;
	struct dvb_demux demux;
	int users;
	struct dmxdev dmxdev;
	struct dvb_frontend *fe;
	int (*orig_voltage)(struct dvb_frontend *fe, fe_sec_voltage_t voltage);
	int (*orig_sleep)  (struct dvb_frontend *fe);
	int (*orig_init)   (struct dvb_frontend *fe);
	fe_sec_voltage_t voltage;
};

enum pt3_ts_pin_mode {
	PT3_TS_PIN_MODE_NORMAL,
	PT3_TS_PIN_MODE_LOW,
	PT3_TS_PIN_MODE_HIGH,
};

struct pt3_ts_pins_mode {
	enum pt3_ts_pin_mode clock_data, byte, valid;
};

#define PT3_BUS_CMD_MAX   4096
#define PT3_BUS_CMD_ADDR0 4096
#define PT3_BUS_CMD_ADDR1 (4096 + 2042)

struct pt3_bus {
	u32 read_addr, cmd_addr, cmd_count, cmd_pos, buf_pos, buf_size;
	u8 cmd_tmp, cmds[PT3_BUS_CMD_MAX], *buf;
};

enum pt3_tc_agc {
	PT3_TC_AGC_AUTO,
	PT3_TC_AGC_MANUAL,
};

enum pt3_dma_mode {
	USE_LFSR = 1 << 16,
	REVERSE  = 1 << 17,
	RESET    = 1 << 18,
};

/* protos */
u8 pt3_bus_data1(struct pt3_bus *bus, u32 index);
void pt3_bus_end(struct pt3_bus *bus);
void pt3_bus_push_read_data(struct pt3_bus *bus, u8 data);
u32 pt3_bus_read(struct pt3_bus *bus, u8 *data, u32 size);
void pt3_bus_sleep(struct pt3_bus *bus, u32 ms);
void pt3_bus_start(struct pt3_bus *bus);
void pt3_bus_stop(struct pt3_bus *bus);
void pt3_bus_write(struct pt3_bus *bus, const u8 *data, u32 size);
ssize_t pt3_dma_copy(struct pt3_dma *dma, struct dvb_demux *demux, loff_t *ppos);
struct pt3_dma *pt3_dma_create(struct pt3_adapter *adap);
void pt3_dma_free(struct pt3_dma *dma);
u32 pt3_dma_get_status(struct pt3_dma *dma);
u32 pt3_dma_get_ts_error_packet_count(struct pt3_dma *dma);
void pt3_dma_set_enabled(struct pt3_dma *dma, bool enabled);
void pt3_dma_set_test_mode(struct pt3_dma *dma, enum pt3_dma_mode mode, u16 initval);
void pt3_i2c_copy(struct pt3_i2c *i2c, struct pt3_bus *bus);
bool pt3_i2c_is_clean(struct pt3_i2c *i2c);
void pt3_i2c_reset(struct pt3_i2c *i2c);
int pt3_i2c_run(struct pt3_i2c *i2c, struct pt3_bus *bus, bool copy);
u32 pt3_tc_index(struct pt3_adapter *adap);
int pt3_tc_init(struct pt3_adapter *adap);
int pt3_tc_read_cn_s(struct pt3_adapter *adap, struct pt3_bus *bus, u32 *cn);
int pt3_tc_read_cndat_t(struct pt3_adapter *adap, struct pt3_bus *bus, u32 *cn);
int pt3_tc_read_id_s(struct pt3_adapter *adap, struct pt3_bus *bus, u16 *id);
int pt3_tc_read_retryov_tmunvld_fulock(struct pt3_adapter *adap, struct pt3_bus *bus, int *retryov, int *tmunvld, int *fulock);
int pt3_tc_read_tmcc_s(struct pt3_adapter *adap, struct pt3_bus *bus, struct tmcc_s *tmcc);
int pt3_tc_read_tmcc_t(struct pt3_adapter *adap, struct pt3_bus *bus, struct tmcc_t *tmcc);
int pt3_tc_read_tuner(struct pt3_adapter *adap, struct pt3_bus *bus, u8 addr, u8 *data);
int pt3_tc_read_tuner_without_addr(struct pt3_adapter *adap, struct pt3_bus *bus, u8 *data);
int pt3_tc_set_agc_s(struct pt3_adapter *adap, enum pt3_tc_agc agc);
int pt3_tc_set_agc_t(struct pt3_adapter *adap, enum pt3_tc_agc agc);
int pt3_tc_set_powers(struct pt3_adapter *adap, struct pt3_bus *bus, bool tuner, bool amp);
int pt3_tc_set_sleep_s(struct pt3_adapter *adap, struct pt3_bus *bus, bool sleep);
int pt3_tc_set_ts_pins_mode(struct pt3_adapter *adap, struct pt3_ts_pins_mode *mode);
u32 pt3_tc_time_diff(struct timeval *st, struct timeval *et);
int pt3_tc_write_id_s(struct pt3_adapter *adap, struct pt3_bus *bus, u16 id);
int pt3_tc_write_sleep_time(struct pt3_adapter *adap, int sleep);
int pt3_tc_write_tuner(struct pt3_adapter *adap, struct pt3_bus *bus, u8 addr, const u8 *data, u32 size);
int pt3_tc_write_tuner_without_addr(struct pt3_adapter *adap, struct pt3_bus *bus, const u8 *data, u32 size);
int pt3_mx_set_frequency(struct pt3_adapter *adap, u32 channel, s32 offset);
int pt3_mx_set_sleep(struct pt3_adapter *adap, bool sleep);
int pt3_qm_set_frequency(struct pt3_qm *qm, u32 channel);
int pt3_qm_set_sleep(struct pt3_qm *qm, bool sleep);
int pt3_qm_tuner_init(struct pt3_i2c *i2c, struct pt3_adapter *adap);
struct dvb_frontend *pt3_fe_s_attach(struct pt3_adapter *adap);
struct dvb_frontend *pt3_fe_t_attach(struct pt3_adapter *adap);

#endif

