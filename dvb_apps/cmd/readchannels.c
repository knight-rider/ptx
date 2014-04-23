#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <getopt.h>
#include <iconv.h>
#include <time.h>

typedef struct _NAMES
{
	char name[255];
	char ontv[255];
} NAMES;

typedef struct _TAG_STATION
{
	char	*name;
	char	*ontv;
	int		tsId;		// OriginalNetworkID
	int		onId;		// TransportStreamID
	int		svId;		// ServiceID
} STATION;

struct channel {
	int id;
	const char *name;
	unsigned int frequency;
	unsigned int ts_id;
};


#define MAX_STATION 100
static NAMES names[MAX_STATION];
static STATION Sta[MAX_STATION] = {};
static struct channel ch[MAX_STATION] = {};
static int StaCount = 0;

int read_channels( char *filename, int onoffset, int onid, NAMES *names, STATION *stas, struct channel *chs )
{
	FILE *fp = fopen( filename, "r" );
	int n=0;
	if( fp ){
		char s[255];
		while( fgets(s, sizeof(s), fp ) != NULL ){
			int freq;
			int tsid;
			int svid;
			char *t;
			memset( &names[n], 0, sizeof(names[n]) );
			memset( &stas[n], 0, sizeof(stas[n]) );
			t = strstr( s, ":DTV_DELIVERY_SYSTEM" );
			if( t ){
				memcpy( names[n].name, s, (t-s) );
			}else{
				continue;
			}
			const char target_freq[] = "DTV_FREQUENCY=";
			t = strstr(s, target_freq);
			if( t ){
				t = t+sizeof(target_freq)-1;
				freq = strtol( t, NULL, 0 );
			}else{
				continue;
			}
			const char target_isdb[] = "DTV_ISDBS_TS_ID=";
			t = strstr(s, target_isdb);
			if( t ){
				char *p;
				t = t+sizeof(target_isdb)-1;
				tsid = strtol( t, &p, 0 );
				if( *p ){
					t = p+1;
					svid = strtol( t, &p, 0 );
				}
				if( *p ){
					sprintf( names[n].ontv, "%d.ontvjapan.com", onoffset+svid );
					stas[n].name = names[n].name;
					stas[n].ontv = names[n].ontv;
					stas[n].tsId = tsid;
					stas[n].onId = onid;
					stas[n].svId = svid;
					chs[n].id = svid;
					chs[n].name = names[n].name;
					chs[n].frequency = freq;
					chs[n].ts_id = tsid;
					//printf("%s,%s,%x,%d,%d,freq=%d\n", stas[n].name, stas[n].ontv, tsid, onid, svid,freq);
					n++;
				}
			}
		}
		fclose(fp);
	}else{
		//puts("cant open");
	}
	return n;
}

void read_channels_epgdump( char *arg_onTV, STATION **psta, int *pcount )
{
	if(strcmp(arg_onTV, "/BS") == 0){
		StaCount = read_channels( "/usr/local/etc/bs_channels.txt",4000,4, names, Sta, ch );
		if( StaCount ){
			*psta = Sta;
			*pcount = StaCount;
		}
	}else if(strcmp(arg_onTV, "/CS") == 0){
		StaCount = read_channels( "/usr/local/etc/cs_channels.txt",5000,7, names, Sta, ch );
		if( StaCount ){
			*psta = Sta;
			*pcount = StaCount;
		}
	}
	return;
}

void read_channels_tune( char *arg_onTV, struct channel **pch, int *pcount )
{
	if(strcmp(arg_onTV, "/BS") == 0){
		StaCount = read_channels( "/usr/local/etc/bs_channels.txt",4000,4, names, Sta, ch );
		if( StaCount ){
			*pch = ch;
			*pcount = StaCount;
		}
	}else if(strcmp(arg_onTV, "/CS") == 0){
		StaCount = read_channels( "/usr/local/etc/cs_channels.txt",5000,7, names, Sta, ch );
		if( StaCount ){
			*pch = ch;
			*pcount = StaCount;
		}
	}
	return;
}

/*
int main()
{
	read_channels_exec();
	return 0;
}
*/


