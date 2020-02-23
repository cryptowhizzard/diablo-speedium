
/*
 * LIB/EXPIRE.C
 *
 * (c)Copyright 1997, Matthew Dillon, All Rights Reserved.  Refer to
 *    the COPYRIGHT file in the base directory of this distribution 
 *    for specific rights granted.
 */

#include "defs.h"

#include <sys/param.h>
#ifndef _AIX
#include <sys/mount.h>
#endif
#ifdef _AIX
#include <sys/statfs.h>
#endif

#if USE_SYSV_STATFS
#include <sys/statfs.h>
#define f_bavail f_bfree
#endif

/*
 * The solaris statvfs is rather limited, but we suffer with reduced
 * capability (and hence take a possible performance hit).
 */
#if USE_SUN_STATVFS
#include <sys/statvfs.h>        /* god knows if this hack will work */
#define f_bsize f_frsize        /* god knows if this hack will work */
#define fsid_t u_long
#define statfs statvfs
#endif

#if USE_SYS_VFS                 /* this is mainly for linux     */
#include <sys/vfs.h>
#endif

Prototype void ArticleFileName(char *path, int pathSize, History *h, int opt);
Prototype int SpoolCompressed(uint16 spool);
Prototype char *GetSpoolPath(uint16 spool, int gmt, int opt);
Prototype uint16 GetSpoolFromPath(char *path);
Prototype uint16 GetSpool(const char *msgid, const char *nglist, int size, int arttype, char *label, int *t, int *complvl);
Prototype uint32 SpoolDirTime(void);
Prototype int AllocateSpools(time_t t);
Prototype void LoadSpoolCtl(time_t gmt, int force, int romode);
Prototype int GetFirstSpool(uint16 *spoolnum, char **path, double *maxsize, double *minfree, long *minfreefiles, long *keeptime, int *expmethod);
Prototype int GetNextSpool(uint16 *spoolnum, char **path, double *maxsize, double *minfree, long *minfreefiles, long *keeptime, int *expmethod);
Prototype SpoolObject *findSpoolObject(uint16 spool);
Prototype off_t getCycBufPos(SpoolObject *so, int size, int *cycles, int *flags);
Prototype int lockCycBufArt(SpoolObject *so, History *h, int *locked);
Prototype int checkCycBufSpool(uint16 spool);
Prototype int checkCycBufBounds(SpoolObject *so, History *h);
Prototype int checkArticle(char *relPath, char *msgId, off_t fileOff, int fileSize, uint16 spoolNum, char *spoolOpt);
Prototype int md5CycBuf(SpoolObject *so, md5hash_t *md5);

SpoolObject *SpoolObjects = NULL;
SpoolObject *CurrentSpoolObject = NULL;
SpoolObject *SpoolObjectMap[MAX_SPOOL_OBJECTS];
MetaSpool *MetaSpools = NULL;
GroupExpire *ExBase = NULL;
MemPool *SPMemPool = NULL;
MemPool *GRMemPool = NULL;
time_t DirTime = 0;

int createSpoolDir(SpoolObject *so, uint32 gmt);
uint16 findSpoolGrp(const char *msgid, GroupList *groups, int size, int ngcount, int arttype, char *label, int *t, int *complvl);
int findLabel(LabelList *ll, char *label);
void loadSpoolCtl(FILE *fi, int romode);
double spaceFreeOn(char *part);
int spoolSizeGb(char *part);
void dumpSpoolConfig(void);

/*
 * ArticleFileName() - get the absolute path for an article file/dir from
 *			the history entry.
 */

void
ArticleFileName(char *path, int pathSize, History *h, int opt)
{
    char *spool;

    if (checkCycBufSpool(H_SPOOL(h->exp))) {
	SpoolObject *so;
	so = findSpoolObject(H_SPOOL(h->exp));
	snprintf(path, pathSize, "%s", so->so_CPth);
    } else {
	spool = GetSpoolPath(H_SPOOL(h->exp), h->gmt, opt);
	if (spool == NULL) {
	    strcpy(path, "/dev/null");
	    return;
	}
	switch (opt) {
	    case ARTFILE_DIR:
	    case ARTFILE_DIR_REL:
		snprintf(path, pathSize, "%s%sD.%08x",
					spool,
					spool[0] ? "/" : "",
					h->gmt - h->gmt % 10);
		break;
	    case ARTFILE_FILE:
	    case ARTFILE_FILE_REL:
		if (h->boffset || h->bsize) {
		    snprintf(path, pathSize, "%s%sD.%08x/B.%04x",
					spool,
					spool[0] ? "/" : "",
					h->gmt - h->gmt % 10,
					h->iter);
		} else {
		    snprintf(path, pathSize, "%s%sD.%08x/%08x.%08x.%04x",
					spool,
					spool[0] ? "/" : "",
					h->gmt - h->gmt % 10,
					h->hv.h1,
					h->hv.h2,
					h->iter);
    		}
		break;
	    default:
		break;
	}
    }
    if (DebugOpt > 4)
	printf("ArticleFileName=%s\n", path);
}

/*
 * Check whether a particular spool is compressed
 *
 * Returns: lvl = compressed spool
 *	    0 = normal spool
 */
int
SpoolCompressed(uint16 spool)
{
    if (spool > MAX_SPOOL_OBJECTS)
	return(0);
    if (SpoolObjectMap[spool] != NULL &&
				SpoolObjectMap[spool]->so_CompressLvl != -1)
	return(SpoolObjectMap[spool]->so_CompressLvl);
    else
	return(0);
}

/*
 * Get the path to a particular spool
 *
 * Could be:
 *	NULL         : /dev/null
 *	empty string : SpoolHomePat
 *	start with / : absolute path
 *	else	     : absolute path from spool object
 */
char *
GetSpoolPath(uint16 spool, int gmt, int opt)
{
    static char path[PATH_MAX];

    if (spool > MAX_SPOOL_OBJECTS)
	return(NULL);
    if (SpoolObjectMap[spool] && SpoolObjectMap[spool]->so_Path != NULL) {
	if (*SpoolObjectMap[spool]->so_Path == '/' ||
			opt == ARTFILE_DIR_REL || opt == ARTFILE_FILE_REL) {
	    snprintf(path, sizeof(path), "%s", SpoolObjectMap[spool]->so_Path);
	    if ((strcmp(path, PatExpand(SpoolHomePat)) == 0) && 
			(opt == ARTFILE_DIR_REL || opt == ARTFILE_FILE_REL))
		path[0] = 0;
	} else if (*SpoolObjectMap[spool]->so_Path) {
	    snprintf(path, sizeof(path), "%s/%s", PatExpand(SpoolHomePat),
					SpoolObjectMap[spool]->so_Path);
	} else {
	    snprintf(path, sizeof(path), "%s", PatExpand(SpoolHomePat));
	}
	if (SpoolObjectMap[spool]->so_SpoolDirs > 0) {
	    int l = strlen(path);
	    snprintf(path + l, sizeof(path) - l, "%sN.%02x",
			(l == 0) ? "" : "/",
			(gmt / 10) % SpoolObjectMap[spool]->so_SpoolDirs);
	}
    } else {
	return("");
    }
    return(path);
}

