DEFS := -DCONFIG_DVB_TC90522 -DCONFIG_MEDIA_TUNER_QM1D1C0042 -DCONFIG_MEDIA_TUNER_MXL301RF

SRCS := $(shell cd $(PWD); find -iname "*.c" -o -iname "*.h")
INCS := $(addprefix -I$(PWD)/, $(sort $(dir $(SRCS))))
TARGET := $(shell eval `sed -e "s/\[0\]//" $(PWD)/dkms.conf`; echo $${PACKAGE_NAME}).ko
KVER ?= `uname -r`
KBUILD := /lib/modules/$(KVER)/build
INSTALL_DIR := /lib/modules/$(KVER)/$(shell eval `sed -e "s/\[0\]//" $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION})
EXTRA_CFLAGS += -Wformat=2 -Wall -Werror -Idrivers/media/dvb-core -Idrivers/media/dvb-frontends $(INCS) $(DEFS)

all: ${TARGET}

debug:
	make "EXTRA_CFLAGS += -DDEBUG $(EXTRA_CFLAGS)" -C $(KBUILD) M=`pwd`

$(basename $(TARGET)).ko: $(SRCS)
	make -C $(KBUILD) M=`pwd` modules

$(basename $(TARGET))-objs := $(filter %.o, $(SRCS:.c=.o))

obj-m := $(basename $(TARGET)).o

clean:
	make -C $(KBUILD) M=`pwd` clean

clean-files := *.o *.ko *.mod.[co] *~
clean-files += $(foreach DIR, (sort $(dir $(SRCS))), $(addprefix $(DIR), $(clean-files)))

uninstall:
	rm -vf $(INSTALL_DIR)/$(TARGET)*

install: uninstall all
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

