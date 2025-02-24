/* $Id: entry.S,v 1.19 1999/10/31 13:19:35 gniibe Exp gniibe $
 *
 *  linux/arch/sh/entry.S
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 */

#include <linux/sys.h>
#include <linux/linkage.h>
#include <linux/config.h>

! NOTE:
! GNU as (as of 2.9.1) changes bf/s into bt/s and bra, when the address
! to be jumped is too far, but it causes illegal slot exception.

/*	
 * entry.S contains the system-call and fault low-level handling routines.
 * This also contains the timer-interrupt handler, as well as all interrupts
 * and faults that can result in a task-switch.
 *
 * NOTE: This code handles signal-recognition, which happens every time
 * after a timer-interrupt and after each system call.
 *
 * Stack layout in 'ret_from_syscall':
 * 	ptrace needs to have all regs on the stack.
 *	if the order here is changed, it needs to be
 *	updated in ptrace.c and ptrace.h
 *
 *	syscall #
 *	ssr
 *	r15 = stack pointer
 *	r0
 *      ...
 *	r14
 *	gbr
 *	mach
 *	macl
 *	pr
 *	spc
 *
 */

/*
 * these are offsets into the task-struct.
 */
state		=  0
flags		=  4
sigpending	=  8
addr_limit	= 12
need_resched	= 20

PF_TRACESYS  = 0x20

ENOSYS = 38

#if defined(__sh3__)
TRA     = 0xffffffd0
EXPEVT  = 0xffffffd4
#ifdef CONFIG_CPU_SUBTYPE_SH7709
INTEVT  = 0xa4000000		! INTEVTE2(0xa4000000)
#else
INTEVT  = 0xffffffd8
#endif
MMU_TEA = 0xfffffffc		! TLB Exception Address Register
#elif defined(__SH4__)
TRA     = 0xff000020
EXPEVT  = 0xff000024
INTEVT  = 0xff000028
MMU_TEA = 0xff00000c		! TLB Exception Address Register
#endif

/* Offsets to the stack */
SYSCALL_NR =  0
SR	   =  4
SP	   =  8
R0         =  12

#define k0	r0
#define k1	r1
#define k2	r2
#define k3	r3

#define kernel_sp	r4	/* r4_bank1 */
#define ksp		r4_bank	/* r4_bank1 */
#define k_ex_code	r2_bank	/* r2_bank1 */

/* Kernel mode register usage:
	k0	scratch
	k1	scratch
	k2	scratch (Exception code)
	k3	scratch (Return address)
	k4	Stack base = current+8192
	k5	reserved
	k6	reserved
	k7	reserved
*/

