#ifndef __ASM_SOFTIRQ_H
#define __ASM_SOFTIRQ_H

#include <asm/atomic.h>
#include <asm/hardirq.h>

#define get_active_bhs()	(bh_mask & bh_active)
#define clear_active_bhs(x)	atomic_clear_mask((x),&bh_active)

extern unsigned int ppc_local_bh_count[NR_CPUS];

extern inline void init_bh(int nr, void (*routine)(void))
{
	bh_base[nr] = routine;
	atomic_set(&bh_mask_count[nr], 0);
	bh_mask |= 1 << nr;
}

extern inline void remove_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	wmb();
	bh_base[nr] = 0;
}

extern inline void mark_bh(int nr)
{
	set_bit(nr, &bh_active);
}

#ifdef __SMP__

/*
 * The locking mechanism for base handlers, to prevent re-entrancy,
 * is entirely private to an implementation, it should not be
 * referenced at all outside of this file.
 */
extern atomic_t global_bh_lock;
extern atomic_t global_bh_count;

extern void synchronize_bh(void);

static inline void start_bh_atomic(void)
{
	atomic_inc(&global_bh_lock);
	synchronize_bh();
}

static inline void end_bh_atomic(void)
{
	atomic_dec(&global_bh_lock);
}

/* These are for the IRQs testing the lock */
static inline int softirq_trylock(int cpu)
{
	if (ppc_local_bh_count[cpu] == 0) {
		ppc_local_bh_count[cpu] = 1;
		if (!test_and_set_bit(0,&global_bh_count)) {
			mb();
			if (atomic_read(&global_bh_lock) == 0)
				return 1;
			clear_bit(0,&global_bh_count);
		}
		ppc_local_bh_count[cpu] = 0;
		mb();
	}
	return 0;
}

static inline void softirq_endlock(int cpu)
{
	mb();
	ppc_local_bh_count[cpu]--;
	clear_bit(0,&global_bh_count);
}

#else

extern inline void start_bh_atomic(void)
{
	ppc_local_bh_count[smp_processor_id()]++;
	barrier();
}

extern inline void end_bh_atomic(void)
{
	barrier();
	ppc_local_bh_count[smp_processor_id()]--;
}

/* These are for the irq's testing the lock */
#define softirq_trylock(cpu)	(ppc_local_bh_count[cpu] ? 0 : (ppc_local_bh_count[cpu]=1))
#define softirq_endlock(cpu)	(ppc_local_bh_count[cpu] = 0)
#define synchronize_bh()	barrier()

#endif	/* SMP */

#define local_bh_disable()	(ppc_local_bh_count[smp_processor_id()]++)
#define local_bh_enable()	(ppc_local_bh_count[smp_processor_id()]--)

/*
 * These use a mask count to correctly handle
 * nested disable/enable calls
 */
extern inline void disable_bh(int nr)
{
	bh_mask &= ~(1 << nr);
	atomic_inc(&bh_mask_count[nr]);
	synchronize_bh();
}

extern inline void enable_bh(int nr)
{
	if (atomic_dec_and_test(&bh_mask_count[nr]))
		bh_mask |= 1 << nr;
}

#endif	/* __ASM_SOFTIRQ_H */
