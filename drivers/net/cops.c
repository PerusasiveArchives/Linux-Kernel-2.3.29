/*      cops.c: LocalTalk driver for Linux.
 *
 *	Authors:
 *      - Jay Schulist <Jay.Schulist@spacs.k12.wi.us>
 *
 *	With more than a little help from;
 *	- Alan Cox <Alan.Cox@linux.org> 
 *
 *      Derived from:
 *      - skeleton.c: A network driver outline for linux.
 *        Written 1993-94 by Donald Becker.
 *	- ltpc.c: A driver for the LocalTalk PC card.
 *	  Written by Bradford W. Johnson.
 *
 *      Copyright 1993 United States Government as represented by the
 *      Director, National Security Agency.
 *
 *      This software may be used and distributed according to the terms
 *      of the GNU Public License, incorporated herein by reference.
 *
 *	Changes:
 *	19970608	Alan Cox	Allowed dual card type support
 *					Can set board type in insmod
 *					Hooks for cops_setup routine
 *					(not yet implemented).
 *	19971101	Jay Schulist	Fixes for multiple lt* devices.
 *	19980607	Steven Hirsch	Fixed the badly broken support
 *					for Tangent type cards. Only
 *                                      tested on Daystar LT200. Some
 *                                      cleanup of formatting and program
 *                                      logic.  Added emacs 'local-vars'
 *                                      setup for Jay's brace style.
 */

static const char *version =
"cops.c:v0.04 6/7/98 Jay Schulist <Jay.Schulist@spacs.k12.wi.us>\n";
/*
 *  Sources:
 *      COPS Localtalk SDK. This provides almost all of the information
 *      needed.
 */

/*
 * insmod/modprobe configurable stuff.
 *	- IO Port, choose one your card supports or 0 if you dare.
 *	- IRQ, also choose one your card supports or nothing and let
 *	  the driver figure it out.
 */

#include <linux/config.h>
#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/ioport.h>
#include <linux/in.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/if_arp.h>
#include <linux/if_ltalk.h>	/* For ltalk_setup() */
#include <linux/delay.h>	/* For udelay() */
#include <linux/atalk.h>

#include "cops.h"		/* Our Stuff */
#include "cops_ltdrv.h"		/* Firmware code for Tangent type cards. */
#include "cops_ffdrv.h"		/* Firmware code for Dayna type cards. */

/*
 *      The name of the card. Is used for messages and in the requests for
 *      io regions, irqs and dma channels
 */

static const char *cardname = "cops";

#ifdef CONFIG_COPS_DAYNA
static int board_type = DAYNA;	/* Module exported */
#else
static int board_type = TANGENT;
#endif

#ifdef MODULE
static int io = 0x240;		/* Default IO for Dayna */
static int irq = 5;		/* Default IRQ */
#else
static int io = 0;		/* Default IO for Dayna */
static int irq = 0;		/* Default IRQ */
#endif

/*
 *	COPS Autoprobe information.
 *	Right now if port address is right but IRQ is not 5 this will
 *      return a 5 no matter what since we will still get a status response.
 *      Need one more additional check to narrow down after we have gotten
 *      the ioaddr. But since only other possible IRQs is 3 and 4 so no real
 *	hurry on this. I *STRONGLY* recommend using IRQ 5 for your card with
 *	this driver.
 * 
 *	This driver has 2 modes and they are: Dayna mode and Tangent mode.
 *	Each mode corresponds with the type of card. It has been found
 *	that there are 2 main types of cards and all other cards are
 *	the same and just have different names or only have minor differences
 *	such as more IO ports. As this driver is tested it will
 *	become more clear on exactly what cards are supported. The driver
 *	defaults to using Dayna mode. To change the drivers mode, simply
 *	select Dayna or Tangent mode when configuring the kernel.
 *
 *      This driver should support:
 *      TANGENT driver mode:
 *              Tangent ATB-II, Novell NL-1000, Daystar Digital LT-200,
 *		COPS LT-1
 *      DAYNA driver mode:
 *              Dayna DL2000/DaynaTalk PC (Half Length), COPS LT-95, 
 *		Farallon PhoneNET PC III, Farallon PhoneNET PC II
 *	Other cards possibly supported mode unkown though:
 *		Dayna DL2000 (Full length), COPS LT/M (Micro-Channel)
 *
 *	Cards NOT supported by this driver but supported by the ltpc.c
 *	driver written by Bradford W. Johnson <johns393@maroon.tc.umn.edu>
 *		Farallon PhoneNET PC
 *		Original Apple LocalTalk PC card
 * 
 *      N.B.
 *
 *      The Daystar Digital LT200 boards do not support interrupt-driven
 *      IO.  You must specify 'irq=0xff' as a module parameter to invoke
 *      polled mode.  I also believe that the port probing logic is quite
 *      dangerous at best and certainly hopeless for a polled card.  Best to 
 *      specify both. - Steve H.
 *
 */

