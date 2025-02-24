/*
 *	Low-Level PCI Support for PC
 *
 *	(c) 1999 Martin Mares <mj@ucw.cz>
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

#include <asm/segment.h>
#include <asm/io.h>
#include <asm/smp.h>

#include "pci-i386.h"

unsigned int pci_probe = PCI_PROBE_BIOS | PCI_PROBE_CONF1 | PCI_PROBE_CONF2;

/*
 * IRQ routing table provided by the BIOS
 */

struct irq_info {
	u8 bus, devfn;			/* Bus, device and function */
	struct {
		u8 link;		/* IRQ line ID, chipset dependent, 0=not routed */
		u16 bitmap;		/* Available IRQs */
	} __attribute__((packed)) irq[4];
	u8 slot;			/* Slot number, 0=onboard */
	u8 rfu;
} __attribute__((packed));

struct irq_routing_table {
	u32 signature;			/* PIRQ_SIGNATURE should be here */
	u16 version;			/* PIRQ_VERSION */
	u16 size;			/* Table size in bytes */
	u8 rtr_bus, rtr_devfn;		/* Where the interrupt router lies */
	u16 exclusive_irqs;		/* IRQs devoted exclusively to PCI usage */
	u16 rtr_vendor, rtr_device;	/* Vendor and device ID of interrupt router */
	u32 miniport_data;		/* Crap */
	u8 rfu[11];
	u8 checksum;			/* Modulo 256 checksum must give zero */
	struct irq_info slots[0];
} __attribute__((packed));

/*
 * Direct access to PCI hardware...
 */

#ifdef CONFIG_PCI_DIRECT

/*
 * Functions for accessing PCI configuration space with type 1 accesses
 */

#define CONFIG_CMD(dev, where)   (0x80000000 | (dev->bus->number << 16) | (dev->devfn << 8) | (where & ~3))

static int pci_conf1_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	outl(CONFIG_CMD(dev,where), 0xCF8);
	*value = inb(0xCFC + (where&3));
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	outl(CONFIG_CMD(dev,where), 0xCF8);    
	*value = inw(0xCFC + (where&2));
	return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	outl(CONFIG_CMD(dev,where), 0xCF8);
	*value = inl(0xCFC);
	return PCIBIOS_SUCCESSFUL;    
}

static int pci_conf1_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	outl(CONFIG_CMD(dev,where), 0xCF8);    
	outb(value, 0xCFC + (where&3));
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	outl(CONFIG_CMD(dev,where), 0xCF8);
	outw(value, 0xCFC + (where&2));
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf1_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	outl(CONFIG_CMD(dev,where), 0xCF8);
	outl(value, 0xCFC);
	return PCIBIOS_SUCCESSFUL;
}

#undef CONFIG_CMD

static struct pci_ops pci_direct_conf1 = {
	pci_conf1_read_config_byte,
	pci_conf1_read_config_word,
	pci_conf1_read_config_dword,
	pci_conf1_write_config_byte,
	pci_conf1_write_config_word,
	pci_conf1_write_config_dword
};

/*
 * Functions for accessing PCI configuration space with type 2 accesses
 */

#define IOADDR(devfn, where)	((0xC000 | ((devfn & 0x78) << 5)) + where)
#define FUNC(devfn)		(((devfn & 7) << 1) | 0xf0)
#define SET(dev)		if (dev->devfn & 0x80) return PCIBIOS_DEVICE_NOT_FOUND;		\
				outb(FUNC(dev->devfn), 0xCF8);					\
				outb(dev->bus->number, 0xCFA);

static int pci_conf2_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	SET(dev);
	*value = inb(IOADDR(dev->devfn,where));
	outb (0, 0xCF8);
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	SET(dev);
	*value = inw(IOADDR(dev->devfn,where));
	outb (0, 0xCF8);
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	SET(dev);
	*value = inl (IOADDR(dev->devfn,where));    
	outb (0, 0xCF8);    
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	SET(dev);
	outb (value, IOADDR(dev->devfn,where));
	outb (0, 0xCF8);    
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	SET(dev);
	outw (value, IOADDR(dev->devfn,where));
	outb (0, 0xCF8);    
	return PCIBIOS_SUCCESSFUL;
}

static int pci_conf2_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	SET(dev);
	outl (value, IOADDR(dev->devfn,where));    
	outb (0, 0xCF8);    
	return PCIBIOS_SUCCESSFUL;
}

#undef SET
#undef IOADDR
#undef FUNC

static struct pci_ops pci_direct_conf2 = {
	pci_conf2_read_config_byte,
	pci_conf2_read_config_word,
	pci_conf2_read_config_dword,
	pci_conf2_write_config_byte,
	pci_conf2_write_config_word,
	pci_conf2_write_config_dword
};

