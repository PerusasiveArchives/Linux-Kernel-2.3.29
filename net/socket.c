/*
 * NET		An implementation of the SOCKET network access protocol.
 *
 * Version:	@(#)socket.c	1.1.93	18/02/95
 *
 * Authors:	Orest Zborowski, <obz@Kodak.COM>
 *		Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *
 * Fixes:
 *		Anonymous	:	NOTSOCK/BADF cleanup. Error fix in
 *					shutdown()
 *		Alan Cox	:	verify_area() fixes
 *		Alan Cox	:	Removed DDI
 *		Jonathan Kamens	:	SOCK_DGRAM reconnect bug
 *		Alan Cox	:	Moved a load of checks to the very
 *					top level.
 *		Alan Cox	:	Move address structures to/from user
 *					mode above the protocol layers.
 *		Rob Janssen	:	Allow 0 length sends.
 *		Alan Cox	:	Asynchronous I/O support (cribbed from the
 *					tty drivers).
 *		Niibe Yutaka	:	Asynchronous I/O for writes (4.4BSD style)
 *		Jeff Uphoff	:	Made max number of sockets command-line
 *					configurable.
 *		Matti Aarnio	:	Made the number of sockets dynamic,
 *					to be allocated when needed, and mr.
 *					Uphoff's max is used as max to be
 *					allowed to allocate.
 *		Linus		:	Argh. removed all the socket allocation
 *					altogether: it's in the inode now.
 *		Alan Cox	:	Made sock_alloc()/sock_release() public
 *					for NetROM and future kernel nfsd type
 *					stuff.
 *		Alan Cox	:	sendmsg/recvmsg basics.
 *		Tom Dyas	:	Export net symbols.
 *		Marcin Dalecki	:	Fixed problems with CONFIG_NET="n".
 *		Alan Cox	:	Added thread locking to sys_* calls
 *					for sockets. May have errors at the
 *					moment.
 *		Kevin Buhr	:	Fixed the dumb errors in the above.
 *		Andi Kleen	:	Some small cleanups, optimizations,
 *					and fixed a copy_from_user() bug.
 *		Tigran Aivazian	:	sys_send(args) calls sys_sendto(args, NULL, 0)
 *		Tigran Aivazian	:	Made listen(2) backlog sanity checks 
 *					protocol-independent
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *
 *	This module is effectively the top level interface to the BSD socket
 *	paradigm. 
 *
 */

#include <linux/config.h>
#include <linux/mm.h>
#include <linux/smp_lock.h>
#include <linux/socket.h>
#include <linux/file.h>
#include <linux/net.h>
#include <linux/interrupt.h>
#include <linux/netdevice.h>
#include <linux/proc_fs.h>
#include <linux/wanrouter.h>
#include <linux/init.h>
#include <linux/poll.h>

#if defined(CONFIG_KMOD) && defined(CONFIG_NET)
#include <linux/kmod.h>
#endif

#include <asm/uaccess.h>

#include <linux/inet.h>
#include <net/ip.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <net/scm.h>
#include <linux/netfilter.h>

static int sock_no_open(struct inode *irrelevant, struct file *dontcare);
static long long sock_lseek(struct file *file, long long offset, int whence);
static ssize_t sock_read(struct file *file, char *buf,
			 size_t size, loff_t *ppos);
static ssize_t sock_write(struct file *file, const char *buf,
			  size_t size, loff_t *ppos);
static int sock_mmap(struct file *file, struct vm_area_struct * vma);

static int sock_close(struct inode *inode, struct file *file);
static unsigned int sock_poll(struct file *file,
			      struct poll_table_struct *wait);
static int sock_ioctl(struct inode *inode, struct file *file,
		      unsigned int cmd, unsigned long arg);
static int sock_fasync(int fd, struct file *filp, int on);


/*
 *	Socket files have a set of 'special' operations as well as the generic file ones. These don't appear
 *	in the operation structures but are done directly via the socketcall() multiplexor.
 */

static struct file_operations socket_file_ops = {
	sock_lseek,
	sock_read,
	sock_write,
	NULL,			/* readdir */
	sock_poll,
	sock_ioctl,
	sock_mmap,
	sock_no_open,		/* special open code to disallow open via /proc */
	NULL,			/* flush */
	sock_close,
	NULL,			/* no fsync */
	sock_fasync
};

/*
 *	The protocol list. Each protocol is registered in here.
 */

static struct net_proto_family *net_families[NPROTO];

#ifdef __SMP__
static atomic_t net_family_lockct = ATOMIC_INIT(0);
static spinlock_t net_family_lock = SPIN_LOCK_UNLOCKED;

/* The strategy is: modifications net_family vector are short, do not
   sleep and veeery rare, but read access should be free of any exclusive
   locks.
 */

static void net_family_write_lock(void)
{
	spin_lock(&net_family_lock);
	while (atomic_read(&net_family_lockct) != 0) {
		spin_unlock(&net_family_lock);

		current->policy |= SCHED_YIELD;
		schedule();

		spin_lock(&net_family_lock);
	}
}

static __inline__ void net_family_write_unlock(void)
{
	spin_unlock(&net_family_lock);
}

static __inline__ void net_family_read_lock(void)
{
	atomic_inc(&net_family_lockct);
	spin_unlock_wait(&net_family_lock);
}

