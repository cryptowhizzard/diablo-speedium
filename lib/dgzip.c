/*-
 * Copyright (c) 2011 MoreUSENET, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id: dgzip.c,v 1.2 2011/05/17 04:42:55 jgreco Exp $
 */




#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <zlib.h>
#include <stdlib.h>

#define	ZBUFFER		1048576
#define	GZIP_MAGIC0	0x1F
#define	GZIP_MAGIC1	0x8B
#define	ORIG_NAME	0x08
#define	OS_TYPECODE	0x03

#define	LOWBYTE(x)	((x) & 0xff)


#include "defs.h"

Prototype int diablo_minigzip(char *file);
Prototype int dominigzip(int ifd, int ofd, const char *outbase, uint32_t mtime, off_t *osize);

int
dominigzip(int ifd, int ofd, const char *outbase, uint32_t mtime, off_t *osizep)
{
	z_stream z;
	char *obuf, *ibuf;
	off_t nin = 0, nout = 0;
	ssize_t rval;
	int i, error;
	uLong crc;

	if (! ((obuf = malloc(ZBUFFER)))) {
		return(-1);
	}
	if (! ((ibuf = malloc(ZBUFFER)))) {
		free(obuf);
		return(-1);
	}

	memset((void *)&z, 0, sizeof(z));
	z.zfree = Z_NULL;
	z.zalloc = Z_NULL;
	z.opaque = 0;

	i = snprintf(obuf, ZBUFFER, "%c%c%c%c%c%c%c%c%c%c%s", 
		     GZIP_MAGIC0, GZIP_MAGIC1, Z_DEFLATED,
		     *outbase ? ORIG_NAME : 0, LOWBYTE(mtime),
		     LOWBYTE(mtime >> 8), LOWBYTE(mtime >> 16),
		     LOWBYTE(mtime >> 24), 2, OS_TYPECODE, outbase);
	i++;

	z.next_out = (unsigned char *)obuf + i;
	z.avail_out = ZBUFFER - i;

	if (((error = deflateInit2(&z, 9, Z_DEFLATED, (-MAX_WBITS), 8, Z_DEFAULT_STRATEGY))) != Z_OK) {
		free(ibuf);
		free(obuf);
		return(-1);
	}
	crc = crc32(0L, Z_NULL, 0);
	for (;;) {
		if (! z.avail_out) {
			if (write(ofd, obuf, ZBUFFER) != ZBUFFER) {
				free(ibuf);
				free(obuf);
				return(-1);
			}
			nout += ZBUFFER;
			z.next_out = (unsigned char *)obuf;
			z.avail_out = ZBUFFER;
		}

		if (! z.avail_in) {
			if (((rval = read(ifd, ibuf, ZBUFFER))) < 0) {
				free(ibuf);
				free(obuf);
				return(-1);
			}
			if (! rval) {
				break;
			}
			crc = crc32(crc, (const Bytef *)ibuf, (unsigned) rval);
			nin += rval;
			z.next_in = (unsigned char *)ibuf;
			z.avail_in = rval;
		}
		error = deflate(&z, Z_NO_FLUSH);
		if (error != Z_OK && error != Z_STREAM_END) {
			free(ibuf);
			free(obuf);
			return(-1);
		}
	}

	/* no more to read */
	for (;;) {
		size_t len;
		ssize_t w;

		error = deflate(&z, Z_FINISH);
		if (error != Z_OK && error != Z_STREAM_END) {
			free(ibuf);
			free(obuf);
			return(-1);
		}

		len = (char *)z.next_out - obuf;
		w = write(ofd, obuf, len);
		if (w < 0 || (size_t)w != len) {
			free(ibuf);
			free(obuf);
			return(-1);
		}
		nout += len;
		z.next_out = (unsigned char *)obuf;
		z.avail_out = ZBUFFER;

		if (error == Z_STREAM_END) {
			break;
		}

	}

	/* finish */
	if (deflateEnd(&z) != Z_OK) {
		free(ibuf);
		free(obuf);
		return(-1);
	}

	snprintf(obuf, ZBUFFER, "%c%c%c%c%c%c%c%c", 
		 LOWBYTE((int)crc), LOWBYTE((int)(crc >> 8)),
		 LOWBYTE((int)(crc >> 16)), LOWBYTE((int)(crc >> 24)),
		 LOWBYTE((int)nin), LOWBYTE((int)(nin >> 8)),
		 LOWBYTE((int)(nin >> 16)),
		 LOWBYTE((int)(nin >> 24)));

	if (write(ofd, obuf, 8) != 8) {
		free(ibuf);
		free(obuf);
		return(-1);
	} else {
		nout += 8;
	}

	free(ibuf);
	free(obuf);
	if (osizep) {
		*osizep = nout;
	}
	return(nin);
}





int
diablo_minigzip(char *file)
{
	char outtmp[PATH_MAX + 1], outname[PATH_MAX + 1];
	char outbase[PATH_MAX + 1], *ptr;
	int ifd, ofd;
	struct stat sb, isb;
	off_t osize;
	struct timeval tv[2];

	/* Input file error - do not proceed */
	if (stat(file, &isb)) {
		return(-1);
	}

	/* Final output file filename */
	snprintf(outname, sizeof(outname), "%s.gz", file);

	/* Figure output tmp filename */
	if ((ptr = strrchr(outname, '/'))) {
		*ptr = '\0';
		snprintf(outtmp, sizeof(outtmp), "%s/.%s", outname, ptr + 1);
		*ptr = '/';
	} else {
		snprintf(outtmp, sizeof(outtmp), ".%s", file);
	}

	/* Base name of input filename (gzip likes this) */
	if ((ptr = strrchr(file, '/'))) {
		snprintf(outbase, sizeof(outbase), "%s", ptr + 1);
	} else {
		snprintf(outbase, sizeof(outbase), "%s", file);
	}

	/* Output tmp file exists - do not proceed */
	if (! stat(outtmp, &sb)) {
		return(-1);
	}

	/* Output file exists - do not proceed */
	if (! stat(outname, &sb)) {
		return(-1);
	}

	if ((ifd = open(file, O_RDONLY)) < 0) {
		return(-1);
	}

	if ((ofd = open(outtmp, O_WRONLY | O_CREAT | O_EXCL, 0600)) < 0) {
		close(ifd);
		return(-1);
	}

	if (dominigzip(ifd, ofd, outbase, (uint32_t)isb.st_mtime, &osize) < 0) {
		close(ofd);
		close(ifd);
		unlink(outtmp);
		return(-1);
	}

	close(ifd);

	if (stat(outtmp, &sb)) {
		close(ofd);
		unlink(outtmp);
		return(-1);
	}

	if (osize != sb.st_size) {
		close(ofd);
		unlink(outtmp);
		return(-1);
	}

	sb = isb;
	sb.st_mode &= S_IRWXU | S_IRWXG | S_IRWXO;

	if (fchmod(ofd, sb.st_mode) < 0) {
		close(ofd);
		unlink(outtmp);
		return(-1);
	}

	memset((void *)&tv, 0, sizeof(tv));
	tv[0].tv_sec = sb.st_atime;
	tv[1].tv_sec = sb.st_mtime;

	if (futimes(ofd, tv) < 0) {
		close(ofd);
		unlink(outtmp);
		return(-1);
	}
	close(ofd);

	if (rename(outtmp, outname)) {
		unlink(outtmp);
		return(-1);
	}

	unlink(file);

	return(0);
}

