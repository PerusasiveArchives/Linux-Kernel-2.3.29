/* ne2.c: A NE/2 Ethernet Driver for Linux. */
/*
   Based on the NE2000 driver written by Donald Becker (1992-94).
   modified by Wim Dumon (Apr 1996)

   This software may be used and distributed according to the terms
   of the GNU Public License, incorporated herein by reference.

   The author may be reached as wimpie@linux.cc.kuleuven.ac.be

   Currently supported: NE/2
   This patch was never tested on other MCA-ethernet adapters, but it
   might work. Just give it a try and let me know if you have problems.
   Also mail me if it really works, please!

   Changelog:
   Mon Feb  3 16:26:02 MET 1997
   - adapted the driver to work with the 2.1.25 kernel
   - multiple ne2 support (untested)
   - module support (untested)

   Fri Aug 28 00:18:36 CET 1998 (David Weinehall)
   - fixed a few minor typos
   - made the MODULE_PARM conditional (it only works with the v2.1.x kernels)
   - fixed the module support (Now it's working...)

   Mon Sep  7 19:01:44 CET 1998 (David Weinehall)
   - added support for Arco Electronics AE/2-card (experimental)

   Mon Sep 14 09:53:42 CET 1998 (David Weinehall)
   - added support for Compex ENET-16MC/P (experimental) 

   Tue Sep 15 16:21:12 CET 1998 (David Weinehall, Magnus Jonsson, Tomas Ogren)
   - Miscellaneous bugfixes

   Tue Sep 19 16:21:12 CET 1998 (Magnus Jonsson)
   - Cleanup

   Wed Sep 23 14:33:34 CET 1998 (David Weinehall)
   - Restructuring and rewriting for v2.1.x compliance

   Wed Oct 14 17:19:21 CET 1998 (David Weinehall)
   - Added code that unregisters irq and proc-info
   - Version# bump

   Mon Nov 16 15:28:23 CET 1998 (Wim Dumon)
   - pass 'dev' as last parameter of request_irq in stead of 'NULL'   
   
   *    WARNING
	-------
	This is alpha-test software.  It is not guaranteed to work. As a
	matter of fact, I'm quite sure there are *LOTS* of bugs in here. I
	would like to hear from you if you use this driver, even if it works.
	If it doesn't work, be sure to send me a mail with the problems !
*/

static const char *version = "ne2.c:v0.91 Nov 16 1998 Wim Dumon <wimpie@kotnet.org>\n";

#include <linux/module.h>
#include <linux/version.h>

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
#include <linux/mca.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include "8390.h"



/* Some defines that people can play with if so inclined. */

/* Do we perform extra sanity checks on stuff ? */
/* #define NE_SANITY_CHECK */

/* Do we implement the read before write bugfix ? */
/* #define NE_RW_BUGFIX */

/* Do we have a non std. amount of memory? (in units of 256 byte pages) */
/* #define PACKETBUF_MEMSIZE	0x40 */


/* ---- No user-serviceable parts below ---- */

#define NE_BASE	 (dev->base_addr)
#define NE_CMD	 	0x00
#define NE_DATAPORT	0x10	/* NatSemi-defined port window offset. */
#define NE_RESET	0x20	/* Issue a read to reset, a write to clear. */
#define NE_IO_EXTENT	0x30

#define NE1SM_START_PG	0x20	/* First page of TX buffer */
#define NE1SM_STOP_PG 	0x40	/* Last page +1 of RX ring */
#define NESM_START_PG	0x40	/* First page of TX buffer */
#define NESM_STOP_PG	0x80	/* Last page +1 of RX ring */

/* From the .ADF file: */
static unsigned int addresses[7]=
		{0x1000, 0x2020, 0x8020, 0xa0a0, 0xb0b0, 0xc0c0, 0xc3d0};
static int irqs[4] = {3, 4, 5, 9};

struct ne2_adapters_t {
	unsigned int	id;
	char		*name;
};

