#include "defs.h"

int resetStat = 0;

void
Usage(void)
{
    printf("Show stats on cyclic buffers.\n\n");
    printf("dcbinfo [-r]\n");
    printf("where:\n");
    printf("\t-r\t- reset bandwidth statistics\n");
    exit(1);
}

void
BandwidthToString (char *buf, size_t bufsize, float bw)
{
    char *symb=" KMGTP";
    int p=0;
    
    while(bw>1000 && p<strlen(symb)) {
	bw = bw/1000;
	p++;
    }
    snprintf(buf, bufsize, "%.2f %cbps", bw, symb[p]);
}

void
TimeToString (char *buf, size_t bufsize, time_t tm)
{
    time_t days=24*3600, hours=3600, minutes=60;

    if(tm>days) {
	int d;
	int l;
	
	d = (int)tm/days;
	tm = tm%days;
	l = snprintf(buf, bufsize, "%i days ", d);
	buf += l;
	bufsize -= l;
    }
    if(tm>hours) {
	int d;
	int l;
	
	d = (int)tm/hours;
	tm = tm%hours;
	l = snprintf(buf, bufsize, "%i hours ", d);
	buf += l;
	bufsize -= l;
    }
    if(tm>minutes) {
	int d;
	int l;
	
	d = (int)tm/minutes;
	tm = tm%minutes;
	l = snprintf(buf, bufsize, "%i minutes ", d);
	buf += l;
	bufsize -= l;
    }
    snprintf(buf, bufsize, "%i seconds", (int)tm);
}


int
main(int ac, char **av)
{
    int i;
    uint16 spoolnum;
    char *path;
    double maxsize = 0.0;
    double minfree = 0.0;
    long minfreefiles = 0;
    long keeptime = 0;
    int expmethod = SPOOL_EXPIRE_SYNC;
    SpoolObject *so = NULL;

    LoadDiabloConfig(ac, av);
    LoadSpoolCtl(0, 1, 0);

    for (i=1; i<ac; ++i) {
	char *ptr = av[i];

	if (*ptr == '-') {
	    ptr += 2;
	    switch(ptr[-1]) {
	    case 'r':
		resetStat = 1;
		break;
	    default:
		Usage();
		break;
	    }
	}
    }

    i = GetFirstSpool(&spoolnum, &path, &maxsize, &minfree, &minfreefiles, &keeptime, &expmethod);
    while(i) {
	if (expmethod&SPOOL_EXPIRE_CYCLIC) {
	    off_t pos, size, deltapos;
	    int retention;
	    time_t deltatime;
	    char buf[200];

	    so = findSpoolObject(spoolnum);
	    printf("found spool %i path %s\n", spoolnum, so->so_Path);

	    pos = so->so_cbhHead->cbh_pos;
	    size = so->so_cbhHead->cbh_size - so->so_cbhHead->cbh_offset;
	    printf("\tposition %lli / %lli (%.2f%%) cycles %i\n", pos, size, pos*100.0/size, so->so_cbhHead->cbh_cycles);

	    deltapos = pos - so->so_cbhHead->cbh_spos + (so->so_cbhHead->cbh_cycles - so->so_cbhHead->cbh_scycles) * size;
	    deltatime = time(NULL) - so->so_cbhHead->cbh_stime;
	    BandwidthToString (buf, sizeof(buf), (float)deltapos*8.0/deltatime);
	    printf("\t%lli bytes used in %i seconds (%s)\n", deltapos, (int)deltatime, buf);

	    retention = deltatime * size / deltapos;
	    TimeToString (buf, sizeof(buf), retention);
	    printf("\tretention should be around %s\n", buf);

	    if(resetStat) {
		so->so_cbhHead->cbh_stime = time(NULL);
		so->so_cbhHead->cbh_spos = pos;
		so->so_cbhHead->cbh_scycles = so->so_cbhHead->cbh_cycles;
	    }
	}
	i = GetNextSpool(&spoolnum, &path, &maxsize, &minfree, &minfreefiles, &keeptime, &expmethod);
    }
    return 0;
}
