/* $Id: ipc.c,v 1.4 1999/05/13 07:11:37 jj Exp $
 * ipc.c: Solaris IPC emulation
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/smp_lock.h>
#include <linux/wait.h>
#include <linux/shm.h>
#include <linux/sem.h>
#include <linux/msg.h>

#include <asm/uaccess.h>
#include <asm/string.h>
#include <asm/ipc.h>

#include "conv.h"

struct solaris_ipc_perm {
	s32	uid;
	s32	gid;
	s32	cuid;
	s32	cgid;
	u32	mode;
	u32	seq;
	int	key;
	s32	pad[4];
};

struct solaris_shmid_ds {
	struct solaris_ipc_perm	shm_perm;
	int			shm_segsz;
	u32			shm_amp;
	unsigned short		shm_lkcnt;
	char			__padxx[2];
	s32			shm_lpid;
	s32			shm_cpid;
	u32			shm_nattch;
	u32			shm_cnattch;
	s32			shm_atime;
	s32			shm_pad1;
	s32			shm_dtime;
	s32			shm_pad2;
	s32			shm_ctime;
	s32			shm_pad3;
	unsigned short		shm_cv;
	char			shm_pad4[2];
	u32			shm_sptas;
	s32			shm_pad5[2];
};

asmlinkage long solaris_shmsys(int cmd, u32 arg1, u32 arg2, u32 arg3)
{
	int (*sys_ipc)(unsigned,int,int,unsigned long,void *,long) = 
		(int (*)(unsigned,int,int,unsigned long,void *,long))SYS(ipc);
	mm_segment_t old_fs;
	unsigned long raddr;
	int ret;
		
	switch (cmd) {
	case 0: /* shmat */
		old_fs = get_fs();
		set_fs(KERNEL_DS);
		ret = sys_ipc(SHMAT, arg1, arg3 & ~0x4000, (unsigned long)&raddr, (void *)A(arg2), 0);
		set_fs(old_fs);
		if (ret >= 0) return (u32)raddr;
		else return ret;
	case 1: /* shmctl */
		switch (arg2) {
		case 3: /* SHM_LOCK */
		case 4: /* SHM_UNLOCK */
			return sys_ipc(SHMCTL, arg1, (arg2 == 3) ? SHM_LOCK : SHM_UNLOCK, 0, NULL, 0);
		case 10: /* IPC_RMID */
			return sys_ipc(SHMCTL, arg1, IPC_RMID, 0, NULL, 0);
		case 11: /* IPC_SET */
			{
				struct shmid_ds s;
				
				if (get_user (s.shm_perm.uid, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.uid)) ||
				    __get_user (s.shm_perm.gid, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.gid)) || 
				    __get_user (s.shm_perm.mode, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.mode)))
					return -EFAULT;
				old_fs = get_fs();
				set_fs(KERNEL_DS);
				ret = sys_ipc(SHMCTL, arg1, IPC_SET, 0, &s, 0);
				set_fs(old_fs);
				return ret;
			}
		case 12: /* IPC_STAT */
			{
				struct shmid_ds s;
				
				old_fs = get_fs();
				set_fs(KERNEL_DS);
				ret = sys_ipc(SHMCTL, arg1, IPC_SET, 0, &s, 0);
				set_fs(old_fs);
				if (get_user (s.shm_perm.uid, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.uid)) ||
				    __get_user (s.shm_perm.gid, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.gid)) || 
				    __get_user (s.shm_perm.cuid, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.cuid)) ||
				    __get_user (s.shm_perm.cgid, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.cgid)) || 
				    __get_user (s.shm_perm.mode, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.mode)) ||
				    __get_user (s.shm_perm.seq, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.seq)) ||
				    __get_user (s.shm_perm.key, &(((struct solaris_shmid_ds *)A(arg3))->shm_perm.key)) ||
				    __get_user (s.shm_segsz, &(((struct solaris_shmid_ds *)A(arg3))->shm_segsz)) ||
				    __get_user (s.shm_lpid, &(((struct solaris_shmid_ds *)A(arg3))->shm_lpid)) ||
				    __get_user (s.shm_cpid, &(((struct solaris_shmid_ds *)A(arg3))->shm_cpid)) ||
				    __get_user (s.shm_nattch, &(((struct solaris_shmid_ds *)A(arg3))->shm_nattch)) ||
				    __get_user (s.shm_atime, &(((struct solaris_shmid_ds *)A(arg3))->shm_atime)) ||
				    __get_user (s.shm_dtime, &(((struct solaris_shmid_ds *)A(arg3))->shm_dtime)) ||
				    __get_user (s.shm_ctime, &(((struct solaris_shmid_ds *)A(arg3))->shm_ctime)))
					return -EFAULT;
				return ret;
			}
		default: return -EINVAL;
		}
	case 2: /* shmdt */
		return sys_ipc(SHMDT, 0, 0, 0, (void *)A(arg1), 0);
	case 3: /* shmget */
		return sys_ipc(SHMGET, arg1, arg2, arg3, NULL, 0);
	}
	return -EINVAL;
}