/*
 * Get the spool object given a path to a spool or spool directory
 */
uint16
GetSpoolFromPath(char *path)
{
    SpoolObject *ts;
    for (ts = SpoolObjects; ts != NULL; ts = ts->so_Next) {
	printf("XX:%s:%s:\n", path, ts->so_Path);
	if (strncmp(ts->so_Path, path, strlen(ts->so_Path)) == 0)
	    return(ts->so_SpoolNum);
    }
    return((uint16)-1);
}

/*
 * GetSpool() return the spool number for an article given the
 * group list and article size
 *
 * Return:  spoolnumber
 *	-1 = no match
 *	-2 = matched, but reject article
 *	-3 = matched, but don't store on disk
 */
uint16
GetSpool(const char *msgid, const char *nglist, int size, int arttype, char *label, int *t, int *complvl)
{
    GroupList *grouplist = NULL;
    GroupList *gl;
    char *gst;
    char *p;
    uint16 spool = (uint16)-1;
    int ngcount = 0;

    /*
     * Split the newsgroups into a structure of group names
     * NOTE: The group names in the struct are in reverse order
     */

    gst = zallocStr(&GRMemPool, nglist);
    for (p = strtok(gst, "*?, \t\n\r"); p; p = strtok(NULL, "*?, \t\n\r")) {
	ngcount++;
	gl = zalloc(&GRMemPool, sizeof(GroupList));
	gl->group = p;
	gl->next = grouplist;
	grouplist = gl;
    }
    if (grouplist)
	spool = findSpoolGrp(msgid, grouplist, size, ngcount, arttype, label, t, complvl);
    if (DebugOpt > 2)
	printf("SPOOL: %02x\n", spool);
    freePool(&GRMemPool);
    return(spool);
}

/*
 * Return the currently allocated spool article directory
 */
uint32
SpoolDirTime(void)
{
    return(DirTime);
}

/*
 * Create the spool article directory on a spool object
 *
 * Returns:
 * 	-1	error
 * 	0	success
 */
int
createSpoolDir(SpoolObject *so, uint32 gmt)
{
    char path[PATH_MAX];
    History h;
    struct stat st;
	
    if (so->so_ExpireMethod==SPOOL_EXPIRE_CYCLIC) {
	return(0); /* no need to create spool dir in a cyclic buffer */
    }

    h.gmt = gmt;
    h.exp = so->so_SpoolNum + 100;
    ArticleFileName(path, sizeof(path), &h, ARTFILE_DIR);
    if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
	if (DebugOpt > 1)
	    printf("Creating spool article dir: %s\n", path);
	if (mkdir(path, 0755) == -1 && errno != EEXIST) {
	    logit(LOG_CRIT, "%s: Unable to create article dir %s: %s",   
					PatLibExpand(DSpoolCtlPat), path,
					strerror(errno));
	    return(-1);
	}
    }
    so->so_DirTime = gmt;
    return(0);
}

/*
 * Allocate a spool object for each metaspool and create the
 * directory on that spool where the article files will be stored.
 *
 * This is run from the master diablo process, with the allocation
 * carrying over from the fork().
 */
int
AllocateSpools(time_t t)
{
    uint32 gmt = t / 60;
    MetaSpool *ms;
    int i;
    int w, n, totw;
    double space;
    double maxspace;

    for (ms = MetaSpools; ms; ms = ms->ms_Next) {
	/*
	 * Don't look for spool objects if we don't need them
	 */
	if (ms->ms_RejectArts)
	    continue;
	if (ms->ms_ReAllocInterval / 60 == 0)
	    DirTime = gmt;
	else
	    DirTime = gmt - gmt % (ms->ms_ReAllocInterval / 60);
	switch (ms->ms_AllocationStrategy) {
	    case SPOOL_ALLOC_NONE:
		ms->ms_AllocationStrategy = SPOOL_ALLOC_SEQ;
	    case SPOOL_ALLOC_SEQ:
		/*
		 * Allocate spools in a sequential fashion
		 */
		if (ms->ms_NextAllocation == -1  &&  ms->ms_NumSpoolObjects > 0)
		    ms->ms_NextAllocation = random() % ms->ms_NumSpoolObjects;
		ms->ms_AllocatedSpool = ms->ms_SpoolObjects[ms->ms_NextAllocation];
		if (++ms->ms_NextAllocation >= ms->ms_NumSpoolObjects)
		    ms->ms_NextAllocation = 0;
		break;
	    case SPOOL_ALLOC_SPACE:
		/*
		 *  Pick a spool object that has the most space free
		 */
		ms->ms_AllocatedSpool = ms->ms_SpoolObjects[0];
		maxspace = 0.0;
		if (ms->ms_NumSpoolObjects > 1) {
		    for (i = 0; i < ms->ms_NumSpoolObjects; i++) {
			SpoolObject *so = ms->ms_SpoolObjects[i];
			if (!so)
			    continue;
			if ((space = spaceFreeOn(GetSpoolPath(
					so->so_SpoolNum, 0, ARTFILE_DIR))) >
							maxspace) {
			    ms->ms_AllocatedSpool = so;
			    maxspace = space;
			}
		    }
		}
		break;
	    case SPOOL_ALLOC_SINGLE:
		/*
		 * Allocate the same spool until a timelimit is up
		 */
		ms->ms_NextAllocation = (t / ms->ms_ReAllocInterval) %
							ms->ms_NumSpoolObjects;
		ms->ms_AllocatedSpool = ms->ms_SpoolObjects[ms->ms_NextAllocation];
		break;
	    case SPOOL_ALLOC_WEIGHTED:
		/*
		 *  Pick a random spool object using weights
		 */
		totw = 0;
		for (i = 0; i < ms->ms_NumSpoolObjects; i++) {
		    SpoolObject *so = ms->ms_SpoolObjects[i];
		    if (so)
			totw += so->so_Weight;
		}
		w = random() % totw;

		n = 0;
		ms->ms_AllocatedSpool = ms->ms_SpoolObjects[0];
		for (i = 0; i < ms->ms_NumSpoolObjects; i++) {
		    SpoolObject *so = ms->ms_SpoolObjects[i];
		    if (!so)
			continue;
		    if (w >= n && w < n + so->so_Weight) {
			ms->ms_AllocatedSpool = so;
			break;
		    }
		    n += so->so_Weight;
		}
		break;
	}

	if (ms->ms_AllocatedSpool == NULL)
	    continue;

	if (ms->ms_AllocatedSpool->so_DirTime == 0 ||
				DirTime != ms->ms_AllocatedSpool->so_DirTime)
	    if (createSpoolDir(ms->ms_AllocatedSpool, DirTime) == -1)
		return(-1);
    }
    return(1);
}

