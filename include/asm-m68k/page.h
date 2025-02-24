#ifndef _M68K_PAGE_H
#define _M68K_PAGE_H

#include <linux/config.h>

/* PAGE_SHIFT determines the page size */
#ifndef CONFIG_SUN3
#define PAGE_SHIFT	(12)
#define PAGE_SIZE	(4096)
#else
#define PAGE_SHIFT	(13)
#define PAGE_SIZE	(8192)
#endif
#define PAGE_MASK	(~(PAGE_SIZE-1))

#ifdef __KERNEL__

#include <asm/setup.h>

#if PAGE_SHIFT < 13
#define KTHREAD_SIZE (8192)
#else
#define KTHREAD_SIZE PAGE_SIZE
#endif
 
#ifndef __ASSEMBLY__
 
#define STRICT_MM_TYPECHECKS

#define get_user_page(vaddr)	__get_free_page(GFP_KERNEL)
#define free_user_page(page, addr)	free_page(addr)

/*
 * We don't need to check for alignment etc.
 */
#ifdef CPU_M68040_OR_M68060_ONLY
static inline void copy_page(unsigned long to, unsigned long from)
{
  unsigned long tmp;

  __asm__ __volatile__("1:\t"
		       ".chip 68040\n\t"
		       "move16 %1@+,%0@+\n\t"
		       "move16 %1@+,%0@+\n\t"
		       ".chip 68k\n\t"
		       "dbra  %2,1b\n\t"
		       : "=a" (to), "=a" (from), "=d" (tmp)
		       : "0" (to), "1" (from) , "2" (PAGE_SIZE / 32 - 1)
		       );
}

static inline void clear_page(unsigned long page)
{
	unsigned long data, sp, tmp;

	sp = page;

	data = 0;

	*((unsigned long *)(page))++ = 0;
	*((unsigned long *)(page))++ = 0;
	*((unsigned long *)(page))++ = 0;
	*((unsigned long *)(page))++ = 0;

	__asm__ __volatile__("1:\t"
			     ".chip 68040\n\t"
			     "move16 %2@+,%0@+\n\t"
			     ".chip 68k\n\t"
			     "subqw  #8,%2\n\t"
			     "subqw  #8,%2\n\t"
			     "dbra   %1,1b\n\t"
			     : "=a" (page), "=d" (tmp)
			     : "a" (sp), "0" (page),
			       "1" ((PAGE_SIZE - 16) / 16 - 1));
}

#else
#define clear_page(page)	memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from)	memcpy((void *)(to), (void *)(from), PAGE_SIZE)
#endif

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)	((x).pte)
#define pmd_val(x)	((&x)->pmd[0])
#define pgd_val(x)	((x).pgd)
#define pgprot_val(x)	((x).pgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef struct { unsigned long pmd[16]; } pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)	(x)
#define pmd_val(x)	((&x)->pmd[0])
#define pgd_val(x)	(x)
#define pgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)	((pmd_t) { (x) } )
#define __pgd(x)	(x)
#define __pgprot(x)	(x)

#endif

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

/* This handles the memory map.. */
#ifndef CONFIG_SUN3
#define PAGE_OFFSET		0
#else
#define PAGE_OFFSET		0x0E000000
#endif

#ifndef CONFIG_SUN3
#define __pa(x)			((unsigned long)(x)-PAGE_OFFSET)
/*
 * A hacky workaround for the problems with mmap() of frame buffer
 * memory in the lower 16MB physical memoryspace.
 *
 * This is a short term solution, we will have to deal properly
 * with this in 2.3.x.
 */
extern inline void *__va(unsigned long physaddr)
{
#ifdef CONFIG_AMIGA
	if (MACH_IS_AMIGA && (physaddr < 16*1024*1024))
		return (void *)0xffffffff;
	else
#endif
		return (void *)(physaddr+PAGE_OFFSET);
}
#else	/* !CONFIG_SUN3 */
/* This #define is a horrible hack to suppress lots of warnings. --m */
#define __pa(x) ___pa((unsigned long)x)
static inline unsigned long ___pa(unsigned long x)
{
     if(x == 0)
	  return 0;
     if(x > PAGE_OFFSET)
        return (x-PAGE_OFFSET);
     else
        return (x+0x2000000);
}

static inline void *__va(unsigned long x)
{
     if(x == 0)
	  return (void *)0;

     if(x < 0x2000000)
        return (void *)(x+PAGE_OFFSET);
     else
        return (void *)(x-0x2000000);
}
#endif	/* CONFIG_SUN3 */

#define MAP_NR(addr)		(__pa(addr) >> PAGE_SHIFT)

#endif /* !__ASSEMBLY__ */

#ifndef CONFIG_SUN3
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	asm volatile("illegal"); \
} while (0)
#else
#define BUG() do { \
	printk("kernel BUG at %s:%d!\n", __FILE__, __LINE__); \
	panic("BUG!"); \
} while (0)
#endif

#define PAGE_BUG(page) do { \
	BUG(); \
} while (0)

#endif /* __KERNEL__ */

#endif /* _M68K_PAGE_H */
