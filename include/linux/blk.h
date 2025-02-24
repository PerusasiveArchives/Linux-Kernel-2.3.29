#ifndef _BLK_H
#define _BLK_H

#include <linux/blkdev.h>
#include <linux/locks.h>
#include <linux/config.h>
#include <linux/spinlock.h>

/*
 * Spinlock for protecting the request queue which
 * is mucked around with in interrupts on potentially
 * multiple CPU's..
 */
extern spinlock_t io_request_lock;

/*
 * NR_REQUEST is the number of entries in the request-queue.
 * NOTE that writes may use only the low 2/3 of these: reads
 * take precedence.
 */
#define NR_REQUEST	128

/*
 * This is used in the elevator algorithm.  We don't prioritise reads
 * over writes any more --- although reads are more time-critical than
 * writes, by treating them equally we increase filesystem throughput.
 * This turns out to give better overall performance.  -- sct
 */
#define IN_ORDER(s1,s2) \
((s1)->rq_dev < (s2)->rq_dev || (((s1)->rq_dev == (s2)->rq_dev && \
(s1)->sector < (s2)->sector)))

/*
 * Initialization functions.
 */
extern int isp16_init(void);
extern int cdu31a_init(void);
extern int acsi_init(void);
extern int mcd_init(void);
extern int mcdx_init(void);
extern int sbpcd_init(void);
extern int aztcd_init(void);
extern int sony535_init(void);
extern int gscd_init(void);
extern int cm206_init(void);
extern int optcd_init(void);
extern int sjcd_init(void);
extern int cdi_init(void);
extern int hd_init(void);
extern int ide_init(void);
extern int xd_init(void);
extern int mfm_init(void);
extern int loop_init(void);
extern int md_init(void);
extern int ap_init(void);
extern int ddv_init(void);
extern int z2_init(void);
extern int swim3_init(void);
extern int swimiop_init(void);
extern int amiga_floppy_init(void);
extern int atari_floppy_init(void);
extern int nbd_init(void);
extern int ez_init(void);
extern int bpcd_init(void);
extern int ps2esdi_init(void);

extern void set_device_ro(kdev_t dev,int flag);
void add_blkdev_randomness(int major);

extern int floppy_init(void);
extern void rd_load(void);
extern int rd_init(void);
extern int rd_doload;		/* 1 = load ramdisk, 0 = don't load */
extern int rd_prompt;		/* 1 = prompt for ramdisk, 0 = don't prompt */
extern int rd_image_start;	/* starting block # of image */

#ifdef CONFIG_BLK_DEV_INITRD

#define INITRD_MINOR 250 /* shouldn't collide with /dev/ram* too soon ... */

extern unsigned long initrd_start,initrd_end;
extern int mount_initrd; /* zero if initrd should not be mounted */
extern int initrd_below_start_ok; /* 1 if it is not an error if initrd_start < memory_start */
void initrd_init(void);

#endif

		 
/*
 * end_request() and friends. Must be called with the request queue spinlock
 * acquired. All functions called within end_request() _must_be_ atomic.
 *
 * Several drivers define their own end_request and call
 * end_that_request_first() and end_that_request_last()
 * for parts of the original function. This prevents
 * code duplication in drivers.
 */

int end_that_request_first(struct request *req, int uptodate, char *name);
void end_that_request_last(struct request *req);

#if defined(MAJOR_NR) || defined(IDE_DRIVER)

/*
 * Add entries as needed.
 */

#ifdef IDE_DRIVER

#define DEVICE_NR(device)	(MINOR(device) >> PARTN_BITS)
#define DEVICE_ON(device)	/* nothing */
#define DEVICE_OFF(device)	/* nothing */
#define DEVICE_NAME "ide"

#elif (MAJOR_NR == RAMDISK_MAJOR)

/* ram disk */
#define DEVICE_NAME "ramdisk"
#define DEVICE_REQUEST rd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)
#define DEVICE_NO_RANDOM

#elif (MAJOR_NR == Z2RAM_MAJOR)

/* Zorro II Ram */
#define DEVICE_NAME "Z2RAM"
#define DEVICE_REQUEST do_z2_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == FLOPPY_MAJOR)

static void floppy_off(unsigned int nr);

#define DEVICE_NAME "floppy"
#define DEVICE_INTR do_floppy
#define DEVICE_REQUEST do_fd_request
#define DEVICE_NR(device) ( (MINOR(device) & 3) | ((MINOR(device) & 0x80 ) >> 5 ))
#define DEVICE_ON(device)
#define DEVICE_OFF(device) floppy_off(DEVICE_NR(device))

