/*----------------------------------------------------------------------------/
/  FatFs - Generic FAT file system module  R0.11                             /
/----------------------------------------------------------------------------*/

#include "ff.h"
#include "diskio.h"
#include <stddef.h>
/*-----------------------------------------------------------------------*/
/* Private Variables                                                     */
/*-----------------------------------------------------------------------*/

static FATFS *FatFs[1];	/* Pointer to the file system object (logical drive) */
/* static WORD Fsid; */		/* File system mount ID */

/*-----------------------------------------------------------------------*/
/* Private Functions                                                     */
/*-----------------------------------------------------------------------*/

static FRESULT check_mount (FATFS* fs)
{
	DWORD sect, dw;
	WORD bcs;

	if (fs->fs_type != 0) return FR_OK;

	/* Initialize FS */
	if (disk_initialize(fs->drv) & STA_NOINIT) return FR_NOT_READY;
	if (disk_read(fs->drv, fs->win, 0, 1) != RES_OK) return FR_DISK_ERR;
	/* Check MBR */
	if (fs->win[510] != 0x55 || fs->win[511] != 0xAA) return FR_NO_FILESYSTEM;
	
	/* Find partition */
	sect = 0;
	if (fs->win[0x1C2] != 0xEE) { /* Not GPT */
		if (fs->win[0x36] == 'F' && fs->win[0x37] == 'A' && fs->win[0x38] == 'T') {
			/* FAT VBR */
		} else {
			/* MBR, get first partition */
			sect = *(DWORD*)&fs->win[0x1C6];
			if (disk_read(fs->drv, fs->win, sect, 1) != RES_OK) return FR_DISK_ERR;
		}
	}

	/* Parse VBR */
	bcs = *(WORD*)&fs->win[11]; /* Bytes per sector */
	if (bcs != 512) return FR_NO_FILESYSTEM;
	
	fs->n_fats = fs->win[16];
	fs->csize = fs->win[13];
	fs->n_rootdir = *(WORD*)&fs->win[17];
	fs->n_fatent = *(WORD*)&fs->win[22] ? *(WORD*)&fs->win[22] : *(DWORD*)&fs->win[32];
	
	fs->volbase = sect;
	fs->fatbase = sect + *(WORD*)&fs->win[14];
	
	fs->database = fs->fatbase + fs->n_fats * fs->n_fatent; // Sectors per FAT
	/* Fix n_fatent calculation */
	dw = *(WORD*)&fs->win[22];
	if (!dw) dw = *(DWORD*)&fs->win[36]; // Sectors per FAT (FAT32)
	fs->database = fs->fatbase + fs->n_fats * dw;

	fs->dirbase = fs->fatbase + fs->n_fats * dw; // For FAT16/12
	
	/* Check FAT32 */
	if (*(WORD*)&fs->win[17] == 0) {
		fs->fs_type = 3;
		fs->dirbase = *(DWORD*)&fs->win[44]; /* Root dir cluster */
		dw = *(DWORD*)&fs->win[32]; /* Total sectors */
		fs->n_fatent = (dw - (fs->database - sect)) / fs->csize;
	} else {
		fs->fs_type = 2; /* Assume FAT16 */
		dw = *(WORD*)&fs->win[19];
		if (!dw) dw = *(DWORD*)&fs->win[32];
		fs->n_fatent = (dw - (fs->database - sect)) / fs->csize;
	}

	return FR_OK;
}

/*-----------------------------------------------------------------------*/
/* String functions                                                      */
/*-----------------------------------------------------------------------*/

/* Copy memory to memory */
static void mem_cpy (void* dst, const void* src, UINT cnt) {
	BYTE *d = (BYTE*)dst;
	const BYTE *s = (const BYTE*)src;
	while (cnt--) *d++ = *s++;
}

/* Fill memory */
static void mem_set (void* dst, int val, UINT cnt) {
	BYTE *d = (BYTE*)dst;
	while (cnt--) *d++ = (BYTE)val;
}

/* Compare memory to memory */
static int mem_cmp (const void* dst, const void* src, UINT cnt) {
	const BYTE *d = (const BYTE*)dst, *s = (const BYTE*)src;
	int r = 0;
	while (cnt-- && (r = *d++ - *s++) == 0) ;
	return r;
}

