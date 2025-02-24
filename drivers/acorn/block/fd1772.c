/*
 *  linux/kernel/arch/arm/drivers/block/fd1772.c
 *  Based on ataflop.c in the m68k Linux
 *  Copyright (C) 1993  Greg Harp
 *  Atari Support by Bjoern Brauel, Roman Hodek
 *  Archimedes Support by Dave Gilbert (gilbertd@cs.man.ac.uk)
 *
 *  Big cleanup Sep 11..14 1994 Roman Hodek:
 *   - Driver now works interrupt driven
 *   - Support for two drives; should work, but I cannot test that :-(
 *   - Reading is done in whole tracks and buffered to speed up things
 *   - Disk change detection and drive deselecting after motor-off
 *     similar to TOS
 *   - Autodetection of disk format (DD/HD); untested yet, because I
 *     don't have an HD drive :-(
 *
 *  Fixes Nov 13 1994 Martin Schaller:
 *   - Autodetection works now
 *   - Support for 5 1/4" disks
 *   - Removed drive type (unknown on atari)
 *   - Do seeks with 8 Mhz
 *
 *  Changes by Andreas Schwab:
 *   - After errors in multiple read mode try again reading single sectors
 *  (Feb 1995):
 *   - Clean up error handling
 *   - Set blk_size for proper size checking
 *   - Initialize track register when testing presence of floppy
 *   - Implement some ioctl's
 *
 *  Changes by Torsten Lang:
 *   - When probing the floppies we should add the FDC1772CMDADD_H flag since
 *     the FDC1772 will otherwise wait forever when no disk is inserted...
 *
 *  Things left to do:
 *   - Formatting
 *   - Maybe a better strategy for disk change detection (does anyone
 *     know one?)
 *   - There are some strange problems left: The strangest one is
 *     that, at least on my TT (4+4MB), the first 2 Bytes of the last
 *     page of the TT-Ram (!) change their contents (some bits get
 *     set) while a floppy DMA is going on. But there are no accesses
 *     to these memory locations from the kernel... (I tested that by
 *     making the page read-only). I cannot explain what's going on...
 *   - Sometimes the drive-change-detection stops to work. The
 *     function is still called, but the WP bit always reads as 0...
 *     Maybe a problem with the status reg mode or a timing problem.
 *     Note 10/12/94: The change detection now seems to work reliably.
 *     There is no proof, but I've seen no hang for a long time...
 *
 * ARCHIMEDES changes: (gilbertd@cs.man.ac.uk)
 *     26/12/95 - Changed all names starting with FDC to FDC1772
 *                Removed all references to clock speed of FDC - we're stuck with 8MHz
 *                Modified disk_type structure to remove HD formats
 *
 *      7/ 1/96 - Wrote FIQ code, removed most remaining atariisms
 *
 *     13/ 1/96 - Well I think its read a single sector; but there is a problem
 *                fd_rwsec_done which is called in FIQ mode starts another transfer
 *                off (in fd_rwsec) while still in FIQ mode.  Because its still in
 *                FIQ mode it can't service the DMA and loses data. So need to
 *                heavily restructure.
 *     14/ 1/96 - Found that the definitions of the register numbers of the
 *                FDC were multiplied by 2 in the header for the 16bit words
 *                of the atari so half the writes were going in the wrong place.
 *                Also realised that the FIQ entry didn't make any attempt to
 *                preserve registers or return correctly; now in assembler.
 *
 *     11/ 2/96 - Hmm - doesn't work on real machine.  Auto detect doesn't
 *                and hacking that past seems to wait forever - check motor
 *                being turned on.
 *
 *     17/ 2/96 - still having problems - forcing track to -1 when selecting
 *                new drives seems to allow it to read first few sectors
 *                but then we get solid hangs at apparently random places
 *                which change depending what is happening.
 *
 *      9/ 3/96 - Fiddled a lot of stuff around to move to kernel 1.3.35
 *                A lot of fiddling in DMA stuff. Having problems with it
 *                constnatly thinking its timeing out. Ah - its timeout
 *                was set to (6*HZ) rather than jiffies+(6*HZ).  Now giving
 *                duff data!
 *
 *      5/ 4/96 - Made it use the new IOC_ macros rather than *ioc
 *                Hmm - giving unexpected FIQ and then timeouts
 *     18/ 8/96 - Ran through indent -kr -i8
 *                Some changes to disc change detect; don't know how well it
 *                works.
 *     24/ 8/96 - Put all the track buffering code back in from the atari
 *                code - I wonder if it will still work... No :-)
 *                Still works if I turn off track buffering.
 *     25/ 8/96 - Changed the timer expires that I'd added back to be 
 *                jiffies + ....; and it all sprang to life! Got 2.8K/sec
 *                off a cp -r of a 679K disc (showed 94% cpu usage!)
 *                (PC gets 14.3K/sec - 0% CPU!) Hmm - hard drive corrupt!
 *                Also perhaps that compile was with cache off.
 *                changed cli in fd_readtrack_check to cliIF
 *                changed vmallocs to kmalloc (whats the difference!!)
 *                Removed the busy wait loop in do_fd_request and replaced
 *                by a routine on tq_immediate; only 11% cpu on a dd off the
 *                raw disc - but the speed is the same.
 *	1/ 9/96 - Idea (failed!) - set the 'disable spin-up seqeunce'
 *		  when we read the track if we know the motor is on; didn't
 *		  help - perhaps we have to do it in stepping as well.
 *		  Nope. Still doesn't help.
 *		  Hmm - what seems to be happening is that fd_readtrack_check
 *		  is never getting called. Its job is to terminate the read
 *		  just after we think we should have got the data; otherwise
 *		  the fdc takes 1 second to timeout; which is what's happening
 *		  Now I can see 'readtrack_timer' being set (which should do the
 *		  call); but it never seems to be called - hmm!
 *		  OK - I've moved the check to my tq_immediate code -
 *		  and it WORKS! 13.95K/second at 19% CPU.
 *		  I wish I knew why that timer didn't work.....
 *
 *     16/11/96 - Fiddled and frigged for 2.0.18
 *
 * DAG 30/01/99 - Started frobbing for 2.2.1
 * DAG 20/06/99 - A little more frobbing:
 *     Included include/asm/uaccess.h for get_user/put_user
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/timer.h>
#include <linux/tqueue.h>
#include <linux/fd.h>
#include <linux/fd1772.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/mm.h>

#include <asm/arch/oldlatches.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/io.h>
#include <asm/ioc.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/segment.h>
#include <asm/uaccess.h>


#define MAJOR_NR FLOPPY_MAJOR
#define FLOPPY_DMA 0
#include <linux/blk.h>

/* Note: FD_MAX_UNITS could be redefined to 2 for the Atari (with
 * little additional rework in this file). But I'm not yet sure if
 * some other code depends on the number of floppies... (It is defined
 * in a public header!)
 */
#if 0
#undef FD_MAX_UNITS
#define	FD_MAX_UNITS	2
#endif

