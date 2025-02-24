/*	ni5010.c: A network driver for the MiCom-Interlan NI5010 ethercard.
 *
 *	Copyright 1996,1997 Jan-Pascal van Best and Andreas Mohr.
 *
 *	This software may be used and distributed according to the terms
 *	of the GNU Public License, incorporated herein by reference.
 *
 * 	The authors may be reached as:
 *		jvbest@wi.leidenuniv.nl		a.mohr@mailto.de
 * 	or by snail mail as
 * 		Jan-Pascal van Best		Andreas Mohr
 *		Klikspaanweg 58-4		Stauferstr. 6
 *		2324 LZ  Leiden			D-71272 Renningen
 *		The Netherlands			Germany
 *
 *	Sources:
 * 	 	Donald Becker's "skeleton.c"
 *  		Crynwr ni5010 packet driver
 *
 *	Changes:
 *		v0.0: First test version
 *		v0.1: First working version
 *		v0.2:
 *		v0.3->v0.90: Now demand setting io and irq when loading as module
 *	970430	v0.91: modified for Linux 2.1.14
 *		v0.92: Implemented Andreas' (better) NI5010 probe
 *	970503	v0.93: Fixed auto-irq failure on warm reboot (JB)
 *	970623	v1.00: First kernel version (AM)
 *	970814	v1.01: Added detection of onboard receive buffer size (AM)
 *	Bugs:
 *		- None known...
 *		- Note that you have to patch ifconfig for the new /proc/net/dev
 *		format. It gives incorrect stats otherwise.
 *
 *	To do:
 *		Fix all bugs :-)
 *		Move some stuff to chipset_init()
 *		Handle xmt errors other than collisions
 *		Complete merge with Andreas' driver
 *		Implement ring buffers (Is this useful? You can't squeeze
 *			too many packet in a 2k buffer!)
 *		Implement DMA (Again, is this useful? Some docs says DMA is
 *			slower than programmed I/O)
 *
 *	Compile with:
 *		gcc -O2 -fomit-frame-pointer -m486 -D__KERNEL__ \
 *			-DMODULE -c ni5010.c 
 *
 *	Insert with e.g.:
 *		insmod ni5010.o io=0x300 irq=5 	
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include "ni5010.h"

static const char *boardname = "NI5010";
static char *version =
	"ni5010.c: v1.00 06/23/97 Jan-Pascal van Best and Andreas Mohr\n";
	
/* bufsize_rcv == 0 means autoprobing */
unsigned int bufsize_rcv = 0;

#define jumpered_interrupts	/* IRQ line jumpered on board */
#undef jumpered_dma		/* No DMA used */
#undef FULL_IODETECT		/* Only detect in portlist */

#ifndef FULL_IODETECT
/* A zero-terminated list of I/O addresses to be probed. */
static unsigned int ni5010_portlist[] __initdata =
	{ 0x300, 0x320, 0x340, 0x360, 0x380, 0x3a0, 0 };
#endif

/* Use 0 for production, 1 for verification, >2 for debug */
#ifndef NI5010_DEBUG
#define NI5010_DEBUG 0
#endif

/* Information that needs to be kept for each board. */
struct ni5010_local {
	struct net_device_stats stats;
	int o_pkt_size;
	int i_pkt_size;
};

/* Index to functions, as function prototypes. */

extern int 	ni5010_probe(struct net_device *dev);
static int	ni5010_probe1(struct net_device *dev, int ioaddr);
static int	ni5010_open(struct net_device *dev);
static int	ni5010_send_packet(struct sk_buff *skb, struct net_device *dev);
static void	ni5010_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void	ni5010_rx(struct net_device *dev);
static int	ni5010_close(struct net_device *dev);
static struct net_device_stats *ni5010_get_stats(struct net_device *dev);
static void 	ni5010_set_multicast_list(struct net_device *dev);
static void	reset_receiver(struct net_device *dev);

