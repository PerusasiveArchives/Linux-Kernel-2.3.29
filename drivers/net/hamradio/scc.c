#define RCS_ID "$Id: scc.c,v 1.75 1998/11/04 15:15:01 jreuter Exp jreuter $"

#define VERSION "3.0"
#define BANNER  "Z8530 SCC driver version "VERSION".dl1bke (experimental) by DL1BKE\n"

/*
 * Please use z8530drv-utils-3.0 with this version.
 *            ------------------
 *
 * You can find a subset of the documentation in 
 * linux/Documentation/networking/z8530drv.txt.
 */

/*
   ********************************************************************
   *   SCC.C - Linux driver for Z8530 based HDLC cards for AX.25      *
   ********************************************************************


   ********************************************************************

	Copyright (c) 1993, 1998 Joerg Reuter DL1BKE

	portions (c) 1993 Guido ten Dolle PE1NNZ

   ********************************************************************
   
   The driver and the programs in the archive are UNDER CONSTRUCTION.
   The code is likely to fail, and so your kernel could --- even 
   a whole network. 

   This driver is intended for Amateur Radio use. If you are running it
   for commercial purposes, please drop me a note. I am nosy...

   ...BUT:
 
   ! You  m u s t  recognize the appropriate legislations of your country !
   ! before you connect a radio to the SCC board and start to transmit or !
   ! receive. The GPL allows you to use the  d r i v e r,  NOT the RADIO! !

   For non-Amateur-Radio use please note that you might need a special
   allowance/licence from the designer of the SCC Board and/or the
   MODEM. 

   This program is free software; you can redistribute it and/or modify 
   it under the terms of the (modified) GNU General Public License 
   delivered with the Linux kernel source.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should find a copy of the GNU General Public License in 
   /usr/src/linux/COPYING; 
   
   ******************************************************************** 

		
   Incomplete history of z8530drv:
   -------------------------------

   1994-09-13	started to write the driver, rescued most of my own
		code (and Hans Alblas' memory buffer pool concept) from 
		an earlier project "sccdrv" which was initiated by 
		Guido ten Dolle. Not much of the old driver survived, 
		though. The first version I put my hands on was sccdrv1.3
		from August 1993. The memory buffer pool concept
		appeared in an unauthorized sccdrv version (1.5) from
		August 1994.

   1995-01-31	changed copyright notice to GPL without limitations.
   
     .
     .	<SNIP>
     .
   		  
   1996-10-05	New semester, new driver... 

   		  * KISS TNC emulator removed (TTY driver)
   		  * Source moved to drivers/net/
   		  * Includes Z8530 defines from drivers/net/z8530.h
   		  * Uses sk_buffer memory management
   		  * Reduced overhead of /proc/net/z8530drv output
   		  * Streamlined quite a lot things
   		  * Invents brand new bugs... ;-)

   		  The move to version number 3.0 reflects theses changes.
   		  You can use 'kissbridge' if you need a KISS TNC emulator.

   1996-12-13	Fixed for Linux networking changes. (G4KLX)
   1997-01-08	Fixed the remaining problems.
   1997-04-02	Hopefully fixed the problems with the new *_timer()
   		routines, added calibration code.
   1997-10-12	Made SCC_DELAY a CONFIG option, added CONFIG_SCC_TRXECHO
   1998-01-29	Small fix to avoid lock-up on initialization
   1998-09-29	Fixed the "grouping" bugs, tx_inhibit works again,
   		using dev->tx_queue_len now instead of MAXQUEUE now.
   1998-10-21	Postponed the spinlock changes, would need a lot of
   		testing I currently don't have the time to. Softdcd doesn't
   		work.
   1998-11-04	Softdcd does not work correctly in DPLL mode, in fact it 
   		never did. The DPLL locks on noise, the SYNC unit sees
   		flags that aren't... Restarting the DPLL does not help
   		either, it resynchronizes too slow and the first received
   		frame gets lost.

   Thanks to all who contributed to this driver with ideas and bug
   reports!
   
   NB -- if you find errors, change something, please let me know
      	 first before you distribute it... And please don't touch
   	 the version number. Just replace my callsign in
   	 "v3.0.dl1bke" with your own. Just to avoid confusion...

   If you want to add your modification to the linux distribution
   please (!) contact me first.
   
   New versions of the driver will be announced on the linux-hams
   mailing list on vger.rutgers.edu. To subscribe send an e-mail
   to majordomo@vger.rutgers.edu with the following line in
   the body of the mail:
   
	   subscribe linux-hams
	   
   The content of the "Subject" field will be ignored.

   vy 73,
   Joerg Reuter	ampr-net: dl1bke@db0pra.ampr.org
		AX-25   : DL1BKE @ DB0ACH.#NRW.DEU.EU
		Internet: jreuter@poboxes.com
		www     : http://poboxes.com/jreuter/
*/

/* ----------------------------------------------------------------------- */

#undef  SCC_LDELAY	1	/* slow it even a bit more down */
#undef  SCC_DONT_CHECK		/* don't look if the SCCs you specified are available */

#define SCC_MAXCHIPS	4       /* number of max. supported chips */
#define SCC_BUFSIZE	384     /* must not exceed 4096 */
#undef  SCC_DISABLE_ALL_INTS	/* use cli()/sti() in ISR instead of */
				/* enable_irq()/disable_irq()        */
#undef	SCC_DEBUG

#define SCC_DEFAULT_CLOCK	4915200 
				/* default pclock if nothing is specified */

/* ----------------------------------------------------------------------- */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/in.h>
#include <linux/fcntl.h>
#include <linux/ptrace.h>
#include <linux/malloc.h>
#include <linux/delay.h>

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <linux/socket.h>
#include <linux/init.h>

#include <linux/scc.h>
#include "z8530.h"

#include <net/ax25.h>
#include <asm/irq.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/bitops.h>

#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>

#ifdef MODULE
int init_module(void);
void cleanup_module(void);
#endif

int scc_init(void);

static void t_dwait(unsigned long);
static void t_txdelay(unsigned long);
static void t_tail(unsigned long);
static void t_busy(unsigned long);
static void t_maxkeyup(unsigned long);
static void t_idle(unsigned long);
static void scc_tx_done(struct scc_channel *);
static void scc_start_tx_timer(struct scc_channel *, void (*)(unsigned long), unsigned long);
static void scc_start_maxkeyup(struct scc_channel *);
static void scc_start_defer(struct scc_channel *);

static void z8530_init(void);

static void init_channel(struct scc_channel *scc);
static void scc_key_trx (struct scc_channel *scc, char tx);
static void scc_isr(int irq, void *dev_id, struct pt_regs *regs);
static void scc_init_timer(struct scc_channel *scc);

static int scc_net_setup(struct scc_channel *scc, unsigned char *name, int addev);
static int scc_net_init(struct net_device *dev);
static int scc_net_open(struct net_device *dev);
static int scc_net_close(struct net_device *dev);
static void scc_net_rx(struct scc_channel *scc, struct sk_buff *skb);
static int scc_net_tx(struct sk_buff *skb, struct net_device *dev);
static int scc_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd);
static int scc_net_set_mac_address(struct net_device *dev, void *addr);
static int scc_net_header(struct sk_buff *skb, struct net_device *dev, unsigned short type, void *daddr, void *saddr, unsigned len);
static struct net_device_stats * scc_net_get_stats(struct net_device *dev);

static unsigned char *SCC_DriverName = "scc";

static struct irqflags { unsigned char used : 1; } Ivec[16];
	
static struct scc_channel SCC_Info[2 * SCC_MAXCHIPS];	/* information per channel */

static struct scc_ctrl {
	io_port chan_A;
	io_port chan_B;
	int irq;
} SCC_ctrl[SCC_MAXCHIPS+1];

static unsigned char Driver_Initialized = 0;
static int Nchips = 0;
static io_port Vector_Latch = 0;

MODULE_AUTHOR("Joerg Reuter <jreuter@poboxes.com>");
MODULE_DESCRIPTION("Network Device Driver for Z8530 based HDLC cards for Amateur Packet Radio");
MODULE_SUPPORTED_DEVICE("scc");

/* ******************************************************************** */
/* *			Port Access Functions			      * */
/* ******************************************************************** */

/* These provide interrupt save 2-step access to the Z8530 registers */

static inline unsigned char InReg(io_port port, unsigned char reg)
{
	unsigned long flags;
	unsigned char r;
	
	save_flags(flags);
	cli();
#ifdef SCC_LDELAY
	Outb(port, reg);
	udelay(SCC_LDELAY);
	r=Inb(port);
	udelay(SCC_LDELAY);
#else
	Outb(port, reg);
	r=Inb(port);
#endif
	restore_flags(flags);
	return r;
}

static inline void OutReg(io_port port, unsigned char reg, unsigned char val)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
#ifdef SCC_LDELAY
	Outb(port, reg); udelay(SCC_LDELAY);
	Outb(port, val); udelay(SCC_LDELAY);
