/*
 * include/asm-arm/processor.h
 *
 * Copyright (C) 1995 Russell King
 */

#ifndef __ASM_ARM_PROCESSOR_H
#define __ASM_ARM_PROCESSOR_H

#include <linux/config.h>
/*
 * Default implementation of macro that returns current
 * instruction pointer ("program counter").
 */
#define current_text_addr() ({ __label__ _l; _l: &&_l;})

#define FP_SIZE 35

struct fp_hard_struct {
	unsigned int save[FP_SIZE];		/* as yet undefined */
};

struct fp_soft_struct {
	unsigned int save[FP_SIZE];		/* undefined information */
};

union fp_state {
	struct fp_hard_struct	hard;
	struct fp_soft_struct	soft;
};

typedef unsigned long mm_segment_t;		/* domain register	*/

#ifdef __KERNEL__

#define NR_DEBUGS	5

#include <asm/atomic.h>
#include <asm/ptrace.h>
#include <asm/arch/memory.h>
#include <asm/arch/processor.h>
#include <asm/proc/processor.h>

struct debug_info {
	int				nsaved;
	struct {
		unsigned long		address;
		unsigned long		insn;
	} bp[2];
};

struct thread_struct {
	atomic_t			refcount;
							/* fault info	  */
	unsigned long			address;
	unsigned long			trap_no;
	unsigned long			error_code;
							/* floating point */
	union fp_state			fpstate;
							/* debugging	  */
	struct debug_info		debug;
							/* context info	  */
	struct context_save_struct	*save;
	EXTRA_THREAD_STRUCT
};

#define INIT_MMAP \
{ &init_mm, 0, 0, NULL, PAGE_SHARED, VM_READ | VM_WRITE | VM_EXEC, 1, NULL, NULL }

#define INIT_THREAD  {				\
	ATOMIC_INIT(1),				\
	0,					\
	0,					\
	0,					\
	{ { { 0, }, }, },			\
	{ 0, },					\
	(struct context_save_struct *)0	\
	EXTRA_THREAD_STRUCT_INIT		\
}

/*
 * Return saved PC of a blocked thread.
 */
extern __inline__ unsigned long thread_saved_pc(struct thread_struct *t)
{
	return t->save ? pc_pointer(t->save->pc) : 0;
}

extern __inline__ unsigned long get_css_fp(struct thread_struct *t)
{
	return t->save ? t->save->fp : 0;
}

asmlinkage void ret_from_sys_call(void) __asm__("ret_from_sys_call");

extern __inline__ void init_thread_css(struct context_save_struct *save)
{
	*save = INIT_CSS;
	save->pc |= (unsigned long)ret_from_sys_call;
}

/* Forward declaration, a strange C thing */
struct task_struct;
struct mm_struct;

/* Free all resources held by a thread. */
extern void release_thread(struct task_struct *);

/* Copy and release all segment info associated with a VM */
#define copy_segments(tsk, mm)		do { } while (0)
#define release_segments(mm)		do { } while (0)
#define forget_segments()		do { } while (0)

unsigned long get_wchan(struct task_struct *p);

#define THREAD_SIZE	(8192)

extern struct task_struct *alloc_task_struct(void);
extern void __free_task_struct(struct task_struct *);
#define get_task_struct(p)	atomic_inc(&(p)->thread.refcount)
#define put_task_struct(p)	free_task_struct(p)
#define free_task_struct(p)					\
 do {								\
	if (atomic_dec_and_test(&(p)->thread.refcount))		\
		__free_task_struct((p));			\
 } while (0)

#define init_task	(init_task_union.task)
#define init_stack	(init_task_union.stack)

/*
 * Create a new kernel thread
 */
extern int kernel_thread(int (*fn)(void *), void *arg, unsigned long flags);

#endif

#endif /* __ASM_ARM_PROCESSOR_H */
