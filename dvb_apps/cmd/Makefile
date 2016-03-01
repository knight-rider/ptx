TGT_APP = nitdump dumpts dumpts2 s2scan ptsdump restamp dumpeid tune checksignal fixpat fixpcr jzap
TGT_LIB = libarib24.so
OBJ_LIB = arib_b24_str.o
HEADERS = arib_b24_str.h

PREFIX = /usr/local
DST_HEADER = $(PREFIX)/include
MAJOR = 0
MINOR = 0
REVISION = 1
VER = $(MAJOR).$(MINOR).$(REVISION)
SONAME = $(TGT_LIB).$(MAJOR)
TARGETS = $(TGT_APP) $(TGT_LIB)
CFLAGS += -Wformat=2 -Wall -Werror -lm -fPIC

all: $(TARGETS)

PSISOURCE = section.c pat.c pmt.c eit.c nit.c sdt.c tot.c

nitdump: nitdump.c $(PSISOURCE) nitscan.h

s2scan: s2scan.c arib_b24_str.c

dumpeid: dumpeid.c section.c arib_b24_str.c nitscan.h

$(TGT_LIB): $(OBJ_LIB)
	$(CC) -shared -o $(TGT_LIB) $(OBJ_LIB) -Wl,-soname,$(SONAME)

clean:
	rm -f *.o *~ $(TARGETS)

install-headers:
	mkdir -p $(DST_HEADER)
	install -m644 $(HEADERS) $(DST_HEADER)

install: $(TARGET) install-headers
	install -m755 $(TGT_LIB) $(PREFIX)/lib/$(TGT_LIB).$(VER)
	ln -sf $(PREFIX)/lib/$(TGT_LIB).$(VER) $(PREFIX)/lib/$(TGT_LIB).$(MAJOR)
	ln -sf $(PREFIX)/lib/$(TGT_LIB).$(MAJOR) $(PREFIX)/lib/$(TGT_LIB)
	ldconfig

