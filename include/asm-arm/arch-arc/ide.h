/*
 * linux/include/asm-arm/arch-arc/ide.h
 *
 * Copyright (c) 1997,1998 Russell King
 *
 * IDE definitions for the Acorn Archimedes/A5000
 * architecture
 *
 * Modifications:
 *  04-04-1998	PJB	Merged `arc' and `a5k' versions
 *  01-07-1998	RMK	Added new ide_ioregspec_t
 *  29-07-1998	RMK	Major re-work of IDE architecture specific code
 */

#include <linux/config.h>
#include <asm/irq.h>

/*
 * Set up a hw structure for a specified data port, control port and IRQ.
 * This should follow whatever the default interface uses.
 */
static __inline__ void
ide_init_hwif_ports(hw_regs_t *hw, int data_port, int ctrl_port, int irq)
{
	ide_ioreg_t reg = (ide_ioreg_t) data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}
	hw->io_ports[IDE_CONTROL_OFFSET] = (ide_ioreg_t) ctrl_port;
	hw->irq = irq;
}

/*
 * This registers the standard ports for this architecture with the IDE
 * driver.
 */
static __inline__ void ide_init_default_hwifs(void)
{
#ifdef CONFIG_ARCH_A5K
	hw_regs_t hw;

        memset(&hw, 0, sizeof(hw));

	ide_init_hwif_ports(&hw, 0x1f0, 0x3f6, IRQ_HARDDISK);
	ide_register_hw(&hw, NULL);
#endif
}