/* Check if chr is contained in the string */
/*
static int chk_chr (const char* str, int chr) {
	while (*str && *str != chr) str++;
	return *str;
}
*/


/*-----------------------------------------------------------------------*/
/* Request/Release grant to access the volume                            */
/*-----------------------------------------------------------------------*/
#if _FS_REENTRANT
/* Create a sync object */
int ff_cre_syncobj (BYTE vol, _SYNC_t* sobj)
{
	*sobj = CreateMutex(NULL, FALSE, NULL);
	return (int)(*sobj != INVALID_HANDLE_VALUE);
}

/* Delete a sync object */
int ff_del_syncobj (_SYNC_t sobj)
{
	return (int)CloseHandle(sobj);
}

/* Lock sync object */
int ff_req_grant (_SYNC_t sobj)
{
	return (int)(WaitForSingleObject(sobj, _FS_TIMEOUT) == WAIT_OBJECT_0);
}

/* Unlock sync object */
void ff_rel_grant (_SYNC_t sobj)
{
	ReleaseMutex(sobj);
}
#endif


/*-----------------------------------------------------------------------*/
/* File/Volume management functions                                      */
/*-----------------------------------------------------------------------*/

static FRESULT move_window (FATFS* fs, DWORD sector)
{
	DWORD wsect;

	wsect = fs->winsect;
	if (wsect != sector) {	/* Changed current window */
#if !_FS_READONLY
		if (fs->wflag) {	/* Write back dirty window if needed */
			if (disk_write(fs->drv, fs->win, wsect, 1) != RES_OK)
				return FR_DISK_ERR;
			fs->wflag = 0;
			if (wsect < (fs->fatbase + fs->fsize)) {	/* In FAT area */
				BYTE nf;
				for (nf = fs->n_fats; nf >= 2; nf--) {	/* Reflect the change to all FAT copies */
					wsect += fs->fsize;
					disk_write(fs->drv, fs->win, wsect, 1);
				}
			}
		}
#endif
		if (disk_read(fs->drv, fs->win, sector, 1) != RES_OK)
			return FR_DISK_ERR;
		fs->winsect = sector;
	}
	return FR_OK;
}



static FRESULT sync_fs (FATFS* fs)
{
	FRESULT res;

	res = move_window(fs, fs->winsect);
	if (res == FR_OK) {
		if (fs->fsi_flag == 1) {
			/* Update FSInfo sector if needed */
			if (move_window(fs, fs->volbase + 1) == FR_OK) {
				fs->win[488] = (BYTE)(fs->free_clst);
				fs->win[489] = (BYTE)(fs->free_clst >> 8);
				fs->win[490] = (BYTE)(fs->free_clst >> 16);
				fs->win[491] = (BYTE)(fs->free_clst >> 24);
				fs->win[492] = (BYTE)(fs->last_clst);
				fs->win[493] = (BYTE)(fs->last_clst >> 8);
				fs->win[494] = (BYTE)(fs->last_clst >> 16);
				fs->win[495] = (BYTE)(fs->last_clst >> 24);
				fs->wflag = 1;
				res = move_window(fs, 0);
				fs->fsi_flag = 0;
			}
		}
		if (res == FR_OK && disk_ioctl(fs->drv, CTRL_SYNC, 0) != RES_OK)
			res = FR_DISK_ERR;
	}
	return res;
}

/* Get sector# from cluster# */
static DWORD clust2sect (FATFS* fs, DWORD clst)
{
	clst -= 2;
	if (clst >= fs->n_fatent - 2) return 0;
	return clst * fs->csize + fs->database;
}