#elif (MAJOR_NR == HD_MAJOR)

/* Hard disk:  timeout is 6 seconds. */
#define DEVICE_NAME "hard disk"
#define DEVICE_INTR do_hd
#define DEVICE_TIMEOUT HD_TIMER
#define TIMEOUT_VALUE (6*HZ)
#define DEVICE_REQUEST do_hd_request
#define DEVICE_NR(device) (MINOR(device)>>6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (SCSI_DISK_MAJOR(MAJOR_NR))

#define DEVICE_NAME "scsidisk"
#define DEVICE_INTR do_sd  
#define TIMEOUT_VALUE (2*HZ)
#define DEVICE_REQUEST do_sd_request
#define DEVICE_NR(device) (((MAJOR(device) & SD_MAJOR_MASK) << (8 - 4)) + (MINOR(device) >> 4))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

/* Kludge to use the same number for both char and block major numbers */
#elif  (MAJOR_NR == MD_MAJOR) && defined(MD_DRIVER)

#define DEVICE_NAME "Multiple devices driver"
#define DEVICE_REQUEST do_md_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_TAPE_MAJOR)

#define DEVICE_NAME "scsitape"
#define DEVICE_INTR do_st  
#define DEVICE_NR(device) (MINOR(device) & 0x7f)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SCSI_CDROM_MAJOR)

#define DEVICE_NAME "CD-ROM"
#define DEVICE_INTR do_sr
#define DEVICE_REQUEST do_sr_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == XT_DISK_MAJOR)

#define DEVICE_NAME "xt disk"
#define DEVICE_REQUEST do_xd_request
#define DEVICE_NR(device) (MINOR(device) >> 6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == PS2ESDI_MAJOR)

#define DEVICE_NAME "PS/2 ESDI"
#define DEVICE_REQUEST do_ps2esdi_request
#define DEVICE_NR(device) (MINOR(device) >> 6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CDU31A_CDROM_MAJOR)

#define DEVICE_NAME "CDU31A"
#define DEVICE_REQUEST do_cdu31a_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == ACSI_MAJOR) && (defined(CONFIG_ATARI_ACSI) || defined(CONFIG_ATARI_ACSI_MODULE))

#define DEVICE_NAME "ACSI"
#define DEVICE_INTR do_acsi
#define DEVICE_REQUEST do_acsi_request
#define DEVICE_NR(device) (MINOR(device) >> 4)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MITSUMI_CDROM_MAJOR)

#define DEVICE_NAME "Mitsumi CD-ROM"
/* #define DEVICE_INTR do_mcd */
#define DEVICE_REQUEST do_mcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MITSUMI_X_CDROM_MAJOR)

#define DEVICE_NAME "Mitsumi CD-ROM"
/* #define DEVICE_INTR do_mcdx */
#define DEVICE_REQUEST do_mcdx_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #1"
#define DEVICE_REQUEST do_sbpcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM2_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #2"
#define DEVICE_REQUEST do_sbpcd2_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM3_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #3"
#define DEVICE_REQUEST do_sbpcd3_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MATSUSHITA_CDROM4_MAJOR)

#define DEVICE_NAME "Matsushita CD-ROM controller #4"
#define DEVICE_REQUEST do_sbpcd4_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == AZTECH_CDROM_MAJOR)

#define DEVICE_NAME "Aztech CD-ROM"
#define DEVICE_REQUEST do_aztcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CDU535_CDROM_MAJOR)

#define DEVICE_NAME "SONY-CDU535"
#define DEVICE_INTR do_cdu535
#define DEVICE_REQUEST do_cdu535_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == GOLDSTAR_CDROM_MAJOR)

#define DEVICE_NAME "Goldstar R420"
#define DEVICE_REQUEST do_gscd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == CM206_CDROM_MAJOR)
#define DEVICE_NAME "Philips/LMS CD-ROM cm206"
#define DEVICE_REQUEST do_cm206_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == OPTICS_CDROM_MAJOR)

#define DEVICE_NAME "DOLPHIN 8000AT CD-ROM"
#define DEVICE_REQUEST do_optcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == SANYO_CDROM_MAJOR)

#define DEVICE_NAME "Sanyo H94A CD-ROM"
#define DEVICE_REQUEST do_sjcd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == APBLOCK_MAJOR)

#define DEVICE_NAME "apblock"
#define DEVICE_REQUEST ap_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == DDV_MAJOR)

