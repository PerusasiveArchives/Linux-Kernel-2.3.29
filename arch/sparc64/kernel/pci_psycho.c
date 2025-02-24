/* $Id: pci_psycho.c,v 1.4 1999/09/05 09:33:36 ecd Exp $
 * pci_psycho.c: PSYCHO/U2P specific PCI controller support.
 *
 * Copyright (C) 1997, 1998, 1999 David S. Miller (davem@caipfs.rutgers.edu)
 * Copyright (C) 1998, 1999 Eddie C. Dost   (ecd@skynet.be)
 * Copyright (C) 1999 Jakub Jelinek   (jj@ultra.linux.cz)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/malloc.h>

#include <asm/pbm.h>
#include <asm/iommu.h>
#include <asm/irq.h>

#include "pci_impl.h"

/* All PSYCHO registers are 64-bits.  The following accessor
 * routines are how they are accessed.  The REG parameter
 * is a physical address.
 */
#define psycho_read(__reg) \
({	u64 __ret; \
	__asm__ __volatile__("ldxa [%1] %2, %0" \
			     : "=r" (__ret) \
			     : "r" (__reg), "i" (ASI_PHYS_BYPASS_EC_E) \
			     : "memory"); \
	__ret; \
})
#define psycho_write(__reg, __val) \
	__asm__ __volatile__("stxa %0, [%1] %2" \
			     : /* no outputs */ \
			     : "r" (__val), "r" (__reg), \
			       "i" (ASI_PHYS_BYPASS_EC_E))

/* Misc. PSYCHO PCI controller register offsets and definitions. */
#define PSYCHO_CONTROL		0x0010UL
#define  PSYCHO_CONTROL_IMPL	 0xf000000000000000 /* Implementation of this PSYCHO*/
#define  PSYCHO_CONTROL_VER	 0x0f00000000000000 /* Version of this PSYCHO       */
#define  PSYCHO_CONTROL_MID	 0x00f8000000000000 /* UPA Module ID of PSYCHO      */
#define  PSYCHO_CONTROL_IGN	 0x0007c00000000000 /* Interrupt Group Number       */
#define  PSYCHO_CONTROL_RESV     0x00003ffffffffff0 /* Reserved                     */
#define  PSYCHO_CONTROL_APCKEN	 0x0000000000000008 /* Address Parity Check Enable  */
#define  PSYCHO_CONTROL_APERR	 0x0000000000000004 /* Incoming System Addr Parerr  */
#define  PSYCHO_CONTROL_IAP	 0x0000000000000002 /* Invert UPA Parity            */
#define  PSYCHO_CONTROL_MODE	 0x0000000000000001 /* PSYCHO clock mode            */
#define PSYCHO_PCIA_CTRL	0x2000UL
#define PSYCHO_PCIB_CTRL	0x4000UL
#define  PSYCHO_PCICTRL_RESV1	 0xfffffff000000000 /* Reserved                     */
#define  PSYCHO_PCICTRL_SBH_ERR	 0x0000000800000000 /* Streaming byte hole error    */
#define  PSYCHO_PCICTRL_SERR	 0x0000000400000000 /* SERR signal asserted         */
#define  PSYCHO_PCICTRL_SPEED	 0x0000000200000000 /* PCI speed (1 is U2P clock)   */
#define  PSYCHO_PCICTRL_RESV2	 0x00000001ffc00000 /* Reserved                     */
#define  PSYCHO_PCICTRL_ARB_PARK 0x0000000000200000 /* PCI arbitration parking      */
#define  PSYCHO_PCICTRL_RESV3	 0x00000000001ff800 /* Reserved                     */
#define  PSYCHO_PCICTRL_SBH_INT	 0x0000000000000400 /* Streaming byte hole int enab */
#define  PSYCHO_PCICTRL_WEN	 0x0000000000000200 /* Power Mgmt Wake Enable       */
#define  PSYCHO_PCICTRL_EEN	 0x0000000000000100 /* PCI Error Interrupt Enable   */
#define  PSYCHO_PCICTRL_RESV4	 0x00000000000000c0 /* Reserved                     */
#define  PSYCHO_PCICTRL_AEN	 0x000000000000003f /* PCI DVMA Arbitration Enable  */

/* U2P Programmer's Manual, page 13-55, configuration space
 * address format:
 * 
 *  32             24 23 16 15    11 10       8 7   2  1 0
 * ---------------------------------------------------------
 * |0 0 0 0 0 0 0 0 1| bus | device | function | reg | 0 0 |
 * ---------------------------------------------------------
 */
#define PSYCHO_CONFIG_BASE(PBM)	\
	((PBM)->parent->config_space | (1UL << 24))
#define PSYCHO_CONFIG_ENCODE(BUS, DEVFN, REG)	\
	(((unsigned long)(BUS)   << 16) |	\
	 ((unsigned long)(DEVFN) << 8)  |	\
	 ((unsigned long)(REG)))

static void *psycho_pci_config_mkaddr(struct pci_pbm_info *pbm,
				      unsigned char bus,
				      unsigned int devfn,
				      int where)
{
	if (!pbm)
		return NULL;
	return (void *)
		(PSYCHO_CONFIG_BASE(pbm) |
		 PSYCHO_CONFIG_ENCODE(bus, devfn, where));
}

static int psycho_out_of_range(struct pci_pbm_info *pbm,
			       unsigned char bus,
			       unsigned char devfn)
{
	return ((pbm->parent == 0) ||
		((pbm == &pbm->parent->pbm_B) &&
		 (bus == pbm->pci_first_busno) &&
		 PCI_SLOT(devfn) > 8) ||
		((pbm == &pbm->parent->pbm_A) &&
		 (bus == pbm->pci_first_busno) &&
		 PCI_SLOT(devfn) > 8));
}

/* PSYCHO PCI configuration space accessors. */

static int psycho_read_byte(struct pci_dev *dev, int where, u8 *value)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[dev->bus->number];
	unsigned char bus = dev->bus->number;
	unsigned int devfn = dev->devfn;
	u8 *addr;

	*value = 0xff;
	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;
	pci_config_read8(addr, value);
	return PCIBIOS_SUCCESSFUL;
}

static int psycho_read_word(struct pci_dev *dev, int where, u16 *value)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[dev->bus->number];
	unsigned char bus = dev->bus->number;
	unsigned int devfn = dev->devfn;
	u16 *addr;

	*value = 0xffff;
	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x01) {
		printk("pcibios_read_config_word: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}
	pci_config_read16(addr, value);
	return PCIBIOS_SUCCESSFUL;
}

static int psycho_read_dword(struct pci_dev *dev, int where, u32 *value)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[dev->bus->number];
	unsigned char bus = dev->bus->number;
	unsigned int devfn = dev->devfn;
	u32 *addr;

	*value = 0xffffffff;
	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x03) {
		printk("pcibios_read_config_dword: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}

	pci_config_read32(addr, value);
	return PCIBIOS_SUCCESSFUL;
}

static int psycho_write_byte(struct pci_dev *dev, int where, u8 value)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[dev->bus->number];
	unsigned char bus = dev->bus->number;
	unsigned int devfn = dev->devfn;
	u8 *addr;

	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	pci_config_write8(addr, value);
	return PCIBIOS_SUCCESSFUL;
}