static int	process_xmt_interrupt(struct net_device *dev);
#define tx_done(dev) 1
extern void	hardware_send_packet(struct net_device *dev, char *buf, int length);
extern void 	chipset_init(struct net_device *dev, int startp);
static void	dump_packet(void *buf, int len);
static void 	show_registers(struct net_device *dev);


int __init ni5010_probe(struct net_device *dev)
{
	int *port;

	int base_addr = dev ? dev->base_addr : 0;

        PRINTK2((KERN_DEBUG "%s: Entering ni5010_probe\n", dev->name));
        
	if (base_addr > 0x1ff)		/* Check a single specified location. */
		return ni5010_probe1(dev, base_addr);
	else if (base_addr != 0)	/* Don't probe at all. */
		return -ENXIO;

#ifdef FULL_IODETECT
		for (int ioaddr=0x200; ioaddr<0x400; ioaddr+=0x20) {
			if (check_region(ioaddr, NI5010_IO_EXTENT))
				continue;
			if (ni5010_probe1(dev, ioaddr) == 0)
				return 0;
		}
#else
		for (port = ni5010_portlist; *port; port++) {
			int ioaddr = *port;
			if (check_region(ioaddr, NI5010_IO_EXTENT))
				continue;
			if (ni5010_probe1(dev, ioaddr) == 0)
				return 0;
		}
#endif	/* FULL_IODETECT */
	return -ENODEV;
}

static inline int rd_port(int ioaddr)
{
	inb(IE_RBUF);
	return inb(IE_SAPROM);
}

void __init trigger_irq(int ioaddr)
{
		outb(0x00, EDLC_RESET);	/* Clear EDLC hold RESET state */
		outb(0x00, IE_RESET);	/* Board reset */
		outb(0x00, EDLC_XMASK);	/* Disable all Xmt interrupts */
		outb(0x00, EDLC_RMASK); /* Disable all Rcv interrupt */
		outb(0xff, EDLC_XCLR);	/* Clear all pending Xmt interrupts */
		outb(0xff, EDLC_RCLR);	/* Clear all pending Rcv interrupts */
		/*
		 * Transmit packet mode: Ignore parity, Power xcvr,
		 * 	Enable loopback
		 */
		outb(XMD_IG_PAR | XMD_T_MODE | XMD_LBC, EDLC_XMODE);
		outb(RMD_BROADCAST, EDLC_RMODE); /* Receive normal&broadcast */
		outb(XM_ALL, EDLC_XMASK);	/* Enable all Xmt interrupts */
		udelay(50);			/* FIXME: Necessary? */
		outb(MM_EN_XMT|MM_MUX, IE_MMODE); /* Start transmission */
}

/*
 *      This is the real probe routine.  Linux has a history of friendly device
 *      probes on the ISA bus.  A good device probes avoids doing writes, and
 *      verifies that the correct device exists and functions.
 */