/*
 * Find the first metaspool object that matches any newsgroup
 * in the list of groups provided.
 *
 * Return:  spoolnumber
 *	-1 = no match
 *	-2 = matched, but reject article
 *	-3 = matched, but don't store on disk
 */
uint16
findSpoolGrp(const char *msgid, GroupList *groups, int size, int ngcount, int arttype, char *label, int *t, int *complvl)
{
    GroupExpire *ex;

    for (ex = ExBase; ex; ex = ex->ex_Next) {
	if (WildGroupFind(ex->ex_Wild, groups)) {
	    if (msgid != NULL && ex->ex_MetaSpool->ms_HashFeed &&
			!HM_CheckForMatch(ex->ex_MetaSpool->ms_HashFeed, (mid_t)msgid))
		continue;
	    if (size && ex->ex_MetaSpool->ms_MaxSize && size >
					ex->ex_MetaSpool->ms_MaxSize)
		continue;
	    if (ngcount && ex->ex_MetaSpool->ms_MaxCross && ngcount >
					ex->ex_MetaSpool->ms_MaxCross)
		continue;
	    if (ex->ex_MetaSpool->ms_Label != NULL &&
			findLabel(ex->ex_MetaSpool->ms_Label, label) == 0)
		continue;
	    if (arttype && ArtTypeMatch(arttype,
					ex->ex_MetaSpool->ms_ArtTypes) == 0)
		continue;
	    if (t)
		*t = ex->ex_MetaSpool->ms_ReAllocInterval;
	    if (ex->ex_MetaSpool->ms_RejectArts)
		return((uint16)-2);
	    if (ex->ex_MetaSpool->ms_DontStore)
		return((uint16)-3);
	    if (complvl)
		*complvl = ex->ex_MetaSpool->ms_AllocatedSpool->so_CompressLvl;
	    return(ex->ex_MetaSpool->ms_AllocatedSpool->so_SpoolNum);
	}
    }
    return((uint16)-1);
}

SpoolObject*
findSpoolObject(uint16 spool)
{
    if (spool > MAX_SPOOL_OBJECTS)
	return(NULL);
    if (SpoolObjectMap[spool] && SpoolObjectMap[spool]->so_SpoolNum == spool) {
	return(SpoolObjectMap[spool]);
    }
    return(NULL);
}

int
checkCycBufSpool(uint16 spool)
{
    SpoolObject *so;
    
    if (spool > MAX_SPOOL_OBJECTS)
	return(0);
    so = findSpoolObject(spool);
    if (so->so_ExpireMethod==SPOOL_EXPIRE_CYCLIC) {
	return(1);
    }
    return(0);
}

int
checkCycBufBounds(SpoolObject *so, History *h)
{
    int cyc1, cyc2;
    off_t pos;

    cyc1=so->so_cbhHead->cbh_cycles;
    pos=so->so_cbhHead->cbh_pos;
    cyc2=so->so_cbhHead->cbh_cycles;
    if (cyc1!=cyc2 || cyc1==-1) {
	hflock(so->so_cbhHeadFD, 0, XLOCK_SH);
	cyc1=so->so_cbhHead->cbh_cycles;
	pos=so->so_cbhHead->cbh_pos;
	hflock(so->so_cbhHeadFD, 0, XLOCK_UN);
    }

    if ((cyc1&0xffff)==h->iter) {
	if (pos > h->boffset+h->bsize)
	    return 0;
	logit(LOG_CRIT, "cyclic spool pos corrupted (article %x.%x pos %lli cycles %i)", h->hv.h1, h->hv.h2, pos, cyc1);
    } else if (((cyc1-1)&0xffff) == h->iter && pos < h->boffset)
	return 0;
    return -1;
}

int
checkArticle(char *relPath, char *msgId, off_t fileOff, int fileSize, uint16 spoolNum, char *spoolOpt)
{
    if (checkCycBufSpool(spoolNum)) { /* spoolOpt should contain cycles */
	SpoolObject *so;
	History h;
	
	so = findSpoolObject(spoolNum);
	h.iter = strtol(spoolOpt, NULL, 10) & 0xffff;
	h.boffset = fileOff;
	h.bsize = fileSize;
	return checkCycBufBounds(so, &h);
    }
    return 0;
}

int
lockCycBufArt(SpoolObject *so, History *h, int *locked)
{
    if (so->so_cbhSpoolFD<0) return -1;

    if (checkCycBufBounds(so, h)) return -1;

    if (lflock(so->so_cbhSpoolFD, h->boffset, h->bsize+1, XLOCK_SH|XLOCK_NB)<0) {
logit(LOG_CRIT, "  locking cycbuf failed (fd %i %x@%llx)", so->so_cbhSpoolFD, h->bsize+1, h->boffset);
	return -1;
    }

    if (checkCycBufBounds(so, h)) {
logit(LOG_CRIT, "unlocking cycbuf (fd %i %x@%llx) due to bounds", so->so_cbhSpoolFD, h->bsize+1, h->boffset);
	lflock(so->so_cbhSpoolFD, h->boffset, h->bsize+1, XLOCK_UN);
	return -1;
    }

    *locked = so->so_cbhSpoolFD;
    return 0;
}

