/*
 * Network device driver for the MACE ethernet controller on
 * Apple Powermacs.  Assumes it's under a DBDMA controller.
 *
 * Copyright (C) 1996 Paul Mackerras.
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/kernel.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <asm/prom.h>
#include <asm/dbdma.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include "mace.h"

#ifdef MODULE
static struct net_device *mace_devs = NULL;
#endif

#define N_RX_RING	8
#define N_TX_RING	6
#define MAX_TX_ACTIVE	1
#define NCMDS_TX	1	/* dma commands per element in tx ring */
#define RX_BUFLEN	(ETH_FRAME_LEN + 8)
#define TX_TIMEOUT	HZ	/* 1 second */

/* Bits in transmit DMA status */
#define TX_DMA_ERR	0x80

struct mace_data {
    volatile struct mace *mace;
    volatile struct dbdma_regs *tx_dma;
    int tx_dma_intr;
    volatile struct dbdma_regs *rx_dma;
    int rx_dma_intr;
    volatile struct dbdma_cmd *tx_cmds;	/* xmit dma command list */
    volatile struct dbdma_cmd *rx_cmds;	/* recv dma command list */
    struct sk_buff *rx_bufs[N_RX_RING];
    int rx_fill;
    int rx_empty;
    struct sk_buff *tx_bufs[N_TX_RING];
    int tx_fill;
    int tx_empty;
    unsigned char maccc;
    unsigned char tx_fullup;
    unsigned char tx_active;
    unsigned char tx_bad_runt;
    struct net_device_stats stats;
    struct timer_list tx_timeout;
    int timeout_active;
};

/*
 * Number of bytes of private data per MACE: allow enough for
 * the rx and tx dma commands plus a branch dma command each,
 * and another 16 bytes to allow us to align the dma command
 * buffers on a 16 byte boundary.
 */
#define PRIV_BYTES	(sizeof(struct mace_data) \
	+ (N_RX_RING + NCMDS_TX * N_TX_RING + 3) * sizeof(struct dbdma_cmd))

static int bitrev(int);
static int mace_open(struct net_device *dev);
static int mace_close(struct net_device *dev);
static int mace_xmit_start(struct sk_buff *skb, struct net_device *dev);
static struct net_device_stats *mace_stats(struct net_device *dev);
static void mace_set_multicast(struct net_device *dev);
static void mace_reset(struct net_device *dev);
static int mace_set_address(struct net_device *dev, void *addr);
static void mace_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static void mace_txdma_intr(int irq, void *dev_id, struct pt_regs *regs);
static void mace_rxdma_intr(int irq, void *dev_id, struct pt_regs *regs);
static void mace_set_timeout(struct net_device *dev);
static void mace_tx_timeout(unsigned long data);
static inline void dbdma_reset(volatile struct dbdma_regs *dma);
static inline void mace_clean_rings(struct mace_data *mp);
static void __mace_set_address(struct net_device *dev, void *addr);

/*
 * If we can't get a skbuff when we need it, we use this area for DMA.
 */
static unsigned char dummy_buf[RX_BUFLEN+2];

/* Bit-reverse one byte of an ethernet hardware address. */
static inline int
bitrev(int b)
{
    int d = 0, i;

    for (i = 0; i < 8; ++i, b >>= 1)
	d = (d << 1) | (b & 1);
    return d;
}

