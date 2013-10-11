obj-$(CONFIG_PT3_DVB) += pt3_dvb.o
ccflags-y += -Idrivers/media/dvb-core/ -Idrivers/media/dvb-frontends/

TARGET := pt3_dvb.ko
VERBOSITY = 0
EXTRA_CFLAGS += -Wformat=2 -Wall -Werror -Idrivers/media/dvb-core -Idrivers/media/dvb-frontends
KVER ?= `uname -r`

KBUILD = /lib/modules/$(KVER)/build
INSTALL_DIR = /lib/modules/$(KVER)/kernel/drivers/video

all: ${TARGET}

pt3_dvb.ko: pt3.c pt3.h pt3_dma.c pt3_bus.c pt3_qm.c pt3_mx.c pt3_i2c.c pt3_tc.c pt3s.c pt3t.c
	make -C $(KBUILD) M=`pwd` V=$(VERBOSITY) modules

clean:
	make -C $(KBUILD) M=`pwd` V=$(VERBOSITY) clean

obj-m := pt3_dvb.o

pt3_dvb-objs := pt3.o

clean-files := *.o *.ko *.mod.[co] *~

uninstall:
	rm -vf $(INSTALL_DIR)/$(TARGET)*

dkms: $(TARGET)

install: uninstall dkms
	install -d $(INSTALL_DIR)
	install -m 644 $(TARGET) $(INSTALL_DIR)
	depmod -a

install_compress: install
	. $(KBUILD)/.config ; \
	if [ $$CONFIG_DECOMPRESS_XZ = "y" ] ; then \
		xz   -9e $(INSTALL_DIR)/$(TARGET); \
	elif [ $$CONFIG_DECOMPRESS_BZIP2 = "y" ] ; then \
		bzip2 -9 $(INSTALL_DIR)/$(TARGET); \
	elif [ $$CONFIG_DECOMPRESS_GZIP = "y" ] ; then \
		gzip  -9 $(INSTALL_DIR)/$(TARGET); \
	fi
	depmod -a