static int psycho_write_word(struct pci_dev *dev, int where, u16 value)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[dev->bus->number];
	unsigned char bus = dev->bus->number;
	unsigned int devfn = dev->devfn;
	u16 *addr;

	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x01) {
		printk("pcibios_write_config_word: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}
	pci_config_write16(addr, value);
	return PCIBIOS_SUCCESSFUL;
}

static int psycho_write_dword(struct pci_dev *dev, int where, u32 value)
{
	struct pci_pbm_info *pbm = pci_bus2pbm[dev->bus->number];
	unsigned char bus = dev->bus->number;
	unsigned int devfn = dev->devfn;
	u32 *addr;

	addr = psycho_pci_config_mkaddr(pbm, bus, devfn, where);
	if (!addr)
		return PCIBIOS_SUCCESSFUL;

	if (psycho_out_of_range(pbm, bus, devfn))
		return PCIBIOS_SUCCESSFUL;

	if (where & 0x03) {
		printk("pcibios_write_config_dword: misaligned reg [%x]\n",
		       where);
		return PCIBIOS_SUCCESSFUL;
	}
	pci_config_write32(addr, value);
	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops psycho_ops = {
	psycho_read_byte,
	psycho_read_word,
	psycho_read_dword,
	psycho_write_byte,
	psycho_write_word,
	psycho_write_dword
};

/* PSYCHO interrupt mapping support. */
#define PSYCHO_IMAP_A_SLOT0	0x0c00UL
#define PSYCHO_IMAP_B_SLOT0	0x0c20UL
static unsigned long psycho_pcislot_imap_offset(unsigned long ino)
{
	unsigned int bus =  (ino & 0x10) >> 4;
	unsigned int slot = (ino & 0x0c) >> 2;

	if (bus == 0)
		return PSYCHO_IMAP_A_SLOT0 + (slot * 8);
	else
		return PSYCHO_IMAP_B_SLOT0 + (slot * 8);
}

#define PSYCHO_IMAP_SCSI	0x1000UL
#define PSYCHO_IMAP_ETH		0x1008UL
#define PSYCHO_IMAP_BPP		0x1010UL
#define PSYCHO_IMAP_AU_REC	0x1018UL
#define PSYCHO_IMAP_AU_PLAY	0x1020UL
#define PSYCHO_IMAP_PFAIL	0x1028UL
#define PSYCHO_IMAP_KMS		0x1030UL
#define PSYCHO_IMAP_FLPY	0x1038UL
#define PSYCHO_IMAP_SHW		0x1040UL
#define PSYCHO_IMAP_KBD		0x1048UL
#define PSYCHO_IMAP_MS		0x1050UL
#define PSYCHO_IMAP_SER		0x1058UL
#define PSYCHO_IMAP_TIM0	0x1060UL
#define PSYCHO_IMAP_TIM1	0x1068UL
#define PSYCHO_IMAP_UE		0x1070UL
#define PSYCHO_IMAP_CE		0x1078UL
#define PSYCHO_IMAP_A_ERR	0x1080UL
#define PSYCHO_IMAP_B_ERR	0x1088UL
#define PSYCHO_IMAP_PMGMT	0x1090UL
#define PSYCHO_IMAP_GFX		0x1098UL
#define PSYCHO_IMAP_EUPA	0x10a0UL

static unsigned long __onboard_imap_off[] = {
/*0x20*/	PSYCHO_IMAP_SCSI,
/*0x21*/	PSYCHO_IMAP_ETH,
/*0x22*/	PSYCHO_IMAP_BPP,
/*0x23*/	PSYCHO_IMAP_AU_REC,
/*0x24*/	PSYCHO_IMAP_AU_PLAY,
/*0x25*/	PSYCHO_IMAP_PFAIL,
/*0x26*/	PSYCHO_IMAP_KMS,
/*0x27*/	PSYCHO_IMAP_FLPY,
/*0x28*/	PSYCHO_IMAP_SHW,
/*0x29*/	PSYCHO_IMAP_KBD,
/*0x2a*/	PSYCHO_IMAP_MS,
/*0x2b*/	PSYCHO_IMAP_SER,
/*0x2c*/	PSYCHO_IMAP_TIM0,
/*0x2d*/	PSYCHO_IMAP_TIM1,
/*0x2e*/	PSYCHO_IMAP_UE,
/*0x2f*/	PSYCHO_IMAP_CE,
/*0x30*/	PSYCHO_IMAP_A_ERR,
/*0x31*/	PSYCHO_IMAP_B_ERR,
/*0x32*/	PSYCHO_IMAP_PMGMT
};
#define PSYCHO_ONBOARD_IRQ_BASE		0x20
#define PSYCHO_ONBOARD_IRQ_LAST		0x32
#define psycho_onboard_imap_offset(__ino) \
	__onboard_imap_off[(__ino) - PSYCHO_ONBOARD_IRQ_BASE]

#define PSYCHO_ICLR_A_SLOT0	0x1400UL
#define PSYCHO_ICLR_SCSI	0x1800UL

#define psycho_iclr_offset(ino)					      \
	((ino & 0x20) ? (PSYCHO_ICLR_SCSI + (((ino) & 0x1f) << 3)) :  \
			(PSYCHO_ICLR_A_SLOT0 + (((ino) & 0x1f)<<3)))

/* PCI PSYCHO INO number to Sparc PIL level. */
static unsigned char psycho_pil_table[] = {
/*0x00*/0, 0, 0, 0,	/* PCI A slot 0  Int A, B, C, D */
/*0x04*/0, 0, 0, 0,	/* PCI A slot 1  Int A, B, C, D */
/*0x08*/0, 0, 0, 0,	/* PCI A slot 2  Int A, B, C, D */
/*0x0c*/0, 0, 0, 0,	/* PCI A slot 3  Int A, B, C, D */
/*0x10*/0, 0, 0, 0,	/* PCI B slot 0  Int A, B, C, D */
/*0x14*/0, 0, 0, 0,	/* PCI B slot 1  Int A, B, C, D */
/*0x18*/0, 0, 0, 0,	/* PCI B slot 2  Int A, B, C, D */
/*0x1c*/0, 0, 0, 0,	/* PCI B slot 3  Int A, B, C, D */
/*0x20*/3,		/* SCSI				*/
/*0x21*/5,		/* Ethernet			*/
/*0x22*/8,		/* Parallel Port		*/
/*0x23*/13,		/* Audio Record			*/
/*0x24*/14,		/* Audio Playback		*/
/*0x25*/15,		/* PowerFail			*/
/*0x26*/3,		/* second SCSI			*/
/*0x27*/11,		/* Floppy			*/
/*0x28*/2,		/* Spare Hardware		*/
/*0x29*/9,		/* Keyboard			*/
/*0x2a*/4,		/* Mouse			*/
/*0x2b*/12,		/* Serial			*/
/*0x2c*/10,		/* Timer 0			*/
/*0x2d*/11,		/* Timer 1			*/
/*0x2e*/15,		/* Uncorrectable ECC		*/
/*0x2f*/15,		/* Correctable ECC		*/
/*0x30*/15,		/* PCI Bus A Error		*/
/*0x31*/15,		/* PCI Bus B Error		*/
/*0x32*/1,		/* Power Management		*/
};

static int __init psycho_ino_to_pil(struct pci_dev *pdev, unsigned int ino)
{
	int ret;

	ret = psycho_pil_table[ino];
	if (ret == 0 && pdev == NULL) {
		ret = 1;
	} else if (ret == 0) {
		switch ((pdev->class >> 16) & 0x0f) {
		case PCI_BASE_CLASS_STORAGE:
			ret = 4;

		case PCI_BASE_CLASS_NETWORK:
			ret = 6;

		case PCI_BASE_CLASS_DISPLAY:
			ret = 9;

		case PCI_BASE_CLASS_MULTIMEDIA:
		case PCI_BASE_CLASS_MEMORY:
		case PCI_BASE_CLASS_BRIDGE:
			ret = 10;

		default:
			ret = 1;
		};
	}

	return ret;
}

static unsigned int __init psycho_irq_build(struct pci_controller_info *p,
					    struct pci_dev *pdev,
					    unsigned int ino)
{
	struct ino_bucket *bucket;
	volatile unsigned int *imap, *iclr;
	unsigned long imap_off, iclr_off;
	int pil, inofixup = 0;

	ino &= PCI_IRQ_INO;
	if (ino < PSYCHO_ONBOARD_IRQ_BASE) {
		/* PCI slot */
		imap_off = psycho_pcislot_imap_offset(ino);
	} else {
		/* Onboard device */
		if (ino > PSYCHO_ONBOARD_IRQ_LAST) {
			prom_printf("psycho_irq_build: Wacky INO [%x]\n", ino);
			prom_halt();
		}
		imap_off = psycho_onboard_imap_offset(ino);
	}

	/* Now build the IRQ bucket. */
	pil = psycho_ino_to_pil(pdev, ino);
	imap = (volatile unsigned int *)__va(p->controller_regs + imap_off);
	imap += 1;

	iclr_off = psycho_iclr_offset(ino);
	iclr = (volatile unsigned int *)__va(p->controller_regs + iclr_off);
	iclr += 1;

	if ((ino & 0x20) == 0)
		inofixup = ino & 0x03;

	bucket = __bucket(build_irq(pil, inofixup, iclr, imap));
	bucket->flags |= IBF_PCI;

	return __irq(bucket);
}

/* PSYCHO error handling support. */
enum psycho_error_type {
	UE_ERR, CE_ERR, PCI_ERR
};

/* Helper function of IOMMU error checking, which checks out
 * the state of the streaming buffers.  The IOMMU lock is
 * held when this is called.
 *
 * For the PCI error case we know which PBM (and thus which
 * streaming buffer) caused the error, but for the uncorrectable
 * error case we do not.  So we always check both streaming caches.
 */
#define PSYCHO_STRBUF_CONTROL_A 0x2800UL
#define PSYCHO_STRBUF_CONTROL_B 0x4800UL
#define  PSYCHO_STRBUF_CTRL_LPTR    0x00000000000000f0 /* LRU Lock Pointer */
#define  PSYCHO_STRBUF_CTRL_LENAB   0x0000000000000008 /* LRU Lock Enable */
#define  PSYCHO_STRBUF_CTRL_RRDIS   0x0000000000000004 /* Rerun Disable */
#define  PSYCHO_STRBUF_CTRL_DENAB   0x0000000000000002 /* Diagnostic Mode Enable */
#define  PSYCHO_STRBUF_CTRL_ENAB    0x0000000000000001 /* Streaming Buffer Enable */
#define PSYCHO_STRBUF_FLUSH_A   0x2808UL
#define PSYCHO_STRBUF_FLUSH_B   0x4808UL
#define PSYCHO_STRBUF_FSYNC_A   0x2810UL
#define PSYCHO_STRBUF_FSYNC_B   0x4810UL
#define PSYCHO_STC_DATA_A	0xb000UL
#define PSYCHO_STC_DATA_B	0xc000UL
#define PSYCHO_STC_ERR_A	0xb400UL
#define PSYCHO_STC_ERR_B	0xc400UL
#define  PSYCHO_STCERR_WRITE	 0x0000000000000002	/* Write Error */
#define  PSYCHO_STCERR_READ	 0x0000000000000001	/* Read Error */
#define PSYCHO_STC_TAG_A	0xb800UL
#define PSYCHO_STC_TAG_B	0xc800UL
#define  PSYCHO_STCTAG_PPN	 0x0fffffff00000000	/* Physical Page Number */
#define  PSYCHO_STCTAG_VPN	 0x00000000ffffe000	/* Virtual Page Number */
#define  PSYCHO_STCTAG_VALID	 0x0000000000000002	/* Valid */
#define  PSYCHO_STCTAG_WRITE	 0x0000000000000001	/* Writable */
#define PSYCHO_STC_LINE_A	0xb900UL
#define PSYCHO_STC_LINE_B	0xc900UL
#define  PSYCHO_STCLINE_LINDX	 0x0000000001e00000	/* LRU Index */
#define  PSYCHO_STCLINE_SPTR	 0x00000000001f8000	/* Dirty Data Start Pointer */
#define  PSYCHO_STCLINE_LADDR	 0x0000000000007f00	/* Line Address */
#define  PSYCHO_STCLINE_EPTR	 0x00000000000000fc	/* Dirty Data End Pointer */
#define  PSYCHO_STCLINE_VALID	 0x0000000000000002	/* Valid */
#define  PSYCHO_STCLINE_FOFN	 0x0000000000000001	/* Fetch Outstanding / Flush Necessary */

static spinlock_t stc_buf_lock = SPIN_LOCK_UNLOCKED;
static unsigned long stc_error_buf[128];
static unsigned long stc_tag_buf[16];
static unsigned long stc_line_buf[16];

static void __psycho_check_one_stc(struct pci_controller_info *p,
				   struct pci_pbm_info *pbm,
				   int is_pbm_a)
{
	struct pci_strbuf *strbuf = &pbm->stc;
	unsigned long regbase = p->controller_regs;
	unsigned long err_base, tag_base, line_base;
	u64 control;
	int i;

	if (is_pbm_a) {
		err_base = regbase + PSYCHO_STC_ERR_A;
		tag_base = regbase + PSYCHO_STC_TAG_A;
		line_base = regbase + PSYCHO_STC_LINE_A;
	} else {
		err_base = regbase + PSYCHO_STC_ERR_A;
		tag_base = regbase + PSYCHO_STC_TAG_A;
		line_base = regbase + PSYCHO_STC_LINE_A;
	}

	spin_lock(&stc_buf_lock);

	/* This is __REALLY__ dangerous.  When we put the
	 * streaming buffer into diagnostic mode to probe
	 * it's tags and error status, we _must_ clear all
	 * of the line tag valid bits before re-enabling
	 * the streaming buffer.  If any dirty data lives
	 * in the STC when we do this, we will end up
	 * invalidating it before it has a chance to reach
	 * main memory.
	 */
	control = psycho_read(strbuf->strbuf_control);
	psycho_write(strbuf->strbuf_control,
		     (control | PSYCHO_STRBUF_CTRL_DENAB));
	for (i = 0; i < 128; i++) {
		unsigned long val;

		val = psycho_read(err_base + (i * 8UL));
		psycho_write(err_base + (i * 8UL), 0UL);
		stc_error_buf[i] = val;
	}
	for (i = 0; i < 16; i++) {
		stc_tag_buf[i] = psycho_read(tag_base + (i * 8UL));
		stc_line_buf[i] = psycho_read(line_base + (i * 8UL));
		psycho_write(tag_base + (i * 8UL), 0UL);
		psycho_write(line_base + (i * 8UL), 0UL);
	}

	/* OK, state is logged, exit diagnostic mode. */
	psycho_write(strbuf->strbuf_control, control);

	for (i = 0; i < 16; i++) {
		int j, saw_error, first, last;

		saw_error = 0;
		first = i * 8;
		last = first + 8;
		for (j = first; j < last; j++) {
			unsigned long errval = stc_error_buf[j];
			if (errval != 0) {
				saw_error++;
				printk("PSYCHO%d(PBM%c): STC_ERR(%d)[wr(%d)rd(%d)]\n",
				       p->index,
				       (is_pbm_a ? 'A' : 'B'),
				       j,
				       (errval & PSYCHO_STCERR_WRITE) ? 1 : 0,
				       (errval & PSYCHO_STCERR_READ) ? 1 : 0);
			}
		}
		if (saw_error != 0) {
			unsigned long tagval = stc_tag_buf[i];
			unsigned long lineval = stc_line_buf[i];
			printk("PSYCHO%d(PBM%c): STC_TAG(%d)[PA(%016lx)VA(%08lx)V(%d)W(%d)]\n",
			       p->index,
			       (is_pbm_a ? 'A' : 'B'),
			       i,
			       ((tagval & PSYCHO_STCTAG_PPN) >> 19UL),
			       (tagval & PSYCHO_STCTAG_VPN),
			       ((tagval & PSYCHO_STCTAG_VALID) ? 1 : 0),
			       ((tagval & PSYCHO_STCTAG_WRITE) ? 1 : 0));
			printk("PSYCHO%d(PBM%c): STC_LINE(%d)[LIDX(%lx)SP(%lx)LADDR(%lx)EP(%lx)"
			       "V(%d)FOFN(%d)]\n",
			       p->index,
			       (is_pbm_a ? 'A' : 'B'),
			       i,
			       ((lineval & PSYCHO_STCLINE_LINDX) >> 21UL),
			       ((lineval & PSYCHO_STCLINE_SPTR) >> 15UL),
			       ((lineval & PSYCHO_STCLINE_LADDR) >> 8UL),
			       ((lineval & PSYCHO_STCLINE_EPTR) >> 2UL),
			       ((lineval & PSYCHO_STCLINE_VALID) ? 1 : 0),
			       ((lineval & PSYCHO_STCLINE_FOFN) ? 1 : 0));
		}
	}

	spin_unlock(&stc_buf_lock);
}

static void __psycho_check_stc_error(struct pci_controller_info *p,
				     unsigned long afsr,
				     unsigned long afar,
				     enum psycho_error_type type)
{
	struct pci_pbm_info *pbm;

	pbm = &p->pbm_A;
	if (pbm->stc.strbuf_enabled)
		__psycho_check_one_stc(p, pbm, 1);

	pbm = &p->pbm_B;
	if (pbm->stc.strbuf_enabled)
		__psycho_check_one_stc(p, pbm, 0);
}

/* When an Uncorrectable Error or a PCI Error happens, we
 * interrogate the IOMMU state to see if it is the cause.
 */
#define PSYCHO_IOMMU_CONTROL	0x0200UL
#define  PSYCHO_IOMMU_CTRL_RESV     0xfffffffff9000000 /* Reserved                      */
#define  PSYCHO_IOMMU_CTRL_XLTESTAT 0x0000000006000000 /* Translation Error Status      */
#define  PSYCHO_IOMMU_CTRL_XLTEERR  0x0000000001000000 /* Translation Error encountered */
#define  PSYCHO_IOMMU_CTRL_LCKEN    0x0000000000800000 /* Enable translation locking    */
#define  PSYCHO_IOMMU_CTRL_LCKPTR   0x0000000000780000 /* Translation lock pointer      */
#define  PSYCHO_IOMMU_CTRL_TSBSZ    0x0000000000070000 /* TSB Size                      */
#define  PSYCHO_IOMMU_TSBSZ_1K      0x0000000000000000 /* TSB Table 1024 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_2K      0x0000000000010000 /* TSB Table 2048 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_4K      0x0000000000020000 /* TSB Table 4096 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_8K      0x0000000000030000 /* TSB Table 8192 8-byte entries */
#define  PSYCHO_IOMMU_TSBSZ_16K     0x0000000000040000 /* TSB Table 16k 8-byte entries  */
#define  PSYCHO_IOMMU_TSBSZ_32K     0x0000000000050000 /* TSB Table 32k 8-byte entries  */
#define  PSYCHO_IOMMU_TSBSZ_64K     0x0000000000060000 /* TSB Table 64k 8-byte entries  */
#define  PSYCHO_IOMMU_TSBSZ_128K    0x0000000000070000 /* TSB Table 128k 8-byte entries */
#define  PSYCHO_IOMMU_CTRL_RESV2    0x000000000000fff8 /* Reserved                      */
#define  PSYCHO_IOMMU_CTRL_TBWSZ    0x0000000000000004 /* Assumed page size, 0=8k 1=64k */
#define  PSYCHO_IOMMU_CTRL_DENAB    0x0000000000000002 /* Diagnostic mode enable        */
#define  PSYCHO_IOMMU_CTRL_ENAB     0x0000000000000001 /* IOMMU Enable                  */
#define PSYCHO_IOMMU_TSBBASE	0x0208UL
#define PSYCHO_IOMMU_FLUSH	0x0210UL
#define PSYCHO_IOMMU_TAG	0xa580UL
#define  PSYCHO_IOMMU_TAG_ERRSTS (0x3UL << 23UL)
#define  PSYCHO_IOMMU_TAG_ERR	 (0x1UL << 22UL)
#define  PSYCHO_IOMMU_TAG_WRITE	 (0x1UL << 21UL)
#define  PSYCHO_IOMMU_TAG_STREAM (0x1UL << 20UL)
#define  PSYCHO_IOMMU_TAG_SIZE	 (0x1UL << 19UL)
#define  PSYCHO_IOMMU_TAG_VPAGE	 0x7ffffUL
#define PSYCHO_IOMMU_DATA	0xa600UL
#define  PSYCHO_IOMMU_DATA_VALID (1UL << 30UL)
#define  PSYCHO_IOMMU_DATA_CACHE (1UL << 28UL)
#define  PSYCHO_IOMMU_DATA_PPAGE 0xfffffffUL
static void psycho_check_iommu_error(struct pci_controller_info *p,
				     unsigned long afsr,
				     unsigned long afar,
				     enum psycho_error_type type)
{
	unsigned long iommu_tag[16];
	unsigned long iommu_data[16];
	unsigned long flags;
	u64 control;
	int i;

	spin_lock_irqsave(&p->iommu.lock, flags);
	control = psycho_read(p->iommu.iommu_control);
	if (control & PSYCHO_IOMMU_CTRL_XLTEERR) {
		char *type_string;

		/* Clear the error encountered bit. */
		control &= ~PSYCHO_IOMMU_CTRL_XLTEERR;
		psycho_write(p->iommu.iommu_control, control);

		switch((control & PSYCHO_IOMMU_CTRL_XLTESTAT) >> 25UL) {
		case 0:
			type_string = "Protection Error";
			break;
		case 1:
			type_string = "Invalid Error";
			break;
		case 2:
			type_string = "TimeOut Error";
			break;
		case 3:
		default:
			type_string = "ECC Error";
			break;
		};
		printk("PSYCHO%d: IOMMU Error, type[%s]\n",
		       p->index, type_string);

		/* Put the IOMMU into diagnostic mode and probe
		 * it's TLB for entries with error status.
		 *
		 * It is very possible for another DVMA to occur
		 * while we do this probe, and corrupt the system
		 * further.  But we are so screwed at this point
		 * that we are likely to crash hard anyways, so
		 * get as much diagnostic information to the
		 * console as we can.
		 */
		psycho_write(p->iommu.iommu_control,
			     control | PSYCHO_IOMMU_CTRL_DENAB);
		for (i = 0; i < 16; i++) {
			unsigned long base = p->controller_regs;

			iommu_tag[i] =
				psycho_read(base + PSYCHO_IOMMU_TAG + (i * 8UL));
			iommu_data[i] =
				psycho_read(base + PSYCHO_IOMMU_DATA + (i * 8UL));

			/* Now clear out the entry. */
			psycho_write(base + PSYCHO_IOMMU_TAG + (i * 8UL), 0);
			psycho_write(base + PSYCHO_IOMMU_DATA + (i * 8UL), 0);
		}

		/* Leave diagnostic mode. */
		psycho_write(p->iommu.iommu_control, control);

		for (i = 0; i < 16; i++) {
			unsigned long tag, data;

			tag = iommu_tag[i];
			if (!(tag & PSYCHO_IOMMU_TAG_ERR))
				continue;

			data = iommu_data[i];
			switch((tag & PSYCHO_IOMMU_TAG_ERRSTS) >> 23UL) {
			case 0:
				type_string = "Protection Error";
				break;
			case 1:
				type_string = "Invalid Error";
				break;
			case 2:
				type_string = "TimeOut Error";
				break;
			case 3:
			default:
				type_string = "ECC Error";
				break;
			};
			printk("PSYCHO%d: IOMMU TAG(%d)[error(%s) wr(%d) str(%d) sz(%dK) vpg(%08lx)]\n",
			       p->index, i, type_string,
			       ((tag & PSYCHO_IOMMU_TAG_WRITE) ? 1 : 0),
			       ((tag & PSYCHO_IOMMU_TAG_STREAM) ? 1 : 0),
			       ((tag & PSYCHO_IOMMU_TAG_SIZE) ? 64 : 8),
			       (tag & PSYCHO_IOMMU_TAG_VPAGE) << PAGE_SHIFT);
			printk("PSYCHO%d: IOMMU DATA(%d)[valid(%d) cache(%d) ppg(%016lx)]\n",
			       p->index, i,
			       ((data & PSYCHO_IOMMU_DATA_VALID) ? 1 : 0),
			       ((data & PSYCHO_IOMMU_DATA_CACHE) ? 1 : 0),
			       (data & PSYCHO_IOMMU_DATA_PPAGE) << PAGE_SHIFT);
		}
	}
	__psycho_check_stc_error(p, afsr, afar, type);
	spin_unlock_irqrestore(&p->iommu.lock, flags);
}

/* Uncorrectable Errors.  Cause of the error and the address are
 * recorded in the UE_AFSR and UE_AFAR of PSYCHO.  They are errors
 * relating to UPA interface transactions.
 */
#define PSYCHO_UE_AFSR	0x0030UL
#define  PSYCHO_UEAFSR_PPIO	0x8000000000000000 /* Primary PIO is cause         */
#define  PSYCHO_UEAFSR_PDRD	0x4000000000000000 /* Primary DVMA read is cause   */
#define  PSYCHO_UEAFSR_PDWR	0x2000000000000000 /* Primary DVMA write is cause  */
#define  PSYCHO_UEAFSR_SPIO	0x1000000000000000 /* Secondary PIO is cause       */
#define  PSYCHO_UEAFSR_SDRD	0x0800000000000000 /* Secondary DVMA read is cause */
#define  PSYCHO_UEAFSR_SDWR	0x0400000000000000 /* Secondary DVMA write is cause*/
#define  PSYCHO_UEAFSR_RESV1	0x03ff000000000000 /* Reserved                     */
#define  PSYCHO_UEAFSR_BMSK	0x0000ffff00000000 /* Bytemask of failed transfer  */
#define  PSYCHO_UEAFSR_DOFF	0x00000000e0000000 /* Doubleword Offset            */
#define  PSYCHO_UEAFSR_MID	0x000000001f000000 /* UPA MID causing the fault    */
#define  PSYCHO_UEAFSR_BLK	0x0000000000800000 /* Trans was block operation    */
#define  PSYCHO_UEAFSR_RESV2	0x00000000007fffff /* Reserved                     */
#define PSYCHO_UE_AFAR	0x0038UL

static void psycho_ue_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->controller_regs + PSYCHO_UE_AFSR;
	unsigned long afar_reg = p->controller_regs + PSYCHO_UE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch uncorrectable error status. */
	afar = psycho_read(afar_reg);
	afsr = psycho_read(afsr_reg);

	/* Clear the primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_UEAFSR_PPIO | PSYCHO_UEAFSR_PDRD | PSYCHO_UEAFSR_PDWR |
		 PSYCHO_UEAFSR_SPIO | PSYCHO_UEAFSR_SDRD | PSYCHO_UEAFSR_SDWR);
	psycho_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PSYCHO%d: Uncorrectable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & PSYCHO_UEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & PSYCHO_UEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & PSYCHO_UEAFSR_PDWR) ?
		   "DMA Write" : "???")))));
	printk("PSYCHO%d: bytemask[%04lx] dword_offset[%lx] UPA_MID[%02lx] was_block(%d)\n",
	       p->index,
	       (afsr & PSYCHO_UEAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_UEAFSR_DOFF) >> 29UL,
	       (afsr & PSYCHO_UEAFSR_MID) >> 24UL,
	       ((afsr & PSYCHO_UEAFSR_BLK) ? 1 : 0));
	printk("PSYCHO%d: UE AFAR [%016lx]\n", p->index, afar);
	printk("PSYCHO%d: UE Secondary errors [", p->index);
	reported = 0;
	if (afsr & PSYCHO_UEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & PSYCHO_UEAFSR_SDRD) {
		reported++;
		printk("(DMA Read)");
	}
	if (afsr & PSYCHO_UEAFSR_SDWR) {
		reported++;
		printk("(DMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* Interrogate IOMMU for error status. */
	psycho_check_iommu_error(p, afsr, afar, UE_ERR);
}

/* Correctable Errors. */
#define PSYCHO_CE_AFSR	0x0040UL
#define  PSYCHO_CEAFSR_PPIO	0x8000000000000000 /* Primary PIO is cause         */
#define  PSYCHO_CEAFSR_PDRD	0x4000000000000000 /* Primary DVMA read is cause   */
#define  PSYCHO_CEAFSR_PDWR	0x2000000000000000 /* Primary DVMA write is cause  */
#define  PSYCHO_CEAFSR_SPIO	0x1000000000000000 /* Secondary PIO is cause       */
#define  PSYCHO_CEAFSR_SDRD	0x0800000000000000 /* Secondary DVMA read is cause */
#define  PSYCHO_CEAFSR_SDWR	0x0400000000000000 /* Secondary DVMA write is cause*/
#define  PSYCHO_CEAFSR_RESV1	0x0300000000000000 /* Reserved                     */
#define  PSYCHO_CEAFSR_ESYND	0x00ff000000000000 /* Syndrome Bits                */
#define  PSYCHO_CEAFSR_BMSK	0x0000ffff00000000 /* Bytemask of failed transfer  */
#define  PSYCHO_CEAFSR_DOFF	0x00000000e0000000 /* Double Offset                */
#define  PSYCHO_CEAFSR_MID	0x000000001f000000 /* UPA MID causing the fault    */
#define  PSYCHO_CEAFSR_BLK	0x0000000000800000 /* Trans was block operation    */
#define  PSYCHO_CEAFSR_RESV2	0x00000000007fffff /* Reserved                     */
#define PSYCHO_CE_AFAR	0x0040UL

static void psycho_ce_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_controller_info *p = dev_id;
	unsigned long afsr_reg = p->controller_regs + PSYCHO_CE_AFSR;
	unsigned long afar_reg = p->controller_regs + PSYCHO_CE_AFAR;
	unsigned long afsr, afar, error_bits;
	int reported;

	/* Latch error status. */
	afar = psycho_read(afar_reg);
	afsr = psycho_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_CEAFSR_PPIO | PSYCHO_CEAFSR_PDRD | PSYCHO_CEAFSR_PDWR |
		 PSYCHO_CEAFSR_SPIO | PSYCHO_CEAFSR_SDRD | PSYCHO_CEAFSR_SDWR);
	psycho_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PSYCHO%d: Correctable Error, primary error type[%s]\n",
	       p->index,
	       (((error_bits & PSYCHO_CEAFSR_PPIO) ?
		 "PIO" :
		 ((error_bits & PSYCHO_CEAFSR_PDRD) ?
		  "DMA Read" :
		  ((error_bits & PSYCHO_CEAFSR_PDWR) ?
		   "DMA Write" : "???")))));
	printk("PSYCHO%d: syndrome[%02lx] bytemask[%04lx] dword_offset[%lx] "
	       "UPA_MID[%02lx] was_block(%d)\n",
	       p->index,
	       (afsr & PSYCHO_CEAFSR_ESYND) >> 48UL,
	       (afsr & PSYCHO_CEAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_CEAFSR_DOFF) >> 29UL,
	       (afsr & PSYCHO_CEAFSR_MID) >> 24UL,
	       ((afsr & PSYCHO_CEAFSR_BLK) ? 1 : 0));
	printk("PSYCHO%d: CE AFAR [%016lx]\n", p->index, afar);
	printk("PSYCHO%d: CE Secondary errors [", p->index);
	reported = 0;
	if (afsr & PSYCHO_CEAFSR_SPIO) {
		reported++;
		printk("(PIO)");
	}
	if (afsr & PSYCHO_CEAFSR_SDRD) {
		reported++;
		printk("(DMA Read)");
	}
	if (afsr & PSYCHO_CEAFSR_SDWR) {
		reported++;
		printk("(DMA Write)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");
}

