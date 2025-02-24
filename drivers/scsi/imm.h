
/*  Driver for the Iomega MatchMaker parallel port SCSI HBA embedded in 
 * the Iomega ZIP Plus drive
 * 
 * (c) 1998     David Campbell     campbell@torque.net
 *
 * Please note that I live in Perth, Western Australia. GMT+0800
 */

#ifndef _IMM_H
#define _IMM_H

#define   IMM_VERSION   "2.03 (for Linux 2.0.0)"

/* 
 * 10 Apr 1998 (Good Friday) - Received EN144302 by email from Iomega.
 * Scarry thing is the level of support from one of their managers.
 * The onus is now on us (the developers) to shut up and start coding.
 *                                              11Apr98 [ 0.10 ]
 *
 * --- SNIP ---
 *
 * It manages to find the drive which is a good start. Writing data during
 * data phase is known to be broken (due to requirements of two byte writes).
 * Removing "Phase" debug messages.
 *
 * PS: Took four hours of coding after I bought a drive.
 *      ANZAC Day (Aus "War Veterans Holiday")  25Apr98 [ 0.14 ]
 *
 * Ten minutes later after a few fixes.... (LITERALLY!!!)
 * Have mounted disk, copied file, dismounted disk, remount disk, diff file
 *                    -----  It actually works!!! -----
 *                                              25Apr98 [ 0.15 ]
 *
 * Twenty minutes of mucking around, rearanged the IEEE negotiate mechanism.
 * Now have byte mode working (only EPP and ECP to go now... :=)
 *                                              26Apr98 [ 0.16 ]
 *
 * Thirty minutes of further coding results in EPP working on my machine.
 *                                              27Apr98 [ 0.17 ]
 *
 * Due to work commitments and inability to get a "true" ECP mode functioning
 * I have decided to code the parport support into imm.
 *                                              09Jun98 [ 0.18 ]
 *
 * Driver is now out of beta testing.
 * Support for parport has been added.
 * Now distributed with the ppa driver.
 *                                              12Jun98 [ 2.00 ]
 *
 * Err.. It appears that imm-2.00 was broken....
 *                                              18Jun98 [ 2.01 ]
 *
 * Patch applied to sync this against the Linux 2.1.x kernel code
 * Included qboot_zip.sh
 *                                              21Jun98 [ 2.02 ]
 *
 * Other clean ups include the follow changes:
 *    CONFIG_SCSI_PPA_HAVE_PEDANTIC => CONFIG_SCSI_IZIP_EPP16
 *    added CONFIG_SCSI_IZIP_SLOW_CTR option
 *                                                      [2.03]
 */
/* ------ END OF USER CONFIGURABLE PARAMETERS ----- */

#ifdef IMM_CODE
#include  <linux/config.h>
#include  <linux/stddef.h>
#include  <linux/module.h>
#include  <linux/kernel.h>
#include  <linux/tqueue.h>
#include  <linux/ioport.h>
#include  <linux/delay.h>
#include  <linux/proc_fs.h>
#include  <linux/stat.h>
#include  <linux/blk.h>
#include  <linux/sched.h>
#include  <linux/interrupt.h>

#include  <asm/io.h>
#include  "sd.h"
#include  "hosts.h"
/* batteries not included :-) */

/*
 * modes in which the driver can operate 
 */
#define   IMM_AUTODETECT        0	/* Autodetect mode                */
#define   IMM_NIBBLE            1	/* work in standard 4 bit mode    */
#define   IMM_PS2               2	/* PS/2 byte mode         */
#define   IMM_EPP_8             3	/* EPP mode, 8 bit                */
#define   IMM_EPP_16            4	/* EPP mode, 16 bit               */
#define   IMM_EPP_32            5	/* EPP mode, 32 bit               */
#define   IMM_UNKNOWN           6	/* Just in case...                */

static char *IMM_MODE_STRING[] =
{
    "Autodetect",
    "SPP",
    "PS/2",
    "EPP 8 bit",
    "EPP 16 bit",
#ifdef CONFIG_SCSI_IZIP_EPP16
    "EPP 16 bit",
#else
    "EPP 32 bit",
#endif
    "Unknown"};

/* This is a global option */
int imm_sg = SG_ALL;		/* enable/disable scatter-gather. */

/* other options */
#define IMM_CAN_QUEUE   1	/* use "queueing" interface */
#define IMM_BURST_SIZE	512	/* data burst size */
#define IMM_SELECT_TMO  500	/* 500 how long to wait for target ? */
#define IMM_SPIN_TMO    5000	/* 50000 imm_wait loop limiter */
#define IMM_DEBUG	0	/* debuging option */
#define IN_EPP_MODE(x) (x == IMM_EPP_8 || x == IMM_EPP_16 || x == IMM_EPP_32)

/* args to imm_connect */
#define CONNECT_EPP_MAYBE 1
#define CONNECT_NORMAL  0

#define r_dtr(x)        (unsigned char)inb((x))
#define r_str(x)        (unsigned char)inb((x)+1)
#define r_ctr(x)        (unsigned char)inb((x)+2)
#define r_epp(x)        (unsigned char)inb((x)+4)
#define r_fifo(x)       (unsigned char)inb((x)+0x400)
#define r_ecr(x)        (unsigned char)inb((x)+0x402)

#define w_dtr(x,y)      outb(y, (x))
#define w_str(x,y)      outb(y, (x)+1)
#define w_epp(x,y)      outb(y, (x)+4)
#define w_fifo(x,y)     outb(y, (x)+0x400)
#define w_ecr(x,y)      outb(y, (x)+0x402)

#ifdef CONFIG_SCSI_IZIP_SLOW_CTR
#define w_ctr(x,y)      outb_p(y, (x)+2)
#else
#define w_ctr(x,y)      outb(y, (x)+2)
#endif

static int imm_engine(imm_struct *, Scsi_Cmnd *);
static int imm_in(int, char *, int);
static int imm_init(int);
static void imm_interrupt(void *);
static int imm_out(int, char *, int);

#else
#define imm_release 0
#endif

int imm_detect(Scsi_Host_Template *);
const char *imm_info(struct Scsi_Host *);
int imm_command(Scsi_Cmnd *);
int imm_queuecommand(Scsi_Cmnd *, void (*done) (Scsi_Cmnd *));
int imm_abort(Scsi_Cmnd *);
int imm_reset(Scsi_Cmnd *);
int imm_proc_info(char *, char **, off_t, int, int, int);
int imm_biosparam(Disk *, kdev_t, int *);

#define IMM {	proc_name:			"imm",			\
		proc_info:			imm_proc_info,		\
		name:				"Iomega VPI2 (imm) interface",\
		detect:				imm_detect,		\
		release:			imm_release,		\
		command:			imm_command,		\
		queuecommand:			imm_queuecommand,	\
                eh_abort_handler:               imm_abort,              \
                eh_device_reset_handler:        NULL,                   \
                eh_bus_reset_handler:           imm_reset,              \
                eh_host_reset_handler:          imm_reset,              \
		bios_param:		        imm_biosparam,		\
		this_id:			7,			\
		sg_tablesize:			SG_ALL,			\
		cmd_per_lun:			1,			\
		use_clustering:			ENABLE_CLUSTERING	\
}
#endif				/* _IMM_H */
