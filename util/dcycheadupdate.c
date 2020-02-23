#include "defs.h"

typedef struct CyclicBufferHead_v1 {
    int                 cbh_version;
    int                 cbh_byteOrder;
    off_t               cbh_offset;     /* space kept for file header */
    off_t               cbh_size;       /* file size */
    off_t               cbh_pos;        /* position inside the cyclic buffer,
                                         * starting at cbh_offset */
    int                 cbh_cycles;     /* to expire history */
    int                 cbh_flnamelen;
    time_t              cbh_stime;      /* for stat needs */
    off_t               cbh_spos;
    int                 cbh_scycles;
    /* if the buffer head is stored in a dedicated file, cbh_flnamelen will be
     * set to the buffer filename len and this filename will be stored just
     * after this struct */
} CyclicBufferHead_v1;

void
Usage(void) {
    printf("Usage : dcycheadupdate file [files...]\n");
    printf("to update cyclic buffer headers\n");
    exit(0);
}

int
main(int argc, char **argv) {
    int i;

    if (argc==1) {
	Usage();
	exit(1);
    }
    for (i=1; i<argc; i++) {
	char *opt = argv[i];
	int r;

	if (opt[0]=='-') {
	    opt++;
	    switch (*opt) {
	    case 'h' :
		Usage();
		break;
	    default :
		fprintf(stderr, "unknown option: %s\n", opt - 1);
		Usage();
	    }
	} else {
	    int file;
	    int version;

	    file = open(opt, O_RDWR);
	    r = read(file, &version, sizeof(version));
	    if (r!=sizeof(version)) {
		printf("Cannot read version %s\n", opt);
		exit(1);
	    }
	    lseek(file, 0, SEEK_SET);

	    switch(version) {
		case 1: 
		     {
			struct CyclicBufferHead_v1 oldcbh;
			struct CyclicBufferHead newcbh;
			char spath[PATH_MAX];

			r = read(file, &oldcbh, sizeof(oldcbh));
			if (r!=sizeof(oldcbh)) {
			    printf("Cannot read header %s\n", opt);
			}
			printf("*** old version was 1 ***\n\tVersion : %i\n\tByteOrder : %x\n\tOffset : %lli\n\tSize : %lli\n\tPos : %lli\n\tCycles : %i\n\tflnamelen : %i\n\tstime : %i\n\tspos : %lli\n\tscycles : %i\n", oldcbh.cbh_version, oldcbh.cbh_byteOrder, oldcbh.cbh_offset, oldcbh.cbh_size, oldcbh.cbh_pos, oldcbh.cbh_cycles, oldcbh.cbh_flnamelen, oldcbh.cbh_stime, oldcbh.cbh_spos, oldcbh.cbh_scycles);
			if (oldcbh.cbh_byteOrder!=CBH_BYTEORDER) {
			    printf("CycBuf : wrong byte order or file has not been initialize (%s)\n", opt);
			    exit(1);
			}
			if (oldcbh.cbh_flnamelen!=0) { /* spool filename */
			    if (oldcbh.cbh_flnamelen>PATH_MAX) {
				printf("CycBuf : spool file name too long %s (%i/%i)\n", opt, oldcbh.cbh_flnamelen, PATH_MAX);
				exit(1);
			    }
			    r = read(file, spath, oldcbh.cbh_flnamelen);
			    if (r!=oldcbh.cbh_flnamelen) {
				printf("CycBuf : short spool filename read %s (%i/%i)\n", opt, r, oldcbh.cbh_flnamelen);
				exit(1);
			    }
			    printf("\tspool : %s\n", spath);
			} else {
			    if (oldcbh.cbh_offset<sizeof(newcbh)) {
				printf("CycBuf : can not update %s headers, data offset is too low to store updated headers\n", opt);
			    }
			}

			newcbh.cbh_version = CBH_VERSION;
			newcbh.cbh_byteOrder = CBH_BYTEORDER;
			
			newcbh.cbh_offset = oldcbh.cbh_offset;
			newcbh.cbh_size = oldcbh.cbh_size;
			newcbh.cbh_pos = oldcbh.cbh_pos;
			newcbh.cbh_cycles = oldcbh.cbh_cycles;
			newcbh.cbh_flnamelen = oldcbh.cbh_flnamelen;
			newcbh.cbh_stime = oldcbh.cbh_stime;
			newcbh.cbh_spos = oldcbh.cbh_spos;
			newcbh.cbh_scycles = oldcbh.cbh_scycles;

			newcbh.cbh_flags = 0;

			/* md5 hash */
			{
			    SpoolObject so;

			    so.so_cbhHead = &newcbh;
			    strcpy(so.so_Path, opt);
			    if (oldcbh.cbh_flnamelen) {
				so.so_cbhSpoolFD = open(spath, O_RDONLY);
			    } else {
				so.so_cbhSpoolFD = file;
			    }

			    md5CycBuf(&so, &newcbh.cbh_md5);

			    if (oldcbh.cbh_flnamelen) close(so.so_cbhSpoolFD);
			}
			printf("*** new version ***\n\tVersion : %i\n\tByteOrder : %x\n\tOffset : %lli\n\tSize : %lli\n\tPos : %lli\n\tCycles : %i\n\tflnamelen : %i\n\tstime : %i\n\tspos : %lli\n\tscycles : %i\n\tFlags : %i\n\tmd5 : %llx%llx\n", newcbh.cbh_version, newcbh.cbh_byteOrder, newcbh.cbh_offset, newcbh.cbh_size, newcbh.cbh_pos, newcbh.cbh_cycles, newcbh.cbh_flnamelen, newcbh.cbh_stime, newcbh.cbh_spos, newcbh.cbh_scycles, newcbh.cbh_flags, newcbh.cbh_md5.h1, newcbh.cbh_md5.h2);
			lseek(file, 0, SEEK_SET);
			{
			    int i;
			    i = write(file, &newcbh, sizeof(newcbh));
			    printf("write returned %i (%i)\n", i, errno);
			}
			if (oldcbh.cbh_flnamelen) {
			    write(file, spath, oldcbh.cbh_flnamelen);
			}
		     }
		     break;
		case CBH_VERSION: /* nothing to do... */
		     {
			struct CyclicBufferHead cbh;
			char spath[PATH_MAX];

			r = read(file, &cbh, sizeof(cbh));
			if (r!=sizeof(cbh)) {
			    printf("Cannot read header %s\n", opt);
			}
			printf("*** Version : %i\n\tByteOrder : %x\n\tOffset : %lli\n\tSize : %lli\n\tPos : %lli\n\tCycles : %i\n\tflnamelen : %i\n\tstime : %i\n\tspos : %lli\n\tscycles : %i\n\tflags : %i\n\tmd5 : %llx%llx\n", cbh.cbh_version, cbh.cbh_byteOrder, cbh.cbh_offset, cbh.cbh_size, cbh.cbh_pos, cbh.cbh_cycles, cbh.cbh_flnamelen, cbh.cbh_stime, cbh.cbh_spos, cbh.cbh_scycles, cbh.cbh_flags, cbh.cbh_md5.h1, cbh.cbh_md5.h2);
			if (cbh.cbh_flnamelen!=0) { /* spool filename */
			    if (cbh.cbh_flnamelen>PATH_MAX) {
				printf("CycBuf : spool file name too long %s (%i/%i)\n", opt, cbh.cbh_flnamelen, PATH_MAX);
				exit(1);
			    }
			    r = read(file, spath, cbh.cbh_flnamelen);
			    if (r!=cbh.cbh_flnamelen) {
				printf("CycBuf : short spool filename read %s (%i/%i)\n", opt, r, cbh.cbh_flnamelen);
				exit(1);
			    }
			    printf("\tspool : %s\n", spath);
			}
		     }
		     break;
		default:
		     printf("unknown version %i in %s header\n", version, opt);
		     exit(1);
	    }
	    close(file);
	}
    }
    return 0;
}
