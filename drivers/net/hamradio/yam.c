/*****************************************************************************/

/*
 *    yam.c  -- YAM radio modem driver.
 *
 *      Copyright (C) 1998 Frederic Rible F1OAT (frible@teaser.fr)
 *      Adapted from baycom.c driver written by Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Please note that the GPL allows you to use the driver, NOT the radio.
 *  In order to use the radio, you need a license from the communications
 *  authority of your country.
 *
 *
 *  History:
 *   0.0 F1OAT 06.06.98  Begin of work with baycom.c source code V 0.3
 *   0.1 F1OAT 07.06.98  Add timer polling routine for channel arbitration
 *   0.2 F6FBB 08.06.98  Added delay after FPGA programming
 *   0.3 F6FBB 29.07.98  Delayed PTT implementation for dupmode=2
 *   0.4 F6FBB 30.07.98  Added TxTail, Slottime and Persistance
 *   0.5 F6FBB 01.08.98  Shared IRQs, /proc/net and network statistics
 *   0.6 F6FBB 25.08.98  Added 1200Bds format
 *   0.7 F6FBB 12.09.98  Added to the kernel configuration
 *   0.8 F6FBB 14.10.98  Fixed slottime/persistance timing bug
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/if.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/system.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>

#include <linux/netdevice.h>
#include <linux/if_arp.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
/* prototypes for ax25_encapsulate and ax25_rebuild_header */
#include <net/ax25.h>
#endif							/* CONFIG_AX25 || CONFIG_AX25_MODULE */

/* make genksyms happy */
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/tcp.h>

#include <linux/kernel.h>
#include <linux/proc_fs.h>

#include <linux/version.h>
#include <asm/uaccess.h>
#include <linux/init.h>

#include <linux/yam.h>
#include "yam9600.h"
#include "yam1200.h"

/* --------------------------------------------------------------------- */

static const char yam_drvname[] = "yam";
static const char yam_drvinfo[] = KERN_INFO "YAM driver version 0.8 by F1OAT/F6FBB\n";

/* --------------------------------------------------------------------- */

#define YAM_9600	1
#define YAM_1200	2

#define NR_PORTS 	4
#define YAM_MAGIC	0xF10A7654

/* Transmitter states */

#define TX_OFF  	0
#define TX_HEAD		1
#define TX_DATA  	2
#define TX_CRC1  	3
#define TX_CRC2  	4
#define TX_TAIL  	5

#define YAM_MAX_FRAME	1024

#define DEFAULT_BITRATE	9600	/* bps */
#define DEFAULT_HOLDD   10		/* sec */
#define DEFAULT_TXD	    300		/* ms */
#define DEFAULT_TXTAIL  10		/* ms */
#define DEFAULT_SLOT    100		/* ms */
#define DEFAULT_PERS    64		/* 0->255 */

struct yam_port {
	int magic;
	int bitrate;
	int baudrate;
	int iobase;
	int irq;
	int dupmode;
	char name[16];

	struct net_device dev;

	/* Stats section */

	struct net_device_stats stats;

	int nb_rxint;
	int nb_mdint;

	/* Parameters section */

	int txd;					/* tx delay */
	int holdd;					/* duplex ptt delay */
	int txtail;					/* txtail delay */
	int slot;					/* slottime */
	int pers;					/* persistence */

	/* Tx section */

	int tx_state;
	int tx_count;
	int slotcnt;
	unsigned char tx_buf[YAM_MAX_FRAME];
	int tx_len;
	int tx_crcl, tx_crch;
	struct sk_buff_head send_queue;		/* Packets awaiting transmission */

	/* Rx section */

	int dcd;
	unsigned char rx_buf[YAM_MAX_FRAME];
	int rx_len;
	int rx_crcl, rx_crch;
};

struct yam_mcs {
	unsigned char bits[YAM_FPGA_SIZE];
	int bitrate;
	struct yam_mcs *next;
};

static struct yam_port yam_ports[NR_PORTS];

static struct yam_mcs *yam_data = NULL;

static unsigned irqs[16];

static char ax25_bcast[7] =
{'Q' << 1, 'S' << 1, 'T' << 1, ' ' << 1, ' ' << 1, ' ' << 1, '0' << 1};
static char ax25_test[7] =
{'L' << 1, 'I' << 1, 'N' << 1, 'U' << 1, 'X' << 1, ' ' << 1, '1' << 1};

static struct timer_list yam_timer;

/* --------------------------------------------------------------------- */

#define RBR(iobase) (iobase+0)
#define THR(iobase) (iobase+0)
#define IER(iobase) (iobase+1)
#define IIR(iobase) (iobase+2)
#define FCR(iobase) (iobase+2)
#define LCR(iobase) (iobase+3)
#define MCR(iobase) (iobase+4)
#define LSR(iobase) (iobase+5)
#define MSR(iobase) (iobase+6)
#define SCR(iobase) (iobase+7)
#define DLL(iobase) (iobase+0)
#define DLM(iobase) (iobase+1)

#define YAM_EXTENT 8

/* Interrupt Identification Register Bit Masks */
#define IIR_NOPEND 	1
#define IIR_MSR  	0
#define IIR_TX     	2
#define IIR_RX     	4
#define IIR_LSR   	6
#define IIR_TIMEOUT	12			/* Fifo mode only */

#define IIR_MASK	0x0F

/* Interrupt Enable Register Bit Masks */
#define IER_RX  1				/* enable rx interrupt */
#define IER_TX  2				/* enable tx interrupt */
#define IER_LSR 4				/* enable line status interrupts */
#define IER_MSR 8				/* enable modem status interrupts */

