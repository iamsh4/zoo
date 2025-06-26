
typedef struct {
  unsigned short EFPAN: 5;
  unsigned short _dummy0: 3;
  unsigned short EFSDL: 4;
  unsigned short _dummy1: 4;
  unsigned short _padding : 16;
} T_AICA_DSP_OUT;

typedef struct {
  // Mark interrupts completed from ARM side
  unsigned char M : 8;
  // 0: arm7 can R/W to DRAM, 1: can only read
  unsigned char RP : 1;
  unsigned char _dummy: 7;
  unsigned short _padding : 16;
} T_AICA_RP_M;


/* GDFS Version */
#define GDD_VERSION_STR  ("GDFS Version 1.06  1999/01/21")
#define GDD_VERSION_LEN  (32)

/* Max length of Filename (removed the version number) */
#define GDD_FS_FNAMESIZE  (30+1)

/* Sector size (Mode 1) */
#define GDD_FS_SCTSIZE  (2048)


/*** Constants ***/


/* Transfer mode */
#define  GDD_FS_TMODE_CPU  (0)
#define  GDD_FS_TMODE_DMA  (1)


/* Seek mode */
#define  GDD_SEEK_SET  (0)
#define  GDD_SEEK_CUR  (1)
#define  GDD_SEEK_END  (2)


/* GDFS Status */
#define  GDD_STAT_IDLE      (0)
#define  GDD_STAT_COMPLETE  (1)
#define  GDD_STAT_READ      (2)
#define  GDD_STAT_SEEK      (3)
#define  GDD_STAT_BUSY      (4)
#define  GDD_STAT_ERR       (5)


/* GDFS Errors */
#define  GDD_ERR_OK          (  0)
#define  GDD_ERR_INIT        ( -1)
#define  GDD_ERR_RESET       ( -2)
#define  GDD_ERR_LIBOV       ( -3)
#define  GDD_ERR_MOUNT       ( -4)
#define  GDD_ERR_DISC        ( -5)
#define  GDD_ERR_DIRREC      ( -6)
#define  GDD_ERR_CANTOPEN    ( -7)
#define  GDD_ERR_NOTFOUND    ( -8)
#define  GDD_ERR_NOHNDL      ( -9)
#define  GDD_ERR_ILLHNDL     (-10)
#define  GDD_ERR_NOTDIR      (-11)
#define  GDD_ERR_DIROVER     (-12)
#define  GDD_ERR_BUSY        (-13)
#define  GDD_ERR_32ALIGN     (-14)
#define  GDD_ERR_SIZE        (-15)
#define  GDD_ERR_SEEK        (-16)
#define  GDD_ERR_OFS         (-17)
#define  GDD_ERR_ILLTMODE    (-18)
#define  GDD_ERR_READ        (-19)
#define  GDD_ERR_NOTREAD     (-20)
#define  GDD_ERR_TOUT        (-21)
#define  GDD_ERR_EOF         (-22)
#define  GDD_ERR_TRAYOPEND   (-23)
#define  GDD_ERR_SIZEOVER    (-24)
#define  GDD_ERR_FATAL       (-25)
#define  GDD_ERR_UNDEF       (-26)
#define  GDD_ERR_NOERR       (-27)
#define  GDD_ERR_RECOVER     (-28)
#define  GDD_ERR_NOTREADY    (-29)
#define  GDD_ERR_MEDIA       (-30)
#define  GDD_ERR_HWARE       (-31)
#define  GDD_ERR_ILLREQ      (-32)
#define  GDD_ERR_UNITATTENT  (-33)
#define  GDD_ERR_PROTECT     (-34)
#define  GDD_ERR_ABORT       (-35)
#define  GDD_ERR_NOREADABLE  (-36)
#define  GDD_ERR_CHECKBUSY   (-37)

/* File flag */
#define  GDD_FF_EXISTENCE        (0x01)
#define  GDD_FF_DIRECTORY        (0x02)
#define  GDD_FF_ASSOCIATED_FILE  (0x04)
#define  GDD_FF_RECORD           (0x08)
#define  GDD_FF_PROTECTION       (0x10)
#define  GDD_FF_MULTI_EXTENT     (0x80)


/* Drive Status */
#define  GDD_DRVSTAT_CANTREAD (-1)
#define  GDD_DRVSTAT_BUSY     (0x00)
#define  GDD_DRVSTAT_PAUSE    (0x01)
#define  GDD_DRVSTAT_STANDBY  (0x02)
#define  GDD_DRVSTAT_PLAY     (0x03)
#define  GDD_DRVSTAT_SEEK     (0x04)
#define  GDD_DRVSTAT_SCAN     (0x05)
#define  GDD_DRVSTAT_OPEN     (0x06)
#define  GDD_DRVSTAT_NODISC   (0x07)
#define  GDD_DRVSTAT_RETRY    (0x08)
#define  GDD_DRVSTAT_ERROR    (0x09)


/* Trans Stat */
#define  GDD_FS_TRANS_READY     (0)
#define  GDD_FS_TRANS_BUSY      (1)
#define  GDD_FS_TRANS_COMPLETE  (2)
#define  GDD_FS_TRANS_ERROR     (3)


/*** Macro functions ***/


