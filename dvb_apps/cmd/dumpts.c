/*
 * read from stdin, find each packet, print its header info to stdout.
 * example: ./dumpts < in-file | lv
 */
#include <stdio.h>

int main(int argc, char **argv)
{
	int c, i, r;
	unsigned long pos = 0;
	unsigned char buf[7];
	unsigned char dummy[188 - 1 - sizeof(buf)];

	unsigned short pid;
	
	for(;;) {
		i = 0;
		while ((c = getchar()) != -1 && c != '\x47') {
			if (!(i++ % 10)) printf(".");
		}
		if (i) printf("\n");
		pos += i;

		if (c != '\x47') return (1);

		r = fread(buf, 1, sizeof(buf), stdin);
		if (r < sizeof(buf))
			 return (0);
		pos += r;

		pid = ((buf[0] & 0x1f) << 8) + buf[1];
		printf("%04hX %1X %s %s%s%s%s%s %02hX %02hX %02hX %02hX\n",
				pid,
				buf[2] & 0xf,
				(buf[2] & 0x80)? "CA" : "--",
				(buf[0]&0x80)? "TSEI " : "",
				(buf[0]&0x40)? "PUSI " : "",
				(buf[0]&0x20)? "PRIO " : "",
				(buf[2]&0x20)? "ADAP " : "",
				(buf[2]&0x10)? "PAYLOAD" : "",
				buf[3], buf[4], buf[5], buf[6]);
		r = fread(dummy, 1, sizeof(dummy), stdin);
		if (r < sizeof(dummy))
			 return(0);
		pos += r;
	}
	return 0;
}