/* Modem Control Register Bit Masks */
#define MCR_DTR  0x01			/* DTR output */
#define MCR_RTS  0x02			/* RTS output */
#define MCR_OUT1 0x04			/* OUT1 output (not accessible in RS232) */
#define MCR_OUT2 0x08			/* Master Interrupt enable (must be set on PCs) */
#define MCR_LOOP 0x10			/* Loopback enable */

/* Modem Status Register Bit Masks */
#define MSR_DCTS 0x01			/* Delta CTS input */
#define MSR_DDSR 0x02			/* Delta DSR */
#define MSR_DRIN 0x04			/* Delta RI */
#define MSR_DDCD 0x08			/* Delta DCD */
#define MSR_CTS  0x10			/* CTS input */
#define MSR_DSR  0x20			/* DSR input */
#define MSR_RING 0x40			/* RI  input */
#define MSR_DCD  0x80			/* DCD input */

/* line status register bit mask */
#define LSR_RXC	   0x01
#define LSR_OE	   0x02
#define LSR_PE	   0x04
#define LSR_FE	   0x08
#define LSR_BREAK  0x10
#define LSR_THRE   0x20
#define LSR_TSRE   0x40

/* Line Control Register Bit Masks */
#define LCR_DLAB    0x80
#define LCR_BREAK   0x40
#define LCR_PZERO   0x28
#define LCR_PEVEN   0x18
#define LCR_PODD    0x08
#define LCR_STOP1   0x00
#define LCR_STOP2   0x04
#define LCR_BIT5    0x00
#define LCR_BIT6    0x02
#define LCR_BIT7    0x01
#define LCR_BIT8    0x03

/* YAM Modem <-> UART Port mapping */

#define TX_RDY 	  MSR_DCTS		/* transmitter ready to send */
#define RX_DCD    MSR_DCD		/* carrier detect */
#define RX_FLAG   MSR_RING		/* hdlc flag received */
#define FPGA_DONE MSR_DSR		/* FPGA is configured */
#define PTT_ON    (MCR_RTS|MCR_OUT2)	/* activate PTT */
#define PTT_OFF   (MCR_DTR|MCR_OUT2)	/* release PTT */

#define ENABLE_RXINT   IER_RX	/* enable uart rx interrupt during rx */
#define ENABLE_TXINT   IER_MSR	/* enable uart ms interrupt during tx */
#define ENABLE_RTXINT  (IER_RX|IER_MSR)		/* full duplex operations */

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

/*************************************************************************
* CRC Tables
************************************************************************/

static const unsigned char chktabl[256] =
{0x00, 0x89, 0x12, 0x9b, 0x24, 0xad, 0x36, 0xbf, 0x48, 0xc1, 0x5a, 0xd3, 0x6c, 0xe5, 0x7e,
 0xf7, 0x81, 0x08, 0x93, 0x1a, 0xa5, 0x2c, 0xb7, 0x3e, 0xc9, 0x40, 0xdb, 0x52, 0xed, 0x64,
 0xff, 0x76, 0x02, 0x8b, 0x10, 0x99, 0x26, 0xaf, 0x34, 0xbd, 0x4a, 0xc3, 0x58, 0xd1, 0x6e,
 0xe7, 0x7c, 0xf5, 0x83, 0x0a, 0x91, 0x18, 0xa7, 0x2e, 0xb5, 0x3c, 0xcb, 0x42, 0xd9, 0x50,
 0xef, 0x66, 0xfd, 0x74, 0x04, 0x8d, 0x16, 0x9f, 0x20, 0xa9, 0x32, 0xbb, 0x4c, 0xc5, 0x5e,
 0xd7, 0x68, 0xe1, 0x7a, 0xf3, 0x85, 0x0c, 0x97, 0x1e, 0xa1, 0x28, 0xb3, 0x3a, 0xcd, 0x44,
 0xdf, 0x56, 0xe9, 0x60, 0xfb, 0x72, 0x06, 0x8f, 0x14, 0x9d, 0x22, 0xab, 0x30, 0xb9, 0x4e,
 0xc7, 0x5c, 0xd5, 0x6a, 0xe3, 0x78, 0xf1, 0x87, 0x0e, 0x95, 0x1c, 0xa3, 0x2a, 0xb1, 0x38,
 0xcf, 0x46, 0xdd, 0x54, 0xeb, 0x62, 0xf9, 0x70, 0x08, 0x81, 0x1a, 0x93, 0x2c, 0xa5, 0x3e,
 0xb7, 0x40, 0xc9, 0x52, 0xdb, 0x64, 0xed, 0x76, 0xff, 0x89, 0x00, 0x9b, 0x12, 0xad, 0x24,
 0xbf, 0x36, 0xc1, 0x48, 0xd3, 0x5a, 0xe5, 0x6c, 0xf7, 0x7e, 0x0a, 0x83, 0x18, 0x91, 0x2e,
 0xa7, 0x3c, 0xb5, 0x42, 0xcb, 0x50, 0xd9, 0x66, 0xef, 0x74, 0xfd, 0x8b, 0x02, 0x99, 0x10,
 0xaf, 0x26, 0xbd, 0x34, 0xc3, 0x4a, 0xd1, 0x58, 0xe7, 0x6e, 0xf5, 0x7c, 0x0c, 0x85, 0x1e,
 0x97, 0x28, 0xa1, 0x3a, 0xb3, 0x44, 0xcd, 0x56, 0xdf, 0x60, 0xe9, 0x72, 0xfb, 0x8d, 0x04,
 0x9f, 0x16, 0xa9, 0x20, 0xbb, 0x32, 0xc5, 0x4c, 0xd7, 0x5e, 0xe1, 0x68, 0xf3, 0x7a, 0x0e,
 0x87, 0x1c, 0x95, 0x2a, 0xa3, 0x38, 0xb1, 0x46, 0xcf, 0x54, 0xdd, 0x62, 0xeb, 0x70, 0xf9,
 0x8f, 0x06, 0x9d, 0x14, 0xab, 0x22, 0xb9, 0x30, 0xc7, 0x4e, 0xd5, 0x5c, 0xe3, 0x6a, 0xf1,
 0x78};