static __inline__ void net_family_read_unlock(void)
{
	atomic_dec(&net_family_lockct);
}

#else
#define net_family_write_lock() do { } while(0)
#define net_family_write_unlock() do { } while(0)
#define net_family_read_lock() do { } while(0)
#define net_family_read_unlock() do { } while(0)
#endif


/*
 *	Statistics counters of the socket lists
 */

static int sockets_in_use  = 0;

/*
 *	Support routines. Move socket addresses back and forth across the kernel/user
 *	divide and look after the messy bits.
 */

#define MAX_SOCK_ADDR	128		/* 108 for Unix domain - 
					   16 for IP, 16 for IPX,
					   24 for IPv6,
					   about 80 for AX.25 
					   must be at least one bigger than
					   the AF_UNIX size (see net/unix/af_unix.c
					   :unix_mkname()).  
					 */

int move_addr_to_kernel(void *uaddr, int ulen, void *kaddr)
{
	if(ulen<0||ulen>MAX_SOCK_ADDR)
		return -EINVAL;
	if(ulen==0)
		return 0;
	if(copy_from_user(kaddr,uaddr,ulen))
		return -EFAULT;
	return 0;
}

int move_addr_to_user(void *kaddr, int klen, void *uaddr, int *ulen)
{
	int err;
	int len;

	if((err=get_user(len, ulen)))
		return err;
	if(len>klen)
		len=klen;
	if(len<0 || len> MAX_SOCK_ADDR)
		return -EINVAL;
	if(len)
	{
		if(copy_to_user(uaddr,kaddr,len))
			return -EFAULT;
	}
	/*
	 *	"fromlen shall refer to the value before truncation.."
	 *			1003.1g
	 */
	return __put_user(klen, ulen);
}

/*
 *	Obtains the first available file descriptor and sets it up for use.
 *
 *	This functions creates file structure and maps it to fd space
 *	of current process. On success it returns file descriptor
 *	and file struct implicitly stored in sock->file.
 *	Note that another thread may close file descriptor before we return
 *	from this function. We use the fact that now we do not refer
 *	to socket after mapping. If one day we will need it, this
 *	function will inincrement ref. count on file by 1.
 *
 *	In any case returned fd MAY BE not valid!
 *	This race condition is inavoidable
 *	with shared fd spaces, we cannot solve is inside kernel,
 *	but we take care of internal coherence yet.
 */

static int sock_map_fd(struct socket *sock)
{
	int fd;

	/*
	 *	Find a file descriptor suitable for return to the user. 
	 */

	fd = get_unused_fd();
	if (fd >= 0) {
		struct file *file = get_empty_filp();

		if (!file) {
			put_unused_fd(fd);
			fd = -ENFILE;
			goto out;
		}

		lock_kernel();
		file->f_dentry = d_alloc_root(sock->inode);
		if (!file->f_dentry) {
			unlock_kernel();
			put_filp(file);
			put_unused_fd(fd);
			fd = -ENOMEM;
			goto out;
		}

		/*
		 * The socket maintains a reference to the inode, so we
		 * have to increment the count.
		 */
		sock->inode->i_count++;
		unlock_kernel();

		file->f_op = &socket_file_ops;
		file->f_mode = 3;
		file->f_flags = O_RDWR;
		file->f_pos = 0;
		fd_install(fd, file);
		sock->file = file;
	}

out:
	return fd;
}

extern __inline__ struct socket *socki_lookup(struct inode *inode)
{
	return &inode->u.socket_i;
}

/*
 *	Go from a file number to its socket slot.
 */

extern struct socket *sockfd_lookup(int fd, int *err)
{
	struct file *file;
	struct inode *inode;
	struct socket *sock;

	if (!(file = fget(fd)))
	{
		*err = -EBADF;
		return NULL;
	}

	inode = file->f_dentry->d_inode;
	if (!inode || !inode->i_sock || !(sock = socki_lookup(inode)))
	{
		*err = -ENOTSOCK;
		fput(file);
		return NULL;
	}

	if (sock->file != file) {
		printk(KERN_ERR "socki_lookup: socket file changed!\n");
		sock->file = file;
	}
	return sock;
}

extern __inline__ void sockfd_put(struct socket *sock)
{
	fput(sock->file);
}

/*
 *	Allocate a socket.
 */

struct socket *sock_alloc(void)
{
	struct inode * inode;
	struct socket * sock;

	lock_kernel();
	/* Damn! get_empty_inode is not SMP safe.
	   I ask, why does it have decorative spinlock
	   at the very beginning? Probably, dcache ops should
	   be lock_kernel'ed inside inode.c
	 */
	inode = get_empty_inode();
	if (!inode) {
		unlock_kernel();
		return NULL;
	}
	unlock_kernel();

	sock = socki_lookup(inode);

	inode->i_mode = S_IFSOCK|S_IRWXUGO;
	inode->i_sock = 1;
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;

	sock->inode = inode;
	init_waitqueue_head(&sock->wait);
	sock->fasync_list = NULL;
	sock->state = SS_UNCONNECTED;
	sock->flags = 0;
	sock->ops = NULL;
	sock->sk = NULL;
	sock->file = NULL;

	sockets_in_use++;
	return sock;
}