int mace_probe(void)
{
	int j, rev;
	struct net_device *dev;
	struct mace_data *mp;
	struct device_node *mace;
	unsigned char *addr;
	static int maces_found = 0;
	static struct device_node *next_mace;

	if (!maces_found) {
		next_mace = find_devices("mace");
		maces_found = 1;
	}
	mace = next_mace;
	if (mace == 0)
		return -ENODEV;
	next_mace = mace->next;

	if (mace->n_addrs != 3 || mace->n_intrs != 3) {
		printk(KERN_ERR "can't use MACE %s: expect 3 addrs and 3 intrs\n",
		       mace->full_name);
		return -ENODEV;
	}

	dev = init_etherdev(0, PRIV_BYTES);
	memset(dev->priv, 0, PRIV_BYTES);

	mp = (struct mace_data *) dev->priv;
	dev->base_addr = mace->addrs[0].address;
	mp->mace = (volatile struct mace *)
		ioremap(mace->addrs[0].address, 0x1000);
	dev->irq = mace->intrs[0].line;

	addr = get_property(mace, "mac-address", NULL);
	if (addr == NULL) {
		addr = get_property(mace, "local-mac-address", NULL);
		if (addr == NULL) {
			printk(KERN_ERR "Can't get mac-address for MACE at %lx\n",
			       dev->base_addr);
			return -EAGAIN;
		}
	}

	printk(KERN_INFO "%s: MACE at", dev->name);
	rev = addr[0] == 0 && addr[1] == 0xA0;
	for (j = 0; j < 6; ++j) {
		dev->dev_addr[j] = rev? bitrev(addr[j]): addr[j];
		printk("%c%.2x", (j? ':': ' '), dev->dev_addr[j]);
	}
	printk(", chip revision %d.%d\n",
		in_8(&mp->mace->chipid_hi), in_8(&mp->mace->chipid_lo));

	mp = (struct mace_data *) dev->priv;
	mp->maccc = ENXMT | ENRCV;
	mp->tx_dma = (volatile struct dbdma_regs *)
		ioremap(mace->addrs[1].address, 0x1000);
	mp->tx_dma_intr = mace->intrs[1].line;
	mp->rx_dma = (volatile struct dbdma_regs *)
		ioremap(mace->addrs[2].address, 0x1000);
	mp->rx_dma_intr = mace->intrs[2].line;

	mp->tx_cmds = (volatile struct dbdma_cmd *) DBDMA_ALIGN(mp + 1);
	mp->rx_cmds = mp->tx_cmds + NCMDS_TX * N_TX_RING + 1;

	memset(&mp->stats, 0, sizeof(mp->stats));
	memset((char *) mp->tx_cmds, 0,
	       (NCMDS_TX*N_TX_RING + N_RX_RING + 2) * sizeof(struct dbdma_cmd));
	init_timer(&mp->tx_timeout);
	mp->timeout_active = 0;

	dev->open = mace_open;
	dev->stop = mace_close;
	dev->hard_start_xmit = mace_xmit_start;
	dev->get_stats = mace_stats;
	dev->set_multicast_list = mace_set_multicast;
	dev->set_mac_address = mace_set_address;

	ether_setup(dev);

	mace_reset(dev);

	if (request_irq(dev->irq, mace_interrupt, 0, "MACE", dev)) {
		printk(KERN_ERR "MACE: can't get irq %d\n", dev->irq);
		return -EAGAIN;
	}
	if (request_irq(mace->intrs[1].line, mace_txdma_intr, 0, "MACE-txdma",
			dev)) {
		printk(KERN_ERR "MACE: can't get irq %d\n", mace->intrs[1].line);
		return -EAGAIN;
	}
	if (request_irq(mace->intrs[2].line, mace_rxdma_intr, 0, "MACE-rxdma",
			dev)) {
		printk(KERN_ERR "MACE: can't get irq %d\n", mace->intrs[2].line);
		return -EAGAIN;
	}

	return 0;
}

static void dbdma_reset(volatile struct dbdma_regs *dma)
{
    int i;

    out_le32(&dma->control, (WAKE|FLUSH|PAUSE|RUN) << 16);

    /*
     * Yes this looks peculiar, but apparently it needs to be this
     * way on some machines.
     */
    for (i = 200; i > 0; --i)
	if (ld_le32(&dma->control) & RUN)
	    udelay(1);
}

