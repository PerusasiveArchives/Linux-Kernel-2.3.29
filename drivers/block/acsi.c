/*
 * acsi.c -- Device driver for Atari ACSI hard disks
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * Some parts are based on hd.c by Linus Torvalds
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive for
 * more details.
 *
 */

/*
 * Still to in this file:
 *  - If a command ends with an error status (!= 0), the following
 *    REQUEST SENSE commands (4 to fill the ST-DMA FIFO) are done by
 *    polling the _IRQ signal (not interrupt-driven). This should be
 *    avoided in future because it takes up a non-neglectible time in
 *    the interrupt service routine while interrupts are disabled.
 *    Maybe a timer interrupt will get lost :-(
 */

/*
 * General notes:
 *
 *  - All ACSI devices (disks, CD-ROMs, ...) use major number 28.
 *    Minors are organized like it is with SCSI: The upper 4 bits
 *    identify the device, the lower 4 bits the partition.
 *    The device numbers (the upper 4 bits) are given in the same
 *    order as the devices are found on the bus.
 *  - Up to 8 LUNs are supported for each target (if CONFIG_ACSI_MULTI_LUN
 *    is defined), but only a total of 16 devices (due to minor
 *    numbers...). Note that Atari allows only a maximum of 4 targets
 *    (i.e. controllers, not devices) on the ACSI bus!
 *  - A optimizing scheme similar to SCSI scatter-gather is implemented.
 *  - Removable media are supported. After a medium change to device
 *    is reinitialized (partition check etc.). Also, if the device
 *    knows the PREVENT/ALLOW MEDIUM REMOVAL command, the door should
 *    be locked and unlocked when mounting the first or unmounting the
 *    last filesystem on the device. The code is untested, because I
 *    don't have a removable hard disk.
 *
 */

#define MAJOR_NR ACSI_MAJOR

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/genhd.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <scsi/scsi.h> /* for SCSI_IOCTL_GET_IDLUN */
typedef void Scsi_Device; /* hack to avoid including scsi.h */
#include <scsi/scsi_ioctl.h>
#include <linux/hdreg.h> /* for HDIO_GETGEO */
#include <linux/blkpg.h>

#include <asm/setup.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/atari_acsi.h>
#include <asm/atari_stdma.h>
#include <asm/atari_stram.h>


#define DEBUG
#undef DEBUG_DETECT
#undef NO_WRITE

#define MAX_ERRORS     		8	/* Max read/write errors/sector */
#define MAX_LUN				8	/* Max LUNs per target */
#define MAX_DEV		   		16

#define ACSI_BUFFER_SIZE			(16*1024) /* "normal" ACSI buffer size */
#define ACSI_BUFFER_MINSIZE			(2048) 	  /* min. buf size if ext. DMA */
#define ACSI_BUFFER_SIZE_ORDER	 	2		  /* order size for above */
#define ACSI_BUFFER_MINSIZE_ORDER	0 	  	  /* order size for above */
#define ACSI_BUFFER_SECTORS	(ACSI_BUFFER_SIZE/512)

#define ACSI_BUFFER_ORDER \
	(ATARIHW_PRESENT(EXTD_DMA) ? \
	 ACSI_BUFFER_MINSIZE_ORDER : \
	 ACSI_BUFFER_SIZE_ORDER)

#define ACSI_TIMEOUT		(4*HZ)

/* minimum delay between two commands */

#define COMMAND_DELAY 500

typedef enum {
	NONE, HARDDISK, CDROM
} ACSI_TYPE;

struct acsi_info_struct {
	ACSI_TYPE		type;			/* type of device */
	unsigned		target;			/* target number */
	unsigned		lun;			/* LUN in target controller */
	unsigned		removable : 1;	/* Flag for removable media */
	unsigned		read_only : 1;	/* Flag for read only devices */
	unsigned		old_atari_disk : 1; /* Is an old Atari disk       */
	unsigned		changed : 1;	/* Medium has been changed */
	unsigned long 	size;			/* #blocks */
} acsi_info[MAX_DEV];

/*
 *	SENSE KEYS
 */

#define NO_SENSE		0x00
#define RECOVERED_ERROR 	0x01
#define NOT_READY		0x02
#define MEDIUM_ERROR		0x03
#define HARDWARE_ERROR		0x04
#define ILLEGAL_REQUEST 	0x05
#define UNIT_ATTENTION		0x06
#define DATA_PROTECT		0x07
#define BLANK_CHECK		0x08
#define COPY_ABORTED		0x0a
#define ABORTED_COMMAND 	0x0b
#define VOLUME_OVERFLOW 	0x0d
#define MISCOMPARE		0x0e


/*
 *	DEVICE TYPES
 */

#define TYPE_DISK	0x00
#define TYPE_TAPE	0x01
#define TYPE_WORM	0x04
#define TYPE_ROM	0x05
#define TYPE_MOD	0x07
#define TYPE_NO_LUN	0x7f

/* The data returned by MODE SENSE differ between the old Atari
 * hard disks and SCSI disks connected to ACSI. In the following, both
 * formats are defined and some macros to operate on them potably.
 */

typedef struct {
	unsigned long	dummy[2];
	unsigned long	sector_size;
	unsigned char	format_code;
#define ATARI_SENSE_FORMAT_FIX	1	
#define ATARI_SENSE_FORMAT_CHNG	2
	unsigned char	cylinders_h;
	unsigned char	cylinders_l;
	unsigned char	heads;
	unsigned char	reduced_h;
	unsigned char	reduced_l;
	unsigned char	precomp_h;
	unsigned char	precomp_l;
	unsigned char	landing_zone;
	unsigned char	steprate;
	unsigned char	type;
#define ATARI_SENSE_TYPE_FIXCHNG_MASK		4
#define ATARI_SENSE_TYPE_SOFTHARD_MASK		8
#define ATARI_SENSE_TYPE_FIX				4
#define ATARI_SENSE_TYPE_CHNG				0
#define ATARI_SENSE_TYPE_SOFT				0
#define ATARI_SENSE_TYPE_HARD				8
	unsigned char	sectors;
} ATARI_SENSE_DATA;

#define ATARI_CAPACITY(sd) \
	(((int)((sd).cylinders_h<<8)|(sd).cylinders_l) * \
	 (sd).heads * (sd).sectors)


typedef struct {
	unsigned char   dummy1;
	unsigned char   medium_type;
	unsigned char   dummy2;
	unsigned char   descriptor_size;
	unsigned long   block_count;
	unsigned long   sector_size;
	/* Page 0 data */
	unsigned char	page_code;
	unsigned char	page_size;
	unsigned char	page_flags;
	unsigned char	qualifier;
} SCSI_SENSE_DATA;

#define SCSI_CAPACITY(sd) 	((sd).block_count & 0xffffff)


typedef union {
	ATARI_SENSE_DATA	atari;
	SCSI_SENSE_DATA		scsi;
} SENSE_DATA;

#define SENSE_TYPE_UNKNOWN	0
#define SENSE_TYPE_ATARI	1
#define SENSE_TYPE_SCSI		2

#define SENSE_TYPE(sd)										\
	(((sd).atari.dummy[0] == 8 &&							\
	  ((sd).atari.format_code == 1 ||						\
	   (sd).atari.format_code == 2)) ? SENSE_TYPE_ATARI :	\
	 ((sd).scsi.dummy1 >= 11) ? SENSE_TYPE_SCSI :			\
	 SENSE_TYPE_UNKNOWN)
	 
#define CAPACITY(sd)							\
	(SENSE_TYPE(sd) == SENSE_TYPE_ATARI ?		\
	 ATARI_CAPACITY((sd).atari) :				\
	 SCSI_CAPACITY((sd).scsi))