/* PCI Errors.  They are signalled by the PCI bus module since they
 * are assosciated with a specific bus segment.
 */
#define PSYCHO_PCI_AFSR_A	0x2010UL
#define PSYCHO_PCI_AFSR_B	0x4010UL
#define  PSYCHO_PCIAFSR_PMA	0x8000000000000000 /* Primary Master Abort Error   */
#define  PSYCHO_PCIAFSR_PTA	0x4000000000000000 /* Primary Target Abort Error   */
#define  PSYCHO_PCIAFSR_PRTRY	0x2000000000000000 /* Primary Excessive Retries    */
#define  PSYCHO_PCIAFSR_PPERR	0x1000000000000000 /* Primary Parity Error         */
#define  PSYCHO_PCIAFSR_SMA	0x0800000000000000 /* Secondary Master Abort Error */
#define  PSYCHO_PCIAFSR_STA	0x0400000000000000 /* Secondary Target Abort Error */
#define  PSYCHO_PCIAFSR_SRTRY	0x0200000000000000 /* Secondary Excessive Retries  */
#define  PSYCHO_PCIAFSR_SPERR	0x0100000000000000 /* Secondary Parity Error       */
#define  PSYCHO_PCIAFSR_RESV1	0x00ff000000000000 /* Reserved                     */
#define  PSYCHO_PCIAFSR_BMSK	0x0000ffff00000000 /* Bytemask of failed transfer  */
#define  PSYCHO_PCIAFSR_BLK	0x0000000080000000 /* Trans was block operation    */
#define  PSYCHO_PCIAFSR_RESV2	0x0000000040000000 /* Reserved                     */
#define  PSYCHO_PCIAFSR_MID	0x000000003e000000 /* MID causing the error        */
#define  PSYCHO_PCIAFSR_RESV3	0x0000000001ffffff /* Reserved                     */
#define PSYCHO_PCI_AFAR_A	0x2018UL
#define PSYCHO_PCI_AFAR_B	0x4018UL

