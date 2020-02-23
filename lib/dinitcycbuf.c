/*
 * dinitcycbuf.c	- init a spool cyclic buffer
 *
 * (c)Copyright 2005, Francois Petillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 *
 */

#include "defs.h"
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef __linux__
#include <linux/fs.h> /* I should not include this but include files here are
buggy and there is no other way to get a fucking BLKGETSIZE64 */
#endif

void
Usage(void)
{
    fprintf(stderr, "Usage: dinitcycbuf [ -h headerfile ] [ -o offset ] [ -s size ] file|blockdevice\n\n");
    fprintf(stderr, "\t-h headerfile\tthe buffer header will be store in a dedicated file\n");
    fprintf(stderr, "\t-o offset\tsetting an offset where the data will be written\n");
    fprintf(stderr, "\t-s size\tsetting the file/device size\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "dinitcycbuf is used to initialize a cyclic buffer, making it usable as a spool\n");
    fprintf(stderr, "object by diablo. An header will be written at the start of the file.\n");
    fprintf(stderr, "You may set an offset to tell diablo where it should start to write spool data.\n");
    fprintf(stderr, "To avoid size guessing, you also may set the file/device size (useful if the\n");
    fprintf(stderr, "guess fails :-).\n");
    exit(1);
}

off_t getsize(char *file)
{
    struct stat st;

    if (stat(file, &st) != 0) {
	fprintf(stderr, "can not stat file %s\n", file);
	exit(1);
    }

    if (S_ISBLK(st.st_mode)) {
	int fd;
	off_t numblocks=0;

	fd = open(file, O_RDONLY);
	if (fd==-1) {
    	    fprintf(stderr, "can not open block device %s\n", file);
	    exit(1);
	}
#ifdef __FreeBSD__
	ioctl(fd, DIOCGMEDIASIZE, &numblocks);
#else /* FreeBSD */
#if defined(__linux__) && _FILE_OFFSET_BITS == 64
	ioctl(fd, BLKGETSIZE64, &numblocks);
#else
	ioctl(fd, BLKGETSIZE, &numblocks);
	numblocks *= 512
#endif /* Linux */
#endif /* FreeBSD */
	close(fd);

	fprintf(stderr, "CycBuf : block device found %s (%lli bytes)\n", file, numblocks);

	return(numblocks);
    } else if (S_ISREG(st.st_mode)) {
	fprintf(stderr, "CycBuf : regular file found %s (%lli bytes)\n", file, st.st_size);
	return(st.st_size);
    } else {
	fprintf(stderr, "CycBuf : unknown type %s\n", file);
	exit(1);
    }

}

void
headwrite(struct CyclicBufferHead *cbh, char *file, char *header)
{
    int fd;

    if (header) {
	fd = open(header, O_WRONLY|O_CREAT|O_TRUNC, S_IRUSR|S_IWUSR|S_IRGRP);
    } else {
	fd = open(file, O_WRONLY, S_IRUSR|S_IWUSR|S_IRGRP);
    }
    if (fd==-1) {
	fprintf(stderr, "can not open file to write header\n");
	exit(1);
    }
    write(fd, cbh, sizeof(CyclicBufferHead));
    if(header) {
    	write(fd, file, strlen(file)+1);
    }
}

int
main(int ac, char **av)
{
    char *file=NULL,*header=NULL;
    int i; 
    off_t size=0, offset=0;
    struct CyclicBufferHead *cbh;

    for (i = 1; i < ac; ++i) {
	char *ptr = av[i];

	if (*ptr != '-') {
	    if (file) {
		fprintf(stderr, "file/device specified twice (%s/%s)\n", file, ptr);
		exit(1);
	    }
	    file = ptr;
	    continue;
	}
	ptr += 2;
	switch(ptr[-1]) {
	case 'h':
	    header = av[++i];
	    break;
	case 'o':
	    offset = strtoll(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	case 's':
	    size = strtoll(((*ptr) ? ptr : av[++i]), NULL, 0);
	    break;
	default:
	    Usage();
	}
    }

    if (!file) Usage();
    if(header && !strcmp(file,header)) {
    	header = NULL;
    }
    if (!header && offset<sizeof(CyclicBufferHead)) {
	offset = (sizeof(CyclicBufferHead)/512+1)*512;
    }

    if (!size) {
	size = getsize(file);
    }

    if (header) {
	fprintf(stdout, "init cyclic buffer %s (header : %s, size : %lli, offset : %lli)\n", file, header, size, offset);
    } else {
	fprintf(stdout, "init cyclic buffer %s (size : %lli, offset : %lli)\n", file, size, offset);
    }

    cbh = (struct CyclicBufferHead*) malloc(sizeof(CyclicBufferHead));
    cbh->cbh_version = CBH_VERSION;
    cbh->cbh_byteOrder = CBH_BYTEORDER;
    cbh->cbh_offset = offset;
    cbh->cbh_size = size;
    cbh->cbh_pos = offset;
    cbh->cbh_cycles = 0;
    cbh->cbh_stime = time(NULL);
    cbh->cbh_spos = offset;
    cbh->cbh_scycles = 0;
    if (header) {
	cbh->cbh_flnamelen = strlen(file)+1;
    } else {
        cbh->cbh_flnamelen = 0;
    }

    headwrite(cbh, file, header);

    if (header) {
	fprintf(stdout, "%s has to be used as the entry in dspool.ctl, %s is an anonymous raw device\n", header, file);
    }
    exit(0);
}