/*
 *	In theory you can't get an open on this inode, but /proc provides
 *	a back door. Remember to keep it shut otherwise you'll let the
 *	creepy crawlies in.
 */
  
static int sock_no_open(struct inode *irrelevant, struct file *dontcare)
{
	return -ENXIO;
}

void sock_release(struct socket *sock)
{
	if (sock->ops) 
		sock->ops->release(sock);

	if (sock->fasync_list)
		printk(KERN_ERR "sock_release: fasync list not empty!\n");

	--sockets_in_use;	/* Bookkeeping.. */
	sock->file=NULL;
	iput(sock->inode);
}

int sock_sendmsg(struct socket *sock, struct msghdr *msg, int size)
{
	int err;
	struct scm_cookie scm;

	err = scm_send(sock, msg, &scm);
	if (err >= 0) {
		err = sock->ops->sendmsg(sock, msg, size, &scm);
		scm_destroy(&scm);
	}
	return err;
}

int sock_recvmsg(struct socket *sock, struct msghdr *msg, int size, int flags)
{
	struct scm_cookie scm;

	memset(&scm, 0, sizeof(scm));

	size = sock->ops->recvmsg(sock, msg, size, flags, &scm);
	if (size >= 0)
		scm_recv(sock, msg, &scm, flags);

	return size;
}


/*
 *	Sockets are not seekable.
 */

static long long sock_lseek(struct file *file,long long offset, int whence)
{
	return -ESPIPE;
}

/*
 *	Read data from a socket. ubuf is a user mode pointer. We make sure the user
 *	area ubuf...ubuf+size-1 is writable before asking the protocol.
 */

static ssize_t sock_read(struct file *file, char *ubuf,
			 size_t size, loff_t *ppos)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	int flags;

	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (size==0)		/* Match SYS5 behaviour */
		return 0;

	sock = socki_lookup(file->f_dentry->d_inode); 

	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	iov.iov_base=ubuf;
	iov.iov_len=size;
	flags = !(file->f_flags & O_NONBLOCK) ? 0 : MSG_DONTWAIT;

	return sock_recvmsg(sock, &msg, size, flags);
}


/*
 *	Write data to a socket. We verify that the user area ubuf..ubuf+size-1
 *	is readable by the user process.
 */

static ssize_t sock_write(struct file *file, const char *ubuf,
			  size_t size, loff_t *ppos)
{
	struct socket *sock;
	struct msghdr msg;
	struct iovec iov;
	
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if(size==0)		/* Match SYS5 behaviour */
		return 0;

	sock = socki_lookup(file->f_dentry->d_inode); 

	msg.msg_name=NULL;
	msg.msg_namelen=0;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_flags=!(file->f_flags & O_NONBLOCK) ? 0 : MSG_DONTWAIT;
	if (sock->type == SOCK_SEQPACKET)
		msg.msg_flags |= MSG_EOR;
	iov.iov_base=(void *)ubuf;
	iov.iov_len=size;
	
	return sock_sendmsg(sock, &msg, size);
}

int sock_readv_writev(int type, struct inode * inode, struct file * file,
		      const struct iovec * iov, long count, long size)
{
	struct msghdr msg;
	struct socket *sock;

	sock = socki_lookup(inode);

	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_control = NULL;
	msg.msg_controllen = 0;
	msg.msg_iov = (struct iovec *) iov;
	msg.msg_iovlen = count;
	msg.msg_flags = (file->f_flags & O_NONBLOCK) ? MSG_DONTWAIT : 0;

	/* read() does a VERIFY_WRITE */
	if (type == VERIFY_WRITE)
		return sock_recvmsg(sock, &msg, size, msg.msg_flags);

	if (sock->type == SOCK_SEQPACKET)
		msg.msg_flags |= MSG_EOR;

	return sock_sendmsg(sock, &msg, size);
}


/*
 *	With an ioctl arg may well be a user mode pointer, but we don't know what to do
 *	with it - that's up to the protocol still.
 */

int sock_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	   unsigned long arg)
{
	struct socket *sock;
	int err;

	unlock_kernel();
	sock = socki_lookup(inode);
	err = sock->ops->ioctl(sock, cmd, arg);
	lock_kernel();

	return err;
}


static unsigned int sock_poll(struct file *file, poll_table * wait)
{
	struct socket *sock;
	int err;

	unlock_kernel();
	sock = socki_lookup(file->f_dentry->d_inode);

	/*
	 *	We can't return errors to poll, so it's either yes or no. 
	 */

	err = sock->ops->poll(file, sock, wait);
	lock_kernel();
	return err;
}

static int sock_mmap(struct file * file, struct vm_area_struct * vma)
{
	struct socket *sock = socki_lookup(file->f_dentry->d_inode);

	return sock->ops->mmap(file, sock, vma);
}

int sock_close(struct inode *inode, struct file *filp)
{
	/*
	 *	It was possible the inode is NULL we were 
	 *	closing an unfinished socket. 
	 */

	if (!inode)
	{
		printk(KERN_DEBUG "sock_close: NULL inode\n");
		return 0;
	}
	unlock_kernel();
	sock_fasync(-1, filp, 0);
	sock_release(socki_lookup(inode));
	lock_kernel();
	return 0;
}

