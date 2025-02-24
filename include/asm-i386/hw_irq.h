#ifndef _ASM_HW_IRQ_H
#define _ASM_HW_IRQ_H

/*
 *	linux/include/asm/hw_irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	moved some of the old arch/i386/kernel/irq.h to here. VY
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 */

#include <asm/irq.h>

/*
 * IDT vectors usable for external interrupt sources start
 * at 0x20:
 */
#define FIRST_EXTERNAL_VECTOR	0x20

#define SYSCALL_VECTOR		0x80

/*
 * Vectors 0x20-0x2f are used for ISA interrupts.
 */

/*
 * Special IRQ vectors used by the SMP architecture, 0x30-0x4f
 *
 *  some of the following vectors are 'rare', they are merged
 *  into a single vector (CALL_FUNCTION_VECTOR) to save vector space.
 *  TLB, reschedule and local APIC vectors are performance-critical.
 */
#define INVALIDATE_TLB_VECTOR	0x30
#define LOCAL_TIMER_VECTOR	0x31
#define RESCHEDULE_VECTOR	0x40

/* 'rare' vectors: */
#define CALL_FUNCTION_VECTOR	0x41

/*
 * These IRQs should never really happen on perfect hardware running
 * a perfect kernel, but we nevertheless print a message to catch the
 * rest ;) Subtle, the APIC architecture mandates the spurious vector
 * to have bits 0-3 set to 1. Note that these vectors do not occur
 * normally, so we violate the 'only 2 vectors per priority level'
 * rule here.
 */
#define SPURIOUS_APIC_VECTOR	0x3f
#define ERROR_APIC_VECTOR	0x43

/*
 * First APIC vector available to drivers: (vectors 0x51-0xfe)
 * we start at 0x51 to spread out vectors between priority levels
 * evenly. (note that 0x80 is the syscall vector)
 */
#define IRQ0_TRAP_VECTOR	0x51

extern int irq_vector[NR_IRQS];
#define IO_APIC_VECTOR(irq)	irq_vector[irq]

/*
 * Various low-level irq details needed by irq.c, process.c,
 * time.c, io_apic.c and smp.c
 *
 * Interrupt entry/exit code at both C and assembly level
 */

extern void no_action(int cpl, void *dev_id, struct pt_regs *regs);
extern void mask_irq(unsigned int irq);
extern void unmask_irq(unsigned int irq);
extern void disable_8259A_irq(unsigned int irq);
extern void enable_8259A_irq(unsigned int irq);
extern int i8259A_irq_pending(unsigned int irq);
extern void make_8259A_irq(unsigned int irq);
extern void init_8259A(int aeoi);
extern void FASTCALL(send_IPI_self(int vector));
extern void init_VISWS_APIC_irqs(void);
extern void setup_IO_APIC(void);
extern int IO_APIC_get_PCI_irq_vector(int bus, int slot, int fn);
extern void send_IPI(int dest, int vector);
extern void init_pic_mode(void);
extern void print_IO_APIC(void);

extern unsigned long io_apic_irqs;
extern volatile unsigned long irq_err_count;

extern char _stext, _etext;

#define MAX_IRQ_SOURCES 128
#define MAX_MP_BUSSES 32
enum mp_bustype {
	MP_BUS_ISA,
	MP_BUS_EISA,
	MP_BUS_PCI
};
extern int mp_bus_id_to_type [MAX_MP_BUSSES];
extern int mp_bus_id_to_pci_bus [MAX_MP_BUSSES];


#ifdef __SMP__
#define IO_APIC_IRQ(x) (((x) >= 16) || ((1<<(x)) & io_apic_irqs))

#else

#define IO_APIC_IRQ(x)	(0)

#endif

#define __STR(x) #x
#define STR(x) __STR(x)

#define SAVE_ALL \
	"cld\n\t" \
	"pushl %es\n\t" \
	"pushl %ds\n\t" \
	"pushl %eax\n\t" \
	"pushl %ebp\n\t" \
	"pushl %edi\n\t" \
	"pushl %esi\n\t" \
	"pushl %edx\n\t" \
	"pushl %ecx\n\t" \
	"pushl %ebx\n\t" \
	"movl $" STR(__KERNEL_DS) ",%edx\n\t" \
	"movl %dx,%ds\n\t" \
	"movl %dx,%es\n\t"