static void mace_reset(struct net_device *dev)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    int i;

    /* soft-reset the chip */
    i = 200;
    while (--i) {
	out_8(&mb->biucc, SWRST);
	if (in_8(&mb->biucc) & SWRST) {
	    udelay(10);
	    continue;
	}
	break;
    }
    if (!i) {
	printk(KERN_ERR "mace: cannot reset chip!\n");
	return;
    }

    out_8(&mb->imr, 0xff);	/* disable all intrs for now */
    i = in_8(&mb->ir);
    out_8(&mb->maccc, 0);	/* turn off tx, rx */

    out_8(&mb->biucc, XMTSP_64);
    out_8(&mb->utr, RTRD);
    out_8(&mb->fifocc, RCVFW_32 | XMTFW_16 | XMTFWU | RCVFWU | XMTBRST);
    out_8(&mb->xmtfc, AUTO_PAD_XMIT); /* auto-pad short frames */
    out_8(&mb->rcvfc, 0);

    /* load up the hardware address */
    __mace_set_address(dev, dev->dev_addr);

    /* clear the multicast filter */
    out_8(&mb->iac, ADDRCHG | LOGADDR);
    while ((in_8(&mb->iac) & ADDRCHG) != 0)
	;
    for (i = 0; i < 8; ++i) {
	out_8(&mb->ladrf, 0);
    }
    /* done changing address */
    out_8(&mb->iac, 0);

    out_8(&mb->plscc, PORTSEL_GPSI + ENPLSIO);
}

static void __mace_set_address(struct net_device *dev, void *addr)
{
    volatile struct mace *mb = ((struct mace_data *) dev->priv)->mace;
    unsigned char *p = addr;
    int i;

    /* load up the hardware address */
    out_8(&mb->iac, ADDRCHG | PHYADDR);
    while ((in_8(&mb->iac) & ADDRCHG) != 0)
	;
    for (i = 0; i < 6; ++i)
	out_8(&mb->padr, dev->dev_addr[i] = p[i]);
}

static int mace_set_address(struct net_device *dev, void *addr)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    unsigned long flags;

    save_flags(flags); cli();

    __mace_set_address(dev, addr);

    out_8(&mb->iac, 0);
    /* note: setting ADDRCHG clears ENRCV */
    out_8(&mb->maccc, mp->maccc);

    restore_flags(flags);
    return 0;
}

static int mace_open(struct net_device *dev)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    volatile struct dbdma_regs *rd = mp->rx_dma;
    volatile struct dbdma_regs *td = mp->tx_dma;
    volatile struct dbdma_cmd *cp;
    int i;
    struct sk_buff *skb;
    unsigned char *data;

    /* reset the chip */
    mace_reset(dev);

    /* initialize list of sk_buffs for receiving and set up recv dma */
    mace_clean_rings(mp);
    memset((char *)mp->rx_cmds, 0, N_RX_RING * sizeof(struct dbdma_cmd));
    cp = mp->rx_cmds;
    for (i = 0; i < N_RX_RING - 1; ++i) {
	skb = dev_alloc_skb(RX_BUFLEN + 2);
	if (skb == 0) {
	    data = dummy_buf;
	} else {
	    skb_reserve(skb, 2);	/* so IP header lands on 4-byte bdry */
	    data = skb->data;
	}
	mp->rx_bufs[i] = skb;
	st_le16(&cp->req_count, RX_BUFLEN);
	st_le16(&cp->command, INPUT_LAST + INTR_ALWAYS);
	st_le32(&cp->phy_addr, virt_to_bus(data));
	cp->xfer_status = 0;
	++cp;
    }
    mp->rx_bufs[i] = 0;
    st_le16(&cp->command, DBDMA_STOP);
    mp->rx_fill = i;
    mp->rx_empty = 0;

    /* Put a branch back to the beginning of the receive command list */
    ++cp;
    st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
    st_le32(&cp->cmd_dep, virt_to_bus(mp->rx_cmds));

    /* start rx dma */
    out_le32(&rd->control, (RUN|PAUSE|FLUSH|WAKE) << 16); /* clear run bit */
    out_le32(&rd->cmdptr, virt_to_bus(mp->rx_cmds));
    out_le32(&rd->control, (RUN << 16) | RUN);

    /* put a branch at the end of the tx command list */
    cp = mp->tx_cmds + NCMDS_TX * N_TX_RING;
    st_le16(&cp->command, DBDMA_NOP + BR_ALWAYS);
    st_le32(&cp->cmd_dep, virt_to_bus(mp->tx_cmds));

    /* reset tx dma */
    out_le32(&td->control, (RUN|PAUSE|FLUSH|WAKE) << 16);
    out_le32(&td->cmdptr, virt_to_bus(mp->tx_cmds));
    mp->tx_fill = 0;
    mp->tx_empty = 0;
    mp->tx_fullup = 0;
    mp->tx_active = 0;
    mp->tx_bad_runt = 0;

    /* turn it on! */
    out_8(&mb->maccc, mp->maccc);
    /* enable all interrupts except receive interrupts */
    out_8(&mb->imr, RCVINT);