const struct ne2_adapters_t ne2_adapters[] = {
	{ 0x6354, "Arco Ethernet Adapter AE/2" },
	{ 0x70DE, "Compex ENET-16 MC/P" },
	{ 0x7154, "Novell Ethernet Adapter NE/2" },
	{ 0x0000, NULL }
};

extern int netcard_probe(struct net_device *dev);

static int ne2_probe1(struct net_device *dev, int slot);

static int ne_open(struct net_device *dev);
static int ne_close(struct net_device *dev);

static void ne_reset_8390(struct net_device *dev);
static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr,
		int ring_page);
static void ne_block_input(struct net_device *dev, int count,
		struct sk_buff *skb, int ring_offset);
static void ne_block_output(struct net_device *dev, const int count,
		const unsigned char *buf, const int start_page);


/*
 * Note that at boot, this probe only picks up one card at a time.
 */

int __init ne2_probe(struct net_device *dev)
{
	static int current_mca_slot = -1;
	int i;
	int adapter_found = 0;

	/* Do not check any supplied i/o locations. 
	   POS registers usually don't fail :) */

	/* MCA cards have POS registers.  
	   Autodetecting MCA cards is extremely simple. 
	   Just search for the card. */

	for(i = 0; (ne2_adapters[i].name != NULL) && !adapter_found; i++) {
		current_mca_slot = 
			mca_find_unused_adapter(ne2_adapters[i].id, 0);

		if((current_mca_slot != MCA_NOTFOUND) && !adapter_found) {
			mca_set_adapter_name(current_mca_slot, 
					ne2_adapters[i].name);
			mca_mark_as_used(current_mca_slot);
			
			return ne2_probe1(dev, current_mca_slot);
		}
	}
	return ENODEV;
}


static int ne2_procinfo(char *buf, int slot, struct net_device *dev)
{
	int len=0;

	len += sprintf(buf+len, "The NE/2 Ethernet Adapter\n" );
	len += sprintf(buf+len, "Driver written by Wim Dumon ");
	len += sprintf(buf+len, "<wimpie@kotnet.org>\n"); 
	len += sprintf(buf+len, "Modified by ");
	len += sprintf(buf+len, "David Weinehall <tao@acc.umu.se>\n");
	len += sprintf(buf+len, "and by Magnus Jonsson <bigfoot@acc.umu.se>\n");
	len += sprintf(buf+len, "Based on the original NE2000 drivers\n" );
	len += sprintf(buf+len, "Base IO: %#x\n", (unsigned int)dev->base_addr);
	len += sprintf(buf+len, "IRQ    : %d\n", dev->irq);

#define HW_ADDR(i) dev->dev_addr[i]
	len += sprintf(buf+len, "HW addr : %x:%x:%x:%x:%x:%x\n", 
			HW_ADDR(0), HW_ADDR(1), HW_ADDR(2), 
			HW_ADDR(3), HW_ADDR(4), HW_ADDR(5) );
#undef HW_ADDR

	return len;
}

