/*
 *  linux/arch/alpha/mm/fault.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <asm/io.h>

#define __EXTERN_INLINE inline
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#undef  __EXTERN_INLINE

#include <linux/signal.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#include <asm/system.h>
#include <asm/uaccess.h>

extern void die_if_kernel(char *,struct pt_regs *,long, unsigned long *);


/*
 * Force a new ASN for a task.
 */

#ifndef __SMP__
unsigned long last_asn = ASN_FIRST_VERSION;
#endif

void ev5_flush_tlb_current(struct mm_struct *mm)
{
	ev5_activate_mm(NULL, mm, smp_processor_id());
}


/*
 * This routine handles page faults.  It determines the address,
 * and the problem, and then passes it off to handle_mm_fault().
 *
 * mmcsr:
 *	0 = translation not valid
 *	1 = access violation
 *	2 = fault-on-read
 *	3 = fault-on-execute
 *	4 = fault-on-write
 *
 * cause:
 *	-1 = instruction fetch
 *	0 = load
 *	1 = store
 *
 * Registers $9 through $15 are saved in a block just prior to `regs' and
 * are saved and restored around the call to allow exception code to
 * modify them.
 */

/* Macro for exception fixup code to access integer registers.  */
#define dpf_reg(r)							\
	(((unsigned long *)regs)[(r) <= 8 ? (r) : (r) <= 15 ? (r)-16 :	\
				 (r) <= 18 ? (r)+8 : (r)-10])

asmlinkage void
do_page_fault(unsigned long address, unsigned long mmcsr,
	      long cause, struct pt_regs *regs)
{
	struct vm_area_struct * vma;
	struct mm_struct *mm = current->mm;
	unsigned int fixup;
	int fault;

	/* As of EV6, a load into $31/$f31 is a prefetch, and never faults
	   (or is suppressed by the PALcode).  Support that for older CPUs
	   by ignoring such an instruction.  */
	if (cause == 0) {
		unsigned int insn;
		__get_user(insn, (unsigned int *)regs->pc);
		if ((insn >> 21 & 0x1f) == 0x1f &&
		    /* ldq ldl ldt lds ldg ldf ldwu ldbu */
		    (1ul << (insn >> 26) & 0x30f00001400ul)) {
			regs->pc += 4;
			return;
		}
	}

	/* If we're in an interrupt context, or have no user context,
	   we must not take the fault.  */
	if (!mm || in_interrupt())
		goto no_context;

	down(&mm->mmap_sem);
	vma = find_vma(mm, address);
	if (!vma)
		goto bad_area;
	if (vma->vm_start <= address)
		goto good_area;
	if (!(vma->vm_flags & VM_GROWSDOWN))
		goto bad_area;
	if (expand_stack(vma, address))
		goto bad_area;
/*
 * Ok, we have a good vm_area for this memory access, so
 * we can handle it..
 */
good_area:
	if (cause < 0) {
		if (!(vma->vm_flags & VM_EXEC))
			goto bad_area;
	} else if (!cause) {
		/* Allow reads even for write-only mappings */
		if (!(vma->vm_flags & (VM_READ | VM_WRITE)))
			goto bad_area;
	} else {
		if (!(vma->vm_flags & VM_WRITE))
			goto bad_area;
	}

	/*
	 * If for any reason at all we couldn't handle the fault,
	 * make sure we exit gracefully rather than endlessly redo
	 * the fault.
	 */
	fault = handle_mm_fault(current, vma, address, cause > 0);
	up(&mm->mmap_sem);

	if (fault < 0)
		goto out_of_memory;
	if (fault == 0)
		goto do_sigbus;

	return;

/*
 * Something tried to access memory that isn't in our memory map..
 * Fix it, but check if it's kernel or user first..
 */
bad_area:
	up(&mm->mmap_sem);

	if (user_mode(regs)) {
		force_sig(SIGSEGV, current);
		return;
	}

no_context:
	/* Are we prepared to handle this fault as an exception?  */
	if ((fixup = search_exception_table(regs->pc)) != 0) {
		unsigned long newpc;
		newpc = fixup_exception(dpf_reg, fixup, regs->pc);
		printk("%s: Exception at [<%lx>] (%lx)\n",
		       current->comm, regs->pc, newpc);
		regs->pc = newpc;
		return;
	}

/*
 * Oops. The kernel tried to access some bad page. We'll have to
 * terminate things with extreme prejudice.
 */
	printk(KERN_ALERT "Unable to handle kernel paging request at "
	       "virtual address %016lx\n", address);
	die_if_kernel("Oops", regs, cause, (unsigned long*)regs - 16);
	do_exit(SIGKILL);

/*
 * We ran out of memory, or some other thing happened to us that made
 * us unable to handle the page fault gracefully.
 */
out_of_memory:
	printk(KERN_ALERT "VM: killing process %s(%d)\n",
	       current->comm, current->pid);
	if (!user_mode(regs))
		goto no_context;
	do_exit(SIGKILL);

do_sigbus:
	/*
	 * Send a sigbus, regardless of whether we were in kernel
	 * or user mode.
	 */
	force_sig(SIGBUS, current);
	if (!user_mode(regs))
		goto no_context;
	return;
}
