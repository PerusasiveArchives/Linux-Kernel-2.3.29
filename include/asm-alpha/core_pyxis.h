#ifndef __ALPHA_PYXIS__H__
#define __ALPHA_PYXIS__H__

#include <linux/types.h>
#include <asm/compiler.h>

/*
 * PYXIS is the internal name for a core logic chipset which provides
 * memory controller and PCI access for the 21164A chip based systems.
 *
 * This file is based on:
 *
 * Pyxis Chipset Spec
 * 14-Jun-96
 * Rev. X2.0
 *
 */

/*------------------------------------------------------------------------**
**                                                                        **
**  I/O procedures                                                        **
**                                                                        **
**      inport[b|w|t|l], outport[b|w|t|l] 8:16:24:32 IO xfers             **
**	inportbxt: 8 bits only                                            **
**      inport:    alias of inportw                                       **
**      outport:   alias of outportw                                      **
**                                                                        **
**      inmem[b|w|t|l], outmem[b|w|t|l] 8:16:24:32 ISA memory xfers       **
**	inmembxt: 8 bits only                                             **
**      inmem:    alias of inmemw                                         **
**      outmem:   alias of outmemw                                        **
**                                                                        **
**------------------------------------------------------------------------*/


/* PYXIS ADDRESS BIT DEFINITIONS
 *
 *  3333 3333 3322 2222 2222 1111 1111 11
 *  9876 5432 1098 7654 3210 9876 5432 1098 7654 3210
 *  ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
 *  1                                             000
 *  ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
 *  |						  |\|
 *  |                               Byte Enable --+ |
 *  |                             Transfer Length --+
 *  +-- IO space, not cached
 *
 *   Byte      Transfer
 *   Enable    Length    Transfer  Byte    Address
 *   adr<6:5>  adr<4:3>  Length    Enable  Adder
 *   ---------------------------------------------
 *      00        00      Byte      1110   0x000
 *      01        00      Byte      1101   0x020
 *      10        00      Byte      1011   0x040
 *      11        00      Byte      0111   0x060
 *
 *      00        01      Word      1100   0x008
 *      01        01      Word      1001   0x028 <= Not supported in this code.
 *      10        01      Word      0011   0x048
 *
 *      00        10      Tribyte   1000   0x010
 *      01        10      Tribyte   0001   0x030
 *
 *      10        11      Longword  0000   0x058
 *
 *      Note that byte enables are asserted low.
 *
 */

#define PYXIS_MEM_R1_MASK 0x1fffffff  /* SPARSE Mem region 1 mask is 29 bits */
#define PYXIS_MEM_R2_MASK 0x07ffffff  /* SPARSE Mem region 2 mask is 27 bits */
#define PYXIS_MEM_R3_MASK 0x03ffffff  /* SPARSE Mem region 3 mask is 26 bits */

#define PYXIS_DMA_WIN_BASE		(1UL*1024*1024*1024)
#define PYXIS_DMA_WIN_SIZE		(2UL*1024*1024*1024)

/* Window 0 at 1GB size 1GB mapping 0 */
#define PYXIS_DMA_WIN0_BASE_DEFAULT	(1UL*1024*1024*1024)
#define PYXIS_DMA_WIN0_SIZE_DEFAULT	(1UL*1024*1024*1024)
#define PYXIS_DMA_WIN0_TRAN_DEFAULT	(0UL)

/* Window 0 at 2GB size 1GB mapping 1GB */
#define PYXIS_DMA_WIN1_BASE_DEFAULT	(2UL*1024*1024*1024)
#define PYXIS_DMA_WIN1_SIZE_DEFAULT	(1UL*1024*1024*1024)
#define PYXIS_DMA_WIN1_TRAN_DEFAULT	(1UL*1024*1024*1024)


/*
 *  General Registers
 */
#define PYXIS_REV			(IDENT_ADDR + 0x8740000080UL)
#define PYXIS_PCI_LAT			(IDENT_ADDR + 0x87400000C0UL)
#define PYXIS_CTRL			(IDENT_ADDR + 0x8740000100UL)
#define PYXIS_CTRL1			(IDENT_ADDR + 0x8740000140UL)
#define PYXIS_FLASH_CTRL		(IDENT_ADDR + 0x8740000200UL)

#define PYXIS_HAE_MEM			(IDENT_ADDR + 0x8740000400UL)
#define PYXIS_HAE_IO			(IDENT_ADDR + 0x8740000440UL)
#define PYXIS_CFG			(IDENT_ADDR + 0x8740000480UL)

/*
 * Diagnostic Registers
 */