off_t
getCycBufPos(SpoolObject *so, int size, int *cycles, int *flags)
{
    if (so->so_cbhHead==NULL || so->so_cbhSpoolFD==-1 || so->so_cbhHeadFD==-1) {
	return -1;
    }

    if(size) { /* �criture */
	CyclicBufferHead *cbh = so->so_cbhHead;
	int cyc;
	off_t pos;

	/* LOCK */
	hflock(so->so_cbhHeadFD, 0, XLOCK_EX);

	if (cbh->cbh_pos+size+1>cbh->cbh_size) { /* wrapping */
	    if (flags) *flags |= CBH_WRAPPED;
	    if (size<CBH_MD5_BUFSIZE) size = CBH_MD5_BUFSIZE-1;
	    cyc = cbh->cbh_cycles;
	    cbh->cbh_cycles = -1; /* read lock trigger */
	    pos = cbh->cbh_offset;
	    cbh->cbh_pos = pos+size+1;
	    cbh->cbh_cycles = cyc+1;
logit(LOG_CRIT, "getBufCycPos: wrap %llx+%x -> %llx (cycles %i)", pos, size, cbh->cbh_pos, cbh->cbh_cycles);
	} else {
	    pos = cbh->cbh_pos;
	    cbh->cbh_pos += size+1;
	}
	*cycles = cbh->cbh_cycles;

	/* UNLOCK */
	hflock(so->so_cbhHeadFD, 0, XLOCK_UN);

	return pos;
    } else { /* lecture */
	int cyc1, cyc2;
	off_t pos;

	cyc1=so->so_cbhHead->cbh_cycles;
	pos=so->so_cbhHead->cbh_pos;
	cyc2=so->so_cbhHead->cbh_cycles;
	if (cyc1!=cyc2 || cyc1==-1) {
	    hflock(so->so_cbhHeadFD, 0, XLOCK_SH);
	    cyc1=so->so_cbhHead->cbh_cycles;
	    pos=so->so_cbhHead->cbh_pos;
	    hflock(so->so_cbhHeadFD, 0, XLOCK_UN);
	}
	*cycles = cyc1;
	return pos;
    }
}

/*
 * the function checks so->so_cbhHead->cbh_md5 and return 0 if it matches
 * if md5 is not NULL, it will store the md5 value into it
 */
int
md5CycBuf(SpoolObject *so, md5hash_t *md5)
{
    char buf[CBH_MD5_BUFSIZE];
    struct diablo_MD5Context ctx;
    md5hash_t m;

    if (md5==NULL) {
	md5 = &m;
    }

    if (so->so_cbhHead->cbh_pos<512) {
	printf("CycBuf : not enough data to process md5 on spool %s\n", so->so_Path);
	exit(1);
    }
    lseek(so->so_cbhSpoolFD, so->so_cbhHead->cbh_offset, SEEK_SET);
    read(so->so_cbhSpoolFD, buf, sizeof(buf));

    diablo_MD5Init(&ctx);
    diablo_MD5Update(&ctx, buf, sizeof(buf));

    diablo_MD5Final((unsigned char *)md5, &ctx);
    return memcmp(md5, &so->so_cbhHead->cbh_md5, sizeof(md5hash_t)); 
}

void
openCycBuf(SpoolObject *so, char *path, char *spth, int romode)
{
    struct CyclicBufferHead cbh;
    ssize_t r;

    if (romode) {
	so->so_cbhSpoolFD = open(path, O_RDONLY);
    } else {
	so->so_cbhSpoolFD = open(path, O_RDWR);
    }
    if (so->so_cbhSpoolFD==-1) {
	logit(LOG_CRIT, "CycBuf : can not open file %s", path);
	exit(1);
    }
    so->so_cbhHeadFD = so->so_cbhSpoolFD;
    r = read(so->so_cbhSpoolFD, &cbh, sizeof(CyclicBufferHead));
    if (r!=sizeof(CyclicBufferHead)) {
	logit(LOG_CRIT, "CycBuf : short read %s (%i/%i)", path, r, sizeof(CyclicBufferHead));
	if (cbh.cbh_version!=CBH_VERSION)
		logit(LOG_CRIT, "CycBuf : wrong version, please update your buffer (%s)", path);
	exit(1);
    }
    if (romode) {
	so->so_cbhHead = xmap(NULL, sizeof(CyclicBufferHead), PROT_READ, MAP_SHARED, so->so_cbhSpoolFD, 0);
    } else {
	so->so_cbhHead = xmap(NULL, sizeof(CyclicBufferHead), PROT_READ|PROT_WRITE, MAP_SHARED, so->so_cbhSpoolFD, 0);
    }
    if (so->so_cbhHead->cbh_byteOrder!=CBH_BYTEORDER) {
	logit(LOG_CRIT, "CycBuf : wrong byte order or file has not been initialize (%s)", path);
	exit(1);
    }
    if (so->so_cbhHead->cbh_version!=CBH_VERSION) {
	logit(LOG_CRIT, "CycBuf : wrong version, please update your buffer (%s)", path);
	exit(1);
    }

    if (cbh.cbh_flnamelen!=0) { /* spool filename */

	if (cbh.cbh_flnamelen>PATH_MAX) {
	    logit(LOG_CRIT, "CycBuf : spool file name too long %s (%i/%i)", path, cbh.cbh_flnamelen, PATH_MAX);
	    exit(1);
	}
	r = read(so->so_cbhSpoolFD, spth, cbh.cbh_flnamelen);
	if (r!=cbh.cbh_flnamelen) {
	    logit(LOG_CRIT, "CycBuf : short spool filename read %s (%i/%i)", path, r, cbh.cbh_flnamelen);
	    exit(1);
	}
	if (romode) {
	    so->so_cbhSpoolFD = open(spth, O_RDONLY);
	} else {
	    so->so_cbhSpoolFD = open(spth, O_RDWR);
	}
	if (so->so_cbhSpoolFD==-1) {
	    logit(LOG_CRIT, "CycBuf : can not open spool file %s", spth);
	    exit(1);
	}
    } else {
	strncpy(spth, path, PATH_MAX);
    }
    if (so->so_cbhHead->cbh_flags&CBH_FLG_SPOOLMASK) {
	if (so->so_SpoolNum != (so->so_cbhHead->cbh_flags & CBH_FLG_SPOOLMASK)) {
	    logit(LOG_CRIT, "CycBuf : spool mismatch, %s is not spool %i (found spool %i)", path, so->so_SpoolNum, (so->so_cbhHead->cbh_flags & CBH_FLG_SPOOLMASK));
	    exit(1);
	}
    	if (md5CycBuf(so, NULL)) {
	    logit(LOG_CRIT, "CycBuf : wrong md5sum on spool %i (%s)", so->so_SpoolNum, spth);
	    exit(1);
	}
    } else {
	if (!romode) {
	    so->so_cbhHead->cbh_flags = (so->so_cbhHead->cbh_flags & ~CBH_FLG_SPOOLMASK) | (so->so_SpoolNum & CBH_FLG_SPOOLMASK);
	    logit(LOG_INFO, "CycBuf : affecting %s to spool %i and processing md5", path, so->so_SpoolNum);
    	    md5CycBuf(so, &so->so_cbhHead->cbh_md5);
	}
    }
}