/*
 * Before we decide to use direct hardware access mechanisms, we try to do some
 * trivial checks to ensure it at least _seems_ to be working -- we just test
 * whether bus 00 contains a host bridge (this is similar to checking
 * techniques used in XFree86, but ours should be more reliable since we
 * attempt to make use of direct access hints provided by the PCI BIOS).
 *
 * This should be close to trivial, but it isn't, because there are buggy
 * chipsets (yes, you guessed it, by Intel and Compaq) that have no class ID.
 */
static int __init pci_sanity_check(struct pci_ops *o)
{
	u16 x;
	struct pci_bus bus;		/* Fake bus and device */
	struct pci_dev dev;

	if (pci_probe & PCI_NO_CHECKS)
		return 1;
	bus.number = 0;
	dev.bus = &bus;
	for(dev.devfn=0; dev.devfn < 0x100; dev.devfn++)
		if ((!o->read_word(&dev, PCI_CLASS_DEVICE, &x) &&
		     (x == PCI_CLASS_BRIDGE_HOST || x == PCI_CLASS_DISPLAY_VGA)) ||
		    (!o->read_word(&dev, PCI_VENDOR_ID, &x) &&
		     (x == PCI_VENDOR_ID_INTEL || x == PCI_VENDOR_ID_COMPAQ)))
			return 1;
	DBG("PCI: Sanity check failed\n");
	return 0;
}

static struct pci_ops * __init pci_check_direct(void)
{
	unsigned int tmp;
	unsigned long flags;

	__save_flags(flags); __cli();

	/*
	 * Check if configuration type 1 works.
	 */
	if (pci_probe & PCI_PROBE_CONF1) {
		outb (0x01, 0xCFB);
		tmp = inl (0xCF8);
		outl (0x80000000, 0xCF8);
		if (inl (0xCF8) == 0x80000000 &&
		    pci_sanity_check(&pci_direct_conf1)) {
			outl (tmp, 0xCF8);
			__restore_flags(flags);
			printk("PCI: Using configuration type 1\n");
			return &pci_direct_conf1;
		}
		outl (tmp, 0xCF8);
	}

	/*
	 * Check if configuration type 2 works.
	 */
	if (pci_probe & PCI_PROBE_CONF2) {
		outb (0x00, 0xCFB);
		outb (0x00, 0xCF8);
		outb (0x00, 0xCFA);
		if (inb (0xCF8) == 0x00 && inb (0xCFA) == 0x00 &&
		    pci_sanity_check(&pci_direct_conf2)) {
			__restore_flags(flags);
			printk("PCI: Using configuration type 2\n");
			return &pci_direct_conf2;
		}
	}

	__restore_flags(flags);
	return NULL;
}

#endif

/*
 * BIOS32 and PCI BIOS handling.
 */

#ifdef CONFIG_PCI_BIOS

#define PCIBIOS_PCI_FUNCTION_ID 	0xb1XX
#define PCIBIOS_PCI_BIOS_PRESENT 	0xb101
#define PCIBIOS_FIND_PCI_DEVICE		0xb102
#define PCIBIOS_FIND_PCI_CLASS_CODE	0xb103
#define PCIBIOS_GENERATE_SPECIAL_CYCLE	0xb106
#define PCIBIOS_READ_CONFIG_BYTE	0xb108
#define PCIBIOS_READ_CONFIG_WORD	0xb109
#define PCIBIOS_READ_CONFIG_DWORD	0xb10a
#define PCIBIOS_WRITE_CONFIG_BYTE	0xb10b
#define PCIBIOS_WRITE_CONFIG_WORD	0xb10c
#define PCIBIOS_WRITE_CONFIG_DWORD	0xb10d
#define PCIBIOS_GET_ROUTING_OPTIONS	0xb10e
#define PCIBIOS_SET_PCI_HW_INT		0xb10f

/* BIOS32 signature: "_32_" */
#define BIOS32_SIGNATURE	(('_' << 0) + ('3' << 8) + ('2' << 16) + ('_' << 24))

/* PCI signature: "PCI " */
#define PCI_SIGNATURE		(('P' << 0) + ('C' << 8) + ('I' << 16) + (' ' << 24))

/* PCI service signature: "$PCI" */
#define PCI_SERVICE		(('$' << 0) + ('P' << 8) + ('C' << 16) + ('I' << 24))

/* PCI BIOS hardware mechanism flags */
#define PCIBIOS_HW_TYPE1		0x01
#define PCIBIOS_HW_TYPE2		0x02
#define PCIBIOS_HW_TYPE1_SPEC		0x10
#define PCIBIOS_HW_TYPE2_SPEC		0x20

/*
 * This is the standard structure used to identify the entry point
 * to the BIOS32 Service Directory, as documented in
 * 	Standard BIOS 32-bit Service Directory Proposal
 * 	Revision 0.4 May 24, 1993
 * 	Phoenix Technologies Ltd.
 *	Norwood, MA
 * and the PCI BIOS specification.
 */