#ifdef MOD_INC_USE_COUNT
    MOD_INC_USE_COUNT;
#endif
    return 0;
}

static inline void mace_clean_rings(struct mace_data *mp)
{
    int i;

    /* free some skb's */
    for (i = 0; i < N_RX_RING; ++i) {
	if (mp->rx_bufs[i] != 0) {
	    dev_kfree_skb(mp->rx_bufs[i]);
	    mp->rx_bufs[i] = 0;
	}
    }
    for (i = mp->tx_empty; i != mp->tx_fill; ) {
	dev_kfree_skb(mp->tx_bufs[i]);
	if (++i >= N_TX_RING)
	    i = 0;
    }
}

static int mace_close(struct net_device *dev)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    volatile struct dbdma_regs *rd = mp->rx_dma;
    volatile struct dbdma_regs *td = mp->tx_dma;

    /* disable rx and tx */
    out_8(&mb->maccc, 0);
    out_8(&mb->imr, 0xff);		/* disable all intrs */

    /* disable rx and tx dma */
    st_le32(&rd->control, (RUN|PAUSE|FLUSH|WAKE) << 16); /* clear run bit */
    st_le32(&td->control, (RUN|PAUSE|FLUSH|WAKE) << 16); /* clear run bit */

    mace_clean_rings(mp);

#ifdef MOD_DEC_USE_COUNT
    MOD_DEC_USE_COUNT;
#endif

    return 0;
}

static inline void mace_set_timeout(struct net_device *dev)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    unsigned long flags;

    save_flags(flags);
    cli();
    if (mp->timeout_active)
	del_timer(&mp->tx_timeout);
    mp->tx_timeout.expires = jiffies + TX_TIMEOUT;
    mp->tx_timeout.function = mace_tx_timeout;
    mp->tx_timeout.data = (unsigned long) dev;
    add_timer(&mp->tx_timeout);
    mp->timeout_active = 1;
    restore_flags(flags);
}

static int mace_xmit_start(struct sk_buff *skb, struct net_device *dev)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct dbdma_regs *td = mp->tx_dma;
    volatile struct dbdma_cmd *cp, *np;
    unsigned long flags;
    int fill, next, len;

    /* see if there's a free slot in the tx ring */
    save_flags(flags); cli();
    fill = mp->tx_fill;
    next = fill + 1;
    if (next >= N_TX_RING)
	next = 0;
    if (next == mp->tx_empty) {
	dev->tbusy = 1;
	mp->tx_fullup = 1;
	restore_flags(flags);
	return 1;		/* can't take it at the moment */
    }
    restore_flags(flags);

    /* partially fill in the dma command block */
    len = skb->len;
    if (len > ETH_FRAME_LEN) {
	printk(KERN_DEBUG "mace: xmit frame too long (%d)\n", len);
	len = ETH_FRAME_LEN;
    }
    mp->tx_bufs[fill] = skb;
    cp = mp->tx_cmds + NCMDS_TX * fill;
    st_le16(&cp->req_count, len);
    st_le32(&cp->phy_addr, virt_to_bus(skb->data));

    np = mp->tx_cmds + NCMDS_TX * next;
    out_le16(&np->command, DBDMA_STOP);

    /* poke the tx dma channel */
    save_flags(flags);
    cli();
    mp->tx_fill = next;
    if (!mp->tx_bad_runt && mp->tx_active < MAX_TX_ACTIVE) {
	out_le16(&cp->xfer_status, 0);
	out_le16(&cp->command, OUTPUT_LAST);
	out_le32(&td->control, ((RUN|WAKE) << 16) + (RUN|WAKE));
	++mp->tx_active;
	mace_set_timeout(dev);
    }
    if (++next >= N_TX_RING)
	next = 0;
    if (next == mp->tx_empty)
	dev->tbusy = 1;
    restore_flags(flags);

    return 0;
}