/* Get Work Size */
#define gdFsGetWorkSize(x)  (sizeof(GDFS_WORK) + (x) * sizeof(GDFS_HANDLE))
#define gdFsGetDirrecSize(x)  (sizeof(GDFS_DIRREC_TBL) - sizeof(GDFS_DIRREC_ENT) + (x) * sizeof(GDFS_DIRREC_ENT))

#define GDFS_WORK_SIZE(x)  (sizeof(GDFS_WORK) + (x) * sizeof(GDFS_HANDLE))
#define GDFS_DIRREC_SIZE(x)  (sizeof(GDFS_DIRREC_TBL) - sizeof(GDFS_DIRREC_ENT) + (x) * sizeof(GDFS_DIRREC_ENT))


/* Get fsctsize */
#define gdFsCalcSctSize(x)  (((x) + 2047) >> 11)

#define GDFS_GETSCT(x)  (((x) + 2047) >> 11)


/***** Structures *****/

typedef struct GDS_FS_WORK GDFS_WORK;

typedef struct GDS_DCF GD_DCF;

typedef struct GDS_FS_LIF GDFS_LIF;

typedef unsigned int Uint32;
typedef int Sint32;
typedef unsigned short Uint16;
typedef short Sint16;
typedef unsigned char Uint8;
typedef char Sint8;


/* GDFS File Handle */
typedef struct GDS_FS_HANDLE {
	GDFS_WORK *wk;		/* pointer of lib work */
	Sint32 fid;		/* file id */
	Sint32 fad;		/* fad */
	Sint32 fsize;		/* file size */
	Sint32 fsctsize;		/* sctor size of the file */
	Sint32 ofs;		/* offset */
	Sint32 trnsed;		/* transfered size */
	Sint32 rsize;		/* reading size */
	Sint32 trsize;		/* transfering size */
	void (*rdendcb)(void *);		/* read_end callback */
	void *rdcb_1st;		/* read_end callback 1st arg. */
	void (*trendcb)(void *);		/* trans_end callback */
	void *trcb_1st;		/* trans_end callback 1st arg. */
	void (*errcb)(void *, Sint32);		/* error callback */
	void *errcb_1st;		/* error callback 1st arg. */
	Sint32 gdchn;			/* gdc handle */
	volatile Sint32 gdchn_wait;		/* gdc wait type */
	Sint32 ex_errcode;		/* extra error code */
	Sint16 act;		/* handle act */
	Sint16 trflag;		/* transfer flag */
	Sint16 used;		/* used flag */
	Sint16 tmode;		/* transfer mode */
	Sint16 stat;		/* handle status */
	Sint16 err;		/* error status */
} GDFS_HANDLE; /* 84 bytes */
typedef GDFS_HANDLE *GDFS;


/* Directory Record */
typedef struct GDS_FS_DIRREC_ENT {
	Sint32 fad;		/* fad */
	Sint32 fsize;		/* file size */
	Uint8 flag;		/* file attribute */
	Uint8 sid;		/* filename search id */
	Sint8 fname[GDD_FS_FNAMESIZE + 1];		/* filename */
	Sint8 pad[2];
} GDFS_DIRREC_ENT; /* 44 bytes */


/* Directory Record Table */
typedef struct GDS_FS_DIRREC_TBL {
	Sint32 dir_num;		/* number of record */
	Sint32 max_ent;		/* max entry of directory record */
	Sint32 dir_fad;		/* fad of directory record */
	Sint32 pad;			/* padding */
	GDFS_DIRREC_ENT dirrec_tbl[1];	/* record table */
} GDFS_DIRREC_TBL;


/* Directory Record Handle */
typedef GDFS_DIRREC_TBL *GDFS_DIRREC;


/* Directory Info */
typedef struct GDS_FS_DIRINFO {
	Sint32 fad;
	Sint32 fsize;
	Uint8  flag;
	Uint8  pad[3];
} GDFS_DIRINFO; /* 12 bytes */


/* GDFS Work Area */
struct GDS_FS_WORK {
	Sint32 max_open;		/* max open files */
	Sint32 pathtbl_fad;		/* fad of path table */
	Sint32 pathtbl_size;	/* size of path table */
	GD_DCF *dcf;		/* device control function table */
	GDFS_DIRREC curdir;		/* current directory handle */
	GDFS syshdl;		/* handle for system command */
	GDFS hndtbl;		/* handle */
	volatile GDFS now_work;		/* working handle */
	volatile Sint32 f_svr;		/* server flag */
	void (*g_errcb)(void *, Sint32);		/* error callback */
	void *g_errcb_1st;		/* error callback 1st arg. */
	Sint32 gdc_ver;		/* gdc version */
	Sint32 errstat;		/* global error */
	Sint32 istray;		/* tray open flag */
	Sint16 f_init;		/* init flag */
	Sint16 daplayed;		/* da playing flag */
	GDFS_LIF *liftbl;		/* lif table */
	Uint32 sctbuf[(GDD_FS_SCTSIZE*2+64)/4];		/* sector buffer */
	GDFS_HANDLE hndlist[1];		/* handle table */
}; /* 64 + 4160 + handles */


/* Function Type for Callback */
typedef void (*GDFS_FUNC)(void *);
typedef void (*GDFS_ERRFUNC)(void *, Sint32);


/* DA Playing Infomation */
typedef struct GDS_FS_DAINFO {
	Sint32 track;
	Sint32 min;
	Sint32 sec;
	Sint32 frame;
	Sint32 fad;
} GDFS_DAINFO; /* 20 bytes */