union bios32 {
	struct {
		unsigned long signature;	/* _32_ */
		unsigned long entry;		/* 32 bit physical address */
		unsigned char revision;		/* Revision level, 0 */
		unsigned char length;		/* Length in paragraphs should be 01 */
		unsigned char checksum;		/* All bytes must add up to zero */
		unsigned char reserved[5]; 	/* Must be zero */
	} fields;
	char chars[16];
};

/*
 * Physical address of the service directory.  I don't know if we're
 * allowed to have more than one of these or not, so just in case
 * we'll make pcibios_present() take a memory start parameter and store
 * the array there.
 */

static struct {
	unsigned long address;
	unsigned short segment;
} bios32_indirect = { 0, __KERNEL_CS };

/*
 * Returns the entry point for the given service, NULL on error
 */

static unsigned long bios32_service(unsigned long service)
{
	unsigned char return_code;	/* %al */
	unsigned long address;		/* %ebx */
	unsigned long length;		/* %ecx */
	unsigned long entry;		/* %edx */
	unsigned long flags;

	__save_flags(flags); __cli();
	__asm__("lcall (%%edi)"
		: "=a" (return_code),
		  "=b" (address),
		  "=c" (length),
		  "=d" (entry)
		: "0" (service),
		  "1" (0),
		  "D" (&bios32_indirect));
	__restore_flags(flags);

	switch (return_code) {
		case 0:
			return address + entry;
		case 0x80:	/* Not present */
			printk("bios32_service(0x%lx): not present\n", service);
			return 0;
		default: /* Shouldn't happen */
			printk("bios32_service(0x%lx): returned 0x%x -- BIOS bug!\n",
				service, return_code);
			return 0;
	}
}

static struct {
	unsigned long address;
	unsigned short segment;
} pci_indirect = { 0, __KERNEL_CS };

static int pci_bios_present;

static int __init check_pcibios(void)
{
	u32 signature, eax, ebx, ecx;
	u8 status, major_ver, minor_ver, hw_mech, last_bus;
	unsigned long flags, pcibios_entry;

	if ((pcibios_entry = bios32_service(PCI_SERVICE))) {
		pci_indirect.address = pcibios_entry + PAGE_OFFSET;

		__save_flags(flags); __cli();
		__asm__(
			"lcall (%%edi)\n\t"
			"jc 1f\n\t"
			"xor %%ah, %%ah\n"
			"1:"
			: "=d" (signature),
			  "=a" (eax),
			  "=b" (ebx),
			  "=c" (ecx)
			: "1" (PCIBIOS_PCI_BIOS_PRESENT),
			  "D" (&pci_indirect)
			: "memory");
		__restore_flags(flags);

		status = (eax >> 8) & 0xff;
		hw_mech = eax & 0xff;
		major_ver = (ebx >> 8) & 0xff;
		minor_ver = ebx & 0xff;
		last_bus = ecx & 0xff;
		DBG("PCI: BIOS probe returned s=%02x hw=%02x ver=%02x.%02x l=%02x\n",
			status, hw_mech, major_ver, minor_ver, last_bus);
		if (status || signature != PCI_SIGNATURE) {
			printk (KERN_ERR "PCI: BIOS BUG #%x[%08x] found, report to <mj@ucw.cz>\n",
				status, signature);
			return 0;
		}
		printk("PCI: PCI BIOS revision %x.%02x entry at 0x%lx\n",
			major_ver, minor_ver, pcibios_entry);
#ifdef CONFIG_PCI_DIRECT
		if (!(hw_mech & PCIBIOS_HW_TYPE1))
			pci_probe &= ~PCI_PROBE_CONF1;
		if (!(hw_mech & PCIBIOS_HW_TYPE2))
			pci_probe &= ~PCI_PROBE_CONF2;
#endif
		return 1;
	}
	return 0;
}

static int __init pci_bios_find_device (unsigned short vendor, unsigned short device_id,
					unsigned short index, unsigned char *bus, unsigned char *device_fn)
{
	unsigned short bx;
	unsigned short ret;

	__asm__("lcall (%%edi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=b" (bx),
		  "=a" (ret)
		: "1" (PCIBIOS_FIND_PCI_DEVICE),
		  "c" (device_id),
		  "d" (vendor),
		  "S" ((int) index),
		  "D" (&pci_indirect));
	*bus = (bx >> 8) & 0xff;
	*device_fn = bx & 0xff;
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_BYTE),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_WORD),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=c" (*value),
		  "=a" (ret)
		: "1" (PCIBIOS_READ_CONFIG_DWORD),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_BYTE),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_WORD),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

static int pci_bios_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	unsigned long ret;
	unsigned long bx = (dev->bus->number << 8) | dev->devfn;

	__asm__("lcall (%%esi)\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret)
		: "0" (PCIBIOS_WRITE_CONFIG_DWORD),
		  "c" (value),
		  "b" (bx),
		  "D" ((long) where),
		  "S" (&pci_indirect));
	return (int) (ret & 0xff00) >> 8;
}

/*
 * Function table for BIOS32 access
 */