void
addSpool(SpoolObject *so, int romode)
{
    struct stat st;
    char *path;
    int i;

    if (so->so_SpoolNum > 0) {
	SpoolObjectMap[so->so_SpoolNum] = so;
	so->so_Next = NULL;
	    if (so->so_ExpireMethod==SPOOL_EXPIRE_CYCLIC) {
            so->so_SpoolDirs = 0;
     	   }
    }

    if (!so->so_Path[0])
	snprintf(so->so_Path, sizeof(so->so_Path), "P.%02d",
				so->so_SpoolNum);
    if (so->so_Weight < 0)
	so->so_Weight = spoolSizeGb(so->so_Path);

    if (so->so_SpoolDirs) {
	for (i = 0; i < so->so_SpoolDirs; i++) {
	    path = GetSpoolPath(so->so_SpoolNum, 10 * i, ARTFILE_DIR);
	    if (stat(path, &st) != 0) {
		logit(LOG_INFO, "Creating spooldir N.%02x for %s", i, path);
		if (mkdir(path, 0755) == -1)  {
		    logit(LOG_CRIT, "%s: Unable to create spooldir N.%02x on %s: %s",
				PatLibExpand(DSpoolCtlPat), i, path, strerror(errno));
		    exit(1);
		}
	    }
	}
    } else {
	path = GetSpoolPath(so->so_SpoolNum, 0, ARTFILE_DIR);
	if (stat(path, &st) != 0) {
	    logit(LOG_CRIT, "%s: Missing spool partition %s",
				PatLibExpand(DSpoolCtlPat), path);
	    exit(1);
	} else if (so->so_ExpireMethod==SPOOL_EXPIRE_CYCLIC) {
	    if (S_ISBLK(st.st_mode)) {
		openCycBuf(so, path, so->so_CPth, romode);
	    } else if (S_ISREG(st.st_mode)) {
		openCycBuf(so, path, so->so_CPth, romode);
	    } else {
	    	logit(LOG_CRIT, "CycBuf : unknown type %s", path);
		exit(1);
	    }
	}
    }

    /* spool 00 is special */
    if (so->so_SpoolNum == 0)
	return;

    if (SpoolObjects == NULL) {
	SpoolObjects = so;
    } else {
	SpoolObject *tso;
	for (tso = SpoolObjects; tso->so_Next != NULL; tso = tso->so_Next);
	tso->so_Next = so;
    }
}

void
addMeta(MetaSpool *ms)
{
    if (!ms->ms_DontStore && !ms->ms_RejectArts &&
						ms->ms_NumSpoolObjects == 0) {
	logit(LOG_ERR, "%s: No spools defined for metaspool '%s'",
				PatLibExpand(DSpoolCtlPat), ms->ms_Name);
	zfree(&SPMemPool, ms, sizeof(MetaSpool));
	exit(1);
    }
    ms->ms_Next = NULL;
    if (MetaSpools == NULL) {
	MetaSpools = ms;
    } else {
	MetaSpool *tms;
	for (tms = MetaSpools; tms->ms_Next != NULL; tms = tms->ms_Next);
	tms->ms_Next = ms;
    }
}

void
addExpire(GroupExpire *ex)
{
    if (ex->ex_MetaSpool == NULL) {
	logit(LOG_ERR, "%s: No metaspool defined for expire '%s'",
				PatLibExpand(DSpoolCtlPat), ex->ex_Wild);
	zfree(&SPMemPool, ex, sizeof(GroupExpire));
	exit(1);
    }
    ex->ex_Next = NULL;
    if (ExBase == NULL) {
	ExBase = ex;
    } else {
	GroupExpire *tex;
	for (tex = ExBase; tex->ex_Next != NULL; tex = tex->ex_Next);
	tex->ex_Next = ex;
    }
}

MetaSpool *
findMeta(char *meta)
{
    MetaSpool *tms;
    for (tms = MetaSpools; tms != NULL; tms = tms->ms_Next) {
	if (strcmp(meta, tms->ms_Name) == 0)
	     return(tms);
    }
    return(NULL);
}

int
findLabel(LabelList *ll, char *label)
{
    while (ll != NULL) {
	if (strcmp(ll->label, label) == 0)
	    return(1);
	ll = ll->next;
    }
    return(0);
}

uint32 SpoolGmtMin = (uint32)-1;
time_t SpoolMTime = 0;
int SpoolRoMode = -1;

#define	EXSTAT_NONE	0x00
#define	EXSTAT_SPOOL	0x01
#define	EXSTAT_META	0x02
#define	EXSTAT_EXPIRE	0x04
#define	EXSTAT_OVERVIEW	0x08

void
cleanSpoolObjects(SpoolObject *so)
{
    while(so) {
	if (so->so_ExpireMethod==SPOOL_EXPIRE_CYCLIC) {
	    if (so->so_cbhHead) {
		xunmap(so->so_cbhHead, sizeof(CyclicBufferHead));
		so->so_cbhHead = NULL;
	    }
	    if (so->so_cbhHeadFD>-1 && so->so_cbhHeadFD!=so->so_cbhSpoolFD) {
		close(so->so_cbhHeadFD);
	    }
	    so->so_cbhHeadFD = -1;
	    if (so->so_cbhSpoolFD > -1) {
		close(so->so_cbhSpoolFD);
		so->so_cbhSpoolFD = -1;
	    }
	}
	so = so->so_Next;
    }
}

