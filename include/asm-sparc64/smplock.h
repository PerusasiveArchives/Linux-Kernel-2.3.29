/*
 * <asm/smplock.h>
 *
 * Default SMP lock implementation
 */
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>

extern spinlock_t kernel_flag;

/*
 * Release global kernel lock and global interrupt lock
 */
#define release_kernel_lock(task, cpu) \
do { \
	if (task->lock_depth >= 0) \
		spin_unlock(&kernel_flag); \
	release_irqlock(cpu); \
	__sti(); \
} while (0)

/*
 * Re-acquire the kernel lock
 */
#define reacquire_kernel_lock(task) \
do { \
	if (task->lock_depth >= 0) \
		spin_lock(&kernel_flag); \
} while (0)


/*
 * Getting the big kernel lock.
 *
 * This cannot happen asynchronously,
 * so we only need to worry about other
 * CPU's.
 */
#define lock_kernel() \
do { \
	if (!++current->lock_depth) \
		spin_lock(&kernel_flag); \
} while(0)

#define unlock_kernel() \
do { \
	if (--current->lock_depth < 0) \
		spin_unlock(&kernel_flag); \
} while(0)
