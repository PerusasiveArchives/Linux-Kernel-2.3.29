/* $Id: ptrace.c,v 1.13 1999/06/17 13:25:46 ralf Exp $
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Ross Biro
 * Copyright (C) Linus Torvalds
 * Copyright (C) 1994, 1995, 1996, 1997, 1998 Ralf Baechle
 * Copyright (C) 1996 David S. Miller
 */
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/user.h>

#include <asm/fp.h>
#include <asm/mipsregs.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <asm/system.h>
#include <asm/uaccess.h>

asmlinkage int sys_ptrace(long request, long pid, long addr, long data)
{
	struct task_struct *child;
	unsigned int flags;
	int res;

	lock_kernel();
#if 0
	printk("ptrace(r=%d,pid=%d,addr=%08lx,data=%08lx)\n",
	       (int) request, (int) pid, (unsigned long) addr,
	       (unsigned long) data);
#endif
	if (request == PTRACE_TRACEME) {
		/* are we already being traced? */
		if (current->flags & PF_PTRACED) {
			res = -EPERM;
			goto out;
		}
		/* set the ptrace bit in the process flags. */
		current->flags |= PF_PTRACED;
		res = 0;
		goto out;
	}
	if (pid == 1) {		/* you may not mess with init */
		res = -EPERM;
		goto out;
	}
	if (!(child = find_task_by_pid(pid))) {
		res = -ESRCH;
		goto out;
	}
	if (request == PTRACE_ATTACH) {
		if (child == current) {
			res = -EPERM;
			goto out;
		}
		if ((!child->dumpable ||
		    (current->uid != child->euid) ||
		    (current->uid != child->suid) ||
		    (current->uid != child->uid) ||
	 	    (current->gid != child->egid) ||
		    (current->gid != child->sgid) ||
	 	    (current->gid != child->gid) ||
		    (!cap_issubset(child->cap_permitted,
		                  current->cap_permitted)) ||
                    (current->gid != child->gid)) && !capable(CAP_SYS_PTRACE)){
			res = -EPERM;
			goto out;
		}
		/* the same process cannot be attached many times */
		if (child->flags & PF_PTRACED)
			goto out;
		child->flags |= PF_PTRACED;

		write_lock_irqsave(&tasklist_lock, flags);
		if (child->p_pptr != current) {
			REMOVE_LINKS(child);
			child->p_pptr = current;
			SET_LINKS(child);
		}
		write_unlock_irqrestore(&tasklist_lock, flags);

		send_sig(SIGSTOP, child, 1);
		res = 0;
		goto out;
	}
	if (!(child->flags & PF_PTRACED)) {
		res = -ESRCH;
		goto out;
	}
	if (child->state != TASK_STOPPED) {
		if (request != PTRACE_KILL) {
			res = -ESRCH;
			goto out;
		}
	}
	if (child->p_pptr != current) {
		res = -ESRCH;
		goto out;
	}

	switch (request) {
	case PTRACE_PEEKTEXT: /* read word at location addr. */ 
	case PTRACE_PEEKDATA: {
		unsigned long tmp;
		int copied;

		copied = access_process_vm(child, addr, &tmp, sizeof(tmp), 0);
		res = -EIO;
		if (copied != sizeof(tmp))
			goto out;
		res = put_user(tmp,(unsigned long *) data);

		goto out;
	}

	/* Read the word at location addr in the USER area.  */
	case PTRACE_PEEKUSR: {
		struct pt_regs *regs;
		unsigned long tmp;

		regs = (struct pt_regs *) ((unsigned long) child +
		       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
		tmp = 0;  /* Default return value. */

		switch(addr) {
		case 0 ... 31:
			tmp = regs->regs[addr];
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				unsigned long long *fregs;

				if (last_task_used_math == child) {
					enable_cp1();
					r4xx0_save_fp(child);
					disable_cp1();
					last_task_used_math = NULL;
				}
				fregs = (unsigned long long *)
					&child->tss.fpu.hard.fp_regs[0];
				tmp = (unsigned long) fregs[(addr - 32)];
			} else {
				tmp = -1;	/* FP not yet used  */
			}
			break;
		case PC:
			tmp = regs->cp0_epc;
			break;
		case CAUSE:
			tmp = regs->cp0_cause;
			break;
		case BADVADDR:
			tmp = regs->cp0_badvaddr;
			break;
		case MMHI:
			tmp = regs->hi;
			break;
		case MMLO:
			tmp = regs->lo;
			break;
		case FPC_CSR:
			tmp = child->tss.fpu.hard.control;
			break;
		case FPC_EIR:	/* implementation / version register */
			tmp = 0;	/* XXX */
			break;
		default:
			tmp = 0;
			res = -EIO;
			goto out;
		}
		res = put_user(tmp, (unsigned long *) data);
		goto out;
		}

	case PTRACE_POKETEXT: /* write the word at location addr. */
	case PTRACE_POKEDATA:
		res = 0;
		if (access_process_vm(child, addr, &data, sizeof(data), 1)
		    == sizeof(data))
			goto out;
		res = -EIO;
		goto out;

	case PTRACE_POKEUSR: {
		unsigned long long *fregs;
		struct pt_regs *regs;
		int res = 0;

		switch (addr) {
		case 0 ... 31:
			regs = (struct pt_regs *) ((unsigned long) child +
			       KERNEL_STACK_SIZE - 32 - sizeof(struct pt_regs));
			break;
		case FPR_BASE ... FPR_BASE + 31:
			if (child->used_math) {
				if (last_task_used_math == child) {
					enable_cp1();
					r4xx0_save_fp(child);
					disable_cp1();
					last_task_used_math = NULL;
				}
			} else {
				/* FP not yet used  */
				memset(&child->tss.fpu.hard, ~0,
				       sizeof(child->tss.fpu.hard));
				child->tss.fpu.hard.control = 0;
			}
			fregs = (unsigned long long *)
				&child->tss.fpu.hard.fp_regs[0];
			fregs[(addr - 32)] = (unsigned long long) data;
			break;
		case PC:
			regs->cp0_epc = data;
			break;
		case MMHI:
			regs->hi = data;
			break;
		case MMLO:
			regs->lo = data;
			break;
		case FPC_CSR:
			child->tss.fpu.hard.control = data;
			break;
		default:
			/* The rest are not allowed. */
			res = -EIO;
			break;
		}
		goto out;
		}

	case PTRACE_SYSCALL: /* continue and stop at next (return from) syscall */
	case PTRACE_CONT: { /* restart after signal. */
		if ((unsigned long) data > _NSIG) {
			res = -EIO;
			goto out;
		}
		if (request == PTRACE_SYSCALL)
			child->flags |= PF_TRACESYS;
		else
			child->flags &= ~PF_TRACESYS;
		child->exit_code = data;
		wake_up_process(child);
		res = data;
		goto out;
		}

	/*
	 * make the child exit.  Best I can do is send it a sigkill. 
	 * perhaps it should be put in the status that it wants to 
	 * exit.
	 */
	case PTRACE_KILL: {
		if (child->state != TASK_ZOMBIE) {
			child->exit_code = SIGKILL;
			wake_up_process(child);
		}
		res = 0;
		goto out;
		}

	case PTRACE_DETACH: { /* detach a process that was attached. */
		if ((unsigned long) data > _NSIG) {
			res = -EIO;
			goto out;
		}
		child->flags &= ~(PF_PTRACED|PF_TRACESYS);
		child->exit_code = data;
		REMOVE_LINKS(child);
		child->p_pptr = child->p_opptr;
		SET_LINKS(child);
		wake_up_process(child);
		res = 0;
		goto out;
		}

	default:
		res = -EIO;
		goto out;
	}
out:
	unlock_kernel();
	return res;
}

asmlinkage void syscall_trace(void)
{
	if ((current->flags & (PF_PTRACED|PF_TRACESYS))
			!= (PF_PTRACED|PF_TRACESYS))
		return;
	current->exit_code = SIGTRAP;
	current->state = TASK_STOPPED;
	notify_parent(current, SIGCHLD);
	schedule();
	/*
	 * this isn't the same as continuing with a signal, but it will do
	 * for normal use.  strace only continues with a signal if the
	 * stopping signal is not SIGTRAP.  -brl
	 */
	if (current->exit_code) {
		send_sig(current->exit_code, current, 1);
		current->exit_code = 0;
	}
}