/* Ditto worries for Arc - DAG */
#define FD_MAX_UNITS 4
#define TRACKBUFFER 0
/*#define DEBUG*/

#ifdef DEBUG
#define DPRINT(a)	printk a
#else
#define DPRINT(a)
#endif

/* Disk types: DD */
static struct archy_disk_type {
	const char *name;
	unsigned spt;		/* sectors per track */
	unsigned blocks;	/* total number of blocks */
	unsigned stretch;	/* track doubling ? */
} disk_type[] = {

	{ "d360", 9, 720, 0 },			/* 360kB diskette */
	{ "D360", 9, 720, 1 },			/* 360kb in 720kb drive */
	{ "D720", 9, 1440, 0 },			/* 720kb diskette (DD) */
	/*{ "D820", 10,1640, 0}, *//* DD disk with 82 tracks/10 sectors 
	                              - DAG - can't see how type detect can distinguish this
				      from 720K until it reads block 4 by which time its too late! */
};

#define	NUM_DISK_TYPES (sizeof(disk_type)/sizeof(*disk_type))

/*
 * Maximum disk size (in kilobytes). This default is used whenever the
 * current disk size is unknown.
 */
#define MAX_DISK_SIZE 720

static int floppy_sizes[256];
static int floppy_blocksizes[256] = {0,};

/* current info on each unit */
static struct archy_floppy_struct {
	int connected;		/* !=0 : drive is connected */
	int autoprobe;		/* !=0 : do autoprobe       */

	struct archy_disk_type *disktype;	/* current type of disk */

	int track;		/* current head position or -1
				   * if unknown */
	unsigned int steprate;	/* steprate setting */
	unsigned int wpstat;	/* current state of WP signal
				   * (for disk change detection) */
} unit[FD_MAX_UNITS];

/* DAG: On Arc we spin on a flag being cleared by fdc1772_comendhandler which
   is an assembler routine */
extern void fdc1772_comendhandler(void);	/* Actually doens't have these parameters - see fd1772.S */
extern volatile int fdc1772_comendstatus;
extern volatile int fdc1772_fdc_int_done;

#define FDC1772BASE ((0x210000>>2)|0x80000000)

#define FDC1772_READ(reg) inb(FDC1772BASE+(reg/2))

/* DAG: You wouldn't be silly to ask why FDC1772_WRITE is a function rather
   than the #def below - well simple - the #def won't compile - and I
   don't understand why (__outwc not defined) */
/* NOTE: Reg is 0,2,4,6 as opposed to 0,1,2,3 or 0,4,8,12 to keep compatibility
   with the ST version of fd1772.h */
/*#define FDC1772_WRITE(reg,val) outw(val,(reg+FDC1772BASE)); */
void FDC1772_WRITE(int reg, unsigned char val)
{
	if (reg == FDC1772REG_CMD) {
		DPRINT(("FDC1772_WRITE new command 0x%x @ %d\n", val,jiffies));
		if (fdc1772_fdc_int_done) {
			DPRINT(("FDC1772_WRITE: Hmm fdc1772_fdc_int_done true - resetting\n"));
			fdc1772_fdc_int_done = 0;
		};
	};
	outb(val, (reg / 2) + FDC1772BASE);
};

#define	MAX_SECTORS	22

unsigned char *DMABuffer;	/* buffer for writes */
/*static unsigned long PhysDMABuffer; *//* physical address */
/* DAG: On Arc we just go straight for the DMA buffer */
#define PhysDMABuffer DMABuffer

#ifdef TRACKBUFFER   
unsigned char *TrackBuffer;       /* buffer for reads */
#define PhysTrackBuffer TrackBuffer /* physical address */
static int BufferDrive, BufferSide, BufferTrack;
static int read_track;    /* non-zero if we are reading whole tracks */
  
#define SECTOR_BUFFER(sec)  (TrackBuffer + ((sec)-1)*512)
#define IS_BUFFERED(drive,side,track) \
    (BufferDrive == (drive) && BufferSide == (side) && BufferTrack == (track))
#endif

/*
 * These are global variables, as that's the easiest way to give
 * information to interrupts. They are the data used for the current
 * request.
 */
static int SelectedDrive = 0;
static int ReqCmd, ReqBlock;
static int ReqSide, ReqTrack, ReqSector, ReqCnt;
static int HeadSettleFlag = 0;
static unsigned char *ReqData, *ReqBuffer;
static int MotorOn = 0, MotorOffTrys;

/* Synchronization of FDC1772 access. */
static volatile int fdc_busy = 0;
static DECLARE_WAIT_QUEUE_HEAD(fdc_wait);


static unsigned int changed_floppies = 0xff, fake_change = 0;
#define	CHECK_CHANGE_DELAY	HZ/2

/* DAG - increased to 30*HZ - not sure if this is the correct thing to do */
#define	FD_MOTOR_OFF_DELAY	(10*HZ)
#define	FD_MOTOR_OFF_MAXTRY	(10*20)

#define FLOPPY_TIMEOUT		(6*HZ)
#define RECALIBRATE_ERRORS	4	/* After this many errors the drive
					 * will be recalibrated. */
#define MAX_ERRORS			8	/* After this many errors the driver
						 * will give up. */


#define	START_MOTOR_OFF_TIMER(delay)			\
    do {						\
        motor_off_timer.expires = jiffies + (delay);		\
        add_timer( &motor_off_timer );			\
        MotorOffTrys = 0;				\
	} while(0)

#define	START_CHECK_CHANGE_TIMER(delay)				\
    do {							\
        timer_table[FLOPPY_TIMER].expires = jiffies + (delay);	\
        timer_active |= (1 << FLOPPY_TIMER);			\
	} while(0)

#define	START_TIMEOUT()					\
    do {						\
        del_timer( &timeout_timer );			\
        timeout_timer.expires = jiffies + FLOPPY_TIMEOUT;	\
        add_timer( &timeout_timer );			\
	} while(0)

#define	STOP_TIMEOUT()					\
    do {						\
        del_timer( &timeout_timer );			\
	} while(0)

#define ENABLE_IRQ() enable_irq(FIQ_FD1772+64);

#define DISABLE_IRQ() disable_irq(FIQ_FD1772+64);

static void fd1772_checkint(void);

struct tq_struct fd1772_tq = 
{ 0,0, (void *)fd1772_checkint, 0 };
/*
 * The driver is trying to determine the correct media format
 * while Probing is set. fd_rwsec_done() clears it after a
 * successful access.
 */
static int Probing = 0;

/* This flag is set when a dummy seek is necesary to make the WP
 * status bit accessible.
 */
static int NeedSeek = 0;


/***************************** Prototypes *****************************/