void
loadSpoolCtl(FILE *fi, int romode)
{
    char buf[MAXGNAME+256];
    int status = EXSTAT_NONE;
    int line = 0;
    int i;
    SpoolObject *spoolObj = NULL;
    MetaSpool *metaSpool = NULL;
    GroupExpire *expire = NULL;

    cleanSpoolObjects(SpoolObjects);
    freePool(&SPMemPool);
    SpoolObjects = NULL;
    MetaSpools = NULL;
    ExBase = NULL;
    for (i = 0; i < MAX_SPOOL_OBJECTS; i++)
	SpoolObjectMap[i] = NULL;
    /*
     * The default spool is just the legacy spool directory patern
     * It can be modified later
     */
    {
	spoolObj = zalloc(&SPMemPool, sizeof(SpoolObject));
	spoolObj->so_SpoolNum = 0;
	spoolObj->so_CompressLvl = -1;
	spoolObj->so_Weight = -1;
	spoolObj->so_Next = NULL;
	strcpy(spoolObj->so_Path, PatExpand(SpoolHomePat));
	SpoolObjects = spoolObj;
	SpoolObjectMap[0] = spoolObj;
	spoolObj = NULL;
    }
    if (DebugOpt > 1)
	printf("Loading dspool.ctl\n");
    while (fi && fgets(buf, sizeof(buf), fi) != NULL) {
	char *cmd = buf;
	char *arg;

	line++;
	while (isspace((int)*cmd))
	    cmd++;
	if (!*cmd || *cmd == '/' || *cmd == '#')
	    continue;

	cmd = strtok(cmd, " \t\n\r");
	arg = strtok(NULL, " \t\n\r");
	if (strcmp(cmd, "end") == 0) {
	    switch (status) {
		case EXSTAT_SPOOL:
		     addSpool(spoolObj, romode);
		     spoolObj = NULL;
		     break;
		case EXSTAT_META:
		     addMeta(metaSpool);
		     metaSpool = NULL;
		     break;
		case EXSTAT_EXPIRE:
		     addExpire(expire);
		     expire = NULL;
		     break;
		default:
		     logit(LOG_ERR, "%s: Extra 'end' in line %d",
					PatLibExpand(DSpoolCtlPat), line);
		     exit(1);
	    }
	    status = EXSTAT_NONE;
	    continue;
	}
	if (arg == NULL || !*arg)
	    continue;

	if (!status && strcmp(cmd, "spool") == 0) {
	    int spoolnum;
	    spoolnum = strtol(arg, NULL, 10);
	    if (spoolnum == 0) {
		spoolObj = SpoolObjects;
	    } else {
		spoolObj = zalloc(&SPMemPool, sizeof(SpoolObject));
		spoolObj->so_SpoolNum = spoolnum;
	    }
	    if (spoolObj->so_SpoolNum > 99) {
		logit(LOG_ERR, "%s: Invalid spool object number %d in line %d",
					PatLibExpand(DSpoolCtlPat),
					spoolObj->so_SpoolNum, line);
		exit(1);
	    }
	    status = EXSTAT_SPOOL;
	    spoolObj->so_CompressLvl = -1;
            spoolObj->so_cbhHeadFD = -1;
            spoolObj->so_cbhSpoolFD = -1;
	    spoolObj->so_Weight = -1;
	    continue;
	}
	if (!status && strcmp(cmd, "metaspool") == 0) {
	    metaSpool = zalloc(&SPMemPool, sizeof(MetaSpool));
	    snprintf(metaSpool->ms_Name, sizeof(metaSpool->ms_Name), "%s", arg);
	    status = EXSTAT_META;
	    metaSpool->ms_ArtTypes = NULL;
	    metaSpool->ms_Label = NULL;
	    metaSpool->ms_ReAllocInterval = 600;
	    metaSpool->ms_NextAllocation = -1;
	    continue;
	}
	if (!status && strcmp(cmd, "expire") == 0) {
	    if (arg == NULL) {
		logit(LOG_ERR, "%s: Missing metaspool for expire in line %d",
					PatLibExpand(DSpoolCtlPat), line);
		exit(1);
	    }
	    cmd = strtok(NULL, " \t\n\r");
	    if (cmd == NULL) {
		logit(LOG_ERR, "%s: Missing metaspool for expire in line %d",
					PatLibExpand(DSpoolCtlPat), line);
		exit(1);
	    }
	    expire = zalloc(&SPMemPool, sizeof(GroupExpire));
	    snprintf(expire->ex_Wild, sizeof(expire->ex_Wild), "%s", arg);
	    if ((expire->ex_MetaSpool = findMeta(cmd)) == NULL) {
		logit(LOG_ERR, "%s: Unknown metaspool '%s' in line %d",
					PatLibExpand(DSpoolCtlPat), cmd, line);
		exit(1);
	    }
	    status = EXSTAT_NONE;
	    addExpire(expire);
	    expire = NULL;
	    continue;
	}

	if (status == EXSTAT_NONE) {
	    logit(LOG_ERR, "%s: Unknown definition '%s' in line %d",
				PatLibExpand(DSpoolCtlPat), cmd, line);
	    exit(1);
	}

	if (status == EXSTAT_SPOOL) {
	    if (strcmp(cmd, "minfree") == 0) {
		spoolObj->so_MinFree = bsizektod(arg);
		continue;
	    } else if (strcmp(cmd, "minfreefiles") == 0) {
		spoolObj->so_MinFreeFiles = atol(arg);
		continue;
	    } else if (strcmp(cmd, "maxsize") == 0) {
		spoolObj->so_MaxSize = bsizektod(arg);
		continue;
	    } else if (strcmp(cmd, "keeptime") == 0) {
		spoolObj->so_KeepTime = btimetol(arg);
		continue;
	    } else if (strcmp(cmd, "expiremethod") == 0) {
		if (strcmp(arg, "sync") == 0)
		    spoolObj->so_ExpireMethod = SPOOL_EXPIRE_SYNC;
		else if (strcmp(arg, "dirsize") == 0)
		    spoolObj->so_ExpireMethod = SPOOL_EXPIRE_DIRSZ;
		else if (strcmp(arg, "cyclic") == 0)
		    spoolObj->so_ExpireMethod = SPOOL_EXPIRE_CYCLIC;
		else  {
		    logit(LOG_ERR, "%s: Unknown expiremethod '%s' in line %d",
				PatLibExpand(DSpoolCtlPat), arg, line);
		    exit(1);
		}                   
		continue;
	    } else if (strcmp(cmd, "path") == 0) {
		const char *shome = PatExpand(SpoolHomePat);
		if (strncmp(arg, "%s/", 3) == 0) {
		    arg += 3;
		    strncpy(spoolObj->so_Path, arg, sizeof(spoolObj->so_Path) -1);
		    spoolObj->so_Path[sizeof(spoolObj->so_Path) - 1] = '\0';
		} else if (strcmp(arg, shome) != 0 &&
				strncmp(arg, shome, strlen(shome)) == 0 &&
				strlen(arg) > strlen(shome) &&
				arg[strlen(shome)] == '/') {
		    strncpy(spoolObj->so_Path, arg + strlen(shome) + 1,
						sizeof(spoolObj->so_Path) - 1);
		    spoolObj->so_Path[sizeof(spoolObj->so_Path) - 1] = '\0';
		} else {
		    strncpy(spoolObj->so_Path, arg, sizeof(spoolObj->so_Path) - 1);
		    spoolObj->so_Path[sizeof(spoolObj->so_Path) - 1] = '\0';
		}
		continue;
	    } else if (strcmp(cmd, "spooldirs") == 0) {
		spoolObj->so_SpoolDirs = strtol(arg, NULL, 0);
		continue;
	    } else if (strcmp(cmd, "compresslvl") == 0) {
		spoolObj->so_CompressLvl = strtol(arg, NULL, 0);
		continue;
	    } else if (strcmp(cmd, "weight") == 0) {
		spoolObj->so_Weight = strtol(arg, NULL, 0);
		continue;
	    } else {
		logit(LOG_ERR, "%s: Unknown spool option '%s' in line %d",
					PatLibExpand(DSpoolCtlPat), cmd, line);
	    }
	}
	if (status == EXSTAT_META) {
	    if (strcmp(cmd, "maxsize") == 0) {
		metaSpool->ms_MaxSize = bsizektod(arg);
		continue;
	    } else if (strcmp(cmd, "maxcross") == 0) {
		metaSpool->ms_MaxCross = bsizektod(arg);
		continue;
	    } else if (strcmp(cmd, "keeptime") == 0) {
		metaSpool->ms_KeepTime = btimetol(arg);
		continue;
	    } else if (strcmp(cmd, "reallocint") == 0) {
		metaSpool->ms_ReAllocInterval = btimetol(arg);
		if (metaSpool->ms_ReAllocInterval < 60)
		    metaSpool->ms_ReAllocInterval = 60;
		continue;
	    } else if (strcmp(cmd, "arttypes") == 0) {
		char *p = arg;
		ArtTypeList *atp = NULL;
		ArtTypeList *tatp;
		while ((p = strsep(&arg, " \t:,")) != NULL) {
		    tatp = zalloc(&SPMemPool, sizeof(ArtTypeList));
		    tatp->negate = 0;
		    if (*p == '!') {
			tatp->negate = 1;
			p++;
		    }
		    tatp->arttype = ArtTypeConv(p);
		    tatp->next = NULL;
		    if (atp != NULL)
			atp->next = tatp;
		    atp = tatp;
		    if (metaSpool->ms_ArtTypes == NULL)
			metaSpool->ms_ArtTypes = atp;
		}
		continue;
	    } else if (strcmp(cmd, "hashfeed") == 0) {
	        if (! ((metaSpool->ms_HashFeed = DiabHashFeedParse(&SPMemPool, arg)))) {
			logit(LOG_ERR, "%s: Unknown hashfeed in line %d\n",
				PatLibExpand(DSpoolCtlPat), line);
		}
		continue;
	    } else if (strcmp(cmd, "rejectarts") == 0) {
		if (tolower((int)*arg) == 'y')
		    metaSpool->ms_RejectArts = 1;
		continue;
	    } else if (strcmp(cmd, "dontstore") == 0) {
		if (tolower((int)*arg) == 'y')
		    metaSpool->ms_DontStore = 1;
		continue;
	    } else if (strcmp(cmd, "label") == 0) {
		LabelList *l;

		l = zalloc(&SPMemPool, sizeof(LabelList));
		l->label = zalloc(&SPMemPool, strlen(arg) + 1);
		l->next = metaSpool->ms_Label;
		metaSpool->ms_Label = l;
		strcpy(metaSpool->ms_Label->label, arg);
		continue;
	    } else if (strcmp(cmd, "allocstrat") == 0) {
		if (strcasecmp(arg, "sequential") == 0)
		    metaSpool->ms_AllocationStrategy = SPOOL_ALLOC_SEQ;
		else if (strcasecmp(arg, "space") == 0)
		    metaSpool->ms_AllocationStrategy = SPOOL_ALLOC_SPACE;
		else if (strcasecmp(arg, "single") == 0)
		    metaSpool->ms_AllocationStrategy = SPOOL_ALLOC_SINGLE;
		else if (strcasecmp(arg, "weighted") == 0)
		    metaSpool->ms_AllocationStrategy = SPOOL_ALLOC_WEIGHTED;
		else
		    logit(LOG_ERR, "%s: Unknown alloc strategy in line %d\n",
				PatLibExpand(DSpoolCtlPat), line);
		continue;
	    } else if (strcmp(cmd, "spool") == 0) {
		SpoolObject *ts;
		uint16 sn = strtol(arg, NULL, 10);
		for (ts = SpoolObjects; ts != NULL; ts = ts->so_Next) {
		    if (ts->so_SpoolNum == sn) {
			metaSpool->ms_SpoolObjects[metaSpool->ms_NumSpoolObjects++] = ts;
			break;
		    }
		}
		if (ts == NULL) {
		    logit(LOG_ERR, "%s: Unknown spool '%d' in line %d",
                                PatLibExpand(DSpoolCtlPat), sn, line);
		    exit(1);

		}
		continue;
	    } else if (strcmp(cmd, "addgroup") == 0) {
		cmd = strtok(cmd, " \t\n\r");
		if (cmd == NULL) {
		    logit(LOG_ERR, "%s: Missing group for addgroup in line %d",
					PatLibExpand(DSpoolCtlPat), line);
		    exit(1);
		}
		expire = zalloc(&SPMemPool, sizeof(GroupExpire));
		snprintf(expire->ex_Wild, sizeof(expire->ex_Wild), "%s", arg);
		expire->ex_MetaSpool = metaSpool;
		addExpire(expire);
		expire = NULL;
		continue;
	    } else {
		logit(LOG_ERR, "%s: Unknown metaspool option '%s' in line %d",
					PatLibExpand(DSpoolCtlPat), cmd, line);
	    }
	}
    }
}

