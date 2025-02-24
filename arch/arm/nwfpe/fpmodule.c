
/*
    NetWinder Floating Point Emulator
    (c) Rebel.com, 1998-1999
    (c) Philip Blundell, 1998-1999

    Direct questions, comments to Scott Bambrough <scottb@netwinder.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/config.h>

/* XXX */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/init.h>
/* XXX */

#include "softfloat.h"
#include "fpopcode.h"
#include "fpmodule.h"
#include "fpa11.h"
#include "fpa11.inl"

/* external data */
extern FPA11 *fpa11;

/* kernel symbols required for signal handling */
typedef struct task_struct*	PTASK;

#ifdef MODULE
int fp_printk(const char *,...);
void fp_send_sig(unsigned long sig, PTASK p, int priv);
#if LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Scott Bambrough <scottb@rebel.com>");
MODULE_DESCRIPTION("NWFPE floating point emulator");
#endif

#else
#define fp_printk	printk
#define fp_send_sig	send_sig
#define kern_fp_enter	fp_enter
#endif

/* kernel function prototypes required */
void fp_setup(void);

/* external declarations for saved kernel symbols */
extern void (*kern_fp_enter)(void);

/* Original value of fp_enter from kernel before patched by fpe_init. */ 
static void (*orig_fp_enter)(void);

/* forward declarations */
extern void nwfpe_enter(void);

/* Address of user registers on the kernel stack. */
unsigned int *userRegisters;

void __init fpe_version(void)
{
  static const char szTitle[] = "<4>NetWinder Floating Point Emulator ";
  static const char szVersion[] = "V0.95 ";
  static const char szCopyright[] = "(c) 1998-1999 Rebel.com\n";
  fp_printk(szTitle);
  fp_printk(szVersion);
  fp_printk(szCopyright);
}

int __init fpe_init(void)
{
  if (sizeof(FPA11) > sizeof(union fp_state))
    printk(KERN_ERR "nwfpe: bad structure size\n");
  else {
    /* Display title, version and copyright information. */
    fpe_version();

    /* Save pointer to the old FP handler and then patch ourselves in */
    orig_fp_enter = kern_fp_enter;
    kern_fp_enter = nwfpe_enter;
  }

  return 0;
}

void __exit fpe_exit(void)
{
  /* Restore the values we saved earlier. */
  kern_fp_enter = orig_fp_enter;
}

/*
ScottB:  November 4, 1998

Moved this function out of softfloat-specialize into fpmodule.c.
This effectively isolates all the changes required for integrating with the
Linux kernel into fpmodule.c.  Porting to NetBSD should only require modifying
fpmodule.c to integrate with the NetBSD kernel (I hope!).

[1/1/99: Not quite true any more unfortunately.  There is Linux-specific
code to access data in user space in some other source files at the 
moment (grep for get_user / put_user calls).  --philb]

float_exception_flags is a global variable in SoftFloat.

This function is called by the SoftFloat routines to raise a floating
point exception.  We check the trap enable byte in the FPSR, and raise
a SIGFPE exception if necessary.  If not the relevant bits in the 
cumulative exceptions flag byte are set and we return.
*/

void float_raise(signed char flags)
{
#ifdef CONFIG_DEBUG_USER
  printk(KERN_DEBUG "NWFPE: %s[%d] takes exception %08x at %p from %08x\n",
	 current->comm, current->pid, flags,
	 __builtin_return_address(0), userRegisters[15]);
#endif

  float_exception_flags |= flags;
  if (readFPSR() & (flags << 16))
  {
    /* raise exception */
    fp_send_sig(SIGFPE, current, 1);
  }
  else
  {
    /* set the cumulative exceptions flags */
    writeFPSR(flags);
  }
}

module_init(fpe_init);
module_exit(fpe_exit);
