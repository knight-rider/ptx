/*******************************************************************************
   Earthsoft PT3 Linux driver

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

#include "version.h"

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/version.h>
#include <linux/mutex.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(3,4,0)
 #include <asm/system.h>
#endif
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>

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

#include	"pt3_com.h"
#include	"pt3_ioctl.h"
#include	"pt3_pci.h"
#include	"pt3_bus.h"
#include	"pt3_i2c.h"
#include	"pt3_tc.h"
#include	"pt3_qm.h"
#include	"pt3_mx.h"
#include	"pt3_dma.h"

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

// These identify the driver base version and may not be removed.
static char version[] __devinitdata = DRV_NAME " " DRV_VERSION " " DRV_RELDATE "\n";

MODULE_AUTHOR("anyone");
MODULE_DESCRIPTION("PCI Earthsoft PT3 driver");
MODULE_LICENSE("GPL");

int debug = 0;		// 1 normal messages, 0 quiet .. 7 verbose
static int lnb = 0;	// LNB OFF:0 +11V:1 +15V:2

module_param(debug, int, S_IRUGO | S_IWUSR);
module_param(lnb, int, 0);
MODULE_PARM_DESC(debug, "debug level (0-7)");
MODULE_PARM_DESC(lnb, "LNB level (0:OFF 1:+11V 2:+15V)");

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
#define DEV_NAME	"pt3"
#define MAX_PCI_DEVICE 128		// 最大64枚

typedef struct {
	__u8		ptn;
	__u8 		regs;
	__u8		fpga;
} PT3_VERSION;

typedef struct {
	__u8		dma_descriptor_page_size;
	__u8		can_transport_ts;
} PT3_SYSTEM;

typedef struct {
	int tuner_no;
	PT3_TC *tc_s;
	PT3_TC *tc_t;
	PT3_QM *qm;
	PT3_MX *mx;
} PT3_TUNER;

typedef struct _pt3_adapter pt3_adapter;

typedef struct {
	int bars;
	__u8 __iomem* hw_addr[2];
	struct mutex	lock ;
	dev_t		dev ;
	int		card_number;
	__u32		base_minor ;
	struct	cdev	cdev[PT3_NR_ADAPS];
	PT3_VERSION	version;
	PT3_SYSTEM	system;
	PT3_I2C	*i2c;
	PT3_TUNER	tuner[MAX_TUNER];
	pt3_adapter	*adapter[PT3_NR_ADAPS];
} pt3_board;

struct _pt3_adapter {
	__u32		valid ;
	__u32		minor;
	PT3_TUNER	*tuner;
	int		type ;
	struct mutex	lock ;
	pt3_board	*pt3 ;
	PT3_I2C	*i2c;
	PT3_DMA	*dma;
};

static int real_adapter[PT3_NR_ADAPS] = {0, 1, 2, 3};
static int adapter_type[PT3_NR_ADAPS] = {PT3_ISDB_S, PT3_ISDB_S, PT3_ISDB_T, PT3_ISDB_T};

static	pt3_board	*device[MAX_PCI_DEVICE];
static struct class	*pt3video_class;

static int
check_fpga_version(pt3_board *pt3)
{
	__u32 val = readl(pt3->hw_addr[0] + REGS_VERSION);

	pt3->version.ptn  = ((val >> 24) & 0xFF);
	pt3->version.regs = ((val >> 16) & 0xFF);
	pt3->version.fpga = ((val >>  8) & 0xFF);
	if (pt3->version.ptn != 3) {
		PT3_PRINTK(0, KERN_ERR, "Not a PT3\n");
		return -1;
	}
	PT3_PRINTK(7, KERN_INFO, "PT%d found\n", pt3->version.ptn);

	if (pt3->version.fpga != 0x04) {
		PT3_PRINTK(0, KERN_ERR, "FPGA version 0x%x is not supported\n",
				pt3->version.fpga);
		return -1;
	}

	val = readl(pt3->hw_addr[0] + REGS_SYSTEM_R);
	pt3->system.can_transport_ts = ((val >> 5) & 0x01);
	pt3->system.dma_descriptor_page_size = (val & 0x1F);

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
set_id_s(PT3_TUNER *tuner, __u32 id)
{
	return pt3_tc_write_id_s(tuner->tc_s, NULL, (__u16)id);
}

static STATUS
get_id_s(PT3_TUNER *tuner, __u32 *id)
{
	STATUS status;
	__u16 short_id;

	if (unlikely(id == NULL))
		return STATUS_INVALID_PARAM_ERROR;
	
	if (!(status = pt3_tc_read_id_s(tuner->tc_s, NULL, &short_id)))
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

static __u32 LNB_SETTINGS[] = {
	0b1100,	// 0v
	0b1101,	// 12v
	0b1111,	// 15v
};

static STATUS
set_lnb(pt3_board *pt3, int lnb)
{
	if (unlikely(lnb < 0 || 2 < lnb))
		return STATUS_INVALID_PARAM_ERROR;
	writel(LNB_SETTINGS[lnb], pt3->hw_addr[0] + REGS_SYSTEM_W);
	return STATUS_OK;
}

static STATUS
set_frequency(int isdb, PT3_TUNER *tuner, __u32 channel, __s32 offset)
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
tuner_power_on(pt3_board *pt3, PT3_BUS *bus)
{
	STATUS status;
	int i, j;
	PT3_TS_PINS_MODE pins;

	PT3_TUNER *tuner;

	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &pt3->tuner[i];
		status = pt3_tc_init_s(tuner->tc_s, NULL);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "tc_init_s[%d] status=0x%x\n", i, status);
	}
	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &pt3->tuner[i];
		status = pt3_tc_init_t(tuner->tc_t, NULL);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "tc_init_t[%d] status=0x%x\n", i, status);
	}

	tuner = &pt3->tuner[1];
	status = pt3_tc_set_powers(tuner->tc_t, NULL, 1, 0);
	if (status) {
		PT3_PRINTK(7, KERN_DEBUG, "fail set powers.\n");
		goto last;
	}

	pins.clock_data = PT3_TS_PIN_MODE_NORMAL;
	pins.byte = PT3_TS_PIN_MODE_NORMAL;
	pins.valid = PT3_TS_PIN_MODE_NORMAL;

	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &pt3->tuner[i];
		status = pt3_tc_set_ts_pins_mode_s(tuner->tc_s, NULL, &pins);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "fail set ts pins mode s [%d] status=0x%x\n", i, status);
	}
	for (i = 0; i < MAX_TUNER; i++) {
		tuner = &pt3->tuner[i];
		status = pt3_tc_set_ts_pins_mode_t(tuner->tc_t, NULL, &pins);
		if (status)
			PT3_PRINTK(1, KERN_INFO, "fail set ts pins mode t [%d] status=0x%x\n", i, status);
	}

	schedule_timeout_interruptible(msecs_to_jiffies(1));	

	for (i = 0; i < MAX_TUNER; i++) {
		for (j = 0; j < 10; j++) {
			if (j != 0)
				PT3_PRINTK(0, KERN_INFO, "retry init_tuner\n");
			status = init_tuner(pt3->i2c, &pt3->tuner[i]);
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
		pt3_i2c_copy(pt3->i2c, bus);

	bus->inst_addr = PT3_BUS_INST_ADDR1;
	if ((status = pt3_i2c_run(pt3->i2c, bus, NULL, 0))) {
		PT3_PRINTK(7, KERN_INFO, "failed inst_addr=0x%x status=0x%x\n",
				PT3_BUS_INST_ADDR1, status);
		goto last;
	}

	tuner = &pt3->tuner[1];
	if ((status = pt3_tc_set_powers(tuner->tc_t, NULL, 1, 1))) {
		 PT3_PRINTK(7, KERN_INFO, "fail tc_set_powers,\n");
		goto last;
	}

last:
	return status;
}

static STATUS
init_all_tuner(pt3_board *pt3)
{
	STATUS status;
	int i, j, channel;
	PT3_I2C *i2c = pt3->i2c;
	PT3_BUS *bus = create_pt3_bus();

	if (bus == NULL)
		return STATUS_OUT_OF_MEMORY_ERROR;

	pt3_bus_end(bus);
	bus->inst_addr = PT3_BUS_INST_ADDR0;

	if (!pt3_i2c_is_clean(i2c)) {
		PT3_PRINTK(0, KERN_INFO, "cleanup I2C bus.\n");
		if ((status = pt3_i2c_run(i2c, bus, NULL, 0)))
			goto last;
		schedule_timeout_interruptible(msecs_to_jiffies(10));
	}

	if ((status = tuner_power_on(pt3, bus)))
		goto last;
	PT3_PRINTK(7, KERN_DEBUG, "tuner_power_on\n");
	
	for (i = 0; i < MAX_TUNER; i++) {
		for (j = 0; j < PT3_ISDB_MAX; j++) {
			if (j == PT3_ISDB_S)
				channel = 0;
			else
				channel = (i == 0) ? 70 : 71;
			if ((status = set_tuner_sleep(j, &pt3->tuner[i], 0)))
				goto last;
			if ((status = set_frequency(j, &pt3->tuner[i], channel, 0)))
				PT3_PRINTK(0, KERN_DEBUG, "fail set_frequency. status=0x%x\n", status);
			if ((status = set_tuner_sleep(j, &pt3->tuner[i], 1)))
				goto last;
		}
	}
last:
	free_pt3_bus(bus);
	return status;
}

static STATUS
get_cn_agc(pt3_adapter *channel, __u32 *cn, __u32 *curr_agc, __u32 *max_agc)
{
	STATUS status;
	PT3_TUNER *tuner = channel->tuner;
	__u8 byte_agc;

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
SetChannel(pt3_adapter *channel, FREQUENCY *freq)
{
	TMCC_S tmcc_s;
	TMCC_T tmcc_t;
	__u32 i, tsid;

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
	pt3_adapter *channel;

	for (lp = 0; lp < MAX_PCI_DEVICE; lp++) {
		if (device[lp] == NULL) {
			PT3_PRINTK(1, KERN_DEBUG, "device does not exist\n");
			return -EIO;
		}

		if (MAJOR(device[lp]->dev) == major &&
			device[lp]->base_minor <= minor &&
			device[lp]->base_minor + PT3_NR_ADAPS > minor) {

			mutex_lock(&device[lp]->lock);
			for (lp2 = 0; lp2 < PT3_NR_ADAPS; lp2++) {
				channel = device[lp]->adapter[lp2];
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
	pt3_adapter *channel = file->private_data;

	mutex_lock(&channel->pt3->lock);
	channel->valid = 0;
	pt3_dma_set_enabled(channel->dma, 0);
	mutex_unlock(&channel->pt3->lock);

	if (debug > 0)
		PT3_PRINTK(0, KERN_INFO, "(%d:%d) error count %d\n",
				imajor(inode), iminor(inode),
				pt3_dma_get_ts_error_packet_count(channel->dma));
	set_tuner_sleep(channel->type, channel->tuner, 1);
	schedule_timeout_interruptible(msecs_to_jiffies(50));

	return 0;
}

static int dma_look_ready[PT3_NR_ADAPS] = {1, 1, 1, 1};
static ssize_t
pt3_read(struct file *file, char __user *buf, size_t cnt, loff_t * ppos)
{
	size_t rcnt;
	pt3_adapter *channel;

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
count_used_bs_tuners(pt3_board *device)
{
	int count, i;
	count = 0;

	for (i = 0; i < PT3_NR_ADAPS; i++) {
		if (device && device->adapter[i] &&
			device->adapter[i]->type == PT3_ISDB_S &&
			device->adapter[i]->valid)
			count++;
	}

	PT3_PRINTK(1, KERN_INFO, "used bs tuners on %p = %d\n", device, count);

	return count;
}

static long
pt3_do_ioctl(struct file  *file, unsigned int cmd, unsigned long arg0)
{
	pt3_adapter *channel;
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
		count = count_used_bs_tuners(channel->pt3);
		if (count <= 1) {
			lnb_usr = (int)arg0;
			lnb_eff = lnb_usr ? lnb_usr : lnb;
			set_lnb(channel->pt3, lnb_eff);
			PT3_PRINTK(1, KERN_INFO, "LNB on %s\n", voltage[lnb_eff]);
		}
		return 0;
	case LNB_DISABLE:
		count = count_used_bs_tuners(channel->pt3);
		if (count <= 1) {
			set_lnb(channel->pt3, 0);
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
		pt3_dma_set_test_mode(channel->dma, 1, (__u16)status, 0, 0);
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

static void __devexit
pt3_pci_remove(struct pci_dev *pdev)
{
	__u32		lp;
	PT3_TUNER	*tuner;
	pt3_adapter	*channel;
	pt3_board	*pt3 = (pt3_board *)pci_get_drvdata(pdev);

	if(pt3){
		for (lp = 0; lp < PT3_NR_ADAPS; lp++) {
			channel = pt3->adapter[lp];
			if (channel->dma->enabled)
				pt3_dma_set_enabled(channel->dma, 0);
			set_tuner_sleep(channel->type, channel->tuner, 1);
		}
		set_lnb(pt3, 0);
		for (lp = 0; lp < MAX_TUNER; lp++) {
			tuner = &pt3->tuner[lp];

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
		for (lp = 0; lp < PT3_NR_ADAPS; lp++) {
			if (pt3->adapter[lp] != NULL) {
				cdev_del(&pt3->cdev[lp]);
				if (pt3->adapter[lp]->dma != NULL)
					free_pt3_dma(pdev, pt3->adapter[lp]->dma);
				kfree(pt3->adapter[lp]);
			}
			device_destroy(pt3video_class,
						MKDEV(MAJOR(pt3->dev), (MINOR(pt3->dev) + lp)));
		}
		pt3_i2c_reset(pt3->i2c);
		free_pt3_i2c(pt3->i2c);

		unregister_chrdev_region(pt3->dev, PT3_NR_ADAPS);
		if (pt3->hw_addr[0])
			iounmap(pt3->hw_addr[0]);
		if (pt3->hw_addr[1])
			iounmap(pt3->hw_addr[1]);
		pci_release_selected_regions(pdev, pt3->bars);
		device[pt3->card_number] = NULL;
		kfree(pt3);
		PT3_PRINTK(0, KERN_DEBUG, "free PT3 DEVICE.\n");
	}
	pci_disable_device(pdev);
	pci_set_drvdata(pdev, NULL);
}

static int __devinit
pt3_pci_probe (struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int		rc ;
	int		lp ;
	int		minor ;
	int		bars;
	u32		class_revision ;
	pt3_board	*pt3 ;
	PT3_TUNER	*tuner;
	pt3_adapter	*channel;

	bars = pci_select_bars(pdev, IORESOURCE_MEM);
	if ((rc = pci_enable_device(pdev)))
		return rc;

	if ((rc = pci_request_selected_regions(pdev, bars, DRV_NAME)))
		goto out_err_pci;

	pci_set_master(pdev);
	PT3_PRINTK(0, KERN_INFO, "Bus Mastering Enabled.\n");
	if ((rc = pci_save_state(pdev)))
		goto out_err_reg;

	pci_read_config_dword(pdev, PCI_CLASS_REVISION, &class_revision);
	if ((class_revision & 0xFF) != 1) {
		PT3_PRINTK(0, KERN_ERR, "Revision %x is not supported\n",
				(class_revision & 0xFF));
		goto out_err_reg;
	}
	PT3_PRINTK(7, KERN_DEBUG, "Revision 0x%x passed\n", class_revision & 0xff);

	pt3 = kzalloc(sizeof(pt3_board), GFP_KERNEL);
	if(!pt3){
		PT3_PRINTK(0, KERN_ERR, "out of memory!\n");
		goto out_err_reg;
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate pt3_board.\n");

	// PCIアドレスをマップする
	pt3->bars = bars;
	pt3->hw_addr[0] = pci_ioremap_bar(pdev, 0);
	if (!pt3->hw_addr[0])
		goto out_err_fpga;
	pt3->hw_addr[1] = pci_ioremap_bar(pdev, 2);
	if (!pt3->hw_addr[1])
		goto out_err_fpga;

	rc = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!rc) {
		rc = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
	} else {
		PT3_PRINTK(0, KERN_ERR, "DMA MASK ERROR\n");
		goto out_err_fpga;
	}

	if(check_fpga_version(pt3)){
		goto out_err_fpga;
	}
	mutex_init(&pt3->lock);
	pt3->i2c = create_pt3_i2c(pt3->hw_addr);
	if (pt3->i2c == NULL) {
		PT3_PRINTK(0, KERN_ERR, "cannot allocate i2c.\n");
		goto out_err_fpga;
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate PT3_I2C.\n");

	set_lnb(pt3, 0);
	// Tuner
	for (lp = 0; lp < MAX_TUNER; lp++) {
		tuner = &pt3->tuner[lp];
		tuner->tuner_no = lp;
		tuner->tc_s = create_pt3_tc(pt3->i2c, pt3_tc_address(0, PT3_ISDB_S, lp), pt3_qm_address(lp));
		tuner->qm   = create_pt3_qm(pt3->i2c, tuner->tc_s);
		tuner->tc_t = create_pt3_tc(pt3->i2c, pt3_tc_address(0, PT3_ISDB_T, lp), pt3_mx_address(lp));
		tuner->mx   = create_pt3_mx(pt3->i2c, tuner->tc_t);
	}
	PT3_PRINTK(7, KERN_DEBUG, "Allocate tuners.\n");

	rc = init_all_tuner(pt3);
	if (rc) {
		PT3_PRINTK(0, KERN_ERR, "fail init_all_tuner. 0x%x\n", rc);
		goto out_err_i2c;
	}

	for(lp = 0 ; lp < MAX_PCI_DEVICE ; lp++){
		PT3_PRINTK(0, KERN_INFO, "device[%d]=%p\n", lp, device[lp]);
		if(device[lp] == NULL){
			device[lp] = pt3 ;
			pt3->card_number = lp;
			break ;
		}
	}

	rc =alloc_chrdev_region(&pt3->dev, 0, PT3_NR_ADAPS, DEV_NAME);
	if (rc < 0)
		goto out_err_i2c;
	minor = MINOR(pt3->dev) ;
	pt3->base_minor = minor ;
	for (lp = 0; lp < PT3_NR_ADAPS; lp++) {
		cdev_init(&pt3->cdev[lp], &pt3_fops);
		pt3->cdev[lp].owner = THIS_MODULE;
		rc = cdev_add(&pt3->cdev[lp],
			MKDEV(MAJOR(pt3->dev), (MINOR(pt3->dev) + lp)), 1);
		if (rc < 0) {
			PT3_PRINTK(0, KERN_ERR, "fail cdev_add.\n");
		}

		channel = kzalloc(sizeof(pt3_adapter), GFP_KERNEL);
		if (channel == NULL) {
			PT3_PRINTK(0, KERN_ERR, "out of memory!\n");
			goto out_err_dma;
		}

		channel->dma = create_pt3_dma(pdev, pt3->i2c, real_adapter[lp]);
		if (channel->dma == NULL) {
			PT3_PRINTK(0, KERN_ERR, "fail create dma.\n");
			kfree(channel);
			goto out_err_dma;
		}

		mutex_init(&channel->lock);
		channel->minor = MINOR(pt3->dev) + lp;
		channel->tuner = &pt3->tuner[real_adapter[lp] & 1];
		channel->type  = adapter_type[lp];
		channel->pt3   = pt3;
		channel->i2c   = pt3->i2c;

		pt3->adapter[lp] = channel;

		PT3_PRINTK(0, KERN_INFO, "card_number=%d channel=%d\n",
					pt3->card_number, real_adapter[lp]);
		device_create(pt3video_class,
				NULL,
				MKDEV(MAJOR(pt3->dev), (MINOR(pt3->dev) + lp)),
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
				NULL,
#endif
				"%s%c%u",
				DEV_NAME,
				(channel->type == PT3_ISDB_S) ? 's': 't',
				MINOR(pt3->dev) + (lp&1) + (pt3->card_number*PT3_NR_ADAPS>>1));
	}

	pci_set_drvdata(pdev, pt3);
	return 0;

out_err_dma:
	for (lp = 0; lp < PT3_NR_ADAPS; lp++) {
		if (pt3->adapter[lp] != NULL) {
			if (pt3->adapter[lp]->dma != NULL)
				free_pt3_dma(pdev, pt3->adapter[lp]->dma);
			kfree(pt3->adapter[lp]);
			device_destroy(pt3video_class,
					MKDEV(MAJOR(pt3->dev), (MINOR(pt3->dev) + lp)));
		}
	}
out_err_i2c:
	for (lp = 0; lp < MAX_TUNER; lp++) {
		tuner = &pt3->tuner[lp];
		free_pt3_tc(tuner->tc_s);
		free_pt3_qm(tuner->qm);
		free_pt3_tc(tuner->tc_t);
		free_pt3_mx(tuner->mx);
	}
	free_pt3_i2c(pt3->i2c);
out_err_fpga:
	if (pt3->hw_addr[0])
		iounmap(pt3->hw_addr[0]);
	if (pt3->hw_addr[1])
		iounmap(pt3->hw_addr[1]);
	kfree(pt3);
out_err_reg:
	pci_release_selected_regions(pdev, bars);
out_err_pci:
	pci_disable_device(pdev);
	return -EIO;
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
	.probe		= pt3_pci_probe,
	.remove	= __devexit_p(pt3_pci_remove),
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

