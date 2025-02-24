/* $Id: rtrap.S,v 1.52 1999/08/14 03:51:18 anton Exp $
 * rtrap.S: Return from Sparc trap low-level code.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <asm/cprefix.h>
#include <asm/page.h>
#include <asm/ptrace.h>
#include <asm/psr.h>
#include <asm/asi.h>
#include <asm/smp.h>
#include <asm/contregs.h>
#include <asm/winmacro.h>
#include <asm/asmmacro.h>

#define t_psr     l0
#define t_pc      l1
#define t_npc     l2
#define t_wim     l3
#define twin_tmp1 l4
#define glob_tmp  g4
#define curptr    g6

	/* 7 WINDOW SPARC PATCH INSTRUCTIONS */
	.globl	rtrap_7win_patch1, rtrap_7win_patch2, rtrap_7win_patch3
	.globl	rtrap_7win_patch4, rtrap_7win_patch5
rtrap_7win_patch1:	srl	%t_wim, 0x6, %glob_tmp
rtrap_7win_patch2:	and	%glob_tmp, 0x7f, %glob_tmp
rtrap_7win_patch3:	srl	%g1, 7, %g2
rtrap_7win_patch4:	srl	%g2, 6, %g2
rtrap_7win_patch5:	and	%g1, 0x7f, %g1
	/* END OF PATCH INSTRUCTIONS */

	/* We need to check for a few things which are:
	 * 1) The need to call schedule() because this
	 *    processes quantum is up.
	 * 2) Pending signals for this process, if any
	 *    exist we need to call do_signal() to do
	 *    the needy.
	 *
	 * Else we just check if the rett would land us
	 * in an invalid window, if so we need to grab
	 * it off the user/kernel stack first.
	 */

	.globl	ret_trap_entry, rtrap_patch1, rtrap_patch2
	.globl	rtrap_patch3, rtrap_patch4, rtrap_patch5
	.globl	C_LABEL(ret_trap_lockless_ipi)
ret_trap_entry:
	sethi	%hi(C_LABEL(bh_active)), %l3
	sethi	%hi(C_LABEL(bh_mask)), %l4
	ld	[%l4 + %lo(C_LABEL(bh_mask))], %g5
	ld	[%l3 + %lo(C_LABEL(bh_active))], %g4
	andcc	%g4, %g5, %g0
	be	C_LABEL(ret_trap_lockless_ipi)
	 nop
	call	C_LABEL(do_bottom_half)
	 nop

C_LABEL(ret_trap_lockless_ipi):
	andcc	%t_psr, PSR_PS, %g0
	be	1f
	 nop

	wr	%t_psr, 0x0, %psr
	b	ret_trap_kernel
	 nop

1:
	ld	[%curptr + AOFF_task_need_resched], %g2
	orcc	%g2, %g0, %g0
	be	signal_p
	 ld	[%curptr + AOFF_task_sigpending], %g2

	call	C_LABEL(schedule)
	 nop

	ld	[%curptr + AOFF_task_sigpending], %g2
signal_p:
	cmp	%g2, 0
	bz,a	ret_trap_continue
	 ld	[%sp + REGWIN_SZ + PT_PSR], %t_psr

	clr	%o0
	mov	%l5, %o2
	mov	%l6, %o3
	call	C_LABEL(do_signal)
	 add	%sp, REGWIN_SZ, %o1	! pt_regs ptr

	/* Fall through. */
	ld	[%sp + REGWIN_SZ + PT_PSR], %t_psr
	clr	%l6
ret_trap_continue:
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	ld	[%curptr + AOFF_task_thread + AOFF_thread_w_saved], %twin_tmp1
	orcc	%g0, %twin_tmp1, %g0
	be	ret_trap_nobufwins
	 nop

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	mov	1, %o1
	call	C_LABEL(try_to_clear_window_buffer)
	 add	%sp, REGWIN_SZ, %o0

	b	signal_p
	 ld	[%curptr + AOFF_task_sigpending], %g2

ret_trap_nobufwins:
	/* Load up the user's out registers so we can pull
	 * a window from the stack, if necessary.
	 */
	LOAD_PT_INS(sp)

	/* If there are already live user windows in the
	 * set we can return from trap safely.
	 */
	ld	[%curptr + AOFF_task_thread + AOFF_thread_uwinmask], %twin_tmp1
	orcc	%g0, %twin_tmp1, %g0
	bne	ret_trap_userwins_ok
	 nop
	
		/* Calculate new %wim, we have to pull a register
		 * window from the users stack.
		 */
ret_trap_pull_one_window:
		rd	%wim, %t_wim
		sll	%t_wim, 0x1, %twin_tmp1
rtrap_patch1:	srl	%t_wim, 0x7, %glob_tmp
		or	%glob_tmp, %twin_tmp1, %glob_tmp
rtrap_patch2:	and	%glob_tmp, 0xff, %glob_tmp

		wr	%glob_tmp, 0x0, %wim

				/* Here comes the architecture specific 
				 * branch to the user stack checking routine
				 * for return from traps.
				 */
				.globl	C_LABEL(rtrap_mmu_patchme)
C_LABEL(rtrap_mmu_patchme):	b	C_LABEL(sun4c_rett_stackchk)
				 andcc	%fp, 0x7, %g0	

ret_trap_userwins_ok:
	LOAD_PT_PRIV(sp, t_psr, t_pc, t_npc)
	or	%t_pc, %t_npc, %g2
	andcc	%g2, 0x3, %g0
	be	1f
	 nop

	b	ret_trap_unaligned_pc
	 add	%sp, REGWIN_SZ, %o0