#define IRQ_NAME2(nr) nr##_interrupt(void)
#define IRQ_NAME(nr) IRQ_NAME2(IRQ##nr)

#define GET_CURRENT \
	"movl %esp, %ebx\n\t" \
	"andl $-8192, %ebx\n\t"

#ifdef __SMP__

/*
 *	SMP has a few special interrupts for IPI messages
 */

	/* there is a second layer of macro just to get the symbolic
	   name for the vector evaluated. This change is for RTLinux */
#define BUILD_SMP_INTERRUPT(x,v) XBUILD_SMP_INTERRUPT(x,v)
#define XBUILD_SMP_INTERRUPT(x,v)\
asmlinkage void x(void); \
asmlinkage void call_##x(void); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(x) ":\n\t" \
	"pushl $"#v"\n\t" \
	SAVE_ALL \
	SYMBOL_NAME_STR(call_##x)":\n\t" \
	"call "SYMBOL_NAME_STR(smp_##x)"\n\t" \
	"jmp ret_from_intr\n");

#define BUILD_SMP_TIMER_INTERRUPT(x,v) XBUILD_SMP_TIMER_INTERRUPT(x,v)
#define XBUILD_SMP_TIMER_INTERRUPT(x,v) \
asmlinkage void x(struct pt_regs * regs); \
asmlinkage void call_##x(void); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(x) ":\n\t" \
	"pushl $"#v"\n\t" \
	SAVE_ALL \
	"movl %esp,%eax\n\t" \
	"pushl %eax\n\t" \
	SYMBOL_NAME_STR(call_##x)":\n\t" \
	"call "SYMBOL_NAME_STR(smp_##x)"\n\t" \
	"addl $4,%esp\n\t" \
	"jmp ret_from_intr\n");

#endif /* __SMP__ */

#define BUILD_COMMON_IRQ() \
asmlinkage void call_do_IRQ(void); \
__asm__( \
	"\n" __ALIGN_STR"\n" \
	"common_interrupt:\n\t" \
	SAVE_ALL \
	"pushl $ret_from_intr\n\t" \
	SYMBOL_NAME_STR(call_do_IRQ)":\n\t" \
	"jmp "SYMBOL_NAME_STR(do_IRQ));

/* 
 * subtle. orig_eax is used by the signal code to distinct between
 * system calls and interrupted 'random user-space'. Thus we have
 * to put a negative value into orig_eax here. (the problem is that
 * both system calls and IRQs want to have small integer numbers in
 * orig_eax, and the syscall code has won the optimization conflict ;)
 *
 * Subtle as a pigs ear.  VY
 */

#define BUILD_IRQ(nr) \
asmlinkage void IRQ_NAME(nr); \
__asm__( \
"\n"__ALIGN_STR"\n" \
SYMBOL_NAME_STR(IRQ) #nr "_interrupt:\n\t" \
	"pushl $"#nr"-256\n\t" \
	"jmp common_interrupt");

/*
 * x86 profiling function, SMP safe. We might want to do this in
 * assembly totally?
 */
static inline void x86_do_profile (unsigned long eip)
{
	if (prof_buffer && current->pid) {
		eip -= (unsigned long) &_stext;
		eip >>= prof_shift;
		/*
		 * Don't ignore out-of-bounds EIP values silently,
		 * put them into the last histogram slot, so if
		 * present, they will show up as a sharp peak.
		 */
		if (eip > prof_len-1)
			eip = prof_len-1;
		atomic_inc((atomic_t *)&prof_buffer[eip]);
	}
}

#ifdef __SMP__ /*more of this file should probably be ifdefed SMP */
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {
	if (IO_APIC_IRQ(i))
		send_IPI_self(IO_APIC_VECTOR(i));
}
#else
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}
#endif

#endif /* _ASM_HW_IRQ_H */
