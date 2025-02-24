/* Generic NS8390 register definitions. */
/* This file is part of Donald Becker's 8390 drivers, and is distributed
   under the same license. Auto-loading of 8390.o added by Paul Gortmaker.
   Some of these names and comments originated from the Crynwr
   packet drivers, which are distributed under the GPL. */

#ifndef _8390_h
#define _8390_h

#include <linux/config.h>
#include <linux/if_ether.h>
#include <linux/ioport.h>
#include <linux/skbuff.h>

/* With kmod, drivers can now load the 8390 module themselves! */
#if 0 /* def CONFIG_KMOD */
#define LOAD_8390_BY_KMOD
#endif

#define TX_2X_PAGES 12
#define TX_1X_PAGES 6

/* Should always use two Tx slots to get back-to-back transmits. */
#define EI_PINGPONG

#ifdef EI_PINGPONG
#define TX_PAGES TX_2X_PAGES
#else
#define TX_PAGES TX_1X_PAGES
#endif

#define ETHER_ADDR_LEN 6

/* The 8390 specific per-packet-header format. */
struct e8390_pkt_hdr {
  unsigned char status; /* status */
  unsigned char next;   /* pointer to next packet. */
  unsigned short count; /* header + packet length in bytes */
};

#ifdef notdef
extern int ei_debug;
#else
#define ei_debug 1
#endif

#ifndef HAVE_AUTOIRQ
/* From auto_irq.c */
extern void autoirq_setup(int waittime);
extern unsigned long autoirq_report(int waittime);
#endif

#if defined(LOAD_8390_BY_KMOD) && defined(MODULE) && !defined(NS8390_CORE)

/* Function pointers to be mapped onto the 8390 core support */
static int (*S_ethdev_init)(struct net_device *dev);
static void (*S_NS8390_init)(struct net_device *dev, int startp);
static int (*S_ei_open)(struct net_device *dev);
static int (*S_ei_close)(struct net_device *dev);
static void (*S_ei_interrupt)(int irq, void *dev_id, struct pt_regs *regs);


#define NS8390_KSYSMS_PRESENT	(			\
	get_module_symbol(NULL, "ethdev_init") != 0 &&	\
	get_module_symbol(NULL, "NS8390_init") != 0 &&	\
	get_module_symbol(NULL, "ei_open") != 0 &&	\
	get_module_symbol(NULL, "ei_close") != 0 &&	\
	get_module_symbol(NULL, "ei_interrupt") != 0)

extern __inline__ int load_8390_module(const char *driver)
{

	if (! NS8390_KSYSMS_PRESENT) {
		int (*request_mod)(const char *module_name);

		if (get_module_symbol("", "request_module") == 0) {
			printk("%s: module auto-load (kmod) support not present.\n", driver);
			printk("%s: unable to auto-load required 8390 module.\n", driver);
			printk("%s: try \"modprobe 8390\" as root 1st.\n", driver);
			return -ENOSYS;
		}

		request_mod = (void*)get_module_symbol("", "request_module");
		if (request_mod("8390")) {
			printk("%s: request to load the 8390 module failed.\n", driver);
			return -ENOSYS;
		}

		/* Check if module really loaded and is valid */
		if (! NS8390_KSYSMS_PRESENT) {
			printk("%s: 8390.o not found/invalid or failed to load.\n", driver);
			return -ENOSYS;
		}

		printk(KERN_INFO "%s: auto-loaded 8390 module.\n", driver);
	}

	/* Map the functions into place */
	S_ethdev_init = (void*)get_module_symbol(0, "ethdev_init");
	S_NS8390_init = (void*)get_module_symbol(0, "NS8390_init");
	S_ei_open = (void*)get_module_symbol(0, "ei_open");
	S_ei_close = (void*)get_module_symbol(0, "ei_close");
	S_ei_interrupt = (void*)get_module_symbol(0, "ei_interrupt");

	return 0;
}

/*
 * Since a kmod aware driver won't explicitly show a dependence on the
 * exported 8390 functions (due to the mapping above), the 8390 module
 * (if present, and not in-kernel) needs to be protected from garbage
 * collection.  NS8390_module is only defined for a modular 8390 core.
 */

extern __inline__  void lock_8390_module(void)
{
	struct module **mod = (struct module**)get_module_symbol(0, "NS8390_module");

	if (mod != NULL && *mod != NULL)
		__MOD_INC_USE_COUNT(*mod);
}
	
extern __inline__  void unlock_8390_module(void)
{
	struct module **mod = (struct module**)get_module_symbol(0, "NS8390_module");

	if (mod != NULL && *mod != NULL)
		__MOD_DEC_USE_COUNT(*mod);
}
	
/*
 * These are last so they only have scope over the driver
 * code (wd, ne, 3c503, etc.)  and not over the above code.
 */
