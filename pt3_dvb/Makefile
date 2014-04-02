DIR1 = drivers/media/pci/pt3/
SRC1 = pt3_pci.c pt3_common.h pt3_dma.c pt3_dma.h pt3_i2c.c pt3_i2c.h
DIR2 = drivers/media/dvb-frontends/
SRC2 = tc90522.c tc90522.h
DIR3 = drivers/media/tuners/
SRC3 = mxl301rf.c mxl301rf.h qm1d1c0042.c qm1d1c0042.h

TARGET := $(shell eval `sed -e "s/\[0\]//" $(PWD)/dkms.conf`; echo $${PACKAGE_NAME}).ko
KVER ?= `uname -r`
KBUILD := /lib/modules/$(KVER)/build
INSTALL_DIR := /lib/modules/$(KVER)/$(shell eval `sed -e "s/\[0\]//" $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION})
EXTRA_CFLAGS += -Wformat=2 -Wall -Werror -Idrivers/media/dvb-core -Idrivers/media/dvb-frontends \
	-I$(PWD)/$(DIR1) -I$(PWD)/$(DIR2) -I$(PWD)/$(DIR3) \
	-DCONFIG_DVB_TC90522 -DCONFIG_MEDIA_TUNER_QM1D1C0042 -DCONFIG_MEDIA_TUNER_MXL301RF

all: ${TARGET}

$(basename $(TARGET)).ko: \
	$(addprefix $(DIR1), $(SRC1)) \
	$(addprefix $(DIR2), $(SRC2)) \
	$(addprefix $(DIR3), $(SRC3))
	make -C $(KBUILD) M=`pwd` modules

$(basename $(TARGET))-objs := \
	$(addprefix $(DIR1), $(filter %.o, $(SRC1:.c=.o))) \
	$(addprefix $(DIR2), $(filter %.o, $(SRC2:.c=.o))) \
	$(addprefix $(DIR3), $(filter %.o, $(SRC3:.c=.o)))

obj-m := $(basename $(TARGET)).o

clean:
	make -C $(KBUILD) M=`pwd` clean

clean-files := *.o *.ko *.mod.[co] *~
clean-files += \
	$(addprefix $(DIR1), $(clean-files)) \
	$(addprefix $(DIR2), $(clean-files)) \
	$(addprefix $(DIR3), $(clean-files))

uninstall:
	rm -vf $(INSTALL_DIR)/$(TARGET)*

dkms: all

install: uninstall dkms
	install -d $(INSTALL_DIR)
	install -m 644 $(TARGET) $(INSTALL_DIR)
	depmod -a $(KVER)

install_compress: install
	. $(KBUILD)/.config ; \
	cd $(INSTALL_DIR); \
	if [ $$CONFIG_DECOMPRESS_XZ = "y" ] ; then \
		xz   -9e $(TARGET); \
	elif [ $$CONFIG_DECOMPRESS_BZIP2 = "y" ] ; then \
		bzip2 -9 $(TARGET); \
	elif [ $$CONFIG_DECOMPRESS_GZIP = "y" ] ; then \
		gzip  -9 $(TARGET); \
	fi
	depmod -a $(KVER)