/* Get FAT entry */
static DWORD get_fat (FATFS* fs, DWORD clst)
{
	UINT wc, bc;
	BYTE *p;
	DWORD val;

	if (clst < 2 || clst >= fs->n_fatent) return 1;

	val = 0xFFFFFFFF;

	switch (fs->fs_type) {
	case 1 :	/* FAT12 */
		bc = (UINT)clst; bc += bc / 2;
		if (move_window(fs, fs->fatbase + (bc / 512)) != FR_OK) break;
		wc = fs->win[bc % 512];
		bc++;
		if (move_window(fs, fs->fatbase + (bc / 512)) != FR_OK) break;
		wc |= fs->win[bc % 512] << 8;
		val = (clst & 1) ? (wc >> 4) : (wc & 0xFFF);
		break;

	case 2 :	/* FAT16 */
		if (move_window(fs, fs->fatbase + (clst / (512 / 2))) != FR_OK) break;
		p = &fs->win[clst * 2 % 512];
		val = p[0] | p[1] << 8;
		break;

	case 3 :	/* FAT32 */
		if (move_window(fs, fs->fatbase + (clst / (512 / 4))) != FR_OK) break;
		p = &fs->win[clst * 4 % 512];
		val = p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24;
		val &= 0x0FFFFFFF;
		break;
	}

	return val;
}


/* Put FAT entry */
static FRESULT put_fat (FATFS* fs, DWORD clst, DWORD val)
{
	UINT bc;
	BYTE *p;
	FRESULT res;

	if (clst < 2 || clst >= fs->n_fatent) return FR_INT_ERR;

	switch (fs->fs_type) {
	case 1 :	/* FAT12 */
		bc = (UINT)clst; bc += bc / 2;
		res = move_window(fs, fs->fatbase + (bc / 512));
		if (res != FR_OK) return res;
		p = &fs->win[bc % 512];
		*p = (clst & 1) ? ((*p & 0x0F) | ((BYTE)val << 4)) : (BYTE)val;
		fs->wflag = 1;
		bc++;
		res = move_window(fs, fs->fatbase + (bc / 512));
		if (res != FR_OK) return res;
		p = &fs->win[bc % 512];
		*p = (clst & 1) ? (BYTE)(val >> 4) : ((*p & 0xF0) | ((BYTE)(val >> 8) & 0x0F));
		fs->wflag = 1;
		break;

	case 2 :	/* FAT16 */
		res = move_window(fs, fs->fatbase + (clst / (512 / 2)));
		if (res != FR_OK) return res;
		p = &fs->win[clst * 2 % 512];
		p[0] = (BYTE)val; p[1] = (BYTE)(val >> 8);
		fs->wflag = 1;
		break;

	case 3 :	/* FAT32 */
		res = move_window(fs, fs->fatbase + (clst / (512 / 4)));
		if (res != FR_OK) return res;
		p = &fs->win[clst * 4 % 512];
		p[0] = (BYTE)val; p[1] = (BYTE)(val >> 8);
		p[2] = (BYTE)(val >> 16); p[3] = (BYTE)(val >> 24);
		fs->wflag = 1;
		break;

	default :
		res = FR_INT_ERR;
	}

	return res;
}


/* Remove a cluster chain */
static FRESULT remove_chain (FATFS* fs, DWORD clst)
{
	FRESULT res;
	DWORD nxt;
#if _USE_TRIM
	DWORD scl = clst;
#endif

	if (clst < 2 || clst >= fs->n_fatent) return FR_INT_ERR;

	for (;;) {
		nxt = get_fat(fs, clst);
		if (nxt == 0) break;
		if (nxt == 1) return FR_INT_ERR;
		if (nxt == 0xFFFFFFFF) return FR_DISK_ERR;

		res = put_fat(fs, clst, 0);
		if (res != FR_OK) return res;

		if (fs->free_clst < fs->n_fatent - 2) {
			fs->free_clst++;
			fs->fsi_flag |= 1;
		}

#if _USE_TRIM
		if (_USE_TRIM && (clst + 1) != nxt) {
			disk_ioctl(fs->drv, CTRL_TRIM, &scl);
			scl = nxt;
		}
#endif
		if (nxt >= fs->n_fatent) break;
		clst = nxt;
	}
#if _USE_TRIM
	if (_USE_TRIM) disk_ioctl(fs->drv, CTRL_TRIM, &scl);
#endif

	return FR_OK;
}