static const unsigned char chktabh[256] =
{0x00, 0x11, 0x23, 0x32, 0x46, 0x57, 0x65, 0x74, 0x8c, 0x9d, 0xaf, 0xbe, 0xca, 0xdb, 0xe9,
 0xf8, 0x10, 0x01, 0x33, 0x22, 0x56, 0x47, 0x75, 0x64, 0x9c, 0x8d, 0xbf, 0xae, 0xda, 0xcb,
 0xf9, 0xe8, 0x21, 0x30, 0x02, 0x13, 0x67, 0x76, 0x44, 0x55, 0xad, 0xbc, 0x8e, 0x9f, 0xeb,
 0xfa, 0xc8, 0xd9, 0x31, 0x20, 0x12, 0x03, 0x77, 0x66, 0x54, 0x45, 0xbd, 0xac, 0x9e, 0x8f,
 0xfb, 0xea, 0xd8, 0xc9, 0x42, 0x53, 0x61, 0x70, 0x04, 0x15, 0x27, 0x36, 0xce, 0xdf, 0xed,
 0xfc, 0x88, 0x99, 0xab, 0xba, 0x52, 0x43, 0x71, 0x60, 0x14, 0x05, 0x37, 0x26, 0xde, 0xcf,
 0xfd, 0xec, 0x98, 0x89, 0xbb, 0xaa, 0x63, 0x72, 0x40, 0x51, 0x25, 0x34, 0x06, 0x17, 0xef,
 0xfe, 0xcc, 0xdd, 0xa9, 0xb8, 0x8a, 0x9b, 0x73, 0x62, 0x50, 0x41, 0x35, 0x24, 0x16, 0x07,
 0xff, 0xee, 0xdc, 0xcd, 0xb9, 0xa8, 0x9a, 0x8b, 0x84, 0x95, 0xa7, 0xb6, 0xc2, 0xd3, 0xe1,
 0xf0, 0x08, 0x19, 0x2b, 0x3a, 0x4e, 0x5f, 0x6d, 0x7c, 0x94, 0x85, 0xb7, 0xa6, 0xd2, 0xc3,
 0xf1, 0xe0, 0x18, 0x09, 0x3b, 0x2a, 0x5e, 0x4f, 0x7d, 0x6c, 0xa5, 0xb4, 0x86, 0x97, 0xe3,
 0xf2, 0xc0, 0xd1, 0x29, 0x38, 0x0a, 0x1b, 0x6f, 0x7e, 0x4c, 0x5d, 0xb5, 0xa4, 0x96, 0x87,
 0xf3, 0xe2, 0xd0, 0xc1, 0x39, 0x28, 0x1a, 0x0b, 0x7f, 0x6e, 0x5c, 0x4d, 0xc6, 0xd7, 0xe5,
 0xf4, 0x80, 0x91, 0xa3, 0xb2, 0x4a, 0x5b, 0x69, 0x78, 0x0c, 0x1d, 0x2f, 0x3e, 0xd6, 0xc7,
 0xf5, 0xe4, 0x90, 0x81, 0xb3, 0xa2, 0x5a, 0x4b, 0x79, 0x68, 0x1c, 0x0d, 0x3f, 0x2e, 0xe7,
 0xf6, 0xc4, 0xd5, 0xa1, 0xb0, 0x82, 0x93, 0x6b, 0x7a, 0x48, 0x59, 0x2d, 0x3c, 0x0e, 0x1f,
 0xf7, 0xe6, 0xd4, 0xc5, 0xb1, 0xa0, 0x92, 0x83, 0x7b, 0x6a, 0x58, 0x49, 0x3d, 0x2c, 0x1e,
 0x0f};

/*************************************************************************
* FPGA functions
************************************************************************/

static void delay(int ms)
{
	unsigned long timeout = jiffies + ((ms * HZ) / 1000);
	while (jiffies < timeout);
}

/*
 * reset FPGA
 */

static void fpga_reset(int iobase)
{
	outb(0, IER(iobase));
	outb(LCR_DLAB | LCR_BIT5, LCR(iobase));
	outb(1, DLL(iobase));
	outb(0, DLM(iobase));

	outb(LCR_BIT5, LCR(iobase));
	inb(LSR(iobase));
	inb(MSR(iobase));
	/* turn off FPGA supply voltage */
	outb(MCR_OUT1 | MCR_OUT2, MCR(iobase));
	delay(100);
	/* turn on FPGA supply voltage again */
	outb(MCR_DTR | MCR_RTS | MCR_OUT1 | MCR_OUT2, MCR(iobase));
	delay(100);
}

/*
 * send one byte to FPGA
 */

static int fpga_write(int iobase, unsigned char wrd)
{
	unsigned char bit;
	int k;
	unsigned long timeout = jiffies + HZ / 10;

	for (k = 0; k < 8; k++) {
		bit = (wrd & 0x80) ? (MCR_RTS | MCR_DTR) : MCR_DTR;
		outb(bit | MCR_OUT1 | MCR_OUT2, MCR(iobase));
		wrd <<= 1;
		outb(0xfc, THR(iobase));
		while ((inb(LSR(iobase)) & LSR_TSRE) == 0)
			if (jiffies > timeout)
				return -1;
	}

	return 0;
}