/*
 *	Update the socket async list
 *
 *	Fasync_list locking strategy.
 *
 *	1. fasync_list is modified only under process context socket lock
 *	   i.e. under semaphore.
 *	2. fasync_list is used under read_lock(&sk->callback_lock)
 *	   or under socket lock.
 *	3. fasync_list is used from any context including IRQ, so that
 *	   modification under socket lock have to be enhanced with
 *	   write_lock_irq(&sk->callback_lock).
 *							--ANK (990710)
 */

static int sock_fasync(int fd, struct file *filp, int on)
{
	struct fasync_struct *fa, *fna=NULL, **prev;
	struct socket *sock;
	struct sock *sk;

	if (on)
	{
		fna=(struct fasync_struct *)kmalloc(sizeof(struct fasync_struct), GFP_KERNEL);
		if(fna==NULL)
			return -ENOMEM;
	}


	sock = socki_lookup(filp->f_dentry->d_inode);
	
	if ((sk=sock->sk) == NULL)
		return -EINVAL;

	lock_sock(sk);

	prev=&(sock->fasync_list);

	for (fa=*prev; fa!=NULL; prev=&fa->fa_next,fa=*prev)
		if (fa->fa_file==filp)
			break;

	if(on)
	{
		if(fa!=NULL)
		{
			write_lock_irq(&sk->callback_lock);
			fa->fa_fd=fd;
			write_unlock_irq(&sk->callback_lock);

			kfree_s(fna,sizeof(struct fasync_struct));
			goto out;
		}
		fna->fa_file=filp;
		fna->fa_fd=fd;
		fna->magic=FASYNC_MAGIC;
		fna->fa_next=sock->fasync_list;
		write_lock_irq(&sk->callback_lock);
		sock->fasync_list=fna;
		write_unlock_irq(&sk->callback_lock);
	}
	else
	{
		if (fa!=NULL)
		{
			write_lock_irq(&sk->callback_lock);
			*prev=fa->fa_next;
			write_unlock_irq(&sk->callback_lock);
			kfree_s(fa,sizeof(struct fasync_struct));
		}
	}

out:
	release_sock(sock->sk);
	return 0;
}

/* This function may be called only under socket lock or callback_lock */

int sock_wake_async(struct socket *sock, int how, int band)
{
	if (!sock || !sock->fasync_list)
		return -1;
	switch (how)
	{
	case 1:
		if (sock->flags & SO_WAITDATA)
			break;
		goto call_kill;
	case 2:
		if (!(sock->flags & SO_NOSPACE))
			break;
		sock->flags &= ~SO_NOSPACE;
		/* fall through */
	case 0:
	call_kill:
		/* read_lock(&sock->sk->callback_lock); */
		if(sock->fasync_list != NULL)
			kill_fasync(sock->fasync_list, SIGIO, band);
		/* read_unlock(&sock->sk->callback_lock); */
		break;
	case 3:
		kill_fasync(sock->fasync_list, SIGURG, band);
	}
	return 0;
}


int sock_create(int family, int type, int protocol, struct socket **res)
{
	int i;
	struct socket *sock;

	/*
	 *	Check protocol is in range
	 */
	if(family<0 || family>=NPROTO)
		return -EINVAL;

	/* Compatibility.

	   This uglymoron is moved from INET layer to here to avoid
	   deadlock in module load.
	 */
	if (family == PF_INET && type == SOCK_PACKET) {
		static int warned; 
		if (!warned) {
			warned = 1;
			printk(KERN_INFO "%s uses obsolete (PF_INET,SOCK_PACKET)\n", current->comm);
		}
		family = PF_PACKET;
	}
		
#if defined(CONFIG_KMOD) && defined(CONFIG_NET)
	/* Attempt to load a protocol module if the find failed. 
	 * 
	 * 12/09/1996 Marcin: But! this makes REALLY only sense, if the user 
	 * requested real, full-featured networking support upon configuration.
	 * Otherwise module support will break!
	 */
	if (net_families[family]==NULL)
	{
		char module_name[30];
		sprintf(module_name,"net-pf-%d",family);
		request_module(module_name);
	}
#endif

	net_family_read_lock();
	if (net_families[family] == NULL) {
		i = -EINVAL;
		goto out;
	}

/*
 *	Allocate the socket and allow the family to set things up. if
 *	the protocol is 0, the family is instructed to select an appropriate
 *	default.
 */

	if (!(sock = sock_alloc())) 
	{
		printk(KERN_WARNING "socket: no more sockets\n");
		i = -ENFILE;		/* Not exactly a match, but its the
					   closest posix thing */
		goto out;
	}

	sock->type  = type;

	if ((i = net_families[family]->create(sock, protocol)) < 0) 
	{
		sock_release(sock);
		goto out;
	}

	*res = sock;

out:
	net_family_read_unlock();
	return i;
}

asmlinkage long sys_socket(int family, int type, int protocol)
{
	int retval;
	struct socket *sock;

	retval = sock_create(family, type, protocol, &sock);
	if (retval < 0)
		goto out;

	retval = sock_map_fd(sock);
	if (retval < 0)
		goto out_release;

out:
	/* It may be already another descriptor 8) Not kernel problem. */
	return retval;

out_release:
	sock_release(sock);
	return retval;
}

/*
 *	Create a pair of connected sockets.
 */

