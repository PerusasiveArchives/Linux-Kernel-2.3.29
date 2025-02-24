/* $Id: socksys.c,v 1.10 1999/08/31 06:55:08 davem Exp $
 * socksys.c: /dev/inet/ stuff for Solaris emulation.
 *
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 * Copyright (C) 1997, 1998 Patrik Rak (prak3264@ss1000.ms.mff.cuni.cz)
 * Copyright (C) 1995, 1996 Mike Jagdis (jaggy@purplet.demon.co.uk)
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>
#include <linux/ioctl.h>
#include <linux/fs.h>
#include <linux/file.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>
#include <asm/termios.h>

#include "conv.h"
#include "socksys.h"

extern asmlinkage int sys_ioctl(unsigned int fd, unsigned int cmd, 
	unsigned long arg);
	
static int af_inet_protocols[] = {
IPPROTO_ICMP, IPPROTO_ICMP, IPPROTO_IGMP, IPPROTO_IPIP, IPPROTO_TCP,
IPPROTO_EGP, IPPROTO_PUP, IPPROTO_UDP, IPPROTO_IDP, IPPROTO_RAW,
0, 0, 0, 0, 0, 0,
};

#ifndef DEBUG_SOLARIS_KMALLOC

#define mykmalloc kmalloc
#define mykfree kfree

#else

extern void * mykmalloc(size_t s, int gfp);
extern void mykfree(void *);

#endif

static unsigned int (*sock_poll)(struct file *, poll_table *);

static struct file_operations socksys_file_ops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	NULL,		/* open */
	NULL,		/* flush */
	NULL,		/* release */
};

static int socksys_open(struct inode * inode, struct file * filp)
{
	int family, type, protocol, fd;
	struct dentry *dentry;
	int (*sys_socket)(int,int,int) =
		(int (*)(int,int,int))SUNOS(97);
        struct sol_socket_struct * sock;
	
	family = ((MINOR(inode->i_rdev) >> 4) & 0xf);
	switch (family) {
	case AF_UNIX:
		type = SOCK_STREAM;
		protocol = 0;
		break;
	case AF_INET:
		protocol = af_inet_protocols[MINOR(inode->i_rdev) & 0xf];
		switch (protocol) {
		case IPPROTO_TCP: type = SOCK_STREAM; break;
		case IPPROTO_UDP: type = SOCK_DGRAM; break;
		default: type = SOCK_RAW; break;
		}
		break;
	default:
		type = SOCK_RAW;
		protocol = 0;
		break;
	}

	fd = sys_socket(family, type, protocol);
	if (fd < 0)
		return fd;
	/*
	 * N.B. The following operations are not legal!
	 * Try instead:
	 * d_delete(filp->f_dentry), then d_instantiate with sock inode
	 */
	dentry = filp->f_dentry;
	filp->f_dentry = dget(fcheck(fd)->f_dentry);
	filp->f_dentry->d_inode->i_rdev = inode->i_rdev;
	filp->f_dentry->d_inode->i_flock = inode->i_flock;
	filp->f_dentry->d_inode->u.socket_i.file = filp;
	filp->f_op = &socksys_file_ops;
        sock = (struct sol_socket_struct*) 
        	mykmalloc(sizeof(struct sol_socket_struct), GFP_KERNEL);
        if (!sock) return -ENOMEM;
	SOLDD(("sock=%016lx(%016lx)\n", sock, filp));
        sock->magic = SOLARIS_SOCKET_MAGIC;
        sock->modcount = 0;
        sock->state = TS_UNBND;
        sock->offset = 0;
        sock->pfirst = sock->plast = NULL;
        filp->private_data = sock;
	SOLDD(("filp->private_data %016lx\n", filp->private_data));

	sys_close(fd);
	dput(dentry);
	return 0;
}

static int socksys_release(struct inode * inode, struct file * filp)
{
        struct sol_socket_struct * sock;
        struct T_primsg *it;

	/* XXX: check this */
	sock = (struct sol_socket_struct *)filp->private_data;
	SOLDD(("sock release %016lx(%016lx)\n", sock, filp));
	it = sock->pfirst;
	while (it) {
		struct T_primsg *next = it->next;
		
		SOLDD(("socksys_release %016lx->%016lx\n", it, next));
		mykfree((char*)it);
		it = next;
	}
	filp->private_data = NULL;
	SOLDD(("socksys_release %016lx\n", sock));
	mykfree((char*)sock);
	return 0;
}

static unsigned int socksys_poll(struct file * filp, poll_table * wait)
{
	struct inode *ino;
	unsigned int mask = 0;

	ino=filp->f_dentry->d_inode;
	if (ino && ino->i_sock) {
		struct sol_socket_struct *sock;
		sock = (struct sol_socket_struct*)filp->private_data;
		if (sock && sock->pfirst) {
			mask |= POLLIN | POLLRDNORM;
			if (sock->pfirst->pri == MSG_HIPRI)
				mask |= POLLPRI;
		}
	}
	if (sock_poll)
		mask |= (*sock_poll)(filp, wait);
	return mask;
}
	
static struct file_operations socksys_fops = {
	NULL,		/* lseek */
	NULL,		/* read */
	NULL,		/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	NULL,		/* ioctl */
	NULL,		/* mmap */
	socksys_open,	/* open */
	NULL,		/* flush */
	socksys_release,/* release */
};

int __init
init_socksys(void)
{
	int ret;
	struct file * file;
	int (*sys_socket)(int,int,int) =
		(int (*)(int,int,int))SUNOS(97);
	int (*sys_close)(unsigned int) = 
		(int (*)(unsigned int))SYS(close);
	
	ret = register_chrdev (30, "socksys", &socksys_fops);
	if (ret < 0) {
		printk ("Couldn't register socksys character device\n");
		return ret;
	}
	ret = sys_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ret < 0) {
		printk ("Couldn't create socket\n");
		return ret;
	}
	file = fcheck(ret);
	/* N.B. Is this valid? Suppose the f_ops are in a module ... */
	socksys_file_ops = *file->f_op;
	sys_close(ret);
	sock_poll = socksys_file_ops.poll;
	socksys_file_ops.poll = socksys_poll;
	socksys_file_ops.release = socksys_release;
	return 0;
}

void
cleanup_socksys(void)
{
	if (unregister_chrdev (30, "socksys"))
		printk ("Couldn't unregister socksys character device\n");
}
