/*  sun4m_irq.c
 *  arch/sparc/kernel/sun4m_irq.c:
 *
 *  djhr: Hacked out of irq.c into a CPU dependent version.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@ipmce.su)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 */

#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/io.h>

static unsigned long dummy;

struct sun4m_intregs *sun4m_interrupts;
unsigned long *irq_rcvreg = &dummy;

/* These tables only apply for interrupts greater than 15..
 * 
 * any intr value below 0x10 is considered to be a soft-int
 * this may be useful or it may not.. but that's how I've done it.
 * and it won't clash with what OBP is telling us about devices.
 *
 * take an encoded intr value and lookup if it's valid
 * then get the mask bits that match from irq_mask
 *
 * P3: Translation from irq 0x0d to mask 0x2000 is for MrCoffee.
 */
static unsigned char irq_xlate[32] = {
    /*  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  a,  b,  c,  d,  e,  f */
	0,  0,  0,  0,  1,  0,  2,  0,  3,  0,  4,  5,  6, 14,  0,  7,
	0,  0,  8,  9,  0, 10,  0, 11,  0, 12,  0, 13,  0, 14,  0,  0
};

static unsigned long irq_mask[] = {
	0,						  /* illegal index */
	SUN4M_INT_SCSI,				  	  /*  1 irq 4 */
	SUN4M_INT_ETHERNET,				  /*  2 irq 6 */
	SUN4M_INT_VIDEO,				  /*  3 irq 8 */
	SUN4M_INT_REALTIME,				  /*  4 irq 10 */
	SUN4M_INT_FLOPPY,				  /*  5 irq 11 */
	(SUN4M_INT_SERIAL | SUN4M_INT_KBDMS),	  	  /*  6 irq 12 */
	SUN4M_INT_MODULE_ERR,			  	  /*  7 irq 15 */
	SUN4M_INT_SBUS(0),				  /*  8 irq 2 */
	SUN4M_INT_SBUS(1),				  /*  9 irq 3 */
	SUN4M_INT_SBUS(2),				  /* 10 irq 5 */
	SUN4M_INT_SBUS(3),				  /* 11 irq 7 */
	SUN4M_INT_SBUS(4),				  /* 12 irq 9 */
	SUN4M_INT_SBUS(5),				  /* 13 irq 11 */
	SUN4M_INT_SBUS(6)				  /* 14 irq 13 */
};

inline unsigned long sun4m_get_irqmask(unsigned int irq)
{
	unsigned long mask;
    
	if (irq > 0x20) {
		/* OBIO/SBUS interrupts */
		irq &= 0x1f;
		mask = irq_mask[irq_xlate[irq]];
		if (!mask)
			printk("sun4m_get_irqmask: IRQ%d has no valid mask!\n",irq);
	} else {
		/* Soft Interrupts will come here.
		 * Currently there is no way to trigger them but I'm sure
		 * something could be cooked up.
		 */
		irq &= 0xf;
		mask = SUN4M_SOFT_INT(irq);
	}
	return mask;
}

static void sun4m_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	mask = sun4m_get_irqmask(irq_nr);
	save_and_cli(flags);
	if (irq_nr > 15)
		sun4m_interrupts->set = mask;
	else
		sun4m_interrupts->cpu_intregs[cpu].set = mask;
	restore_flags(flags);    
}

static void sun4m_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	/* Dreadful floppy hack. When we use 0x2b instead of
         * 0x0b the system blows (it starts to whistle!).
         * So we continue to use 0x0b. Fixme ASAP. --P3
         */
        if (irq_nr != 0x0b) {
		mask = sun4m_get_irqmask(irq_nr);
		save_and_cli(flags);
		if (irq_nr > 15)
			sun4m_interrupts->clear = mask;
		else
			sun4m_interrupts->cpu_intregs[cpu].clear = mask;
		restore_flags(flags);    
	} else {
		save_and_cli(flags);
		sun4m_interrupts->clear = SUN4M_INT_FLOPPY;
		restore_flags(flags);
	}
}

