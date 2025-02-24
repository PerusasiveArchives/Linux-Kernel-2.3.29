#ifndef __ALPHA_JENSEN_H
#define __ALPHA_JENSEN_H

#include <asm/compiler.h>

/*
 * Defines for the AlphaPC EISA IO and memory address space.
 */

/* The Jensen is strange */
#define AUX_IRQ (9)

/*
 * NOTE! The memory operations do not set any memory barriers, as it's
 * not needed for cases like a frame buffer that is essentially memory-like.
 * You need to do them by hand if the operations depend on ordering.
 *
 * Similarly, the port IO operations do a "mb" only after a write operation:
 * if an mb is needed before (as in the case of doing memory mapped IO
 * first, and then a port IO operation to the same device), it needs to be
 * done by hand.
 *
 * After the above has bitten me 100 times, I'll give up and just do the
 * mb all the time, but right now I'm hoping this will work out.  Avoiding
 * mb's may potentially be a noticeable speed improvement, but I can't
 * honestly say I've tested it.
 *
 * Handling interrupts that need to do mb's to synchronize to non-interrupts
 * is another fun race area.  Don't do it (because if you do, I'll have to
 * do *everything* with interrupts disabled, ugh).
 */

/*
 * EISA Interrupt Acknowledge address
 */
#define EISA_INTA		(IDENT_ADDR + 0x100000000UL)

/*
 * FEPROM addresses
 */
#define EISA_FEPROM0		(IDENT_ADDR + 0x180000000UL)
#define EISA_FEPROM1		(IDENT_ADDR + 0x1A0000000UL)

/*
 * VL82C106 base address
 */
#define EISA_VL82C106		(IDENT_ADDR + 0x1C0000000UL)

/*
 * EISA "Host Address Extension" address (bits 25-31 of the EISA address)
 */
#define EISA_HAE		(IDENT_ADDR + 0x1D0000000UL)

/*
 * "SYSCTL" register address
 */
#define EISA_SYSCTL		(IDENT_ADDR + 0x1E0000000UL)

/*
 * "spare" register address
 */
#define EISA_SPARE		(IDENT_ADDR + 0x1F0000000UL)

/*
 * EISA memory address offset
 */
#define EISA_MEM		(IDENT_ADDR + 0x200000000UL)

/*
 * EISA IO address offset
 */
#define EISA_IO			(IDENT_ADDR + 0x300000000UL)


#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * Change virtual addresses to bus addresses and vv.
 *
 * NOTE! On the Jensen, the physical address is the same
 * as the bus address, but this is not necessarily true on
 * other alpha hardware.
 */
__EXTERN_INLINE unsigned long jensen_virt_to_bus(void * address)
{
	return virt_to_phys(address);
}

__EXTERN_INLINE void * jensen_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address);
}


/*
 * Handle the "host address register". This needs to be set
 * to the high 7 bits of the EISA address.  This is also needed
 * for EISA IO addresses, which are only 16 bits wide (the
 * hae needs to be set to 0).
 *
 * HAE isn't needed for the local IO operations, though.
 */

#define JENSEN_HAE_ADDRESS	EISA_HAE
#define JENSEN_HAE_MASK		0x1ffffff

__EXTERN_INLINE void jensen_set_hae(unsigned long addr)
{
	/* hae on the Jensen is bits 31:25 shifted right */
	addr >>= 25;
	if (addr != alpha_mv.hae_cache)
		set_hae(addr);
}

#define vuip	volatile unsigned int *

/*
 * IO functions
 *
 * The "local" functions are those that don't go out to the EISA bus,
 * but instead act on the VL82C106 chip directly.. This is mainly the
 * keyboard, RTC,  printer and first two serial lines..
 *
 * The local stuff makes for some complications, but it seems to be
 * gone in the PCI version. I hope I can get DEC suckered^H^H^H^H^H^H^H^H
 * convinced that I need one of the newer machines.
 */

static inline unsigned int jensen_local_inb(unsigned long addr)
{
	return 0xff & *(vuip)((addr << 9) + EISA_VL82C106);
}

static inline void jensen_local_outb(unsigned char b, unsigned long addr)
{
	*(vuip)((addr << 9) + EISA_VL82C106) = b;
	mb();
}

static inline unsigned int jensen_bus_inb(unsigned long addr)
{
	long result;

	jensen_set_hae(0);
	result = *(volatile int *)((addr << 7) + EISA_IO + 0x00);
	return __kernel_extbl(result, addr & 3);
}

static inline void jensen_bus_outb(unsigned char b, unsigned long addr)
{
	jensen_set_hae(0);
	*(vuip)((addr << 7) + EISA_IO + 0x00) = b * 0x01010101;
	mb();
}

/*
 * It seems gcc is not very good at optimizing away logical
 * operations that result in operations across inline functions.
 * Which is why this is a macro.
 */