/* Create a cluster chain */
static DWORD create_chain (FATFS* fs, DWORD clst)
{
	DWORD cs, ncl, scl;
	FRESULT res;

	if (clst == 0) {	/* Create a new chain */
		scl = fs->last_clst;
		if (scl == 0 || scl >= fs->n_fatent) scl = 1;
	}
	else {				/* Stretch the current chain */
		cs = get_fat(fs, clst);
		if (cs < 2) return 1;
		if (cs == 0xFFFFFFFF) return cs;
		if (cs < fs->n_fatent) return cs;
		scl = clst;
	}

	ncl = scl;
	for (;;) {
		ncl++;
		if (ncl >= fs->n_fatent) {
			ncl = 2;
			if (ncl > scl) return 0;
		}
		cs = get_fat(fs, ncl);
		if (cs == 0) break;
		if (cs == 0xFFFFFFFF || cs == 1) return 0xFFFFFFFF;
		if (ncl == scl) return 0;
	}

	res = put_fat(fs, ncl, 0x0FFFFFFF);
	if (res != FR_OK) return 0xFFFFFFFF;

	if (clst != 0) {
		res = put_fat(fs, clst, ncl);
		if (res != FR_OK) return 0xFFFFFFFF;
	}

	fs->last_clst = ncl;
	if (fs->free_clst < fs->n_fatent - 2) fs->free_clst--;
	fs->fsi_flag |= 1;

	return ncl;
}


/* Directory handling */
static FRESULT dir_sdi (DIR* dp, UINT idx)
{
	DWORD clst, sect;
	UINT ic;

	dp->index = (WORD)idx;
	clst = dp->sclust;
	if (clst == 1 || clst >= dp->fs->n_fatent) return FR_INT_ERR;

	if (!clst && dp->fs->fs_type == 3) clst = dp->fs->dirbase;

	if (clst == 0) {	/* Static table (root-dir in FAT12/16) */
		if (idx >= dp->fs->n_rootdir) return FR_INT_ERR;
		sect = dp->fs->dirbase;
	}
	else {				/* Dynamic table (sub-dirs or root-dir in FAT32) */
		ic = 512 / 32 * dp->fs->csize;
		while (idx >= ic) {
			clst = get_fat(dp->fs, clst);
			if (clst == 0xFFFFFFFF) return FR_DISK_ERR;
			if (clst < 2 || clst >= dp->fs->n_fatent) return FR_INT_ERR;
			idx -= ic;
		}
		sect = clust2sect(dp->fs, clst);
	}
	dp->clust = clst;
	if (!sect) return FR_INT_ERR;
	dp->sect = sect + idx / (512 / 32);
	dp->dir = dp->fs->win + (idx % (512 / 32)) * 32;

	return FR_OK;
}

/*
static FRESULT dir_read (DIR* dp, int vol)
{
	FRESULT res;

	res = move_window(dp->fs, dp->sect);
	if (res != FR_OK) return res;
	if (vol)
		dp->fs->wflag = 1;
	return FR_OK;
}
*/

static FRESULT dir_register (DIR* dp)
{
	FRESULT res;
	
	if (move_window(dp->fs, dp->sect) != FR_OK) return FR_DISK_ERR;
	
	mem_set(dp->dir, 0, 32);	/* Clean the entry */
	mem_cpy(dp->dir, dp->fn, 11);	/* Set file name */
	dp->dir[11] = 0;			/* Set attribute (Archive) */
	dp->fs->wflag = 1;

	return FR_OK;
}

static FRESULT dir_find (DIR* dp)
{
	FRESULT res;
	BYTE c, *dir;

	res = dir_sdi(dp, 0);
	if (res != FR_OK) return res;

	do {
		res = move_window(dp->fs, dp->sect);
		if (res != FR_OK) return res;
		dir = dp->dir;
		c = dir[0];
		if (c == 0) { res = FR_NO_FILE; break; }
		if (c != 0xE5 && !(dir[11] & 0x08) && !mem_cmp(dir, dp->fn, 11)) /* Match? */
			break;
		res = dir_sdi(dp, dp->index + 1);
	} while (res == FR_OK);

	return res;
}

