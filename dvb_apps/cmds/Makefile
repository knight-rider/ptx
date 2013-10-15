PROGRAMS = nitdump dumpts dumpts2 s2scan ptsdump restamp dumpeid tune
CFLAGS += -Wformat=2 -Wall

all: $(PROGRAMS)

PSISOURCE = section.c pat.c pmt.c eit.c nit.c sdt.c tot.c

nitdump: nitdump.c $(PSISOURCE) nitscan.h

s2scan: s2scan.c arib_b24_str.c

dumpeid: dumpeid.c section.c arib_b24_str.c nitscan.h

clean:
	rm -f *.o *~ $(PROGRAMS)