asmlinkage long sys_socketpair(int family, int type, int protocol, int usockvec[2])
{
	struct socket *sock1, *sock2;
	int fd1, fd2, err;

	/*
	 * Obtain the first socket and check if the underlying protocol
	 * supports the socketpair call.
	 */

	err = sock_create(family, type, protocol, &sock1);
	if (err < 0)
		goto out;

	err = sock_create(family, type, protocol, &sock2);
	if (err < 0)
		goto out_release_1;

	err = sock1->ops->socketpair(sock1, sock2);
	if (err < 0) 
		goto out_release_both;

	fd1 = fd2 = -1;

	err = sock_map_fd(sock1);
	if (err < 0)
		goto out_release_both;
	fd1 = err;

	err = sock_map_fd(sock2);
	if (err < 0)
		goto out_close_1;
	fd2 = err;

	/* fd1 and fd2 may be already another descriptors.
	 * Not kernel problem.
	 */

	err = put_user(fd1, &usockvec[0]); 
	if (!err)
		err = put_user(fd2, &usockvec[1]);
	if (!err)
		return 0;

	sys_close(fd2);
	sys_close(fd1);
	return err;

out_close_1:
        sock_release(sock2);
	sys_close(fd1);
	return err;

out_release_both:
        sock_release(sock2);
out_release_1:
        sock_release(sock1);
out:
	return err;
}


/*
 *	Bind a name to a socket. Nothing much to do here since it's
 *	the protocol's responsibility to handle the local address.
 *
 *	We move the socket address to kernel space before we call
 *	the protocol layer (having also checked the address is ok).
 */

asmlinkage long sys_bind(int fd, struct sockaddr *umyaddr, int addrlen)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int err;

	if((sock = sockfd_lookup(fd,&err))!=NULL)
	{
		if((err=move_addr_to_kernel(umyaddr,addrlen,address))>=0)
			err = sock->ops->bind(sock, (struct sockaddr *)address, addrlen);
		sockfd_put(sock);
	}			
	return err;
}


/*
 *	Perform a listen. Basically, we allow the protocol to do anything
 *	necessary for a listen, and if that works, we mark the socket as
 *	ready for listening.
 */

asmlinkage long sys_listen(int fd, int backlog)
{
	struct socket *sock;
	int err;
	
	if ((sock = sockfd_lookup(fd, &err)) != NULL) {
		if ((unsigned) backlog == 0)	/* BSDism */
			backlog = 1;
		if ((unsigned) backlog > SOMAXCONN)
			backlog = SOMAXCONN;
		err=sock->ops->listen(sock, backlog);
		sockfd_put(sock);
	}
	return err;
}


/*
 *	For accept, we attempt to create a new socket, set up the link
 *	with the client, wake up the client, then return the new
 *	connected fd. We collect the address of the connector in kernel
 *	space and move it to user at the very end. This is unclean because
 *	we open the socket then return an error.
 *
 *	1003.1g adds the ability to recvmsg() to query connection pending
 *	status to recvmsg. We need to add that support in a way thats
 *	clean when we restucture accept also.
 */

asmlinkage long sys_accept(int fd, struct sockaddr *upeer_sockaddr, int *upeer_addrlen)
{
	struct socket *sock, *newsock;
	int err, len;
	char address[MAX_SOCK_ADDR];

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	err = -EMFILE;
	if (!(newsock = sock_alloc())) 
		goto out_put;

	newsock->type = sock->type;
	newsock->ops = sock->ops;

	err = sock->ops->accept(sock, newsock, sock->file->f_flags);
	if (err < 0)
		goto out_release;

	if (upeer_sockaddr) {
		if(newsock->ops->getname(newsock, (struct sockaddr *)address, &len, 1)<0) {
			err = -ECONNABORTED;
			goto out_release;
		}
		err = move_addr_to_user(address, len, upeer_sockaddr, upeer_addrlen);
		if (err < 0)
			goto out_release;
	}

	if ((err = sock_map_fd(newsock)) < 0)
		goto out_release;

out_put:
	sockfd_put(sock);
out:
	return err;

out_release:
	sock_release(newsock);
	goto out_put;
}


/*
 *	Attempt to connect to a socket with the server address.  The address
 *	is in user space so we verify it is OK and move it to kernel space.
 *
 *	For 1003.1g we need to add clean support for a bind to AF_UNSPEC to
 *	break bindings
 *
 *	NOTE: 1003.1g draft 6.3 is broken with respect to AX.25/NetROM and
 *	other SEQPACKET protocols that take time to connect() as it doesn't
 *	include the -EINPROGRESS status for such sockets.
 */

asmlinkage long sys_connect(int fd, struct sockaddr *uservaddr, int addrlen)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int err;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;
	err = move_addr_to_kernel(uservaddr, addrlen, address);
	if (err < 0)
		goto out_put;
	err = sock->ops->connect(sock, (struct sockaddr *) address, addrlen,
				 sock->file->f_flags);
out_put:
	sockfd_put(sock);
out:
	return err;
}

/*
 *	Get the local address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

asmlinkage long sys_getsockname(int fd, struct sockaddr *usockaddr, int *usockaddr_len)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int len, err;
	
	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;
	err = sock->ops->getname(sock, (struct sockaddr *)address, &len, 0);
	if (err)
		goto out_put;
	err = move_addr_to_user(address, len, usockaddr, usockaddr_len);

out_put:
	sockfd_put(sock);
out:
	return err;
}

/*
 *	Get the remote address ('name') of a socket object. Move the obtained
 *	name to user space.
 */

