#ifndef __ASM_ARM_STRING_H
#define __ASM_ARM_STRING_H

/*
 * We don't do inline string functions, since the
 * optimised inline asm versions are not small.
 */

#define __HAVE_ARCH_STRRCHR
extern char * strrchr(const char * s, int c);

#define __HAVE_ARCH_STRCHR
extern char * strchr(const char * s, int c);

#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMMOVE
#define __HAVE_ARCH_MEMCHR
#define __HAVE_ARCH_MEMZERO
#define __HAVE_ARCH_MEMSET

extern void __memzero(void *ptr, __kernel_size_t n);
extern void __memset(void *ptr, int v, __kernel_size_t n);

#define memset(p,v,n)							\
	({								\
		if ((n) != 0) {						\
			if (__builtin_constant_p((v)) && (v) == 0)	\
				__memzero((p),(n));			\
			else						\
				__memset((p),(v),(n));			\
		}							\
		(p);							\
	})

#define memzero(p,n) ({ if ((n) != 0) __memzero((p),(n)); (p); })

#endif
 