static void psycho_pcierr_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct pci_pbm_info *pbm = dev_id;
	struct pci_controller_info *p = pbm->parent;
	unsigned long afsr_reg, afar_reg;
	unsigned long afsr, afar, error_bits;
	int is_pbm_a, reported;

	is_pbm_a = (pbm == &pbm->parent->pbm_A);
	if (is_pbm_a) {
		afsr_reg = p->controller_regs + PSYCHO_PCI_AFSR_A;
		afar_reg = p->controller_regs + PSYCHO_PCI_AFAR_A;
	} else {
		afsr_reg = p->controller_regs + PSYCHO_PCI_AFSR_B;
		afar_reg = p->controller_regs + PSYCHO_PCI_AFAR_B;
	}

	/* Latch error status. */
	afar = psycho_read(afar_reg);
	afsr = psycho_read(afsr_reg);

	/* Clear primary/secondary error status bits. */
	error_bits = afsr &
		(PSYCHO_PCIAFSR_PMA | PSYCHO_PCIAFSR_PTA |
		 PSYCHO_PCIAFSR_PRTRY | PSYCHO_PCIAFSR_PPERR |
		 PSYCHO_PCIAFSR_SMA | PSYCHO_PCIAFSR_STA |
		 PSYCHO_PCIAFSR_SRTRY | PSYCHO_PCIAFSR_SPERR);
	psycho_write(afsr_reg, error_bits);

	/* Log the error. */
	printk("PSYCHO%d(PBM%c): PCI Error, primary error type[%s]\n",
	       p->index, (is_pbm_a ? 'A' : 'B'),
	       (((error_bits & PSYCHO_PCIAFSR_PMA) ?
		 "Master Abort" :
		 ((error_bits & PSYCHO_PCIAFSR_PTA) ?
		  "Target Abort" :
		  ((error_bits & PSYCHO_PCIAFSR_PRTRY) ?
		   "Excessive Retries" :
		   ((error_bits & PSYCHO_PCIAFSR_PPERR) ?
		    "Parity Error" : "???"))))));
	printk("PSYCHO%d(PBM%c): bytemask[%04lx] UPA_MID[%02lx] was_block(%d)\n",
	       p->index, (is_pbm_a ? 'A' : 'B'),
	       (afsr & PSYCHO_PCIAFSR_BMSK) >> 32UL,
	       (afsr & PSYCHO_PCIAFSR_MID) >> 25UL,
	       (afsr & PSYCHO_PCIAFSR_BLK) ? 1 : 0);
	printk("PSYCHO%d(PBM%c): PCI AFAR [%016lx]\n",
	       p->index, (is_pbm_a ? 'A' : 'B'), afar);
	printk("PSYCHO%d(PBM%c): PCI Secondary errors [",
	       p->index, (is_pbm_a ? 'A' : 'B'));
	reported = 0;
	if (afsr & PSYCHO_PCIAFSR_SMA) {
		reported++;
		printk("(Master Abort)");
	}
	if (afsr & PSYCHO_PCIAFSR_STA) {
		reported++;
		printk("(Target Abort)");
	}
	if (afsr & PSYCHO_PCIAFSR_SRTRY) {
		reported++;
		printk("(Excessive Retries)");
	}
	if (afsr & PSYCHO_PCIAFSR_SPERR) {
		reported++;
		printk("(Parity Error)");
	}
	if (!reported)
		printk("(none)");
	printk("]\n");

	/* For the error types shown, scan PBM's PCI bus for devices
	 * which have logged that error type.
	 */

	/* If we see a Target Abort, this could be the result of an
	 * IOMMU translation error of some sort.  It is extremely
	 * useful to log this information as usually it indicates
	 * a bug in the IOMMU support code or a PCI device driver.
	 */
	if (error_bits & (PSYCHO_PCIAFSR_PTA | PSYCHO_PCIAFSR_STA)) {
		psycho_check_iommu_error(p, afsr, afar, PCI_ERR);
		pci_scan_for_target_abort(p, pbm, pbm->pci_bus);
	}
	if (error_bits & (PSYCHO_PCIAFSR_PMA | PSYCHO_PCIAFSR_SMA))
		pci_scan_for_master_abort(p, pbm, pbm->pci_bus);

	/* For excessive retries, PSYCHO/PBM will abort the device
	 * and there is no way to specifically check for excessive
	 * retries in the config space status registers.  So what
	 * we hope is that we'll catch it via the master/target
	 * abort events.
	 */

	if (error_bits & (PSYCHO_PCIAFSR_PPERR | PSYCHO_PCIAFSR_SPERR))
		pci_scan_for_parity_error(p, pbm, pbm->pci_bus);
}

