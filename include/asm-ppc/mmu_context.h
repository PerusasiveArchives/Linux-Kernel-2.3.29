#include <linux/config.h>

#ifndef __PPC_MMU_CONTEXT_H
#define __PPC_MMU_CONTEXT_H

/* the way contexts are handled on the ppc they are vsid's and
   don't need any special treatment right now.
   perhaps I can defer flushing the tlb by keeping a list of
   zombie vsid/context's and handling that through destroy_context
   later -- Cort

   The MPC8xx has only 16 contexts.  We rotate through them on each
   task switch.  A better way would be to keep track of tasks that
   own contexts, and implement an LRU usage.  That way very active
   tasks don't always have to pay the TLB reload overhead.  The
   kernel pages are mapped shared, so the kernel can run on behalf
   of any task that makes a kernel entry.  Shared does not mean they
   are not protected, just that the ASID comparison is not performed.
        -- Dan
 */

#ifdef CONFIG_8xx
#define NO_CONTEXT      	16
#define LAST_CONTEXT    	15
#define MUNGE_CONTEXT(n)        (n)

#else

/* PPC 6xx, 7xx CPUs */
#define NO_CONTEXT      	0
#define LAST_CONTEXT    	0xfffff

/*
 * Allocating context numbers this way tends to spread out
 * the entries in the hash table better than a simple linear
 * allocation.
 */
#define MUNGE_CONTEXT(n)        (((n) * 897) & LAST_CONTEXT)
#endif

extern atomic_t next_mmu_context;
extern void mmu_context_overflow(void);

/*
 * Set the current MMU context.
 * On 32-bit PowerPCs (other than the 8xx embedded chips), this is done by
 * loading up the segment registers for the user part of the address space.
 */
extern void set_context(int context);

#ifdef CONFIG_8xx
extern inline void mmu_context_overflow(void)
{
	atomic_set(&next_mmu_context, -1);
}
#endif

/*
 * Get a new mmu context for task tsk if necessary.
 */
#define get_mmu_context(mm)					\
do { 								\
	if (mm->context == NO_CONTEXT) {			\
		if (atomic_read(&next_mmu_context) == LAST_CONTEXT)		\
			mmu_context_overflow();			\
		mm->context = MUNGE_CONTEXT(atomic_inc_return(&next_mmu_context));\
	}							\
} while (0)

/*
 * Set up the context for a new address space.
 */
#define init_new_context(tsk,mm)	((mm)->context = NO_CONTEXT)

/*
 * We're finished using the context for an address space.
 */
#define destroy_context(mm)     do { } while (0)

static inline void switch_mm(struct mm_struct *prev, struct mm_struct *next,
			     struct task_struct *tsk, int cpu)
{
	tsk->thread.pgdir = next->pgd;
	get_mmu_context(next);
	set_context(next->context);
}

/*
 * After we have set current->mm to a new value, this activates
 * the context for the new mm so we see the new mappings.
 */
static inline void activate_mm(struct mm_struct *active_mm, struct mm_struct *mm)
{
	current->thread.pgdir = mm->pgd;
	get_mmu_context(mm);
	set_context(mm->context);
}

/*
 * compute the vsid from the context and segment
 * segments > 7 are kernel segments and their
 * vsid is the segment -- Cort
 */
#define	VSID_FROM_CONTEXT(segment,context) \
   ((segment < 8) ? ((segment) | (context)<<4) : (segment))

#endif