static struct pci_ops pci_bios_access = {
      pci_bios_read_config_byte,
      pci_bios_read_config_word,
      pci_bios_read_config_dword,
      pci_bios_write_config_byte,
      pci_bios_write_config_word,
      pci_bios_write_config_dword
};

/*
 * Try to find PCI BIOS.
 */

static struct pci_ops * __init pci_find_bios(void)
{
	union bios32 *check;
	unsigned char sum;
	int i, length;

	/*
	 * Follow the standard procedure for locating the BIOS32 Service
	 * directory by scanning the permissible address range from
	 * 0xe0000 through 0xfffff for a valid BIOS32 structure.
	 */

	for (check = (union bios32 *) __va(0xe0000);
	     check <= (union bios32 *) __va(0xffff0);
	     ++check) {
		if (check->fields.signature != BIOS32_SIGNATURE)
			continue;
		length = check->fields.length * 16;
		if (!length)
			continue;
		sum = 0;
		for (i = 0; i < length ; ++i)
			sum += check->chars[i];
		if (sum != 0)
			continue;
		if (check->fields.revision != 0) {
			printk("PCI: unsupported BIOS32 revision %d at 0x%p, report to <mj@ucw.cz>\n",
				check->fields.revision, check);
			continue;
		}
		DBG("PCI: BIOS32 Service Directory structure at 0x%p\n", check);
		if (check->fields.entry >= 0x100000) {
			printk("PCI: BIOS32 entry (0x%p) in high memory, cannot use.\n", check);
			return NULL;
		} else {
			unsigned long bios32_entry = check->fields.entry;
			DBG("PCI: BIOS32 Service Directory entry at 0x%lx\n", bios32_entry);
			bios32_indirect.address = bios32_entry + PAGE_OFFSET;
			if (check_pcibios())
				return &pci_bios_access;
		}
		break;	/* Hopefully more than one BIOS32 cannot happen... */
	}

	return NULL;
}

/*
 * Sort the device list according to PCI BIOS. Nasty hack, but since some
 * fool forgot to define the `correct' device order in the PCI BIOS specs
 * and we want to be (possibly bug-to-bug ;-]) compatible with older kernels
 * which used BIOS ordering, we are bound to do this...
 */

static void __init pcibios_sort(void)
{
	struct pci_dev *dev = pci_devices;
	struct pci_dev **last = &pci_devices;
	struct pci_dev *d, **dd, *e;
	int idx;
	unsigned char bus, devfn;

	DBG("PCI: Sorting device list...\n");
	while ((e = dev)) {
		idx = 0;
		while (pci_bios_find_device(e->vendor, e->device, idx, &bus, &devfn) == PCIBIOS_SUCCESSFUL) {
			idx++;
			for(dd=&dev; (d = *dd); dd = &d->next) {
				if (d->bus->number == bus && d->devfn == devfn) {
					*dd = d->next;
					*last = d;
					last = &d->next;
					break;
				}
			}
			if (!d) {
				printk("PCI: BIOS reporting unknown device %02x:%02x\n", bus, devfn);
				/*
				 * We must not continue scanning as several buggy BIOSes
				 * return garbage after the last device. Grr.
				 */
				break;
			}
		}
		if (e == dev) {
			printk("PCI: Device %02x:%02x not found by BIOS\n",
				dev->bus->number, dev->devfn);
			d = dev;
			dev = dev->next;
			*last = d;
			last = &d->next;
		}
	}
	*last = NULL;
}

/*
 *  Ask BIOS for IRQ Routing Table
 */

struct irq_routing_options {
	u16 size;
	struct irq_info *table;
	u16 segment;
} __attribute__((packed));

static unsigned long pcibios_irq_page __initdata = 0;

static inline void __init pcibios_free_irq_routing_table(void)
{
	if (pcibios_irq_page)
		free_page(pcibios_irq_page);
}

static struct irq_routing_table * __init pcibios_get_irq_routing_table(void)
{
	struct irq_routing_options opt;
	struct irq_routing_table *rt;
	int ret, map;

	if (!(pci_probe & PCI_BIOS_IRQ_SCAN))
		return NULL;
	pcibios_irq_page = __get_free_page(GFP_KERNEL);
	if (!pcibios_irq_page)
		return 0;
	rt = (void *) pcibios_irq_page;
	opt.table = rt->slots;
	opt.size = PAGE_SIZE - sizeof(struct irq_routing_table);
	opt.segment = __KERNEL_DS;

	DBG("PCI: Fetching IRQ routing table... ");
	__asm__("push %%es\n\t"
		"push %%ds\n\t"
		"pop  %%es\n\t"
		"lcall (%%esi)\n\t"
		"pop %%es\n\t"
		"jc 1f\n\t"
		"xor %%ah, %%ah\n"
		"1:"
		: "=a" (ret),
		  "=b" (map)
		: "0" (PCIBIOS_GET_ROUTING_OPTIONS),
		  "1" (0),
		  "D" ((long) &opt),
		  "S" (&pci_indirect));
	DBG("OK  ret=%d, size=%d, map=%x\n", ret, opt.size, map);
	if (ret & 0xff00) {
		printk(KERN_ERR "PCI: Error %02x when fetching IRQ routing table.\n", (ret >> 8) & 0xff);
		return 0;
	}

