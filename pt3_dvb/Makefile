TARGET := pt3_dvb.ko
DIRS = drivers/media/pci/pt3_dvb/
SRCS = pt3.c pt3.h pt3_dma.c pt3_bus.c pt3_i2c.c pt3_tc.c pt3_fe.c

KVER ?= `uname -r`
KBUILD = /lib/modules/$(KVER)/build
INSTALL_DIR = /lib/modules/$(KVER)/kernel/drivers/video
VERBOSITY = 0

EXTRA_CFLAGS += -Wformat=2 -Wall -Werror -DDEBUG -Idrivers/media/dvb-core -Idrivers/media/dvb-frontends

all: ${TARGET}

%.ko: $(addprefix $(DIRS), $(SRCS))
	make -C $(KBUILD) M=`pwd` V=$(VERBOSITY) modules


obj-m := $(basename $(TARGET)).o

$(basename $(TARGET))-objs := $(addprefix $(DIRS), $(filter %.o, $(SRCS:.c=.o)))

clean:
	make -C $(KBUILD) M=`pwd` V=$(VERBOSITY) clean

clean-files := *.o *.ko *.mod.[co] *~
clean-files += $(addprefix $(DIRS), $(clean-files))

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