#ifdef MODULE
static void free_mcs(void)
{
	struct yam_mcs *p;

	while (yam_data) {
		p = yam_data;
		yam_data = yam_data->next;
		kfree(p);
	}
}
#endif

static unsigned char *
 add_mcs(unsigned char *bits, int bitrate)
{
	struct yam_mcs *p;

	/* If it already exists, replace the bit data */
	p = yam_data;
	while (p) {
		if (p->bitrate == bitrate) {
			memcpy(p->bits, bits, YAM_FPGA_SIZE);
			return p->bits;
		}
		p = p->next;
	}

	/* Allocate a new mcs */
	p = kmalloc(sizeof(struct yam_mcs), GFP_ATOMIC);
	if (p == NULL) {
		printk(KERN_WARNING "YAM: no memory to allocate mcs\n");
		return NULL;
	}
	memcpy(p->bits, bits, YAM_FPGA_SIZE);
	p->bitrate = bitrate;
	p->next = yam_data;
	yam_data = p;

	return p->bits;
}

static unsigned char *get_mcs(int bitrate)
{
	struct yam_mcs *p;

	p = yam_data;
	while (p) {
		if (p->bitrate == bitrate)
			return p->bits;
		p = p->next;
	}

	/* Load predefined mcs data */
	switch (bitrate) {
	case 1200:
		return add_mcs(bits_1200, bitrate);
	default:
		return add_mcs(bits_9600, bitrate);
	}
}

/*
 * download bitstream to FPGA
 * data is contained in bits[] array in fpgaconf.h
 */

static int fpga_download(int iobase, int bitrate)
{
	int i, rc;
	unsigned char *pbits;

	pbits = get_mcs(bitrate);
	if (pbits == NULL)
		return -1;

	fpga_reset(iobase);
	for (i = 0; i < YAM_FPGA_SIZE; i++) {
		if (fpga_write(iobase, pbits[i])) {
			printk("yam: error in write cycle\n");
			return -1;			/* write... */
		}
	}

	fpga_write(iobase, 0xFF);
	rc = inb(MSR(iobase));		/* check DONE signal */

	/* Needed for some hardwares */
	delay(50);

	return (rc & MSR_DSR) ? 0 : -1;
}


/************************************************************************
* Serial port init 
************************************************************************/

static void yam_set_uart(struct net_device *dev)
{
	struct yam_port *yp = (struct yam_port *) dev->priv;
	int divisor = 115200 / yp->baudrate;

	outb(0, IER(dev->base_addr));
	outb(LCR_DLAB | LCR_BIT8, LCR(dev->base_addr));
	outb(divisor, DLL(dev->base_addr));
	outb(0, DLM(dev->base_addr));
	outb(LCR_BIT8, LCR(dev->base_addr));
	outb(PTT_OFF, MCR(dev->base_addr));
	outb(0x00, FCR(dev->base_addr));

	/* Flush pending irq */

	inb(RBR(dev->base_addr));
	inb(MSR(dev->base_addr));

	/* Enable rx irq */

	outb(ENABLE_RTXINT, IER(dev->base_addr));
}


/* --------------------------------------------------------------------- */

enum uart {
	c_uart_unknown, c_uart_8250,
	c_uart_16450, c_uart_16550, c_uart_16550A
};

static const char *uart_str[] =
{"unknown", "8250", "16450", "16550", "16550A"};

static enum uart yam_check_uart(unsigned int iobase)
{
	unsigned char b1, b2, b3;
	enum uart u;
	enum uart uart_tab[] =
	{c_uart_16450, c_uart_unknown, c_uart_16550, c_uart_16550A};

	b1 = inb(MCR(iobase));
	outb(b1 | 0x10, MCR(iobase));	/* loopback mode */
	b2 = inb(MSR(iobase));
	outb(0x1a, MCR(iobase));
	b3 = inb(MSR(iobase)) & 0xf0;
	outb(b1, MCR(iobase));		/* restore old values */
	outb(b2, MSR(iobase));
	if (b3 != 0x90)
		return c_uart_unknown;
	inb(RBR(iobase));
	inb(RBR(iobase));
	outb(0x01, FCR(iobase));	/* enable FIFOs */
	u = uart_tab[(inb(IIR(iobase)) >> 6) & 3];
	if (u == c_uart_16450) {
		outb(0x5a, SCR(iobase));
		b1 = inb(SCR(iobase));
		outb(0xa5, SCR(iobase));
		b2 = inb(SCR(iobase));
		if ((b1 != 0x5a) || (b2 != 0xa5))
			u = c_uart_8250;
	}
	return u;
}

/******************************************************************************
* Rx Section
******************************************************************************/
static void inline
 yam_rx_flag(struct net_device *dev, struct yam_port *yp)
{
	if (yp->dcd && yp->rx_len >= 3 && yp->rx_len < YAM_MAX_FRAME) {
		int pkt_len = yp->rx_len - 2 + 1;	/* -CRC + kiss */
		struct sk_buff *skb;

		if ((yp->rx_crch & yp->rx_crcl) != 0xFF) {
			/* Bad crc */
		} else {
			if (!(skb = dev_alloc_skb(pkt_len))) {
				printk("%s: memory squeeze, dropping packet\n", dev->name);
				++yp->stats.rx_dropped;
			} else {
				unsigned char *cp;
				skb->dev = dev;
				cp = skb_put(skb, pkt_len);
				*cp++ = 0;		/* KISS kludge */
				memcpy(cp, yp->rx_buf, pkt_len - 1);
				skb->protocol = htons(ETH_P_AX25);
				skb->mac.raw = skb->data;
				netif_rx(skb);
				++yp->stats.rx_packets;
			}
		}
	}
	yp->rx_len = 0;
	yp->rx_crcl = 0x21;
	yp->rx_crch = 0xf3;
}

