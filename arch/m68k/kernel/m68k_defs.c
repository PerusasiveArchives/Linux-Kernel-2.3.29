/*
 * This program is used to generate definitions needed by
 * assembly language modules.
 *
 * We use the technique used in the OSF Mach kernel code:
 * generate asm statements containing #defines,
 * compile this file to assembler, and then extract the
 * #defines from the assembly-language output.
 */

#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <asm/bootinfo.h>
#include <asm/irq.h>
#include <asm/amigahw.h>
#include <video/font.h>

#define DEFINE(sym, val) \
	asm volatile("\n#define " #sym " %c0" : : "i" (val))

int main(void)
{
	/* offsets into the task struct */
	DEFINE(TASK_STATE, offsetof(struct task_struct, state));
	DEFINE(TASK_FLAGS, offsetof(struct task_struct, flags));
	DEFINE(TASK_SIGPENDING, offsetof(struct task_struct, sigpending));
	DEFINE(TASK_NEEDRESCHED, offsetof(struct task_struct, need_resched));
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(TASK_MM, offsetof(struct task_struct, mm));
	DEFINE(TASK_ACTIVE_MM, offsetof(struct task_struct, active_mm));

	/* offsets into the thread struct */
	DEFINE(THREAD_KSP, offsetof(struct thread_struct, ksp));
	DEFINE(THREAD_USP, offsetof(struct thread_struct, usp));
	DEFINE(THREAD_SR, offsetof(struct thread_struct, sr));
	DEFINE(THREAD_FS, offsetof(struct thread_struct, fs));
	DEFINE(THREAD_CRP, offsetof(struct thread_struct, crp));
	DEFINE(THREAD_ESP0, offsetof(struct thread_struct, esp0));
	DEFINE(THREAD_FPREG, offsetof(struct thread_struct, fp));
	DEFINE(THREAD_FPCNTL, offsetof(struct thread_struct, fpcntl));
	DEFINE(THREAD_FPSTATE, offsetof(struct thread_struct, fpstate));

	/* offsets into the pt_regs */
	DEFINE(PT_D0, offsetof(struct pt_regs, d0));
	DEFINE(PT_ORIG_D0, offsetof(struct pt_regs, orig_d0));
	DEFINE(PT_D1, offsetof(struct pt_regs, d1));
	DEFINE(PT_D2, offsetof(struct pt_regs, d2));
	DEFINE(PT_D3, offsetof(struct pt_regs, d3));
	DEFINE(PT_D4, offsetof(struct pt_regs, d4));
	DEFINE(PT_D5, offsetof(struct pt_regs, d5));
	DEFINE(PT_A0, offsetof(struct pt_regs, a0));
	DEFINE(PT_A1, offsetof(struct pt_regs, a1));
	DEFINE(PT_A2, offsetof(struct pt_regs, a2));
	DEFINE(PT_PC, offsetof(struct pt_regs, pc));
	DEFINE(PT_SR, offsetof(struct pt_regs, sr));
	/* bitfields are a bit difficult */
	DEFINE(PT_VECTOR, offsetof(struct pt_regs, pc) + 4);

	/* offsets into the irq_handler struct */
	DEFINE(IRQ_HANDLER, offsetof(struct irq_node, handler));
	DEFINE(IRQ_DEVID, offsetof(struct irq_node, dev_id));
	DEFINE(IRQ_NEXT, offsetof(struct irq_node, next));

	/* offsets into the kernel_stat struct */
	DEFINE(STAT_IRQ, offsetof(struct kernel_stat, irqs));

	/* offsets into the bi_record struct */
	DEFINE(BIR_TAG, offsetof(struct bi_record, tag));
	DEFINE(BIR_SIZE, offsetof(struct bi_record, size));
	DEFINE(BIR_DATA, offsetof(struct bi_record, data));

	/* offsets into fbcon_font_desc (video/font.h) */
	DEFINE(FBCON_FONT_DESC_IDX, offsetof(struct fbcon_font_desc, idx));
	DEFINE(FBCON_FONT_DESC_NAME, offsetof(struct fbcon_font_desc, name));
	DEFINE(FBCON_FONT_DESC_WIDTH, offsetof(struct fbcon_font_desc, width));
	DEFINE(FBCON_FONT_DESC_HEIGHT, offsetof(struct fbcon_font_desc, height));
	DEFINE(FBCON_FONT_DESC_DATA, offsetof(struct fbcon_font_desc, data));
	DEFINE(FBCON_FONT_DESC_PREF, offsetof(struct fbcon_font_desc, pref));

	/* signal defines */
	DEFINE(SIGSEGV, SIGSEGV);
	DEFINE(SEGV_MAPERR, SEGV_MAPERR);
	DEFINE(SIGTRAP, SIGTRAP);
	DEFINE(TRAP_TRACE, TRAP_TRACE);

	/* offsets into the custom struct */
	DEFINE(CUSTOMBASE, &custom);
	DEFINE(C_INTENAR, offsetof(struct CUSTOM, intenar));
	DEFINE(C_INTREQR, offsetof(struct CUSTOM, intreqr));
	DEFINE(C_INTENA, offsetof(struct CUSTOM, intena));
	DEFINE(C_INTREQ, offsetof(struct CUSTOM, intreq));
	DEFINE(C_SERDATR, offsetof(struct CUSTOM, serdatr));
	DEFINE(C_SERDAT, offsetof(struct CUSTOM, serdat));
	DEFINE(C_SERPER, offsetof(struct CUSTOM, serper));
	DEFINE(CIAABASE, &ciaa);
	DEFINE(CIABBASE, &ciab);
	DEFINE(C_PRA, offsetof(struct CIA, pra));
	DEFINE(ZTWOBASE, zTwoBase);

	return 0;
}