/*
 * Zero terminated list of IO ports to probe.
 */

static unsigned int cops_portlist[] = { 
	0x240, 0x340, 0x200, 0x210, 0x220, 0x230, 0x260, 
	0x2A0, 0x300, 0x310, 0x320, 0x330, 0x350, 0x360,
	0
};

/*
 * Zero terminated list of IRQ ports to probe.
 */

static int cops_irqlist[] = {
	5, 4, 3, 0 
};

static struct timer_list cops_timer;

/* use 0 for production, 1 for verification, 2 for debug, 3 for verbose debug */
#ifndef COPS_DEBUG
#define COPS_DEBUG 1 
#endif
static unsigned int cops_debug = COPS_DEBUG;

/* The number of low I/O ports used by the card. */
#define COPS_IO_EXTENT       8

/* Information that needs to be kept for each board. */

struct cops_local
{
        struct enet_statistics stats;
        int board;			/* Holds what board type is. */
	int nodeid;			/* Set to 1 once have nodeid. */
        unsigned char node_acquire;	/* Node ID when acquired. */
        struct at_addr node_addr;	/* Full node addres */
};

/* Index to functions, as function prototypes. */
extern int  cops_probe (struct net_device *dev);
static int  cops_probe1 (struct net_device *dev, int ioaddr);
static int  cops_irq (int ioaddr, int board);

static int  cops_open (struct net_device *dev);
static int  cops_jumpstart (struct net_device *dev);
static void cops_reset (struct net_device *dev, int sleep);
static void cops_load (struct net_device *dev);
static int  cops_nodeid (struct net_device *dev, int nodeid);

static void cops_interrupt (int irq, void *dev_id, struct pt_regs *regs);
static void cops_poll (unsigned long ltdev);
static void cops_rx (struct net_device *dev);
static int  cops_send_packet (struct sk_buff *skb, struct net_device *dev);
static void set_multicast_list (struct net_device *dev);
static int  cops_hard_header (struct sk_buff *skb, struct net_device *dev,
			      unsigned short type, void *daddr, void *saddr, 
			      unsigned len);

static int  cops_ioctl (struct net_device *dev, struct ifreq *rq, int cmd);
static int  cops_close (struct net_device *dev);
static struct enet_statistics *cops_get_stats (struct net_device *dev);


/*
 *      Check for a network adaptor of this type, and return '0' iff one exists.
 *      If dev->base_addr == 0, probe all likely locations.
 *      If dev->base_addr in [1..0x1ff], always return failure.
 *        otherwise go with what we pass in.
 */
int __init cops_probe(struct net_device *dev)
{
	int i;
        int base_addr = dev ? dev->base_addr : 0;
        
        if(base_addr == 0 && io)
        	base_addr=io;

        if(base_addr > 0x1ff)    /* Check a single specified location. */
                return cops_probe1(dev, base_addr);
	else if(base_addr != 0)  /* Don't probe at all. */
               	return -ENXIO;
	
	/* FIXME  Does this really work for cards which generate irq?
	 * It's definitely N.G. for polled Tangent. sh
	 * Dayna cards don't autoprobe well at all, but if your card is
	 * at IRQ 5 & IO 0x240 we find it every time. ;) JS
	 */
        for(i=0; cops_portlist[i]; i++) {
		int ioaddr = cops_portlist[i];
		if(check_region(ioaddr, COPS_IO_EXTENT))
                        continue;
                if(cops_probe1(dev, ioaddr) == 0)
                        return 0;
        }
	
        return -ENODEV;
}