static void inline
 yam_rx_byte(struct net_device *dev, struct yam_port *yp, unsigned char rxb)
{
	if (yp->rx_len < YAM_MAX_FRAME) {
		unsigned char c = yp->rx_crcl;
		yp->rx_crcl = (chktabl[c] ^ yp->rx_crch);
		yp->rx_crch = (chktabh[c] ^ rxb);
		yp->rx_buf[yp->rx_len++] = rxb;
	}
}

/********************************************************************************
* TX Section
********************************************************************************/

static void ptt_on(struct net_device *dev)
{
	outb(PTT_ON, MCR(dev->base_addr));
}

static void ptt_off(struct net_device *dev)
{
	outb(PTT_OFF, MCR(dev->base_addr));
}

static int yam_send_packet(struct sk_buff *skb, struct net_device *dev)
{
	struct yam_port *yp = dev->priv;

	if (skb == NULL) {
		return 0;
	}
	skb_queue_tail(&yp->send_queue, skb);
	dev->trans_start = jiffies;
	return 0;
}

static void yam_start_tx(struct net_device *dev, struct yam_port *yp)
{
	if ((yp->tx_state == TX_TAIL) || (yp->txd == 0))
		yp->tx_count = 1;
	else
		yp->tx_count = (yp->bitrate * yp->txd) / 8000;
	yp->tx_state = TX_HEAD;
	ptt_on(dev);
}

static unsigned short random_seed;

static inline unsigned short random_num(void)
{
	random_seed = 28629 * random_seed + 157;
	return random_seed;
}

static void yam_arbitrate(struct net_device *dev)
{
	struct yam_port *yp = dev->priv;

	if (!yp || yp->magic != YAM_MAGIC
		|| yp->tx_state != TX_OFF || skb_queue_empty(&yp->send_queue)) {
		return;
	}
	/* tx_state is TX_OFF and there is data to send */

	if (yp->dupmode) {
		/* Full duplex mode, don't wait */
		yam_start_tx(dev, yp);
		return;
	}
	if (yp->dcd) {
		/* DCD on, wait slotime ... */
		yp->slotcnt = yp->slot / 10;
		return;
	}
	/* Is slottime passed ? */
	if ((--yp->slotcnt) > 0)
		return;

	yp->slotcnt = yp->slot / 10;

	/* is random > persist ? */
	if ((random_num() % 256) > yp->pers)
		return;

	yam_start_tx(dev, yp);
}

static void yam_dotimer(unsigned long dummy)
{
	int i;

	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = &yam_ports[i].dev;
		if (dev->start)
			yam_arbitrate(dev);
	}
	yam_timer.expires = jiffies + HZ / 100;
	add_timer(&yam_timer);
}

static void yam_tx_byte(struct net_device *dev, struct yam_port *yp)
{
	struct sk_buff *skb;
	unsigned char b, temp;

	switch (yp->tx_state) {
	case TX_OFF:
		break;
	case TX_HEAD:
		if (--yp->tx_count <= 0) {
			if (!(skb = skb_dequeue(&yp->send_queue))) {
				ptt_off(dev);
				yp->tx_state = TX_OFF;
				break;
			}
			yp->tx_state = TX_DATA;
			if (skb->data[0] != 0) {
/*                              do_kiss_params(s, skb->data, skb->len); */
				dev_kfree_skb(skb);
				break;
			}
			yp->tx_len = skb->len - 1;	/* strip KISS byte */
			if (yp->tx_len >= YAM_MAX_FRAME || yp->tx_len < 2) {
				dev_kfree_skb(skb);
				break;
			}
			memcpy(yp->tx_buf, skb->data + 1, yp->tx_len);
			dev_kfree_skb(skb);
			yp->tx_count = 0;
			yp->tx_crcl = 0x21;
			yp->tx_crch = 0xf3;
			yp->tx_state = TX_DATA;
		}
		break;
	case TX_DATA:
		b = yp->tx_buf[yp->tx_count++];
		outb(b, THR(dev->base_addr));
		temp = yp->tx_crcl;
		yp->tx_crcl = chktabl[temp] ^ yp->tx_crch;
		yp->tx_crch = chktabh[temp] ^ b;
		if (yp->tx_count >= yp->tx_len) {
			yp->tx_state = TX_CRC1;
		}
		break;
	case TX_CRC1:
		yp->tx_crch = chktabl[yp->tx_crcl] ^ yp->tx_crch;
		yp->tx_crcl = chktabh[yp->tx_crcl] ^ chktabl[yp->tx_crch] ^ 0xff;
		outb(yp->tx_crcl, THR(dev->base_addr));
		yp->tx_state = TX_CRC2;
		break;
	case TX_CRC2:
		outb(chktabh[yp->tx_crch] ^ 0xFF, THR(dev->base_addr));
		if (skb_queue_empty(&yp->send_queue)) {
			yp->tx_count = (yp->bitrate * yp->txtail) / 8000;
			if (yp->dupmode == 2)
				yp->tx_count += (yp->bitrate * yp->holdd) / 8;
			if (yp->tx_count == 0)
				yp->tx_count = 1;
			yp->tx_state = TX_TAIL;
		} else {
			yp->tx_count = 1;
			yp->tx_state = TX_HEAD;
		}
		++yp->stats.tx_packets;
		break;
	case TX_TAIL:
		if (--yp->tx_count <= 0) {
			yp->tx_state = TX_OFF;
			ptt_off(dev);
		}
		break;
	}
}