static void fd_select_side(int side);
static void fd_select_drive(int drive);
static void fd_deselect(void);
static void fd_motor_off_timer(unsigned long dummy);
static void check_change(void);
static __inline__ void set_head_settle_flag(void);
static __inline__ int get_head_settle_flag(void);
static void floppy_irqconsequencehandler(void);
static void fd_error(void);
static void do_fd_action(int drive);
static void fd_calibrate(void);
static void fd_calibrate_done(int status);
static void fd_seek(void);
static void fd_seek_done(int status);
static void fd_rwsec(void);
#ifdef TRACKBUFFER   
static void fd_readtrack_check( unsigned long dummy );  
#endif
static void fd_rwsec_done(int status);
static void fd_times_out(unsigned long dummy);
static void finish_fdc(void);
static void finish_fdc_done(int dummy);
static void floppy_off(unsigned int nr);
static __inline__ void copy_buffer(void *from, void *to);
static void setup_req_params(int drive);
static void redo_fd_request(void);
static int fd_ioctl(struct inode *inode, struct file *filp, unsigned int
		    cmd, unsigned long param);
static void fd_probe(int drive);
static int fd_test_drive_present(int drive);
static void config_types(void);
static int floppy_open(struct inode *inode, struct file *filp);
static void floppy_release(struct inode *inode, struct file *filp);

/************************* End of Prototypes **************************/

static struct timer_list motor_off_timer =
{NULL, NULL, 0, 0, fd_motor_off_timer};
#ifdef TRACKBUFFER
static struct timer_list readtrack_timer =
             { NULL, NULL, 0, 0, fd_readtrack_check };
#endif
static struct timer_list timeout_timer =
{NULL, NULL, 0, 0, fd_times_out};

/* DAG: Haven't got a clue what this is? */
int stdma_islocked(void)
{
	return 0;
};

/* Select the side to use. */

static void fd_select_side(int side)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	oldlatch_aupdate(LATCHA_SIDESEL, side ? 0 : LATCHA_SIDESEL);
	restore_flags(flags);
}


/* Select a drive, update the FDC1772's track register
 */

static void fd_select_drive(int drive)
{
	unsigned long flags;

#ifdef DEBUG
	printk("fd_select_drive:%d\n", drive);
#endif
	/* Hmm - nowhere do we seem to turn the motor on - I'm going to do it here! */
	oldlatch_aupdate(LATCHA_MOTOR | LATCHA_INUSE, 0);

	if (drive == SelectedDrive)
		return;

	save_flags(flags);
	cli();
	oldlatch_aupdate(LATCHA_FDSELALL, 0xf - (1 << drive));
	restore_flags(flags);

	/* restore track register to saved value */
	FDC1772_WRITE(FDC1772REG_TRACK, unit[drive].track);
	udelay(25);

	SelectedDrive = drive;
}


/* Deselect both drives. */

static void fd_deselect(void)
{
	unsigned long flags;

	DPRINT(("fd_deselect\n"));

	save_flags(flags);
	cli();
	oldlatch_aupdate(LATCHA_FDSELALL | LATCHA_MOTOR | LATCHA_INUSE, 0xf | LATCHA_MOTOR | LATCHA_INUSE);
	restore_flags(flags);

	SelectedDrive = -1;
}


/* This timer function deselects the drives when the FDC1772 switched the
 * motor off. The deselection cannot happen earlier because the FDC1772
 * counts the index signals, which arrive only if one drive is selected.
 */

static void fd_motor_off_timer(unsigned long dummy)
{
	unsigned long flags;
	unsigned char status;
	int delay;

	del_timer(&motor_off_timer);

	if (SelectedDrive < 0)
		/* no drive selected, needn't deselect anyone */
		return;

	save_flags(flags);
	cli();

	if (fdc_busy)		/* was stdma_islocked */
		goto retry;

	status = FDC1772_READ(FDC1772REG_STATUS);

	if (!(status & 0x80)) {
		/* motor already turned off by FDC1772 -> deselect drives */
     /* In actual fact its this deselection which turns the motor off on the
        Arc, since the motor control is actually on Latch A */
		DPRINT(("fdc1772: deselecting in fd_motor_off_timer\n"));
		fd_deselect();
		MotorOn = 0;
		restore_flags(flags);
		return;
	}
	/* not yet off, try again */

      retry:
	restore_flags(flags);
	/* Test again later; if tested too often, it seems there is no disk
	 * in the drive and the FDC1772 will leave the motor on forever (or,
	 * at least until a disk is inserted). So we'll test only twice
	 * per second from then on...
	 */
	delay = (MotorOffTrys < FD_MOTOR_OFF_MAXTRY) ?
	    (++MotorOffTrys, HZ / 20) : HZ / 2;
	START_MOTOR_OFF_TIMER(delay);
}


/* This function is repeatedly called to detect disk changes (as good
 * as possible) and keep track of the current state of the write protection.
 */

static void check_change(void)
{
	static int drive = 0;

	unsigned long flags;
	int stat;

	if (fdc_busy)
		return;		/* Don't start poking about if the fdc is busy */

	return;			/* let's just forget it for the mo DAG */

	if (++drive > 1 || !unit[drive].connected)
		drive = 0;

	save_flags(flags);
	cli();

	if (!stdma_islocked()) {
		stat = !!(FDC1772_READ(FDC1772REG_STATUS) & FDC1772STAT_WPROT);

		/* The idea here is that if the write protect line has changed then
		the disc must have changed */
		if (stat != unit[drive].wpstat) {
			DPRINT(("wpstat[%d] = %d\n", drive, stat));
			unit[drive].wpstat = stat;
			set_bit(drive, &changed_floppies);
		}
	}
	restore_flags(flags);

	START_CHECK_CHANGE_TIMER(CHECK_CHANGE_DELAY);
}


/* Handling of the Head Settling Flag: This flag should be set after each
 * seek operation, because we don't use seeks with verify.
 */

static __inline__ void set_head_settle_flag(void)
{
	HeadSettleFlag = FDC1772CMDADD_E;
}

static __inline__ int get_head_settle_flag(void)
{
	int tmp = HeadSettleFlag;
	HeadSettleFlag = 0;
	return (tmp);
}




/* General Interrupt Handling */

static void (*FloppyIRQHandler) (int status) = NULL;

static void floppy_irqconsequencehandler(void)
{
	unsigned char status;
	void (*handler) (int);

	fdc1772_fdc_int_done = 0;

	handler = FloppyIRQHandler;
	FloppyIRQHandler = NULL;

	if (handler) {
		nop();
		status = (unsigned char) fdc1772_comendstatus;
		DPRINT(("FDC1772 irq, status = %02x handler = %08lx\n", (unsigned int) status, (unsigned long) handler));
		handler(status);
	} else {
		DPRINT(("FDC1772 irq, no handler status=%02x\n", fdc1772_comendstatus));
	}
	DPRINT(("FDC1772 irq: end of floppy_irq\n"));
}


/* Error handling: If some error happened, retry some times, then
 * recalibrate, then try again, and fail after MAX_ERRORS.
 */