#else
	Outb(port, reg);
	Outb(port, val);
#endif
	restore_flags(flags);
}

static inline void wr(struct scc_channel *scc, unsigned char reg,
	unsigned char val)
{
	OutReg(scc->ctrl, reg, (scc->wreg[reg] = val));
}

static inline void or(struct scc_channel *scc, unsigned char reg, unsigned char val)
{
	OutReg(scc->ctrl, reg, (scc->wreg[reg] |= val));
}

static inline void cl(struct scc_channel *scc, unsigned char reg, unsigned char val)
{
	OutReg(scc->ctrl, reg, (scc->wreg[reg] &= ~val));
}

#ifdef SCC_DISABLE_ALL_INTS
static inline void scc_cli(int irq)
{ cli(); }
static inline void scc_sti(int irq)
{ sti(); }
#else
static inline void scc_cli(int irq)
{ disable_irq(irq); }
static inline void scc_sti(int irq)
{ enable_irq(irq); }
#endif

/* ******************************************************************** */
/* *			Some useful macros			      * */
/* ******************************************************************** */


static inline void scc_lock_dev(struct scc_channel *scc)
{
	scc->dev->tbusy = 1;
}

static inline void scc_unlock_dev(struct scc_channel *scc)
{
	scc->dev->tbusy = 0;
}

static inline void scc_discard_buffers(struct scc_channel *scc)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();
	
	if (scc->tx_buff != NULL)
	{
		dev_kfree_skb(scc->tx_buff);
		scc->tx_buff = NULL;
	}
	
	while (skb_queue_len(&scc->tx_queue))
		dev_kfree_skb(skb_dequeue(&scc->tx_queue));

	restore_flags(flags);
}



/* ******************************************************************** */
/* *			Interrupt Service Routines		      * */
/* ******************************************************************** */


/* ----> subroutines for the interrupt handlers <---- */

static inline void scc_notify(struct scc_channel *scc, int event)
{
	struct sk_buff *skb;
	char *bp;
	
        if (scc->kiss.fulldup != KISS_DUPLEX_OPTIMA)
		return;

	skb = dev_alloc_skb(2);
	if (skb != NULL)
	{
		bp = skb_put(skb, 2);
		*bp++ = PARAM_HWEVENT;
		*bp++ = event;
		scc_net_rx(scc, skb);
	} else
		scc->stat.nospace++;
}

static inline void flush_rx_FIFO(struct scc_channel *scc)
{
	int k;
	
	for (k=0; k<3; k++)
		Inb(scc->data);
		
	if(scc->rx_buff != NULL)		/* did we receive something? */
	{
		scc->stat.rxerrs++;  /* then count it as an error */
		kfree_skb(scc->rx_buff);
		scc->rx_buff = NULL;
	}
}

static void start_hunt(struct scc_channel *scc)
{
	if ((scc->modem.clocksrc != CLK_EXTERNAL))
		OutReg(scc->ctrl,R14,SEARCH|scc->wreg[R14]); /* DPLL: enter search mode */
	or(scc,R3,ENT_HM|RxENABLE);  /* enable the receiver, hunt mode */
}

/* ----> four different interrupt handlers for Tx, Rx, changing of	*/
/*       DCD/CTS and Rx/Tx errors					*/

/* Transmitter interrupt handler */
static inline void scc_txint(struct scc_channel *scc)
{
	struct sk_buff *skb;

	scc->stat.txints++;
	skb = scc->tx_buff;
	
	/* send first octet */
	
	if (skb == NULL)
	{
		skb = skb_dequeue(&scc->tx_queue);
		scc->tx_buff = skb;
		scc_unlock_dev(scc);

		if (skb == NULL)
		{
			scc_tx_done(scc);
			Outb(scc->ctrl, RES_Tx_P);
			return;
		}
		
		if (skb->len == 0)		/* Paranoia... */
		{
			dev_kfree_skb(skb);
			scc->tx_buff = NULL;
			scc_tx_done(scc);
			Outb(scc->ctrl, RES_Tx_P);
			return;
		}

		scc->stat.tx_state = TXS_ACTIVE;

		OutReg(scc->ctrl, R0, RES_Tx_CRC);
						/* reset CRC generator */
		or(scc,R10,ABUNDER);		/* re-install underrun protection */
		Outb(scc->data,*skb->data);	/* send byte */
		skb_pull(skb, 1);

		if (!scc->enhanced)		/* reset EOM latch */
			Outb(scc->ctrl,RES_EOM_L);
		return;
	}
	
	/* End Of Frame... */
	
	if (skb->len == 0)
	{
		Outb(scc->ctrl, RES_Tx_P);	/* reset pending int */
		cl(scc, R10, ABUNDER);		/* send CRC */
		dev_kfree_skb(skb);
		scc->tx_buff = NULL;
		scc->stat.tx_state = TXS_NEWFRAME; /* next frame... */
		return;
	} 
	
	/* send octet */
	
	Outb(scc->data,*skb->data);		
	skb_pull(skb, 1);
}


/* External/Status interrupt handler */
static inline void scc_exint(struct scc_channel *scc)
{
	unsigned char status,changes,chg_and_stat;

	scc->stat.exints++;

	status = InReg(scc->ctrl,R0);
	changes = status ^ scc->status;
	chg_and_stat = changes & status;
	
	/* ABORT: generated whenever DCD drops while receiving */

	if (chg_and_stat & BRK_ABRT)		/* Received an ABORT */
		flush_rx_FIFO(scc);

	/* HUNT: software DCD; on = waiting for SYNC, off = receiving frame */

	if ((changes & SYNC_HUNT) && scc->kiss.softdcd)
	{
		if (status & SYNC_HUNT)
		{
			scc->dcd = 0;
			flush_rx_FIFO(scc);
			if ((scc->modem.clocksrc != CLK_EXTERNAL))
				OutReg(scc->ctrl,R14,SEARCH|scc->wreg[R14]); /* DPLL: enter search mode */
		} else {
			scc->dcd = 1;
		}

		scc_notify(scc, scc->dcd? HWEV_DCD_OFF:HWEV_DCD_ON);
	}

	/* DCD: on = start to receive packet, off = ABORT condition */
	/* (a successfully received packet generates a special condition int) */
	
	if((changes & DCD) && !scc->kiss.softdcd) /* DCD input changed state */
	{
		if(status & DCD)                /* DCD is now ON */
		{
			start_hunt(scc);
			scc->dcd = 1;
		} else {                        /* DCD is now OFF */
			cl(scc,R3,ENT_HM|RxENABLE); /* disable the receiver */
			flush_rx_FIFO(scc);
			scc->dcd = 0;
		}
		
		scc_notify(scc, scc->dcd? HWEV_DCD_ON:HWEV_DCD_OFF);
	}

#ifdef notdef
	/* CTS: use external TxDelay (what's that good for?!)
	 * Anyway: If we _could_ use it (BayCom USCC uses CTS for
	 * own purposes) we _should_ use the "autoenable" feature
	 * of the Z8530 and not this interrupt...
	 */
	 
	if (chg_and_stat & CTS)			/* CTS is now ON */
	{
		if (scc->kiss.txdelay == 0)	/* zero TXDELAY = wait for CTS */
			scc_start_tx_timer(scc, t_txdelay, 0);
	}
#endif
	
	if (scc->stat.tx_state == TXS_ACTIVE && (status & TxEOM))
	{
		scc->stat.tx_under++;	  /* oops, an underrun! count 'em */
		Outb(scc->ctrl, RES_EXT_INT);	/* reset ext/status interrupts */

		if (scc->tx_buff != NULL)
		{
			dev_kfree_skb(scc->tx_buff);
			scc->tx_buff = NULL;
		}
		
		or(scc,R10,ABUNDER);
		scc_start_tx_timer(scc, t_txdelay, 0);	/* restart transmission */
	}
		
	scc->status = status;
	Outb(scc->ctrl,RES_EXT_INT);
}


/* Receiver interrupt handler */
static inline void scc_rxint(struct scc_channel *scc)
{
	struct sk_buff *skb;

	scc->stat.rxints++;

	if((scc->wreg[5] & RTS) && scc->kiss.fulldup == KISS_DUPLEX_HALF)
	{
		Inb(scc->data);		/* discard char */
		or(scc,R3,ENT_HM);	/* enter hunt mode for next flag */
		return;
	}

	skb = scc->rx_buff;
	
	if (skb == NULL)
	{
		skb = dev_alloc_skb(scc->stat.bufsize);
		if (skb == NULL)
		{
			scc->dev_stat.rx_dropped++;
			scc->stat.nospace++;
			Inb(scc->data);
			or(scc, R3, ENT_HM);
			return;
		}
		
		scc->rx_buff = skb;
		*(skb_put(skb, 1)) = 0;	/* KISS data */
	}
	
	if (skb->len >= scc->stat.bufsize)
	{
#ifdef notdef
		printk(KERN_DEBUG "z8530drv: oops, scc_rxint() received huge frame...\n");
#endif
		kfree_skb(skb);
		scc->rx_buff = NULL;
		Inb(scc->data);
		or(scc, R3, ENT_HM);
		return;
	}

	*(skb_put(skb, 1)) = Inb(scc->data);
}