asmlinkage long sys_getpeername(int fd, struct sockaddr *usockaddr, int *usockaddr_len)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int len, err;

	if ((sock = sockfd_lookup(fd, &err))!=NULL)
	{
		err = sock->ops->getname(sock, (struct sockaddr *)address, &len, 1);
		if (!err)
			err=move_addr_to_user(address,len, usockaddr, usockaddr_len);
		sockfd_put(sock);
	}
	return err;
}

/*
 *	Send a datagram to a given address. We move the address into kernel
 *	space and check the user space data area is readable before invoking
 *	the protocol.
 */

asmlinkage long sys_sendto(int fd, void * buff, size_t len, unsigned flags,
			   struct sockaddr *addr, int addr_len)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	int err;
	struct msghdr msg;
	struct iovec iov;
	
	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;
	iov.iov_base=buff;
	iov.iov_len=len;
	msg.msg_name=NULL;
	msg.msg_iov=&iov;
	msg.msg_iovlen=1;
	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_namelen=addr_len;
	if(addr)
	{
		err = move_addr_to_kernel(addr, addr_len, address);
		if (err < 0)
			goto out_put;
		msg.msg_name=address;
	}
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	msg.msg_flags = flags;
	err = sock_sendmsg(sock, &msg, len);

out_put:		
	sockfd_put(sock);
out:
	return err;
}

/*
 *	Send a datagram down a socket. 
 */

asmlinkage long sys_send(int fd, void * buff, size_t len, unsigned flags)
{
	return sys_sendto(fd, buff, len, flags, NULL, 0);
}

/*
 *	Receive a frame from the socket and optionally record the address of the 
 *	sender. We verify the buffers are writable and if needed move the
 *	sender address from kernel to user space.
 */

asmlinkage long sys_recvfrom(int fd, void * ubuf, size_t size, unsigned flags,
			     struct sockaddr *addr, int *addr_len)
{
	struct socket *sock;
	struct iovec iov;
	struct msghdr msg;
	char address[MAX_SOCK_ADDR];
	int err,err2;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	msg.msg_control=NULL;
	msg.msg_controllen=0;
	msg.msg_iovlen=1;
	msg.msg_iov=&iov;
	iov.iov_len=size;
	iov.iov_base=ubuf;
	msg.msg_name=address;
	msg.msg_namelen=MAX_SOCK_ADDR;
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err=sock_recvmsg(sock, &msg, size, flags);

	if(err >= 0 && addr != NULL)
	{
		err2=move_addr_to_user(address, msg.msg_namelen, addr, addr_len);
		if(err2<0)
			err=err2;
	}
	sockfd_put(sock);			
out:
	return err;
}

/*
 *	Receive a datagram from a socket. 
 */

asmlinkage long sys_recv(int fd, void * ubuf, size_t size, unsigned flags)
{
	return sys_recvfrom(fd, ubuf, size, flags, NULL, NULL);
}

/*
 *	Set a socket option. Because we don't know the option lengths we have
 *	to pass the user mode parameter for the protocols to sort out.
 */

asmlinkage long sys_setsockopt(int fd, int level, int optname, char *optval, int optlen)
{
	int err;
	struct socket *sock;
	
	if ((sock = sockfd_lookup(fd, &err))!=NULL)
	{
		if (level == SOL_SOCKET)
			err=sock_setsockopt(sock,level,optname,optval,optlen);
		else
			err=sock->ops->setsockopt(sock, level, optname, optval, optlen);
		sockfd_put(sock);
	}
	return err;
}

/*
 *	Get a socket option. Because we don't know the option lengths we have
 *	to pass a user mode parameter for the protocols to sort out.
 */

asmlinkage long sys_getsockopt(int fd, int level, int optname, char *optval, int *optlen)
{
	int err;
	struct socket *sock;

	if ((sock = sockfd_lookup(fd, &err))!=NULL)
	{
		if (level == SOL_SOCKET)
			err=sock_getsockopt(sock,level,optname,optval,optlen);
		else
			err=sock->ops->getsockopt(sock, level, optname, optval, optlen);
		sockfd_put(sock);
	}
	return err;
}


/*
 *	Shutdown a socket.
 */

asmlinkage long sys_shutdown(int fd, int how)
{
	int err;
	struct socket *sock;

	if ((sock = sockfd_lookup(fd, &err))!=NULL)
	{
		err=sock->ops->shutdown(sock, how);
		sockfd_put(sock);
	}
	return err;
}

/*
 *	BSD sendmsg interface
 */