	memset(rt, 0, sizeof(struct irq_routing_table));
	rt->size = opt.size + sizeof(struct irq_routing_table);
	printk("PCI: Using BIOS Interrupt Routing Table\n");
	return rt;
}

#endif

/*
 * Several buggy motherboards address only 16 devices and mirror
 * them to next 16 IDs. We try to detect this `feature' on all
 * primary buses (those containing host bridges as they are
 * expected to be unique) and remove the ghost devices.
 */

static void __init pcibios_fixup_ghosts(struct pci_bus *b)
{
	struct pci_dev *d, *e, **z;
	int mirror = PCI_DEVFN(16,0);
	int seen_host_bridge = 0;
	int i;

	DBG("PCI: Scanning for ghost devices on bus %d\n", b->number);
	for(d=b->devices; d && d->devfn < mirror; d=d->sibling) {
		if ((d->class >> 8) == PCI_CLASS_BRIDGE_HOST)
			seen_host_bridge++;
		for(e=d->next; e; e=e->sibling) {
			if (e->devfn != d->devfn + mirror ||
			    e->vendor != d->vendor ||
			    e->device != d->device ||
			    e->class != d->class)
				continue;
			for(i=0; i<PCI_NUM_RESOURCES; i++)
				if (e->resource[i].start != d->resource[i].start ||
				    e->resource[i].end != d->resource[i].end ||
				    e->resource[i].flags != d->resource[i].flags)
					continue;
			break;
		}
		if (!e)
			return;
	}
	if (!seen_host_bridge)
		return;
	printk("PCI: Ignoring ghost devices on bus %02x\n", b->number);
	for(e=b->devices; e->sibling != d; e=e->sibling);
	e->sibling = NULL;
	for(z=&pci_devices; (d=*z);)
		if (d->bus == b && d->devfn >= mirror) {
			*z = d->next;
			kfree_s(d, sizeof(*d));
		} else
			z = &d->next;
}

/*
 * In case there are peer host bridges, scan bus behind each of them.
 * Although several sources claim that the host bridges should have
 * header type 1 and be assigned a bus number as for PCI2PCI bridges,
 * the reality doesn't pass this test and the bus number is usually
 * set by BIOS to the first free value.
 */
static void __init pcibios_fixup_peer_bridges(void)
{
	struct pci_bus *b = pci_root;
	int n, cnt=-1;
	struct pci_dev *d;
	struct pci_ops *ops = pci_root->ops;

#ifdef CONFIG_PCI_DIRECT
	/*
	 * Don't search for peer host bridges if we use config type 2
	 * since it reads bogus values for non-existent buses and
	 * chipsets supporting multiple primary buses use conf1 anyway.
	 */
	if (ops == &pci_direct_conf2)
		return;
#endif

	for(d=b->devices; d; d=d->sibling)
		if ((d->class >> 8) == PCI_CLASS_BRIDGE_HOST)
			cnt++;
	n = b->subordinate + 1;
	while (n <= 0xff) {
		int found = 0;
		u16 l;
		struct pci_bus bus;
		struct pci_dev dev;
		bus.number = n;
		bus.ops = ops;
		dev.bus = &bus;
		for(dev.devfn=0; dev.devfn<256; dev.devfn += 8)
			if (!pci_read_config_word(&dev, PCI_VENDOR_ID, &l) &&
			    l != 0x0000 && l != 0xffff) {
#ifdef CONFIG_PCI_BIOS
				if (pci_bios_present) {
					int err, idx = 0;
					u8 bios_bus, bios_dfn;
					u16 d;
					pci_read_config_word(&dev, PCI_DEVICE_ID, &d);
					DBG("BIOS test for %02x:%02x (%04x:%04x)\n", n, dev.devfn, l, d);
					while (!(err = pci_bios_find_device(l, d, idx, &bios_bus, &bios_dfn)) &&
					       (bios_bus != n || bios_dfn != dev.devfn))
						idx++;
					if (err)
						break;
				}
#endif
				DBG("Found device at %02x:%02x\n", n, dev.devfn);
				found++;
				if (!pci_read_config_word(&dev, PCI_CLASS_DEVICE, &l) &&
				    l == PCI_CLASS_BRIDGE_HOST)
					cnt++;
			}
		if (cnt-- <= 0)
			break;
		if (found) {
			printk("PCI: Discovered primary peer bus %02x\n", n);
			b = pci_scan_bus(n, ops, NULL);
			if (b)
				n = b->subordinate;
		}
		n++;
	}
}

/*
 * Exceptions for specific devices. Usually work-arounds for fatal design flaws.
 */