static void fd_error(void)
{
	printk("FDC1772: fd_error\n");
	/*panic("fd1772: fd_error"); *//* DAG tmp */
	if (!CURRENT)
		return;
	CURRENT->errors++;
	if (CURRENT->errors >= MAX_ERRORS) {
		printk("fd%d: too many errors.\n", SelectedDrive);
		end_request(0);
	} else if (CURRENT->errors == RECALIBRATE_ERRORS) {
		printk("fd%d: recalibrating\n", SelectedDrive);
		if (SelectedDrive != -1)
			unit[SelectedDrive].track = -1;
	}
	redo_fd_request();
}



#define	SET_IRQ_HANDLER(proc) do { FloppyIRQHandler = (proc); } while(0)


/* do_fd_action() is the general procedure for a fd request: All
 * required parameter settings (drive select, side select, track
 * position) are checked and set if needed. For each of these
 * parameters and the actual reading or writing exist two functions:
 * one that starts the setting (or skips it if possible) and one
 * callback for the "done" interrupt. Each done func calls the next
 * set function to propagate the request down to fd_rwsec_done().
 */

static void do_fd_action(int drive)
{
	DPRINT(("do_fd_action unit[drive].track=%d\n", unit[drive].track));

#ifdef TRACKBUFFER
  repeat:

  if (IS_BUFFERED( drive, ReqSide, ReqTrack )) {
    if (ReqCmd == READ) {
      copy_buffer( SECTOR_BUFFER(ReqSector), ReqData );
      if (++ReqCnt < CURRENT->current_nr_sectors) {
        /* read next sector */
        setup_req_params( drive );
        goto repeat;
      }
      else {
        /* all sectors finished */
        CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
        CURRENT->sector += CURRENT->current_nr_sectors;
        end_request( 1 );
        redo_fd_request();
        return;
      }
    }
    else {
      /* cmd == WRITE, pay attention to track buffer
       * consistency! */
      copy_buffer( ReqData, SECTOR_BUFFER(ReqSector) );
    }
  }
#endif

	if (SelectedDrive != drive) {
		/*unit[drive].track = -1; DAG */
		fd_select_drive(drive);
	};


	if (unit[drive].track == -1)
		fd_calibrate();
	else if (unit[drive].track != ReqTrack << unit[drive].disktype->stretch)
		fd_seek();
	else
		fd_rwsec();
}


/* Seek to track 0 if the current track is unknown */

static void fd_calibrate(void)
{
	DPRINT(("fd_calibrate\n"));
	if (unit[SelectedDrive].track >= 0) {
		fd_calibrate_done(0);
		return;
	}
	DPRINT(("fd_calibrate (after track compare)\n"));
	SET_IRQ_HANDLER(fd_calibrate_done);
	/* we can't verify, since the speed may be incorrect */
	FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_RESTORE | unit[SelectedDrive].steprate);

	NeedSeek = 1;
	MotorOn = 1;
	START_TIMEOUT();
	/* wait for IRQ */
}


static void fd_calibrate_done(int status)
{
	DPRINT(("fd_calibrate_done()\n"));
	STOP_TIMEOUT();

	/* set the correct speed now */
	if (status & FDC1772STAT_RECNF) {
		printk("fd%d: restore failed\n", SelectedDrive);
		fd_error();
	} else {
		unit[SelectedDrive].track = 0;
		fd_seek();
	}
}


/* Seek the drive to the requested track. The drive must have been
 * calibrated at some point before this.
 */

static void fd_seek(void)
{
  unsigned long flags;
	DPRINT(("fd_seek() to track %d (unit[SelectedDrive].track=%d)\n", ReqTrack,
		unit[SelectedDrive].track));
	if (unit[SelectedDrive].track == ReqTrack <<
	    unit[SelectedDrive].disktype->stretch) {
		fd_seek_done(0);
		return;
	}
	FDC1772_WRITE(FDC1772REG_DATA, ReqTrack <<
		      unit[SelectedDrive].disktype->stretch);
	udelay(25);
  save_flags(flags);
  cliIF();
	SET_IRQ_HANDLER(fd_seek_done);
	FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_SEEK | unit[SelectedDrive].steprate |
		/* DAG */
		(MotorOn?FDC1772CMDADD_H:0));

  restore_flags(flags);
	MotorOn = 1;
	set_head_settle_flag();
	START_TIMEOUT();
	/* wait for IRQ */
}


static void fd_seek_done(int status)
{
	DPRINT(("fd_seek_done()\n"));
	STOP_TIMEOUT();

	/* set the correct speed */
	if (status & FDC1772STAT_RECNF) {
		printk("fd%d: seek error (to track %d)\n",
		       SelectedDrive, ReqTrack);
		/* we don't know exactly which track we are on now! */
		unit[SelectedDrive].track = -1;
		fd_error();
	} else {
		unit[SelectedDrive].track = ReqTrack <<
		    unit[SelectedDrive].disktype->stretch;
		NeedSeek = 0;
		fd_rwsec();
	}
}


/* This does the actual reading/writing after positioning the head
 * over the correct track.
 */

#ifdef TRACKBUFFER
static int MultReadInProgress = 0;
#endif


