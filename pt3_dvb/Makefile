KVER ?= `uname -r`
KBUILD := /lib/modules/$(KVER)/build
DST0 := /lib/modules/$(KVER)/$(shell eval `cat $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION[0]})
DST1 := /lib/modules/$(KVER)/$(shell eval `cat $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION[1]})
DST2 := /lib/modules/$(KVER)/$(shell eval `cat $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION[2]})
DST3 := /lib/modules/$(KVER)/$(shell eval `cat $(PWD)/dkms.conf`; echo $${DEST_MODULE_LOCATION[3]})

TGT0 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[0]})
TGT1 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[1]})
TGT2 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[2]})
TGT3 := $(shell eval `cat $(PWD)/dkms.conf`; echo $${BUILT_MODULE_NAME[3]})
TGT := $(TGT0).ko $(TGT1).ko $(TGT2).ko $(TGT3).ko

SRC0 := $(shell cd $(PWD); find -name $(TGT0).c)
SRC1 := $(shell cd $(PWD); find -name $(TGT1).c)
SRC2 := $(shell cd $(PWD); find -name $(TGT2).c)
SRC3 := $(shell cd $(PWD); find -name $(TGT3).c)
SRCS := $(SRC0) $(SRC1) $(SRC2) $(SRC3)
HDRS := $(shell cd $(PWD); find -name "*.h")
INCS := $(addprefix -I$(PWD)/, $(sort $(dir $(HDRS))))
EXTRA_CFLAGS += -Wformat=2 -Wall -Werror -Idrivers/media/dvb-core -Idrivers/media/dvb-frontends $(INCS)

$(TGT0)-objs := $(filter %.o, $(SRC0:.c=.o))
$(TGT1)-objs := $(filter %.o, $(SRC1:.c=.o))
$(TGT2)-objs := $(filter %.o, $(SRC2:.c=.o))
$(TGT3)-objs := $(filter %.o, $(SRC3:.c=.o))
obj-m := $(TGT0).o $(TGT1).o $(TGT2).o $(TGT3).o

all: $(TGT)
	make -C $(KBUILD) M=`pwd`
$(TGT0).ko: $(SRC0) $(HDRS)
$(TGT1).ko: $(SRC1) $(HDRS)
$(TGT2).ko: $(SRC2) $(HDRS)
$(TGT3).ko: $(SRC3) $(HDRS)

debug:
	make "EXTRA_CFLAGS += -DDEBUG $(EXTRA_CFLAGS)"

clean:
	make -C $(KBUILD) M=`pwd` clean

clean-files := *.o *.ko *.mod.[co] *~
clean-files += $(foreach DIR, (sort $(dir $(SRCS) $(HDRS))), $(addprefix $(DIR), $(clean-files)))

uninstall:
	rm -vfr $(DST0) $(DST1)/$(TGT1)* $(DST2)/$(TGT2)* $(DST3)/$(TGT3)*

install: uninstall all
	install -d $(DST0)
	install -m 644 $(TGT) $(DST0)
	depmod -a $(KVER)

install_compress: install
	. $(KBUILD)/.config ; \
	cd $(DST0); \
	if [ $$CONFIG_DECOMPRESS_XZ = "y" ] ; then \
		xz   -9e $(TGT); \
	elif [ $$CONFIG_DECOMPRESS_BZIP2 = "y" ] ; then \
		bzip2 -9 $(TGT); \
	elif [ $$CONFIG_DECOMPRESS_GZIP = "y" ] ; then \
		gzip  -9 $(TGT); \
	fi
	depmod -a $(KVER)