/*
 *      This is the real probe routine. Linux has a history of friendly device
 *      probes on the ISA bus. A good device probes avoids doing writes, and
 *      verifies that the correct device exists and functions.
 */
static int __init cops_probe1(struct net_device *dev, int ioaddr)
{
        struct cops_local *lp;
	static unsigned version_printed = 0;

	int board = board_type;
	
        if(cops_debug && version_printed++ == 0)
		printk("%s", version);

        /*
         * Since this board has jumpered interrupts, allocate the interrupt
         * vector now. There is no point in waiting since no other device
         * can use the interrupt, and this marks the irq as busy. Jumpered
         * interrupts are typically not reported by the boards, and we must
         * used AutoIRQ to find them.
	 */
	switch (dev->irq)
	{
		case 0:
			/* COPS AutoIRQ routine */
			dev->irq = cops_irq(ioaddr, board);
			if(!dev->irq)
				return -EINVAL;	/* No IRQ found on this port */
			break;

		case 1:
			return -EINVAL;
			break;

		/* Fixup for users that don't know that IRQ 2 is really
		 * IRQ 9, or don't know which one to set.
		 */
		case 2:
			dev->irq = 9;
			break;

		/* Polled operation requested. Although irq of zero passed as
		 * a parameter tells the init routines to probe, we'll
		 * overload it to denote polled operation at runtime.
		 */
		case 0xff:
			dev->irq = 0;
			break;

		default:
			break;
	}

	/* Reserve any actual interrupt. */
	if(dev->irq && request_irq(dev->irq, &cops_interrupt, 0, cardname, dev))
		return -EINVAL;

	/* Grab the region so no one else tries to probe our ioports. */
	request_region(ioaddr, COPS_IO_EXTENT, cardname);
	dev->base_addr          = ioaddr;

	/* Initialize the private device structure. */
        dev->priv = kmalloc(sizeof(struct cops_local), GFP_KERNEL);
        if(dev->priv == NULL)
        	return -ENOMEM;

        lp = (struct cops_local *)dev->priv;
        memset(lp, 0, sizeof(struct cops_local));

	/* Copy local board variable to lp struct. */
	lp->board               = board;

	/* Fill in the fields of the device structure with LocalTalk values. */
	ltalk_setup(dev);

	dev->hard_start_xmit    = &cops_send_packet;
	dev->hard_header	= cops_hard_header;
        dev->get_stats          = cops_get_stats;
	dev->open               = cops_open;
        dev->stop               = cops_close;
        dev->do_ioctl           = &cops_ioctl;
	dev->set_multicast_list = &set_multicast_list;
        dev->mc_list            = NULL;

	/* Tell the user where the card is and what mode we're in. */
	if(board==DAYNA)
		printk("%s: %s at %#3x, using IRQ %d, in Dayna mode.\n", 
			dev->name, cardname, ioaddr, dev->irq);
	if(board==TANGENT) {
		if(dev->irq)
			printk("%s: %s at %#3x, IRQ %d, in Tangent mode\n", 
				dev->name, cardname, ioaddr, dev->irq);
		else
			printk("%s: %s at %#3x, using polled IO, in Tangent mode.\n", 
				dev->name, cardname, ioaddr);

	}
        return 0;
}

static int __init cops_irq (int ioaddr, int board)
{       /*
         * This does not use the IRQ to determine where the IRQ is. We just
         * assume that when we get a correct status response that it's the IRQ.
         * This really just verifies the IO port but since we only have access
         * to such a small number of IRQs (5, 4, 3) this is not bad.
         * This will probably not work for more than one card.
         */
        int irqaddr=0;
        int i, x, status;

        if(board==DAYNA)
        {
                outb(0, ioaddr+DAYNA_RESET);
                inb(ioaddr+DAYNA_RESET);
                udelay(333333);
        }
        if(board==TANGENT)
        {
                inb(ioaddr);
                outb(0, ioaddr);
                outb(0, ioaddr+TANG_RESET);
        }

        for(i=0; cops_irqlist[i] !=0; i++)
        {
                irqaddr = cops_irqlist[i];
                for(x = 0xFFFF; x>0; x --)    /* wait for response */
                {
                        if(board==DAYNA)
                        {
                                status = (inb(ioaddr+DAYNA_CARD_STATUS)&3);
                                if(status == 1)
                                        return irqaddr;
                        }
                        if(board==TANGENT)
                        {
                                if((inb(ioaddr+TANG_CARD_STATUS)& TANG_TX_READY) !=0)
                                        return irqaddr;
                        }
                }
        }
        return 0;       /* no IRQ found */
}