/* Receive Special Condition interrupt handler */
static inline void scc_spint(struct scc_channel *scc)
{
	unsigned char status;
	struct sk_buff *skb;

	scc->stat.spints++;

	status = InReg(scc->ctrl,R1);		/* read receiver status */
	
	Inb(scc->data);				/* throw away Rx byte */
	skb = scc->rx_buff;

	if(status & Rx_OVR)			/* receiver overrun */
	{
		scc->stat.rx_over++;             /* count them */
		or(scc,R3,ENT_HM);               /* enter hunt mode for next flag */
		
		if (skb != NULL) 
			kfree_skb(skb);
		scc->rx_buff = NULL;
	}

	if(status & END_FR && skb != NULL)	/* end of frame */
	{
		/* CRC okay, frame ends on 8 bit boundary and received something ? */
		
		if (!(status & CRC_ERR) && (status & 0xe) == RES8 && skb->len > 0)
		{
			/* ignore last received byte (first of the CRC bytes) */
			skb_trim(skb, skb->len-1);
			scc_net_rx(scc, skb);
			scc->rx_buff = NULL;
			scc->stat.rxframes++;
		} else {				/* a bad frame */
			kfree_skb(skb);
			scc->rx_buff = NULL;
			scc->stat.rxerrs++;
		}
	} 

	Outb(scc->ctrl,ERR_RES);
}


/* ----> interrupt service routine for the Z8530 <---- */

static void scc_isr_dispatch(struct scc_channel *scc, int vector)
{
	switch (vector & VECTOR_MASK)
	{
		case TXINT: scc_txint(scc); break;
		case EXINT: scc_exint(scc); break;
		case RXINT: scc_rxint(scc); break;
		case SPINT: scc_spint(scc); break;
	}
}

/* If the card has a latch for the interrupt vector (like the PA0HZP card)
   use it to get the number of the chip that generated the int.
   If not: poll all defined chips.
 */

#define SCC_IRQTIMEOUT 30000

static void scc_isr(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char vector;	
	struct scc_channel *scc;
	struct scc_ctrl *ctrl;
	int k;
	
	if (Vector_Latch)
	{
	    	for(k=0; k < SCC_IRQTIMEOUT; k++)
    		{
			Outb(Vector_Latch, 0);      /* Generate INTACK */
        
			/* Read the vector */
			if((vector=Inb(Vector_Latch)) >= 16 * Nchips) break; 
			if (vector & 0x01) break;
        	 
		        scc=&SCC_Info[vector >> 3 ^ 0x01];
			if (!scc->dev) break;

			scc_isr_dispatch(scc, vector);

			OutReg(scc->ctrl,R0,RES_H_IUS);              /* Reset Highest IUS */
		}  

		if (k == SCC_IRQTIMEOUT)
			printk(KERN_WARNING "z8530drv: endless loop in scc_isr()?\n");

		return;
	}

	/* Find the SCC generating the interrupt by polling all attached SCCs
	 * reading RR3A (the interrupt pending register)
	 */

	ctrl = SCC_ctrl;
	while (ctrl->chan_A)
	{
		if (ctrl->irq != irq)
		{
			ctrl++;
			continue;
		}

		scc = NULL;
		for (k = 0; InReg(ctrl->chan_A,R3) && k < SCC_IRQTIMEOUT; k++)
		{
			vector=InReg(ctrl->chan_B,R2);	/* Read the vector */
			if (vector & 0x01) break; 

			scc = &SCC_Info[vector >> 3 ^ 0x01];
		        if (!scc->dev) break;

			scc_isr_dispatch(scc, vector);
		}

		if (k == SCC_IRQTIMEOUT)
		{
			printk(KERN_WARNING "z8530drv: endless loop in scc_isr()?!\n");
			break;
		}

		/* This looks wierd and it is. At least the BayCom USCC doesn't
		 * use the Interrupt Daisy Chain, thus we'll have to start
		 * all over again to be sure not to miss an interrupt from 
		 * (any of) the other chip(s)...
		 * Honestly, the situation *is* braindamaged...
		 */

		if (scc != NULL)
		{
			OutReg(scc->ctrl,R0,RES_H_IUS);
			ctrl = SCC_ctrl; 
		} else
			ctrl++;
	}
}



/* ******************************************************************** */
/* *			Init Channel					*/
/* ******************************************************************** */


/* ----> set SCC channel speed <---- */

static inline void set_brg(struct scc_channel *scc, unsigned int tc)
{
	cl(scc,R14,BRENABL);		/* disable baudrate generator */
	wr(scc,R12,tc & 255);		/* brg rate LOW */
	wr(scc,R13,tc >> 8);   		/* brg rate HIGH */
	or(scc,R14,BRENABL);		/* enable baudrate generator */
}

static inline void set_speed(struct scc_channel *scc)
{
	disable_irq(scc->irq);

	if (scc->modem.speed > 0)	/* paranoia... */
		set_brg(scc, (unsigned) (scc->clock / (scc->modem.speed * 64)) - 2);

	enable_irq(scc->irq);
}


/* ----> initialize a SCC channel <---- */

static inline void init_brg(struct scc_channel *scc)
{
	wr(scc, R14, BRSRC);				/* BRG source = PCLK */
	OutReg(scc->ctrl, R14, SSBR|scc->wreg[R14]);	/* DPLL source = BRG */
	OutReg(scc->ctrl, R14, SNRZI|scc->wreg[R14]);	/* DPLL NRZI mode */
}

/*
 * Initialization according to the Z8530 manual (SGS-Thomson's version):
 *
 * 1. Modes and constants
 *
 * WR9	11000000	chip reset
 * WR4	XXXXXXXX	Tx/Rx control, async or sync mode
 * WR1	0XX00X00	select W/REQ (optional)
 * WR2	XXXXXXXX	program interrupt vector
 * WR3	XXXXXXX0	select Rx control
 * WR5	XXXX0XXX	select Tx control
 * WR6	XXXXXXXX	sync character
 * WR7	XXXXXXXX	sync character
 * WR9	000X0XXX	select interrupt control
 * WR10	XXXXXXXX	miscellaneous control (optional)
 * WR11	XXXXXXXX	clock control
 * WR12	XXXXXXXX	time constant lower byte (optional)
 * WR13	XXXXXXXX	time constant upper byte (optional)
 * WR14	XXXXXXX0	miscellaneous control
 * WR14	XXXSSSSS	commands (optional)
 *
 * 2. Enables
 *
 * WR14	000SSSS1	baud rate enable
 * WR3	SSSSSSS1	Rx enable
 * WR5	SSSS1SSS	Tx enable
 * WR0	10000000	reset Tx CRG (optional)
 * WR1	XSS00S00	DMA enable (optional)
 *
 * 3. Interrupt status
 *
 * WR15	XXXXXXXX	enable external/status
 * WR0	00010000	reset external status
 * WR0	00010000	reset external status twice
 * WR1	SSSXXSXX	enable Rx, Tx and Ext/status
 * WR9	000SXSSS	enable master interrupt enable
 *
 * 1 = set to one, 0 = reset to zero
 * X = user defined, S = same as previous init
 *
 *
 * Note that the implementation differs in some points from above scheme.
 *
 */
 