#define ethdev_init S_ethdev_init
#define NS8390_init S_NS8390_init
#define ei_open S_ei_open
#define ei_close S_ei_close
#define ei_interrupt S_ei_interrupt

#else	/* not a module or kmod support not wanted */

#define load_8390_module(driver)	0
#define lock_8390_module()		do { } while (0)
#define unlock_8390_module()		do { } while (0)
extern int ethdev_init(struct net_device *dev);
extern void NS8390_init(struct net_device *dev, int startp);
extern int ei_open(struct net_device *dev);
extern int ei_close(struct net_device *dev);
extern void ei_interrupt(int irq, void *dev_id, struct pt_regs *regs);

#endif

/* Most of these entries should be in 'struct net_device' (or most of the
   things in there should be here!) */
/* You have one of these per-board */
struct ei_device {
	const char *name;
	void (*reset_8390)(struct net_device *);
	void (*get_8390_hdr)(struct net_device *, struct e8390_pkt_hdr *, int);
	void (*block_output)(struct net_device *, int, const unsigned char *, int);
	void (*block_input)(struct net_device *, int, struct sk_buff *, int);
	unsigned char mcfilter[8];
	unsigned open:1;
	unsigned word16:1;  		/* We have the 16-bit (vs 8-bit) version of the card. */
	unsigned txing:1;		/* Transmit Active */
	unsigned irqlock:1;		/* 8390's intrs disabled when '1'. */
	unsigned dmaing:1;		/* Remote DMA Active */
	unsigned char tx_start_page, rx_start_page, stop_page;
	unsigned char current_page;	/* Read pointer in buffer  */
	unsigned char interface_num;	/* Net port (AUI, 10bT.) to use. */
	unsigned char txqueue;		/* Tx Packet buffer queue length. */
	short tx1, tx2;			/* Packet lengths for ping-pong tx. */
	short lasttx;			/* Alpha version consistency check. */
	unsigned char reg0;		/* Register '0' in a WD8013 */
	unsigned char reg5;		/* Register '5' in a WD8013 */
	unsigned char saved_irq;	/* Original dev->irq value. */
	struct net_device_stats stat;	/* The new statistics table. */
	u32 *reg_offset;		/* Register mapping table */
	spinlock_t page_lock;		/* Page register locks */
	unsigned long priv;		/* Private field to store bus IDs etc. */
};

/* The maximum number of 8390 interrupt service routines called per IRQ. */
#define MAX_SERVICE 12

/* The maximum time waited (in jiffies) before assuming a Tx failed. (20ms) */
#define TX_TIMEOUT (20*HZ/100)

#define ei_status (*(struct ei_device *)(dev->priv))

/* Some generic ethernet register configurations. */
#define E8390_TX_IRQ_MASK	0xa	/* For register EN0_ISR */
#define E8390_RX_IRQ_MASK	0x5
#define E8390_RXCONFIG		0x4	/* EN0_RXCR: broadcasts, no multicast,errors */
#define E8390_RXOFF		0x20	/* EN0_RXCR: Accept no packets */
#define E8390_TXCONFIG		0x00	/* EN0_TXCR: Normal transmit mode */
#define E8390_TXOFF		0x02	/* EN0_TXCR: Transmitter off */

/*  Register accessed at EN_CMD, the 8390 base addr.  */
#define E8390_STOP	0x01	/* Stop and reset the chip */
#define E8390_START	0x02	/* Start the chip, clear reset */
#define E8390_TRANS	0x04	/* Transmit a frame */
#define E8390_RREAD	0x08	/* Remote read */
#define E8390_RWRITE	0x10	/* Remote write  */
#define E8390_NODMA	0x20	/* Remote DMA */
#define E8390_PAGE0	0x00	/* Select page chip registers */
#define E8390_PAGE1	0x40	/* using the two high-order bits */
#define E8390_PAGE2	0x80	/* Page 3 is invalid. */

/*
 *	Only generate indirect loads given a machine that needs them.
 */
 
#if defined(CONFIG_MAC) || defined(CONFIG_AMIGA_PCMCIA) || \
    defined(CONFIG_ARIADNE2) || defined(CONFIG_ARIADNE2_MODULE)
#define EI_SHIFT(x)	(ei_local->reg_offset[x])
#else
#define EI_SHIFT(x)	(x)
#endif

