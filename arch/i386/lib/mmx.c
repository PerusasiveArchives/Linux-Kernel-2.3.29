#include <linux/types.h>
#include <linux/string.h>
#include <linux/sched.h>

/*
 *	MMX 3DNow! library helper functions
 *
 *	To do:
 *	We can use MMX just for prefetch in IRQ's. This may be a win. 
 *		(reported so on K6-III)
 *	We should use a better code neutral filler for the short jump
 *		leal ebx. [ebx] is apparently best for K6-2, but Cyrix ??
 *	We also want to clobber the filler register so we dont get any
 *		register forwarding stalls on the filler. 
 *
 *	Add *user handling. Checksums are not a win with MMX on any CPU
 *	tested so far for any MMX solution figured.
 *
 */
 
void *_mmx_memcpy(void *to, const void *from, size_t len)
{
	void *p=to;
	int i= len >> 6;	/* len/64 */

	if (!(current->flags & PF_USEDFPU))
		clts();
	else
	{
		__asm__ __volatile__ ( " fnsave %0; fwait\n"::"m"(current->thread.i387));
		current->flags &= ~PF_USEDFPU;
	}

	__asm__ __volatile__ (
		"1: prefetch (%0)\n"		/* This set is 28 bytes */
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from) );
		
	
	for(; i>0; i--)
	{
		__asm__ __volatile__ (
		"1:  prefetch 320(%0)\n"
		"2:  movq (%0), %%mm0\n"
		"  movq 8(%0), %%mm1\n"
		"  movq 16(%0), %%mm2\n"
		"  movq 24(%0), %%mm3\n"
		"  movq %%mm0, (%1)\n"
		"  movq %%mm1, 8(%1)\n"
		"  movq %%mm2, 16(%1)\n"
		"  movq %%mm3, 24(%1)\n"
		"  movq 32(%0), %%mm0\n"
		"  movq 40(%0), %%mm1\n"
		"  movq 48(%0), %%mm2\n"
		"  movq 56(%0), %%mm3\n"
		"  movq %%mm0, 32(%1)\n"
		"  movq %%mm1, 40(%1)\n"
		"  movq %%mm2, 48(%1)\n"
		"  movq %%mm3, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	/*
	 *	Now do the tail of the block
	 */
	__memcpy(to, from, len&63);
	stts();
	return p;
}

static void fast_clear_page(void *page)
{
	int i;
	if (!(current->flags & PF_USEDFPU))
		clts();
	else
	{
		__asm__ __volatile__ ( " fnsave %0; fwait\n"::"m"(current->thread.i387));
		current->flags &= ~PF_USEDFPU;
	}
	
	__asm__ __volatile__ (
		"  pxor %%mm0, %%mm0\n" : :
	);

	for(i=0;i<4096/128;i++)
	{
		__asm__ __volatile__ (
		"  movq %%mm0, (%0)\n"
		"  movq %%mm0, 8(%0)\n"
		"  movq %%mm0, 16(%0)\n"
		"  movq %%mm0, 24(%0)\n"
		"  movq %%mm0, 32(%0)\n"
		"  movq %%mm0, 40(%0)\n"
		"  movq %%mm0, 48(%0)\n"
		"  movq %%mm0, 56(%0)\n"
		"  movq %%mm0, 64(%0)\n"
		"  movq %%mm0, 72(%0)\n"
		"  movq %%mm0, 80(%0)\n"
		"  movq %%mm0, 88(%0)\n"
		"  movq %%mm0, 96(%0)\n"
		"  movq %%mm0, 104(%0)\n"
		"  movq %%mm0, 112(%0)\n"
		"  movq %%mm0, 120(%0)\n"
		: : "r" (page) : "memory");
		page+=128;
	}
	stts();
}

static void fast_copy_page(void *to, void *from)
{
	int i;
	if (!(current->flags & PF_USEDFPU))
		clts();
	else
	{
		__asm__ __volatile__ ( " fnsave %0; fwait\n"::"m"(current->thread.i387));
		current->flags &= ~PF_USEDFPU;
	}

	__asm__ __volatile__ (
		"1: prefetch (%0)\n"
		"   prefetch 64(%0)\n"
		"   prefetch 128(%0)\n"
		"   prefetch 192(%0)\n"
		"   prefetch 256(%0)\n"
		"2:  \n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x1AEB, 1b\n"	/* jmp on 26 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from) );

	for(i=0; i<4096/64; i++)
	{
		__asm__ __volatile__ (
		"1: prefetch 320(%0)\n"
		"2: movq (%0), %%mm0\n"
		"   movq 8(%0), %%mm1\n"
		"   movq 16(%0), %%mm2\n"
		"   movq 24(%0), %%mm3\n"
		"   movq %%mm0, (%1)\n"
		"   movq %%mm1, 8(%1)\n"
		"   movq %%mm2, 16(%1)\n"
		"   movq %%mm3, 24(%1)\n"
		"   movq 32(%0), %%mm0\n"
		"   movq 40(%0), %%mm1\n"
		"   movq 48(%0), %%mm2\n"
		"   movq 56(%0), %%mm3\n"
		"   movq %%mm0, 32(%1)\n"
		"   movq %%mm1, 40(%1)\n"
		"   movq %%mm2, 48(%1)\n"
		"   movq %%mm3, 56(%1)\n"
		".section .fixup, \"ax\"\n"
		"3: movw $0x05EB, 1b\n"	/* jmp on 5 bytes */
		"   jmp 2b\n"
		".previous\n"
		".section __ex_table,\"a\"\n"
		"	.align 4\n"
		"	.long 1b, 3b\n"
		".previous"
		: : "r" (from), "r" (to) : "memory");
		from+=64;
		to+=64;
	}
	stts();
}

/*
 *	Favour MMX for page clear and copy. 
 */

static void slow_zero_page(void * page)
{
	int d0, d1;
	__asm__ __volatile__( \
		"cld\n\t" \
		"rep ; stosl" \
		: "=&c" (d0), "=&D" (d1)
		:"a" (0),"1" (page),"0" (1024)
		:"memory");
}
 
void mmx_clear_page(void * page)
{
	if(in_interrupt())
		slow_zero_page(page);
	else
		fast_clear_page(page);
}

static void slow_copy_page(void *to, void *from)
{
	int d0, d1, d2;
	__asm__ __volatile__( \
		"cld\n\t" \
		"rep ; movsl" \
		: "=&c" (d0), "=&D" (d1), "=&S" (d2) \
		: "0" (1024),"1" ((long) to),"2" ((long) from) \
		: "memory");
}
  

void mmx_copy_page(void *to, void *from)
{
	if(in_interrupt())
		slow_copy_page(to, from);
	else
		fast_copy_page(to, from);
}