static int __init ni5010_probe1(struct net_device *dev, int ioaddr)
{
	static unsigned version_printed = 0;
	int i;
	unsigned int data;
	int boguscount = 40;

	/*
	 * This is no "official" probe method, I've rather tested which
	 * probe works best with my seven NI5010 cards
	 * (they have very different serial numbers)
	 * Suggestions or failure reports are very, very welcome !
	 * But I think it is a relatively good probe method
	 * since it doesn't use any "outb"
	 * It should be nearly 100% reliable !
	 * well-known WARNING: this probe method (like many others)
	 * will hang the system if a NE2000 card region is probed !
	 *
	 *   - Andreas
	 */
	
 	PRINTK2((KERN_DEBUG "%s: entering ni5010_probe1(%#3x)\n", 
 		dev->name, ioaddr));

	if (inb(ioaddr+0) == 0xff) return -ENODEV;

	while ( (rd_port(ioaddr) & rd_port(ioaddr) & rd_port(ioaddr) &
		 rd_port(ioaddr) & rd_port(ioaddr) & rd_port(ioaddr)) != 0xff)
	{
		if (boguscount-- == 0) return -ENODEV;
	}

	PRINTK2((KERN_DEBUG "%s: I/O #1 passed!\n", dev->name));

	for (i=0; i<32; i++)
		if ( (data = rd_port(ioaddr)) != 0xff) break;
	if (data==0xff) return -ENODEV;

	PRINTK2((KERN_DEBUG "%s: I/O #2 passed!\n", dev->name));

	if (		(data == SA_ADDR0) &&
	     (rd_port(ioaddr) == SA_ADDR1) &&
	     (rd_port(ioaddr) == SA_ADDR2) ) {
		for (i=0; i<4; i++) rd_port(ioaddr);
		if ( (rd_port(ioaddr) != NI5010_MAGICVAL1) ||
		     (rd_port(ioaddr) != NI5010_MAGICVAL2) ) {
		     	return -ENODEV;
		}
	} else return -ENODEV;
	
	PRINTK2((KERN_DEBUG "%s: I/O #3 passed!\n", dev->name));

	if (dev == NULL) {
		dev = init_etherdev(0,0);
		if (dev == NULL) {
			printk(KERN_WARNING "%s: Failed to allocate device memory\n", boardname);
			return -ENOMEM;
		}
	}

	if (NI5010_DEBUG && version_printed++ == 0)
		printk(KERN_INFO "%s", version);

	printk("NI5010 ethercard probe at 0x%x: ", ioaddr);

	dev->base_addr = ioaddr;

	for (i=0; i<6; i++) {
		outw(i, IE_GP);
		printk("%2.2x ", dev->dev_addr[i] = inb(IE_SAPROM));
	}

	PRINTK2((KERN_DEBUG "%s: I/O #4 passed!\n", dev->name));

#ifdef jumpered_interrupts
	if (dev->irq == 0xff)
		;
	else if (dev->irq < 2) {
		PRINTK2((KERN_DEBUG "%s: I/O #5 passed!\n", dev->name));

		autoirq_setup(0);
		trigger_irq(ioaddr);
		dev->irq = autoirq_report(2);

		PRINTK2((KERN_DEBUG "%s: I/O #6 passed!\n", dev->name));

		if (dev->irq == 0) {
			printk(KERN_WARNING "%s: no IRQ found!\n", dev->name);
			return -EAGAIN;
		}
		PRINTK2((KERN_DEBUG "%s: I/O #7 passed!\n", dev->name));
	} else if (dev->irq == 2) {
		dev->irq = 9;
	}
#endif	/* jumpered_irq */
	PRINTK2((KERN_DEBUG "%s: I/O #9 passed!\n", dev->name));

	/* DMA is not supported (yet?), so no use detecting it */

	if (dev->priv == NULL) {
		dev->priv = kmalloc(sizeof(struct ni5010_local), GFP_KERNEL|GFP_DMA);
		if (dev->priv == NULL) {
			printk(KERN_WARNING "%s: Failed to allocate private memory\n", dev->name);
			return -ENOMEM;
		}
	}

	PRINTK2((KERN_DEBUG "%s: I/O #10 passed!\n", dev->name));

/* get the size of the onboard receive buffer
 * higher addresses than bufsize are wrapped into real buffer
 * i.e. data for offs. 0x801 is written to 0x1 with a 2K onboard buffer
 */
	if (!bufsize_rcv) {
        	outb(1, IE_MMODE);      /* Put Rcv buffer on system bus */
        	outw(0, IE_GP);		/* Point GP at start of packet */
        	outb(0, IE_RBUF);	/* set buffer byte 0 to 0 */
        	for (i = 1; i < 0xff; i++) {
                	outw(i << 8, IE_GP); /* Point GP at packet size to be tested */
                	outb(i, IE_RBUF);
                	outw(0x0, IE_GP); /* Point GP at start of packet */
                	data = inb(IE_RBUF);
                	if (data == i) break;
        	}
		bufsize_rcv = i << 8;
        	outw(0, IE_GP);		/* Point GP at start of packet */
        	outb(0, IE_RBUF);	/* set buffer byte 0 to 0 again */
	}
        printk("// bufsize rcv/xmt=%d/%d\n", bufsize_rcv, NI5010_BUFSIZE);
	memset(dev->priv, 0, sizeof(struct ni5010_local));

	/* Grab the region so we can find another board if autoIRQ fails. */
	request_region(ioaddr, NI5010_IO_EXTENT, boardname);
	
	dev->open		= ni5010_open;
	dev->stop		= ni5010_close;
	dev->hard_start_xmit	= ni5010_send_packet;
	dev->get_stats		= ni5010_get_stats;
	dev->set_multicast_list = &ni5010_set_multicast_list;

	/* Fill in the fields of the device structure with ethernet values. */
	ether_setup(dev);
	
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 0;

	dev->flags &= ~IFF_MULTICAST;	/* Multicast doesn't work */

	/* Shut up the ni5010 */
	outb(0, EDLC_RMASK);	/* Mask all receive interrupts */
	outb(0, EDLC_XMASK);	/* Mask all xmit interrupts */
	outb(0xff, EDLC_RCLR);	/* Kill all pending rcv interrupts */
	outb(0xff, EDLC_XCLR); 	/* Kill all pending xmt interrupts */

	printk(KERN_INFO "%s: NI5010 found at 0x%x, using IRQ %d", dev->name, ioaddr, dev->irq);
	if (dev->dma) printk(" & DMA %d", dev->dma);
	printk(".\n");

	printk(KERN_INFO "Join the NI5010 driver development team!\n");
	printk(KERN_INFO "Mail to a.mohr@mailto.de or jvbest@wi.leidenuniv.nl\n");
	return 0;
}