static int __init ne2_probe1(struct net_device *dev, int slot)
{
	int i, base_addr, irq;
	unsigned char POS;
	unsigned char SA_prom[32];
	const char *name = "NE/2";
	int start_page, stop_page;
	static unsigned version_printed = 0;

	/* We should have a "dev" from Space.c or the static module table. */
	if (dev == NULL) {
		printk(KERN_ERR "ne2.c: Passed a NULL device.\n");
		dev = init_etherdev(0, 0);
	}

	if (ei_debug && version_printed++ == 0)
		printk(version);

	printk("NE/2 ethercard found in slot %d:", slot);

	/* Read base IO and IRQ from the POS-registers */
	POS = mca_read_stored_pos(slot, 2);
	if(!(POS % 2)) {
		printk(" disabled.\n");
		return ENODEV;
	}

	i = (POS & 0xE)>>1;
	/* printk("Halleluja sdog, als er na de pijl een 1 staat is 1 - 1 == 0"
	   " en zou het moeten werken -> %d\n", i);
	   The above line was for remote testing, thanx to sdog ... */
	base_addr = addresses[i - 1];
	irq = irqs[(POS & 0x60)>>5];

#ifdef DEBUG
	printk("POS info : pos 2 = %#x ; base = %#x ; irq = %ld\n", POS,
			base_addr, irq);
#endif

#ifndef CRYNWR_WAY
	/* Reset the card the way they do it in the Crynwr packet driver */
	for (i=0; i<8; i++) 
		outb(0x0, base_addr + NE_RESET);
	inb(base_addr + NE_RESET);
	outb(0x21, base_addr + NE_CMD);
	if (inb(base_addr + NE_CMD) != 0x21) {
		printk("NE/2 adapter not responding\n");
		return ENODEV;
	}

	/* In the crynwr sources they do a RAM-test here. I skip it. I suppose
	   my RAM is okay.  Suppose your memory is broken.  Then this test
	   should fail and you won't be able to use your card.  But if I do not
	   test, you won't be able to use your card, neither.  So this test
	   won't help you. */

#else  /* _I_ never tested it this way .. Go ahead and try ...*/
	/* Reset card. Who knows what dain-bramaged state it was left in. */
	{ 
		unsigned long reset_start_time = jiffies;

		/* DON'T change these to inb_p/outb_p or reset will fail on 
		   clones.. */
		outb(inb(base_addr + NE_RESET), base_addr + NE_RESET);

		while ((inb_p(base_addr + EN0_ISR) & ENISR_RESET) == 0)
			if (jiffies - reset_start_time > 2*HZ/100) {
				printk(" not found (no reset ack).\n");
				return ENODEV;
			}

		outb_p(0xff, base_addr + EN0_ISR);         /* Ack all intr. */
	}
#endif


	/* Read the 16 bytes of station address PROM.
	   We must first initialize registers, similar to 
	   NS8390_init(eifdev, 0).
	   We can't reliably read the SAPROM address without this.
	   (I learned the hard way!). */
	{
		struct { 
			unsigned char value, offset; 
		} program_seq[] = {
						/* Select page 0 */
			{E8390_NODMA+E8390_PAGE0+E8390_STOP, E8390_CMD}, 
			{0x49,	EN0_DCFG},  /* Set WORD-wide (0x49) access. */
			{0x00,	EN0_RCNTLO},  /* Clear the count regs. */
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_IMR},  /* Mask completion irq. */
			{0xFF,	EN0_ISR},
			{E8390_RXOFF, EN0_RXCR},  /* 0x20  Set to monitor */
			{E8390_TXOFF, EN0_TXCR},  /* 0x02  and loopback mode. */
			{32,	EN0_RCNTLO},
			{0x00,	EN0_RCNTHI},
			{0x00,	EN0_RSARLO},  /* DMA starting at 0x0000. */
			{0x00,	EN0_RSARHI},
			{E8390_RREAD+E8390_START, E8390_CMD},
		};

		for (i = 0; i < sizeof(program_seq)/sizeof(program_seq[0]); i++)
			outb_p(program_seq[i].value, base_addr + 
				program_seq[i].offset);

	}
	for(i = 0; i < 6 /*sizeof(SA_prom)*/; i+=1) {
		SA_prom[i] = inb(base_addr + NE_DATAPORT);
	}

	start_page = NESM_START_PG;
	stop_page = NESM_STOP_PG;

	dev->irq=irq;

	/* Snarf the interrupt now.  There's no point in waiting since we cannot
	   share and the board will usually be enabled. */
	{
		int irqval = request_irq(dev->irq, ei_interrupt, 
				0, name, dev);
		if (irqval) {
			printk (" unable to get IRQ %d (irqval=%d).\n", 
					dev->irq, +irqval);
			return EAGAIN;
		}
	}

	dev->base_addr = base_addr;

	/* Allocate dev->priv and fill in 8390 specific dev fields. */
	if (ethdev_init(dev)) {
		printk (" unable to get memory for dev->priv.\n");
		free_irq(dev->irq, dev);
		return -ENOMEM;
	}

	request_region(base_addr, NE_IO_EXTENT, name);

	for(i = 0; i < ETHER_ADDR_LEN; i++) {
		printk(" %2.2x", SA_prom[i]);
		dev->dev_addr[i] = SA_prom[i];
	}

	printk("\n%s: %s found at %#x, using IRQ %d.\n",
			dev->name, name, base_addr, dev->irq);

	mca_set_adapter_procfn(slot, (MCA_ProcFn) ne2_procinfo, dev);

	ei_status.name = name;
	ei_status.tx_start_page = start_page;
	ei_status.stop_page = stop_page;
	ei_status.word16 = (2 == 2);

	ei_status.rx_start_page = start_page + TX_PAGES;