/* XXX What about PowerFail/PowerManagement??? -DaveM */
#define PSYCHO_ECC_CTRL		0x0020
#define  PSYCHO_ECCCTRL_EE	 0x8000000000000000 /* Enable ECC Checking */
#define  PSYCHO_ECCCTRL_UE	 0x4000000000000000 /* Enable UE Interrupts */
#define  PSYCHO_ECCCTRL_CE	 0x2000000000000000 /* Enable CE INterrupts */
#define PSYCHO_UE_INO		0x2e
#define PSYCHO_CE_INO		0x2f
#define PSYCHO_PCIERR_A_INO	0x30
#define PSYCHO_PCIERR_B_INO	0x31
static void __init psycho_register_error_handlers(struct pci_controller_info *p)
{
	unsigned long base = p->controller_regs;
	unsigned int irq, portid = p->portid;
	u64 tmp;

	/* Build IRQs and register handlers. */
	irq = psycho_irq_build(p, NULL, (portid << 6) | PSYCHO_UE_INO);
	if (request_irq(irq, psycho_ue_intr,
			SA_SHIRQ, "PSYCHO UE", p) < 0) {
		prom_printf("PSYCHO%d: Cannot register UE interrupt.\n",
			    p->index);
		prom_halt();
	}

	irq = psycho_irq_build(p, NULL, (portid << 6) | PSYCHO_CE_INO);
	if (request_irq(irq, psycho_ce_intr,
			SA_SHIRQ, "PSYCHO CE", p) < 0) {
		prom_printf("PSYCHO%d: Cannot register CE interrupt.\n",
			    p->index);
		prom_halt();
	}

	irq = psycho_irq_build(p, NULL, (portid << 6) | PSYCHO_PCIERR_A_INO);
	if (request_irq(irq, psycho_pcierr_intr,
			SA_SHIRQ, "PSYCHO PCIERR", &p->pbm_A) < 0) {
		prom_printf("PSYCHO%d(PBMA): Cannot register PciERR interrupt.\n",
			    p->index);
		prom_halt();
	}

	irq = psycho_irq_build(p, NULL, (portid << 6) | PSYCHO_PCIERR_B_INO);
	if (request_irq(irq, psycho_pcierr_intr,
			SA_SHIRQ, "PSYCHO PCIERR", &p->pbm_B) < 0) {
		prom_printf("PSYCHO%d(PBMB): Cannot register PciERR interrupt.\n",
			    p->index);
		prom_halt();
	}

	/* Enable UE and CE interrupts for controller. */
	psycho_write(base + PSYCHO_ECC_CTRL,
		     (PSYCHO_ECCCTRL_EE |
		      PSYCHO_ECCCTRL_UE |
		      PSYCHO_ECCCTRL_CE));

	/* Enable PCI Error interrupts and clear error
	 * bits for each PBM.
	 */
	tmp = psycho_read(base + PSYCHO_PCIA_CTRL);
	tmp |= (PSYCHO_PCICTRL_SBH_ERR |
		PSYCHO_PCICTRL_SERR |
		PSYCHO_PCICTRL_SBH_INT |
		PSYCHO_PCICTRL_EEN);
	psycho_write(base + PSYCHO_PCIA_CTRL, tmp);
		     
	tmp = psycho_read(base + PSYCHO_PCIB_CTRL);
	tmp |= (PSYCHO_PCICTRL_SBH_ERR |
		PSYCHO_PCICTRL_SERR |
		PSYCHO_PCICTRL_SBH_INT |
		PSYCHO_PCICTRL_EEN);
	psycho_write(base + PSYCHO_PCIB_CTRL, tmp);
}