#define PYXIS_DIAG			(IDENT_ADDR + 0x8740002000UL)
#define PYXIS_DIAG_CHECK		(IDENT_ADDR + 0x8740003000UL)

/*
 * Performance Monitor registers
 */
#define PYXIS_PERF_MONITOR		(IDENT_ADDR + 0x8740004000UL)
#define PYXIS_PERF_CONTROL		(IDENT_ADDR + 0x8740004040UL)

/*
 * Error registers
 */
#define PYXIS_ERR			(IDENT_ADDR + 0x8740008200UL)
#define PYXIS_STAT			(IDENT_ADDR + 0x8740008240UL)
#define PYXIS_ERR_MASK			(IDENT_ADDR + 0x8740008280UL)
#define PYXIS_SYN			(IDENT_ADDR + 0x8740008300UL)
#define PYXIS_ERR_DATA			(IDENT_ADDR + 0x8740008308UL)

#define PYXIS_MEAR			(IDENT_ADDR + 0x8740008400UL)
#define PYXIS_MESR			(IDENT_ADDR + 0x8740008440UL)
#define PYXIS_PCI_ERR0			(IDENT_ADDR + 0x8740008800UL)
#define PYXIS_PCI_ERR1			(IDENT_ADDR + 0x8740008840UL)
#define PYXIS_PCI_ERR2			(IDENT_ADDR + 0x8740008880UL)

/*
 * PCI Address Translation Registers.
 */
#define PYXIS_TBIA			(IDENT_ADDR + 0x8760000100UL)

#define PYXIS_W0_BASE			(IDENT_ADDR + 0x8760000400UL)
#define PYXIS_W0_MASK			(IDENT_ADDR + 0x8760000440UL)
#define PYXIS_T0_BASE			(IDENT_ADDR + 0x8760000480UL)

#define PYXIS_W1_BASE			(IDENT_ADDR + 0x8760000500UL)
#define PYXIS_W1_MASK			(IDENT_ADDR + 0x8760000540UL)
#define PYXIS_T1_BASE			(IDENT_ADDR + 0x8760000580UL)

#define PYXIS_W2_BASE			(IDENT_ADDR + 0x8760000600UL)
#define PYXIS_W2_MASK			(IDENT_ADDR + 0x8760000640UL)
#define PYXIS_T2_BASE			(IDENT_ADDR + 0x8760000680UL)

#define PYXIS_W3_BASE			(IDENT_ADDR + 0x8760000700UL)
#define PYXIS_W3_MASK			(IDENT_ADDR + 0x8760000740UL)
#define PYXIS_T3_BASE			(IDENT_ADDR + 0x8760000780UL)

/*
 * Memory Control registers
 */
#define PYXIS_MCR			(IDENT_ADDR + 0x8750000000UL)

/*
 * Memory spaces:
 */
#define PYXIS_IACK_SC		        (IDENT_ADDR + 0x8720000000UL)
#define PYXIS_CONF		        (IDENT_ADDR + 0x8700000000UL)
#define PYXIS_IO			(IDENT_ADDR + 0x8580000000UL)
#define PYXIS_SPARSE_MEM		(IDENT_ADDR + 0x8000000000UL)
#define PYXIS_SPARSE_MEM_R2		(IDENT_ADDR + 0x8400000000UL)
#define PYXIS_SPARSE_MEM_R3		(IDENT_ADDR + 0x8500000000UL)
#define PYXIS_DENSE_MEM		        (IDENT_ADDR + 0x8600000000UL)

/*
 * Byte/Word PCI Memory Spaces:
 */
#define PYXIS_BW_MEM			(IDENT_ADDR + 0x8800000000UL)
#define PYXIS_BW_IO			(IDENT_ADDR + 0x8900000000UL)
#define PYXIS_BW_CFG_0			(IDENT_ADDR + 0x8a00000000UL)
#define PYXIS_BW_CFG_1			(IDENT_ADDR + 0x8b00000000UL)

/*
 * Interrupt Control registers
 */
#define PYXIS_INT_REQ			(IDENT_ADDR + 0x87A0000000UL)
#define PYXIS_INT_MASK			(IDENT_ADDR + 0x87A0000040UL)
#define PYXIS_INT_HILO			(IDENT_ADDR + 0x87A00000C0UL)
#define PYXIS_INT_ROUTE			(IDENT_ADDR + 0x87A0000140UL)
#define PYXIS_GPO			(IDENT_ADDR + 0x87A0000180UL)
#define PYXIS_INT_CNFG			(IDENT_ADDR + 0x87A00001C0UL)
#define PYXIS_RT_COUNT			(IDENT_ADDR + 0x87A0000200UL)
#define PYXIS_INT_TIME			(IDENT_ADDR + 0x87A0000240UL)
#define PYXIS_IIC_CTRL			(IDENT_ADDR + 0x87A00002C0UL)
#define PYXIS_RESET			(IDENT_ADDR + 0x8780000900UL)