static void init_channel(struct scc_channel *scc)
{
	del_timer(&scc->tx_t);
	del_timer(&scc->tx_wdog);

	disable_irq(scc->irq);

	wr(scc,R4,X1CLK|SDLC);		/* *1 clock, SDLC mode */
	wr(scc,R1,0);			/* no W/REQ operation */
	wr(scc,R3,Rx8|RxCRC_ENAB);	/* RX 8 bits/char, CRC, disabled */	
	wr(scc,R5,Tx8|DTR|TxCRC_ENAB);	/* TX 8 bits/char, disabled, DTR */
	wr(scc,R6,0);			/* SDLC address zero (not used) */
	wr(scc,R7,FLAG);		/* SDLC flag value */
	wr(scc,R9,VIS);			/* vector includes status */
	wr(scc,R10,(scc->modem.nrz? NRZ : NRZI)|CRCPS|ABUNDER); /* abort on underrun, preset CRC generator, NRZ(I) */
	wr(scc,R14, 0);


/* set clock sources:

   CLK_DPLL: normal halfduplex operation
   
		RxClk: use DPLL
		TxClk: use DPLL
		TRxC mode DPLL output
		
   CLK_EXTERNAL: external clocking (G3RUH or DF9IC modem)
   
  	        BayCom: 		others:
  	        
  	        TxClk = pin RTxC	TxClk = pin TRxC
  	        RxClk = pin TRxC 	RxClk = pin RTxC
  	     

   CLK_DIVIDER:
   		RxClk = use DPLL
   		TxClk = pin RTxC
   		
   		BayCom:			others:
   		pin TRxC = DPLL		pin TRxC = BRG
   		(RxClk * 1)		(RxClk * 32)
*/  

   		
	switch(scc->modem.clocksrc)
	{
		case CLK_DPLL:
			wr(scc, R11, RCDPLL|TCDPLL|TRxCOI|TRxCDP);
			init_brg(scc);
			break;

		case CLK_DIVIDER:
			wr(scc, R11, ((scc->brand & BAYCOM)? TRxCDP : TRxCBR) | RCDPLL|TCRTxCP|TRxCOI);
			init_brg(scc);
			break;

		case CLK_EXTERNAL:
			wr(scc, R11, (scc->brand & BAYCOM)? RCTRxCP|TCRTxCP : RCRTxCP|TCTRxCP);
			OutReg(scc->ctrl, R14, DISDPLL);
			break;

	}
	
	set_speed(scc);			/* set baudrate */
	
	if(scc->enhanced)
	{
		or(scc,R15,SHDLCE|FIFOE);	/* enable FIFO, SDLC/HDLC Enhancements (From now R7 is R7') */
		wr(scc,R7,AUTOEOM);
	}

	if(scc->kiss.softdcd || (InReg(scc->ctrl,R0) & DCD))
						/* DCD is now ON */
	{
		start_hunt(scc);
	}
	
	/* enable ABORT, DCD & SYNC/HUNT interrupts */

	wr(scc,R15, BRKIE|TxUIE|(scc->kiss.softdcd? SYNCIE:DCDIE));

	Outb(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	Outb(scc->ctrl,RES_EXT_INT);	/* must be done twice */

	or(scc,R1,INT_ALL_Rx|TxINT_ENAB|EXT_INT_ENAB); /* enable interrupts */
	
	scc->status = InReg(scc->ctrl,R0);	/* read initial status */
	
	or(scc,R9,MIE);			/* master interrupt enable */
	
	scc_init_timer(scc);
			
	enable_irq(scc->irq);
}




/* ******************************************************************** */
/* *			SCC timer functions			      * */
/* ******************************************************************** */


/* ----> scc_key_trx sets the time constant for the baudrate 
         generator and keys the transmitter		     <---- */

static void scc_key_trx(struct scc_channel *scc, char tx)
{
	unsigned int time_const;
		
	if (scc->brand & PRIMUS)
		Outb(scc->ctrl + 4, scc->option | (tx? 0x80 : 0));

	if (scc->modem.speed < 300) 
		scc->modem.speed = 1200;

	time_const = (unsigned) (scc->clock / (scc->modem.speed * (tx? 2:64))) - 2;

	disable_irq(scc->irq);

	if (tx)
	{
		or(scc, R1, TxINT_ENAB);	/* t_maxkeyup may have reset these */
		or(scc, R15, TxUIE);
	}

	if (scc->modem.clocksrc == CLK_DPLL)
	{				/* force simplex operation */
		if (tx)
		{
#ifdef CONFIG_SCC_TRXECHO
			cl(scc, R3, RxENABLE|ENT_HM);	/* switch off receiver */
			cl(scc, R15, DCDIE|SYNCIE);	/* No DCD changes, please */
#endif
			set_brg(scc, time_const);	/* reprogram baudrate generator */

			/* DPLL -> Rx clk, BRG -> Tx CLK, TRxC mode output, TRxC = BRG */
			wr(scc, R11, RCDPLL|TCBR|TRxCOI|TRxCBR);
			
			/* By popular demand: tx_inhibit */
			if (scc->kiss.tx_inhibit)
			{
				or(scc,R5, TxENAB);
				scc->wreg[R5] |= RTS;
			} else {
				or(scc,R5,RTS|TxENAB);	/* set the RTS line and enable TX */
			}
		} else {
			cl(scc,R5,RTS|TxENAB);
			
			set_brg(scc, time_const);	/* reprogram baudrate generator */
			
			/* DPLL -> Rx clk, DPLL -> Tx CLK, TRxC mode output, TRxC = DPLL */
			wr(scc, R11, RCDPLL|TCDPLL|TRxCOI|TRxCDP);

#ifndef CONFIG_SCC_TRXECHO
			if (scc->kiss.softdcd)
#endif
			{
				or(scc,R15, scc->kiss.softdcd? SYNCIE:DCDIE);
				start_hunt(scc);
			}
		}
	} else {
		if (tx)
		{
#ifdef CONFIG_SCC_TRXECHO
			if (scc->kiss.fulldup == KISS_DUPLEX_HALF)
			{
				cl(scc, R3, RxENABLE);
				cl(scc, R15, DCDIE|SYNCIE);
			}
#endif
				
			if (scc->kiss.tx_inhibit)
			{
				or(scc,R5, TxENAB);
				scc->wreg[R5] |= RTS;
			} else {	
				or(scc,R5,RTS|TxENAB);	/* enable tx */
			}
		} else {
			cl(scc,R5,RTS|TxENAB);		/* disable tx */

			if ((scc->kiss.fulldup == KISS_DUPLEX_HALF) &&
#ifndef CONFIG_SCC_TRXECHO
			    scc->kiss.softdcd)
#else
			    1)
#endif
			{
				or(scc, R15, scc->kiss.softdcd? SYNCIE:DCDIE);
				start_hunt(scc);
			}
		}
	}

	enable_irq(scc->irq);
}


/* ----> SCC timer interrupt handler and friends. <---- */

static void scc_start_tx_timer(struct scc_channel *scc, void (*handler)(unsigned long), unsigned long when)
{
	unsigned long flags;
	
	
	save_flags(flags);
	cli();

	del_timer(&scc->tx_t);

	if (when == 0)
	{
		handler((unsigned long) scc);
	} else 
	if (when != TIMER_OFF)
	{
		scc->tx_t.data = (unsigned long) scc;
		scc->tx_t.function = handler;
		scc->tx_t.expires = jiffies + (when*HZ)/100;
		add_timer(&scc->tx_t);
	}
	
	restore_flags(flags);
}

static void scc_start_defer(struct scc_channel *scc)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();

	del_timer(&scc->tx_wdog);
	
	if (scc->kiss.maxdefer != 0 && scc->kiss.maxdefer != TIMER_OFF)
	{
		scc->tx_wdog.data = (unsigned long) scc;
		scc->tx_wdog.function = t_busy;
		scc->tx_wdog.expires = jiffies + HZ*scc->kiss.maxdefer;
		add_timer(&scc->tx_wdog);
	}
	restore_flags(flags);
}

static void scc_start_maxkeyup(struct scc_channel *scc)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();

	del_timer(&scc->tx_wdog);
	
	if (scc->kiss.maxkeyup != 0 && scc->kiss.maxkeyup != TIMER_OFF)
	{
		scc->tx_wdog.data = (unsigned long) scc;
		scc->tx_wdog.function = t_maxkeyup;
		scc->tx_wdog.expires = jiffies + HZ*scc->kiss.maxkeyup;
		add_timer(&scc->tx_wdog);
	}
	
	restore_flags(flags);
}

/* 
 * This is called from scc_txint() when there are no more frames to send.
 * Not exactly a timer function, but it is a close friend of the family...
 */

static void scc_tx_done(struct scc_channel *scc)
{
	/* 
	 * trx remains keyed in fulldup mode 2 until t_idle expires.
	 */
				 
	switch (scc->kiss.fulldup)
	{
		case KISS_DUPLEX_LINK:
			scc->stat.tx_state = TXS_IDLE2;
			if (scc->kiss.idletime != TIMER_OFF)
			scc_start_tx_timer(scc, t_idle, scc->kiss.idletime*100);
			break;
		case KISS_DUPLEX_OPTIMA:
			scc_notify(scc, HWEV_ALL_SENT);
			break;
		default:
			scc->stat.tx_state = TXS_BUSY;
			scc_start_tx_timer(scc, t_tail, scc->kiss.tailtime);
	}

	scc_unlock_dev(scc);
}


static unsigned char Rand = 17;

static inline int is_grouped(struct scc_channel *scc)
{
	int k;
	struct scc_channel *scc2;
	unsigned char grp1, grp2;

	grp1 = scc->kiss.group;
	
	for (k = 0; k < (Nchips * 2); k++)
	{
		scc2 = &SCC_Info[k];
		grp2 = scc2->kiss.group;
		
		if (scc2 == scc || !(scc2->dev && grp2))
			continue;
		
		if ((grp1 & 0x3f) == (grp2 & 0x3f))
		{
			if ( (grp1 & TXGROUP) && (scc2->wreg[R5] & RTS) )
				return 1;
			
			if ( (grp1 & RXGROUP) && scc2->dcd )
				return 1;
		}
	}
	return 0;
}