/* PSYCHO boot time probing and initialization. */
static void __init psycho_resource_adjust(struct pci_dev *pdev,
					  struct resource *res,
					  struct resource *root)
{
	res->start += root->start;
	res->end += root->start;
}

static void __init psycho_base_address_update(struct pci_dev *pdev, int resource)
{
	struct pcidev_cookie *pcp = pdev->sysdata;
	struct pci_pbm_info *pbm = pcp->pbm;
	struct resource *res = &pdev->resource[resource];
	struct resource *root;
	u32 reg;
	int where, size;

	if (res->flags & IORESOURCE_IO)
		root = &pbm->io_space;
	else
		root = &pbm->mem_space;

	where = PCI_BASE_ADDRESS_0 + (resource * 4);
	size = res->end - res->start;
	pci_read_config_dword(pdev, where, &reg);
	reg = ((reg & size) |
	       (((u32)(res->start - root->start)) & ~size));
	pci_write_config_dword(pdev, where, reg);
}

/* We have to do the config space accesses by hand, thus... */
#define PBM_BRIDGE_BUS		0x40
#define PBM_BRIDGE_SUBORDINATE	0x41
static void __init pbm_renumber(struct pci_pbm_info *pbm, u8 orig_busno)
{
	u8 *addr, busno;
	int nbus;

	busno = pci_highest_busnum;
	nbus = pbm->pci_last_busno - pbm->pci_first_busno;

	addr = psycho_pci_config_mkaddr(pbm, orig_busno,
					0, PBM_BRIDGE_BUS);
	pci_config_write8(addr, busno);
	addr = psycho_pci_config_mkaddr(pbm, busno,
					0, PBM_BRIDGE_SUBORDINATE);
	pci_config_write8(addr, busno + nbus);

	pbm->pci_first_busno = busno;
	pbm->pci_last_busno = busno + nbus;
	pci_highest_busnum = busno + nbus + 1;

	do {
		pci_bus2pbm[busno++] = pbm;
	} while (nbus--);
}

/* We have to do the config space accesses by hand here since
 * the pci_bus2pbm array is not ready yet.
 */
static void __init pbm_pci_bridge_renumber(struct pci_pbm_info *pbm,
					   u8 busno)
{
	u32 devfn, l, class;
	u8 hdr_type;
	int is_multi = 0;

	for(devfn = 0; devfn < 0xff; ++devfn) {
		u32 *dwaddr;
		u8 *baddr;

		if (PCI_FUNC(devfn) != 0 && is_multi == 0)
			continue;

		/* Anything there? */
		dwaddr = psycho_pci_config_mkaddr(pbm, busno, devfn, PCI_VENDOR_ID);
		l = 0xffffffff;
		pci_config_read32(dwaddr, &l);
		if (l == 0xffffffff || l == 0x00000000 ||
		    l == 0x0000ffff || l == 0xffff0000) {
			is_multi = 0;
			continue;
		}

		baddr = psycho_pci_config_mkaddr(pbm, busno, devfn, PCI_HEADER_TYPE);
		pci_config_read8(baddr, &hdr_type);
		if (PCI_FUNC(devfn) == 0)
			is_multi = hdr_type & 0x80;

		dwaddr = psycho_pci_config_mkaddr(pbm, busno, devfn, PCI_CLASS_REVISION);
		class = 0xffffffff;
		pci_config_read32(dwaddr, &class);
		if ((class >> 16) == PCI_CLASS_BRIDGE_PCI) {
			u32 buses = 0xffffffff;

			dwaddr = psycho_pci_config_mkaddr(pbm, busno, devfn,
							  PCI_PRIMARY_BUS);
			pci_config_read32(dwaddr, &buses);
			pbm_pci_bridge_renumber(pbm, (buses >> 8) & 0xff);
			buses &= 0xff000000;
			pci_config_write32(dwaddr, buses);
		}
	}
}

static void __init pbm_bridge_reconfigure(struct pci_controller_info *p)
{
	struct pci_pbm_info *pbm;
	u8 *addr;

	/* Clear out primary/secondary/subordinate bus numbers on
	 * all PCI-to-PCI bridges under each PBM.  The generic bus
	 * probing will fix them up.
	 */
	pbm_pci_bridge_renumber(&p->pbm_B, p->pbm_B.pci_first_busno);
	pbm_pci_bridge_renumber(&p->pbm_A, p->pbm_A.pci_first_busno);

	/* Move PBM A out of the way. */
	pbm = &p->pbm_A;
	addr = psycho_pci_config_mkaddr(pbm, pbm->pci_first_busno,
					0, PBM_BRIDGE_BUS);
	pci_config_write8(addr, 0xff);
	addr = psycho_pci_config_mkaddr(pbm, 0xff,
					0, PBM_BRIDGE_SUBORDINATE);
	pci_config_write8(addr, 0xff);

	/* Now we can safely renumber both PBMs. */
	pbm_renumber(&p->pbm_B, p->pbm_B.pci_first_busno);
	pbm_renumber(&p->pbm_A, 0xff);
}

