PROGRAMS = nitdump dumpts dumpts2 s2scan ptsdump restamp dumpeid fixpat fixpat2 fixpcr jzap tune tcscan tctune

all: $(PROGRAMS)

PSISOURCE = section.c pat.c pmt.c eit.c nit.c sdt.c tot.c

nitdump: nitdump.c $(PSISOURCE) nitscan.h

s2scan: s2scan.c arib_b24_str.c

tcscan: tcscan.c arib_b24_str.c

dumpeid: dumpeid.c section.c arib_b24_str.c nitscan.h

clean:
	rm -f *.o *~ $(PROGRAMS)