/* 
 * Open/initialize the board.  This is called (in the current kernel)
 * sometime after booting when the 'ifconfig' program is run.
 *
 * This routine should set everything up anew at each open, even
 * registers that "should" only need to be set once at boot, so that
 * there is non-reboot way to recover if something goes wrong.
 */
   
static int ni5010_open(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	int i;

	PRINTK2((KERN_DEBUG "%s: entering ni5010_open()\n", dev->name)); 
	
	if (request_irq(dev->irq, &ni5010_interrupt, 0, boardname, dev)) {
		printk(KERN_WARNING "%s: Cannot get irq %#2x\n", dev->name, dev->irq);
		return -EAGAIN;
	}
	PRINTK3((KERN_DEBUG "%s: passed open() #1\n", dev->name));
        /*
         * Always allocate the DMA channel after the IRQ,
         * and clean up on failure.
         */
#ifdef jumpered_dma
        if (request_dma(dev->dma, cardname)) {
		printk(KERN_WARNING "%s: Cannot get dma %#2x\n", dev->name, dev->dma);
                free_irq(dev->irq, NULL);
                return -EAGAIN;
        }
#endif	/* jumpered_dma */

	PRINTK3((KERN_DEBUG "%s: passed open() #2\n", dev->name));
	/* Reset the hardware here.  Don't forget to set the station address. */

	outb(RS_RESET, EDLC_RESET);	/* Hold up EDLC_RESET while configing board */
	outb(0, IE_RESET);		/* Hardware reset of ni5010 board */
	outb(XMD_LBC, EDLC_XMODE);	/* Only loopback xmits */

	PRINTK3((KERN_DEBUG "%s: passed open() #3\n", dev->name));
	/* Set the station address */
	for(i = 0;i < 6; i++) {
		outb(dev->dev_addr[i], EDLC_ADDR + i);
	}
	
	PRINTK3((KERN_DEBUG "%s: Initialising ni5010\n", dev->name)); 
	outb(0, EDLC_XMASK);	/* No xmit interrupts for now */
	outb(XMD_IG_PAR | XMD_T_MODE | XMD_LBC, EDLC_XMODE); 
				/* Normal packet xmit mode */
	outb(0xff, EDLC_XCLR);	/* Clear all pending xmit interrupts */
	outb(RMD_BROADCAST, EDLC_RMODE);
				/* Receive broadcast and normal packets */
	reset_receiver(dev);	/* Ready ni5010 for receiving packets */
	
	outb(0, EDLC_RESET);	/* Un-reset the ni5010 */
	
	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;
	
	if (NI5010_DEBUG) show_registers(dev); 

    	MOD_INC_USE_COUNT;
	PRINTK((KERN_DEBUG "%s: open successful\n", dev->name));
     	return 0;
}