static void fd_rwsec(void)
{
	unsigned long paddr, flags;
	unsigned int rwflag, old_motoron;
	unsigned int track;

	DPRINT(("fd_rwsec(), Sec=%d, Access=%c\n", ReqSector, ReqCmd == WRITE ? 'w' : 'r'));
	if (ReqCmd == WRITE) {
		/*cache_push( (unsigned long)ReqData, 512 ); */
		paddr = (unsigned long) ReqData;
		rwflag = 0x100;
	} else {
#ifdef TRACKBUFFER
    if (read_track)
      paddr = (unsigned long)PhysTrackBuffer;
    else
      paddr =(unsigned long)PhysDMABuffer;
#else
    paddr = (unsigned long)PhysDMABuffer;
#endif
		rwflag = 0;
	}

	DPRINT(("fd_rwsec() before sidesel rwflag=%d sec=%d trk=%d\n", rwflag,
		ReqSector, FDC1772_READ(FDC1772REG_TRACK)));
	fd_select_side(ReqSide);

	/*DPRINT(("fd_rwsec() before start sector \n")); */
	/* Start sector of this operation */
#ifdef TRACKBUFFER
  FDC1772_WRITE( FDC1772REG_SECTOR, !read_track ? ReqSector : 1 );
#else
  FDC1772_WRITE( FDC1772REG_SECTOR, ReqSector );
#endif

	/* Cheat for track if stretch != 0 */
	if (unit[SelectedDrive].disktype->stretch) {
		track = FDC1772_READ(FDC1772REG_TRACK);
		FDC1772_WRITE(FDC1772REG_TRACK, track >>
			      unit[SelectedDrive].disktype->stretch);
	}
	udelay(25);

	DPRINT(("fd_rwsec() before setup DMA \n"));
	/* Setup DMA - Heavily modified by DAG */
	save_flags(flags);
	cliIF();
	disable_dma(FLOPPY_DMA);
	set_dma_mode(FLOPPY_DMA, rwflag ? DMA_MODE_WRITE : DMA_MODE_READ);
	set_dma_addr(FLOPPY_DMA, (long) paddr);		/* DAG - changed from Atari specific */
#ifdef TRACKBUFFER
  set_dma_count(FLOPPY_DMA,(!read_track ? 1 : unit[SelectedDrive].disktype->spt)*512);
#else
	set_dma_count(FLOPPY_DMA, 512);		/* Block/sector size - going to have to change */
#endif
	SET_IRQ_HANDLER(fd_rwsec_done);
	/* Turn on dma int */
	enable_dma(FLOPPY_DMA);
	/* Now give it something to do */
	FDC1772_WRITE(FDC1772REG_CMD, (rwflag ? (FDC1772CMD_WRSEC | FDC1772CMDADD_P) : 
#ifdef TRACKBUFFER
              (FDC1772CMD_RDSEC | (read_track ? FDC1772CMDADD_M : 0) |
	      /* Hmm - the idea here is to stop the FDC spinning the disc
	      up when we know that we already still have it spinning */
	      (MotorOn?FDC1772CMDADD_H:0))
#else
              FDC1772CMD_RDSEC
#endif
    ));

	restore_flags(flags);
	DPRINT(("fd_rwsec() after DMA setup flags=0x%08x\n", flags));
	/*sti(); *//* DAG - Hmm */
	/* Hmm - should do something DAG */
	old_motoron = MotorOn;
	MotorOn = 1;
	NeedSeek = 1;

	/* wait for interrupt */

#ifdef TRACKBUFFER
  if (read_track) {
    /* If reading a whole track, wait about one disk rotation and
     * then check if all sectors are read. The FDC will even
     * search for the first non-existant sector and need 1 sec to
     * recognise that it isn't present :-(
     */
    del_timer( &readtrack_timer );
    readtrack_timer.function = fd_readtrack_check;
    readtrack_timer.expires = jiffies + HZ/5 + (old_motoron ? 0 : HZ);
                              /* 1 rot. + 5 rot.s if motor was off  */
    DPRINT(("Setting readtrack_timer to %d @ %d\n",readtrack_timer.expires,jiffies));
    add_timer( &readtrack_timer );
    MultReadInProgress = 1;
 }
#endif

	/*DPRINT(("fd_rwsec() before START_TIMEOUT \n")); */
	START_TIMEOUT();
	/*DPRINT(("fd_rwsec() after START_TIMEOUT \n")); */
}


#ifdef TRACKBUFFER

static void fd_readtrack_check( unsigned long dummy )

{ unsigned long flags, addr;
  extern unsigned char *fdc1772_dataaddr;

  DPRINT(("fd_readtrack_check @ %d\n",jiffies));

  save_flags(flags);
  cliIF();

  del_timer( &readtrack_timer );

  if (!MultReadInProgress) {
    /* This prevents a race condition that could arise if the
     * interrupt is triggered while the calling of this timer
     * callback function takes place. The IRQ function then has
     * already cleared 'MultReadInProgress'  when control flow
     * gets here.
     */
        restore_flags(flags);
    return;
  }

  /* get the current DMA address */
  addr=fdc1772_dataaddr; /* DAG - ? */
  DPRINT(("fd_readtrack_check: addr=%x PhysTrackBuffer=%x\n",addr,PhysTrackBuffer));

  if (addr >= PhysTrackBuffer + unit[SelectedDrive].disktype->spt*512) {
    /* already read enough data, force an FDC interrupt to stop
     * the read operation
     */
    SET_IRQ_HANDLER( NULL );
    restore_flags(flags);
    DPRINT(("fd_readtrack_check(): done\n"));
    FDC1772_WRITE( FDC1772REG_CMD, FDC1772CMD_FORCI );
    udelay(25);

    /* No error until now -- the FDC would have interrupted
     * otherwise!
     */
    fd_rwsec_done( 0 );
  }
  else {
    /* not yet finished, wait another tenth rotation */
    restore_flags(flags);
    DPRINT(("fd_readtrack_check(): not yet finished\n"));
    readtrack_timer.expires = jiffies + HZ/5/10;
    add_timer( &readtrack_timer );
  }
}

#endif

static void fd_rwsec_done(int status)
{
	unsigned int track;

	DPRINT(("fd_rwsec_done() status=%d @ %d\n", status,jiffies));

#ifdef TRACKBUFFER
  if (read_track && !MultReadInProgress) return;
  MultReadInProgress = 0;

  STOP_TIMEOUT();

  if (read_track)
    del_timer( &readtrack_timer );
#endif


	/* Correct the track if stretch != 0 */
	if (unit[SelectedDrive].disktype->stretch) {
		track = FDC1772_READ(FDC1772REG_TRACK);
		FDC1772_WRITE(FDC1772REG_TRACK, track <<
			      unit[SelectedDrive].disktype->stretch);
	}
	if (ReqCmd == WRITE && (status & FDC1772STAT_WPROT)) {
		printk("fd%d: is write protected\n", SelectedDrive);
		goto err_end;
	}
	if ((status & FDC1772STAT_RECNF)
#ifdef TRACKBUFFER
    /* RECNF is no error after a multiple read when the FDC
     * searched for a non-existant sector!
     */
    && !(read_track &&
       FDC1772_READ(FDC1772REG_SECTOR) > unit[SelectedDrive].disktype->spt)
#endif
    ) {
		if (Probing) {
			if (unit[SelectedDrive].disktype > disk_type) {
				/* try another disk type */
				unit[SelectedDrive].disktype--;
				floppy_sizes[SelectedDrive]
				    = unit[SelectedDrive].disktype->blocks >> 1;
			} else
				Probing = 0;
		} else {
			/* record not found, but not probing. Maybe stretch wrong ? Restart probing */
			if (unit[SelectedDrive].autoprobe) {
				unit[SelectedDrive].disktype = disk_type + NUM_DISK_TYPES - 1;
				floppy_sizes[SelectedDrive]
				    = unit[SelectedDrive].disktype->blocks >> 1;
				Probing = 1;
			}
		}
		if (Probing) {
			setup_req_params(SelectedDrive);
#ifdef TRACKBUFFER
      BufferDrive = -1;
#endif
			do_fd_action(SelectedDrive);
			return;
		}
		printk("fd%d: sector %d not found (side %d, track %d)\n",
		       SelectedDrive, FDC1772_READ(FDC1772REG_SECTOR), ReqSide, ReqTrack);
		goto err_end;
	}
	if (status & FDC1772STAT_CRC) {
		printk("fd%d: CRC error (side %d, track %d, sector %d)\n",
		       SelectedDrive, ReqSide, ReqTrack, FDC1772_READ(FDC1772REG_SECTOR));
		goto err_end;
	}
	if (status & FDC1772STAT_LOST) {
		printk("fd%d: lost data (side %d, track %d, sector %d)\n",
		       SelectedDrive, ReqSide, ReqTrack, FDC1772_READ(FDC1772REG_SECTOR));
		goto err_end;
	}
	Probing = 0;

	if (ReqCmd == READ) {
#ifdef TRACKBUFFER
    if (!read_track)
      {
        /*cache_clear (PhysDMABuffer, 512);*/
        copy_buffer (DMABuffer, ReqData);
      }
    else
      {
        /*cache_clear (PhysTrackBuffer, MAX_SECTORS * 512);*/
        BufferDrive = SelectedDrive;
        BufferSide  = ReqSide;
        BufferTrack = ReqTrack;
        copy_buffer (SECTOR_BUFFER (ReqSector), ReqData);
      }
#else
		/*cache_clear( PhysDMABuffer, 512 ); */
		copy_buffer(DMABuffer, ReqData);
#endif
	}
	if (++ReqCnt < CURRENT->current_nr_sectors) {
		/* read next sector */
		setup_req_params(SelectedDrive);
		do_fd_action(SelectedDrive);
	} else {
		/* all sectors finished */
		CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
		CURRENT->sector += CURRENT->current_nr_sectors;
		end_request(1);
		redo_fd_request();
	}
	return;

  err_end:
#ifdef TRACKBUFFER
  BufferDrive = -1;
#endif

	fd_error();
}


