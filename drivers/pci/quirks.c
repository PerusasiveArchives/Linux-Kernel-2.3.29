/*
 * $Id: quirks.c,v 1.5 1998/05/02 19:24:14 mj Exp $
 *
 *  This file contains work-arounds for many known PCI hardware
 *  bugs.  Devices present only on certain architectures (host
 *  bridges et cetera) should be handled in arch-specific code.
 *
 *  Copyright (c) 1999 Martin Mares <mj@ucw.cz>
 *
 *  The bridge optimization stuff has been removed. If you really
 *  have a silly BIOS which is unable to set your host bridge right,
 *  use the PowerTweak utility (see http://linux.powertweak.com/).
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#undef DEBUG

/* Deal with broken BIOS'es that neglect to enable passive release,
   which can cause problems in combination with the 82441FX/PPro MTRRs */
static void __init quirk_passive_release(struct pci_dev *dev)
{
	struct pci_dev *d = NULL;
	unsigned char dlc;

	/* We have to make sure a particular bit is set in the PIIX3
	   ISA bridge, so we have to go out and find it. */
	while ((d = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0, d))) {
		pci_read_config_byte(d, 0x82, &dlc);
		if (!(dlc & 1<<1)) {
			printk("PCI: PIIX3: Enabling Passive Release on %s\n", d->slot_name);
			dlc |= 1<<1;
			pci_write_config_byte(d, 0x82, dlc);
		}
	}
}

/*  The VIA VP2/VP3/MVP3 seem to have some 'features'. There may be a workaround
    but VIA don't answer queries. If you happen to have good contacts at VIA
    ask them for me please -- Alan 
    
    This appears to be BIOS not version dependent. So presumably there is a 
    chipset level fix */
    

int isa_dma_bridge_buggy = 0;		/* Exported */
    
static void __init quirk_isa_dma_hangs(struct pci_dev *dev)
{
	if (!isa_dma_bridge_buggy) {
		isa_dma_bridge_buggy=1;
		printk(KERN_INFO "Activating ISA DMA hang workarounds.\n");
	}
}

int pci_pci_problems = 0;

/*
 *	Chipsets where PCI->PCI transfers vanish or hang
 */

static void __init quirk_nopcipci(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_FAIL)==0)
	{
		printk(KERN_INFO "Disabling direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_FAIL;
	}
}

/*
 *	Triton requires workarounds to be used by the drivers
 */
 
static void __init quirk_triton(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_TRITON)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_TRITON;
	}
}

/*
 *	Natoma has some interesting boundary conditions with Zoran stuff
 *	at least
 */
 
static void __init quirk_natoma(struct pci_dev *dev)
{
	if((pci_pci_problems&PCIPCI_NATOMA)==0)
	{
		printk(KERN_INFO "Limiting direct PCI/PCI transfers.\n");
		pci_pci_problems|=PCIPCI_NATOMA;
	}
}

/*
 *  S3 868 and 968 chips report region size equal to 32M, but they decode 64M.
 *  If it's needed, re-allocate the region.
 */

static void __init quirk_s3_64M(struct pci_dev *dev)
{
	struct resource *r = &dev->resource[0];

	if ((r->start & 0x3ffffff) || r->end != r->start + 0x3ffffff) {
		r->start = 0;
		r->end = 0x3ffffff;
	}
}


/*
 *  The main table of quirks.
 */

static struct pci_fixup pci_fixups[] __initdata = {
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82441,	quirk_passive_release },
	/*
	 * Its not totally clear which chipsets are the problematic ones
	 * We know 82C586 and 82C596 variants are affected.
	 *
	 */
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C586_0,	quirk_isa_dma_hangs },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_VIA,	PCI_DEVICE_ID_VIA_82C596,	quirk_isa_dma_hangs },
	{ PCI_FIXUP_FINAL,      PCI_VENDOR_ID_INTEL,    PCI_DEVICE_ID_INTEL_82371SB_0,  quirk_isa_dma_hangs },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_868,		quirk_s3_64M },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_S3,	PCI_DEVICE_ID_S3_968,		quirk_s3_64M },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82437VX, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82439TX, 	quirk_triton }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82441, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_0, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443LX_1, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_0, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_1, 	quirk_natoma }, 
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_INTEL, 	PCI_DEVICE_ID_INTEL_82443BX_2, 	quirk_natoma },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_5597,		quirk_nopcipci },
	{ PCI_FIXUP_FINAL,	PCI_VENDOR_ID_SI,	PCI_DEVICE_ID_SI_496,		quirk_nopcipci },
	{ 0 }
};


static void pci_do_fixups(struct pci_dev *dev, int pass, struct pci_fixup *f)
{
	while (f->pass) {
		if (f->pass == pass &&
 		    (f->vendor == dev->vendor || f->vendor == (u16) PCI_ANY_ID) &&
 		    (f->device == dev->device || f->device == (u16) PCI_ANY_ID)) {
#ifdef DEBUG
			printk("PCI: Calling quirk %p for %s\n", f->hook, dev->slot_name);
#endif
			f->hook(dev);
		}
		f++;
	}
}

void pci_fixup_device(int pass, struct pci_dev *dev)
{
	pci_do_fixups(dev, pass, pcibios_fixups);
	pci_do_fixups(dev, pass, pci_fixups);
}