static struct net_device_stats *mace_stats(struct net_device *dev)
{
    struct mace_data *p = (struct mace_data *) dev->priv;

    return &p->stats;
}

/*
 * CRC polynomial - used in working out multicast filter bits.
 */
#define CRC_POLY	0xedb88320

static void mace_set_multicast(struct net_device *dev)
{
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    int i, j, k, b;
    unsigned long crc;

    mp->maccc &= ~PROM;
    if (dev->flags & IFF_PROMISC) {
	mp->maccc |= PROM;
    } else {
	unsigned char multicast_filter[8];
	struct dev_mc_list *dmi = dev->mc_list;

	if (dev->flags & IFF_ALLMULTI) {
	    for (i = 0; i < 8; i++)
		multicast_filter[i] = 0xff;
	} else {
	    for (i = 0; i < 8; i++)
		multicast_filter[i] = 0;
	    for (i = 0; i < dev->mc_count; i++) {
		crc = ~0;
		for (j = 0; j < 6; ++j) {
		    b = dmi->dmi_addr[j];
		    for (k = 0; k < 8; ++k) {
			if ((crc ^ b) & 1)
			    crc = (crc >> 1) ^ CRC_POLY;
			else
			    crc >>= 1;
			b >>= 1;
		    }
		}
		j = crc >> 26;	/* bit number in multicast_filter */
		multicast_filter[j >> 3] |= 1 << (j & 7);
		dmi = dmi->next;
	    }
	}
#if 0
	printk("Multicast filter :");
	for (i = 0; i < 8; i++)
	    printk("%02x ", multicast_filter[i]);
	printk("\n");
#endif

	out_8(&mb->iac, ADDRCHG | LOGADDR);
	while ((in_8(&mb->iac) & ADDRCHG) != 0)
	    ;
	for (i = 0; i < 8; ++i) {
	    out_8(&mb->ladrf, multicast_filter[i]);
	}
    }
    /* reset maccc */
    out_8(&mb->maccc, mp->maccc);
}

static void mace_handle_misc_intrs(struct mace_data *mp, int intr)
{
    volatile struct mace *mb = mp->mace;
    static int mace_babbles, mace_jabbers;

    if (intr & MPCO)
	mp->stats.rx_missed_errors += 256;
    mp->stats.rx_missed_errors += in_8(&mb->mpc);   /* reading clears it */
    if (intr & RNTPCO)
	mp->stats.rx_length_errors += 256;
    mp->stats.rx_length_errors += in_8(&mb->rntpc); /* reading clears it */
    if (intr & CERR)
	++mp->stats.tx_heartbeat_errors;
    if (intr & BABBLE)
	if (mace_babbles++ < 4)
	    printk(KERN_DEBUG "mace: babbling transmitter\n");
    if (intr & JABBER)
	if (mace_jabbers++ < 4)
	    printk(KERN_DEBUG "mace: jabbering transceiver\n");
}

