/*
 * linux/arch/arm/kernel/sys_arm.c
 *
 * Copyright (C) People who wrote linux/arch/i386/kernel/sys_i386.c
 * Copyright (C) 1995, 1996 Russell King.
 * 
 * This file contains various random system calls that
 * have a non-standard calling sequence on the Linux/arm
 * platform.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/sem.h>
#include <linux/msg.h>
#include <linux/shm.h>
#include <linux/stat.h>
#include <linux/mman.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/utsname.h>

#include <asm/uaccess.h>
#include <asm/ipc.h>

/*
 * Constant strings used in inlined functions in header files
 */

/*
 * sys_pipe() is the normal C calling standard for creating
 * a pipe. It's not the way unix traditionally does this, though.
 */
asmlinkage int sys_pipe(unsigned long * fildes)
{
	int fd[2];
	int error;

	lock_kernel();
	error = do_pipe(fd);
	unlock_kernel();
	if (!error) {
		if (copy_to_user(fildes, fd, 2*sizeof(int)))
			error = -EFAULT;
	}
	return error;
}

/*
 * Perform the select(nd, in, out, ex, tv) and mmap() system
 * calls. ARM Linux didn't use to be able to handle more than
 * 4 system call parameters, so these system calls used a memory
 * block for parameter passing..
 */

struct mmap_arg_struct {
	unsigned long addr;
	unsigned long len;
	unsigned long prot;
	unsigned long flags;
	unsigned long fd;
	unsigned long offset;
};

asmlinkage int old_mmap(struct mmap_arg_struct *arg)
{
	int error = -EFAULT;
	struct file * file = NULL;
	struct mmap_arg_struct a;

	down(&current->mm->mmap_sem);
	lock_kernel();
	if (copy_from_user(&a, arg, sizeof(a)))
		goto out;
	if (!(a.flags & MAP_ANONYMOUS)) {
		error = -EBADF;
		file = fget(a.fd);
		if (!file)
			goto out;
	}
	a.flags &= ~(MAP_EXECUTABLE | MAP_DENYWRITE);
	error = do_mmap(file, a.addr, a.len, a.prot, a.flags, a.offset);
	if (file)
		fput(file);
out:
	unlock_kernel();
	up(&current->mm->mmap_sem);
	return error;
}


extern asmlinkage int sys_select(int, fd_set *, fd_set *, fd_set *, struct timeval *);

struct sel_arg_struct {
	unsigned long n;
	fd_set *inp, *outp, *exp;
	struct timeval *tvp;
};

asmlinkage int old_select(struct sel_arg_struct *arg)
{
	struct sel_arg_struct a;

	if (copy_from_user(&a, arg, sizeof(a)))
		return -EFAULT;
	/* sys_select() does the appropriate kernel locking */
	return sys_select(a.n, a.inp, a.outp, a.exp, a.tvp);
}

/*
 * sys_ipc() is the de-multiplexer for the SysV IPC calls..
 *
 * This is really horribly ugly.
 */
asmlinkage int sys_ipc (uint call, int first, int second, int third, void *ptr, long fifth)
{
	int version, ret;

	version = call >> 16; /* hack for backward compatibility */
	call &= 0xffff;

	switch (call) {
	case SEMOP:
		return sys_semop (first, (struct sembuf *)ptr, second);
	case SEMGET:
		return sys_semget (first, second, third);
	case SEMCTL: {
		union semun fourth;
		if (!ptr)
			return -EINVAL;
		if (get_user(fourth.__pad, (void **) ptr))
			return -EFAULT;
		return sys_semctl (first, second, third, fourth);
	}

	case MSGSND:
		return sys_msgsnd (first, (struct msgbuf *) ptr, 
				   second, third);
	case MSGRCV:
		switch (version) {
		case 0: {
			struct ipc_kludge tmp;
			if (!ptr)
				return -EINVAL;
			if (copy_from_user(&tmp,(struct ipc_kludge *) ptr,
					   sizeof (tmp)))
				return -EFAULT;
			return sys_msgrcv (first, tmp.msgp, second,
					   tmp.msgtyp, third);
		}
		default:
			return sys_msgrcv (first,
					   (struct msgbuf *) ptr,
					   second, fifth, third);
		}
	case MSGGET:
		return sys_msgget ((key_t) first, second);
	case MSGCTL:
		return sys_msgctl (first, second, (struct msqid_ds *) ptr);

	case SHMAT:
		switch (version) {
		default: {
			ulong raddr;
			ret = sys_shmat (first, (char *) ptr, second, &raddr);
			if (ret)
				return ret;
			return put_user (raddr, (ulong *) third);
		}
		case 1:	/* iBCS2 emulator entry point */
			if (!segment_eq(get_fs(), get_ds()))
				return -EINVAL;
			return sys_shmat (first, (char *) ptr,
					  second, (ulong *) third);
		}
	case SHMDT: 
		return sys_shmdt ((char *)ptr);
	case SHMGET:
		return sys_shmget (first, second, third);
	case SHMCTL:
		return sys_shmctl (first, second,
				   (struct shmid_ds *) ptr);
	default:
		return -EINVAL;
	}
}