static unsigned long cpu_pil_to_imask[16] = {
/*0*/	0x00000000,
/*1*/	0x00000000,
/*2*/	SUN4M_INT_SBUS(0) | SUN4M_INT_VME(0),
/*3*/	SUN4M_INT_SBUS(1) | SUN4M_INT_VME(1),
/*4*/	SUN4M_INT_SCSI,
/*5*/	SUN4M_INT_SBUS(2) | SUN4M_INT_VME(2),
/*6*/	SUN4M_INT_ETHERNET,
/*7*/	SUN4M_INT_SBUS(3) | SUN4M_INT_VME(3),
/*8*/	SUN4M_INT_VIDEO,
/*9*/	SUN4M_INT_SBUS(4) | SUN4M_INT_VME(4) | SUN4M_INT_MODULE_ERR,
/*10*/	SUN4M_INT_REALTIME,
/*11*/	SUN4M_INT_SBUS(5) | SUN4M_INT_VME(5) | SUN4M_INT_FLOPPY,
/*12*/	SUN4M_INT_SERIAL | SUN4M_INT_KBDMS,
/*13*/	SUN4M_INT_AUDIO,
/*14*/	SUN4M_INT_E14,
/*15*/	0x00000000
};

/* We assume the caller is local cli()'d when these are called, or else
 * very bizarre behavior will result.
 */
static void sun4m_disable_pil_irq(unsigned int pil)
{
	sun4m_interrupts->set = cpu_pil_to_imask[pil];
}

static void sun4m_enable_pil_irq(unsigned int pil)
{
	sun4m_interrupts->clear = cpu_pil_to_imask[pil];
}

#ifdef __SMP__
static void sun4m_send_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].set = mask;
}

static void sun4m_clear_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].clear = mask;
}

static void sun4m_set_udt(int cpu)
{
	sun4m_interrupts->undirected_target = cpu;
}
#endif

#define OBIO_INTR	0x20
#define TIMER_IRQ  	(OBIO_INTR | 10)
#define PROFILE_IRQ	(OBIO_INTR | 14)

struct sun4m_timer_regs *sun4m_timers;
unsigned int lvl14_resolution = (((1000000/HZ) + 1) << 10);

static void sun4m_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4m_timers->l10_timer_limit;
}

static void sun4m_clear_profile_irq(int cpu)
{
	volatile unsigned int clear;
    
	clear = sun4m_timers->cpu_timers[cpu].l14_timer_limit;
}

static void sun4m_load_profile_irq(int cpu, unsigned int limit)
{
	sun4m_timers->cpu_timers[cpu].l14_timer_limit = limit;
}

char *sun4m_irq_itoa(unsigned int irq)
{
	static char buff[16];
	sprintf(buff, "%d", irq);
	return buff;
}

