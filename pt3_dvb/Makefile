KVER ?= `uname -r`
KBUILD := /lib/modules/$(KVER)/build
INSTALL_DIR := /lib/modules/$(KVER)/$(shell eval `cat $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION[0]})

TGT0 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[0]}).ko
TGT1 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[1]}).ko
TGT2 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[2]}).ko
TGT3 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[3]}).ko
TGT := $(TGT0) $(TGT1) $(TGT2) $(TGT3)

SRC0 := $(shell cd $(PWD); eval `sed -e "s|/kernel/||" dkms.conf`; find $${DEST_MODULE_LOCATION[0]} -iname "*.c")
SRC1 := $(shell cd $(PWD); eval `sed -e "s|/kernel/||" dkms.conf`; find $${DEST_MODULE_LOCATION[1]} -iname "*.c")
SRC2 := $(shell cd $(PWD); eval `sed -e "s|/kernel/||" dkms.conf`; find $${DEST_MODULE_LOCATION[2]} -iname $(basename $(TGT2)).c)
SRC3 := $(shell cd $(PWD); eval `sed -e "s|/kernel/||" dkms.conf`; find $${DEST_MODULE_LOCATION[3]} -iname $(basename $(TGT3)).c)
SRCS := $(SRC0) $(SRC1) $(SRC2) $(SRC3)
HDRS := $(shell cd $(PWD); find -iname "*.h")
INCS := $(addprefix -I$(PWD)/, $(sort $(dir $(HDRS))))
EXTRA_CFLAGS += -Wformat=2 -Wall -Werror -Idrivers/media/dvb-core -Idrivers/media/dvb-frontends $(INCS)

$(basename $(TGT0))-objs := $(filter %.o, $(SRC0:.c=.o))
$(basename $(TGT1))-objs := $(filter %.o, $(SRC1:.c=.o))
$(basename $(TGT2))-objs := $(filter %.o, $(SRC2:.c=.o))
$(basename $(TGT3))-objs := $(filter %.o, $(SRC3:.c=.o))
obj-m := $(basename $(TGT0)).o $(basename $(TGT1)).o $(basename $(TGT2)).o $(basename $(TGT3)).o

all: $(TGT)
	make -C $(KBUILD) M=`pwd`
$(TGT0): $(SRC0) $(HDRS)
$(TGT1): $(SRC1) $(HDRS)
$(TGT2): $(SRC2) $(HDRS)
$(TGT3): $(SRC3) $(HDRS)

debug:
	make "EXTRA_CFLAGS += -DDEBUG $(EXTRA_CFLAGS)"

clean:
	make -C $(KBUILD) M=`pwd` clean

clean-files := *.o *.ko *.mod.[co] *~
clean-files += $(foreach DIR, (sort $(dir $(SRCS) $(HDRS))), $(addprefix $(DIR), $(clean-files)))

uninstall:
	rm -vf $(INSTALL_DIR)/$(TGT)*

install: uninstall all
	install -d $(INSTALL_DIR)
	install -m 644 $(TGT) $(INSTALL_DIR)
	depmod -a $(KVER)

install_compress: install
	. $(KBUILD)/.config ; \
	cd $(INSTALL_DIR); \
	if [ $$CONFIG_DECOMPRESS_XZ = "y" ] ; then \
		xz   -9e $(TGT); \
	elif [ $$CONFIG_DECOMPRESS_BZIP2 = "y" ] ; then \
		bzip2 -9 $(TGT); \
	elif [ $$CONFIG_DECOMPRESS_GZIP = "y" ] ; then \
		gzip  -9 $(TGT); \
	fi
	depmod -a $(KVER)