#define SECTOR_SIZE(sd)							\
	(SENSE_TYPE(sd) == SENSE_TYPE_ATARI ?		\
	 (sd).atari.sector_size :					\
	 (sd).scsi.sector_size & 0xffffff)

/* Default size if capacity cannot be determined (1 GByte) */
#define	DEFAULT_SIZE	0x1fffff

#define CARTRCH_STAT(dev,buf)					\
	(acsi_info[(dev)].old_atari_disk ?			\
	 (((buf)[0] & 0x7f) == 0x28) :					\
	 ((((buf)[0] & 0x70) == 0x70) ?					\
	  (((buf)[2] & 0x0f) == 0x06) :					\
	  (((buf)[0] & 0x0f) == 0x06)))					\

/* These two are also exported to other drivers that work on the ACSI bus and
 * need an ST-RAM buffer. */
char 			*acsi_buffer;
unsigned long 	phys_acsi_buffer;

static int				NDevices = 0;
static int				acsi_sizes[MAX_DEV<<4] = { 0, };
static int				acsi_blocksizes[MAX_DEV<<4] = { 0, };
static struct hd_struct	acsi_part[MAX_DEV<<4] = { {0,0}, };
static int 				access_count[MAX_DEV] = { 0, };
static char 			busy[MAX_DEV] = { 0, };
static DECLARE_WAIT_QUEUE_HEAD(busy_wait);

static int				CurrentNReq;
static int				CurrentNSect;
static char				*CurrentBuffer;


#define SET_TIMER()	mod_timer(&acsi_timer, jiffies + ACSI_TIMEOUT)
#define CLEAR_TIMER()	del_timer(&acsi_timer)

static unsigned long	STramMask;
#define STRAM_ADDR(a)	(((a) & STramMask) == 0)



/* ACSI commands */

static char tur_cmd[6]        = { 0x00, 0, 0, 0, 0, 0 };
static char modesense_cmd[6]  = { 0x1a, 0, 0, 0, 24, 0 };
static char modeselect_cmd[6] = { 0x15, 0, 0, 0, 12, 0 };
static char inquiry_cmd[6]    = { 0x12, 0, 0, 0,255, 0 };
static char reqsense_cmd[6]   = { 0x03, 0, 0, 0, 4, 0 };
static char read_cmd[6]       = { 0x08, 0, 0, 0, 0, 0 };
static char write_cmd[6]      = { 0x0a, 0, 0, 0, 0, 0 };
static char pa_med_rem_cmd[6] = { 0x1e, 0, 0, 0, 0, 0 };

#define CMDSET_TARG_LUN(cmd,targ,lun)			\
    do {						\
		cmd[0] = (cmd[0] & ~0xe0) | (targ)<<5;	\
		cmd[1] = (cmd[1] & ~0xe0) | (lun)<<5;	\
	} while(0)

#define CMDSET_BLOCK(cmd,blk)						\
    do {											\
		unsigned long __blk = (blk);				\
		cmd[3] = __blk; __blk >>= 8;				\
		cmd[2] = __blk; __blk >>= 8;				\
		cmd[1] = (cmd[1] & 0xe0) | (__blk & 0x1f);	\
	} while(0)

#define CMDSET_LEN(cmd,len)						\
	do {										\
		cmd[4] = (len);							\
	} while(0)

#define min(a,b)	(((a)<(b))?(a):(b))


/* ACSI errors (from REQUEST SENSE); There are two tables, one for the
 * old Atari disks and one for SCSI on ACSI disks.
 */

struct acsi_error {
	unsigned char	code;
	const char		*text;
} atari_acsi_errors[] = {
	{ 0x00, "No error (??)" },
	{ 0x01, "No index pulses" },
	{ 0x02, "Seek not complete" },
	{ 0x03, "Write fault" },
	{ 0x04, "Drive not ready" },
	{ 0x06, "No Track 00 signal" },
	{ 0x10, "ECC error in ID field" },
	{ 0x11, "Uncorrectable data error" },
	{ 0x12, "ID field address mark not found" },
	{ 0x13, "Data field address mark not found" },
	{ 0x14, "Record not found" },
	{ 0x15, "Seek error" },
	{ 0x18, "Data check in no retry mode" },
	{ 0x19, "ECC error during verify" },
	{ 0x1a, "Access to bad block" },
	{ 0x1c, "Unformatted or bad format" },
	{ 0x20, "Invalid command" },
	{ 0x21, "Invalid block address" },
	{ 0x23, "Volume overflow" },
	{ 0x24, "Invalid argument" },
	{ 0x25, "Invalid drive number" },
	{ 0x26, "Byte zero parity check" },
	{ 0x28, "Cartride changed" },
	{ 0x2c, "Error count overflow" },
	{ 0x30, "Controller selftest failed" }
},

	scsi_acsi_errors[] = {
	{ 0x00, "No error (??)" },
	{ 0x01, "Recovered error" },
	{ 0x02, "Drive not ready" },
	{ 0x03, "Uncorrectable medium error" },
	{ 0x04, "Hardware error" },
	{ 0x05, "Illegal request" },
	{ 0x06, "Unit attention (Reset or cartridge changed)" },
	{ 0x07, "Data protection" },
	{ 0x08, "Blank check" },
	{ 0x0b, "Aborted Command" },
	{ 0x0d, "Volume overflow" }
};



/***************************** Prototypes *****************************/

static int acsicmd_dma( const char *cmd, char *buffer, int blocks, int
                        rwflag, int enable);
static int acsi_reqsense( char *buffer, int targ, int lun);
static void acsi_print_error( const unsigned char *errblk, int dev );
static void acsi_interrupt (int irq, void *data, struct pt_regs *fp);
static void unexpected_acsi_interrupt( void );
static void bad_rw_intr( void );
static void read_intr( void );
static void write_intr( void);
static void acsi_times_out( unsigned long dummy );
static void copy_to_acsibuffer( void );
static void copy_from_acsibuffer( void );
static void do_end_requests( void );
static void do_acsi_request( void );
static void redo_acsi_request( void );
static int acsi_ioctl( struct inode *inode, struct file *file, unsigned int
                       cmd, unsigned long arg );
static int acsi_open( struct inode * inode, struct file * filp );
static int acsi_release( struct inode * inode, struct file * file );
static void acsi_prevent_removal( int target, int flag );
static int acsi_change_blk_size( int target, int lun);
static int acsi_mode_sense( int target, int lun, SENSE_DATA *sd );
static void acsi_geninit( struct gendisk *gd );
static int revalidate_acsidisk( int dev, int maxusage );
static int acsi_revalidate (dev_t);

/************************* End of Prototypes **************************/


struct timer_list acsi_timer = { NULL, NULL, 0, 0, acsi_times_out };


#ifdef CONFIG_ATARI_SLM

extern int attach_slm( int target, int lun );
extern int slm_init( void );

#endif



/***********************************************************************
 *
 *   ACSI primitives
 *
 **********************************************************************/


/*
 * The following two functions wait for _IRQ to become Low or High,
 * resp., with a timeout. The 'timeout' parameter is in jiffies
 * (10ms).
 * If the functions are called with timer interrupts on (int level <
 * 6), the timeout is based on the 'jiffies' variable to provide exact
 * timeouts for device probing etc.
 * If interrupts are disabled, the number of tries is based on the
 * 'loops_per_sec' variable. A rough estimation is sufficient here...
 */

#define INT_LEVEL													\
	({	unsigned __sr;												\
		__asm__ __volatile__ ( "movew	%/sr,%0" : "=dm" (__sr) );	\
		(__sr >> 8) & 7;											\
	})

int acsi_wait_for_IRQ( unsigned timeout )