/* Create a file name */
static void create_name (DIR* dp, const TCHAR* path)
{
	BYTE *p = dp->fn;
	BYTE c, *s = (BYTE*)path;
	int i, ni;

	/* Create 8.3 name */
	for (i = 0; i < 11; i++) p[i] = ' ';
	i = 0; ni = 8;
	while ((c = *s++) != 0 && c != '/' && c != '\\') {
		if (c == '.') {
			if (ni != 8) { i = 8; ni = 11; }
			continue;
		}
		if (c >= 'a' && c <= 'z') c -= 0x20;
		if (i < ni) p[i++] = c;
	}
}


/*-----------------------------------------------------------------------*/
/* Public Functions                                                      */
/*-----------------------------------------------------------------------*/

FRESULT f_mount (FATFS* fs, const TCHAR* path, BYTE opt)
{
	FATFS *cfs;
	int vol;

	vol = 0; /* Assume Drive 0 */
	
	if (fs) {
		fs->fs_type = 0;
		fs->drv = (BYTE)vol;
	}
	FatFs[vol] = fs;

	if (opt == 1) { /* Force mount */
		FRESULT res = check_mount(fs);
		if (res != FR_OK) return res;
	}

	return FR_OK;
}


FRESULT f_open (FIL* fp, const TCHAR* path, BYTE mode)
{
	FRESULT res;
	DIR dj;
	BYTE *dir;
	/* BYTE b; */
	DWORD dw, cl, bcs, sect /*, type */;

	fp->fs = NULL;
	if (!FatFs[0]) return FR_NOT_ENABLED;
	
	dj.fs = FatFs[0];
	dj.fn = dj.fs->win; // Use window buffer for filename temp
	create_name(&dj, path);
	
	/* Check mount */
	res = check_mount(dj.fs);
	if (res != FR_OK) return res;

	
	/* Find File */
	dj.sclust = 0; /* Start from root */
	if (dj.fs->fs_type == 3) dj.sclust = dj.fs->dirbase;
	
	res = dir_find(&dj);

	if (mode & FA_CREATE_ALWAYS) {
		if (res != FR_OK) {
			/* Create new */
			/* Find free entry */
			res = dir_sdi(&dj, 0);
			if (res == FR_OK) {
				do {
					res = move_window(dj.fs, dj.sect);
					if (res != FR_OK) break;
					if (dj.dir[0] == 0 || dj.dir[0] == 0xE5) break; /* Free entry */
					res = dir_sdi(&dj, dj.index + 1);
				} while (res == FR_OK);
			}
			if (res == FR_OK) res = dir_register(&dj);
		} else {
			/* Truncate (simple: just set size to 0) */
			dir = dj.dir;
			*(DWORD*)(dir + 28) = 0;
			cl = *(WORD*)(dir + 26) | (*(WORD*)(dir + 20) << 16);
			remove_chain(dj.fs, cl);
			*(WORD*)(dir + 26) = 0;
			*(WORD*)(dir + 20) = 0;
			dj.fs->wflag = 1;
		}
	}
	else if (mode & FA_OPEN_EXISTING) {
		if (res != FR_OK) return res;
	}
	else if (mode & FA_OPEN_ALWAYS) {
		if (res != FR_OK) {
			/* Create new (same logic as above) */
			res = dir_sdi(&dj, 0);
			if (res == FR_OK) {
				do {
					res = move_window(dj.fs, dj.sect);
					if (res != FR_OK) break;
					if (dj.dir[0] == 0 || dj.dir[0] == 0xE5) break;
					res = dir_sdi(&dj, dj.index + 1);
				} while (res == FR_OK);
			}
			if (res == FR_OK) res = dir_register(&dj);
		}
	}

	if (res == FR_OK) {
		fp->fs = dj.fs;
		fp->id = dj.fs->id;
		fp->flag = mode;
		fp->err = 0;
		fp->fptr = 0;
		fp->dsect = 0;
		
		dir = dj.dir;
		fp->fsize = *(DWORD*)(dir + 28);
		fp->sclust = *(WORD*)(dir + 26) | (*(WORD*)(dir + 20) << 16);
		fp->clust = fp->sclust;
		fp->dir_sect = dj.sect;
		fp->dir_ptr = dj.dir;
	}

	return res;
}