static void fd_times_out(unsigned long dummy)
{
	SET_IRQ_HANDLER(NULL);
	/* If the timeout occurred while the readtrack_check timer was
	 * active, we need to cancel it, else bad things will happen */
	del_timer( &readtrack_timer ); 
	FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_FORCI);
	udelay(25);

	printk("floppy timeout\n");
	STOP_TIMEOUT();		/* hmm - should we do this ? */
	fd_error();
}


/* The (noop) seek operation here is needed to make the WP bit in the
 * FDC1772 status register accessible for check_change. If the last disk
 * operation would have been a RDSEC, this bit would always read as 0
 * no matter what :-( To save time, the seek goes to the track we're
 * already on.
 */

static void finish_fdc(void)
{
	/* DAG - just try without this dummy seek! */
	finish_fdc_done(0);
	return;

	if (!NeedSeek) {
		finish_fdc_done(0);
	} else {
		DPRINT(("finish_fdc: dummy seek started\n"));
		FDC1772_WRITE(FDC1772REG_DATA, unit[SelectedDrive].track);
		SET_IRQ_HANDLER(finish_fdc_done);
		FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_SEEK);
		MotorOn = 1;
		START_TIMEOUT();
		/* we must wait for the IRQ here, because the ST-DMA is
		 * released immediatly afterwards and the interrupt may be
		 * delivered to the wrong driver.
		 */
	}
}


static void finish_fdc_done(int dummy)
{
	unsigned long flags;

	DPRINT(("finish_fdc_done entered\n"));
	STOP_TIMEOUT();
	NeedSeek = 0;

	if ((timer_active & (1 << FLOPPY_TIMER)) &&
	    time_after(jiffies + 5, timer_table[FLOPPY_TIMER].expires)) 
		/* If the check for a disk change is done too early after this
		 * last seek command, the WP bit still reads wrong :-((
		 */
		timer_table[FLOPPY_TIMER].expires = jiffies + 5;
	else {
		/*      START_CHECK_CHANGE_TIMER( CHECK_CHANGE_DELAY ); */
	};
	del_timer(&motor_off_timer);
	START_MOTOR_OFF_TIMER(FD_MOTOR_OFF_DELAY);

	save_flags(flags);
	cli();
	/* stdma_release(); - not sure if I should do something DAG  */
	fdc_busy = 0;
	wake_up(&fdc_wait);
	restore_flags(flags);

	DPRINT(("finish_fdc() finished\n"));
}


/* Prevent "aliased" accesses. */
static fd_ref[4] =
{0, 0, 0, 0};
static fd_device[4] =
{0, 0, 0, 0};

/*
 * Current device number. Taken either from the block header or from the
 * format request descriptor.
 */
#define CURRENT_DEVICE (CURRENT->rq_dev)

/* Current error count. */
#define CURRENT_ERRORS (CURRENT->errors)


/* dummy for blk.h */
static void floppy_off(unsigned int nr)
{
}


/* On the old arcs write protect depends on the particular model
   of machine.  On the A310, R140, and A440 there is a disc changed
   detect, however on the A4x0/1 range there is not.  There
   is nothing to tell you which machine your on.
   At the moment I'm just marking changed always. I've
   left the Atari's 'change on write protect change' code in this
   part (but nothing sets it).
   RiscOS apparently checks the disc serial number etc. to detect changes
   - but if it sees a disc change line go high (?) it flips to using
   it. Well  maybe I'll add that in the future (!?)
*/
static int check_floppy_change(dev_t dev)
{
	unsigned int drive = (dev & 0x03);

	if (MAJOR(dev) != MAJOR_NR) {
		printk("floppy_changed: not a floppy\n");
		return 0;
	}
	if (test_bit(drive, &fake_change)) {
		/* simulated change (e.g. after formatting) */
		return 1;
	}
	if (test_bit(drive, &changed_floppies)) {
		/* surely changed (the WP signal changed at least once) */
		return 1;
	}
	if (unit[drive].wpstat) {
		/* WP is on -> could be changed: to be sure, buffers should be
		   * invalidated...
		 */
		return 1;
	}
	return 1; /* DAG - was 0 */
}

static int floppy_revalidate(dev_t dev)
{
	int drive = dev & 3;

	if (test_bit(drive, &changed_floppies) || test_bit(drive, &fake_change)
	    || unit[drive].disktype == 0) {
#ifdef TRACKBUFFER
      BufferDrive = -1;
#endif
		clear_bit(drive, &fake_change);
		clear_bit(drive, &changed_floppies);
		unit[drive].disktype = 0;
	}
	return 0;
}

static __inline__ void copy_buffer(void *from, void *to)
{
	ulong *p1 = (ulong *) from, *p2 = (ulong *) to;
	int cnt;

	for (cnt = 512 / 4; cnt; cnt--)
		*p2++ = *p1++;
}


/* This sets up the global variables describing the current request. */

static void setup_req_params(int drive)
{
	int block = ReqBlock + ReqCnt;

	ReqTrack = block / unit[drive].disktype->spt;
	ReqSector = block - ReqTrack * unit[drive].disktype->spt + 1;
	ReqSide = ReqTrack & 1;
	ReqTrack >>= 1;
	ReqData = ReqBuffer + 512 * ReqCnt;

#ifdef TRACKBUFFER
  read_track = (ReqCmd == READ && CURRENT_ERRORS == 0);
#endif

	DPRINT(("Request params: Si=%d Tr=%d Se=%d Data=%08lx\n", ReqSide,
		ReqTrack, ReqSector, (unsigned long) ReqData));
}