/*
 * Load spool.ctl
 *
 * NOTE: exit program if any errors
 */
void
LoadSpoolCtl(time_t gmt, int force, int romode)
{
    /*
     * check for dspool.ctl file modified once a minute
     */

    gmt = gmt / 60;
    if (SpoolRoMode!=romode) force = 1;

    if (force || gmt != SpoolGmtMin) {
	struct stat st = { 0 };

	SpoolGmtMin = gmt;

	/*
	 * dspool.ctl
	 */

	{
	    FILE *fi;

	    fi = fopen(PatLibExpand(DSpoolCtlPat), "r");

	    if (fi == NULL) {
		logit(LOG_EMERG, "%s file not found",
					PatLibExpand(DSpoolCtlPat));
		exit(1);
	    }

	    if (force || fi == NULL || 
		(fstat(fileno(fi), &st) == 0 && st.st_mtime != SpoolMTime)
	    ) {
		if (force)
		    fstat(fileno(fi), &st);
		SpoolMTime = st.st_mtime; /* may be 0 if file failed to open */
		loadSpoolCtl(fi, romode);
	    }
	    if (fi)
		fclose(fi);
	    if (DebugOpt > 3)
		dumpSpoolConfig();
	}
    }
    SpoolRoMode=romode;
}

/*
 * Return some values for the first spool object
 */