/***********************************************************************************
* ISR routine
************************************************************************************/

static void yam_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct net_device *dev;
	struct yam_port *yp;
	unsigned char iir;
	int counter = 100;
	int i;

	sti();

	for (i = 0; i < NR_PORTS; i++) {
		yp = &yam_ports[i];
		dev = &yp->dev;

		if (!dev->start)
			continue;

		while ((iir = IIR_MASK & inb(IIR(dev->base_addr))) != IIR_NOPEND) {
			unsigned char msr = inb(MSR(dev->base_addr));
			unsigned char lsr = inb(LSR(dev->base_addr));
			unsigned char rxb;

			if (lsr & LSR_OE)
				++yp->stats.rx_fifo_errors;

			yp->dcd = (msr & RX_DCD) ? 1 : 0;

			if (--counter <= 0) {
				printk("%s: too many irq iir=%d\n", dev->name, iir);
				return;
			}
			if (msr & TX_RDY) {
				++yp->nb_mdint;
				yam_tx_byte(dev, yp);
			}
			if (lsr & LSR_RXC) {
				++yp->nb_rxint;
				rxb = inb(RBR(dev->base_addr));
				if (msr & RX_FLAG)
					yam_rx_flag(dev, yp);
				else
					yam_rx_byte(dev, yp, rxb);
			}
		}
	}
}

static int yam_net_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len = 0;
	int i;
	off_t pos = 0;
	off_t begin = 0;

	cli();

	for (i = 0; i < NR_PORTS; i++) {
		if (yam_ports[i].iobase == 0 || yam_ports[i].irq == 0)
			continue;
		len += sprintf(buffer + len, "Device %s\n", yam_ports[i].name);
		len += sprintf(buffer + len, "  Up       %d\n", yam_ports[i].dev.start);
		len += sprintf(buffer + len, "  Speed    %u\n", yam_ports[i].bitrate);
		len += sprintf(buffer + len, "  IoBase   0x%x\n", yam_ports[i].iobase);
		len += sprintf(buffer + len, "  BaudRate %u\n", yam_ports[i].baudrate);
		len += sprintf(buffer + len, "  IRQ      %u\n", yam_ports[i].irq);
		len += sprintf(buffer + len, "  TxState  %u\n", yam_ports[i].tx_state);
		len += sprintf(buffer + len, "  Duplex   %u\n", yam_ports[i].dupmode);
		len += sprintf(buffer + len, "  HoldDly  %u\n", yam_ports[i].holdd);
		len += sprintf(buffer + len, "  TxDelay  %u\n", yam_ports[i].txd);
		len += sprintf(buffer + len, "  TxTail   %u\n", yam_ports[i].txtail);
		len += sprintf(buffer + len, "  SlotTime %u\n", yam_ports[i].slot);
		len += sprintf(buffer + len, "  Persist  %u\n", yam_ports[i].pers);
		len += sprintf(buffer + len, "  TxFrames %lu\n", yam_ports[i].stats.tx_packets);
		len += sprintf(buffer + len, "  RxFrames %lu\n", yam_ports[i].stats.rx_packets);
		len += sprintf(buffer + len, "  TxInt    %u\n", yam_ports[i].nb_mdint);
		len += sprintf(buffer + len, "  RxInt    %u\n", yam_ports[i].nb_rxint);
		len += sprintf(buffer + len, "  RxOver   %lu\n", yam_ports[i].stats.rx_fifo_errors);
		len += sprintf(buffer + len, "\n");

		pos = begin + len;

		if (pos < offset) {
			len = 0;
			begin = pos;
		}
		if (pos > offset + length)
			break;
	}

	sti();

	*start = buffer + (offset - begin);
	len -= (offset - begin);

	if (len > length)
		len = length;

	return len;
}

#ifdef CONFIG_INET
#ifdef CONFIG_PROC_FS
#define yam_net_procfs_init() proc_net_create("yam",0,yam_net_get_info)
#define yam_net_procfs_remove() proc_net_remove("yam")
#else
#define yam_net_procfs_init()
#define yam_net_procfs_remove()
#endif /* CONFIG_PROC_FS */
#else
#define yam_net_procfs_init()
#define yam_net_procfs_remove()
#endif /* CONFIG_INET */

/* --------------------------------------------------------------------- */

static struct net_device_stats *yam_get_stats(struct net_device *dev)
{
	struct yam_port *yp;

	if (!dev || !dev->priv)
		return NULL;

	yp = (struct yam_port *) dev->priv;
	if (yp->magic != YAM_MAGIC)
		return NULL;

	/* 
	 * Get the current statistics.  This may be called with the
	 * card open or closed. 
	 */
	return &yp->stats;
}

/* --------------------------------------------------------------------- */