static void mace_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct net_device *dev = (struct net_device *) dev_id;
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    volatile struct dbdma_regs *td = mp->tx_dma;
    volatile struct dbdma_cmd *cp;
    int intr, fs, i, stat, x;
    int xcount, dstat;
    /* static int mace_last_fs, mace_last_xcount; */

    intr = in_8(&mb->ir);		/* read interrupt register */
    in_8(&mb->xmtrc);			/* get retries */
    mace_handle_misc_intrs(mp, intr);

    i = mp->tx_empty;
    while (in_8(&mb->pr) & XMTSV) {
	del_timer(&mp->tx_timeout);
	mp->timeout_active = 0;
	/*
	 * Clear any interrupt indication associated with this status
	 * word.  This appears to unlatch any error indication from
	 * the DMA controller.
	 */
	intr = in_8(&mb->ir);
	if (intr != 0)
	    mace_handle_misc_intrs(mp, intr);
	if (mp->tx_bad_runt) {
	    fs = in_8(&mb->xmtfs);
	    mp->tx_bad_runt = 0;
	    out_8(&mb->xmtfc, AUTO_PAD_XMIT);
	    continue;
	}
	dstat = ld_le32(&td->status);
	/* stop DMA controller */
	out_le32(&td->control, RUN << 16);
	/*
	 * xcount is the number of complete frames which have been
	 * written to the fifo but for which status has not been read.
	 */
	xcount = (in_8(&mb->fifofc) >> XMTFC_SH) & XMTFC_MASK;
	if (xcount == 0 || (dstat & DEAD)) {
	    /*
	     * If a packet was aborted before the DMA controller has
	     * finished transferring it, it seems that there are 2 bytes
	     * which are stuck in some buffer somewhere.  These will get
	     * transmitted as soon as we read the frame status (which
	     * reenables the transmit data transfer request).  Turning
	     * off the DMA controller and/or resetting the MACE doesn't
	     * help.  So we disable auto-padding and FCS transmission
	     * so the two bytes will only be a runt packet which should
	     * be ignored by other stations.
	     */
	    out_8(&mb->xmtfc, DXMTFCS);
	}
	fs = in_8(&mb->xmtfs);
	if ((fs & XMTSV) == 0) {
	    printk(KERN_ERR "mace: xmtfs not valid! (fs=%x xc=%d ds=%x)\n",
		   fs, xcount, dstat);
	    mace_reset(dev);
		/*
		 * XXX mace likes to hang the machine after a xmtfs error.
		 * This is hard to reproduce, reseting *may* help
		 */
	}
	cp = mp->tx_cmds + NCMDS_TX * i;
	stat = ld_le16(&cp->xfer_status);
	if ((fs & (UFLO|LCOL|LCAR|RTRY)) || (dstat & DEAD) || xcount == 0) {
	    /*
	     * Check whether there were in fact 2 bytes written to
	     * the transmit FIFO.
	     */
	    udelay(1);
	    x = (in_8(&mb->fifofc) >> XMTFC_SH) & XMTFC_MASK;
	    if (x != 0) {
		/* there were two bytes with an end-of-packet indication */
		mp->tx_bad_runt = 1;
		mace_set_timeout(dev);
	    } else {
		/*
		 * Either there weren't the two bytes buffered up, or they
		 * didn't have an end-of-packet indication.
		 * We flush the transmit FIFO just in case (by setting the
		 * XMTFWU bit with the transmitter disabled).
		 */
		out_8(&mb->maccc, in_8(&mb->maccc) & ~ENXMT);
		out_8(&mb->fifocc, in_8(&mb->fifocc) | XMTFWU);
		udelay(1);
		out_8(&mb->maccc, in_8(&mb->maccc) | ENXMT);
		out_8(&mb->xmtfc, AUTO_PAD_XMIT);
	    }
	}
	/* dma should have finished */
	if (i == mp->tx_fill) {
	    printk(KERN_DEBUG "mace: tx ring ran out? (fs=%x xc=%d ds=%x)\n",
		   fs, xcount, dstat);
	    continue;
	}
	/* Update stats */
	if (fs & (UFLO|LCOL|LCAR|RTRY)) {
	    ++mp->stats.tx_errors;
	    if (fs & LCAR)
		++mp->stats.tx_carrier_errors;
	    if (fs & (UFLO|LCOL|RTRY))
		++mp->stats.tx_aborted_errors;
	} else {
	    mp->stats.tx_bytes += mp->tx_bufs[i]->len;
	    ++mp->stats.tx_packets;
	}
	dev_kfree_skb(mp->tx_bufs[i]);
	--mp->tx_active;
	if (++i >= N_TX_RING)
	    i = 0;
#if 0
	mace_last_fs = fs;
	mace_last_xcount = xcount;
#endif
    }

    if (i != mp->tx_empty) {
	mp->tx_fullup = 0;
	dev->tbusy = 0;
	mark_bh(NET_BH);
    }
    mp->tx_empty = i;
    i += mp->tx_active;
    if (i >= N_TX_RING)
	i -= N_TX_RING;
    if (!mp->tx_bad_runt && i != mp->tx_fill && mp->tx_active < MAX_TX_ACTIVE) {
	do {
	    /* set up the next one */
	    cp = mp->tx_cmds + NCMDS_TX * i;
	    out_le16(&cp->xfer_status, 0);
	    out_le16(&cp->command, OUTPUT_LAST);
	    ++mp->tx_active;
	    if (++i >= N_TX_RING)
		i = 0;
	} while (i != mp->tx_fill && mp->tx_active < MAX_TX_ACTIVE);
	out_le32(&td->control, ((RUN|WAKE) << 16) + (RUN|WAKE));
	mace_set_timeout(dev);
    }
}