/* Fork a new task - this creates a new program thread.
 * This is called indirectly via a small wrapper
 */
asmlinkage int sys_fork(struct pt_regs *regs)
{
	return do_fork(SIGCHLD, regs->ARM_sp, regs);
}

/* Clone a task - this clones the calling program thread.
 * This is called indirectly via a small wrapper
 */
asmlinkage int sys_clone(unsigned long clone_flags, unsigned long newsp, struct pt_regs *regs)
{
	if (!newsp)
		newsp = regs->ARM_sp;
	return do_fork(clone_flags, newsp, regs);
}

asmlinkage int sys_vfork(struct pt_regs *regs)
{
	return do_fork(CLONE_VFORK | CLONE_VM | SIGCHLD, regs->ARM_sp, regs);
}

/* sys_execve() executes a new program.
 * This is called indirectly via a small wrapper
 */
asmlinkage int sys_execve(char *filenamei, char **argv, char **envp, struct pt_regs *regs)
{
	int error;
	char * filename;

	lock_kernel();
	filename = getname(filenamei);
	error = PTR_ERR(filename);
	if (IS_ERR(filename))
		goto out;
	error = do_execve(filename, argv, envp, regs);
	putname(filename);
out:
	unlock_kernel();
	return error;
}

/*
 * Detect the old function calling standard
 */
static inline unsigned long old_calling_standard (struct pt_regs  *regs)
{
	unsigned long instr, *pcv = (unsigned long *)(instruction_pointer(regs) - 8);
	return (!get_user (instr, pcv) && instr == 0xe1a0300d);
}

/* Compatability functions - we used to pass 5 parameters as r0, r1, r2, *r3, *(r3+4)
 * We now use r0 - r4, and return an error if the old style calling standard is used.
 * Eventually these functions will disappear.
 */
asmlinkage int
sys_compat_llseek (unsigned int fd, unsigned long offset_high, unsigned long offset_low,
		loff_t *result, unsigned int origin, struct pt_regs *regs)
{
	extern int sys_llseek (unsigned int, unsigned long, unsigned long, loff_t *, unsigned int);

	if (old_calling_standard (regs)) {
		printk (KERN_NOTICE "%s (%d): unsupported llseek call standard\n",
			current->comm, current->pid);
		return -EINVAL;
	}
	return sys_llseek (fd, offset_high, offset_low, result, origin);
}

asmlinkage int
sys_compat_mount (char *devname, char *dirname, char *type, unsigned long flags, void *data,
		  struct pt_regs *regs)
{
	extern int sys_mount (char *, char *, char *, unsigned long, void *);

	if (old_calling_standard (regs)) {
		printk (KERN_NOTICE "%s (%d): unsupported mount call standard\n",
			current->comm, current->pid);
		return -EINVAL;
	}
	return sys_mount (devname, dirname, type, flags, data);
}

asmlinkage int sys_uname (struct old_utsname * name)
{
	static int warned = 0;
	int err;
	
	if (warned == 0) {
		warned ++;
		printk (KERN_NOTICE "%s (%d): obsolete uname call\n",
			current->comm, current->pid);
	}

	if(!name)
		return -EFAULT;
	down(&uts_sem);
	err=copy_to_user (name, &system_utsname, sizeof (*name));
	up(&uts_sem);
	return err?-EFAULT:0;
}

asmlinkage int sys_olduname(struct oldold_utsname * name)
{
	int error;
	static int warned = 0;

	if (warned == 0) {
		warned ++;
		printk (KERN_NOTICE "%s (%d): obsolete olduname call\n",
			current->comm, current->pid);
	}

	if (!name)
		return -EFAULT;

	if (!access_ok(VERIFY_WRITE,name,sizeof(struct oldold_utsname)))
		return -EFAULT;

	down(&uts_sem);
	
	error = __copy_to_user(&name->sysname,&system_utsname.sysname,__OLD_UTS_LEN);
	error -= __put_user(0,name->sysname+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->nodename,&system_utsname.nodename,__OLD_UTS_LEN);
	error -= __put_user(0,name->nodename+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->release,&system_utsname.release,__OLD_UTS_LEN);
	error -= __put_user(0,name->release+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->version,&system_utsname.version,__OLD_UTS_LEN);
	error -= __put_user(0,name->version+__OLD_UTS_LEN);
	error -= __copy_to_user(&name->machine,&system_utsname.machine,__OLD_UTS_LEN);
	error -= __put_user(0,name->machine+__OLD_UTS_LEN);
	
	up(&uts_sem);
	
	error = error ? -EFAULT : 0;

	return error;
}

asmlinkage int sys_pause(void)
{
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	return -ERESTARTNOHAND;
}