static void __init pbm_scan_bus(struct pci_controller_info *p,
				struct pci_pbm_info *pbm)
{
	pbm->pci_bus = pci_scan_bus(pbm->pci_first_busno,
				    p->pci_ops,
				    pbm);
	pci_fill_in_pbm_cookies(pbm->pci_bus, pbm, pbm->prom_node);
	pci_record_assignments(pbm, pbm->pci_bus);
	pci_assign_unassigned(pbm, pbm->pci_bus);
	pci_fixup_irq(pbm, pbm->pci_bus);
}

static void __init psycho_scan_bus(struct pci_controller_info *p)
{
	pbm_bridge_reconfigure(p);
	pbm_scan_bus(p, &p->pbm_B);
	pbm_scan_bus(p, &p->pbm_A);

	/* After the PCI bus scan is complete, we can register
	 * the error interrupt handlers.
	 */
	psycho_register_error_handlers(p);
}

static void __init psycho_iommu_init(struct pci_controller_info *p, int tsbsize)
{
	extern int this_is_starfire;
	extern void *starfire_hookup(int);
	struct linux_mlist_p1275 *mlist;
	unsigned long tsbbase, i, n, order;
	iopte_t *iopte;
	u64 control;

	/* Setup initial software IOMMU state. */
	spin_lock_init(&p->iommu.lock);
	p->iommu.iommu_cur_ctx = 0;

	/* PSYCHO's IOMMU lacks ctx flushing. */
	p->iommu.iommu_has_ctx_flush = 0;

	/* Register addresses. */
	p->iommu.iommu_control  = p->controller_regs + PSYCHO_IOMMU_CONTROL;
	p->iommu.iommu_tsbbase  = p->controller_regs + PSYCHO_IOMMU_TSBBASE;
	p->iommu.iommu_flush    = p->controller_regs + PSYCHO_IOMMU_FLUSH;
	p->iommu.iommu_ctxflush = 0;

	/* We use the main control register of PSYCHO as the write
	 * completion register.
	 */
	p->iommu.write_complete_reg = p->controller_regs + PSYCHO_CONTROL;

	/*
	 * Invalidate TLB Entries.
	 */
	control = psycho_read(p->controller_regs + PSYCHO_IOMMU_CONTROL);
	control |= PSYCHO_IOMMU_CTRL_DENAB;
	psycho_write(p->controller_regs + PSYCHO_IOMMU_CONTROL, control);
	for(i = 0; i < 16; i++)
		psycho_write(p->controller_regs + PSYCHO_IOMMU_DATA + (i * 8UL), 0);

	control &= ~(PSYCHO_IOMMU_CTRL_DENAB);
	psycho_write(p->controller_regs + PSYCHO_IOMMU_CONTROL, control);

	for(order = 0;; order++)
		if((PAGE_SIZE << order) >= ((tsbsize * 1024) * 8))
			break;

	tsbbase = __get_free_pages(GFP_DMA, order);
	if (!tsbbase) {
		prom_printf("PSYCHO_IOMMU: Error, gfp(tsb) failed.\n");
		prom_halt();
	}
	p->iommu.page_table = iopte = (iopte_t *)tsbbase;
	p->iommu.page_table_sz = (tsbsize * 1024);

	/* Initialize to "none" settings. */
	for(i = 0; i < PCI_DVMA_HASHSZ; i++) {
		pci_dvma_v2p_hash[i] = PCI_DVMA_HASH_NONE;
		pci_dvma_p2v_hash[i] = PCI_DVMA_HASH_NONE;
	}

	n = 0;
	mlist = *prom_meminfo()->p1275_totphys;
	while (mlist) {
		unsigned long paddr = mlist->start_adr;
		unsigned long num_bytes = mlist->num_bytes;

		if(paddr >= (((unsigned long) high_memory) - PAGE_OFFSET))
			goto next;

		if((paddr + num_bytes) >= (((unsigned long) high_memory) - PAGE_OFFSET))
			num_bytes = (((unsigned long) high_memory) - PAGE_OFFSET) - paddr;

		/* Align base and length so we map whole hash table sized chunks
		 * at a time (and therefore full 64K IOMMU pages).
		 */
		paddr &= ~((1UL << 24UL) - 1);
		num_bytes = (num_bytes + ((1UL << 24UL) - 1)) & ~((1UL << 24) - 1);

		/* Move up the base for mappings already created. */
		while(pci_dvma_v2p_hash[pci_dvma_ahashfn(paddr)] !=
		      PCI_DVMA_HASH_NONE) {
			paddr += (1UL << 24UL);
			num_bytes -= (1UL << 24UL);
			if(num_bytes == 0UL)
				goto next;
		}

		/* Move down the size for tail mappings already created. */
		while(pci_dvma_v2p_hash[pci_dvma_ahashfn(paddr + num_bytes - (1UL << 24UL))] !=
		      PCI_DVMA_HASH_NONE) {
			num_bytes -= (1UL << 24UL);
			if(num_bytes == 0UL)
				goto next;
		}

		/* Now map the rest. */
		for (i = 0; i < ((num_bytes + ((1 << 16) - 1)) >> 16); i++) {
			iopte_val(*iopte) = ((IOPTE_VALID | IOPTE_64K |
					      IOPTE_CACHE | IOPTE_WRITE) |
					     (paddr & IOPTE_PAGE));

			if (!(n & 0xff))
				set_dvma_hash(0x80000000, paddr, (n << 16));

			if (++n > (tsbsize * 1024))
				goto out;

			paddr += (1 << 16);
			iopte++;
		}
	next:
		mlist = mlist->theres_more;
	}
out:
	if (mlist) {
		prom_printf("WARNING: not all physical memory mapped in IOMMU\n");
		prom_printf("Try booting with mem=xxxM or similar\n");
		prom_halt();
	}

	psycho_write(p->controller_regs + PSYCHO_IOMMU_TSBBASE, __pa(tsbbase));

	control = psycho_read(p->controller_regs + PSYCHO_IOMMU_CONTROL);
	control &= ~(PSYCHO_IOMMU_CTRL_TSBSZ);
	control |= (PSYCHO_IOMMU_CTRL_TBWSZ | PSYCHO_IOMMU_CTRL_ENAB);
	switch(tsbsize) {
	case 8:
		p->iommu.page_table_map_base = 0xe0000000;
		control |= PSYCHO_IOMMU_TSBSZ_8K;
		break;
	case 16:
		p->iommu.page_table_map_base = 0xc0000000;
		control |= PSYCHO_IOMMU_TSBSZ_16K;
		break;
	case 32:
		p->iommu.page_table_map_base = 0x80000000;
		control |= PSYCHO_IOMMU_TSBSZ_32K;
		break;
	default:
		prom_printf("iommu_init: Illegal TSB size %d\n", tsbsize);
		prom_halt();
		break;
	}
	psycho_write(p->controller_regs + PSYCHO_IOMMU_CONTROL, control);

	/* If necessary, hook us up for starfire IRQ translations. */
	if(this_is_starfire)
		p->starfire_cookie = starfire_hookup(p->portid);
	else
		p->starfire_cookie = NULL;
}

#define PSYCHO_IRQ_RETRY	0x1a00UL
#define PSYCHO_PCIA_DIAG	0x2020UL
#define PSYCHO_PCIB_DIAG	0x4020UL
#define  PSYCHO_PCIDIAG_RESV	 0xffffffffffffff80 /* Reserved                     */
#define  PSYCHO_PCIDIAG_DRETRY	 0x0000000000000040 /* Disable retry limit          */
#define  PSYCHO_PCIDIAG_DISYNC	 0x0000000000000020 /* Disable DMA wr / irq sync    */
#define  PSYCHO_PCIDIAG_DDWSYNC	 0x0000000000000010 /* Disable DMA wr / PIO rd sync */
#define  PSYCHO_PCIDIAG_IDDPAR	 0x0000000000000008 /* Invert DMA data parity       */
#define  PSYCHO_PCIDIAG_IPDPAR	 0x0000000000000004 /* Invert PIO data parity       */
#define  PSYCHO_PCIDIAG_IPAPAR	 0x0000000000000002 /* Invert PIO address parity    */
#define  PSYCHO_PCIDIAG_LPBACK	 0x0000000000000001 /* Enable loopback mode         */