{
	if (INT_LEVEL < 6) {
		unsigned long maxjif = jiffies + timeout;
		while (time_before(jiffies, maxjif))
			if (!(mfp.par_dt_reg & 0x20)) return( 1 );
	}
	else {
		long tries = loops_per_sec / HZ / 8 * timeout;
		while( --tries >= 0 )
			if (!(mfp.par_dt_reg & 0x20)) return( 1 );
	}		
	return( 0 ); /* timeout! */
}


int acsi_wait_for_noIRQ( unsigned timeout )

{
	if (INT_LEVEL < 6) {
		unsigned long maxjif = jiffies + timeout;
		while (time_before(jiffies, maxjif))
			if (mfp.par_dt_reg & 0x20) return( 1 );
	}
	else {
		long tries = loops_per_sec * timeout / HZ / 8;
		while( tries-- >= 0 )
			if (mfp.par_dt_reg & 0x20) return( 1 );
	}		
	return( 0 ); /* timeout! */
}

static struct timeval start_time;

void
acsi_delay_start(void)
{
	do_gettimeofday(&start_time);
}

/* wait from acsi_delay_start to now usec (<1E6) usec */

void
acsi_delay_end(long usec)
{
	struct timeval end_time;
	long deltau,deltas;
	do_gettimeofday(&end_time);
	deltau=end_time.tv_usec - start_time.tv_usec;
	deltas=end_time.tv_sec - start_time.tv_sec;
	if (deltas > 1 || deltas < 0)
		return;
	if (deltas > 0)
		deltau += 1000*1000;
	if (deltau >= usec)
		return;
	udelay(usec-deltau);
}

/* acsicmd_dma() sends an ACSI command and sets up the DMA to transfer
 * 'blocks' blocks of 512 bytes from/to 'buffer'.
 * Because the _IRQ signal is used for handshaking the command bytes,
 * the ACSI interrupt has to be disabled in this function. If the end
 * of the operation should be signalled by a real interrupt, it has to be
 * reenabled afterwards.
 */

static int acsicmd_dma( const char *cmd, char *buffer, int blocks, int rwflag, int enable)

{	unsigned long	flags, paddr;
	int				i;

#ifdef NO_WRITE
	if (rwflag || *cmd == 0x0a) {
		printk( "ACSI: Write commands disabled!\n" );
		return( 0 );
	}
#endif
	
	rwflag = rwflag ? 0x100 : 0;
	paddr = virt_to_phys( buffer );

	acsi_delay_end(COMMAND_DELAY);
	DISABLE_IRQ();

	save_flags(flags);  
	cli();
	/* Low on A1 */
	dma_wd.dma_mode_status = 0x88 | rwflag;
	MFPDELAY();

	/* set DMA address */
	dma_wd.dma_lo = (unsigned char)paddr;
	paddr >>= 8;
	MFPDELAY();
	dma_wd.dma_md = (unsigned char)paddr;
	paddr >>= 8;
	MFPDELAY();
	if (ATARIHW_PRESENT(EXTD_DMA))
		st_dma_ext_dmahi = (unsigned short)paddr;
	else
		dma_wd.dma_hi = (unsigned char)paddr;
	MFPDELAY();
	restore_flags(flags);

	/* send the command bytes except the last */
	for( i = 0; i < 5; ++i ) {
		DMA_LONG_WRITE( *cmd++, 0x8a | rwflag );
		udelay(20);
		if (!acsi_wait_for_IRQ( HZ/2 )) return( 0 ); /* timeout */
	}

	/* Clear FIFO and switch DMA to correct direction */  
	dma_wd.dma_mode_status = 0x92 | (rwflag ^ 0x100);  
	MFPDELAY();
	dma_wd.dma_mode_status = 0x92 | rwflag;
	MFPDELAY();

	/* How many sectors for DMA */
	dma_wd.fdc_acces_seccount = blocks;
	MFPDELAY();
	
	/* send last command byte */
	dma_wd.dma_mode_status = 0x8a | rwflag;
	MFPDELAY();
	DMA_LONG_WRITE( *cmd++, 0x0a | rwflag );
	if (enable)
		ENABLE_IRQ();
	udelay(80);

	return( 1 );
}


/*
 * acsicmd_nodma() sends an ACSI command that requires no DMA.
 */

int acsicmd_nodma( const char *cmd, int enable)

{	int	i;

	acsi_delay_end(COMMAND_DELAY);
	DISABLE_IRQ();

	/* send first command byte */
	dma_wd.dma_mode_status = 0x88;
	MFPDELAY();
	DMA_LONG_WRITE( *cmd++, 0x8a );
	udelay(20);
	if (!acsi_wait_for_IRQ( HZ/2 )) return( 0 ); /* timeout */

	/* send the intermediate command bytes */
	for( i = 0; i < 4; ++i ) {
		DMA_LONG_WRITE( *cmd++, 0x8a );
		udelay(20);
		if (!acsi_wait_for_IRQ( HZ/2 )) return( 0 ); /* timeout */
	}

	/* send last command byte */
	DMA_LONG_WRITE( *cmd++, 0x0a );
	if (enable)
		ENABLE_IRQ();
	udelay(80);
	
	return( 1 );
	/* Note that the ACSI interrupt is still disabled after this
	 * function. If you want to get the IRQ delivered, enable it manually!
	 */
}


static int acsi_reqsense( char *buffer, int targ, int lun)

{
	CMDSET_TARG_LUN( reqsense_cmd, targ, lun);
	if (!acsicmd_dma( reqsense_cmd, buffer, 1, 0, 0 )) return( 0 );
	if (!acsi_wait_for_IRQ( 10 )) return( 0 );
	acsi_getstatus();
	if (!acsicmd_nodma( reqsense_cmd, 0 )) return( 0 );
	if (!acsi_wait_for_IRQ( 10 )) return( 0 );
	acsi_getstatus();
	if (!acsicmd_nodma( reqsense_cmd, 0 )) return( 0 );
	if (!acsi_wait_for_IRQ( 10 )) return( 0 );
	acsi_getstatus();
	if (!acsicmd_nodma( reqsense_cmd, 0 )) return( 0 );
	if (!acsi_wait_for_IRQ( 10 )) return( 0 );
	acsi_getstatus();
	dma_cache_maintenance( virt_to_phys(buffer), 16, 0 );
	
	return( 1 );
}	


/*
 * ACSI status phase: get the status byte from the bus
 *
 * I've seen several times that a 0xff status is read, propably due to
 * a timing error. In this case, the procedure is repeated after the
 * next _IRQ edge.
 */

int acsi_getstatus( void )

{	int	status;

	DISABLE_IRQ();
	for(;;) {
		if (!acsi_wait_for_IRQ( 100 )) {
			acsi_delay_start();
			return( -1 );
		}
		dma_wd.dma_mode_status = 0x8a;
		MFPDELAY();
		status = dma_wd.fdc_acces_seccount;
		if (status != 0xff) break;
#ifdef DEBUG
		printk("ACSI: skipping 0xff status byte\n" );
#endif
		udelay(40);
		acsi_wait_for_noIRQ( 20 );
	}
	dma_wd.dma_mode_status = 0x80;
	udelay(40);
	acsi_wait_for_noIRQ( 20 );

	acsi_delay_start();
	return( status & 0x1f ); /* mask of the device# */
}


#if (defined(CONFIG_ATARI_SLM) || defined(CONFIG_ATARI_SLM_MODULE))

/* Receive data in an extended status phase. Needed by SLM printer. */

int acsi_extstatus( char *buffer, int cnt )

{	int	status;

	DISABLE_IRQ();
	udelay(80);
	while( cnt-- > 0 ) {
		if (!acsi_wait_for_IRQ( 40 )) return( 0 );
		dma_wd.dma_mode_status = 0x8a;
		MFPDELAY();
		status = dma_wd.fdc_acces_seccount;
		MFPDELAY();
		*buffer++ = status & 0xff;
		udelay(40);
	}
	return( 1 );
}


/* Finish an extended status phase */