#define jensen_is_local(addr) ( \
/* keyboard */	(addr == 0x60 || addr == 0x64) || \
/* RTC */	(addr == 0x170 || addr == 0x171) || \
/* mb COM2 */	(addr >= 0x2f8 && addr <= 0x2ff) || \
/* mb LPT1 */	(addr >= 0x3bc && addr <= 0x3be) || \
/* mb COM2 */	(addr >= 0x3f8 && addr <= 0x3ff))

__EXTERN_INLINE unsigned int jensen_inb(unsigned long addr)
{
	if (jensen_is_local(addr))
		return jensen_local_inb(addr);
	else
		return jensen_bus_inb(addr);
}

__EXTERN_INLINE void jensen_outb(unsigned char b, unsigned long addr)
{
	if (jensen_is_local(addr))
		jensen_local_outb(b, addr);
	else
		jensen_bus_outb(b, addr);
}

__EXTERN_INLINE unsigned int jensen_inw(unsigned long addr)
{
	long result;

	jensen_set_hae(0);
	result = *(volatile int *) ((addr << 7) + EISA_IO + 0x20);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

__EXTERN_INLINE unsigned int jensen_inl(unsigned long addr)
{
	jensen_set_hae(0);
	return *(vuip) ((addr << 7) + EISA_IO + 0x60);
}

__EXTERN_INLINE void jensen_outw(unsigned short b, unsigned long addr)
{
	jensen_set_hae(0);
	*(vuip) ((addr << 7) + EISA_IO + 0x20) = b * 0x00010001;
	mb();
}

__EXTERN_INLINE void jensen_outl(unsigned int b, unsigned long addr)
{
	jensen_set_hae(0);
	*(vuip) ((addr << 7) + EISA_IO + 0x60) = b;
	mb();
}

/*
 * Memory functions.
 */

__EXTERN_INLINE unsigned long jensen_readb(unsigned long addr)
{
	long result;

	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	result = *(volatile int *) ((addr << 7) + EISA_MEM + 0x00);
	result >>= (addr & 3) * 8;
	return 0xffUL & result;
}

__EXTERN_INLINE unsigned long jensen_readw(unsigned long addr)
{
	long result;

	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	result = *(volatile int *) ((addr << 7) + EISA_MEM + 0x20);
	result >>= (addr & 3) * 8;
	return 0xffffUL & result;
}

__EXTERN_INLINE unsigned long jensen_readl(unsigned long addr)
{
	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	return *(vuip) ((addr << 7) + EISA_MEM + 0x60);
}

__EXTERN_INLINE unsigned long jensen_readq(unsigned long addr)
{
	unsigned long r0, r1;

	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	addr = (addr << 7) + EISA_MEM + 0x60;
	r0 = *(vuip) (addr);
	r1 = *(vuip) (addr + (4 << 7));
	return r1 << 32 | r0;
}

__EXTERN_INLINE void jensen_writeb(unsigned char b, unsigned long addr)
{
	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	*(vuip) ((addr << 7) + EISA_MEM + 0x00) = b * 0x01010101;
}

__EXTERN_INLINE void jensen_writew(unsigned short b, unsigned long addr)
{
	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	*(vuip) ((addr << 7) + EISA_MEM + 0x20) = b * 0x00010001;
}

__EXTERN_INLINE void jensen_writel(unsigned int b, unsigned long addr)
{
	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	*(vuip) ((addr << 7) + EISA_MEM + 0x60) = b;
}

__EXTERN_INLINE void jensen_writeq(unsigned long b, unsigned long addr)
{
	jensen_set_hae(addr);
	addr &= JENSEN_HAE_MASK;
	addr = (addr << 7) + EISA_MEM + 0x60;
	*(vuip) (addr) = b;
	*(vuip) (addr + (4 << 7)) = b >> 32;
}

__EXTERN_INLINE unsigned long jensen_ioremap(unsigned long addr)
{
	return addr;
}

__EXTERN_INLINE int jensen_is_ioaddr(unsigned long addr)
{
	return (long)addr >= 0;
}

#undef vuip

#ifdef __WANT_IO_DEF

#define virt_to_bus	jensen_virt_to_bus
#define bus_to_virt	jensen_bus_to_virt
#define __inb		jensen_inb
#define __inw		jensen_inw
#define __inl		jensen_inl
#define __outb		jensen_outb
#define __outw		jensen_outw
#define __outl		jensen_outl
#define __readb		jensen_readb
#define __readw		jensen_readw
#define __writeb	jensen_writeb
#define __writew	jensen_writew
#define __readl		jensen_readl
#define __readq		jensen_readq
#define __writel	jensen_writel
#define __writeq	jensen_writeq
#define __ioremap	jensen_ioremap
#define __is_ioaddr	jensen_is_ioaddr

/*
 * The above have so much overhead that it probably doesn't make
 * sense to have them inlined (better icache behaviour).
 */
#define inb(port) \
(__builtin_constant_p((port))?__inb(port):_inb(port))

#define outb(x, port) \
(__builtin_constant_p((port))?__outb((x),(port)):_outb((x),(port)))

#endif /* __WANT_IO_DEF */

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_JENSEN_H */