int
GetFirstSpool(uint16 *spoolnum, char **path, double *maxsize, double *minfree, long *minfreefiles, long *keeptime, int *expmethod)
{
    CurrentSpoolObject = SpoolObjects;
    if (CurrentSpoolObject == NULL)
	return(0);
    if (spoolnum)
	*spoolnum = CurrentSpoolObject->so_SpoolNum;
    if (path)
	*path = CurrentSpoolObject->so_Path;
    if (maxsize)
	*maxsize = CurrentSpoolObject->so_MaxSize;
    if (minfree)
	*minfree = CurrentSpoolObject->so_MinFree;
    if (minfreefiles)
	*minfreefiles = CurrentSpoolObject->so_MinFreeFiles;
    if (keeptime)
	*keeptime = CurrentSpoolObject->so_KeepTime;
    if (expmethod)
	*expmethod = CurrentSpoolObject->so_ExpireMethod;
    return(1);
}

/*
 * Return some values for the first spool object
 */
int
GetNextSpool(uint16 *spoolnum, char **path, double *maxsize, double *minfree, long *minfreefiles, long *keeptime, int *expmethod)
{
    CurrentSpoolObject = CurrentSpoolObject->so_Next;
    if (CurrentSpoolObject == NULL)
	return(0);
    if (spoolnum)
	*spoolnum = CurrentSpoolObject->so_SpoolNum;
    if (path)
	*path = CurrentSpoolObject->so_Path;
    if (maxsize)
	*maxsize = CurrentSpoolObject->so_MaxSize;
    if (minfree)
	*minfree = CurrentSpoolObject->so_MinFree;
    if (minfreefiles)
	*minfreefiles = CurrentSpoolObject->so_MinFreeFiles;
    if (keeptime)
	*keeptime = CurrentSpoolObject->so_KeepTime;
    if (expmethod)
	*expmethod = CurrentSpoolObject->so_ExpireMethod;
    return(1);
}

double
spaceFreeOn(char *part)
{
    struct statfs stmp;

#if USE_SYSV_STATFS
    if (statfs(part, &stmp, sizeof(stmp), 0) != 0) {
#else
    if (statfs(part, &stmp) != 0) {
#endif
        logit(LOG_ERR, "unable to statfs %s (%s)", part, strerror(errno));   
        return(0.0);
    }
    return(stmp.f_bavail * 1.0 * stmp.f_bsize);
}

int
spoolSizeGb(char *part)
{
    struct statfs stmp;
    long tmp;

#if USE_SYSV_STATFS
    if (statfs(part, &stmp, sizeof(stmp), 0) != 0) {
#else
    if (statfs(part, &stmp) != 0) {
#endif
        logit(LOG_ERR, "unable to statfs %s (%s)", part, strerror(errno));   
        return 1;
    }
    tmp = (1024*1024*1024) / stmp.f_bsize;
    return stmp.f_blocks / tmp;
}

void
dumpSpoolConfig(void)
{
    SpoolObject *so = NULL;
    MetaSpool *ms = NULL;
    GroupExpire *ex = NULL;
    int i;
    
    for (i = 0; i < MAX_SPOOL_OBJECTS; i++) {
	if (!SpoolObjectMap[i])
	    continue;
	printf("-----------------------------------------------------\n");
	printf("SPOOL num      : %02x\n", SpoolObjectMap[i]->so_SpoolNum);
	printf("SPOOL path     : %s\n", SpoolObjectMap[i]->so_Path);
	printf("SPOOL spooldirs: %d\n", SpoolObjectMap[i]->so_SpoolDirs);
	printf("SPOOL minfree  : %s\n", ftos(SpoolObjectMap[i]->so_MinFree));
	printf("SPOOL minfreef : %s\n", ftos(SpoolObjectMap[i]->so_MinFreeFiles));
	printf("SPOOL maxsize  : %s\n", ftos(SpoolObjectMap[i]->so_MaxSize));
	printf("SPOOL expmethod: %d\n", SpoolObjectMap[i]->so_ExpireMethod);
	printf("SPOOL keeptime : %ld\n", SpoolObjectMap[i]->so_KeepTime);
	printf("SPOOL dirtime  : %d\n", SpoolObjectMap[i]->so_DirTime);
    }
    for (ex = ExBase; ex; ex = ex->ex_Next) {
	printf("====================================================\n");
	printf("wild: %s\n", ex->ex_Wild);
	ms = ex->ex_MetaSpool;
	    printf("  meta name: %s\n", ms->ms_Name);
	    printf("  meta maxsize: %s\n", ftos(ms->ms_MaxSize));
	    printf("  meta keeptime: %ld\n", ms->ms_KeepTime);
	    printf("  meta maxcross: %d\n", ms->ms_MaxCross);
	    printf("  meta numspool: %d\n", ms->ms_NumSpoolObjects);
	    printf("  meta dontstore: %d\n", ms->ms_DontStore);
	    printf("  meta rejectarts: %d\n", ms->ms_RejectArts);
	    printf("  meta arttypes: ");
	    {
		ArtTypeList *at = ms->ms_ArtTypes;
		for (; at != NULL; at = at->next)
		    printf("%s%08x ", at->negate ? "!" : "", at->arttype);
		printf("\n");
	    }
	    if (ms->ms_AllocatedSpool)
		printf("  meta allocspool: %02x\n", ms->ms_AllocatedSpool->so_SpoolNum);
	    else
		printf("  meta allocspool: NONE\n");
	    for (i = 0; i < ms->ms_NumSpoolObjects; i++) {
		so = ms->ms_SpoolObjects[i];
		printf("  meta spool: %d\n", i);
		printf("    spool num      : %02x\n", so->so_SpoolNum);
		printf("    spool path     : %s\n", so->so_Path);
		printf("    spool spooldirs: %d\n", so->so_SpoolDirs);
		printf("    spool minfree  : %s\n", ftos(so->so_MinFree));
		printf("    spool minfreef : %s\n", ftos(so->so_MinFreeFiles));
		printf("    spool dirtime  : %d\n", so->so_DirTime);
	    }
    }
    printf("====================================================\n");
}