void acsi_end_extstatus( void )

{
	dma_wd.dma_mode_status = 0x80;
	udelay(40);
	acsi_wait_for_noIRQ( 20 );
	acsi_delay_start();
}


/* Send data in an extended command phase */

int acsi_extcmd( unsigned char *buffer, int cnt )

{
	while( cnt-- > 0 ) {
		DMA_LONG_WRITE( *buffer++, 0x8a );
		udelay(20);
		if (!acsi_wait_for_IRQ( HZ/2 )) return( 0 ); /* timeout */
	}
	return( 1 );
}

#endif


static void acsi_print_error( const unsigned char *errblk, int dev  )

{	int atari_err, i, errcode;
	struct acsi_error *arr;

	atari_err = acsi_info[dev].old_atari_disk;
	if (atari_err)
		errcode = errblk[0] & 0x7f;
	else
		if ((errblk[0] & 0x70) == 0x70)
			errcode = errblk[2] & 0x0f;
		else
			errcode = errblk[0] & 0x0f;
	
	printk( KERN_ERR "ACSI error 0x%02x", errcode );

	if (errblk[0] & 0x80)
		printk( " for sector %d",
				((errblk[1] & 0x1f) << 16) |
				(errblk[2] << 8) | errblk[0] );

	arr = atari_err ? atari_acsi_errors : scsi_acsi_errors;
	i = atari_err ? sizeof(atari_acsi_errors)/sizeof(*atari_acsi_errors) :
		            sizeof(scsi_acsi_errors)/sizeof(*scsi_acsi_errors);
	
	for( --i; i >= 0; --i )
		if (arr[i].code == errcode) break;
	if (i >= 0)
		printk( ": %s\n", arr[i].text );
}

/*******************************************************************
 *
 * ACSI interrupt routine
 *   Test, if this is a ACSI interrupt and call the irq handler
 *   Otherwise ignore this interrupt.
 *
 *******************************************************************/

static void acsi_interrupt(int irq, void *data, struct pt_regs *fp )

{	void (*acsi_irq_handler)(void) = DEVICE_INTR;

	DEVICE_INTR = NULL;
	CLEAR_TIMER();

	if (!acsi_irq_handler)
		acsi_irq_handler = unexpected_acsi_interrupt;
	acsi_irq_handler();
}


/******************************************************************
 *
 * The Interrupt handlers
 *
 *******************************************************************/


static void unexpected_acsi_interrupt( void )

{
	printk( KERN_WARNING "Unexpected ACSI interrupt\n" );
}


/* This function is called in case of errors. Because we cannot reset
 * the ACSI bus or a single device, there is no other choice than
 * retrying several times :-(
 */

static void bad_rw_intr( void )

{
	if (!CURRENT)
		return;

	if (++CURRENT->errors >= MAX_ERRORS)
		end_request(0);
	/* Otherwise just retry */
}


static void read_intr( void )

{	int		status;
	
	status = acsi_getstatus();
	if (status != 0) {
		int dev = DEVICE_NR(MINOR(CURRENT->rq_dev));
		printk( KERN_ERR "ad%c: ", dev+'a' );
		if (!acsi_reqsense( acsi_buffer, acsi_info[dev].target, 
					acsi_info[dev].lun))
			printk( "ACSI error and REQUEST SENSE failed (status=0x%02x)\n", status );
		else {
			acsi_print_error( acsi_buffer, dev );
			if (CARTRCH_STAT( dev, acsi_buffer ))
				acsi_info[dev].changed = 1;
		}
		ENABLE_IRQ();
		bad_rw_intr();
		redo_acsi_request();
		return;
	}

	dma_cache_maintenance( virt_to_phys(CurrentBuffer), CurrentNSect*512, 0 );
	if (CurrentBuffer == acsi_buffer)
		copy_from_acsibuffer();

	do_end_requests();
	redo_acsi_request();
}


static void write_intr(void)

{	int	status;

	status = acsi_getstatus();
	if (status != 0) {
		int	dev = DEVICE_NR(MINOR(CURRENT->rq_dev));
		printk( KERN_ERR "ad%c: ", dev+'a' );
		if (!acsi_reqsense( acsi_buffer, acsi_info[dev].target,
					acsi_info[dev].lun))
			printk( "ACSI error and REQUEST SENSE failed (status=0x%02x)\n", status );
		else {
			acsi_print_error( acsi_buffer, dev );
			if (CARTRCH_STAT( dev, acsi_buffer ))
				acsi_info[dev].changed = 1;
		}
		bad_rw_intr();
		redo_acsi_request();
		return;
	}

	do_end_requests();
	redo_acsi_request();
}


static void acsi_times_out( unsigned long dummy )

{
	DISABLE_IRQ();
	if (!DEVICE_INTR) return;

	DEVICE_INTR = NULL;
	printk( KERN_ERR "ACSI timeout\n" );
	if (!CURRENT) return;
	if (++CURRENT->errors >= MAX_ERRORS) {
#ifdef DEBUG
		printk( KERN_ERR "ACSI: too many errors.\n" );
#endif
		end_request(0);
	}

	redo_acsi_request();
}



/***********************************************************************
 *
 *  Scatter-gather utility functions
 *
 ***********************************************************************/


static void copy_to_acsibuffer( void )

{	int					i;
	char				*src, *dst;
	struct buffer_head	*bh;
	
	src = CURRENT->buffer;
	dst = acsi_buffer;
	bh = CURRENT->bh;

	if (!bh)
		memcpy( dst, src, CurrentNSect*512 );
	else
		for( i = 0; i < CurrentNReq; ++i ) {
			memcpy( dst, src, bh->b_size );
			dst += bh->b_size;
			if ((bh = bh->b_reqnext))
				src = bh->b_data;
		}
}


static void copy_from_acsibuffer( void )

{	int					i;
	char				*src, *dst;
	struct buffer_head	*bh;
	
	dst = CURRENT->buffer;
	src = acsi_buffer;
	bh = CURRENT->bh;

	if (!bh)
		memcpy( dst, src, CurrentNSect*512 );
	else
		for( i = 0; i < CurrentNReq; ++i ) {
			memcpy( dst, src, bh->b_size );
			src += bh->b_size;
			if ((bh = bh->b_reqnext))
				dst = bh->b_data;
		}
}


static void do_end_requests( void )

{	int		i, n;

	if (!CURRENT->bh) {
		CURRENT->nr_sectors -= CurrentNSect;
		CURRENT->current_nr_sectors -= CurrentNSect;
		CURRENT->sector += CurrentNSect;
		if (CURRENT->nr_sectors == 0)
			end_request(1);
	}
	else {
		for( i = 0; i < CurrentNReq; ++i ) {
			n = CURRENT->bh->b_size >> 9;
			CURRENT->nr_sectors -= n;
			CURRENT->current_nr_sectors -= n;
			CURRENT->sector += n;
			end_request(1);
		}
	}
}




/***********************************************************************
 *
 *  do_acsi_request and friends
 *
 ***********************************************************************/

static void do_acsi_request( void )

{
	stdma_lock( acsi_interrupt, NULL );
	redo_acsi_request();
}


static void redo_acsi_request( void )