/* DWAIT and SLOTTIME expired
 *
 * fulldup == 0:  DCD is active or Rand > P-persistence: start t_busy timer
 *                else key trx and start txdelay
 * fulldup == 1:  key trx and start txdelay
 * fulldup == 2:  mintime expired, reset status or key trx and start txdelay
 */

static void t_dwait(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;
	
	if (scc->stat.tx_state == TXS_WAIT)	/* maxkeyup or idle timeout */
	{
		if (skb_queue_len(&scc->tx_queue) == 0)	/* nothing to send */
		{
			scc->stat.tx_state = TXS_IDLE;
			scc_unlock_dev(scc);	/* t_maxkeyup locked it. */
			return;
		}

		scc->stat.tx_state = TXS_BUSY;
	}

	if (scc->kiss.fulldup == KISS_DUPLEX_HALF)
	{
		Rand = Rand * 17 + 31;
		
		if (scc->dcd || (scc->kiss.persist) < Rand || (scc->kiss.group && is_grouped(scc)) )
		{
			scc_start_defer(scc);
			scc_start_tx_timer(scc, t_dwait, scc->kiss.slottime);
			return ;
		}
	}

	if ( !(scc->wreg[R5] & RTS) )
	{
		scc_key_trx(scc, TX_ON);
		scc_start_tx_timer(scc, t_txdelay, scc->kiss.txdelay);
	} else {
		scc_start_tx_timer(scc, t_txdelay, 0);
	}
}


/* TXDELAY expired
 *
 * kick transmission by a fake scc_txint(scc), start 'maxkeyup' watchdog.
 */

static void t_txdelay(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;

	scc_start_maxkeyup(scc);

	if (scc->tx_buff == NULL)
	{
		disable_irq(scc->irq);
		scc_txint(scc);	
		enable_irq(scc->irq);
	}
}
	

/* TAILTIME expired
 *
 * switch off transmitter. If we were stopped by Maxkeyup restart
 * transmission after 'mintime' seconds
 */

static void t_tail(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;
	unsigned long flags;
	
 	save_flags(flags);
 	cli();
 
 	del_timer(&scc->tx_wdog);	
 	scc_key_trx(scc, TX_OFF);

 	restore_flags(flags);

 	if (scc->stat.tx_state == TXS_TIMEOUT)		/* we had a timeout? */
 	{
 		scc->stat.tx_state = TXS_WAIT;

 		if (scc->kiss.mintime != TIMER_OFF)	/* try it again */
 			scc_start_tx_timer(scc, t_dwait, scc->kiss.mintime*100);
 		else
 			scc_start_tx_timer(scc, t_dwait, 0);
 		return;
 	}
 	
 	scc->stat.tx_state = TXS_IDLE;
	scc_unlock_dev(scc);
}


/* BUSY timeout
 *
 * throw away send buffers if DCD remains active too long.
 */

static void t_busy(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;

	del_timer(&scc->tx_t);
	scc_lock_dev(scc);

	scc_discard_buffers(scc);

	scc->stat.txerrs++;
	scc->stat.tx_state = TXS_IDLE;
	
	scc_unlock_dev(scc);
}

/* MAXKEYUP timeout
 *
 * this is our watchdog.
 */

static void t_maxkeyup(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;
	unsigned long flags;

	save_flags(flags);
	cli();

	/* 
	 * let things settle down before we start to
	 * accept new data.
	 */

	scc_lock_dev(scc);
	scc_discard_buffers(scc);

	del_timer(&scc->tx_t);

	cl(scc, R1, TxINT_ENAB);	/* force an ABORT, but don't */
	cl(scc, R15, TxUIE);		/* count it. */
	OutReg(scc->ctrl, R0, RES_Tx_P);

	restore_flags(flags);

	scc->stat.txerrs++;
	scc->stat.tx_state = TXS_TIMEOUT;
	scc_start_tx_timer(scc, t_tail, scc->kiss.tailtime);
}

/* IDLE timeout
 *
 * in fulldup mode 2 it keys down the transmitter after 'idle' seconds
 * of inactivity. We will not restart transmission before 'mintime'
 * expires.
 */

static void t_idle(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;
	
	del_timer(&scc->tx_wdog);

	scc_key_trx(scc, TX_OFF);

	if (scc->kiss.mintime != TIMER_OFF)
		scc_start_tx_timer(scc, t_dwait, scc->kiss.mintime*100);
	scc->stat.tx_state = TXS_WAIT;
}

static void scc_init_timer(struct scc_channel *scc)
{
	unsigned long flags;
	
	save_flags(flags); 
	cli();
	
	scc->stat.tx_state = TXS_IDLE;

	restore_flags(flags);
}


/* ******************************************************************** */
/* *			Set/get L1 parameters			      * */
/* ******************************************************************** */


/*
 * this will set the "hardware" parameters through KISS commands or ioctl()
 */

#define CAST(x) (unsigned long)(x)

static unsigned int scc_set_param(struct scc_channel *scc, unsigned int cmd, unsigned int arg)
{
	switch (cmd)
	{
		case PARAM_TXDELAY:	scc->kiss.txdelay=arg;		break;
		case PARAM_PERSIST:	scc->kiss.persist=arg;		break;
		case PARAM_SLOTTIME:	scc->kiss.slottime=arg;		break;
		case PARAM_TXTAIL:	scc->kiss.tailtime=arg;		break;
		case PARAM_FULLDUP:	scc->kiss.fulldup=arg;		break;
		case PARAM_DTR:		break; /* does someone need this? */
		case PARAM_GROUP:	scc->kiss.group=arg;		break;
		case PARAM_IDLE:	scc->kiss.idletime=arg;		break;
		case PARAM_MIN:		scc->kiss.mintime=arg;		break;
		case PARAM_MAXKEY:	scc->kiss.maxkeyup=arg;		break;
		case PARAM_WAIT:	scc->kiss.waittime=arg;		break;
		case PARAM_MAXDEFER:	scc->kiss.maxdefer=arg;		break;
		case PARAM_TX:		scc->kiss.tx_inhibit=arg;	break;

		case PARAM_SOFTDCD:	
			scc->kiss.softdcd=arg;
			if (arg)
			{
				or(scc, R15, SYNCIE);
				cl(scc, R15, DCDIE);
				start_hunt(scc);
			} else {
				or(scc, R15, DCDIE);
				cl(scc, R15, SYNCIE);
			}
			break;
				
		case PARAM_SPEED:
			if (arg < 256)
				scc->modem.speed=arg*100;
			else
				scc->modem.speed=arg;

			if (scc->stat.tx_state == 0)	/* only switch baudrate on rx... ;-) */
				set_speed(scc);
			break;
			
		case PARAM_RTS:	
			if ( !(scc->wreg[R5] & RTS) )
			{
				if (arg != TX_OFF)
					scc_key_trx(scc, TX_ON);
					scc_start_tx_timer(scc, t_txdelay, scc->kiss.txdelay);
			} else {
				if (arg == TX_OFF)
				{
					scc->stat.tx_state = TXS_BUSY;
					scc_start_tx_timer(scc, t_tail, scc->kiss.tailtime);
				}
			}
			break;
			
		case PARAM_HWEVENT:
			scc_notify(scc, scc->dcd? HWEV_DCD_ON:HWEV_DCD_OFF);
			break;

		default:		return -EINVAL;
	}
	
	return 0;
}


 
static unsigned long scc_get_param(struct scc_channel *scc, unsigned int cmd)
{
	switch (cmd)
	{
		case PARAM_TXDELAY:	return CAST(scc->kiss.txdelay);
		case PARAM_PERSIST:	return CAST(scc->kiss.persist);
		case PARAM_SLOTTIME:	return CAST(scc->kiss.slottime);
		case PARAM_TXTAIL:	return CAST(scc->kiss.tailtime);
		case PARAM_FULLDUP:	return CAST(scc->kiss.fulldup);
		case PARAM_SOFTDCD:	return CAST(scc->kiss.softdcd);
		case PARAM_DTR:		return CAST((scc->wreg[R5] & DTR)? 1:0);
		case PARAM_RTS:		return CAST((scc->wreg[R5] & RTS)? 1:0);
		case PARAM_SPEED:	return CAST(scc->modem.speed);
		case PARAM_GROUP:	return CAST(scc->kiss.group);
		case PARAM_IDLE:	return CAST(scc->kiss.idletime);
		case PARAM_MIN:		return CAST(scc->kiss.mintime);
		case PARAM_MAXKEY:	return CAST(scc->kiss.maxkeyup);
		case PARAM_WAIT:	return CAST(scc->kiss.waittime);
		case PARAM_MAXDEFER:	return CAST(scc->kiss.maxdefer);
		case PARAM_TX:		return CAST(scc->kiss.tx_inhibit);
		default:		return NO_SUCH_PARAM;
	}

}