static int yam_open(struct net_device *dev)
{
	struct yam_port *yp = (struct yam_port *) dev->priv;
	enum uart u;
	int i;

	printk(KERN_INFO "Trying %s at iobase 0x%lx irq %u\n", dev->name, dev->base_addr, dev->irq);

	if (!dev || !yp || !yp->bitrate)
		return -ENXIO;
	if (!dev->base_addr || dev->base_addr > 0x1000 - YAM_EXTENT ||
		dev->irq < 2 || dev->irq > 15) {
		return -ENXIO;
	}
	if (check_region(dev->base_addr, YAM_EXTENT)) {
		printk("%s: cannot 0x%lx busy\n", dev->name, dev->base_addr);
		return -EACCES;
	}
	if ((u = yam_check_uart(dev->base_addr)) == c_uart_unknown) {
		printk("%s: cannot find uart type\n", dev->name);
		return -EIO;
	}
	if (fpga_download(dev->base_addr, yp->bitrate)) {
		printk("%s: cannot init FPGA\n", dev->name);
		return -EIO;
	}
	outb(0, IER(dev->base_addr));
	if (request_irq(dev->irq, yam_interrupt, SA_INTERRUPT | SA_SHIRQ, dev->name, NULL)) {
		printk("%s: irq %d busy\n", dev->name, dev->irq);
		return -EBUSY;
	}
	request_region(dev->base_addr, YAM_EXTENT, dev->name);

	yam_set_uart(dev);
	dev->start = 1;
	yp->slotcnt = yp->slot / 10;

	/* Reset overruns for all ports - FPGA programming makes overruns */
	for (i = 0; i < NR_PORTS; i++) {
		inb(LSR(yam_ports[i].dev.base_addr));
		yam_ports[i].stats.rx_fifo_errors = 0;
	}

	printk(KERN_INFO "%s at iobase 0x%lx irq %u uart %s\n", dev->name, dev->base_addr, dev->irq,
		   uart_str[u]);
	MOD_INC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int yam_close(struct net_device *dev)
{
	struct sk_buff *skb;
	struct yam_port *yp = (struct yam_port *) dev->priv;

	if (!dev || !yp)
		return -EINVAL;
	/*
	 * disable interrupts
	 */
	outb(0, IER(dev->base_addr));
	outb(1, MCR(dev->base_addr));
	/* Remove IRQ handler if last */
	free_irq(dev->irq, NULL);
	release_region(dev->base_addr, YAM_EXTENT);
	dev->start = 0;
	dev->tbusy = 1;
	while ((skb = skb_dequeue(&yp->send_queue)))
		dev_kfree_skb(skb);

	printk(KERN_INFO "%s: close yam at iobase 0x%lx irq %u\n",
		   yam_drvname, dev->base_addr, dev->irq);
	MOD_DEC_USE_COUNT;
	return 0;
}

/* --------------------------------------------------------------------- */

static int yam_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct yam_port *yp = (struct yam_port *) dev->priv;
	struct yamdrv_ioctl_cfg yi;
	struct yamdrv_ioctl_mcs *ym;
	int ioctl_cmd;

	if (copy_from_user(&ioctl_cmd, ifr->ifr_data, sizeof(int)))
		 return -EFAULT;

	if (yp == NULL || yp->magic != YAM_MAGIC)
		return -EINVAL;

	if (!suser())
		return -EPERM;

	if (cmd != SIOCDEVPRIVATE)
		return -EINVAL;

	switch (ioctl_cmd) {

	case SIOCYAMRESERVED:
		return -EINVAL;			/* unused */

	case SIOCYAMSMCS:
		if (dev->start)
			return -EINVAL;		/* Cannot change this parameter when up */
		ym = kmalloc(sizeof(struct yamdrv_ioctl_mcs), GFP_ATOMIC);
		ym->bitrate = 9600;
		if (copy_from_user(ym, ifr->ifr_data, sizeof(struct yamdrv_ioctl_mcs)))
			 return -EFAULT;
		if (ym->bitrate > YAM_MAXBITRATE)
			return -EINVAL;
		add_mcs(ym->bits, ym->bitrate);
		kfree(ym);
		break;

	case SIOCYAMSCFG:
		if (copy_from_user(&yi, ifr->ifr_data, sizeof(struct yamdrv_ioctl_cfg)))
			 return -EFAULT;

		if ((yi.cfg.mask & YAM_IOBASE) && dev->start)
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((yi.cfg.mask & YAM_IRQ) && dev->start)
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((yi.cfg.mask & YAM_BITRATE) && dev->start)
			return -EINVAL;		/* Cannot change this parameter when up */
		if ((yi.cfg.mask & YAM_BAUDRATE) && dev->start)
			return -EINVAL;		/* Cannot change this parameter when up */

		if (yi.cfg.mask & YAM_IOBASE) {
			yp->iobase = yi.cfg.iobase;
			dev->base_addr = yi.cfg.iobase;
		}
		if (yi.cfg.mask & YAM_IRQ) {
			if (yi.cfg.irq > 15)
				return -EINVAL;
			yp->irq = yi.cfg.irq;
			dev->irq = yi.cfg.irq;
		}
		if (yi.cfg.mask & YAM_BITRATE) {
			if (yi.cfg.bitrate > YAM_MAXBITRATE)
				return -EINVAL;
			yp->bitrate = yi.cfg.bitrate;
		}
		if (yi.cfg.mask & YAM_BAUDRATE) {
			if (yi.cfg.baudrate > YAM_MAXBAUDRATE)
				return -EINVAL;
			yp->baudrate = yi.cfg.baudrate;
		}
		if (yi.cfg.mask & YAM_MODE) {
			if (yi.cfg.mode > YAM_MAXMODE)
				return -EINVAL;
			yp->dupmode = yi.cfg.mode;
		}
		if (yi.cfg.mask & YAM_HOLDDLY) {
			if (yi.cfg.holddly > YAM_MAXHOLDDLY)
				return -EINVAL;
			yp->holdd = yi.cfg.holddly;
		}
		if (yi.cfg.mask & YAM_TXDELAY) {
			if (yi.cfg.txdelay > YAM_MAXTXDELAY)
				return -EINVAL;
			yp->txd = yi.cfg.txdelay;
		}
		if (yi.cfg.mask & YAM_TXTAIL) {
			if (yi.cfg.txtail > YAM_MAXTXTAIL)
				return -EINVAL;
			yp->txtail = yi.cfg.txtail;
		}
		if (yi.cfg.mask & YAM_PERSIST) {
			if (yi.cfg.persist > YAM_MAXPERSIST)
				return -EINVAL;
			yp->pers = yi.cfg.persist;
		}
		if (yi.cfg.mask & YAM_SLOTTIME) {
			if (yi.cfg.slottime > YAM_MAXSLOTTIME)
				return -EINVAL;
			yp->slot = yi.cfg.slottime;
			yp->slotcnt = yp->slot / 10;
		}
		break;

	case SIOCYAMGCFG:
		yi.cfg.mask = 0xffffffff;
		yi.cfg.iobase = yp->iobase;
		yi.cfg.irq = yp->irq;
		yi.cfg.bitrate = yp->bitrate;
		yi.cfg.baudrate = yp->baudrate;
		yi.cfg.mode = yp->dupmode;
		yi.cfg.txdelay = yp->txd;
		yi.cfg.holddly = yp->holdd;
		yi.cfg.txtail = yp->txtail;
		yi.cfg.persist = yp->pers;
		yi.cfg.slottime = yp->slot;
		if (copy_to_user(ifr->ifr_data, &yi, sizeof(struct yamdrv_ioctl_cfg)))
			 return -EFAULT;
		break;

	default:
		return -EINVAL;

	}

	return 0;
}