{	unsigned			block, dev, target, lun, nsect;
	char 				*buffer;
	unsigned long		pbuffer;
	struct buffer_head	*bh;
	
	if (CURRENT && CURRENT->rq_status == RQ_INACTIVE) {
		if (!DEVICE_INTR) {
			ENABLE_IRQ();
			stdma_release();
		}
		return;
	}

	if (DEVICE_INTR)
		return;

  repeat:
	CLEAR_TIMER();
	/* Another check here: An interrupt or timer event could have
	 * happened since the last check!
	 */
	if (CURRENT && CURRENT->rq_status == RQ_INACTIVE) {
		if (!DEVICE_INTR) {
			ENABLE_IRQ();
			stdma_release();
		}
		return;
	}
	if (DEVICE_INTR)
		return;

	if (!CURRENT) {
		CLEAR_INTR;
		ENABLE_IRQ();
		stdma_release();
		return;
	}
	
	if (MAJOR(CURRENT->rq_dev) != MAJOR_NR)
		panic(DEVICE_NAME ": request list destroyed");
	if (CURRENT->bh) {
		if (!CURRENT->bh && !buffer_locked(CURRENT->bh))
			panic(DEVICE_NAME ": block not locked");
	}

	dev = MINOR(CURRENT->rq_dev);
	block = CURRENT->sector;
	if (DEVICE_NR(dev) >= NDevices ||
		block+CURRENT->nr_sectors >= acsi_part[dev].nr_sects) {
#ifdef DEBUG
		printk( "ad%c: attempted access for blocks %d...%ld past end of device at block %ld.\n",
		       DEVICE_NR(dev)+'a',
		       block, block + CURRENT->nr_sectors - 1,
		       acsi_part[dev].nr_sects);
#endif
		end_request(0);
		goto repeat;
	}
	if (acsi_info[DEVICE_NR(dev)].changed) {
		printk( KERN_NOTICE "ad%c: request denied because cartridge has "
				"been changed.\n", DEVICE_NR(dev)+'a' );
		end_request(0);
		goto repeat;
	}
	
	block += acsi_part[dev].start_sect;
	target = acsi_info[DEVICE_NR(dev)].target;
	lun    = acsi_info[DEVICE_NR(dev)].lun;

	/* Find out how many sectors should be transferred from/to
	 * consecutive buffers and thus can be done with a single command.
	 */
	buffer      = CURRENT->buffer;
	pbuffer     = virt_to_phys(buffer);
	nsect       = CURRENT->current_nr_sectors;
	CurrentNReq = 1;

	if ((bh = CURRENT->bh) && bh != CURRENT->bhtail) {
		if (!STRAM_ADDR(pbuffer)) {
			/* If transfer is done via the ACSI buffer anyway, we can
			 * assemble as much bh's as fit in the buffer.
			 */
			while( (bh = bh->b_reqnext) ) {
				if (nsect + (bh->b_size>>9) > ACSI_BUFFER_SECTORS) break;
				nsect += bh->b_size >> 9;
				++CurrentNReq;
				if (bh == CURRENT->bhtail) break;
			}
			buffer = acsi_buffer;
			pbuffer = phys_acsi_buffer;
		}
		else {
			unsigned long pendadr, pnewadr;
			pendadr = pbuffer + nsect*512;
			while( (bh = bh->b_reqnext) ) {
				pnewadr = virt_to_phys(bh->b_data);
				if (!STRAM_ADDR(pnewadr) || pendadr != pnewadr) break;
				nsect += bh->b_size >> 9;
				pendadr = pnewadr + bh->b_size;
				++CurrentNReq;
				if (bh == CURRENT->bhtail) break;
			}
		}
	}
	else {
		if (!STRAM_ADDR(pbuffer)) {
			buffer = acsi_buffer;
			pbuffer = phys_acsi_buffer;
			if (nsect > ACSI_BUFFER_SECTORS)
				nsect = ACSI_BUFFER_SECTORS;
		}
	}
	CurrentBuffer = buffer;
	CurrentNSect  = nsect;
	
	if (CURRENT->cmd == WRITE) {
		CMDSET_TARG_LUN( write_cmd, target, lun );
		CMDSET_BLOCK( write_cmd, block );
		CMDSET_LEN( write_cmd, nsect );
		if (buffer == acsi_buffer)
			copy_to_acsibuffer();
		dma_cache_maintenance( pbuffer, nsect*512, 1 );
		SET_INTR(write_intr);
		if (!acsicmd_dma( write_cmd, buffer, nsect, 1, 1)) {
			CLEAR_INTR;
			printk( KERN_ERR "ACSI (write): Timeout in command block\n" );
			bad_rw_intr();
			goto repeat;
		}
		SET_TIMER();
		return;
	}
	if (CURRENT->cmd == READ) {
		CMDSET_TARG_LUN( read_cmd, target, lun );
		CMDSET_BLOCK( read_cmd, block );
		CMDSET_LEN( read_cmd, nsect );
		SET_INTR(read_intr);
		if (!acsicmd_dma( read_cmd, buffer, nsect, 0, 1)) {
			CLEAR_INTR;
			printk( KERN_ERR "ACSI (read): Timeout in command block\n" );
			bad_rw_intr();
			goto repeat;
		}
		SET_TIMER();
		return;
	}
	panic("unknown ACSI command");
}



/***********************************************************************
 *
 *  Misc functions: ioctl, open, release, check_change, ...
 *
 ***********************************************************************/


static int acsi_ioctl( struct inode *inode, struct file *file,
					   unsigned int cmd, unsigned long arg )
{	int dev;

	if (!inode)
		return -EINVAL;
	dev = DEVICE_NR(MINOR(inode->i_rdev));
	if (dev >= NDevices)
		return -EINVAL;
	switch (cmd) {
	  case HDIO_GETGEO:
		/* HDIO_GETGEO is supported more for getting the partition's
		 * start sector... */
	  { struct hd_geometry *geo = (struct hd_geometry *)arg;
	    /* just fake some geometry here, it's nonsense anyway; to make it
		 * easy, use Adaptec's usual 64/32 mapping */
	    put_user( 64, &geo->heads );
	    put_user( 32, &geo->sectors );
	    put_user( acsi_info[dev].size >> 11, &geo->cylinders );
		put_user( acsi_part[MINOR(inode->i_rdev)].start_sect, &geo->start );
		return 0;
	  }
		
	  case SCSI_IOCTL_GET_IDLUN:
		/* SCSI compatible GET_IDLUN call to get target's ID and LUN number */
		put_user( acsi_info[dev].target | (acsi_info[dev].lun << 8),
				  &((Scsi_Idlun *) arg)->dev_id );
		put_user( 0, &((Scsi_Idlun *) arg)->host_unique_id );
		return 0;
		
	  case BLKGETSIZE:   /* Return device size */
		return put_user(acsi_part[MINOR(inode->i_rdev)].nr_sects,
				(long *) arg);

	  case BLKROSET:
	  case BLKROGET:
	  case BLKFLSBUF:
	  case BLKPG:
		return blk_ioctl(inode->i_rdev, cmd, arg);

	  case BLKRRPART: /* Re-read partition tables */
	        if (!capable(CAP_SYS_ADMIN)) 
			return -EACCES;
		return revalidate_acsidisk(inode->i_rdev, 1);

	  default:
		return -EINVAL;
	}
}


/*
 * Open a device, check for read-only and lock the medium if it is
 * removable.
 *
 * Changes by Martin Rogge, 9th Aug 1995:
 * Check whether check_disk_change (and therefore revalidate_acsidisk)
 * was successful. They fail when there is no medium in the drive.
 *
 * The problem of media being changed during an operation can be 
 * ignored because of the prevent_removal code.
 *
 * Added check for the validity of the device number.
 *
 */

static int acsi_open( struct inode * inode, struct file * filp )
{
	int  device;
	struct acsi_info_struct *aip;

	device = DEVICE_NR(MINOR(inode->i_rdev));
	if (device >= NDevices)
		return -ENXIO;
	aip = &acsi_info[device];
	while (busy[device])
		sleep_on(&busy_wait);

	if (access_count[device] == 0 && aip->removable) {
#if 0
		aip->changed = 1;	/* safety first */
#endif
		check_disk_change( inode->i_rdev );
		if (aip->changed)	/* revalidate was not successful (no medium) */
			return -ENXIO;
		acsi_prevent_removal(device, 1);
	}
	access_count[device]++;
	MOD_INC_USE_COUNT;

	if (filp && filp->f_mode) {
		check_disk_change( inode->i_rdev );
		if (filp->f_mode & 2) {
			if (aip->read_only) {
				acsi_release( inode, filp );
				return -EROFS;
			}
		}
	}

	return 0;
}