#undef CAST
#undef SVAL

/* ******************************************************************* */
/* *			Send calibration pattern		     * */
/* ******************************************************************* */

static void scc_stop_calibrate(unsigned long channel)
{
	struct scc_channel *scc = (struct scc_channel *) channel;
	unsigned long flags;
	
	save_flags(flags);
	cli();

	del_timer(&scc->tx_wdog);
	scc_key_trx(scc, TX_OFF);
	wr(scc, R6, 0);
	wr(scc, R7, FLAG);
	Outb(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	Outb(scc->ctrl,RES_EXT_INT);

	scc_unlock_dev(scc);
	
	restore_flags(flags);
}


static void
scc_start_calibrate(struct scc_channel *scc, int duration, unsigned char pattern)
{
	unsigned long flags;
	
	save_flags(flags);
	cli();

	scc_lock_dev(scc);
	scc_discard_buffers(scc);

	del_timer(&scc->tx_wdog);

	scc->tx_wdog.data = (unsigned long) scc;
	scc->tx_wdog.function = scc_stop_calibrate;
	scc->tx_wdog.expires = jiffies + HZ*duration;
	add_timer(&scc->tx_wdog);

	/* This doesn't seem to work. Why not? */	
	wr(scc, R6, 0);
	wr(scc, R7, pattern);

	/* 
	 * Don't know if this works. 
	 * Damn, where is my Z8530 programming manual...? 
	 */

	Outb(scc->ctrl,RES_EXT_INT);	/* reset ext/status interrupts */
	Outb(scc->ctrl,RES_EXT_INT);

	scc_key_trx(scc, TX_ON);
	
	restore_flags(flags);
}

/* ******************************************************************* */
/* *		Init channel structures, special HW, etc...	     * */
/* ******************************************************************* */

/*
 * Reset the Z8530s and setup special hardware
 */

static void z8530_init(void)
{
	struct scc_channel *scc;
	int chip, k;
	unsigned long flags;
	char *flag;


	printk(KERN_INFO "Init Z8530 driver: %u channels, IRQ", Nchips*2);
	
	flag=" ";
	for (k = 0; k < 16; k++)
		if (Ivec[k].used) 
		{
			printk("%s%d", flag, k);
			flag=",";
		}
	printk("\n");


	/* reset and pre-init all chips in the system */
	for (chip = 0; chip < Nchips; chip++)
	{
		scc=&SCC_Info[2*chip];
		if (!scc->ctrl) continue;

		/* Special SCC cards */

		if(scc->brand & EAGLE)			/* this is an EAGLE card */
			Outb(scc->special,0x08);	/* enable interrupt on the board */
			
		if(scc->brand & (PC100 | PRIMUS))	/* this is a PC100/PRIMUS card */
			Outb(scc->special,scc->option);	/* set the MODEM mode (0x22) */

			
		/* Reset and pre-init Z8530 */

		save_flags(flags);
		cli();
		
		Outb(scc->ctrl, 0);
		OutReg(scc->ctrl,R9,FHWRES);		/* force hardware reset */
		udelay(100);				/* give it 'a bit' more time than required */
		wr(scc, R2, chip*16);			/* interrupt vector */
		wr(scc, R9, VIS);			/* vector includes status */
		
        	restore_flags(flags);
        }

 
	Driver_Initialized = 1;
}

/*
 * Allocate device structure, err, instance, and register driver
 */

static int scc_net_setup(struct scc_channel *scc, unsigned char *name, int addev)
{
	unsigned char *buf;
	struct net_device *dev;

	if (dev_get(name))
	{
		printk(KERN_INFO "Z8530drv: device %s already exists.\n", name);
		return -EEXIST;
	}

	if ((scc->dev = (struct net_device *) kmalloc(sizeof(struct net_device), GFP_KERNEL)) == NULL)
		return -ENOMEM;

	dev = scc->dev;
	memset(dev, 0, sizeof(struct net_device));

	buf = (unsigned char *) kmalloc(10, GFP_KERNEL);
	strcpy(buf, name);

	dev->priv = (void *) scc;
	dev->name = buf;
	dev->init = scc_net_init;

	if ((addev? register_netdevice(dev) : register_netdev(dev)) != 0)
	{
		kfree(dev);
                return -EIO;
        }

	return 0;
}



/* ******************************************************************** */
/* *			    Network driver methods		      * */
/* ******************************************************************** */

static unsigned char ax25_bcast[AX25_ADDR_LEN] =
{'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '0' << 1};
static unsigned char ax25_nocall[AX25_ADDR_LEN] =
{'L' << 1, 'I' << 1, 'N' << 1, 'U' << 1, 'X' << 1, ' ' << 1, '1' << 1};

/* ----> Initialize device <----- */

static int scc_net_init(struct net_device *dev)
{
	dev_init_buffers(dev);
	
	dev->tx_queue_len    = 16;	/* should be enough... */

	dev->open            = scc_net_open;
	dev->stop	     = scc_net_close;

	dev->hard_start_xmit = scc_net_tx;
	dev->hard_header     = scc_net_header;
	dev->rebuild_header  = ax25_rebuild_header;
	dev->set_mac_address = scc_net_set_mac_address;
	dev->get_stats       = scc_net_get_stats;
	dev->do_ioctl        = scc_net_ioctl;

	memcpy(dev->broadcast, ax25_bcast,  AX25_ADDR_LEN);
	memcpy(dev->dev_addr,  ax25_nocall, AX25_ADDR_LEN);
 
	dev->flags      = 0;

	dev->type = ARPHRD_AX25;
	dev->hard_header_len = AX25_MAX_HEADER_LEN + AX25_BPQ_HEADER_LEN;
	dev->mtu = AX25_DEF_PACLEN;
	dev->addr_len = AX25_ADDR_LEN;

	return 0;
}

/* ----> open network device <---- */

static int scc_net_open(struct net_device *dev)
{
	struct scc_channel *scc = (struct scc_channel *) dev->priv;

	if (scc == NULL || scc->magic != SCC_MAGIC)
		return -ENODEV;

 	if (!scc->init)
		return -EINVAL;

	MOD_INC_USE_COUNT;
	
	scc->tx_buff = NULL;
	skb_queue_head_init(&scc->tx_queue);
 
	init_channel(scc);

	dev->tbusy = 0;
	dev->start = 1;

	return 0;
}

/* ----> close network device <---- */

static int scc_net_close(struct net_device *dev)
{
	struct scc_channel *scc = (struct scc_channel *) dev->priv;
	unsigned long flags;

	if (scc == NULL || scc->magic != SCC_MAGIC)
		return -ENODEV;

	MOD_DEC_USE_COUNT;

	save_flags(flags); 
	cli();
	
	Outb(scc->ctrl,0);		/* Make sure pointer is written */
	wr(scc,R1,0);			/* disable interrupts */
	wr(scc,R3,0);

	del_timer(&scc->tx_t);
	del_timer(&scc->tx_wdog);

	restore_flags(flags);
	
	scc_discard_buffers(scc);

	dev->tbusy = 1;
	dev->start = 0;

	return 0;
}

/* ----> receive frame, called from scc_rxint() <---- */

static void scc_net_rx(struct scc_channel *scc, struct sk_buff *skb)
{
	if (skb->len == 0)
	{
		kfree_skb(skb);
		return;
	}
		
	scc->dev_stat.rx_packets++;

	skb->dev      = scc->dev;
	skb->protocol = htons(ETH_P_AX25);
	skb->mac.raw  = skb->data;
	skb->pkt_type = PACKET_HOST;
	
	netif_rx(skb);
	return;
}

/* ----> transmit frame <---- */

static int scc_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct scc_channel *scc = (struct scc_channel *) dev->priv;
	unsigned long flags;
	char kisscmd;
	
	if (scc == NULL || scc->magic != SCC_MAGIC || dev->tbusy)
	{
		dev_kfree_skb(skb);
		return 0;
	}

	if (skb->len > scc->stat.bufsize || skb->len < 2)
	{
		scc->dev_stat.tx_dropped++;	/* bogus frame */
		dev_kfree_skb(skb);
		return 0;
	}
	
	scc->dev_stat.tx_packets++;
	scc->stat.txframes++;
	
	kisscmd = *skb->data & 0x1f;
	skb_pull(skb, 1);

	if (kisscmd)
	{
		scc_set_param(scc, kisscmd, *skb->data);
		dev_kfree_skb(skb);
		return 0;
	}

	save_flags(flags);
	cli();
	
	if (skb_queue_len(&scc->tx_queue) > scc->dev->tx_queue_len)
	{
		struct sk_buff *skb_del;
		skb_del = __skb_dequeue(&scc->tx_queue);
		dev_kfree_skb(skb_del);
	}
	__skb_queue_tail(&scc->tx_queue, skb);

	dev->trans_start = jiffies;

	/*
	 * Start transmission if the trx state is idle or
	 * t_idle hasn't expired yet. Use dwait/persistance/slottime
	 * algorithm for normal halfduplex operation.
	 */

	if(scc->stat.tx_state == TXS_IDLE || scc->stat.tx_state == TXS_IDLE2)
	{
		scc->stat.tx_state = TXS_BUSY;
		if (scc->kiss.fulldup == KISS_DUPLEX_HALF)
			scc_start_tx_timer(scc, t_dwait, scc->kiss.waittime);
		else
			scc_start_tx_timer(scc, t_dwait, 0);
	}

	restore_flags(flags);

	return 0;
}

/* ----> ioctl functions <---- */

/*
 * SIOCSCCCFG		- configure driver	arg: (struct scc_hw_config *) arg
 * SIOCSCCINI		- initialize driver	arg: ---
 * SIOCSCCCHANINI	- initialize channel	arg: (struct scc_modem *) arg
 * SIOCSCCSMEM		- set memory		arg: (struct scc_mem_config *) arg
 * SIOCSCCGKISS		- get level 1 parameter	arg: (struct scc_kiss_cmd *) arg
 * SIOCSCCSKISS		- set level 1 parameter arg: (struct scc_kiss_cmd *) arg
 * SIOCSCCGSTAT		- get driver status	arg: (struct scc_stat *) arg
 * SIOCSCCCAL		- send calib. pattern	arg: (struct scc_calibrate *) arg
 */

static int scc_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct scc_kiss_cmd kiss_cmd;
	struct scc_mem_config memcfg;
	struct scc_hw_config hwcfg;
	struct scc_calibrate cal;
	int chan;
	unsigned char device_name[10];
	void *arg;
	struct scc_channel *scc;
	
	scc = (struct scc_channel *) dev->priv;
	if (scc == NULL || scc->magic != SCC_MAGIC)
		return -EINVAL;
		
	arg = (void *) ifr->ifr_data;
	
	if (!Driver_Initialized)
	{
		if (cmd == SIOCSCCCFG)
		{
			int found = 1;

			if (!suser()) return -EPERM;
			if (!arg) return -EFAULT;

			if (Nchips >= SCC_MAXCHIPS) 
				return -EINVAL;

			if (copy_from_user(&hwcfg, arg, sizeof(hwcfg)))
				return -EFAULT;

			if (hwcfg.irq == 2) hwcfg.irq = 9;

			if (!Ivec[hwcfg.irq].used && hwcfg.irq)
			{
				if (request_irq(hwcfg.irq, scc_isr, SA_INTERRUPT, "AX.25 SCC", NULL))
					printk(KERN_WARNING "z8530drv: warning, cannot get IRQ %d\n", hwcfg.irq);
				else
					Ivec[hwcfg.irq].used = 1;
			}

			if (hwcfg.vector_latch) 
				Vector_Latch = hwcfg.vector_latch;

			if (hwcfg.clock == 0)
				hwcfg.clock = SCC_DEFAULT_CLOCK;

#ifndef SCC_DONT_CHECK
			disable_irq(hwcfg.irq);

			check_region(scc->ctrl, 1);
			Outb(hwcfg.ctrl_a, 0);
			OutReg(hwcfg.ctrl_a, R9, FHWRES);
			udelay(100);
			OutReg(hwcfg.ctrl_a,R13,0x55);		/* is this chip really there? */
			udelay(5);

			if (InReg(hwcfg.ctrl_a,R13) != 0x55)
				found = 0;

			enable_irq(hwcfg.irq);
#endif

			if (found)
			{
				SCC_Info[2*Nchips  ].ctrl = hwcfg.ctrl_a;
				SCC_Info[2*Nchips  ].data = hwcfg.data_a;
				SCC_Info[2*Nchips  ].irq  = hwcfg.irq;
				SCC_Info[2*Nchips+1].ctrl = hwcfg.ctrl_b;
				SCC_Info[2*Nchips+1].data = hwcfg.data_b;
				SCC_Info[2*Nchips+1].irq  = hwcfg.irq;
			
				SCC_ctrl[Nchips].chan_A = hwcfg.ctrl_a;
				SCC_ctrl[Nchips].chan_B = hwcfg.ctrl_b;
				SCC_ctrl[Nchips].irq    = hwcfg.irq;
			}


			for (chan = 0; chan < 2; chan++)
			{
				sprintf(device_name, "%s%i", SCC_DriverName, 2*Nchips+chan);

				SCC_Info[2*Nchips+chan].special = hwcfg.special;
				SCC_Info[2*Nchips+chan].clock = hwcfg.clock;
				SCC_Info[2*Nchips+chan].brand = hwcfg.brand;
				SCC_Info[2*Nchips+chan].option = hwcfg.option;
				SCC_Info[2*Nchips+chan].enhanced = hwcfg.escc;

#ifdef SCC_DONT_CHECK
				printk(KERN_INFO "%s: data port = 0x%3.3x  control port = 0x%3.3x\n",
					device_name, 
					SCC_Info[2*Nchips+chan].data, 
					SCC_Info[2*Nchips+chan].ctrl);

#else
				printk(KERN_INFO "%s: data port = 0x%3.3lx  control port = 0x%3.3lx -- %s\n",
					device_name,
					chan? hwcfg.data_b : hwcfg.data_a, 
					chan? hwcfg.ctrl_b : hwcfg.ctrl_a,
					found? "found" : "missing");
#endif

				if (found)
				{
					request_region(SCC_Info[2*Nchips+chan].ctrl, 1, "scc ctrl");
					request_region(SCC_Info[2*Nchips+chan].data, 1, "scc data");
					if (Nchips+chan != 0)
						scc_net_setup(&SCC_Info[2*Nchips+chan], device_name, 1);
				}
			}
			
			if (found) Nchips++;
			
			return 0;
		}
		
		if (cmd == SIOCSCCINI)
		{
			if (!suser())
				return -EPERM;
				
			if (Nchips == 0)
				return -EINVAL;

			z8530_init();
			return 0;
		}
		
		return -EINVAL;	/* confuse the user */
	}
	
	if (!scc->init)
	{
		if (cmd == SIOCSCCCHANINI)
		{
			if (!suser()) return -EPERM;
			if (!arg) return -EINVAL;
			
			scc->stat.bufsize   = SCC_BUFSIZE;

			if (copy_from_user(&scc->modem, arg, sizeof(struct scc_modem)))
				return -EINVAL;
			
			/* default KISS Params */
		
			if (scc->modem.speed < 4800)
			{
				scc->kiss.txdelay = 36;		/* 360 ms */
				scc->kiss.persist = 42;		/* 25% persistence */			/* was 25 */
				scc->kiss.slottime = 16;	/* 160 ms */
				scc->kiss.tailtime = 4;		/* minimal reasonable value */
				scc->kiss.fulldup = 0;		/* CSMA */
				scc->kiss.waittime = 50;	/* 500 ms */
				scc->kiss.maxkeyup = 10;	/* 10 s */
				scc->kiss.mintime = 3;		/* 3 s */
				scc->kiss.idletime = 30;	/* 30 s */
				scc->kiss.maxdefer = 120;	/* 2 min */
				scc->kiss.softdcd = 0;		/* hardware dcd */
			} else {
				scc->kiss.txdelay = 10;		/* 100 ms */
				scc->kiss.persist = 64;		/* 25% persistence */			/* was 25 */
				scc->kiss.slottime = 8;		/* 160 ms */
				scc->kiss.tailtime = 1;		/* minimal reasonable value */
				scc->kiss.fulldup = 0;		/* CSMA */
				scc->kiss.waittime = 50;	/* 500 ms */
				scc->kiss.maxkeyup = 7;		/* 7 s */
				scc->kiss.mintime = 3;		/* 3 s */
				scc->kiss.idletime = 30;	/* 30 s */
				scc->kiss.maxdefer = 120;	/* 2 min */
				scc->kiss.softdcd = 0;		/* hardware dcd */
			}
			
			scc->tx_buff = NULL;
			skb_queue_head_init(&scc->tx_queue);
			scc->init = 1;
			
			return 0;
		}
		
		return -EINVAL;
	}
	
	switch(cmd)
	{
		case SIOCSCCRESERVED:
			return -ENOIOCTLCMD;

		case SIOCSCCSMEM:
			if (!suser()) return -EPERM;
			if (!arg || copy_from_user(&memcfg, arg, sizeof(memcfg)))
				return -EINVAL;
			scc->stat.bufsize   = memcfg.bufsize;
			return 0;
		
		case SIOCSCCGSTAT:
			if (!arg || copy_to_user(arg, &scc->stat, sizeof(scc->stat)))
				return -EINVAL;
			return 0;
		
		case SIOCSCCGKISS:
			if (!arg || copy_from_user(&kiss_cmd, arg, sizeof(kiss_cmd)))
				return -EINVAL;
			kiss_cmd.param = scc_get_param(scc, kiss_cmd.command);
			if (copy_to_user(arg, &kiss_cmd, sizeof(kiss_cmd)))
				return -EINVAL;
			return 0;
		
		case SIOCSCCSKISS:
			if (!suser()) return -EPERM;
			if (!arg || copy_from_user(&kiss_cmd, arg, sizeof(kiss_cmd)))
				return -EINVAL;
			return scc_set_param(scc, kiss_cmd.command, kiss_cmd.param);
		
		case SIOCSCCCAL:
			if (!suser()) return -EPERM;
			if (!arg || copy_from_user(&cal, arg, sizeof(cal)) || cal.time == 0)
				return -EINVAL;

			scc_start_calibrate(scc, cal.time, cal.pattern);
			return 0;

		default:
			return -ENOIOCTLCMD;
		
	}
	
	return -EINVAL;
}