!
! TLB Miss / Initial Page write exception handling
!			_and_
! TLB hits, but the access violate the protection.
! It can be valid access, such as stack grow and/or C-O-W.
!
!
! Find the pmd/pte entry and loadtlb
! If it's not found, cause address error (SEGV)
!
! Although this could be written in assembly language (and it'd be faster),
! this first version depends *much* on C implementation.
!

#define DO_FAULT(write)			\
	mov.l	4f,r0;			\
	mov.l	@r0,r6; 		\
	/* STI */ 			\
	mov.l	3f,r1; 			\
	stc	sr,r0; 			\
	and	r1,r0; 			\
	ldc	r0,sr; 			\
	/*    */ 			\
	mov	r15,r4; 		\
	mov.l	2f,r0; 			\
	jmp	@r0; 			\
	 mov	#write,r5;

	.balign	4
tlb_protection_violation_load:
tlb_miss_load:
	mov	#-1,r0
	mov.l	r0,@r15		! syscall nr = -1
	DO_FAULT(0)

	.balign	4
tlb_protection_violation_store:
tlb_miss_store:
initial_page_write:
	mov	#-1,r0
	mov.l	r0,@r15		! syscall nr = -1
	DO_FAULT(1)

	.balign 4
2:	.long	SYMBOL_NAME(do_page_fault)
3:	.long	0xefffffff	! BL=0
4:	.long	MMU_TEA

#ifdef CONFIG_DEBUG_KERNEL_WITH_GDB_STUB
	.balign	4
	/* Unwind the stack and jmp to the debug entry */
debug:
	add	#4,r15		! skip syscall number
	mov.l	@r15+,r11	! SSR
	mov.l	@r15+,r10	! original stack
	mov.l	@r15+,r0
	mov.l	@r15+,r1
	mov.l	@r15+,r2
	mov.l	@r15+,r3
	mov.l	@r15+,r4
	mov.l	@r15+,r5
	mov.l	@r15+,r6
	mov.l	@r15+,r7
	stc	sr,r14
	mov.l	8f,r9			! BL =1, RB=1
	or	r9,r14
	ldc	r14,sr			! here, change the register bank
	mov	r10,k0
	mov	r11,k1
	mov.l	@r15+,r8
	mov.l	@r15+,r9
	mov.l	@r15+,r10
	mov.l	@r15+,r11
	mov.l	@r15+,r12
	mov.l	@r15+,r13
	mov.l	@r15+,r14
	ldc.l	@r15+,gbr
	lds.l	@r15+,mach
	lds.l	@r15+,macl
	lds.l	@r15+,pr
	ldc.l	@r15+,spc
	mov	k0,r15
	!
	mov.l	9f,k0
	jmp	@k0
	 ldc	k1,ssr
	.balign	4
8:	.long	0x300000f0
9:	.long	0xa0000100
#endif

	.balign	4
error:	
	! STI
	mov.l	2f,r1
	stc	sr,r0
	and	r1,r0
	ldc	r0,sr
	!
	mov.l	1f,r1
	mov	#-1,r0
	jmp	@r1
	 mov.l	r0,@r15		! syscall nr = -1
	.balign	4
1:	.long	SYMBOL_NAME(do_exception_error)
2:	.long	0xefffffff	! BL=0

reschedule:
	mova	SYMBOL_NAME(ret_from_syscall),r0
	mov.l	1f,r1
	jmp	@r1
	 lds	r0,pr
	.balign	4
1:	.long	SYMBOL_NAME(schedule)

badsys:	mov	#-ENOSYS,r0
	rts			! go to ret_from_syscall..
	 mov.l	r0,@(R0,r15)

signal_return:
	! We can reach here from an interrupt handler,
	! so, we need to unblock interrupt.
	/*  STI */
	mov.l	1f,r1
	stc	sr,r0
	and	r1,r0
	ldc	r0,sr
	!
	mov	r15,r4
	mov	#0,r5
	mov.l	2f,r1
	mova	restore_all,r0
	jmp	@r1
	 lds	r0,pr
	.balign	4
1:	.long	0xefffffff		! BL=0
2:	.long	SYMBOL_NAME(do_signal)

!
!
!
ENTRY(ret_from_fork)
	bra	SYMBOL_NAME(ret_from_syscall)
	 add	#4,r15		! pop down bogus r0 (see switch_to MACRO)

!
! The immediate value of "trapa" indicates the number of arguments
! placed on the stack.
!
! Note that TRA register contains the value = Imm x 4.
!
system_call:
	mov.l	1f,r2
	mov.l	@r2,r8
	!
	! DEBUG DEBUG
	! mov.l	led,r1
	! mov	r0,r2
	! mov.b	r2,@r1
	!
#ifdef CONFIG_DEBUG_KERNEL_WITH_GDB_STUB
	mov	#0x20,r1
	extu.b	r1,r1
	shll2	r1
	cmp/hs	r1,r8
	bt	debug
#endif
	! STI
	mov.l	2f,r1
	stc	sr,r2
	and	r1,r2
	ldc	r2,sr
	!
	mov.l	__n_sys,r1
	cmp/ge	r1,r0
	bt/s	badsys
	 mov	r0,r2
	!
	stc	ksp,r1		!
	mov.l	__tsk_flags,r0	!
	add	r0,r1		!
	mov.l	@r1,r0		! Is it trace?
	tst	#PF_TRACESYS,r0
	bt	5f
	!                     Trace system call
	mov	#-ENOSYS,r1
	mov.l	r1,@(R0,r15)
	mov.l	3f,r1
	jsr	@r1
	 nop
	mova	4f,r0
	bra	6f
	 lds	r0,pr
	!
5:	mova	ret,r0		! normal case
	lds	r0,pr
	!			Build the stack frame if TRA > 0
	!
6:	mov	r2,r3
	mov	r8,r2
	cmp/pl	r8
	bf	9f
	mov.l	@(SP,r15),r0	! get original stack
7:	add	#-4,r8
8:	mov.l	@(r0,r8),r1	! May cause address error exception..
	mov.l	r1,@-r15
	cmp/pl	r8
	bt	7b
	!
9:	mov	r3,r0
	shll2	r0		! x4
	mov.l	__sct,r1
	add	r1,r0
	mov.l	@r0,r1
	jmp	@r1
	 mov	r2,r8

	! In case of trace
	.balign	4
4:	add	r8,r15		! pop off the arguments
	mov.l	r0,@(R0,r15)	! save the return value
	mov.l	3f,r1
	mova	SYMBOL_NAME(ret_from_syscall),r0
	jmp	@r1
	 lds	r0,pr
	.balign	4
3:	.long	SYMBOL_NAME(syscall_trace)
2:	.long	0xefffffff	! BL=0
1:	.long	TRA
led:	.long	0xa8000000	! For my board -- gN

	.section	.fixup,"ax"
fixup_syscall_argerr:
	rts
	 mov.l	1f,r0
1:	.long	-22	! -EINVAL
.previous

	.section	__ex_table, "a"
	.balign	4
	.long	8b,fixup_syscall_argerr
.previous


ENTRY(ret_from_irq)
	mov.l	@(SR,r15),r0	! get original stack
	shll	r0
	shll	r0		! kernel space?
	bt	restore_all	! Yes, it's from kernel, go back soon
	! XXX: Is it better to run through bottom half?
	! In such a case, we should go "ret_from_syscall" instead
	bra	ret_with_reschedule
	 nop

ret:	add	r8,r15		! pop off the arguments
	mov.l	r0,@(R0,r15)	! save the return value
	/* fall through */

ENTRY(ret_from_syscall)
	mov.l	__bh_mask,r0
	mov.l	@r0,r1
	mov.l	__bh_active,r0
	mov.l	@r0,r2
	tst	r2,r1
	bt	ret_with_reschedule
handle_bottom_half:
	mov.l	__dbh,r0
	jsr	@r0
	 nop
ret_with_reschedule:
	stc	ksp,r1
	mov.l	__minus8192,r0
	add	r0,r1
	mov.l	@(need_resched,r1),r0
	tst	#0xff,r0
	bf	reschedule
	mov.l	@(sigpending,r1),r0
	tst	#0xff,r0
	bf	signal_return
	!
restore_all:
	add	#4,r15		! skip syscall number
	mov.l	@r15+,r11	! SSR
	mov.l	@r15+,r10	! original stack
	mov.l	@r15+,r0
	mov.l	@r15+,r1
	mov.l	@r15+,r2
	mov.l	@r15+,r3
	mov.l	@r15+,r4
	mov.l	@r15+,r5
	mov.l	@r15+,r6
	mov.l	@r15+,r7
	stc	sr,r14
	mov.l	__blrb_flags,r9		! BL =1, RB=1
	or	r9,r14
	ldc	r14,sr			! here, change the register bank
	mov	r10,k0
	mov	r11,k1
	mov.l	@r15+,r8
	mov.l	@r15+,r9
	mov.l	@r15+,r10
	mov.l	@r15+,r11
	mov.l	@r15+,r12
	mov.l	@r15+,r13
	mov.l	@r15+,r14
	ldc.l	@r15+,gbr
	lds.l	@r15+,mach
	lds.l	@r15+,macl
	lds.l	@r15+,pr
	ldc.l	@r15+,spc
	mov	k0,r15
	ldc	k1,ssr
	rte
	 nop

	.balign	4
__n_sys:	.long	NR_syscalls
__sct:		.long	SYMBOL_NAME(sys_call_table)
__bh_mask:	.long	SYMBOL_NAME(bh_mask)
__bh_active:	.long	SYMBOL_NAME(bh_active)
__dbh:		.long	SYMBOL_NAME(do_bottom_half)
__blrb_flags:	.long	0x30000000
__minus8192:	.long	-8192		! offset from stackbase to tsk
__tsk_flags:	.long	flags-8192	! offset from stackbase to tsk->flags


! Exception Vector Base
!
!	Should be aligned page boundary.
!
	.balign 	4096,0,4096
ENTRY(vbr_base)
	.long	0
!
	.balign 	256,0,256
general_exception:
	mov.l	1f,k2
	mov.l	2f,k3
	bra	handle_exception
	 mov.l	@k2,k2
	.balign	4
2:	.long	SYMBOL_NAME(ret_from_syscall)
1:	.long	EXPEVT
!
!
	.balign 	1024,0,1024
tlb_miss:
	mov.l	1f,k2
	mov.l	3f,k3
	bra	handle_exception
	 mov.l	@k2,k2
!
	.balign 	512,0,512
interrupt:
	mov.l	2f,k2
	mov.l	4f,k3
	bra	handle_exception
	 mov.l	@k2,k2

	.balign	4
1:	.long	EXPEVT
2:	.long	INTEVT
3:	.long	SYMBOL_NAME(ret_from_syscall)
4:	.long	SYMBOL_NAME(ret_from_irq)

!
!
handle_exception:
	! Using k0, k1 for scratch registers (r0_bank1, and r1_bank1),
	! save all registers onto stack.
	!
	stc	ssr,k0		! from kernel space?
	shll	k0		! Check MD bit (bit30)
	shll	k0
	bt/s	1f		! it's from kernel to kernel transition
	 mov	r15,k0		! save original stack to k0 anyway
	mov	kernel_sp,r15	! change to kernel stack
1:	stc.l	spc,@-r15
	sts.l	pr,@-r15
	!
	lds	k3,pr		! Set the return address to pr
	!
	sts.l	macl,@-r15
	sts.l	mach,@-r15
	stc.l	gbr,@-r15
	mov.l	r14,@-r15
	!
	mov.l	2f,k1
	stc	sr,r14		! back to normal register bank, and
	and	k1,r14		! ..
	ldc	r14,sr		! ...changed here.
	!
	mov.l	r13,@-r15
	mov.l	r12,@-r15
	mov.l	r11,@-r15
	mov.l	r10,@-r15
	mov.l	r9,@-r15
	mov.l	r8,@-r15
	mov.l	r7,@-r15
	mov.l	r6,@-r15
	mov.l	r5,@-r15
	mov.l	r4,@-r15
	mov.l	r3,@-r15
	mov.l	r2,@-r15
	mov.l	r1,@-r15
	mov.l	r0,@-r15
	stc.l	r0_bank,@-r15	! save orignal stack
	stc.l	ssr,@-r15
	mov.l	r0,@-r15	! push r0 again (for syscall number)
	! Then, dispatch to the handler, according to the excepiton code.
	stc	k_ex_code,r1
	shlr2	r1
	shlr	r1
	mov.l	1f,r0
	add	r1,r0
	mov.l	@r0,r0
	jmp	@r0
	 mov.l	@r15,r0		! recovering r0..
	.balign	4
1:	.long	SYMBOL_NAME(exception_handling_table)
2:	.long	0xdfffffff	! RB=0, BL=1

none:
	rts
	 nop

.data
ENTRY(exception_handling_table)
	.long	error
	.long	error
	.long	tlb_miss_load
	.long	tlb_miss_store
	.long	initial_page_write
	.long	tlb_protection_violation_load
	.long	tlb_protection_violation_store
	.long	error	! address_error_load        (filled by trap_init)
	.long	error	! address_error_store       (filled by trap_init)
	.long	error	! fpu_exception
	.long	error
	.long	system_call			! Unconditional Trap
	.long	error	! reserved_instruction      (filled by trap_init)
	.long	error	! illegal_slot_instruction  (filled by trap_init)
ENTRY(nmi_slot)
	.long	none				! Not implemented yet
ENTRY(user_break_point_trap)
	.long	error				! Not implemented yet
ENTRY(interrupt_table)
	! external hardware
	.long	SYMBOL_NAME(do_IRQ)	! 0000
	.long	SYMBOL_NAME(do_IRQ)	! 0001
	.long	SYMBOL_NAME(do_IRQ)	! 0010
	.long	SYMBOL_NAME(do_IRQ)	! 0011
	.long	SYMBOL_NAME(do_IRQ)	! 0100
	.long	SYMBOL_NAME(do_IRQ)	! 0101
	.long	SYMBOL_NAME(do_IRQ)	! 0110
	.long	SYMBOL_NAME(do_IRQ)	! 0111
	.long	SYMBOL_NAME(do_IRQ)	! 1000
	.long	SYMBOL_NAME(do_IRQ)	! 1001
	.long	SYMBOL_NAME(do_IRQ)	! 1010
	.long	SYMBOL_NAME(do_IRQ)	! 1011
	.long	SYMBOL_NAME(do_IRQ)	! 1100
	.long	SYMBOL_NAME(do_IRQ)	! 1101
	.long	SYMBOL_NAME(do_IRQ)	! 1110
	.long	error
	! Internal hardware
	.long	SYMBOL_NAME(do_IRQ)	! TMU0 tuni0
	.long	SYMBOL_NAME(do_IRQ)	! TMU1 tuni1
	.long	SYMBOL_NAME(do_IRQ)	! TMU2 tuni2
	.long	SYMBOL_NAME(do_IRQ)	!      ticpi2
	.long	SYMBOL_NAME(do_IRQ)	! RTC  ati
	.long	SYMBOL_NAME(do_IRQ)	!      pri
	.long	SYMBOL_NAME(do_IRQ)	!      cui
	.long	SYMBOL_NAME(do_IRQ)	! SCI  eri
	.long	SYMBOL_NAME(do_IRQ)	!      rxi
	.long	SYMBOL_NAME(do_IRQ)	!      txi
	.long	SYMBOL_NAME(do_IRQ)	!      tei
	.long	SYMBOL_NAME(do_IRQ)	! WDT  iti
	.long	SYMBOL_NAME(do_IRQ)	! REF  rcmi
	.long	SYMBOL_NAME(do_IRQ)	!      rovi
	.long	SYMBOL_NAME(do_IRQ)
	.long	SYMBOL_NAME(do_IRQ)
#if defined(CONFIG_CPU_SUBTYPE_SH7709)
	.long	SYMBOL_NAME(do_IRQ)	! 32 IRQ  irq0
	.long	SYMBOL_NAME(do_IRQ)	! 33      irq1
	.long	SYMBOL_NAME(do_IRQ)	! 34      irq2
	.long	SYMBOL_NAME(do_IRQ)	! 35      irq3
	.long	SYMBOL_NAME(do_IRQ)	! 36      irq4
	.long	SYMBOL_NAME(do_IRQ)	! 37      irq5
	.long	SYMBOL_NAME(do_IRQ)	! 38
	.long	SYMBOL_NAME(do_IRQ)	! 39
	.long	SYMBOL_NAME(do_IRQ)	! 40 PINT pint0-7
	.long	SYMBOL_NAME(do_IRQ)	! 41      pint8-15
	.long	SYMBOL_NAME(do_IRQ)	! 42
	.long	SYMBOL_NAME(do_IRQ)	! 43
	.long	SYMBOL_NAME(do_IRQ)	! 44
	.long	SYMBOL_NAME(do_IRQ)	! 45
	.long	SYMBOL_NAME(do_IRQ)	! 46
	.long	SYMBOL_NAME(do_IRQ)	! 47
	.long	SYMBOL_NAME(do_IRQ)	! 48 DMAC dei0
	.long	SYMBOL_NAME(do_IRQ)	! 49      dei1
	.long	SYMBOL_NAME(do_IRQ)	! 50      dei2
	.long	SYMBOL_NAME(do_IRQ)	! 51      dei3
	.long	SYMBOL_NAME(do_IRQ)	! 52 IrDA eri1
	.long	SYMBOL_NAME(do_IRQ)	! 53      rxi1
	.long	SYMBOL_NAME(do_IRQ)	! 54      bri1
	.long	SYMBOL_NAME(do_IRQ)	! 55      txi1
	.long	SYMBOL_NAME(do_IRQ)	! 56 SCIF eri2
	.long	SYMBOL_NAME(do_IRQ)	! 57      rxi2
	.long	SYMBOL_NAME(do_IRQ)	! 58      bri2
	.long	SYMBOL_NAME(do_IRQ)	! 59      txi2
	.long	SYMBOL_NAME(do_IRQ)	! 60 ADC  adi
#elif defined(__SH4__)
	.long	SYMBOL_NAME(do_IRQ)	! Hitachi UDI
	.long	SYMBOL_NAME(do_IRQ)	! GPIO
	.long	SYMBOL_NAME(do_IRQ)	! DMAC dmte0
	.long	SYMBOL_NAME(do_IRQ)	!      dmte1
	.long	SYMBOL_NAME(do_IRQ)	!      dmte2
	.long	SYMBOL_NAME(do_IRQ)	!      dmte3
	.long	SYMBOL_NAME(do_IRQ)	!      dmae
	.long	SYMBOL_NAME(do_IRQ)
	.long	SYMBOL_NAME(do_IRQ)	! SCIF eri
	.long	SYMBOL_NAME(do_IRQ)	!      rxi
	.long	SYMBOL_NAME(do_IRQ)	!      bri
	.long	SYMBOL_NAME(do_IRQ)	!      txi
	.long	error
	.long	error
	.long	error
	.long	error
	.long	error ! fpu
	.long	error ! fpu
#endif

ENTRY(sys_call_table)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 0  -  old "setup()" system call*/
	.long SYMBOL_NAME(sys_exit)
	.long SYMBOL_NAME(sys_fork)
	.long SYMBOL_NAME(sys_read)
	.long SYMBOL_NAME(sys_write)
	.long SYMBOL_NAME(sys_open)		/* 5 */
	.long SYMBOL_NAME(sys_close)
	.long SYMBOL_NAME(sys_waitpid)
	.long SYMBOL_NAME(sys_creat)
	.long SYMBOL_NAME(sys_link)
	.long SYMBOL_NAME(sys_unlink)		/* 10 */
	.long SYMBOL_NAME(sys_execve)
	.long SYMBOL_NAME(sys_chdir)
	.long SYMBOL_NAME(sys_time)
	.long SYMBOL_NAME(sys_mknod)
	.long SYMBOL_NAME(sys_chmod)		/* 15 */
	.long SYMBOL_NAME(sys_lchown)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old break syscall holder */
	.long SYMBOL_NAME(sys_stat)
	.long SYMBOL_NAME(sys_lseek)
	.long SYMBOL_NAME(sys_getpid)		/* 20 */
	.long SYMBOL_NAME(sys_mount)
	.long SYMBOL_NAME(sys_oldumount)
	.long SYMBOL_NAME(sys_setuid)
	.long SYMBOL_NAME(sys_getuid)
	.long SYMBOL_NAME(sys_stime)		/* 25 */
	.long SYMBOL_NAME(sys_ptrace)
	.long SYMBOL_NAME(sys_alarm)
	.long SYMBOL_NAME(sys_fstat)
	.long SYMBOL_NAME(sys_pause)
	.long SYMBOL_NAME(sys_utime)		/* 30 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old stty syscall holder */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old gtty syscall holder */
	.long SYMBOL_NAME(sys_access)
	.long SYMBOL_NAME(sys_nice)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 35 */		/* old ftime syscall holder */
	.long SYMBOL_NAME(sys_sync)
	.long SYMBOL_NAME(sys_kill)
	.long SYMBOL_NAME(sys_rename)
	.long SYMBOL_NAME(sys_mkdir)
	.long SYMBOL_NAME(sys_rmdir)		/* 40 */
	.long SYMBOL_NAME(sys_dup)
	.long SYMBOL_NAME(sys_pipe)
	.long SYMBOL_NAME(sys_times)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old prof syscall holder */
	.long SYMBOL_NAME(sys_brk)		/* 45 */
	.long SYMBOL_NAME(sys_setgid)
	.long SYMBOL_NAME(sys_getgid)
	.long SYMBOL_NAME(sys_signal)
	.long SYMBOL_NAME(sys_geteuid)
	.long SYMBOL_NAME(sys_getegid)		/* 50 */
	.long SYMBOL_NAME(sys_acct)
	.long SYMBOL_NAME(sys_umount)					/* recycled never used phys() */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old lock syscall holder */
	.long SYMBOL_NAME(sys_ioctl)
	.long SYMBOL_NAME(sys_fcntl)		/* 55 */
	.long SYMBOL_NAME(sys_ni_syscall)	/* old mpx syscall holder */
	.long SYMBOL_NAME(sys_setpgid)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old ulimit syscall holder */
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_olduname */
	.long SYMBOL_NAME(sys_umask)		/* 60 */
	.long SYMBOL_NAME(sys_chroot)
	.long SYMBOL_NAME(sys_ustat)
	.long SYMBOL_NAME(sys_dup2)
	.long SYMBOL_NAME(sys_getppid)
	.long SYMBOL_NAME(sys_getpgrp)		/* 65 */
	.long SYMBOL_NAME(sys_setsid)
	.long SYMBOL_NAME(sys_sigaction)
	.long SYMBOL_NAME(sys_sgetmask)
	.long SYMBOL_NAME(sys_ssetmask)
	.long SYMBOL_NAME(sys_setreuid)		/* 70 */
	.long SYMBOL_NAME(sys_setregid)
	.long SYMBOL_NAME(sys_sigsuspend)
	.long SYMBOL_NAME(sys_sigpending)
	.long SYMBOL_NAME(sys_sethostname)
	.long SYMBOL_NAME(sys_setrlimit)	/* 75 */
	.long SYMBOL_NAME(sys_getrlimit)
	.long SYMBOL_NAME(sys_getrusage)
	.long SYMBOL_NAME(sys_gettimeofday)
	.long SYMBOL_NAME(sys_settimeofday)
	.long SYMBOL_NAME(sys_getgroups)	/* 80 */
	.long SYMBOL_NAME(sys_setgroups)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old_select */
	.long SYMBOL_NAME(sys_symlink)
	.long SYMBOL_NAME(sys_lstat)
	.long SYMBOL_NAME(sys_readlink)		/* 85 */
	.long SYMBOL_NAME(sys_uselib)
	.long SYMBOL_NAME(sys_swapon)
	.long SYMBOL_NAME(sys_reboot)
	.long SYMBOL_NAME(old_readdir)
	.long SYMBOL_NAME(sys_mmap)		/* 90 */
	.long SYMBOL_NAME(sys_munmap)
	.long SYMBOL_NAME(sys_truncate)
	.long SYMBOL_NAME(sys_ftruncate)
	.long SYMBOL_NAME(sys_fchmod)
	.long SYMBOL_NAME(sys_fchown)		/* 95 */
	.long SYMBOL_NAME(sys_getpriority)
	.long SYMBOL_NAME(sys_setpriority)
	.long SYMBOL_NAME(sys_ni_syscall)	/* old profil syscall holder */
	.long SYMBOL_NAME(sys_statfs)
	.long SYMBOL_NAME(sys_fstatfs)		/* 100 */
	.long SYMBOL_NAME(sys_ni_syscall)		/* ioperm */
	.long SYMBOL_NAME(sys_socketcall)
	.long SYMBOL_NAME(sys_syslog)
	.long SYMBOL_NAME(sys_setitimer)
	.long SYMBOL_NAME(sys_getitimer)	/* 105 */
	.long SYMBOL_NAME(sys_newstat)
	.long SYMBOL_NAME(sys_newlstat)
	.long SYMBOL_NAME(sys_newfstat)
	.long SYMBOL_NAME(sys_uname)
	.long SYMBOL_NAME(sys_ni_syscall)	/* 110 */ /* iopl */
	.long SYMBOL_NAME(sys_vhangup)
	.long SYMBOL_NAME(sys_ni_syscall)	/* idle */
	.long SYMBOL_NAME(sys_ni_syscall)	/* vm86old */
	.long SYMBOL_NAME(sys_wait4)
	.long SYMBOL_NAME(sys_swapoff)		/* 115 */
	.long SYMBOL_NAME(sys_sysinfo)
	.long SYMBOL_NAME(sys_ipc)
	.long SYMBOL_NAME(sys_fsync)
	.long SYMBOL_NAME(sys_sigreturn)
	.long SYMBOL_NAME(sys_clone)		/* 120 */
	.long SYMBOL_NAME(sys_setdomainname)
	.long SYMBOL_NAME(sys_newuname)
	.long SYMBOL_NAME(sys_ni_syscall)	/* sys_modify_ldt */
	.long SYMBOL_NAME(sys_adjtimex)
	.long SYMBOL_NAME(sys_mprotect)		/* 125 */
	.long SYMBOL_NAME(sys_sigprocmask)
	.long SYMBOL_NAME(sys_create_module)
	.long SYMBOL_NAME(sys_init_module)
	.long SYMBOL_NAME(sys_delete_module)
	.long SYMBOL_NAME(sys_get_kernel_syms)	/* 130 */
	.long SYMBOL_NAME(sys_quotactl)
	.long SYMBOL_NAME(sys_getpgid)
	.long SYMBOL_NAME(sys_fchdir)
	.long SYMBOL_NAME(sys_bdflush)
	.long SYMBOL_NAME(sys_sysfs)		/* 135 */
	.long SYMBOL_NAME(sys_personality)
	.long SYMBOL_NAME(sys_ni_syscall)	/* for afs_syscall */
	.long SYMBOL_NAME(sys_setfsuid)
	.long SYMBOL_NAME(sys_setfsgid)
	.long SYMBOL_NAME(sys_llseek)		/* 140 */
	.long SYMBOL_NAME(sys_getdents)
	.long SYMBOL_NAME(sys_select)
	.long SYMBOL_NAME(sys_flock)
	.long SYMBOL_NAME(sys_msync)
	.long SYMBOL_NAME(sys_readv)		/* 145 */
	.long SYMBOL_NAME(sys_writev)
	.long SYMBOL_NAME(sys_getsid)
	.long SYMBOL_NAME(sys_fdatasync)
	.long SYMBOL_NAME(sys_sysctl)
	.long SYMBOL_NAME(sys_mlock)		/* 150 */
	.long SYMBOL_NAME(sys_munlock)
	.long SYMBOL_NAME(sys_mlockall)
	.long SYMBOL_NAME(sys_munlockall)
	.long SYMBOL_NAME(sys_sched_setparam)
	.long SYMBOL_NAME(sys_sched_getparam)   /* 155 */
	.long SYMBOL_NAME(sys_sched_setscheduler)
	.long SYMBOL_NAME(sys_sched_getscheduler)
	.long SYMBOL_NAME(sys_sched_yield)
	.long SYMBOL_NAME(sys_sched_get_priority_max)
	.long SYMBOL_NAME(sys_sched_get_priority_min)  /* 160 */
	.long SYMBOL_NAME(sys_sched_rr_get_interval)
	.long SYMBOL_NAME(sys_nanosleep)
	.long SYMBOL_NAME(sys_mremap)
	.long SYMBOL_NAME(sys_setresuid)
	.long SYMBOL_NAME(sys_getresuid)	/* 165 */
	.long SYMBOL_NAME(sys_ni_syscall)		/* vm86 */
	.long SYMBOL_NAME(sys_query_module)
	.long SYMBOL_NAME(sys_poll)
	.long SYMBOL_NAME(sys_nfsservctl)
	.long SYMBOL_NAME(sys_setresgid)	/* 170 */
	.long SYMBOL_NAME(sys_getresgid)
	.long SYMBOL_NAME(sys_prctl)
	.long SYMBOL_NAME(sys_rt_sigreturn)
	.long SYMBOL_NAME(sys_rt_sigaction)
	.long SYMBOL_NAME(sys_rt_sigprocmask)	/* 175 */
	.long SYMBOL_NAME(sys_rt_sigpending)
	.long SYMBOL_NAME(sys_rt_sigtimedwait)
	.long SYMBOL_NAME(sys_rt_sigqueueinfo)
	.long SYMBOL_NAME(sys_rt_sigsuspend)
	.long SYMBOL_NAME(sys_pread)		/* 180 */
	.long SYMBOL_NAME(sys_pwrite)
	.long SYMBOL_NAME(sys_chown)
	.long SYMBOL_NAME(sys_getcwd)
	.long SYMBOL_NAME(sys_capget)
	.long SYMBOL_NAME(sys_capset)           /* 185 */
	.long SYMBOL_NAME(sys_sigaltstack)
	.long SYMBOL_NAME(sys_sendfile)
	.long SYMBOL_NAME(sys_ni_syscall)		/* streams1 */
	.long SYMBOL_NAME(sys_ni_syscall)		/* streams2 */
	.long SYMBOL_NAME(sys_vfork)            /* 190 */

	/*
	 * NOTE!! This doesn't have to be exact - we just have
	 * to make sure we have _enough_ of the "sys_ni_syscall"
	 * entries. Don't panic if you notice that this hasn't
	 * been shrunk every time we add a new system call.
	 */
	.rept NR_syscalls-190
		.long SYMBOL_NAME(sys_ni_syscall)
	.endr

/* End of entry.S */