static void reset_receiver(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	
	PRINTK3((KERN_DEBUG "%s: resetting receiver\n", dev->name));
	outw(0, IE_GP);		/* Receive packet at start of buffer */
	outb(0xff, EDLC_RCLR);	/* Clear all pending rcv interrupts */
	outb(0, IE_MMODE);	/* Put EDLC to rcv buffer */
	outb(MM_EN_RCV, IE_MMODE); /* Enable rcv */
	outb(0xff, EDLC_RMASK);	/* Enable all rcv interrupts */
}

static int ni5010_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	PRINTK2((KERN_DEBUG "%s: entering ni5010_send_packet\n", dev->name));
	if (dev->tbusy) {
		/* 
                 * If we get here, some higher level has decided we are broken.
		 * There should really be a "kick me" function call instead. 
		 */
		int tickssofar = jiffies - dev->trans_start;
		if (tickssofar < 5)
			return 1;
		printk("tbusy\n");
		printk(KERN_WARNING "%s: transmit timed out, %s?\n", dev->name,
			   tx_done(dev) ? "IRQ conflict" : "network cable problem");
		/* Try to restart the adaptor. */
		/* FIXME: Give it a real kick here */
		chipset_init(dev, 1);
		dev->tbusy=0;
		dev->trans_start = jiffies;
	}

	/* 
         * Block a timer-based transmit from overlapping.  This could better be
	 * done with atomic_swap(1, dev->tbusy), but test_and_set_bit() works as well. 
	 */
	if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
		printk(KERN_WARNING "%s: Transmitter access conflict.\n", dev->name);
		return 1;
	} else {
		int length = ETH_ZLEN < skb->len ? skb->len : ETH_ZLEN;

		hardware_send_packet(dev, (unsigned char *)skb->data, length);
		dev->trans_start = jiffies;
	}
	dev_kfree_skb (skb);

	return 0;
}

/* 
 * The typical workload of the driver:
 * Handle the network interface interrupts. 
 */
static void 
ni5010_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev = dev_id;
	struct ni5010_local *lp;
	int ioaddr, status;
	int xmit_was_error = 0;

	if (dev == NULL || dev->irq != irq) {
		printk(KERN_WARNING "%s: irq %d for unknown device.\n", 
				boardname, irq);
		return;
	}

	if (dev->interrupt) printk(KERN_WARNING "%s: Reentering IRQ-handler!\n", dev->name);
	dev->interrupt = 1;

	PRINTK2((KERN_DEBUG "%s: entering ni5010_interrupt\n", dev->name));

	ioaddr = dev->base_addr;
	lp = (struct ni5010_local *)dev->priv;
	
	status = inb(IE_ISTAT); 
	PRINTK3((KERN_DEBUG "%s: IE_ISTAT = %#02x\n", dev->name, status));
		
        if ((status & IS_R_INT) == 0) ni5010_rx(dev);

        if ((status & IS_X_INT) == 0) {
                xmit_was_error = process_xmt_interrupt(dev);
        }

        if ((status & IS_DMA_INT) == 0) {
                PRINTK((KERN_DEBUG "%s: DMA complete (???)\n", dev->name));
                outb(0, IE_DMA_RST); /* Reset DMA int */
        }

	if (!xmit_was_error) 
		reset_receiver(dev); 

	dev->interrupt = 0;
	return;
}


static void dump_packet(void *buf, int len)
{
	int i;
	
	printk(KERN_DEBUG "Packet length = %#4x\n", len);
	for (i = 0; i < len; i++){
		if (i % 16 == 0) printk(KERN_DEBUG "%#4.4x", i);
		if (i % 2 == 0) printk(" ");
		printk("%2.2x", ((unsigned char *)buf)[i]);
		if (i % 16 == 15) printk("\n");
	}
	printk("\n");
	
	return;
}

