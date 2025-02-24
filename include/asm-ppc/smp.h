/* smp.h: PPC specific SMP stuff.
 *
 * Taken from asm-sparc/smp.h
 */

#ifndef _PPC_SMP_H
#define _PPC_SMP_H

#include <linux/kernel.h>

#ifdef __SMP__

#ifndef __ASSEMBLY__

struct cpuinfo_PPC {
	unsigned long loops_per_sec;
	unsigned long pvr;
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
};
extern struct cpuinfo_PPC cpu_data[NR_CPUS];

extern unsigned long smp_proc_in_lock[NR_CPUS];

extern void smp_message_pass(int target, int msg, unsigned long data, int wait);
extern void smp_store_cpu_info(int id);
extern void smp_message_recv(void);

#define NO_PROC_ID		0xFF            /* No processor magic marker */
#define PROC_CHANGE_PENALTY	20

/* 1 to 1 mapping on PPC -- Cort */
#define cpu_logical_map(cpu) (cpu)
extern int cpu_number_map[NR_CPUS];
extern volatile unsigned long cpu_callin_map[NR_CPUS];

#define hard_smp_processor_id() (0)
#define smp_processor_id() (current->processor)

struct klock_info_struct {
	unsigned long kernel_flag;
	unsigned char akp;
};

extern struct klock_info_struct klock_info;
#define KLOCK_HELD       0xffffffff
#define KLOCK_CLEAR      0x0

#endif /* __ASSEMBLY__ */

#else /* !(__SMP__) */

#endif /* !(__SMP__) */

#endif /* !(_PPC_SMP_H) */