/*
 * Releasing a block device means we sync() it, so that it can safely
 * be forgotten about...
 */

static int acsi_release( struct inode * inode, struct file * file )
{
	int device;

	sync_dev(inode->i_rdev);

	device = DEVICE_NR(MINOR(inode->i_rdev));
	if (--access_count[device] == 0 && acsi_info[device].removable)
		acsi_prevent_removal(device, 0);
	MOD_DEC_USE_COUNT;
	return( 0 );
}

/*
 * Prevent or allow a media change for removable devices.
 */

static void acsi_prevent_removal(int device, int flag)
{
	stdma_lock( NULL, NULL );
	
	CMDSET_TARG_LUN(pa_med_rem_cmd, acsi_info[device].target,
			acsi_info[device].lun);
	CMDSET_LEN( pa_med_rem_cmd, flag );
	
	if (acsicmd_nodma(pa_med_rem_cmd, 0) && acsi_wait_for_IRQ(3*HZ))
		acsi_getstatus();
	/* Do not report errors -- some devices may not know this command. */

	ENABLE_IRQ();
	stdma_release();
}

static int acsi_media_change (dev_t dev)
{
	int device = DEVICE_NR(MINOR(dev));
	struct acsi_info_struct *aip;

	aip = &acsi_info[device];
	if (!aip->removable) 
		return 0;

	if (aip->changed)
		/* We can be sure that the medium has been changed -- REQUEST
		 * SENSE has reported this earlier.
		 */
		return 1;

	/* If the flag isn't set, make a test by reading block 0.
	 * If errors happen, it seems to be better to say "changed"...
	 */
	stdma_lock( NULL, NULL );
	CMDSET_TARG_LUN(read_cmd, aip->target, aip->lun);
	CMDSET_BLOCK( read_cmd, 0 );
	CMDSET_LEN( read_cmd, 1 );
	if (acsicmd_dma(read_cmd, acsi_buffer, 1, 0, 0) &&
	    acsi_wait_for_IRQ(3*HZ)) {
		if (acsi_getstatus()) {
			if (acsi_reqsense(acsi_buffer, aip->target, aip->lun)) {
				if (CARTRCH_STAT(device, acsi_buffer))
					aip->changed = 1;
			}
			else {
				printk( KERN_ERR "ad%c: REQUEST SENSE failed in test for "
				       "medium change; assuming a change\n", device + 'a' );
				aip->changed = 1;
			}
		}
	}
	else {
		printk( KERN_ERR "ad%c: Test for medium changed timed out; "
				"assuming a change\n", device + 'a');
		aip->changed = 1;
	}
	ENABLE_IRQ();
	stdma_release();

	/* Now, after reading a block, the changed status is surely valid. */
	return aip->changed;
}


static int acsi_change_blk_size( int target, int lun)

{	int i;

	for (i=0; i<12; i++)
		acsi_buffer[i] = 0;

	acsi_buffer[3] = 8;
	acsi_buffer[10] = 2;
	CMDSET_TARG_LUN( modeselect_cmd, target, lun);

	if (!acsicmd_dma( modeselect_cmd, acsi_buffer, 1,1,0) ||
		!acsi_wait_for_IRQ( 3*HZ ) ||
		acsi_getstatus() != 0 ) {
		return(0);
	}
	return(1);
}


static int acsi_mode_sense( int target, int lun, SENSE_DATA *sd )

{
	int page;

	CMDSET_TARG_LUN( modesense_cmd, target, lun );
	for (page=0; page<4; page++) {
		modesense_cmd[2] = page;
		if (!acsicmd_dma( modesense_cmd, acsi_buffer, 1, 0, 0 ) ||
		    !acsi_wait_for_IRQ( 3*HZ ) ||
		    acsi_getstatus())
			continue;

		/* read twice to jump over the second 16-byte border! */
		udelay(300);
		if (acsi_wait_for_noIRQ( 20 ) &&
		    acsicmd_nodma( modesense_cmd, 0 ) &&
		    acsi_wait_for_IRQ( 3*HZ ) &&
		    acsi_getstatus() == 0)
			break;
	}
	if (page == 4) {
		return(0);
	}

	dma_cache_maintenance( phys_acsi_buffer, sizeof(SENSE_DATA), 0 );
	*sd = *(SENSE_DATA *)acsi_buffer;

	/* Validity check, depending on type of data */
	
	switch( SENSE_TYPE(*sd) ) {

	  case SENSE_TYPE_ATARI:
		if (CAPACITY(*sd) == 0)
			goto invalid_sense;
		break;

	  case SENSE_TYPE_SCSI:
		if (sd->scsi.descriptor_size != 8)
			goto invalid_sense;
		break;

	  case SENSE_TYPE_UNKNOWN:

		printk( KERN_ERR "ACSI target %d, lun %d: Cannot interpret "
				"sense data\n", target, lun ); 
		
	  invalid_sense:

#ifdef DEBUG
		{	int i;
		printk( "Mode sense data for ACSI target %d, lun %d seem not valid:",
				target, lun );
		for( i = 0; i < sizeof(SENSE_DATA); ++i )
			printk( "%02x ", (unsigned char)acsi_buffer[i] );
		printk( "\n" );
		}
#endif
		return( 0 );
	}
		
	return( 1 );
}



/*******************************************************************
 *
 *  Initialization
 *
 ********************************************************************/


static struct gendisk acsi_gendisk = {
	MAJOR_NR,			/* Major number */	
	"ad",				/* Major name */
	4,					/* Bits to shift to get real from partition */
	1 << 4,				/* Number of partitions per real */
	MAX_DEV,			/* maximum number of real */
#ifdef MODULE
	NULL,				/* called from init_module() */
#else
	acsi_geninit,		/* init function */
#endif
	acsi_part,			/* hd struct */
	acsi_sizes,			/* block sizes */
	0,					/* number */
	(void *)acsi_info,	/* internal */
	NULL				/* next */
};
	
#define MAX_SCSI_DEVICE_CODE 10

static const char *const scsi_device_types[MAX_SCSI_DEVICE_CODE] =
{
 "Direct-Access    ",
 "Sequential-Access",
 "Printer          ",
 "Processor        ",
 "WORM             ",
 "CD-ROM           ",
 "Scanner          ",
 "Optical Device   ",
 "Medium Changer   ",
 "Communications   "
};

static void print_inquiry(unsigned char *data)
{
	int i;

	printk(KERN_INFO "  Vendor: ");
	for (i = 8; i < 16; i++)
		{
	        if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
		}

	printk("  Model: ");
	for (i = 16; i < 32; i++)
		{
	        if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
		}

	printk("  Rev: ");
	for (i = 32; i < 36; i++)
		{
	        if (data[i] >= 0x20 && i < data[4] + 5)
			printk("%c", data[i]);
		else
			printk(" ");
		}

	printk("\n");

	i = data[0] & 0x1f;

	printk(KERN_INFO "  Type:   %s ", (i < MAX_SCSI_DEVICE_CODE
									   ? scsi_device_types[i]
									   : "Unknown          "));
	printk("                 ANSI SCSI revision: %02x", data[2] & 0x07);
	if ((data[2] & 0x07) == 1 && (data[3] & 0x0f) == 1)
	  printk(" CCS\n");
	else
	  printk("\n");
}


/* 
 * Changes by Martin Rogge, 9th Aug 1995: 
 * acsi_devinit has been taken out of acsi_geninit, because it needs 
 * to be called from revalidate_acsidisk. The result of request sense 
 * is now checked for DRIVE NOT READY.
 *
 * The structure *aip is only valid when acsi_devinit returns 
 * DEV_SUPPORTED. 
 *
 */
	