asmlinkage long sys_sendmsg(int fd, struct msghdr *msg, unsigned flags)
{
	struct socket *sock;
	char address[MAX_SOCK_ADDR];
	struct iovec iovstack[UIO_FASTIOV], *iov = iovstack;
	unsigned char ctl[sizeof(struct cmsghdr) + 20];	/* 20 is size of ipv6_pktinfo */
	unsigned char *ctl_buf = ctl;
	struct msghdr msg_sys;
	int err, ctl_len, iov_size, total_len;
	
	err = -EFAULT;
	if (copy_from_user(&msg_sys,msg,sizeof(struct msghdr)))
		goto out; 

	sock = sockfd_lookup(fd, &err);
	if (!sock) 
		goto out;

	/* do not move before msg_sys is valid */
	err = -EINVAL;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;

	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/* This will also move the address data into kernel space */
	err = verify_iovec(&msg_sys, iov, address, VERIFY_READ);
	if (err < 0) 
		goto out_freeiov;
	total_len = err;

	err = -ENOBUFS;

	if (msg_sys.msg_controllen > INT_MAX)
		goto out_freeiov;
	ctl_len = msg_sys.msg_controllen; 
	if (ctl_len) 
	{
		if (ctl_len > sizeof(ctl))
		{
			err = -ENOBUFS;
			ctl_buf = sock_kmalloc(sock->sk, ctl_len, GFP_KERNEL);
			if (ctl_buf == NULL) 
				goto out_freeiov;
		}
		err = -EFAULT;
		if (copy_from_user(ctl_buf, msg_sys.msg_control, ctl_len))
			goto out_freectl;
		msg_sys.msg_control = ctl_buf;
	}
	msg_sys.msg_flags = flags;

	if (sock->file->f_flags & O_NONBLOCK)
		msg_sys.msg_flags |= MSG_DONTWAIT;
	err = sock_sendmsg(sock, &msg_sys, total_len);

out_freectl:
	if (ctl_buf != ctl)    
		sock_kfree_s(sock->sk, ctl_buf, ctl_len);
out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:       
	return err;
}

/*
 *	BSD recvmsg interface
 */

asmlinkage long sys_recvmsg(int fd, struct msghdr *msg, unsigned int flags)
{
	struct socket *sock;
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov=iovstack;
	struct msghdr msg_sys;
	unsigned long cmsg_ptr;
	int err, iov_size, total_len, len;

	/* kernel mode address */
	char addr[MAX_SOCK_ADDR];

	/* user mode address pointers */
	struct sockaddr *uaddr;
	int *uaddr_len;
	
	err=-EFAULT;
	if (copy_from_user(&msg_sys,msg,sizeof(struct msghdr)))
		goto out;

	sock = sockfd_lookup(fd, &err);
	if (!sock)
		goto out;

	err = -EINVAL;
	if (msg_sys.msg_iovlen > UIO_MAXIOV)
		goto out_put;
	
	/* Check whether to allocate the iovec area*/
	err = -ENOMEM;
	iov_size = msg_sys.msg_iovlen * sizeof(struct iovec);
	if (msg_sys.msg_iovlen > UIO_FASTIOV) {
		iov = sock_kmalloc(sock->sk, iov_size, GFP_KERNEL);
		if (!iov)
			goto out_put;
	}

	/*
	 *	Save the user-mode address (verify_iovec will change the
	 *	kernel msghdr to use the kernel address space)
	 */
	 
	uaddr = msg_sys.msg_name;
	uaddr_len = &msg->msg_namelen;
	err = verify_iovec(&msg_sys, iov, addr, VERIFY_WRITE);
	if (err < 0)
		goto out_freeiov;
	total_len=err;

	cmsg_ptr = (unsigned long)msg_sys.msg_control;
	msg_sys.msg_flags = 0;
	
	if (sock->file->f_flags & O_NONBLOCK)
		flags |= MSG_DONTWAIT;
	err = sock_recvmsg(sock, &msg_sys, total_len, flags);
	if (err < 0)
		goto out_freeiov;
	len = err;

	if (uaddr != NULL) {
		err = move_addr_to_user(addr, msg_sys.msg_namelen, uaddr, uaddr_len);
		if (err < 0)
			goto out_freeiov;
	}
	err = __put_user(msg_sys.msg_flags, &msg->msg_flags);
	if (err)
		goto out_freeiov;
	err = __put_user((unsigned long)msg_sys.msg_control-cmsg_ptr, 
							 &msg->msg_controllen);
	if (err)
		goto out_freeiov;
	err = len;

out_freeiov:
	if (iov != iovstack)
		sock_kfree_s(sock->sk, iov, iov_size);
out_put:
	sockfd_put(sock);
out:
	return err;
}


/*
 *	Perform a file control on a socket file descriptor.
 *
 *	Doesn't aquire a fd lock, because no network fcntl
 *	function sleeps currently.
 */

int sock_fcntl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct socket *sock;

	sock = socki_lookup (filp->f_dentry->d_inode);
	if (sock && sock->ops)
		return sock->ops->fcntl(sock, cmd, arg);
	return(-EINVAL);
}

/* Argument list sizes for sys_socketcall */
#define AL(x) ((x) * sizeof(unsigned long))
static unsigned char nargs[18]={AL(0),AL(3),AL(3),AL(3),AL(2),AL(3),
				AL(3),AL(3),AL(4),AL(4),AL(4),AL(6),
				AL(6),AL(2),AL(5),AL(5),AL(3),AL(3)};
#undef AL

/*
 *	System call vectors. 
 *
 *	Argument checking cleaned up. Saved 20% in size.
 *  This function doesn't need to set the kernel lock because
 *  it is set by the callees. 
 */