static void redo_fd_request(void)
{
	int device, drive, type;
	struct archy_floppy_struct *floppy;

	DPRINT(("redo_fd_request: CURRENT=%08lx CURRENT->rq_dev=%04x CURRENT->sector=%ld\n",
		(unsigned long) CURRENT, CURRENT ? CURRENT->rq_dev : 0,
		CURRENT ? CURRENT->sector : 0));

	if (CURRENT && CURRENT->rq_status == RQ_INACTIVE)
		goto the_end;

      repeat:

	if (!CURRENT)
		goto the_end;

	if (MAJOR(CURRENT->rq_dev) != MAJOR_NR)
		panic(DEVICE_NAME ": request list destroyed");

	if (CURRENT->bh) {
		if (!buffer_locked(CURRENT->bh))
			panic(DEVICE_NAME ": block not locked");
	}
	device = MINOR(CURRENT_DEVICE);
	drive = device & 3;
	type = device >> 2;
	floppy = &unit[drive];

	if (!floppy->connected) {
		/* drive not connected */
		printk("Unknown Device: fd%d\n", drive);
		end_request(0);
		goto repeat;
	}
	if (type == 0) {
		if (!floppy->disktype) {
			Probing = 1;
			floppy->disktype = disk_type + NUM_DISK_TYPES - 1;
			floppy_sizes[drive] = floppy->disktype->blocks >> 1;
			floppy->autoprobe = 1;
		}
	} else {
		/* user supplied disk type */
		--type;
		if (type >= NUM_DISK_TYPES) {
			printk("fd%d: invalid disk format", drive);
			end_request(0);
			goto repeat;
		}
		floppy->disktype = &disk_type[type];
		floppy_sizes[drive] = disk_type[type].blocks >> 1;
		floppy->autoprobe = 0;
	}

	if (CURRENT->sector + 1 > floppy->disktype->blocks) {
		end_request(0);
		goto repeat;
	}
	/* stop deselect timer */
	del_timer(&motor_off_timer);

	ReqCnt = 0;
	ReqCmd = CURRENT->cmd;
	ReqBlock = CURRENT->sector;
	ReqBuffer = CURRENT->buffer;
	setup_req_params(drive);
	do_fd_action(drive);

	return;

      the_end:
	finish_fdc();
}

static void fd1772_checkint(void)
{
  extern int fdc1772_bytestogo;

  /*printk("fd1772_checkint %d\n",fdc1772_fdc_int_done);*/
  if (fdc1772_fdc_int_done)
    floppy_irqconsequencehandler();
  if ((MultReadInProgress) && (fdc1772_bytestogo==0)) fd_readtrack_check(0);
  if (fdc_busy) {
    queue_task(&fd1772_tq,&tq_immediate);
    mark_bh(IMMEDIATE_BH);
  };
};

void do_fd_request(void)
{
	unsigned long flags;

	DPRINT(("do_fd_request for pid %d\n", current->pid));
	if (fdc_busy) return;
	save_flags(flags);
	cli();
	while (fdc_busy)
		sleep_on(&fdc_wait);
	fdc_busy = 1;
	ENABLE_IRQ();
	restore_flags(flags);

	fdc1772_fdc_int_done = 0;

	redo_fd_request();

  queue_task(&fd1772_tq,&tq_immediate);
  mark_bh(IMMEDIATE_BH);
}


static int invalidate_drive(int rdev)
{
	/* invalidate the buffer track to force a reread */
#ifdef TRACKBUFFER
  BufferDrive = -1;
#endif

	set_bit(rdev & 3, &fake_change);
	check_disk_change(rdev);
	return 0;
}

static int fd_ioctl(struct inode *inode, struct file *filp,
		    unsigned int cmd, unsigned long param)
{
#define IOCTL_MODE_BIT 8
#define OPEN_WRITE_BIT 16
#define IOCTL_ALLOWED (filp && (filp->f_mode & IOCTL_MODE_BIT))

	int drive, device;

	device = inode->i_rdev;
	switch (cmd) {
		RO_IOCTLS(inode->i_rdev, param);
	}
	drive = MINOR(device);
	if (!IOCTL_ALLOWED)
		return -EPERM;
	switch (cmd) {
	case FDFMTBEG:
		return 0;
		/* case FDC1772LRPRM:  ??? DAG what does this do?? 
		   unit[drive].disktype = NULL;
		   floppy_sizes[drive] = MAX_DISK_SIZE;
		   return invalidate_drive (device); */
	case FDFMTEND:
	case FDFLUSH:
		return invalidate_drive(drive);
	}
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;
	if (drive < 0 || drive > 3)
		return -EINVAL;
	switch (cmd) {
	default:
		return -EINVAL;
	}
	return 0;
}


/* Initialize the 'unit' variable for drive 'drive' */

static void fd_probe(int drive)
{
	unit[drive].connected = 0;
	unit[drive].disktype = NULL;

	if (!fd_test_drive_present(drive))
		return;

	unit[drive].connected = 1;
	unit[drive].track = -1; /* If we put the auto detect back in this can go to 0 */
	unit[drive].steprate = FDC1772STEP_6;
	MotorOn = 1;		/* from probe restore operation! */
}


/* This function tests the physical presence of a floppy drive (not
 * whether a disk is inserted). This is done by issuing a restore
 * command, waiting max. 2 seconds (that should be enough to move the
 * head across the whole disk) and looking at the state of the "TR00"
 * signal. This should now be raised if there is a drive connected
 * (and there is no hardware failure :-) Otherwise, the drive is
 * declared absent.
 */

static int fd_test_drive_present(int drive)
{
	unsigned long timeout;
	unsigned char status;
	int ok;

	printk("fd_test_drive_present %d\n", drive);
	if (drive > 1)
		return (0);
	return (1);		/* Simple hack for the moment - the autodetect doesn't seem to work on arc */
	fd_select_drive(drive);

	/* disable interrupt temporarily */
	DISABLE_IRQ();
	FDC1772_WRITE(FDC1772REG_TRACK, 0x00);	/* was ff00 why? */
	FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_RESTORE | FDC1772CMDADD_H | FDC1772STEP_6);

	/*printk("fd_test_drive_present: Going into timeout loop\n"); */
	for (ok = 0, timeout = jiffies + 2 * HZ + HZ / 2; time_before(jiffies, timeout);) {
		/*  What does this piece of atariism do? - query for an interrupt? */
		/*  if (!(mfp.par_dt_reg & 0x20))
		   break; */
		/* Well this is my nearest guess - quit when we get an FDC interrupt */
		if (IOC_FIQSTAT & 2)
			break;
	}

	/*printk("fd_test_drive_present: Coming out of timeout loop\n"); */
	status = FDC1772_READ(FDC1772REG_STATUS);
	ok = (status & FDC1772STAT_TR00) != 0;

	/*printk("fd_test_drive_present: ok=%d\n",ok); */
	/* force interrupt to abort restore operation (FDC1772 would try
	 * about 50 seconds!) */
	FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_FORCI);
	udelay(500);
	status = FDC1772_READ(FDC1772REG_STATUS);
	udelay(20);
	/*printk("fd_test_drive_present: just before OK code %d\n",ok); */

	if (ok) {
		/* dummy seek command to make WP bit accessible */
		FDC1772_WRITE(FDC1772REG_DATA, 0);
		FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_SEEK);
		printk("fd_test_drive_present: just before wait for int\n");
		/* DAG: Guess means wait for interrupt */
		while (!(IOC_FIQSTAT & 2));
		printk("fd_test_drive_present: just after wait for int\n");
		status = FDC1772_READ(FDC1772REG_STATUS);
	}
	printk("fd_test_drive_present: just before ENABLE_IRQ\n");
	ENABLE_IRQ();
	printk("fd_test_drive_present: about to return\n");
	return (ok);
}