#define DEV_NONE	0
#define DEV_UNKNOWN	1
#define DEV_SUPPORTED	2
#define DEV_SLM		3

static int acsi_devinit(struct acsi_info_struct *aip)
{
	int status, got_inquiry;
	SENSE_DATA sense;
	unsigned char reqsense, extsense;

	/*****************************************************************/
	/* Do a TEST UNIT READY command to test the presence of a device */
	/*****************************************************************/

	CMDSET_TARG_LUN(tur_cmd, aip->target, aip->lun);
	if (!acsicmd_nodma(tur_cmd, 0)) {
		/* timed out -> no device here */
#ifdef DEBUG_DETECT
		printk("target %d lun %d: timeout\n", aip->target, aip->lun);
#endif
		return DEV_NONE;
	}
		
	/*************************/
	/* Read the ACSI status. */
	/*************************/

	status = acsi_getstatus();
	if (status) {
		if (status == 0x12) {
			/* The SLM printer should be the only device that
			 * responds with the error code in the status byte. In
			 * correct status bytes, bit 4 is never set.
			 */
			printk( KERN_INFO "Detected SLM printer at id %d lun %d\n",
			       aip->target, aip->lun);
			return DEV_SLM;
		}
		/* ignore CHECK CONDITION, since some devices send a
		   UNIT ATTENTION */
		if ((status & 0x1e) != 0x2) {
#ifdef DEBUG_DETECT
			printk("target %d lun %d: status %d\n",
			       aip->target, aip->lun, status);
#endif
			return DEV_UNKNOWN;
		}
	}

	/*******************************/
	/* Do a REQUEST SENSE command. */
	/*******************************/

	if (!acsi_reqsense(acsi_buffer, aip->target, aip->lun)) {
		printk( KERN_WARNING "acsi_reqsense failed\n");
		acsi_buffer[0] = 0;
		acsi_buffer[2] = UNIT_ATTENTION;
	}
	reqsense = acsi_buffer[0];
	extsense = acsi_buffer[2] & 0xf;
	if (status) {
		if ((reqsense & 0x70) == 0x70) {	/* extended sense */
			if (extsense != UNIT_ATTENTION &&
			    extsense != NOT_READY) {
#ifdef DEBUG_DETECT
				printk("target %d lun %d: extended sense %d\n",
				       aip->target, aip->lun, extsense);
#endif
				return DEV_UNKNOWN;
			}
		}
		else {
			if (reqsense & 0x7f) {
#ifdef DEBUG_DETECT
				printk("target %d lun %d: sense %d\n",
				       aip->target, aip->lun, reqsense);
#endif
				return DEV_UNKNOWN;
			}
		}
	}
	else 
		if (reqsense == 0x4) {	/* SH204 Bug workaround */
#ifdef DEBUG_DETECT
			printk("target %d lun %d status=0 sense=4\n",
			       aip->target, aip->lun);
#endif
			return DEV_UNKNOWN;
		}

	/***********************************************************/
	/* Do an INQUIRY command to get more infos on this device. */
	/***********************************************************/

	/* Assume default values */
	aip->removable = 1;
	aip->read_only = 0;
	aip->old_atari_disk = 0;
	aip->changed = (extsense == NOT_READY);	/* medium inserted? */
	aip->size = DEFAULT_SIZE;
	got_inquiry = 0;
	/* Fake inquiry result for old atari disks */
	memcpy(acsi_buffer, "\000\000\001\000    Adaptec 40xx"
	       "                    ", 40);
	CMDSET_TARG_LUN(inquiry_cmd, aip->target, aip->lun);
	if (acsicmd_dma(inquiry_cmd, acsi_buffer, 1, 0, 0) &&
	    acsi_getstatus() == 0) {
		acsicmd_nodma(inquiry_cmd, 0);
		acsi_getstatus();
		dma_cache_maintenance( phys_acsi_buffer, 256, 0 );
		got_inquiry = 1;
		aip->removable = !!(acsi_buffer[1] & 0x80);
	}
	if (aip->type == NONE)	/* only at boot time */
		print_inquiry(acsi_buffer);
	switch(acsi_buffer[0]) {
	  case TYPE_DISK:
		aip->type = HARDDISK;
		break;
	  case TYPE_ROM:
		aip->type = CDROM;
		aip->read_only = 1;
		break;
	  default:
		return DEV_UNKNOWN;
	}
	/****************************/
	/* Do a MODE SENSE command. */
	/****************************/

	if (!acsi_mode_sense(aip->target, aip->lun, &sense)) {
		printk( KERN_WARNING "No mode sense data.\n" );
		return DEV_UNKNOWN;
	}
	if ((SECTOR_SIZE(sense) != 512) &&
	    ((aip->type != CDROM) ||
	     !acsi_change_blk_size(aip->target, aip->lun) ||
	     !acsi_mode_sense(aip->target, aip->lun, &sense) ||
	     (SECTOR_SIZE(sense) != 512))) {
		printk( KERN_WARNING "Sector size != 512 not supported.\n" );
		return DEV_UNKNOWN;
	}
	/* There are disks out there that claim to have 0 sectors... */
	if (CAPACITY(sense))
		aip->size = CAPACITY(sense);	/* else keep DEFAULT_SIZE */
	if (!got_inquiry && SENSE_TYPE(sense) == SENSE_TYPE_ATARI) {
		/* If INQUIRY failed and the sense data suggest an old
		 * Atari disk (SH20x, Megafile), the disk is not removable
		 */
		aip->removable = 0;
		aip->old_atari_disk = 1;
	}
	
	/******************/
	/* We've done it. */
	/******************/
	
	return DEV_SUPPORTED;
}

EXPORT_SYMBOL(acsi_delay_start);
EXPORT_SYMBOL(acsi_delay_end);
EXPORT_SYMBOL(acsi_wait_for_IRQ);
EXPORT_SYMBOL(acsi_wait_for_noIRQ);
EXPORT_SYMBOL(acsicmd_nodma);
EXPORT_SYMBOL(acsi_getstatus);
EXPORT_SYMBOL(acsi_buffer);
EXPORT_SYMBOL(phys_acsi_buffer);

#ifdef CONFIG_ATARI_SLM_MODULE
void acsi_attach_SLMs( int (*attach_func)( int, int ) );

EXPORT_SYMBOL(acsi_extstatus);
EXPORT_SYMBOL(acsi_end_extstatus);
EXPORT_SYMBOL(acsi_extcmd);
EXPORT_SYMBOL(acsi_attach_SLMs);

/* to remember IDs of SLM devices, SLM module is loaded later
 * (index is target#, contents is lun#, -1 means "no SLM") */
int SLM_devices[8];
#endif