/*
 * Open/initialize the board. This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 */
static int cops_open(struct net_device *dev)
{
    struct cops_local *lp = (struct cops_local *)dev->priv;

	if(dev->irq==0)
	{
		/*
		 * I don't know if the Dayna-style boards support polled 
		 * operation.  For now, only allow it for Tangent.
		 */
		if(lp->board==TANGENT)	/* Poll 20 times per second */
		{
		    init_timer(&cops_timer);
		    cops_timer.function = cops_poll;
		    cops_timer.data 	= (unsigned long)dev;
		    cops_timer.expires 	= jiffies + 5;
		    add_timer(&cops_timer);
		} 
		else 
		{
			printk(KERN_WARNING "%s: No irq line set\n", dev->name);
			return -EAGAIN;
		}
	}

	cops_jumpstart(dev);	/* Start the card up. */

        dev->tbusy = 0;
        dev->interrupt = 0;
        dev->start = 1;

#ifdef MODULE
        MOD_INC_USE_COUNT;
#endif

        return 0;
}

/*
 *	This allows for a dynamic start/restart of the entire card.
 */
static int cops_jumpstart(struct net_device *dev)
{
	struct cops_local *lp = (struct cops_local *)dev->priv;

	/*
         *      Once the card has the firmware loaded and has acquired
         *      the nodeid, if it is reset it will lose it all.
         */
        cops_reset(dev,1);	/* Need to reset card before load firmware. */
        cops_load(dev);		/* Load the firmware. */

	/*
	 *	If atalkd already gave us a nodeid we will use that
	 *	one again, else we wait for atalkd to give us a nodeid
	 *	in cops_ioctl. This may cause a problem if someone steals
	 *	our nodeid while we are resetting.
	 */	
	if(lp->nodeid == 1)
		cops_nodeid(dev,lp->node_acquire);

	return 0;
}

static void tangent_wait_reset(int ioaddr)
{
	int timeout=0;

	while(timeout++ < 5 && (inb(ioaddr+TANG_CARD_STATUS)&TANG_TX_READY)==0)
		mdelay(1);   /* Wait 1 second */
}

/*
 *      Reset the LocalTalk board.
 */
static void cops_reset(struct net_device *dev, int sleep)
{
        struct cops_local *lp = (struct cops_local *)dev->priv;
        int ioaddr=dev->base_addr;

        if(lp->board==TANGENT)
        {
                inb(ioaddr);		/* Clear request latch. */
                outb(0,ioaddr);		/* Clear the TANG_TX_READY flop. */
                outb(0, ioaddr+TANG_RESET);	/* Reset the adapter. */

		tangent_wait_reset(ioaddr);
                outb(0, ioaddr+TANG_CLEAR_INT);
        }
        if(lp->board==DAYNA)
        {
                outb(0, ioaddr+DAYNA_RESET);	/* Assert the reset port */
                inb(ioaddr+DAYNA_RESET);	/* Clear the reset */
                if(sleep)
                {
                        long snap=jiffies;

			/* Let card finish initializing, about 1/3 second */
	                while(jiffies-snap<HZ/3)
                                schedule();
                }
                else
                        udelay(333333);
        }
        dev->tbusy=0;

	return;
}