static void mace_tx_timeout(unsigned long data)
{
    struct net_device *dev = (struct net_device *) data;
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct mace *mb = mp->mace;
    volatile struct dbdma_regs *td = mp->tx_dma;
    volatile struct dbdma_regs *rd = mp->rx_dma;
    volatile struct dbdma_cmd *cp;
    unsigned long flags;
    int i;

    save_flags(flags);
    cli();
    mp->timeout_active = 0;
    if (mp->tx_active == 0 && !mp->tx_bad_runt)
	goto out;

    /* update various counters */
    mace_handle_misc_intrs(mp, in_8(&mb->ir));

    cp = mp->tx_cmds + NCMDS_TX * mp->tx_empty;

    /* turn off both tx and rx and reset the chip */
    out_8(&mb->maccc, 0);
    printk(KERN_ERR "mace: transmit timeout - resetting\n");
    dbdma_reset(td);
    mace_reset(dev);

    /* restart rx dma */
    cp = bus_to_virt(ld_le32(&rd->cmdptr));
    dbdma_reset(rd);
    out_le16(&cp->xfer_status, 0);
    out_le32(&rd->cmdptr, virt_to_bus(cp));
    out_le32(&rd->control, (RUN << 16) | RUN);

    /* fix up the transmit side */
    i = mp->tx_empty;
    mp->tx_active = 0;
    ++mp->stats.tx_errors;
    if (mp->tx_bad_runt) {
	mp->tx_bad_runt = 0;
    } else if (i != mp->tx_fill) {
	dev_kfree_skb(mp->tx_bufs[i]);
	if (++i >= N_TX_RING)
	    i = 0;
	mp->tx_empty = i;
    }
    mp->tx_fullup = 0;
    dev->tbusy = 0;
    mark_bh(NET_BH);
    if (i != mp->tx_fill) {
	cp = mp->tx_cmds + NCMDS_TX * i;
	out_le16(&cp->xfer_status, 0);
	out_le16(&cp->command, OUTPUT_LAST);
	out_le32(&td->cmdptr, virt_to_bus(cp));
	out_le32(&td->control, (RUN << 16) | RUN);
	++mp->tx_active;
	mace_set_timeout(dev);
    }

    /* turn it back on */
    out_8(&mb->imr, RCVINT);
    out_8(&mb->maccc, mp->maccc);

out:
    restore_flags(flags);
}

static void mace_txdma_intr(int irq, void *dev_id, struct pt_regs *regs)
{
}