asmlinkage long sys_socketcall(int call, unsigned long *args)
{
	unsigned long a[6];
	unsigned long a0,a1;
	int err;

	if(call<1||call>SYS_RECVMSG)
		return -EINVAL;

	/* copy_from_user should be SMP safe. */
	if (copy_from_user(a, args, nargs[call]))
		return -EFAULT;
		
	a0=a[0];
	a1=a[1];
	
	switch(call) 
	{
		case SYS_SOCKET:
			err = sys_socket(a0,a1,a[2]);
			break;
		case SYS_BIND:
			err = sys_bind(a0,(struct sockaddr *)a1, a[2]);
			break;
		case SYS_CONNECT:
			err = sys_connect(a0, (struct sockaddr *)a1, a[2]);
			break;
		case SYS_LISTEN:
			err = sys_listen(a0,a1);
			break;
		case SYS_ACCEPT:
			err = sys_accept(a0,(struct sockaddr *)a1, (int *)a[2]);
			break;
		case SYS_GETSOCKNAME:
			err = sys_getsockname(a0,(struct sockaddr *)a1, (int *)a[2]);
			break;
		case SYS_GETPEERNAME:
			err = sys_getpeername(a0, (struct sockaddr *)a1, (int *)a[2]);
			break;
		case SYS_SOCKETPAIR:
			err = sys_socketpair(a0,a1, a[2], (int *)a[3]);
			break;
		case SYS_SEND:
			err = sys_send(a0, (void *)a1, a[2], a[3]);
			break;
		case SYS_SENDTO:
			err = sys_sendto(a0,(void *)a1, a[2], a[3],
					 (struct sockaddr *)a[4], a[5]);
			break;
		case SYS_RECV:
			err = sys_recv(a0, (void *)a1, a[2], a[3]);
			break;
		case SYS_RECVFROM:
			err = sys_recvfrom(a0, (void *)a1, a[2], a[3],
					   (struct sockaddr *)a[4], (int *)a[5]);
			break;
		case SYS_SHUTDOWN:
			err = sys_shutdown(a0,a1);
			break;
		case SYS_SETSOCKOPT:
			err = sys_setsockopt(a0, a1, a[2], (char *)a[3], a[4]);
			break;
		case SYS_GETSOCKOPT:
			err = sys_getsockopt(a0, a1, a[2], (char *)a[3], (int *)a[4]);
			break;
		case SYS_SENDMSG:
			err = sys_sendmsg(a0, (struct msghdr *) a1, a[2]);
			break;
		case SYS_RECVMSG:
			err = sys_recvmsg(a0, (struct msghdr *) a1, a[2]);
			break;
		default:
			err = -EINVAL;
			break;
	}
	return err;
}

/*
 *	This function is called by a protocol handler that wants to
 *	advertise its address family, and have it linked into the
 *	SOCKET module.
 */

int sock_register(struct net_proto_family *ops)
{
	int err;

	if (ops->family >= NPROTO) {
		printk(KERN_CRIT "protocol %d >= NPROTO(%d)\n", ops->family, NPROTO);
		return -ENOBUFS;
	}
	net_family_write_lock();
	err = -EEXIST;
	if (net_families[ops->family] == NULL) {
		net_families[ops->family]=ops;
		err = 0;
	}
	net_family_write_unlock();
	return err;
}

/*
 *	This function is called by a protocol handler that wants to
 *	remove its address family, and have it unlinked from the
 *	SOCKET module.
 */

int sock_unregister(int family)
{
	if (family < 0 || family >= NPROTO)
		return -1;

	net_family_write_lock();
	net_families[family]=NULL;
	net_family_write_unlock();
	return 0;
}

void __init proto_init(void)
{
	extern struct net_proto protocols[];	/* Network protocols */
	struct net_proto *pro;

	/* Kick all configured protocols. */
	pro = protocols;
	while (pro->name != NULL) 
	{
		(*pro->init_func)(pro);
		pro++;
	}
	/* We're all done... */
}

extern void sk_init(void);
#ifdef CONFIG_WAN_ROUTER
extern void wanrouter_init(void);
#endif

void __init sock_init(void)
{
	int i;

	printk(KERN_INFO "Linux NET4.0 for Linux 2.3\n");
	printk(KERN_INFO "Based upon Swansea University Computer Society NET3.039\n");

	/*
	 *	Initialize all address (protocol) families. 
	 */
	 
	for (i = 0; i < NPROTO; i++) 
		net_families[i] = NULL;

	/*
	 *	Initialize sock SLAB cache.
	 */
	 
	sk_init();

#ifdef SLAB_SKB
	/*
	 *	Initialize skbuff SLAB cache 
	 */
	skb_init();
#endif


	/*
	 *	Wan router layer. 
	 */

#ifdef CONFIG_WAN_ROUTER	 
	wanrouter_init();
#endif

	/*
	 *	Initialize the protocols module. 
	 */

	proto_init();

	/*
	 *	The netlink device handler may be needed early.
	 */

#ifdef  CONFIG_RTNETLINK
	rtnetlink_init();
#endif
#ifdef CONFIG_NETLINK_DEV
	init_netlink();
#endif
#ifdef CONFIG_NETFILTER
	netfilter_init();
#endif
}

int socket_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len = sprintf(buffer, "sockets: used %d\n", sockets_in_use);
	if (offset >= len)
	{
		*start = buffer;
		return 0;
	}
	*start = buffer + offset;
	len -= offset;
	if (len > length)
		len = length;
	return len;
}