static void cops_load (struct net_device *dev)
{
        struct ifreq ifr;
        struct ltfirmware *ltf= (struct ltfirmware *)&ifr.ifr_data;
        struct cops_local *lp=(struct cops_local *)dev->priv;
        int ioaddr=dev->base_addr;
	int length, i = 0;

        strcpy(ifr.ifr_name,"lt0");

        /* Get card's firmware code and do some checks on it. */
#ifdef CONFIG_COPS_DAYNA        
        if(lp->board==DAYNA)
        {
                ltf->length=sizeof(ffdrv_code);
                ltf->data=ffdrv_code;
        }
        else
#endif        
#ifdef CONFIG_COPS_TANGENT
        if(lp->board==TANGENT)
        {
                ltf->length=sizeof(ltdrv_code);
                ltf->data=ltdrv_code;
        }
        else
#endif
	{
		printk(KERN_INFO "%s; unsupported board type.\n", dev->name);
		return;
	}
	
        /* Check to make sure firmware is correct length. */
        if(lp->board==DAYNA && ltf->length!=5983)
        {
                printk(KERN_WARNING "%s: Firmware is not length of FFDRV.BIN.\n", dev->name);
                return;
        }
        if(lp->board==TANGENT && ltf->length!=2501)
        {
                printk(KERN_WARNING "%s: Firmware is not length of DRVCODE.BIN.\n", dev->name);
                return;
        }

        if(lp->board==DAYNA)
        {
                /*
                 *      We must wait for a status response
                 *      with the DAYNA board.
                 */
                while(++i<65536)
                {
                       if((inb(ioaddr+DAYNA_CARD_STATUS)&3)==1)
                                break;
                }

                if(i==65536)
                        return;
        }

        /*
         *      Upload the firmware and kick. Byte-by-byte works nicely here.
         */
	i=0;
        length = ltf->length;
        while(length--)
        {
                outb(ltf->data[i], ioaddr);
                i++;
        }

	if(cops_debug > 1)
		printk("%s: Uploaded firmware - %d bytes of %d bytes.\n", 
			dev->name, i, ltf->length);

        if(lp->board==DAYNA) 	/* Tell Dayna to run the firmware code. */
                outb(1, ioaddr+DAYNA_INT_CARD);
	else			/* Tell Tang to run the firmware code. */
		inb(ioaddr);

        if(lp->board==TANGENT)
        {
                tangent_wait_reset(ioaddr);
                inb(ioaddr);	/* Clear initial ready signal. */
        }

        return;
}

/*
 * 	Get the LocalTalk Nodeid from the card. We can suggest
 *	any nodeid 1-254. The card will try and get that exact
 *	address else we can specify 0 as the nodeid and the card
 *	will autoprobe for a nodeid.
 */
static int cops_nodeid (struct net_device *dev, int nodeid)
{
	struct cops_local *lp = (struct cops_local *) dev->priv;
	int ioaddr = dev->base_addr;

	if(lp->board == DAYNA)
        {
        	/* Empty any pending adapter responses. */
                while((inb(ioaddr+DAYNA_CARD_STATUS)&DAYNA_TX_READY)==0)
                {
			outb(0, ioaddr+COPS_CLEAR_INT);	/* Clear interrupts. */
        		if((inb(ioaddr+DAYNA_CARD_STATUS)&0x03)==DAYNA_RX_REQUEST)
                		cops_rx(dev);	/* Kick any packets waiting. */
			schedule();
                }

                outb(2, ioaddr);       	/* Output command packet length as 2. */
                outb(0, ioaddr);
                outb(LAP_INIT, ioaddr);	/* Send LAP_INIT command byte. */
                outb(nodeid, ioaddr);  	/* Suggest node address. */
        }

	if(lp->board == TANGENT)
        {
                /* Empty any pending adapter responses. */
                while(inb(ioaddr+TANG_CARD_STATUS)&TANG_RX_READY)
                {
			outb(0, ioaddr+COPS_CLEAR_INT);	/* Clear interrupt. */
                	cops_rx(dev);          	/* Kick out packets waiting. */
			schedule();
                }

		/* Not sure what Tangent does if nodeid picked is used. */
                if(nodeid == 0)	         		/* Seed. */
                	nodeid = jiffies&0xFF;		/* Get a random try */
                outb(2, ioaddr);        		/* Command length LSB */
                outb(0, ioaddr);       			/* Command length MSB */
                outb(LAP_INIT, ioaddr); 		/* Send LAP_INIT byte */
                outb(nodeid, ioaddr); 		  	/* LAP address hint. */
                outb(0xFF, ioaddr);     		/* Int. level to use */
        }

	lp->node_acquire=0;		/* Set nodeid holder to 0. */
        while(lp->node_acquire==0)	/* Get *True* nodeid finally. */
	{
		outb(0, ioaddr+COPS_CLEAR_INT);	/* Clear any interrupt. */

		if(lp->board == DAYNA)
		{
                	if((inb(ioaddr+DAYNA_CARD_STATUS)&0x03)==DAYNA_RX_REQUEST)
                		cops_rx(dev);	/* Grab the nodeid put in lp->node_acquire. */
		}
		if(lp->board == TANGENT)
		{	
			if(inb(ioaddr+TANG_CARD_STATUS)&TANG_RX_READY)
                                cops_rx(dev);   /* Grab the nodeid put in lp->node_acquire. */
		}
		schedule();
	}

	if(cops_debug > 1)
		printk(KERN_DEBUG "%s: Node ID %d has been acquired.\n", 
			dev->name, lp->node_acquire);

	lp->nodeid=1;	/* Set got nodeid to 1. */

        return 0;
}