static void psycho_controller_hwinit(struct pci_controller_info *p)
{
	u64 tmp;

	/* PROM sets the IRQ retry value too low, increase it. */
	psycho_write(p->controller_regs + PSYCHO_IRQ_RETRY, 0xff);

	/* Enable arbiter for all PCI slots. */
	tmp = psycho_read(p->controller_regs + PSYCHO_PCIA_CTRL);
	tmp |= PSYCHO_PCICTRL_AEN;
	psycho_write(p->controller_regs + PSYCHO_PCIA_CTRL, tmp);

	tmp = psycho_read(p->controller_regs + PSYCHO_PCIB_CTRL);
	tmp |= PSYCHO_PCICTRL_AEN;
	psycho_write(p->controller_regs + PSYCHO_PCIB_CTRL, tmp);

	/* Disable DMA write / PIO read synchronization on
	 * both PCI bus segments.
	 * [ U2P Erratum 1243770, STP2223BGA data sheet ]
	 */
	tmp = psycho_read(p->controller_regs + PSYCHO_PCIA_DIAG);
	tmp |= PSYCHO_PCIDIAG_DDWSYNC;
	psycho_write(p->controller_regs + PSYCHO_PCIA_DIAG, tmp);

	tmp = psycho_read(p->controller_regs + PSYCHO_PCIB_DIAG);
	tmp |= PSYCHO_PCIDIAG_DDWSYNC;
	psycho_write(p->controller_regs + PSYCHO_PCIB_DIAG, tmp);
}

static void __init pbm_register_toplevel_resources(struct pci_controller_info *p,
						   struct pci_pbm_info *pbm)
{
	char *name = pbm->name;

	sprintf(name, "PSYCHO%d PBM%c",
		p->index,
		(pbm == &p->pbm_A ? 'A' : 'B'));
	pbm->io_space.name = pbm->mem_space.name = name;

	request_resource(&ioport_resource, &pbm->io_space);
	request_resource(&iomem_resource, &pbm->mem_space);
}

static void psycho_pbm_strbuf_init(struct pci_controller_info *p,
				   struct pci_pbm_info *pbm,
				   int is_pbm_a)
{
	unsigned long base = p->controller_regs;

	/* Currently we don't even use it. */
	pbm->stc.strbuf_enabled = 0;

	/* PSYCHO's streaming buffer lacks ctx flushing. */
	pbm->stc.strbuf_has_ctx_flush = 0;

	if (is_pbm_a) {
		pbm->stc.strbuf_control  = base + PSYCHO_STRBUF_CONTROL_A;
		pbm->stc.strbuf_pflush   = base + PSYCHO_STRBUF_FLUSH_A;
		pbm->stc.strbuf_fsync    = base + PSYCHO_STRBUF_FSYNC_A;
	} else {
		pbm->stc.strbuf_control  = base + PSYCHO_STRBUF_CONTROL_B;
		pbm->stc.strbuf_pflush   = base + PSYCHO_STRBUF_FLUSH_B;
		pbm->stc.strbuf_fsync    = base + PSYCHO_STRBUF_FSYNC_B;
	}
	pbm->stc.strbuf_ctxflush      = 0;
	pbm->stc.strbuf_ctxmatch_base = 0;

	pbm->stc.strbuf_flushflag = (volatile unsigned long *)
		((((unsigned long)&pbm->stc.__flushflag_buf[0])
		  + 63UL)
		 & ~63UL);
	pbm->stc.strbuf_flushflag_pa = (unsigned long)
		__pa(pbm->stc.strbuf_flushflag);

#if 0
	/* And when we do enable it, these are the sorts of things
	 * we'll do.
	 */
	control = psycho_read(pbm->stc.strbuf_control);
	control |= PSYCHO_SBUFCTRL_SB_EN;
	psycho_write(pbm->stc.strbuf_control, control);
#endif
}

#define PSYCHO_IOSPACE_A	0x002000000UL
#define PSYCHO_IOSPACE_B	0x002010000UL
#define PSYCHO_IOSPACE_SIZE	0x00000ffffUL
#define PSYCHO_MEMSPACE_A	0x100000000UL
#define PSYCHO_MEMSPACE_B	0x180000000UL
#define PSYCHO_MEMSPACE_SIZE	0x07fffffffUL

static void psycho_pbm_init(struct pci_controller_info *p,
			    int prom_node, int is_pbm_a)
{
	unsigned int busrange[2];
	struct pci_pbm_info *pbm;
	int err;

	if (is_pbm_a) {
		pbm = &p->pbm_A;
		pbm->io_space.start = p->controller_regs + PSYCHO_IOSPACE_A;
		pbm->mem_space.start = p->controller_regs + PSYCHO_MEMSPACE_A;
	} else {
		pbm = &p->pbm_B;
		pbm->io_space.start = p->controller_regs + PSYCHO_IOSPACE_B;
		pbm->mem_space.start = p->controller_regs + PSYCHO_MEMSPACE_B;
	}
	pbm->io_space.end = pbm->io_space.start + PSYCHO_IOSPACE_SIZE;
	pbm->io_space.flags = IORESOURCE_IO;
	pbm->mem_space.end = pbm->mem_space.start + PSYCHO_MEMSPACE_SIZE;
	pbm->mem_space.flags = IORESOURCE_MEM;
	pbm_register_toplevel_resources(p, pbm);

	pbm->parent = p;
	pbm->prom_node = prom_node;
	prom_getstring(prom_node, "name",
		       pbm->prom_name,
		       sizeof(pbm->prom_name));

	err = prom_getproperty(prom_node, "ranges",
			       (char *)pbm->pbm_ranges,
			       sizeof(pbm->pbm_ranges));
	if (err != -1)
		pbm->num_pbm_ranges =
			(err / sizeof(struct linux_prom_pci_ranges));
	else
		pbm->num_pbm_ranges = 0;

	err = prom_getproperty(prom_node, "interrupt-map",
			       (char *)pbm->pbm_intmap,
			       sizeof(pbm->pbm_intmap));
	if (err != -1) {
		pbm->num_pbm_intmap = (err / sizeof(struct linux_prom_pci_intmap));
		err = prom_getproperty(prom_node, "interrupt-map-mask",
				       (char *)&pbm->pbm_intmask,
				       sizeof(pbm->pbm_intmask));
		if (err == -1) {
			prom_printf("PSYCHO-PBM: Fatal error, no "
				    "interrupt-map-mask.\n");
			prom_halt();
		}
	} else {
		pbm->num_pbm_intmap = 0;
		memset(&pbm->pbm_intmask, 0, sizeof(pbm->pbm_intmask));
	}

	err = prom_getproperty(prom_node, "bus-range",
			       (char *)&busrange[0],
			       sizeof(busrange));
	if (err == 0 || err == -1) {
		prom_printf("PSYCHO-PBM: Fatal error, no bus-range.\n");
		prom_halt();
	}
	pbm->pci_first_busno = busrange[0];
	pbm->pci_last_busno = busrange[1];

	psycho_pbm_strbuf_init(p, pbm, is_pbm_a);
}

#define PSYCHO_CONFIGSPACE	0x001000000UL

void __init psycho_init(int node)
{
	struct linux_prom64_registers pr_regs[3];
	struct pci_controller_info *p;
	unsigned long flags;
	u32 upa_portid;
	int is_pbm_a, err;

	upa_portid = prom_getintdefault(node, "upa-portid", 0xff);

	spin_lock_irqsave(&pci_controller_lock, flags);
	for(p = pci_controller_root; p; p = p->next) {
		if (p->portid == upa_portid) {
			spin_unlock_irqrestore(&pci_controller_lock, flags);
			is_pbm_a = (p->pbm_A.prom_node == 0);
			psycho_pbm_init(p, node, is_pbm_a);
			return;
		}
	}
	spin_unlock_irqrestore(&pci_controller_lock, flags);

	p = kmalloc(sizeof(struct pci_controller_info), GFP_ATOMIC);
	if (!p) {
		prom_printf("PSYCHO: Fatal memory allocation error.\n");
		prom_halt();
	}
	memset(p, 0, sizeof(*p));

	spin_lock_irqsave(&pci_controller_lock, flags);
	p->next = pci_controller_root;
	pci_controller_root = p;
	spin_unlock_irqrestore(&pci_controller_lock, flags);

	p->portid = upa_portid;
	p->index = pci_num_controllers++;
	p->scan_bus = psycho_scan_bus;
	p->irq_build = psycho_irq_build;
	p->base_address_update = psycho_base_address_update;
	p->resource_adjust = psycho_resource_adjust;
	p->pci_ops = &psycho_ops;

	err = prom_getproperty(node, "reg",
			       (char *)&pr_regs[0],
			       sizeof(pr_regs));
	if (err == 0 || err == -1) {
		prom_printf("PSYCHO: Fatal error, no reg property.\n");
		prom_halt();
	}

	p->controller_regs = pr_regs[2].phys_addr;
	printk("PCI: Found PSYCHO, control regs at %016lx\n",
	       p->controller_regs);

	p->config_space = pr_regs[2].phys_addr + PSYCHO_CONFIGSPACE;
	printk("PSYCHO: PCI config space at %016lx\n", p->config_space);

	/*
	 * Psycho's PCI MEM space is mapped to a 2GB aligned area, so
	 * we need to adjust our MEM space mask.
	 */
	pci_memspace_mask = 0x7fffffffUL;

	psycho_controller_hwinit(p);

	psycho_iommu_init(p, 32);

	is_pbm_a = ((pr_regs[0].phys_addr & 0x6000) == 0x2000);
	psycho_pbm_init(p, node, is_pbm_a);
}