/* --------------------------------------------------------------------- */

static int yam_set_mac_address(struct net_device *dev, void *addr)
{
	struct sockaddr *sa = (struct sockaddr *) addr;

	/* addr is an AX.25 shifted ASCII mac address */
	memcpy(dev->dev_addr, sa->sa_data, dev->addr_len);
	return 0;
}

/* --------------------------------------------------------------------- */

static int yam_probe(struct net_device *dev)
{
	struct yam_port *yp;

	if (!dev)
		return -ENXIO;

	yp = (struct yam_port *) dev->priv;

	dev->open = yam_open;
	dev->stop = yam_close;
	dev->do_ioctl = yam_ioctl;
	dev->hard_start_xmit = yam_send_packet;
	dev->get_stats = yam_get_stats;

	dev_init_buffers(dev);
	skb_queue_head_init(&yp->send_queue);

#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
	dev->hard_header = ax25_encapsulate;
	dev->rebuild_header = ax25_rebuild_header;
#else							/* CONFIG_AX25 || CONFIG_AX25_MODULE */
	dev->hard_header = NULL;
	dev->rebuild_header = NULL;
#endif							/* CONFIG_AX25 || CONFIG_AX25_MODULE */

	dev->set_mac_address = yam_set_mac_address;

	dev->type = ARPHRD_AX25;	/* AF_AX25 device */
	dev->hard_header_len = 73;	/* We do digipeaters now */
	dev->mtu = 256;				/* AX25 is the default */
	dev->addr_len = 7;			/* sizeof an ax.25 address */
	memcpy(dev->broadcast, ax25_bcast, 7);
	memcpy(dev->dev_addr, ax25_test, 7);

	/* New style flags */
	dev->flags = 0;

	return 0;
}

/* --------------------------------------------------------------------- */

int __init yam_init(void)
{
	struct net_device *dev;
	int i;

	printk(yam_drvinfo);

	/* Clears the IRQ table */
	memset(irqs, 0, sizeof(irqs));
	memset(yam_ports, 0, sizeof(yam_ports));

	for (i = 0; i < NR_PORTS; i++) {
		sprintf(yam_ports[i].name, "yam%d", i);
		yam_ports[i].magic = YAM_MAGIC;
		yam_ports[i].bitrate = DEFAULT_BITRATE;
		yam_ports[i].baudrate = DEFAULT_BITRATE * 2;
		yam_ports[i].iobase = 0;
		yam_ports[i].irq = 0;
		yam_ports[i].dupmode = 0;
		yam_ports[i].holdd = DEFAULT_HOLDD;
		yam_ports[i].txd = DEFAULT_TXD;
		yam_ports[i].txtail = DEFAULT_TXTAIL;
		yam_ports[i].slot = DEFAULT_SLOT;
		yam_ports[i].pers = DEFAULT_PERS;

		dev = &yam_ports[i].dev;

		dev->priv = &yam_ports[i];
		dev->name = yam_ports[i].name;
		dev->base_addr = yam_ports[i].iobase;
		dev->irq = yam_ports[i].irq;
		dev->init = yam_probe;
		dev->if_port = 0;
		dev->start = 0;
		dev->tbusy = 1;

		if (register_netdev(dev)) {
			printk(KERN_WARNING "yam: cannot register net  device %s\n", dev->name);
			return -ENXIO;
		}
	}

	yam_timer.function = yam_dotimer;
	yam_timer.expires = jiffies + HZ / 100;
	add_timer(&yam_timer);

	yam_net_procfs_init();
	return 1;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE

/*
 * command line settable parameters
 */


MODULE_AUTHOR("Frederic Rible F1OAT frible@teaser.fr");
MODULE_DESCRIPTION("Yam amateur radio modem driver");

int init_module(void)
{
	int ret = yam_init();

	return (ret == 1) ? 0 : ret;
}

/* --------------------------------------------------------------------- */

void cleanup_module(void)
{
	int i;

	del_timer(&yam_timer);
	for (i = 0; i < NR_PORTS; i++) {
		struct net_device *dev = &yam_ports[i].dev;
		if (!dev->priv)
			continue;
		if (dev->start)
			yam_close(dev);
		unregister_netdev(dev);
	}
	free_mcs();
	yam_net_procfs_remove();
}

#endif							/* MODULE */
/* --------------------------------------------------------------------- */