/*
 *	Poll the Tangent type cards to see if we have work.
 */
static void cops_poll(unsigned long ltdev)
{
	int ioaddr, status;
	int boguscount = 0;

	struct net_device *dev = (struct net_device *)ltdev;

	del_timer(&cops_timer);

	if(dev == NULL)
		return;	/* We've been downed */

	ioaddr = dev->base_addr;
	do {
		status=inb(ioaddr+TANG_CARD_STATUS);
		if(status & TANG_RX_READY)
			cops_rx(dev);
		if(status & TANG_TX_READY)
			dev->tbusy = 0;
		status = inb(ioaddr+TANG_CARD_STATUS);
	} while((++boguscount < 20) && (status&(TANG_RX_READY|TANG_TX_READY)));

	cops_timer.expires = jiffies+5;
	add_timer(&cops_timer);

	return;
}

/*
 *      The typical workload of the driver:
 *      Handle the network interface interrupts.
 */
static void cops_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
        struct net_device *dev = dev_id;
        struct cops_local *lp;
        int ioaddr, status;
        int boguscount = 0;

        if(dev == NULL)
        {
                printk(KERN_WARNING "%s: irq %d for unknown device.\n",
			cardname, irq);
                return;
        }
        dev->interrupt = 1;

        ioaddr = dev->base_addr;
        lp = (struct cops_local *)dev->priv;

	if(lp->board==DAYNA)
	{
		do {
			outb(0, ioaddr + COPS_CLEAR_INT);
                       	status=inb(ioaddr+DAYNA_CARD_STATUS);
                       	if((status&0x03)==DAYNA_RX_REQUEST)
                       	        cops_rx(dev);
			dev->tbusy = 0;
			mark_bh(NET_BH);
		} while(++boguscount < 20);
	}
	else
	{
		do {
                       	status=inb(ioaddr+TANG_CARD_STATUS);
			if(status & TANG_RX_READY)
				cops_rx(dev);
			if(status & TANG_TX_READY)
				dev->tbusy = 0;
			status=inb(ioaddr+TANG_CARD_STATUS);
		} while((++boguscount < 20) &&
				(status&(TANG_RX_READY|TANG_TX_READY)));
	}

        dev->interrupt = 0;
        return;
}

/*
 *      We have a good packet(s), get it/them out of the buffers.
 */