/* Look how many and which kind of drives are connected. If there are
 * floppies, additionally start the disk-change and motor-off timers.
 */

static void config_types(void)
{
	int drive, cnt = 0;

	printk("Probing floppy drive(s):\n");
	for (drive = 0; drive < FD_MAX_UNITS; drive++) {
		fd_probe(drive);
		if (unit[drive].connected) {
			printk("fd%d\n", drive);
			++cnt;
		}
	}

	if (FDC1772_READ(FDC1772REG_STATUS) & FDC1772STAT_BUSY) {
		/* If FDC1772 is still busy from probing, give it another FORCI
		 * command to abort the operation. If this isn't done, the FDC1772
		 * will interrupt later and its IRQ line stays low, because
		 * the status register isn't read. And this will block any
		 * interrupts on this IRQ line :-(
		 */
		FDC1772_WRITE(FDC1772REG_CMD, FDC1772CMD_FORCI);
		udelay(500);
		FDC1772_READ(FDC1772REG_STATUS);
		udelay(20);
	}
	if (cnt > 0) {
		START_MOTOR_OFF_TIMER(FD_MOTOR_OFF_DELAY);
		if (cnt == 1)
			fd_select_drive(0);
		/*START_CHECK_CHANGE_TIMER( CHECK_CHANGE_DELAY ); */
	}
}

/*
 * floppy_open check for aliasing (/dev/fd0 can be the same as
 * /dev/PS0 etc), and disallows simultaneous access to the same
 * drive with different device numbers.
 */

static int floppy_open(struct inode *inode, struct file *filp)
{
	int drive;
	int old_dev;

	if (!filp) {
		DPRINT(("Weird, open called with filp=0\n"));
		return -EIO;
	}
	drive = MINOR(inode->i_rdev) & 3;
	if ((MINOR(inode->i_rdev) >> 2) > NUM_DISK_TYPES)
		return -ENXIO;

	old_dev = fd_device[drive];

	if (fd_ref[drive])
		if (old_dev != inode->i_rdev)
			return -EBUSY;

	if (fd_ref[drive] == -1 || (fd_ref[drive] && filp->f_flags & O_EXCL))
		return -EBUSY;

	if (filp->f_flags & O_EXCL)
		fd_ref[drive] = -1;
	else
		fd_ref[drive]++;

	fd_device[drive] = inode->i_rdev;

	if (old_dev && old_dev != inode->i_rdev)
		invalidate_buffers(old_dev);

	/* Allow ioctls if we have write-permissions even if read-only open */
	if (filp->f_mode & 2 || permission(inode, 2) == 0)
		filp->f_mode |= IOCTL_MODE_BIT;
	if (filp->f_mode & 2)
		filp->f_mode |= OPEN_WRITE_BIT;

	if (filp->f_flags & O_NDELAY)
		return 0;

	if (filp->f_mode & 3) {
		check_disk_change(inode->i_rdev);
		if (filp->f_mode & 2) {
			if (unit[drive].wpstat) {
				floppy_release(inode, filp);
				return -EROFS;
			}
		}
	}
	return 0;
}


static void floppy_release(struct inode *inode, struct file *filp)
{
	int drive;

	drive = inode->i_rdev & 3;

	if (!filp || (filp->f_mode & (2 | OPEN_WRITE_BIT)))
		/* if the file is mounted OR (writable now AND writable at open
		   time) Linus: Does this cover all cases? */
		block_fsync(inode, filp);

	if (fd_ref[drive] < 0)
		fd_ref[drive] = 0;
	else if (!fd_ref[drive]--) {
		printk("floppy_release with fd_ref == 0");
		fd_ref[drive] = 0;
	}
}

static struct file_operations floppy_fops =
{
	NULL,			/* lseek - default */
	block_read,		/* read - general block-dev read */
	block_write,		/* write - general block-dev write */
	NULL,			/* readdir - bad */
	NULL,			/* select */
	fd_ioctl,		/* ioctl */
	NULL,			/* mmap */
	floppy_open,		/* open */
	NULL,			/* flush */
	floppy_release,		/* release */
	block_fsync,		/* fsync */
	NULL,			/* fasync */
	check_floppy_change,	/* media_change */
	floppy_revalidate,	/* revalidate */
};


int fd1772_init(void)
{
	int i;

	if (register_blkdev(MAJOR_NR, "fd", &floppy_fops)) {
		printk("Unable to get major %d for floppy\n", MAJOR_NR);
		return 1;
	}

	if (request_dma(FLOPPY_DMA, "fd1772")) {
		printk("Unable to grab DMA%d for the floppy (1772) driver\n", FLOPPY_DMA);
		return 1;
	};

	if (request_dma(FIQ_FD1772, "fd1772 end")) {
		printk("Unable to grab DMA%d for the floppy (1772) driver\n", FIQ_FD1772);
		free_dma(FLOPPY_DMA);
		return 1;
	};
	enable_dma(FIQ_FD1772);	/* This inserts a call to our command end routine */

	/* initialize variables */
	SelectedDrive = -1;
#ifdef TRACKBUFFER
  BufferDrive = -1;
#endif

	/* initialize check_change timer */
	timer_table[FLOPPY_TIMER].fn = check_change;
	timer_active &= ~(1 << FLOPPY_TIMER);


#ifdef TRACKBUFFER
  DMABuffer = (char *)kmalloc((MAX_SECTORS+1)*512,GFP_KERNEL); /* Atari uses 512 - I want to eventually cope with 1K sectors */
  TrackBuffer = DMABuffer + 512;
#else
	/* Allocate memory for the DMAbuffer - on the Atari this takes it
	   out of some special memory... */
	DMABuffer = (char *) kmalloc(2048);	/* Copes with pretty large sectors */
#endif
#ifdef TRACKBUFFER  
  BufferDrive = BufferSide = BufferTrack = -1;
#endif

	for (i = 0; i < FD_MAX_UNITS; i++) {
		unit[i].track = -1;
	}

	for (i = 0; i < 256; i++)
		if ((i >> 2) > 0 && (i >> 2) <= NUM_DISK_TYPES)
			floppy_sizes[i] = disk_type[(i >> 2) - 1].blocks >> 1;
		else
			floppy_sizes[i] = MAX_DISK_SIZE;

	blk_size[MAJOR_NR] = floppy_sizes;
	blksize_size[MAJOR_NR] = floppy_blocksizes;
	blk_dev[MAJOR_NR].request_fn = DEVICE_REQUEST;

	config_types();

	return 0;
}

/* Just a dummy at the moment */
void floppy_setup(char *str, int *ints)
{
}

void floppy_eject(void) {
}
