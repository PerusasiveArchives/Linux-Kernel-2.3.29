#ifndef _ASMARM_PAGE_H
#define _ASMARM_PAGE_H

#include <asm/proc/page.h>

#define PAGE_SIZE       (1UL << PAGE_SHIFT)
#define PAGE_MASK       (~(PAGE_SIZE-1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#define STRICT_MM_TYPECHECKS

#define clear_page(page)	memzero((void *)(page), PAGE_SIZE)
extern void copy_page(void *to, void *from);

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;

#define pte_val(x)      ((x).pte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)      ((x).pgd)
#define pgprot_val(x)   ((x).pgprot)

#define __pte(x)        ((pte_t) { (x) } )
#define __pmd(x)        ((pmd_t) { (x) } )
#define __pgd(x)        ((pgd_t) { (x) } )
#define __pgprot(x)     ((pgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long pgprot_t;

#define pte_val(x)      (x)
#define pmd_val(x)      (x)
#define pgd_val(x)      (x)
#define pgprot_val(x)   (x)

#define __pte(x)        (x)
#define __pmd(x)        (x)
#define __pgd(x)        (x)
#define __pgprot(x)     (x)

#endif
#endif /* !__ASSEMBLY__ */

/* to align the pointer to the (next) page boundary */
#define PAGE_ALIGN(addr)	(((addr)+PAGE_SIZE-1)&PAGE_MASK)

#ifndef __ASSEMBLY__

extern void __bug(const char *file, int line, void *data);

#define BUG()		__bug(__FILE__, __LINE__, NULL)
#define PAGE_BUG(page)	__bug(__FILE__, __LINE__, page)

#endif /* !__ASSEMBLY__ */

#include <asm/arch/memory.h>

#define __pa(x)			((unsigned long)(x) - PAGE_OFFSET + PHYS_OFFSET)
#define __va(x)			((void *)((unsigned long)(x) + PAGE_OFFSET - PHYS_OFFSET))
#define MAP_NR(addr)		(((unsigned long)(addr) - PAGE_OFFSET) >> PAGE_SHIFT)

#endif

#endif