#ifdef PACKETBUF_MEMSIZE
	/* Allow the packet buffer size to be overridden by know-it-alls. */
	ei_status.stop_page = ei_status.tx_start_page + PACKETBUF_MEMSIZE;
#endif

	ei_status.reset_8390 = &ne_reset_8390;
	ei_status.block_input = &ne_block_input;
	ei_status.block_output = &ne_block_output;
	ei_status.get_8390_hdr = &ne_get_8390_hdr;
	
	ei_status.priv = slot;
	
	dev->open = &ne_open;
	dev->stop = &ne_close;
	NS8390_init(dev, 0);
	return 0;
}

static int ne_open(struct net_device *dev)
{
	ei_open(dev);
	MOD_INC_USE_COUNT;
	return 0;
}

static int ne_close(struct net_device *dev)
{
	if (ei_debug > 1)
		printk("%s: Shutting down ethercard.\n", dev->name);
	ei_close(dev);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* Hard reset the card.  This used to pause for the same period that a
   8390 reset command required, but that shouldn't be necessary. */
static void ne_reset_8390(struct net_device *dev)
{
	unsigned long reset_start_time = jiffies;

	if (ei_debug > 1) 
		printk("resetting the 8390 t=%ld...", jiffies);

	/* DON'T change these to inb_p/outb_p or reset will fail on clones. */
	outb(inb(NE_BASE + NE_RESET), NE_BASE + NE_RESET);

	ei_status.txing = 0;
	ei_status.dmaing = 0;

	/* This check _should_not_ be necessary, omit eventually. */
	while ((inb_p(NE_BASE+EN0_ISR) & ENISR_RESET) == 0)
		if (jiffies - reset_start_time > 2*HZ/100) {
			printk("%s: ne_reset_8390() did not complete.\n", 
					dev->name);
			break;
		}
	outb_p(ENISR_RESET, NE_BASE + EN0_ISR);	/* Ack intr. */
}

/* Grab the 8390 specific header. Similar to the block_input routine, but
   we don't need to be concerned with ring wrap as the header will be at
   the start of a page, so we optimize accordingly. */

static void ne_get_8390_hdr(struct net_device *dev, struct e8390_pkt_hdr *hdr, 
		int ring_page)
{

	int nic_base = dev->base_addr;

	/* This *shouldn't* happen. 
	   If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_get_8390_hdr "
				"[DMAstat:%d][irqlock:%d][intr:%ld].\n",
				dev->name, ei_status.dmaing, ei_status.irqlock,
				dev->interrupt);
		return;
	}

	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb_p(sizeof(struct e8390_pkt_hdr), nic_base + EN0_RCNTLO);
	outb_p(0, nic_base + EN0_RCNTHI);
	outb_p(0, nic_base + EN0_RSARLO);		/* On page boundary */
	outb_p(ring_page, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);

	if (ei_status.word16)
		insw(NE_BASE + NE_DATAPORT, hdr, 
				sizeof(struct e8390_pkt_hdr)>>1);
	else
		insb(NE_BASE + NE_DATAPORT, hdr, 
				sizeof(struct e8390_pkt_hdr));

	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

/* Block input and output, similar to the Crynwr packet driver.  If you
   are porting to a new ethercard, look at the packet driver source for
   hints. The NEx000 doesn't share the on-board packet memory -- you have
   to put the packet out through the "remote DMA" dataport using outb. */

static void ne_block_input(struct net_device *dev, int count, struct sk_buff *skb, 
		int ring_offset)
{
#ifdef NE_SANITY_CHECK
	int xfer_count = count;
#endif
	int nic_base = dev->base_addr;
	char *buf = skb->data;

	/* This *shouldn't* happen. 
	   If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_block_input "
				"[DMAstat:%d][irqlock:%d][intr:%ld].\n",
				dev->name, ei_status.dmaing, ei_status.irqlock,
				dev->interrupt);
		return;
	}
	ei_status.dmaing |= 0x01;
	outb_p(E8390_NODMA+E8390_PAGE0+E8390_START, nic_base+ NE_CMD);
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8, nic_base + EN0_RCNTHI);
	outb_p(ring_offset & 0xff, nic_base + EN0_RSARLO);
	outb_p(ring_offset >> 8, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16) {
		insw(NE_BASE + NE_DATAPORT,buf,count>>1);
		if (count & 0x01) {
			buf[count-1] = inb(NE_BASE + NE_DATAPORT);
#ifdef NE_SANITY_CHECK
			xfer_count++;
#endif
		}
	} else {
		insb(NE_BASE + NE_DATAPORT, buf, count);
	}

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here.  If you see
	   this message you either 1) have a slightly incompatible clone
	   or 2) have noise/speed problems with your bus. */
	if (ei_debug > 1) {	/* DMA termination address check... */
		int addr, tries = 20;
		do {
			/* DON'T check for 'inb_p(EN0_ISR) & ENISR_RDC' here
			   -- it's broken for Rx on some cards! */
			int high = inb_p(nic_base + EN0_RSARHI);
			int low = inb_p(nic_base + EN0_RSARLO);
			addr = (high << 8) + low;
			if (((ring_offset + xfer_count) & 0xff) == low)
				break;
		} while (--tries > 0);
		if (tries <= 0)
			printk("%s: RX transfer address mismatch,"
				"%#4.4x (expected) vs. %#4.4x (actual).\n",
				dev->name, ring_offset + xfer_count, addr);
	}
#endif
	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
}

static void ne_block_output(struct net_device *dev, int count,
		const unsigned char *buf, const int start_page)
{
	int nic_base = NE_BASE;
	unsigned long dma_start;
#ifdef NE_SANITY_CHECK
	int retries = 0;
#endif

	/* Round the count up for word writes. Do we need to do this?
	   What effect will an odd byte count have on the 8390?
	   I should check someday. */
	if (ei_status.word16 && (count & 0x01))
		count++;

	/* This *shouldn't* happen. 
	   If it does, it's the last thing you'll see */
	if (ei_status.dmaing) {
		printk("%s: DMAing conflict in ne_block_output."
				"[DMAstat:%d][irqlock:%d][intr:%ld]\n",
				dev->name, ei_status.dmaing, ei_status.irqlock,
				dev->interrupt);
		return;
	}
	ei_status.dmaing |= 0x01;
	/* We should already be in page 0, but to be safe... */
	outb_p(E8390_PAGE0+E8390_START+E8390_NODMA, nic_base + NE_CMD);

#ifdef NE_SANITY_CHECK
retry:
#endif

#ifdef NE8390_RW_BUGFIX
	/* Handle the read-before-write bug the same way as the
	   Crynwr packet driver -- the NatSemi method doesn't work.
	   Actually this doesn't always work either, but if you have
	   problems with your NEx000 this is better than nothing! */
	outb_p(0x42, nic_base + EN0_RCNTLO);
	outb_p(0x00, nic_base + EN0_RCNTHI);
	outb_p(0x42, nic_base + EN0_RSARLO);
	outb_p(0x00, nic_base + EN0_RSARHI);
	outb_p(E8390_RREAD+E8390_START, nic_base + NE_CMD);
	/* Make certain that the dummy read has occurred. */
	SLOW_DOWN_IO;
	SLOW_DOWN_IO;
	SLOW_DOWN_IO;
#endif

	outb_p(ENISR_RDC, nic_base + EN0_ISR);

	/* Now the normal output. */
	outb_p(count & 0xff, nic_base + EN0_RCNTLO);
	outb_p(count >> 8,   nic_base + EN0_RCNTHI);
	outb_p(0x00, nic_base + EN0_RSARLO);
	outb_p(start_page, nic_base + EN0_RSARHI);

	outb_p(E8390_RWRITE+E8390_START, nic_base + NE_CMD);
	if (ei_status.word16) {
		outsw(NE_BASE + NE_DATAPORT, buf, count>>1);
	} else {
		outsb(NE_BASE + NE_DATAPORT, buf, count);
	}

	dma_start = jiffies;

#ifdef NE_SANITY_CHECK
	/* This was for the ALPHA version only, but enough people have
	   been encountering problems so it is still here. */

	if (ei_debug > 1) {		/* DMA termination address check... */
		int addr, tries = 20;
		do {
			int high = inb_p(nic_base + EN0_RSARHI);
			int low = inb_p(nic_base + EN0_RSARLO);
			addr = (high << 8) + low;
			if ((start_page << 8) + count == addr)
				break;
		} while (--tries > 0);
		if (tries <= 0) {
			printk("%s: Tx packet transfer address mismatch,"
					"%#4.4x (expected) vs. %#4.4x (actual).\n",
					dev->name, (start_page << 8) + count, addr);
			if (retries++ == 0)
				goto retry;
		}
	}
#endif

	while ((inb_p(nic_base + EN0_ISR) & ENISR_RDC) == 0)
		if (jiffies - dma_start > 2*HZ/100) {		/* 20ms */
			printk("%s: timeout waiting for Tx RDC.\n", dev->name);
			ne_reset_8390(dev);
			NS8390_init(dev,1);
			break;
		}

	outb_p(ENISR_RDC, nic_base + EN0_ISR);	/* Ack intr. */
	ei_status.dmaing &= ~0x01;
	return;
}


#ifdef MODULE
#define MAX_NE_CARDS	4	/* Max number of NE cards per module */
#define NAMELEN		8	/* # of chars for storing dev->name */
static char namelist[NAMELEN * MAX_NE_CARDS] = { 0, };
static struct net_device dev_ne[MAX_NE_CARDS] = {
	{
		NULL,		/* assign a chunk of namelist[] below */
		0, 0, 0, 0,
		0, 0,
		0, 0, 0, NULL, NULL
	},
};

static int io[MAX_NE_CARDS] = { 0, };
static int irq[MAX_NE_CARDS]  = { 0, };
static int bad[MAX_NE_CARDS]  = { 0, };	/* 0xbad = bad sig or no reset ack */

#ifdef MODULE_PARM
MODULE_PARM(io, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
MODULE_PARM(bad, "1-" __MODULE_STRING(MAX_NE_CARDS) "i");
#endif

/* Module code fixed by David Weinehall */

int init_module(void)
{
	int this_dev, found = 0;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		struct net_device *dev = &dev_ne[this_dev];
		dev->name = namelist+(NAMELEN*this_dev);
		dev->irq = irq[this_dev];
		dev->mem_end = bad[this_dev];
		dev->base_addr = io[this_dev];
		dev->init = ne2_probe;
		if (register_netdev(dev) != 0) {
			if (found != 0) return 0;   /* Got at least one. */

			printk(KERN_WARNING "ne2.c: No NE/2 card found.\n");
			return -ENXIO;
		}
		found++;
	}
	return 0;
}

void cleanup_module(void)
{
	int this_dev;

	for (this_dev = 0; this_dev < MAX_NE_CARDS; this_dev++) {
		struct net_device *dev = &dev_ne[this_dev];
		if (dev->priv != NULL) {
			mca_mark_as_unused(ei_status.priv);
			mca_set_adapter_procfn( ei_status.priv, NULL, NULL);
			kfree(dev->priv);
			free_irq(dev->irq, dev);
			release_region(dev->base_addr, NE_IO_EXTENT);
			unregister_netdev(dev);
		}
	}
}
#endif /* MODULE */

/*
 * Local variables:
 *  compile-command: "gcc -DKERNEL -Wall -O6 -fomit-frame-pointer -I/usr/src/linux/net/tcp -c ne2.c"
 *  version-control: t
 *  kept-new-versions: 5
 * End:
 */