/* ----> set interface callsign <---- */

static int scc_net_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = (struct sockaddr *) addr;
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	return 0;
}

/* ----> "hard" header <---- */

static int  scc_net_header(struct sk_buff *skb, struct net_device *dev, 
	unsigned short type, void *daddr, void *saddr, unsigned len)
{
	return ax25_encapsulate(skb, dev, type, daddr, saddr, len);
}

/* ----> get statistics <---- */

static struct net_device_stats *scc_net_get_stats(struct net_device *dev)
{
	struct scc_channel *scc = (struct scc_channel *) dev->priv;
	
	if (scc == NULL || scc->magic != SCC_MAGIC)
		return NULL;
		
	scc->dev_stat.rx_errors = scc->stat.rxerrs + scc->stat.rx_over;
	scc->dev_stat.tx_errors = scc->stat.txerrs + scc->stat.tx_under;
	scc->dev_stat.rx_fifo_errors = scc->stat.rx_over;
	scc->dev_stat.tx_fifo_errors = scc->stat.tx_under;

	return &scc->dev_stat;
}

/* ******************************************************************** */
/* *		dump statistics to /proc/net/z8530drv		      * */
/* ******************************************************************** */


static int scc_net_get_info(char *buffer, char **start, off_t offset, int length)
{
	struct scc_channel *scc;
	struct scc_kiss *kiss;
	struct scc_stat *stat;
	int len = 0;
	off_t pos = 0;
	off_t begin = 0;
	int k;

	len += sprintf(buffer, "z8530drv-"VERSION"\n");

	if (!Driver_Initialized)
	{
		len += sprintf(buffer+len, "not initialized\n");
		goto done;
	}

	if (!Nchips)
	{
		len += sprintf(buffer+len, "chips missing\n");
		goto done;
	}

	for (k = 0; k < Nchips*2; k++)
	{
		scc = &SCC_Info[k];
		stat = &scc->stat;
		kiss = &scc->kiss;

		if (!scc->init)
			continue;

		/* dev	data ctrl irq clock brand enh vector special option 
		 *	baud nrz clocksrc softdcd bufsize
		 *	rxints txints exints spints
		 *	rcvd rxerrs over / xmit txerrs under / nospace bufsize
		 *	txd pers slot tail ful wait min maxk idl defr txof grp
		 *	W ## ## ## ## ## ## ## ## ## ## ## ## ## ## ## ##
		 *	R ## ## XX ## ## ## ## ## XX ## ## ## ## ## ## ##
		 */

		len += sprintf(buffer+len, "%s\t%3.3lx %3.3lx %d %lu %2.2x %d %3.3lx %3.3lx %d\n",
				scc->dev->name,
				scc->data, scc->ctrl, scc->irq, scc->clock, scc->brand,
				scc->enhanced, Vector_Latch, scc->special,
				scc->option);
		len += sprintf(buffer+len, "\t%lu %d %d %d %d\n",
				scc->modem.speed, scc->modem.nrz,
				scc->modem.clocksrc, kiss->softdcd,
				stat->bufsize);
		len += sprintf(buffer+len, "\t%lu %lu %lu %lu\n",
				stat->rxints, stat->txints, stat->exints, stat->spints);
		len += sprintf(buffer+len, "\t%lu %lu %d / %lu %lu %d / %d %d\n",
				stat->rxframes, stat->rxerrs, stat->rx_over,
				stat->txframes, stat->txerrs, stat->tx_under,
				stat->nospace,  stat->tx_state);

#define K(x) kiss->x
		len += sprintf(buffer+len, "\t%d %d %d %d %d %d %d %d %d %d %d %d\n",
				K(txdelay), K(persist), K(slottime), K(tailtime),
				K(fulldup), K(waittime), K(mintime), K(maxkeyup),
				K(idletime), K(maxdefer), K(tx_inhibit), K(group));
#undef K
#ifdef SCC_DEBUG
		{
			int reg;

		len += sprintf(buffer+len, "\tW ");
			for (reg = 0; reg < 16; reg++)
				len += sprintf(buffer+len, "%2.2x ", scc->wreg[reg]);
			len += sprintf(buffer+len, "\n");
			
		len += sprintf(buffer+len, "\tR %2.2x %2.2x XX ", InReg(scc->ctrl,R0), InReg(scc->ctrl,R1));
			for (reg = 3; reg < 8; reg++)
				len += sprintf(buffer+len, "%2.2x ", InReg(scc->ctrl, reg));
			len += sprintf(buffer+len, "XX ");
			for (reg = 9; reg < 16; reg++)
				len += sprintf(buffer+len, "%2.2x ", InReg(scc->ctrl, reg));
			len += sprintf(buffer+len, "\n");
		}
#endif
		len += sprintf(buffer+len, "\n");

                pos = begin + len;

                if (pos < offset) {
                        len   = 0;
                        begin = pos;
                }

                if (pos > offset + length)
                        break;
	}

done:

        *start = buffer + (offset - begin);
        len   -= (offset - begin);

        if (len > length) len = length;

        return len;
}

