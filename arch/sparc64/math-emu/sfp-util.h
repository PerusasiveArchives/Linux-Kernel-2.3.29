/* $Id: sfp-util.h,v 1.2 1999/06/07 18:24:15 jj Exp $
 * arch/sparc64/math-emu/sfp-util.h
 *
 * Copyright (C) 1999 Jakub Jelinek (jj@ultra.linux.cz)
 * Copyright (C) 1999 David S. Miller (davem@redhat.com)
 *
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <asm/byteorder.h>

#define add_ssaaaa(sh, sl, ah, al, bh, bl) 						\
  __asm__ ("addcc %4,%5,%1
  	    add %2,%3,%0
  	    bcs,a,pn %%xcc, 1f
  	    add %0, 1, %0
  	    1:"										\
	   : "=r" ((UDItype)(sh)),				      			\
	     "=&r" ((UDItype)(sl))				      			\
	   : "r" ((UDItype)(ah)),				     			\
	     "r" ((UDItype)(bh)),				      			\
	     "r" ((UDItype)(al)),				     			\
	     "r" ((UDItype)(bl))				       			\
	   : "cc")
	   
#define sub_ddmmss(sh, sl, ah, al, bh, bl) 						\
  __asm__ ("subcc %4,%5,%1
  	    sub %2,%3,%0
  	    bcs,a,pn %%xcc, 1f
  	    sub %0, 1, %0
  	    1:"										\
	   : "=r" ((UDItype)(sh)),				      			\
	     "=&r" ((UDItype)(sl))				      			\
	   : "r" ((UDItype)(ah)),				     			\
	     "r" ((UDItype)(bh)),				      			\
	     "r" ((UDItype)(al)),				     			\
	     "r" ((UDItype)(bl))				       			\
	   : "cc")

#define umul_ppmm(wh, wl, u, v)								\
  do {											\
	  UDItype tmp1, tmp2, tmp3, tmp4;						\
	  __asm__ __volatile__ (							\
		   "srl %7,0,%3
		    mulx %3,%6,%1
		    srlx %6,32,%2
		    mulx %2,%3,%4
		    sllx %4,32,%5
		    srl %6,0,%3
		    sub %1,%5,%5
		    srlx %5,32,%5
		    addcc %4,%5,%4
		    srlx %7,32,%5
		    mulx %3,%5,%3
		    mulx %2,%5,%5
		    sethi %%hi(0x80000000),%2
		    addcc %4,%3,%4
		    srlx %4,32,%4
		    add %2,%2,%2
		    movcc %%xcc,%%g0,%2
		    addcc %5,%4,%5
		    sllx %3,32,%3
		    add %1,%3,%1
		    add %5,%2,%0"							\
	   : "=r" ((UDItype)(wh)),							\
	     "=&r" ((UDItype)(wl)),							\
	     "=&r" (tmp1), "=&r" (tmp2), "=&r" (tmp3), "=&r" (tmp4)			\
	   : "r" ((UDItype)(u)),							\
	     "r" ((UDItype)(v))								\
	   : "cc");									\
  } while (0)
  
#define udiv_qrnnd(q, r, n1, n0, d) 							\
  do {                                                                  		\
    UWtype __d1, __d0, __q1, __q0, __r1, __r0, __m;                     		\
    __d1 = (d >> 32);                                           			\
    __d0 = (USItype)d;                                            			\
                                                                        		\
    __r1 = (n1) % __d1;                                                 		\
    __q1 = (n1) / __d1;                                                 		\
    __m = (UWtype) __q1 * __d0;                                         		\
    __r1 = (__r1 << 32) | (n0 >> 32);                          				\
    if (__r1 < __m)                                                     		\
      {                                                                 		\
        __q1--, __r1 += (d);                                            		\
        if (__r1 >= (d)) /* i.e. we didn't get carry when adding to __r1 */		\
          if (__r1 < __m)                                               		\
            __q1--, __r1 += (d);                                        		\
      }                                                                 		\
    __r1 -= __m;                                                        		\
                                                                        		\
    __r0 = __r1 % __d1;                                                 		\
    __q0 = __r1 / __d1;                                                 		\
    __m = (UWtype) __q0 * __d0;                                         		\
    __r0 = (__r0 << 32) | ((USItype)n0);                           			\
    if (__r0 < __m)                                                     		\
      {                                                                 		\
        __q0--, __r0 += (d);                                            		\
        if (__r0 >= (d))                                                		\
          if (__r0 < __m)                                               		\
            __q0--, __r0 += (d);                                        		\
      }                                                                 		\
    __r0 -= __m;                                                        		\
                                                                        		\
    (q) = (UWtype) (__q1 << 32)  | __q0;                                		\
    (r) = __r0;                                                         		\
  } while (0)

#define UDIV_NEEDS_NORMALIZATION 1  

#define abort()										\
	return 0

#ifdef __BIG_ENDIAN
#define __BYTE_ORDER __BIG_ENDIAN
#else
#define __BYTE_ORDER __LITTLE_ENDIAN
#endif
