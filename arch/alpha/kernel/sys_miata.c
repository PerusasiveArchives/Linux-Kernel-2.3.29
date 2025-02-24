/*
 *	linux/arch/alpha/kernel/sys_miata.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code supporting the MIATA (EV56+PYXIS).
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_pyxis.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void 
miata_update_irq_hw(unsigned long irq, unsigned long mask, int unmask_p)
{
	if (irq >= 16) {
		/* Make CERTAIN none of the bogus ints get enabled... */
		*(vulp)PYXIS_INT_MASK =
			~((long)mask >> 16) & ~0x400000000000063bUL;
		mb();
		/* ... and read it back to make sure it got written.  */
		*(vulp)PYXIS_INT_MASK;
	}
	else if (irq >= 8)
		outb(mask >> 8, 0xA1);	/* ISA PIC2 */
	else
		outb(mask, 0x21);	/* ISA PIC1 */
}

static void 
miata_device_interrupt(unsigned long vector, struct pt_regs *regs)
{
	unsigned long pld, tmp;
	unsigned int i;

	/* Read the interrupt summary register of PYXIS */
	pld = *(vulp)PYXIS_INT_REQ;

	/*
	 * For now, AND off any bits we are not interested in:
	 *   HALT (2), timer (6), ISA Bridge (7), 21142/3 (8)
	 * then all the PCI slots/INTXs (12-31).
	 */
	/* Maybe HALT should only be used for SRM console boots? */
	pld &= 0x00000000fffff9c4UL;

	/*
	 * Now for every possible bit set, work through them and call
	 * the appropriate interrupt handler.
	 */
	while (pld) {
		i = ffz(~pld);
		pld &= pld - 1; /* clear least bit set */
		if (i == 7) {
			isa_device_interrupt(vector, regs);
		} else if (i == 6) {
			continue;
		} else {
			/* if not timer int */
			handle_irq(16 + i, 16 + i, regs);
		}
		*(vulp)PYXIS_INT_REQ = 1UL << i; mb();
		tmp = *(vulp)PYXIS_INT_REQ;
	}
}

static void 
miata_srm_device_interrupt(unsigned long vector, struct pt_regs * regs)
{
	int irq, ack;

	ack = irq = (vector - 0x800) >> 4;

	/*
	 * I really hate to do this, but the MIATA SRM console ignores the
	 *  low 8 bits in the interrupt summary register, and reports the
	 *  vector 0x80 *lower* than I expected from the bit numbering in
	 *  the documentation.
	 * This was done because the low 8 summary bits really aren't used
	 *  for reporting any interrupts (the PCI-ISA bridge, bit 7, isn't
	 *  used for this purpose, as PIC interrupts are delivered as the
	 *  vectors 0x800-0x8f0).
	 * But I really don't want to change the fixup code for allocation
	 *  of IRQs, nor the alpha_irq_mask maintenance stuff, both of which
	 *  look nice and clean now.
	 * So, here's this grotty hack... :-(
	 */
	if (irq >= 16)
		ack = irq = irq + 8;

	handle_irq(irq, ack, regs);
}

static void __init
miata_init_irq(void)
{
	STANDARD_INIT_IRQ_PROLOG;

	if (alpha_using_srm)
		alpha_mv.device_interrupt = miata_srm_device_interrupt;

	/* Note invert on MASK bits.  */
	*(vulp)PYXIS_INT_MASK =
	  ~((long)alpha_irq_mask >> 16) & ~0x400000000000063bUL; mb();
#if 0
	/* These break on MiataGL so we'll try not to do it at all.  */
	*(vulp)PYXIS_INT_HILO = 0x000000B2UL; mb();	/* ISA/NMI HI */
	*(vulp)PYXIS_RT_COUNT = 0UL; mb();		/* clear count */
#endif
	/* Clear upper timer.  */
	*(vulp)PYXIS_INT_REQ  = 0x4000000000000180UL; mb();

	enable_irq(16 + 2);	/* enable HALT switch - SRM only? */
	enable_irq(16 + 6);     /* enable timer */
	enable_irq(16 + 7);     /* enable ISA PIC cascade */
	enable_irq(2);		/* enable cascade */
}