static void cops_rx(struct net_device *dev)
{
        int pkt_len = 0;
        int rsp_type = 0;
        struct sk_buff *skb;
        struct cops_local *lp = (struct cops_local *)dev->priv;
        int ioaddr = dev->base_addr;
        int boguscount = 0;

        cli();  /* Disable interrupts. */

        if(lp->board==DAYNA)
        {
                outb(0, ioaddr);                /* Send out Zero length. */
                outb(0, ioaddr);
                outb(DATA_READ, ioaddr);        /* Send read command out. */

                /* Wait for DMA to turn around. */
                while(++boguscount<1000000)
                {
                        if((inb(ioaddr+DAYNA_CARD_STATUS)&0x03)==DAYNA_RX_READY)
                                break;
                }

                if(boguscount==1000000)
                {
                        printk(KERN_WARNING "%s: DMA timed out.\n",dev->name);
                        return;
                }
        }

        /* Get response length. */
	if(lp->board==DAYNA)
        	pkt_len = inb(ioaddr) & 0xFF;
	else
		pkt_len = inb(ioaddr) & 0x00FF;
        pkt_len |= (inb(ioaddr) << 8);
        /* Input IO code. */
        rsp_type=inb(ioaddr);

        /* Malloc up new buffer. */
        skb = dev_alloc_skb(pkt_len);
        if(skb == NULL)
        {
                printk(KERN_NOTICE "%s: Memory squeeze, dropping packet.\n",
			dev->name);
                lp->stats.rx_dropped++;
                while(pkt_len--)        /* Discard packet */
                        inb(ioaddr);
                return;
        }
        skb->dev = dev;
        skb_put(skb, pkt_len);
        skb->protocol = htons(ETH_P_LOCALTALK);

        insb(ioaddr, skb->data, pkt_len);               /* Eat the Data */

        if(lp->board==DAYNA)
                outb(1, ioaddr+DAYNA_INT_CARD);         /* Interrupt the card */

        sti();  /* Restore interrupts. */

        /* Check for bad response length */
        if(pkt_len < 0 || pkt_len > MAX_LLAP_SIZE)
        {
		printk(KERN_NOTICE "%s: Bad packet length of %d bytes.\n", 
			dev->name, pkt_len);
                lp->stats.tx_errors++;
                kfree_skb(skb);
                return;
        }

        /* Set nodeid and then get out. */
        if(rsp_type == LAP_INIT_RSP)
        {	/* Nodeid taken from received packet. */
                lp->node_acquire = skb->data[0];
                kfree_skb(skb);
                return;
        }

        /* One last check to make sure we have a good packet. */
        if(rsp_type != LAP_RESPONSE)
        {
                printk("%s: Bad packet type %d.\n", dev->name, rsp_type);
                lp->stats.tx_errors++;
                kfree_skb(skb);
                return;
        }

        skb->mac.raw    = skb->data;    /* Point to entire packet. */
        skb_pull(skb,3);
        skb->h.raw      = skb->data;    /* Point to data (Skip header). */

        /* Update the counters. */
        lp->stats.rx_packets++;
        lp->stats.rx_bytes += skb->len;

        /* Send packet to a higher place. */
        netif_rx(skb);

        return;
}

/*
 *	Make the card transmit a LocalTalk packet.
 */
static int cops_send_packet(struct sk_buff *skb, struct net_device *dev)
{
        struct cops_local *lp = (struct cops_local *)dev->priv;
        int ioaddr = dev->base_addr;

        if(dev->tbusy)
        {
                /*
                 * If we get here, some higher level has decided we are broken.
                 * There should really be a "kick me" function call instead.
                 */
                int tickssofar = jiffies - dev->trans_start;
                if(tickssofar < 5)
                        return 1;
		lp->stats.tx_errors++;
                if(lp->board==TANGENT)
                {
                        if((inb(ioaddr+TANG_CARD_STATUS)&TANG_TX_READY)==0)
                        	printk(KERN_WARNING "%s: No TX complete interrupt.\n", dev->name);
                }
                printk(KERN_WARNING "%s: Transmit timed out.\n", dev->name);
		cops_jumpstart(dev);	/* Restart the card. */
                dev->tbusy=0;
                dev->trans_start = jiffies;
        }

        /*
         * Block a timer-based transmit from overlapping. This could better be
         * done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	 */
        if(test_and_set_bit(0, (void*) &dev->tbusy) != 0)
                printk(KERN_WARNING "%s: Transmitter access conflict.\n",
			dev->name);
        else
        {
		cli();	/* Disable interrupts. */
		if(lp->board == DAYNA)	 /* Wait for adapter transmit buffer. */
			while((inb(ioaddr+DAYNA_CARD_STATUS)&DAYNA_TX_READY)==0);
		if(lp->board == TANGENT) /* Wait for adapter transmit buffer. */
			while((inb(ioaddr+TANG_CARD_STATUS)&TANG_TX_READY)==0);

		/* Output IO length. */
		outb(skb->len, ioaddr);
		if(lp->board == DAYNA)
                	outb(skb->len >> 8, ioaddr);
		else
			outb((skb->len >> 8)&0x0FF, ioaddr);

		/* Output IO code. */
                outb(LAP_WRITE, ioaddr);

		if(lp->board == DAYNA)	/* Check the transmit buffer again. */
                        while((inb(ioaddr+DAYNA_CARD_STATUS)&DAYNA_TX_READY)==0);

                outsb(ioaddr, skb->data, skb->len);	/* Send out the data. */

                if(lp->board==DAYNA)	/* Dayna requires you kick the card */
                        outb(1, ioaddr+DAYNA_INT_CARD);

		sti();	/* Restore interrupts. */

		/* Done sending packet, update counters and cleanup. */
		lp->stats.tx_packets++;
		lp->stats.tx_bytes += skb->len;
		dev->trans_start = jiffies;
	}

	dev_kfree_skb (skb);
	dev->tbusy = 0;

        return 0;
}