static void mace_rxdma_intr(int irq, void *dev_id, struct pt_regs *regs)
{
    struct net_device *dev = (struct net_device *) dev_id;
    struct mace_data *mp = (struct mace_data *) dev->priv;
    volatile struct dbdma_regs *rd = mp->rx_dma;
    volatile struct dbdma_cmd *cp, *np;
    int i, nb, stat, next;
    struct sk_buff *skb;
    unsigned frame_status;
    static int mace_lost_status;
    unsigned char *data;

    for (i = mp->rx_empty; i != mp->rx_fill; ) {
	cp = mp->rx_cmds + i;
	stat = ld_le16(&cp->xfer_status);
	if ((stat & ACTIVE) == 0) {
	    next = i + 1;
	    if (next >= N_RX_RING)
		next = 0;
	    np = mp->rx_cmds + next;
	    if (next != mp->rx_fill
		&& (ld_le16(&np->xfer_status) & ACTIVE) != 0) {
		printk(KERN_DEBUG "mace: lost a status word\n");
		++mace_lost_status;
	    } else
		break;
	}
	nb = ld_le16(&cp->req_count) - ld_le16(&cp->res_count);
	out_le16(&cp->command, DBDMA_STOP);
	/* got a packet, have a look at it */
	skb = mp->rx_bufs[i];
	if (skb == 0) {
	    ++mp->stats.rx_dropped;
	} else if (nb > 8) {
	    data = skb->data;
	    frame_status = (data[nb-3] << 8) + data[nb-4];
	    if (frame_status & (RS_OFLO|RS_CLSN|RS_FRAMERR|RS_FCSERR)) {
		++mp->stats.rx_errors;
		if (frame_status & RS_OFLO)
		    ++mp->stats.rx_over_errors;
		if (frame_status & RS_FRAMERR)
		    ++mp->stats.rx_frame_errors;
		if (frame_status & RS_FCSERR)
		    ++mp->stats.rx_crc_errors;
	    } else {
		/* Mace feature AUTO_STRIP_RCV is on by default, dropping the
		 * FCS on frames with 802.3 headers. This means that Ethernet
		 * frames have 8 extra octets at the end, while 802.3 frames
		 * have only 4. We need to correctly account for this. */
		if (*(unsigned short *)(data+12) < 1536) /* 802.3 header */
		    nb -= 4;
		else	/* Ethernet header; mace includes FCS */
		    nb -= 8;
		skb_put(skb, nb);
		skb->dev = dev;
		skb->protocol = eth_type_trans(skb, dev);
		netif_rx(skb);
		mp->rx_bufs[i] = 0;
		mp->stats.rx_bytes += skb->len;
		++mp->stats.rx_packets;
	    }
	} else {
	    ++mp->stats.rx_errors;
	    ++mp->stats.rx_length_errors;
	}

	/* advance to next */
	if (++i >= N_RX_RING)
	    i = 0;
    }
    mp->rx_empty = i;

    i = mp->rx_fill;
    for (;;) {
	next = i + 1;
	if (next >= N_RX_RING)
	    next = 0;
	if (next == mp->rx_empty)
	    break;
	cp = mp->rx_cmds + i;
	skb = mp->rx_bufs[i];
	if (skb == 0) {
	    skb = dev_alloc_skb(RX_BUFLEN + 2);
	    if (skb != 0) {
		skb_reserve(skb, 2);
		mp->rx_bufs[i] = skb;
	    }
	}
	st_le16(&cp->req_count, RX_BUFLEN);
	data = skb? skb->data: dummy_buf;
	st_le32(&cp->phy_addr, virt_to_bus(data));
	out_le16(&cp->xfer_status, 0);
	out_le16(&cp->command, INPUT_LAST + INTR_ALWAYS);
#if 0
	if ((ld_le32(&rd->status) & ACTIVE) != 0) {
	    out_le32(&rd->control, (PAUSE << 16) | PAUSE);
	    while ((in_le32(&rd->status) & ACTIVE) != 0)
		;
	}
#endif
	i = next;
    }
    if (i != mp->rx_fill) {
	out_le32(&rd->control, ((RUN|WAKE) << 16) | (RUN|WAKE));
	mp->rx_fill = i;
    }
}

#ifdef MODULE

#if LINUX_VERSION_CODE > 0x20118
MODULE_AUTHOR("Paul Mackerras");
MODULE_DESCRIPTION("PowerMac MACE driver.");
#endif

int init_module(void)
{
    int res;

    if(mace_devs != NULL)
	return -EBUSY;
    res = mace_probe();
    return res;
}

void cleanup_module(void)
{
    struct mace_data *mp = (struct mace_data *) mace_devs->priv;
    unregister_netdev(mace_devs);

    free_irq(mace_devs->irq, mace_interrupt);
    free_irq(mp->tx_dma_intr, mace_txdma_intr);
    free_irq(mp->rx_dma_intr, mace_rxdma_intr);

    kfree(mace_devs);
    mace_devs = NULL;
}

#endif