static void __init pci_fixup_i450nx(struct pci_dev *d)
{
	/*
	 * i450NX -- Find and scan all secondary buses on all PXB's.
	 */
	int pxb, reg;
	u8 busno, suba, subb;
	printk("PCI: Searching for i450NX host bridges on %s\n", d->slot_name);
	reg = 0xd0;
	for(pxb=0; pxb<2; pxb++) {
		pci_read_config_byte(d, reg++, &busno);
		pci_read_config_byte(d, reg++, &suba);
		pci_read_config_byte(d, reg++, &subb);
		DBG("i450NX PXB %d: %02x/%02x/%02x\n", pxb, busno, suba, subb);
		if (busno)
			pci_scan_bus(busno, pci_root->ops, NULL);	/* Bus A */
		if (suba < subb)
			pci_scan_bus(suba+1, pci_root->ops, NULL);	/* Bus B */
	}
}

static void __init pci_fixup_rcc(struct pci_dev *d)
{
	/*
	 * RCC host bridges -- Find and scan all secondary buses.
	 * Register 0x44 contains first, 0x45 last bus number routed there.
	 */
	u8 busno;
	pci_read_config_byte(d, 0x44, &busno);
	printk("PCI: RCC host bridge: secondary bus %02x\n", busno);
	pci_scan_bus(busno, pci_root->ops, NULL);
}

static void __init pci_fixup_compaq(struct pci_dev *d)
{
	/*	
	 * Compaq host bridges -- Find and scan all secondary buses.
	 * This time registers 0xc8 and 0xc9.
	 */
	u8 busno;
	pci_read_config_byte(d, 0xc8, &busno);
	printk("PCI: Compaq host bridge: secondary bus %02x\n", busno);
	pci_scan_bus(busno, pci_root->ops, NULL);
}

static void __init pci_fixup_umc_ide(struct pci_dev *d)
{
	/*
	 * UM8886BF IDE controller sets region type bits incorrectly,
	 * therefore they look like memory despite of them being I/O.
	 */
	int i;

	printk("PCI: Fixing base address flags for device %s\n", d->slot_name);
	for(i=0; i<4; i++)
		d->resource[i].flags |= PCI_BASE_ADDRESS_SPACE_IO;
}

static void __init pci_fixup_ide_bases(struct pci_dev *d)
{
	int i;

	/*
	 * PCI IDE controllers use non-standard I/O port decoding, respect it.
	 */
	if ((d->class >> 8) != PCI_CLASS_STORAGE_IDE)
		return;
	DBG("PCI: IDE base address fixup for %s\n", d->slot_name);
	for(i=0; i<4; i++) {
		struct resource *r = &d->resource[i];
		if ((r->start & ~0x80) == 0x374) {
			r->start |= 2;
			r->end = r->start;
		}
	}
}

struct pci_fixup pcibios_fixups[] = {
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_INTEL,	PCI_DEVICE_ID_INTEL_82451NX,	pci_fixup_i450nx },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_RCC,	PCI_DEVICE_ID_RCC_HE,		pci_fixup_rcc },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_RCC,	PCI_DEVICE_ID_RCC_LE,		pci_fixup_rcc },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_COMPAQ,	PCI_DEVICE_ID_COMPAQ_6010,	pci_fixup_compaq },
	{ PCI_FIXUP_HEADER,	PCI_VENDOR_ID_UMC,	PCI_DEVICE_ID_UMC_UM8886BF,	pci_fixup_umc_ide },
	{ PCI_FIXUP_HEADER,	PCI_ANY_ID,		PCI_ANY_ID,			pci_fixup_ide_bases },
	{ 0 }
};

/*
 *  Fix up IRQs of all PCI devices.
 */

extern int skip_ioapic_setup;

#define PIRQ_SIGNATURE	(('$' << 0) + ('P' << 8) + ('I' << 16) + ('R' << 24))
#define PIRQ_VERSION 0x0100

static struct irq_routing_table *pirq_table;

/*
 *  Search 0xf0000 -- 0xfffff for the PCI IRQ Routing Table.
 */

static struct irq_routing_table * __init pcibios_find_irq_routing_table(void)
{
	u8 *addr;
	struct irq_routing_table *rt;
	int i;
	u8 sum;

	for(addr = (u8 *) __va(0xf0000); addr < (u8 *) __va(0x100000); addr += 16) {
		rt = (struct irq_routing_table *) addr;
		if (rt->signature != PIRQ_SIGNATURE ||
		    rt->version != PIRQ_VERSION ||
		    rt->size % 16 ||
		    rt->size < sizeof(struct irq_routing_table))
			continue;
		sum = 0;
		for(i=0; i<rt->size; i++)
			sum += addr[i];
		if (!sum) {
			printk("PCI: Interrupt Routing Table found at 0x%p [router type %04x/%04x]\n",
			       rt, rt->rtr_vendor, rt->rtr_device);
			return rt;
		}
	}
	return NULL;
}

/*
 *  If we have a IRQ routing table, use it to search for peer host
 *  bridges.  It's a gross hack, but since there are no other known
 *  ways how to get a list of buses, we have to go this way.
 */