FRESULT f_read (FIL* fp, void* buff, UINT btr, UINT* br)
{
	FRESULT res;
	DWORD clst, sect, remain;
	UINT rcnt, cc;
	BYTE *rbuff = (BYTE*)buff;

	*br = 0;
	if (!fp->fs) return FR_INVALID_OBJECT;
	if (fp->flag & FA_WRITE && !(fp->flag & FA_READ)) return FR_DENIED;

	remain = fp->fsize - fp->fptr;
	if (btr > remain) btr = (UINT)remain;

	for ( ;  btr; /* */ ) {
		if ((fp->fptr % 512) == 0) {
			/* On sector boundary */
			if (fp->dsect == 0 || (fp->fptr % (fp->fs->csize * 512)) == 0) {
				/* On cluster boundary */
				if (fp->fptr == 0) {
					clst = fp->sclust;
				} else {
					clst = get_fat(fp->fs, fp->clust);
				}
				if (clst < 2) break; /* Error */
				fp->clust = clst;
				fp->dsect = clust2sect(fp->fs, clst);
			} else {
				fp->dsect++;
			}
		}
		
		sect = fp->dsect;
		cc = 512 - (fp->fptr % 512);
		if (cc > btr) cc = btr;

		if (move_window(fp->fs, sect) != FR_OK) break;
		mem_cpy(rbuff, &fp->fs->win[fp->fptr % 512], cc);
		
		fp->fptr += cc;
		rbuff += cc;
		btr -= cc;
		*br += cc;
	}

	return FR_OK;
}


FRESULT f_write (FIL* fp, const void* buff, UINT btw, UINT* bw)
{
	FRESULT res;
	DWORD clst, sect;
	UINT wcnt, cc;
	const BYTE *wbuff = (const BYTE*)buff;

	*bw = 0;
	if (!fp->fs) return FR_INVALID_OBJECT;
	if (!(fp->flag & FA_WRITE)) return FR_DENIED;

	for ( ;  btw; /* */ ) {
		if ((fp->fptr % 512) == 0) {
			/* On sector boundary */
			if (fp->dsect == 0 || (fp->fptr % (fp->fs->csize * 512)) == 0) {
				/* On cluster boundary */
				if (fp->fptr == 0) {
					if (fp->sclust == 0) {
						clst = create_chain(fp->fs, 0);
						fp->sclust = clst;
						fp->clust = clst;
						/* Update Dir Entry */
						move_window(fp->fs, fp->dir_sect);
						fp->fs->win[fp->dir_ptr - fp->fs->win + 26] = (BYTE)clst;
						fp->fs->win[fp->dir_ptr - fp->fs->win + 27] = (BYTE)(clst >> 8);
						fp->fs->win[fp->dir_ptr - fp->fs->win + 20] = (BYTE)(clst >> 16);
						fp->fs->win[fp->dir_ptr - fp->fs->win + 21] = (BYTE)(clst >> 24);
						fp->fs->wflag = 1;
					} else {
						clst = fp->clust;
					}
				} else {
					clst = create_chain(fp->fs, fp->clust);
				}
				if (clst == 0xFFFFFFFF) break; /* Full or Error */
				fp->clust = clst;
				fp->dsect = clust2sect(fp->fs, clst);
			} else {
				fp->dsect++;
			}
		}

		sect = fp->dsect;
		cc = 512 - (fp->fptr % 512);
		if (cc > btw) cc = btw;

		if (move_window(fp->fs, sect) != FR_OK) break;
		mem_cpy(&fp->fs->win[fp->fptr % 512], wbuff, cc);
		fp->fs->wflag = 1;

		fp->fptr += cc;
		wbuff += cc;
		btw -= cc;
		*bw += cc;
		
		if (fp->fptr > fp->fsize) {
			fp->fsize = fp->fptr;
			/* Update Dir Entry Size */
			move_window(fp->fs, fp->dir_sect);
			*(DWORD*)&fp->fs->win[fp->dir_ptr - fp->fs->win + 28] = fp->fsize;
			fp->fs->wflag = 1;
		}
	}
	
	sync_fs(fp->fs);

	return FR_OK;
}