#define DEVICE_NAME "ddv"
#define DEVICE_REQUEST ddv_request
#define DEVICE_NR(device) (MINOR(device)>>PARTN_BITS)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == MFM_ACORN_MAJOR)

#define DEVICE_NAME "mfm disk"
#define DEVICE_INTR do_mfm
#define DEVICE_REQUEST do_mfm_request
#define DEVICE_NR(device) (MINOR(device) >> 6)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == NBD_MAJOR)

#define DEVICE_NAME "nbd"
#define DEVICE_REQUEST do_nbd_request
#define DEVICE_NR(device) (MINOR(device))
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == I2O_MAJOR)

#define DEVICE_NAME "I2O block"
#define DEVICE_REQUEST do_i2ob_request
#define DEVICE_NR(device) (MINOR(device)>>4)
#define DEVICE_ON(device) 
#define DEVICE_OFF(device)

#elif (MAJOR_NR == COMPAQ_SMART2_MAJOR)

#define DEVICE_NAME "ida"
#define DEVICE_INTR do_ida
#define TIMEOUT_VALUE (25*HZ)
#define DEVICE_REQUEST do_ida_request0
#define DEVICE_NR(device) (MINOR(device) >> 4)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#elif (MAJOR_NR == COMPAQ_SMART2_MAJOR)

#define DEVICE_NAME "ida"
#define TIMEOUT_VALUE (25*HZ)
#define DEVICE_REQUEST do_ida_request0
#define DEVICE_NR(device) (MINOR(device) >> 4)
#define DEVICE_ON(device)
#define DEVICE_OFF(device)

#endif /* MAJOR_NR == whatever */

#if (MAJOR_NR != SCSI_TAPE_MAJOR)
#if !defined(IDE_DRIVER)

#ifndef CURRENT
#define CURRENT (blk_dev[MAJOR_NR].current_request)
#endif

#ifndef DEVICE_NAME
#define DEVICE_NAME "unknown"
#endif

#define CURRENT_DEV DEVICE_NR(CURRENT->rq_dev)

#ifdef DEVICE_INTR
static void (*DEVICE_INTR)(void) = NULL;
#endif

#ifdef DEVICE_TIMEOUT

#define SET_TIMER \
((timer_table[DEVICE_TIMEOUT].expires = jiffies + TIMEOUT_VALUE), \
(timer_active |= 1<<DEVICE_TIMEOUT))

#define CLEAR_TIMER \
timer_active &= ~(1<<DEVICE_TIMEOUT)

#define SET_INTR(x) \
if ((DEVICE_INTR = (x)) != NULL) \
	SET_TIMER; \
else \
	CLEAR_TIMER;

#else

#define SET_INTR(x) (DEVICE_INTR = (x))

#endif /* DEVICE_TIMEOUT */

static void (DEVICE_REQUEST)(void);
  
#ifdef DEVICE_INTR
#define CLEAR_INTR SET_INTR(NULL)
#else
#define CLEAR_INTR
#endif

#define INIT_REQUEST \
	if (!CURRENT) {\
		CLEAR_INTR; \
		return; \
	} \
	if (MAJOR(CURRENT->rq_dev) != MAJOR_NR) \
		panic(DEVICE_NAME ": request list destroyed"); \
	if (CURRENT->bh) { \
		if (!buffer_locked(CURRENT->bh)) \
			panic(DEVICE_NAME ": block not locked"); \
	}

#endif /* !defined(IDE_DRIVER) */


#ifndef LOCAL_END_REQUEST	/* If we have our own end_request, we do not want to include this mess */

#if ! SCSI_BLK_MAJOR(MAJOR_NR) && (MAJOR_NR != COMPAQ_SMART2_MAJOR)

static void end_request(int uptodate) {
	struct request *req = CURRENT;

	if (end_that_request_first(req, uptodate, DEVICE_NAME))
		return;

#ifndef DEVICE_NO_RANDOM
	add_blkdev_randomness(MAJOR(req->rq_dev));
#endif
	DEVICE_OFF(req->rq_dev);
	CURRENT = req->next;
	end_that_request_last(req);
}

#endif /* ! SCSI_BLK_MAJOR(MAJOR_NR) */
#endif /* LOCAL_END_REQUEST */

#endif /* (MAJOR_NR != SCSI_TAPE_MAJOR) */
#endif /* defined(MAJOR_NR) || defined(IDE_DRIVER) */

#endif /* _BLK_H */