static void acsi_geninit( struct gendisk *gd )
{
	int i, target, lun;
	struct acsi_info_struct *aip;
#ifdef CONFIG_ATARI_SLM
	int n_slm = 0;
#endif

	printk( KERN_INFO "Probing ACSI devices:\n" );
	NDevices = 0;
#ifdef CONFIG_ATARI_SLM_MODULE
	for( i = 0; i < 8; ++i )
		SLM_devices[i] = -1;
#endif
	stdma_lock(NULL, NULL);

	for (target = 0; target < 8 && NDevices < MAX_DEV; ++target) {
		lun = 0;
		do {
			aip = &acsi_info[NDevices];
			aip->type = NONE;
			aip->target = target;
			aip->lun = lun;
			i = acsi_devinit(aip);
			switch (i) {
			  case DEV_SUPPORTED:
				printk( KERN_INFO "Detected ");
				switch (aip->type) {
				  case HARDDISK:
					printk("disk");
					break;
				  case CDROM:
					printk("cdrom");
					break;
				  default:
				}
				printk(" ad%c at id %d lun %d ",
				       'a' + NDevices, target, lun);
				if (aip->removable) 
					printk("(removable) ");
				if (aip->read_only) 
					printk("(read-only) ");
				if (aip->size == DEFAULT_SIZE)
					printk(" unkown size, using default ");
				printk("%ld MByte\n",
				       (aip->size*512+1024*1024/2)/(1024*1024));
				NDevices++;
				break;
			  case DEV_SLM:
#ifdef CONFIG_ATARI_SLM
				n_slm += attach_slm( target, lun );
				break;
#endif
#ifdef CONFIG_ATARI_SLM_MODULE
				SLM_devices[target] = lun;
				break;
#endif
				/* neither of the above: fall through to unknown device */
			  case DEV_UNKNOWN:
				printk( KERN_INFO "Detected unsupported device at "
						"id %d lun %d\n", target, lun);
				break;
			}
		}
#ifdef CONFIG_ACSI_MULTI_LUN
		while (i != DEV_NONE && ++lun < MAX_LUN);
#else
		while (0);
#endif
	}

	/* reenable interrupt */
	ENABLE_IRQ();
	stdma_release();

#ifndef CONFIG_ATARI_SLM
	printk( KERN_INFO "Found %d ACSI device(s) total.\n", NDevices );
#else
	printk( KERN_INFO "Found %d ACSI device(s) and %d SLM printer(s) total.\n",
			NDevices, n_slm );
#endif
					 
	for( i = 0; i < NDevices; ++i ) {
		acsi_part[i<<4].start_sect = 0;
		acsi_part[i<<4].nr_sects = acsi_info[i].size;
	}
	acsi_gendisk.nr_real = NDevices;
	for( i = 0; i < (MAX_DEV << 4); i++ )
		acsi_blocksizes[i] = 1024;
	blksize_size[MAJOR_NR] = acsi_blocksizes;
}

#ifdef CONFIG_ATARI_SLM_MODULE
/* call attach_slm() for each device that is a printer; needed for init of SLM
 * driver as a module, since it's not yet present if acsi.c is inited and thus
 * the bus gets scanned. */
void acsi_attach_SLMs( int (*attach_func)( int, int ) )
{
	int i, n = 0;

	for( i = 0; i < 8; ++i )
		if (SLM_devices[i] >= 0)
			n += (*attach_func)( i, SLM_devices[i] );
	printk( KERN_INFO "Found %d SLM printer(s) total.\n", n );
}
#endif /* CONFIG_ATARI_SLM_MODULE */

static struct file_operations acsi_fops = {
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,	/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	acsi_ioctl,		/* ioctl */
	NULL,			/* mmap */
	acsi_open,		/* open */
	NULL,			/* flush */
	acsi_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	acsi_media_change,	/* media_change */
	acsi_revalidate,	/* revalidate */
};


int acsi_init( void )

{
	if (!MACH_IS_ATARI || !ATARIHW_PRESENT(ACSI))
		return 0;

	if (register_blkdev( MAJOR_NR, "ad", &acsi_fops )) {
		printk( KERN_ERR "Unable to get major %d for ACSI\n", MAJOR_NR );
		return -EBUSY;
	}

	if (!(acsi_buffer =
		  (char *)atari_stram_alloc( ACSI_BUFFER_SIZE, NULL, "acsi" ))) {
		printk( KERN_ERR "Unable to get ACSI ST-Ram buffer.\n" );
		unregister_blkdev( MAJOR_NR, "ad" );
		return -ENOMEM;
	}
	phys_acsi_buffer = virt_to_phys( acsi_buffer );
	STramMask = ATARIHW_PRESENT(EXTD_DMA) ? 0x00000000 : 0xff000000;
	
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;
	read_ahead[MAJOR_NR] = 8;		/* 8 sector (4kB) read-ahead */
	acsi_gendisk.next = gendisk_head;
	gendisk_head = &acsi_gendisk;

#ifdef CONFIG_ATARI_SLM
	return( slm_init() );
#else
	return 0;
#endif
}


#ifdef MODULE
int init_module(void)
{
	int err;

	if ((err = acsi_init()))
		return( err );
	printk( KERN_INFO "ACSI driver loaded as module.\n");
	acsi_geninit( &(struct gendisk){ 0,0,0,0,0,0,0,0,0,0,0 } );
	return( 0 );
}

void cleanup_module(void)
{
	struct gendisk ** gdp;

	del_timer( &acsi_timer );
	blk_dev[MAJOR_NR].request_fn = 0;
	atari_stram_free( acsi_buffer );

	if (unregister_blkdev( MAJOR_NR, "ad" ) != 0)
		printk( KERN_ERR "acsi: cleanup_module failed\n");

	for (gdp = &gendisk_head; *gdp; gdp = &((*gdp)->next))
		if (*gdp == &acsi_gendisk)
			break;
	if (!*gdp)
		printk( KERN_ERR "acsi: entry in disk chain missing!\n" );
	else
		*gdp = (*gdp)->next;
}
#endif

#define DEVICE_BUSY busy[device]
#define USAGE access_count[device]
#define GENDISK_STRUCT acsi_gendisk

/*
 * This routine is called to flush all partitions and partition tables
 * for a changed scsi disk, and then re-read the new partition table.
 * If we are revalidating a disk because of a media change, then we
 * enter with usage == 0.  If we are using an ioctl, we automatically have
 * usage == 1 (we need an open channel to use an ioctl :-), so this
 * is our limit.
 *
 * Changes by Martin Rogge, 9th Aug 1995: 
 * got cd-roms to work by calling acsi_devinit. There are only two problems:
 * First, if there is no medium inserted, the status will remain "changed".
 * That is no problem at all, but our design of three-valued logic (medium
 * changed, medium not changed, no medium inserted).
 * Secondly the check could fail completely and the drive could deliver
 * nonsensical data, which could mess up the acsi_info[] structure. In
 * that case we try to make the entry safe.
 *
 */

static int revalidate_acsidisk( int dev, int maxusage )
{
	int device;
	struct gendisk * gdev;
	int max_p, start, i;
	struct acsi_info_struct *aip;
	
	device = DEVICE_NR(MINOR(dev));
	aip = &acsi_info[device];
	gdev = &GENDISK_STRUCT;

	cli();
	if (DEVICE_BUSY || USAGE > maxusage) {
		sti();
		return -EBUSY;
	};
	DEVICE_BUSY = 1;
	sti();

	max_p = gdev->max_p;
	start = device << gdev->minor_shift;

	for( i = max_p - 1; i >= 0 ; i-- ) {
		if (gdev->part[start + i].nr_sects != 0) {
			kdev_t devp = MKDEV(MAJOR_NR, start + i);
			struct super_block *sb = get_super(devp);

			fsync_dev(devp);
			if (sb)
				invalidate_inodes(sb);
			invalidate_buffers(devp);
			gdev->part[start + i].nr_sects = 0;
		}
		gdev->part[start+i].start_sect = 0;
	};

	stdma_lock( NULL, NULL );

	if (acsi_devinit(aip) != DEV_SUPPORTED) {
		printk( KERN_ERR "ACSI: revalidate failed for target %d lun %d\n",
		       aip->target, aip->lun);
		aip->size = 0;
		aip->read_only = 1;
		aip->removable = 1;
		aip->changed = 1; /* next acsi_open will try again... */
	}

	ENABLE_IRQ();
	stdma_release();
	
	gdev->part[start].nr_sects = aip->size;
	if (aip->type == HARDDISK && aip->size > 0)
		resetup_one_dev(gdev, device);

	DEVICE_BUSY = 0;
	wake_up(&busy_wait);
	return 0;
}


static int acsi_revalidate (dev_t dev)
{
  return revalidate_acsidisk (dev, 0);
}