static void __init pcibios_irq_peer_trick(struct irq_routing_table *rt)
{
	u8 busmap[256];
	int i;
	struct irq_info *e;

	memset(busmap, 0, sizeof(busmap));
	for(i=0; i < (rt->size - sizeof(struct irq_routing_table)) / sizeof(struct irq_info); i++) {
		e = &rt->slots[i];
		DBG("b=%02x d=%02x s=%02x\n", e->bus, e->devfn, e->slot);
		busmap[e->bus] = 1;
	}
	for(i=1; i<256; i++)
		/*
		 *  It might be a secondary bus, but in this case its parent is already
		 *  known (ascending bus order) and therefore pci_scan_bus returns immediately.
		 */
		if (busmap[i] && pci_scan_bus(i, pci_root->ops, NULL))
			printk("PCI: Discovered primary peer bus %02x [IRQ]\n", i);
}

/*
 *  In case BIOS forgets to tell us about IRQ, we try to look it up in the routing
 *  table, but unfortunately we have to know the interrupt router chip.
 */

static char *pcibios_lookup_irq(struct pci_dev *dev, struct irq_routing_table *rt, int pin, int assign)
{
	struct irq_info *q;
	struct pci_dev *router;
	int i, pirq, newirq;
	u32 rtrid, mask;
	u8 x;

	pin--;
	DBG("IRQ for %s(%d)", dev->slot_name, pin);
	while (dev->bus->self) {
		pin = (pin + PCI_SLOT(dev->devfn)) % 4;
		dev = dev->bus->self;
		DBG(" -> %s(%d)", dev->slot_name, pin);
	}
	for(q = rt->slots, i = rt->size - sizeof(struct irq_routing_table);
	    i && (q->bus != dev->bus->number || PCI_SLOT(q->devfn) != PCI_SLOT(dev->devfn));
	    i -= sizeof(struct irq_info), q++)
		;
	if (!i) {
		DBG(" -> not found in routing table\n");
		return NULL;
	}
	pirq = q->irq[pin].link;
	mask = q->irq[pin].bitmap;
	if (!pirq) {
		DBG(" -> not routed\n");
		return NULL;
	}
	DBG(" -> PIRQ %02x, mask %04x", pirq, mask);
	if (!assign || (dev->class >> 8) == PCI_CLASS_DISPLAY_VGA)
		newirq = 0;
	else for(newirq = 13; newirq && !(mask & (1 << newirq)); newirq--)
		;
	if (!(router = pci_find_slot(rt->rtr_bus, rt->rtr_devfn))) {
		DBG(" -> router not found\n");
		return NULL;
	}
#define ID(x,y) ((x << 16) | y)
	rtrid = ID(rt->rtr_vendor, rt->rtr_device);
	if (!rtrid) {
		/*
		 * Several BIOSes forget to set the router type. In such cases, we
		 * use chip vendor/device. This doesn't guarantee us semantics of
		 * PIRQ values, but was found to work in practice and it's still
		 * better than not trying.
		 */
		DBG(" [%s]", router->slot_name);
		rtrid = ID(router->vendor, router->device);
	}
	switch (rtrid) {
	case ID(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371FB_0):
	case ID(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371SB_0):
	case ID(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82371AB_0):
		/* Intel PIIX: PIRQ holds configuration register address */
		pci_read_config_byte(router, pirq, &x);
		if (x < 16) {
			DBG(" -> [PIIX] %02x\n", x);
			dev->irq = x;
			return "PIIX";
		} else if (newirq) {
			DBG(" -> [PIIX] set to %02x\n", newirq);
			pci_write_config_byte(router, pirq, newirq);
			dev->irq = newirq;
			return "PIIX-NEW";
		}
		DBG(" -> [PIIX] sink\n");
		return NULL;
	default:
		DBG(" -> unknown router %04x/%04x\n", rt->rtr_vendor, rt->rtr_device);
		if (newirq && mask == (1 << newirq)) {
			/* Only one IRQ available -> use it */
			dev->irq = newirq;
			return "guess";
		}
		return NULL;
	}
#undef ID
}