/*
 * PCI Fixup configuration.
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        Fan Fault
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        none
 * 4        CID0 (Riser ID)
 * 5        CID1 (Riser ID)
 * 6        Interval timer
 * 7        PCI-ISA Bridge
 * 8        Ethernet
 * 9        EIDE (deprecated, ISA 14/15 used)
 *10        none
 *11        USB
 *12        Interrupt Line A from slot 4
 *13        Interrupt Line B from slot 4
 *14        Interrupt Line C from slot 4
 *15        Interrupt Line D from slot 4
 *16        Interrupt Line A from slot 5
 *17        Interrupt line B from slot 5
 *18        Interrupt Line C from slot 5
 *19        Interrupt Line D from slot 5
 *20        Interrupt Line A from slot 1
 *21        Interrupt Line B from slot 1
 *22        Interrupt Line C from slot 1
 *23        Interrupt Line D from slot 1
 *24        Interrupt Line A from slot 2
 *25        Interrupt Line B from slot 2
 *26        Interrupt Line C from slot 2
 *27        Interrupt Line D from slot 2
 *27        Interrupt Line A from slot 3
 *29        Interrupt Line B from slot 3
 *30        Interrupt Line C from slot 3
 *31        Interrupt Line D from slot 3
 *
 * The device to slot mapping looks like:
 *
 * Slot     Device
 *  3       DC21142 Ethernet
 *  4       EIDE CMD646
 *  5       none
 *  6       USB
 *  7       PCI-ISA bridge
 *  8       PCI-PCI Bridge      (SBU Riser)
 *  9       none
 * 10       none
 * 11       PCI on board slot 4 (SBU Riser)
 * 12       PCI on board slot 5 (SBU Riser)
 *
 *  These are behind the bridge, so I'm not sure what to do...
 *
 * 13       PCI on board slot 1 (SBU Riser)
 * 14       PCI on board slot 2 (SBU Riser)
 * 15       PCI on board slot 3 (SBU Riser)
 *   
 *
 * This two layered interrupt approach means that we allocate IRQ 16 and 
 * above for PCI interrupts.  The IRQ relates to which bit the interrupt
 * comes in on.  This makes interrupt processing much easier.
 */

static int __init
miata_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
        static char irq_tab[18][5] __initlocaldata = {
		/*INT    INTA   INTB   INTC   INTD */
		{16+ 8, 16+ 8, 16+ 8, 16+ 8, 16+ 8},  /* IdSel 14,  DC21142 */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 15,  EIDE    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 16,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 17,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 18,  PCI-ISA */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 19,  PCI-PCI */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 20,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 21,  none    */
		{16+12, 16+12, 16+13, 16+14, 16+15},  /* IdSel 22,  slot 4  */
		{16+16, 16+16, 16+17, 16+18, 16+19},  /* IdSel 23,  slot 5  */
		/* the next 7 are actually on PCI bus 1, across the bridge */
		{16+11, 16+11, 16+11, 16+11, 16+11},  /* IdSel 24,  QLISP/GL*/
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 25,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 26,  none    */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 27,  none    */
		{16+20, 16+20, 16+21, 16+22, 16+23},  /* IdSel 28,  slot 1  */
		{16+24, 16+24, 16+25, 16+26, 16+27},  /* IdSel 29,  slot 2  */
		{16+28, 16+28, 16+29, 16+30, 16+31},  /* IdSel 30,  slot 3  */
		/* This bridge is on the main bus of the later orig MIATA */
		{   -1,    -1,    -1,    -1,    -1},  /* IdSel 31,  PCI-PCI */
        };
	const long min_idsel = 3, max_idsel = 20, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static u8 __init
miata_swizzle(struct pci_dev *dev, u8 *pinp)
{
	int slot, pin = *pinp;

	if (dev->bus->number == 0) {
		slot = PCI_SLOT(dev->devfn);
	}		
	/* Check for the built-in bridge.  */
	else if ((PCI_SLOT(dev->bus->self->devfn) == 8) ||
		 (PCI_SLOT(dev->bus->self->devfn) == 20)) {
		slot = PCI_SLOT(dev->devfn) + 9;
	}
	else 
	{
		/* Must be a card-based bridge.  */
		do {
			if ((PCI_SLOT(dev->bus->self->devfn) == 8) ||
			    (PCI_SLOT(dev->bus->self->devfn) == 20)) {
				slot = PCI_SLOT(dev->devfn) + 9;
				break;
			}
			pin = bridge_swizzle(pin, PCI_SLOT(dev->devfn));

			/* Move up the chain of bridges.  */
			dev = dev->bus->self;
			/* Slot of the next bridge.  */
			slot = PCI_SLOT(dev->devfn);
		} while (dev->bus->self);
	}
	*pinp = pin;
	return slot;
}

static void __init
miata_init_pci(void)
{
	common_init_pci();
	SMC669_Init(0); /* it might be a GL (fails harmlessly if not) */
	es1888_init();
}

static void
miata_kill_arch (int mode, char *reboot_cmd)
{
	/* Who said DEC engineers have no sense of humor? ;-)  */
	if (alpha_using_srm) {
		*(vuip) PYXIS_RESET = 0x0000dead;
		mb();
	}
	common_kill_arch(mode, reboot_cmd);
}


/*
 * The System Vector
 */

struct alpha_machine_vector miata_mv __initmv = {
	vector_name:		"Miata",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	DO_PYXIS_BUS,
	machine_check:		pyxis_machine_check,
	max_dma_address:	ALPHA_MAX_DMA_ADDRESS,
	min_io_address:		DEFAULT_IO_BASE,
	min_mem_address:	DEFAULT_MEM_BASE,

	nr_irqs:		48,
	irq_probe_mask:		_PROBE_MASK(48),
	update_irq_hw:		miata_update_irq_hw,
	ack_irq:		common_ack_irq,
	device_interrupt:	miata_device_interrupt,

	init_arch:		pyxis_init_arch,
	init_irq:		miata_init_irq,
	init_pit:		common_init_pit,
	init_pci:		miata_init_pci,
	kill_arch:		miata_kill_arch,
	pci_map_irq:		miata_map_irq,
	pci_swizzle:		miata_swizzle,
};
ALIAS_MV(miata)