/* We have a good packet, get it out of the buffer. */
static void
ni5010_rx(struct net_device *dev)
{
	struct ni5010_local *lp = (struct ni5010_local *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned char rcv_stat;
	struct sk_buff *skb;
	
	PRINTK2((KERN_DEBUG "%s: entering ni5010_rx()\n", dev->name)); 
	
	rcv_stat = inb(EDLC_RSTAT);
	PRINTK3((KERN_DEBUG "%s: EDLC_RSTAT = %#2x\n", dev->name, rcv_stat)); 
	
	if ( (rcv_stat & RS_VALID_BITS) != RS_PKT_OK) {
		PRINTK((KERN_INFO "%s: receive error.\n", dev->name));
		lp->stats.rx_errors++;
		if (rcv_stat & RS_RUNT) lp->stats.rx_length_errors++;
		if (rcv_stat & RS_ALIGN) lp->stats.rx_frame_errors++;
		if (rcv_stat & RS_CRC_ERR) lp->stats.rx_crc_errors++;
		if (rcv_stat & RS_OFLW) lp->stats.rx_fifo_errors++;
        	outb(0xff, EDLC_RCLR); /* Clear the interrupt */
		return;
	}
	
        outb(0xff, EDLC_RCLR);  /* Clear the interrupt */

	lp->i_pkt_size = inw(IE_RCNT);
	if (lp->i_pkt_size > ETH_FRAME_LEN || lp->i_pkt_size < 10 ) {
		PRINTK((KERN_DEBUG "%s: Packet size error, packet size = %#4.4x\n", 
			dev->name, lp->i_pkt_size));
		lp->stats.rx_errors++;
		lp->stats.rx_length_errors++;
		return;
	}

	/* Malloc up new buffer. */
	skb = dev_alloc_skb(lp->i_pkt_size + 3);
	if (skb == NULL) {
		printk(KERN_WARNING "%s: Memory squeeze, dropping packet.\n", dev->name);
		lp->stats.rx_dropped++;
		return;
	}
	
	skb->dev = dev;
	skb_reserve(skb, 2);
	
	/* Read packet into buffer */
        outb(MM_MUX, IE_MMODE); /* Rcv buffer to system bus */
	outw(0, IE_GP);	/* Seek to beginning of packet */
	insb(IE_RBUF, skb_put(skb, lp->i_pkt_size), lp->i_pkt_size); 
	
	if (NI5010_DEBUG >= 4) 
		dump_packet(skb->data, skb->len); 
		
	skb->protocol = eth_type_trans(skb,dev);
	netif_rx(skb);
	lp->stats.rx_packets++;
	lp->stats.rx_bytes += lp->i_pkt_size;

	PRINTK2((KERN_DEBUG "%s: Received packet, size=%#4.4x\n", 
		dev->name, lp->i_pkt_size));
	
}

static int process_xmt_interrupt(struct net_device *dev)
{
	struct ni5010_local *lp = (struct ni5010_local *)dev->priv;
	int ioaddr = dev->base_addr;
	int xmit_stat;

	PRINTK2((KERN_DEBUG "%s: entering process_xmt_interrupt\n", dev->name));

	xmit_stat = inb(EDLC_XSTAT);
	PRINTK3((KERN_DEBUG "%s: EDLC_XSTAT = %2.2x\n", dev->name, xmit_stat));
	
	outb(0, EDLC_XMASK);	/* Disable xmit IRQ's */
	outb(0xff, EDLC_XCLR);	/* Clear all pending xmit IRQ's */
	
	if (xmit_stat & XS_COLL){
                printk("ether collision\n"); /* FIXME: remove */
		PRINTK((KERN_DEBUG "%s: collision detected, retransmitting\n", 
			dev->name));
		outw(NI5010_BUFSIZE - lp->o_pkt_size, IE_GP);
		/* outb(0, IE_MMODE); */ /* xmt buf on sysbus FIXME: needed ? */
		outb(MM_EN_XMT | MM_MUX, IE_MMODE);
		outb(XM_ALL, EDLC_XMASK); /* Enable xmt IRQ's */
		lp->stats.collisions++;
		return 1;
	}

	/* FIXME: handle other xmt error conditions */

	lp->stats.tx_packets++;
	lp->stats.tx_bytes += lp->o_pkt_size;
	dev->tbusy = 0;
	mark_bh(NET_BH);	/* Inform upper layers. */
			
	PRINTK2((KERN_DEBUG "%s: sent packet, size=%#4.4x\n", 
		dev->name, lp->o_pkt_size));

	return 0;
}

/* The inverse routine to ni5010_open(). */
static int
ni5010_close(struct net_device *dev)
{
	int ioaddr = dev->base_addr;

	PRINTK2((KERN_DEBUG "%s: entering ni5010_close\n", dev->name));
#ifdef jumpered_interrupts	
	free_irq(dev->irq, NULL);
#endif
	/* Put card in held-RESET state */
	outb(0, IE_MMODE);
	outb(RS_RESET, EDLC_RESET);

        dev->tbusy = 1;
	dev->start = 0;

	MOD_DEC_USE_COUNT;
	PRINTK((KERN_DEBUG "%s: %s closed down\n", dev->name, boardname));
	return 0;

}

/* Get the current statistics.	This may be called with the card open or
   closed. */
static struct net_device_stats *
ni5010_get_stats(struct net_device *dev)
{
	struct ni5010_local *lp = (struct ni5010_local *)dev->priv;

	PRINTK2((KERN_DEBUG "%s: entering ni5010_get_stats\n", dev->name));
	
	if (NI5010_DEBUG) show_registers(dev);
	
	/* cli(); */
	/* Update the statistics from the device registers. */
	/* We do this in the interrupt handler */
	/* sti(); */

	return &lp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   num_addrs == -1      Promiscuous mode, receive all packets
   num_addrs == 0       Normal mode, clear multicast list
   num_addrs > 0        Multicast mode, receive normal and MC packets, and do
                        best-effort filtering.
*/
static void
ni5010_set_multicast_list(struct net_device *dev)
{
	short ioaddr = dev->base_addr;  

	PRINTK2((KERN_DEBUG "%s: entering set_multicast_list\n", dev->name));

	if (dev->flags&IFF_PROMISC || dev->flags&IFF_ALLMULTI) {
		dev->flags |= IFF_PROMISC;
		outb(RMD_PROMISC, EDLC_RMODE); /* Enable promiscuous mode */
		PRINTK((KERN_DEBUG "%s: Entering promiscuous mode\n", dev->name));
	} else if (dev->mc_list) {
		/* Sorry, multicast not supported */
		PRINTK((KERN_DEBUG "%s: No multicast, entering broadcast mode\n", dev->name));
		outb(RMD_BROADCAST, EDLC_RMODE);
	} else {
		PRINTK((KERN_DEBUG "%s: Entering broadcast mode\n", dev->name));
		outb(RMD_BROADCAST, EDLC_RMODE);  /* Disable promiscuous mode, use normal mode */
	}
}

extern void hardware_send_packet(struct net_device *dev, char *buf, int length)
{
	struct ni5010_local *lp = (struct ni5010_local *)dev->priv;
	int ioaddr = dev->base_addr;
	unsigned long flags;
	unsigned int buf_offs;

	PRINTK2((KERN_DEBUG "%s: entering hardware_send_packet\n", dev->name));
	
        if (length > ETH_FRAME_LEN) {
                PRINTK((KERN_WARNING "%s: packet too large, not possible\n",
                        dev->name));
                return;
        }

	if (NI5010_DEBUG) show_registers(dev);

	if (inb(IE_ISTAT) & IS_EN_XMT) {
		PRINTK((KERN_WARNING "%s: sending packet while already transmitting, not possible\n", 
			dev->name));
		return;
	}
	
	if (NI5010_DEBUG > 3) dump_packet(buf, length);

        buf_offs = NI5010_BUFSIZE - length;
        lp->o_pkt_size = length;

	save_flags(flags);	
	cli();

	outb(0, EDLC_RMASK);	/* Mask all receive interrupts */
	outb(0, IE_MMODE);	/* Put Xmit buffer on system bus */
	outb(0xff, EDLC_RCLR);	/* Clear out pending rcv interrupts */

	outw(buf_offs, IE_GP); /* Point GP at start of packet */
	outsb(IE_XBUF, buf, length); /* Put data in buffer */
	outw(buf_offs, IE_GP); /* Rewrite where packet starts */

	/* should work without that outb() (Crynwr used it) */
	/*outb(MM_MUX, IE_MMODE);*/ /* Xmt buffer to EDLC bus */
	outb(MM_EN_XMT | MM_MUX, IE_MMODE); /* Begin transmission */
	outb(XM_ALL, EDLC_XMASK); /* Cause interrupt after completion or fail */

	restore_flags(flags);

	if (NI5010_DEBUG) show_registers(dev);	
}

extern void chipset_init(struct net_device *dev, int startp)
{
	/* FIXME: Move some stuff here */
	PRINTK3((KERN_DEBUG "%s: doing NOTHING in chipset_init\n", dev->name));
}

static void show_registers(struct net_device *dev)
{
	int ioaddr = dev->base_addr;
	
	PRINTK3((KERN_DEBUG "%s: XSTAT %#2.2x\n", dev->name, inb(EDLC_XSTAT)));
	PRINTK3((KERN_DEBUG "%s: XMASK %#2.2x\n", dev->name, inb(EDLC_XMASK)));
	PRINTK3((KERN_DEBUG "%s: RSTAT %#2.2x\n", dev->name, inb(EDLC_RSTAT)));
	PRINTK3((KERN_DEBUG "%s: RMASK %#2.2x\n", dev->name, inb(EDLC_RMASK)));
	PRINTK3((KERN_DEBUG "%s: RMODE %#2.2x\n", dev->name, inb(EDLC_RMODE)));
	PRINTK3((KERN_DEBUG "%s: XMODE %#2.2x\n", dev->name, inb(EDLC_XMODE)));
	PRINTK3((KERN_DEBUG "%s: ISTAT %#2.2x\n", dev->name, inb(IE_ISTAT)));
}

#ifdef MODULE
static char devicename[9] = { 0, };
static struct net_device dev_ni5010 = {
        devicename,
        0, 0, 0, 0,
        0, 0,
        0, 0, 0, NULL, ni5010_probe };

int io  = 0;
int irq = 0;

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");

int init_module(void)
{
	int result;
	
	PRINTK2((KERN_DEBUG "%s: entering init_module\n", boardname));
	/*
	if(io <= 0 || irq == 0){
	   	printk(KERN_WARNING "%s: Autoprobing not allowed for modules.\n", boardname);
		printk(KERN_WARNING "%s: Set symbols 'io' and 'irq'\n", boardname);
	   	return -EINVAL;
	}
	*/
	if (io <= 0){
		printk(KERN_WARNING "%s: Autoprobing for modules is hazardous, trying anyway..\n", boardname);
	}

	PRINTK2((KERN_DEBUG "%s: init_module irq=%#2x, io=%#3x\n", boardname, irq, io));
        dev_ni5010.irq=irq;
        dev_ni5010.base_addr=io;
        if ((result = register_netdev(&dev_ni5010)) != 0) {
        	PRINTK((KERN_WARNING "%s: register_netdev returned %d.\n", 
        		boardname, result));
                return -EIO;
        }
        return 0;
}

void
cleanup_module(void)
{
	PRINTK2((KERN_DEBUG "%s: entering cleanup_module\n", boardname));

        unregister_netdev(&dev_ni5010);

	release_region(dev_ni5010.base_addr, NI5010_IO_EXTENT);
	if (dev_ni5010.priv != NULL){
	        kfree(dev_ni5010.priv);
	        dev_ni5010.priv = NULL;
	}
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c ni5010.c"
 *  version-control: t
 *  kept-new-versions: 5
 *  tab-width: 4
 * End:
 */