static void __init pcibios_fixup_irqs(void)
{
	struct irq_routing_table *rtable;
	struct pci_dev *dev;
	u8 pin;

	rtable = pirq_table = pcibios_find_irq_routing_table();
#ifdef CONFIG_PCI_BIOS
	if (!rtable && pci_bios_present)
		rtable = pcibios_get_irq_routing_table();
#endif

	if (rtable)
		pcibios_irq_peer_trick(rtable);

	for(dev=pci_devices; dev; dev=dev->next) {
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
#if defined(CONFIG_X86_IO_APIC)
		/*
		 * Recalculate IRQ numbers if we use the I/O APIC.
		 */
		if(!skip_ioapic_setup)
		{
			int irq;

			if (pin) {
				pin--;		/* interrupt pins are numbered starting from 1 */
				irq = IO_APIC_get_PCI_irq_vector(dev->bus->number, PCI_SLOT(dev->devfn), pin);
				if (irq < 0 && dev->bus->parent) { /* go back to the bridge */
					struct pci_dev * bridge = dev->bus->self;

					pin = (pin + PCI_SLOT(dev->devfn)) % 4;
					irq = IO_APIC_get_PCI_irq_vector(bridge->bus->number, 
							PCI_SLOT(bridge->devfn), pin);
					if (irq >= 0)
						printk(KERN_WARNING "PCI: using PPB(B%d,I%d,P%d) to get irq %d\n", 
							bridge->bus->number, PCI_SLOT(bridge->devfn), pin, irq);
				}
				if (irq >= 0) {
					printk("PCI->APIC IRQ transform: (B%d,I%d,P%d) -> %d\n",
						dev->bus->number, PCI_SLOT(dev->devfn), pin, irq);
					dev->irq = irq;
				}
			}
			pirq_table = NULL;	/* Avoid automatic IRQ assignment */
		}
#endif
		/*
		 * Fix out-of-range IRQ numbers and missing IRQs.
		 */
		if (dev->irq >= NR_IRQS)
			dev->irq = 0;
		if (pin && !dev->irq && pirq_table) {
			char *msg = pcibios_lookup_irq(dev, pirq_table, pin, 0);
			if (msg)
				printk("PCI: Found IRQ %d for device %s [%s]\n", dev->irq, dev->slot_name, msg);
		}
	}

#ifdef CONFIG_PCI_BIOS
	pcibios_free_irq_routing_table();
#endif
}

/*
 *  Called after each bus is probed, but before its children
 *  are examined.
 */

void __init pcibios_fixup_bus(struct pci_bus *b)
{
	pcibios_fixup_ghosts(b);
	pci_read_bridge_bases(b);
}

/*
 * Initialization. Try all known PCI access methods. Note that we support
 * using both PCI BIOS and direct access: in such cases, we use I/O ports
 * to access config space, but we still keep BIOS order of cards to be
 * compatible with 2.0.X. This should go away some day.
 */

void __init pcibios_init(void)
{
	struct pci_ops *bios = NULL;
	struct pci_ops *dir = NULL;
	struct pci_ops *ops;

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_PROBE_BIOS) && ((bios = pci_find_bios()))) {
		pci_probe |= PCI_BIOS_SORT;
		pci_bios_present = 1;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	if (pci_probe & (PCI_PROBE_CONF1 | PCI_PROBE_CONF2))
		dir = pci_check_direct();
#endif
	if (dir)
		ops = dir;
	else if (bios)
		ops = bios;
	else {
		printk("PCI: No PCI bus detected\n");
		return;
	}

	printk("PCI: Probing PCI hardware\n");
	pci_scan_bus(0, ops, NULL);

	pcibios_fixup_irqs();
	if (pci_probe & PCI_PEER_FIXUP)
		pcibios_fixup_peer_bridges();
	pcibios_resource_survey();

#ifdef CONFIG_PCI_BIOS
	if ((pci_probe & PCI_BIOS_SORT) && !(pci_probe & PCI_NO_SORT))
		pcibios_sort();
#endif
}

char * __init pcibios_setup(char *str)
{
	if (!strcmp(str, "off")) {
		pci_probe = 0;
		return NULL;
	}
#ifdef CONFIG_PCI_BIOS
	else if (!strcmp(str, "bios")) {
		pci_probe = PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nobios")) {
		pci_probe &= ~PCI_PROBE_BIOS;
		return NULL;
	} else if (!strcmp(str, "nosort")) {
		pci_probe |= PCI_NO_SORT;
		return NULL;
	} else if (!strcmp(str, "biosirq")) {
		pci_probe |= PCI_BIOS_IRQ_SCAN;
		return NULL;
	}
#endif
#ifdef CONFIG_PCI_DIRECT
	else if (!strcmp(str, "conf1")) {
		pci_probe = PCI_PROBE_CONF1 | PCI_NO_CHECKS;
		return NULL;
	}
	else if (!strcmp(str, "conf2")) {
		pci_probe = PCI_PROBE_CONF2 | PCI_NO_CHECKS;
		return NULL;
	}
#endif
	else if (!strcmp(str, "peer")) {
		pci_probe |= PCI_PEER_FIXUP;
		return NULL;
	} else if (!strcmp(str, "rom")) {
		pci_probe |= PCI_ASSIGN_ROMS;
		return NULL;
	}
	return str;
}

int pcibios_enable_device(struct pci_dev *dev)
{
	int err;

	if ((err = pcibios_enable_resources(dev)) < 0)
		return err;
	if (!dev->irq && pirq_table) {
		u8 pin;
		pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);
		if (pin) {
			char *msg = pcibios_lookup_irq(dev, pirq_table, pin, 1);
			if (msg)
				printk("PCI: Assigned IRQ %d to device %s [%s]\n", dev->irq, dev->slot_name, msg);
		}
	}
	return 0;
}
