# 通常は弄らない。dkms.conf 或いは下位 Makefile を編集して下さい。

KVER ?= $(shell uname -r)
KBUILD := /lib/modules/$(KVER)/build
SRCS := $(shell find $(PWD)/drivers -name "*.c")
HDRS := $(shell find $(PWD)/drivers -name "*.h")
IDIR := $(sort $(dir $(HDRS))) drivers/media/dvb-core drivers/media/dvb-frontends drivers/media/tuners
ldflags-y += -s
ccflags-y += -O3 -Os -Wformat=2 -Wall -Werror $(addprefix -I, $(IDIR))
MODS := $(shell eval source $(PWD)/dkms.conf; echo $${BUILT_MODULE_NAME[*]})
DIRS := $(addprefix /lib/modules/$(KVER), $(shell eval source $(PWD)/dkms.conf; echo $${DEST_MODULE_LOCATION[*]}))
DIR0 := $(firstword $(DIRS))
DSTS := $(join $(DIRS), $(addprefix /, $(addsuffix *, $(MODS))))
TGTS := $(addsuffix .ko, $(MODS))
obj-m := $(TGTS:.ko=.o)

OBJS := $(join $(shell eval source $(PWD)/dkms.conf; echo $${DEST_MODULE_LOCATION[*]} | sed "s|/kernel/||g"), $(addprefix /, $(addsuffix .o, $(MODS))))
$(shell echo $(join $(TGTS:.ko=-objs:=), $(OBJS)) | sed "s/ /\n/g" > $(PWD)/m~)
$(foreach OBJ, $(OBJS), $(shell egrep -s ccflags-y $(PWD)/$(dir $(OBJ))Makefile >> $(PWD)/m~))
$(foreach OBJ, $(OBJS), $(shell echo $(patsubst %.o, $(dir $(OBJ))%.o, $(shell egrep -s $(notdir $(OBJ:.o=-objs)) $(PWD)/$(dir $(OBJ))Makefile)) >> $(PWD)/m~))
include $(PWD)/m~

all: $(TGTS)
	make -C $(KBUILD) M=`pwd`
$(TGTS): $(SRCS) $(HDRS)

debug:
	make "ccflags-y += -DDEBUG $(ccflags-y)"

clean-files := *.o *.ko *.mod.[co] *~
clean-files += $(foreach DIR, $(shell find $(PWD) -type d), $(addprefix $(DIR)/, $(clean-files)))
clean:
	make -C $(KBUILD) M=`pwd` clean
	@$(RM) -v $(clean-files)

check: clean
	$(KBUILD)/scripts/checkpatch.pl --no-tree --show-types --ignore GCC_BINARY_CONSTANT --max-line-length=200 -f \
		`find \( -iname "*c" -o -iname "*h" \)` | tee warns~
	if [ -f /usr/local/smatch/smatch ] ; then \
		make CHECK="/usr/local/smatch/smatch --full-path" CC=/usr/local/smatch/cgcc |& tee -a warns~; \
	fi

uninstall:
	rm -vfr $(DSTS)

install: uninstall all
	install -d $(DIR0)
	install -m 644 $(TGTS) $(DIR0)
	depmod -a $(KVER)

install_compress: install
	. $(KBUILD)/.config ; \
	cd $(DIR0); \
	if [ $$CONFIG_DECOMPRESS_XZ = "y" ]; then \
		xz -9e $(TGTS); \
	elif [ $$CONFIG_DECOMPRESS_BZIP2 = "y" ]; then \
		bzip2 -9 $(TGTS); \
	elif [ $$CONFIG_DECOMPRESS_GZIP = "y" ]; then \
		gzip -9 $(TGTS); \
	fi
	depmod -a $(KVER)