FRESULT f_sync (FIL* fp)
{
	if (!fp->fs) return FR_INVALID_OBJECT;
	return sync_fs(fp->fs);
}

FRESULT f_close (FIL* fp)
{
	return f_sync(fp);
}

FRESULT f_lseek (FIL* fp, DWORD ofs)
{
	if (!fp->fs) return FR_INVALID_OBJECT;
	
	if (ofs > fp->fsize) ofs = fp->fsize;
	fp->fptr = ofs;
	
	/* Re-calculate cluster/sector (simplified) */
	/* This simplified version expects sequential write, random seek might fail to update fp->clust */
	/* For append mode (seek end), we iterate chain */
	if (ofs > 0) {
		DWORD clst = fp->sclust;
		DWORD csz = fp->fs->csize * 512;
		DWORD cidx = ofs / csz;
		
		while (cidx--) {
			clst = get_fat(fp->fs, clst);
		}
		fp->clust = clst;
		fp->dsect = clust2sect(fp->fs, clst) + (ofs % csz) / 512;
	}
	
	return FR_OK;
}

FRESULT f_opendir (DIR* dp, const TCHAR* path)
{
	if (!FatFs[0]) return FR_NOT_ENABLED;
	
	dp->fs = FatFs[0];
	FRESULT res = check_mount(dp->fs);
	if (res != FR_OK) return res;

	dp->sclust = 0;
	if (dp->fs->fs_type == 3) dp->sclust = dp->fs->dirbase;
	dp->index = 0;
	
	return FR_OK;
}

FRESULT f_readdir (DIR* dp, FILINFO* fno)
{
	FRESULT res;
	
	if (!fno) return FR_INVALID_PARAMETER;
	
	res = dir_sdi(dp, dp->index);
	if (res != FR_OK) return res;
	
	res = move_window(dp->fs, dp->sect);
	if (res != FR_OK) return res;
	
	BYTE *dir = dp->dir;
	if (dir[0] == 0) return FR_NO_FILE; /* End of dir */
	
	if (dir[0] != 0xE5 && !(dir[11] & 0x08)) { /* Valid file */
		mem_cpy(fno->fname, dir, 8);
		fno->fname[8] = '.';
		mem_cpy(&fno->fname[9], &dir[8], 3);
		fno->fname[12] = 0;
		fno->fsize = *(DWORD*)&dir[28];
	} else {
		/* Skip deleted or label */
		dp->index++;
		return f_readdir(dp, fno);
	}
	
	dp->index++;
	return FR_OK;
}

FRESULT f_getfree (const TCHAR* path, DWORD* nclst, FATFS** fatfs)
{
	FATFS *fs;
	DWORD n = 0, clst;

	/* Get logical drive */
	fs = FatFs[0]; /* Simplified for single drive */
	if (!fs) return FR_NOT_ENABLED;
	
	{
		FRESULT res = check_mount(fs);
		if (res != FR_OK) return res;
	}

	*fatfs = fs;

	/* If free_clst is unknown, scan FAT */
	/* Note: In this simplified version, we assume free_clst is tracked or we scan */
	/* If fs->free_clst is initialized to 0xFFFFFFFF on mount, this works */
	/* But f_mount in this file doesn't seem to init free_clst explicitly to 0xFFFFFFFF unless read from FSINFO */
	/* Let's assume if it is 0xFFFFFFFF or we want to trust it. */
	/* For safety in this patch, we can implement the scan */
	
	/* Simplified scan */
	if (fs->fs_type != 0) {
		clst = 2;
		while (clst < fs->n_fatent) {
			if (get_fat(fs, clst) == 0) n++;
			clst++;
		}
		fs->free_clst = n;
	}
	
	*nclst = fs->free_clst;
	return FR_OK;
}

FRESULT f_closedir (DIR* dp)
{
	dp->fs = 0;
	return FR_OK;
}

int f_puts (const TCHAR* str, FIL* fp)
{
	int n = 0;
	UINT bw;
	while (str[n]) n++;
	f_write(fp, str, n, &bw);
	return bw;
}