/*
 * Bit definitions for I/O Controller status register 0:
 */
#define PYXIS_STAT0_CMD			0xf
#define PYXIS_STAT0_ERR			(1<<4)
#define PYXIS_STAT0_LOST		(1<<5)
#define PYXIS_STAT0_THIT		(1<<6)
#define PYXIS_STAT0_TREF		(1<<7)
#define PYXIS_STAT0_CODE_SHIFT		8
#define PYXIS_STAT0_CODE_MASK		0x7
#define PYXIS_STAT0_P_NBR_SHIFT		13
#define PYXIS_STAT0_P_NBR_MASK		0x7ffff

#define PYXIS_HAE_ADDRESS                PYXIS_HAE_MEM

/*
 * Data structure for handling PYXIS machine checks:
 */
struct el_PYXIS_sysdata_mcheck {
    u_long      coma_gcr;
    u_long      coma_edsr;
    u_long      coma_ter;
    u_long      coma_elar;
    u_long      coma_ehar;
    u_long      coma_ldlr;
    u_long      coma_ldhr;
    u_long      coma_base0;
    u_long      coma_base1;
    u_long      coma_base2;
    u_long      coma_cnfg0;
    u_long      coma_cnfg1;
    u_long      coma_cnfg2;
    u_long      epic_dcsr;
    u_long      epic_pear;
    u_long      epic_sear;
    u_long      epic_tbr1;
    u_long      epic_tbr2;
    u_long      epic_pbr1;
    u_long      epic_pbr2;
    u_long      epic_pmr1;
    u_long      epic_pmr2;
    u_long      epic_harx1;
    u_long      epic_harx2;
    u_long      epic_pmlt;
    u_long      epic_tag0;
    u_long      epic_tag1;
    u_long      epic_tag2;
    u_long      epic_tag3;
    u_long      epic_tag4;
    u_long      epic_tag5;
    u_long      epic_tag6;
    u_long      epic_tag7;
    u_long      epic_data0;
    u_long      epic_data1;
    u_long      epic_data2;
    u_long      epic_data3;
    u_long      epic_data4;
    u_long      epic_data5;
    u_long      epic_data6;
    u_long      epic_data7;
};


#ifdef __KERNEL__

#ifndef __EXTERN_INLINE
#define __EXTERN_INLINE extern inline
#define __IO_EXTERN_INLINE
#endif

/*
 * Translate physical memory address as seen on (PCI) bus into
 * a kernel virtual address and vv.
 */

__EXTERN_INLINE unsigned long pyxis_virt_to_bus(void * address)
{
	return virt_to_phys(address) + PYXIS_DMA_WIN_BASE;
}

__EXTERN_INLINE void * pyxis_bus_to_virt(unsigned long address)
{
	return phys_to_virt(address - PYXIS_DMA_WIN_BASE);
}


/*
 * I/O functions:
 *
 * PYXIS, the 21174 PCI/memory support chipset for the EV56 (21164A)
 * and PCA56 (21164PC) processors, can use either a sparse address
 * mapping scheme, or the so-called byte-word PCI address space, to
 * get at PCI memory and I/O.
 */

#define vucp	volatile unsigned char *
#define vusp	volatile unsigned short *
#define vip	volatile int *
#define vuip	volatile unsigned int *
#define vulp	volatile unsigned long *

__EXTERN_INLINE unsigned int pyxis_inb(unsigned long addr)
{
	/* ??? I wish I could get rid of this.  But there's no ioremap
	   equivalent for I/O space.  PCI I/O can be forced into the
	   PYXIS I/O region, but that doesn't take care of legacy ISA crap.  */

	return __kernel_ldbu(*(vucp)(addr+PYXIS_BW_IO));
}

__EXTERN_INLINE void pyxis_outb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)(addr+PYXIS_BW_IO));
	mb();
}

__EXTERN_INLINE unsigned int pyxis_inw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)(addr+PYXIS_BW_IO));
}

__EXTERN_INLINE void pyxis_outw(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)(addr+PYXIS_BW_IO));
	mb();
}

__EXTERN_INLINE unsigned int pyxis_inl(unsigned long addr)
{
	return *(vuip)(addr+PYXIS_BW_IO);
}