static void __init sun4m_init_timers(void (*counter_fn)(int, void *, struct pt_regs *))
{
	int reg_count, irq, cpu;
	struct linux_prom_registers cnt_regs[PROMREG_MAX];
	int obio_node, cnt_node;

	cnt_node = 0;
	if((obio_node =
	    prom_searchsiblings (prom_getchild(prom_root_node), "obio")) == 0 ||
	   (obio_node = prom_getchild (obio_node)) == 0 ||
	   (cnt_node = prom_searchsiblings (obio_node, "counter")) == 0) {
		prom_printf("Cannot find /obio/counter node\n");
		prom_halt();
	}
	reg_count = prom_getproperty(cnt_node, "reg",
				     (void *) cnt_regs, sizeof(cnt_regs));
	reg_count = (reg_count/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to the timer registers. */
	prom_apply_obio_ranges(cnt_regs, reg_count);
    
	cnt_regs[4].phys_addr = cnt_regs[reg_count-1].phys_addr;
	cnt_regs[4].reg_size = cnt_regs[reg_count-1].reg_size;
	cnt_regs[4].which_io = cnt_regs[reg_count-1].which_io;
	for(obio_node = 1; obio_node < 4; obio_node++) {
		cnt_regs[obio_node].phys_addr =
			cnt_regs[obio_node-1].phys_addr + PAGE_SIZE;
		cnt_regs[obio_node].reg_size = cnt_regs[obio_node-1].reg_size;
		cnt_regs[obio_node].which_io = cnt_regs[obio_node-1].which_io;
	}
    
	/* Map the per-cpu Counter registers. */
	sun4m_timers = sparc_alloc_io(cnt_regs[0].phys_addr, 0,
				      PAGE_SIZE*SUN4M_NCPUS, "counters_percpu",
				      cnt_regs[0].which_io, 0x0);
    
	/* Map the system Counter register. */
	sparc_alloc_io(cnt_regs[4].phys_addr, 0,
		       cnt_regs[4].reg_size,
		       "counters_system",
		       cnt_regs[4].which_io, 0x0);
    
	sun4m_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4m_timers->l10_cur_count;
	master_l10_limit = &sun4m_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (SA_INTERRUPT | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
    
	if(linux_num_cpus > 1) {
		for(cpu = 0; cpu < 4; cpu++)
			sun4m_timers->cpu_timers[cpu].l14_timer_limit = 0;
		sun4m_interrupts->set = SUN4M_INT_E14;
	} else {
		sun4m_timers->cpu_timers[0].l14_timer_limit = 0;
	}
#ifdef __SMP__
	{
		unsigned long flags;
		extern unsigned long lvl14_save[4];
		struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];

		/* For SMP we use the level 14 ticker, however the bootup code
		 * has copied the firmwares level 14 vector into boot cpu's
		 * trap table, we must fix this now or we get squashed.
		 */
		__save_and_cli(flags);
		trap_table->inst_one = lvl14_save[0];
		trap_table->inst_two = lvl14_save[1];
		trap_table->inst_three = lvl14_save[2];
		trap_table->inst_four = lvl14_save[3];
		local_flush_cache_all();
		__restore_flags(flags);
	}
#endif
}

void __init sun4m_init_IRQ(void)
{
	int ie_node,i;
	struct linux_prom_registers int_regs[PROMREG_MAX];
	int num_regs;
    
	__cli();
	if((ie_node = prom_searchsiblings(prom_getchild(prom_root_node), "obio")) == 0 ||
	   (ie_node = prom_getchild (ie_node)) == 0 ||
	   (ie_node = prom_searchsiblings (ie_node, "interrupt")) == 0) {
		prom_printf("Cannot find /obio/interrupt node\n");
		prom_halt();
	}
	num_regs = prom_getproperty(ie_node, "reg", (char *) int_regs,
				    sizeof(int_regs));
	num_regs = (num_regs/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to these registers. */
	prom_apply_obio_ranges(int_regs, num_regs);
    
	int_regs[4].phys_addr = int_regs[num_regs-1].phys_addr;
	int_regs[4].reg_size = int_regs[num_regs-1].reg_size;
	int_regs[4].which_io = int_regs[num_regs-1].which_io;
	for(ie_node = 1; ie_node < 4; ie_node++) {
		int_regs[ie_node].phys_addr = int_regs[ie_node-1].phys_addr + PAGE_SIZE;
		int_regs[ie_node].reg_size = int_regs[ie_node-1].reg_size;
		int_regs[ie_node].which_io = int_regs[ie_node-1].which_io;
	}

	/* Map the interrupt registers for all possible cpus. */
	sun4m_interrupts = sparc_alloc_io(int_regs[0].phys_addr, 0,
					  PAGE_SIZE*SUN4M_NCPUS, "interrupts_percpu",
					  int_regs[0].which_io, 0x0);
    
	/* Map the system interrupt control registers. */
	sparc_alloc_io(int_regs[4].phys_addr, 0,
		       int_regs[4].reg_size, "interrupts_system",
		       int_regs[4].which_io, 0x0);
    
	sun4m_interrupts->set = ~SUN4M_INT_MASKALL;
	for (i=0; i<linux_num_cpus; i++)
		sun4m_interrupts->cpu_intregs[i].clear = ~0x17fff;
    
	if (linux_num_cpus > 1) {
		/* system wide interrupts go to cpu 0, this should always
		 * be safe because it is guaranteed to be fitted or OBP doesn't
		 * come up
		 *
		 * Not sure, but writing here on SLAVIO systems may puke
		 * so I don't do it unless there is more than 1 cpu.
		 */
		irq_rcvreg = (unsigned long *)
				&sun4m_interrupts->undirected_target;
		sun4m_interrupts->undirected_target = 0;
	}
	BTFIXUPSET_CALL(enable_irq, sun4m_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4m_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, sun4m_enable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, sun4m_disable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4m_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, sun4m_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4m_load_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__irq_itoa, sun4m_irq_itoa, BTFIXUPCALL_NORM);
	init_timers = sun4m_init_timers;
#ifdef __SMP__
	BTFIXUPSET_CALL(set_cpu_int, sun4m_send_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4m_clear_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(set_irq_udt, sun4m_set_udt, BTFIXUPCALL_NORM);
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}