/*
 *	Dummy function to keep the Appletalk layer happy.
 */
 
static void set_multicast_list(struct net_device *dev)
{
        if(cops_debug >= 3)
		printk("%s: set_multicast_list executed\n", dev->name);
}

/*
 *      Another Dummy function to keep the Appletalk layer happy.
 */
 
static int cops_hard_header(struct sk_buff *skb, struct net_device *dev,
			    unsigned short type, void *daddr, void *saddr, 
			    unsigned len)
{
        if(cops_debug >= 3)
                printk("%s: cops_hard_header executed. Wow!\n", dev->name);
        return 0;
}

/*
 *      System ioctls for the COPS LocalTalk card.
 */
 
static int cops_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
        struct cops_local *lp = (struct cops_local *)dev->priv;
        struct sockaddr_at *sa=(struct sockaddr_at *)&ifr->ifr_addr;
        struct at_addr *aa=(struct at_addr *)&lp->node_addr;

        switch(cmd)
        {
                case SIOCSIFADDR:
			/* Get and set the nodeid and network # atalkd wants. */
			cops_nodeid(dev, sa->sat_addr.s_node);
			aa->s_net               = sa->sat_addr.s_net;
                        aa->s_node              = lp->node_acquire;

			/* Set broardcast address. */
                        dev->broadcast[0]       = 0xFF;
			
			/* Set hardware address. */
                        dev->dev_addr[0]        = aa->s_node;
                        dev->addr_len           = 1;
                        return 0;

                case SIOCGIFADDR:
                        sa->sat_addr.s_net      = aa->s_net;
                        sa->sat_addr.s_node     = aa->s_node;
                        return 0;

                default:
                        return -EOPNOTSUPP;
        }
}

/*
 *	The inverse routine to cops_open().
 */
 
static int cops_close(struct net_device *dev)
{
	struct cops_local *lp = (struct cops_local *)dev->priv;

	/* If we were running polled, yank the timer.
	 */
	if(lp->board==TANGENT && dev->irq==0)
		del_timer(&cops_timer);

        dev->tbusy = 1;
        dev->start = 0;

#ifdef MODULE
        MOD_DEC_USE_COUNT;
#endif
	
        return 0;
}

/*
 *      Get the current statistics.
 *      This may be called with the card open or closed.
 */
static struct enet_statistics *cops_get_stats(struct net_device *dev)
{
        struct cops_local *lp = (struct cops_local *)dev->priv;
        return &lp->stats;
}

#ifdef MODULE
static char lt_name[16];

static struct net_device cops0_dev =
{
	lt_name,	/* device name */
        0, 0, 0, 0,
        0x0, 0,  /* I/O address, IRQ */
        0, 0, 0, NULL, cops_probe
};


MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(board_type, "i");

int init_module(void)
{
        int result, err;

        if(io == 0)
		printk(KERN_WARNING "%s: You shouldn't autoprobe with insmod\n",
			cardname);

        /* Copy the parameters from insmod into the device structure. */
        cops0_dev.base_addr = io;
        cops0_dev.irq       = irq;

	err=dev_alloc_name(&cops0_dev, "lt%d");
        if(err < 0)
                return err;

        if((result = register_netdev(&cops0_dev)) != 0)
                return result;

        return 0;
}

void cleanup_module(void)
{
        /* No need to check MOD_IN_USE, as sys_delete_module() checks. */
	unregister_netdev(&cops0_dev);
	if(cops0_dev.priv)
                kfree_s(cops0_dev.priv, sizeof(struct cops_local));
	if(cops0_dev.irq)
		free_irq(cops0_dev.irq, &cops0_dev);
        release_region(cops0_dev.base_addr, COPS_IO_EXTENT);
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DMODVERSIONS -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O2 -c cops.c"
 *  c-basic-offset: 4
 *  c-file-offsets: ((substatement-open . 0))
 * End:
 */