__EXTERN_INLINE void pyxis_outl(unsigned int b, unsigned long addr)
{
	*(vuip)(addr+PYXIS_BW_IO) = b;
	mb();
}


/*
 * Memory functions.  64-bit and 32-bit accesses are done through
 * dense memory space, everything else through sparse space.
 *
 * For reading and writing 8 and 16 bit quantities we need to
 * go through one of the three sparse address mapping regions
 * and use the HAE_MEM CSR to provide some bits of the address.
 * The following few routines use only sparse address region 1
 * which gives 1Gbyte of accessible space which relates exactly
 * to the amount of PCI memory mapping *into* system address space.
 * See p 6-17 of the specification but it looks something like this:
 *
 * 21164 Address:
 *
 *          3         2         1
 * 9876543210987654321098765432109876543210
 * 1ZZZZ0.PCI.QW.Address............BBLL
 *
 * ZZ = SBZ
 * BB = Byte offset
 * LL = Transfer length
 *
 * PCI Address:
 *
 * 3         2         1
 * 10987654321098765432109876543210
 * HHH....PCI.QW.Address........ 00
 *
 * HHH = 31:29 HAE_MEM CSR
 *
 */

__EXTERN_INLINE unsigned long pyxis_readb(unsigned long addr)
{
	return __kernel_ldbu(*(vucp)addr);
}

__EXTERN_INLINE unsigned long pyxis_readw(unsigned long addr)
{
	return __kernel_ldwu(*(vusp)addr);
}

__EXTERN_INLINE unsigned long pyxis_readl(unsigned long addr)
{
	return *(vuip)addr;
}

__EXTERN_INLINE unsigned long pyxis_readq(unsigned long addr)
{
	return *(vulp)addr;
}

__EXTERN_INLINE void pyxis_writeb(unsigned char b, unsigned long addr)
{
	__kernel_stb(b, *(vucp)addr);
}

__EXTERN_INLINE void pyxis_writew(unsigned short b, unsigned long addr)
{
	__kernel_stw(b, *(vusp)addr);
}

__EXTERN_INLINE void pyxis_writel(unsigned int b, unsigned long addr)
{
	*(vuip)addr = b;
}

__EXTERN_INLINE void pyxis_writeq(unsigned long b, unsigned long addr)
{
	*(vulp)addr = b;
}

__EXTERN_INLINE unsigned long pyxis_ioremap(unsigned long addr)
{
	return addr + PYXIS_BW_MEM;
}

__EXTERN_INLINE int pyxis_is_ioaddr(unsigned long addr)
{
	return addr >= IDENT_ADDR + 0x8740000000UL;
}

#undef vucp
#undef vusp
#undef vip
#undef vuip
#undef vulp

#ifdef __WANT_IO_DEF

#define virt_to_bus	pyxis_virt_to_bus
#define bus_to_virt	pyxis_bus_to_virt

#define __inb		pyxis_inb
#define __inw		pyxis_inw
#define __inl		pyxis_inl
#define __outb		pyxis_outb
#define __outw		pyxis_outw
#define __outl		pyxis_outl
#define __readb		pyxis_readb
#define __readw		pyxis_readw
#define __writeb	pyxis_writeb
#define __writew	pyxis_writew
#define __readl		pyxis_readl
#define __readq		pyxis_readq
#define __writel	pyxis_writel
#define __writeq	pyxis_writeq
#define __ioremap	pyxis_ioremap
#define __is_ioaddr	pyxis_is_ioaddr

#define inb(port)		__inb((port))
#define inw(port)		__inw((port))
#define inl(port)		__inl((port))
#define outb(x, port)		__outb((x),(port))
#define outw(x, port)		__outw((x),(port))
#define outl(x, port)		__outl((x),(port))
#define __raw_readb(addr)	__readb((addr))
#define __raw_readw(addr)	__readw((addr))
#define __raw_writeb(b, addr)	__writeb((b),(addr))
#define __raw_writew(b, addr)	__writew((b),(addr))
#define __raw_readl(a)		__readl((unsigned long)(a))
#define __raw_readq(a)		__readq((unsigned long)(a))
#define __raw_writel(v,a)	__writel((v),(unsigned long)(a))
#define __raw_writeq(v,a)	__writeq((v),(unsigned long)(a))

#endif /* __WANT_IO_DEF */

#ifdef __IO_EXTERN_INLINE
#undef __EXTERN_INLINE
#undef __IO_EXTERN_INLINE
#endif

#endif /* __KERNEL__ */

#endif /* __ALPHA_PYXIS__H__ */