#ifdef CONFIG_PROC_FS
#define scc_net_procfs_init() proc_net_create("z8530drv",0,scc_net_get_info)
#define scc_net_procfs_remove() proc_net_remove("z8530drv")
#else
#define scc_net_procfs_init()
#define scc_net_procfs_remove()
#endif

  
/* ******************************************************************** */
/* * 			Init SCC driver 			      * */
/* ******************************************************************** */

int __init scc_init (void)
{
	int chip, chan, k, result;
	char devname[10];
	
	printk(KERN_INFO BANNER);
	
	memset(&SCC_ctrl, 0, sizeof(SCC_ctrl));
	
	/* pre-init channel information */
	
	for (chip = 0; chip < SCC_MAXCHIPS; chip++)
	{
		memset((char *) &SCC_Info[2*chip  ], 0, sizeof(struct scc_channel));
		memset((char *) &SCC_Info[2*chip+1], 0, sizeof(struct scc_channel));
		
		for (chan = 0; chan < 2; chan++)
			SCC_Info[2*chip+chan].magic = SCC_MAGIC;
	}

	for (k = 0; k < 16; k++) Ivec[k].used = 0;

	sprintf(devname,"%s0", SCC_DriverName);
	
	result = scc_net_setup(SCC_Info, devname, 0);
	if (result)
	{
		printk(KERN_ERR "z8530drv: cannot initialize module\n");
		return result;
	}

	scc_net_procfs_init();

	return 0;
}

/* ******************************************************************** */
/* *			    Module support 			      * */
/* ******************************************************************** */


#ifdef MODULE
int init_module(void)
{
	int result = 0;
	
	result = scc_init();

	if (result == 0)
		printk(KERN_INFO "Copyright 1993,1998 Joerg Reuter DL1BKE (jreuter@poboxes.com)\n");
		
	return result;
}

void cleanup_module(void)
{
	long flags;
	io_port ctrl;
	int k;
	struct scc_channel *scc;
	
	save_flags(flags); 
	cli();

	if (Nchips == 0)
		unregister_netdev(SCC_Info[0].dev);

	for (k = 0; k < Nchips; k++)
		if ( (ctrl = SCC_ctrl[k].chan_A) )
		{
			Outb(ctrl, 0);
			OutReg(ctrl,R9,FHWRES);	/* force hardware reset */
			udelay(50);
		}
		
	for (k = 0; k < Nchips*2; k++)
	{
		scc = &SCC_Info[k];
		if (scc)
		{
			release_region(scc->ctrl, 1);
			release_region(scc->data, 1);
			if (scc->dev)
			{
				unregister_netdev(scc->dev);
				kfree(scc->dev);
			}
		}
	}
	
	for (k=0; k < 16 ; k++)
		if (Ivec[k].used) free_irq(k, NULL);
		
	restore_flags(flags);

	scc_net_procfs_remove();
}
#endif