#define E8390_CMD	EI_SHIFT(0x00)  /* The command register (for all pages) */
/* Page 0 register offsets. */
#define EN0_CLDALO	EI_SHIFT(0x01)	/* Low byte of current local dma addr  RD */
#define EN0_STARTPG	EI_SHIFT(0x01)	/* Starting page of ring bfr WR */
#define EN0_CLDAHI	EI_SHIFT(0x02)	/* High byte of current local dma addr  RD */
#define EN0_STOPPG	EI_SHIFT(0x02)	/* Ending page +1 of ring bfr WR */
#define EN0_BOUNDARY	EI_SHIFT(0x03)	/* Boundary page of ring bfr RD WR */
#define EN0_TSR		EI_SHIFT(0x04)	/* Transmit status reg RD */
#define EN0_TPSR	EI_SHIFT(0x04)	/* Transmit starting page WR */
#define EN0_NCR		EI_SHIFT(0x05)	/* Number of collision reg RD */
#define EN0_TCNTLO	EI_SHIFT(0x05)	/* Low  byte of tx byte count WR */
#define EN0_FIFO	EI_SHIFT(0x06)	/* FIFO RD */
#define EN0_TCNTHI	EI_SHIFT(0x06)	/* High byte of tx byte count WR */
#define EN0_ISR		EI_SHIFT(0x07)	/* Interrupt status reg RD WR */
#define EN0_CRDALO	EI_SHIFT(0x08)	/* low byte of current remote dma address RD */
#define EN0_RSARLO	EI_SHIFT(0x08)	/* Remote start address reg 0 */
#define EN0_CRDAHI	EI_SHIFT(0x09)	/* high byte, current remote dma address RD */
#define EN0_RSARHI	EI_SHIFT(0x09)	/* Remote start address reg 1 */
#define EN0_RCNTLO	EI_SHIFT(0x0a)	/* Remote byte count reg WR */
#define EN0_RCNTHI	EI_SHIFT(0x0b)	/* Remote byte count reg WR */
#define EN0_RSR		EI_SHIFT(0x0c)	/* rx status reg RD */
#define EN0_RXCR	EI_SHIFT(0x0c)	/* RX configuration reg WR */
#define EN0_TXCR	EI_SHIFT(0x0d)	/* TX configuration reg WR */
#define EN0_COUNTER0	EI_SHIFT(0x0d)	/* Rcv alignment error counter RD */
#define EN0_DCFG	EI_SHIFT(0x0e)	/* Data configuration reg WR */
#define EN0_COUNTER1	EI_SHIFT(0x0e)	/* Rcv CRC error counter RD */
#define EN0_IMR		EI_SHIFT(0x0f)	/* Interrupt mask reg WR */
#define EN0_COUNTER2	EI_SHIFT(0x0f)	/* Rcv missed frame error counter RD */

/* Bits in EN0_ISR - Interrupt status register */
#define ENISR_RX	0x01	/* Receiver, no error */
#define ENISR_TX	0x02	/* Transmitter, no error */
#define ENISR_RX_ERR	0x04	/* Receiver, with error */
#define ENISR_TX_ERR	0x08	/* Transmitter, with error */
#define ENISR_OVER	0x10	/* Receiver overwrote the ring */
#define ENISR_COUNTERS	0x20	/* Counters need emptying */
#define ENISR_RDC	0x40	/* remote dma complete */
#define ENISR_RESET	0x80	/* Reset completed */
#define ENISR_ALL	0x3f	/* Interrupts we will enable */

/* Bits in EN0_DCFG - Data config register */
#define ENDCFG_WTS	0x01	/* word transfer mode selection */

/* Page 1 register offsets. */
#define EN1_PHYS   EI_SHIFT(0x01)	/* This board's physical enet addr RD WR */
#define EN1_PHYS_SHIFT(i)  EI_SHIFT(i+1) /* Get and set mac address */
#define EN1_CURPAG EI_SHIFT(0x07)	/* Current memory page RD WR */
#define EN1_MULT   EI_SHIFT(0x08)	/* Multicast filter mask array (8 bytes) RD WR */
#define EN1_MULT_SHIFT(i)  EI_SHIFT(8+i) /* Get and set multicast filter */

/* Bits in received packet status byte and EN0_RSR*/
#define ENRSR_RXOK	0x01	/* Received a good packet */
#define ENRSR_CRC	0x02	/* CRC error */
#define ENRSR_FAE	0x04	/* frame alignment error */
#define ENRSR_FO	0x08	/* FIFO overrun */
#define ENRSR_MPA	0x10	/* missed pkt */
#define ENRSR_PHY	0x20	/* physical/multicast address */
#define ENRSR_DIS	0x40	/* receiver disable. set in monitor mode */
#define ENRSR_DEF	0x80	/* deferring */

/* Transmitted packet status, EN0_TSR. */
#define ENTSR_PTX 0x01	/* Packet transmitted without error */
#define ENTSR_ND  0x02	/* The transmit wasn't deferred. */
#define ENTSR_COL 0x04	/* The transmit collided at least once. */
#define ENTSR_ABT 0x08  /* The transmit collided 16 times, and was deferred. */
#define ENTSR_CRS 0x10	/* The carrier sense was lost. */
#define ENTSR_FU  0x20  /* A "FIFO underrun" occurred during transmit. */
#define ENTSR_CDH 0x40	/* The collision detect "heartbeat" signal was lost. */
#define ENTSR_OWC 0x80  /* There was an out-of-window collision. */

#endif /* _8390_h */