1:
	LOAD_PT_YREG(sp, g1)
	LOAD_PT_GLOBALS(sp)

	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc
	
ret_trap_unaligned_pc:
	ld	[%sp + REGWIN_SZ + PT_PC], %o1
	ld	[%sp + REGWIN_SZ + PT_NPC], %o2
	ld	[%sp + REGWIN_SZ + PT_PSR], %o3

	wr	%t_wim, 0x0, %wim		! or else...

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(do_memaccess_unaligned)
	 nop

	b	signal_p
	 ld	[%curptr + AOFF_task_sigpending], %g2

ret_trap_kernel:
		/* Will the rett land us in the invalid window? */
		mov	2, %g1
		sll	%g1, %t_psr, %g1
rtrap_patch3:	srl	%g1, 8, %g2
		or	%g1, %g2, %g1
		rd	%wim, %g2
		andcc	%g2, %g1, %g0
		be	1f		! Nope, just return from the trap
		 sll	%g2, 0x1, %g1

		/* We have to grab a window before returning. */
rtrap_patch4:	srl	%g2, 7,  %g2
		or	%g1, %g2, %g1
rtrap_patch5:	and	%g1, 0xff, %g1

	wr	%g1, 0x0, %wim

	/* Grrr, make sure we load from the right %sp... */
	LOAD_PT_ALL(sp, t_psr, t_pc, t_npc, g1)

	restore	%g0, %g0, %g0
	LOAD_WINDOW(sp)
	b	2f
	 save	%g0, %g0, %g0

	/* Reload the entire frame in case this is from a
	 * kernel system call or whatever...
	 */
1:
	LOAD_PT_ALL(sp, t_psr, t_pc, t_npc, g1)
2:
	wr	%t_psr, 0x0, %psr
	WRITE_PAUSE

	jmp	%t_pc
	rett	%t_npc

ret_trap_user_stack_is_bolixed:
	wr	%t_wim, 0x0, %wim

	wr	%t_psr, PSR_ET, %psr
	WRITE_PAUSE

	call	C_LABEL(window_ret_fault)
	 add	%sp, REGWIN_SZ, %o0

	b	signal_p
	 ld	[%curptr + AOFF_task_sigpending], %g2

	.globl	C_LABEL(sun4c_rett_stackchk)
C_LABEL(sun4c_rett_stackchk):
	be	1f
	 and	%fp, 0xfff, %g1		! delay slot

	b	ret_trap_user_stack_is_bolixed + 0x4
	 wr	%t_wim, 0x0, %wim

	/* See if we have to check the sanity of one page or two */
1:
	add	%g1, 0x38, %g1
	sra	%fp, 29, %g2
	add	%g2, 0x1, %g2
	andncc	%g2, 0x1, %g0
	be	1f
	 andncc	%g1, 0xff8, %g0

	/* %sp is in vma hole, yuck */
	b	ret_trap_user_stack_is_bolixed + 0x4
	 wr	%t_wim, 0x0, %wim

1:
	be	sun4c_rett_onepage	/* Only one page to check */
	 lda	[%fp] ASI_PTE, %g2

sun4c_rett_twopages:
	add	%fp, 0x38, %g1
	sra	%g1, 29, %g2
	add	%g2, 0x1, %g2
	andncc	%g2, 0x1, %g0
	be	1f
	 lda	[%g1] ASI_PTE, %g2

	/* Second page is in vma hole */
	b	ret_trap_user_stack_is_bolixed + 0x4
	 wr	%t_wim, 0x0, %wim

1:
	srl	%g2, 29, %g2
	andcc	%g2, 0x4, %g0
	bne	sun4c_rett_onepage
	 lda	[%fp] ASI_PTE, %g2

	/* Second page has bad perms */
	b	ret_trap_user_stack_is_bolixed + 0x4
	 wr	%t_wim, 0x0, %wim

sun4c_rett_onepage:
	srl	%g2, 29, %g2
	andcc	%g2, 0x4, %g0
	bne,a	1f
	 restore %g0, %g0, %g0

	/* A page had bad page permissions, losing... */
	b	ret_trap_user_stack_is_bolixed + 0x4
	 wr	%t_wim, 0x0, %wim

	/* Whee, things are ok, load the window and continue. */
1:
	LOAD_WINDOW(sp)

	b	ret_trap_userwins_ok
	 save	%g0, %g0, %g0

	.globl	C_LABEL(srmmu_rett_stackchk)
C_LABEL(srmmu_rett_stackchk):
	bne	ret_trap_user_stack_is_bolixed
	 GET_PAGE_OFFSET(g1)
	cmp	%g1, %fp
	bleu	ret_trap_user_stack_is_bolixed
	 mov	AC_M_SFSR, %g1
	lda	[%g1] ASI_M_MMUREGS, %g0

	lda	[%g0] ASI_M_MMUREGS, %g1
	or	%g1, 0x2, %g1
	sta	%g1, [%g0] ASI_M_MMUREGS

	restore	%g0, %g0, %g0

	LOAD_WINDOW(sp)

	save	%g0, %g0, %g0

	andn	%g1, 0x2, %g1
	sta	%g1, [%g0] ASI_M_MMUREGS

	mov	AC_M_SFAR, %g2
	lda	[%g2] ASI_M_MMUREGS, %g2

	mov	AC_M_SFSR, %g1
	lda	[%g1] ASI_M_MMUREGS, %g1
	andcc	%g1, 0x2, %g0
	be	ret_trap_userwins_ok
	 nop

	b,a	ret_trap_user_stack_is_bolixed
