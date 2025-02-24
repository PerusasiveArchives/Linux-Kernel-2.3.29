/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		PF_INET protocol family socket handler.
 *
 * Version:	$Id: af_inet.c,v 1.97 1999/09/08 03:46:46 davem Exp $
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Florian La Roche, <flla@stud.uni-sb.de>
 *		Alan Cox, <A.Cox@swansea.ac.uk>
 *
 * Changes (see also sock.c)
 *
 *		A.N.Kuznetsov	:	Socket death error in accept().
 *		John Richardson :	Fix non blocking error in connect()
 *					so sockets that fail to connect
 *					don't return -EINPROGRESS.
 *		Alan Cox	:	Asynchronous I/O support
 *		Alan Cox	:	Keep correct socket pointer on sock structures
 *					when accept() ed
 *		Alan Cox	:	Semantics of SO_LINGER aren't state moved
 *					to close when you look carefully. With
 *					this fixed and the accept bug fixed 
 *					some RPC stuff seems happier.
 *		Niibe Yutaka	:	4.4BSD style write async I/O
 *		Alan Cox, 
 *		Tony Gale 	:	Fixed reuse semantics.
 *		Alan Cox	:	bind() shouldn't abort existing but dead
 *					sockets. Stops FTP netin:.. I hope.
 *		Alan Cox	:	bind() works correctly for RAW sockets. Note
 *					that FreeBSD at least was broken in this respect
 *					so be careful with compatibility tests...
 *		Alan Cox	:	routing cache support
 *		Alan Cox	:	memzero the socket structure for compactness.
 *		Matt Day	:	nonblock connect error handler
 *		Alan Cox	:	Allow large numbers of pending sockets
 *					(eg for big web sites), but only if
 *					specifically application requested.
 *		Alan Cox	:	New buffering throughout IP. Used dumbly.
 *		Alan Cox	:	New buffering now used smartly.
 *		Alan Cox	:	BSD rather than common sense interpretation of
 *					listen.
 *		Germano Caronni	:	Assorted small races.
 *		Alan Cox	:	sendmsg/recvmsg basic support.
 *		Alan Cox	:	Only sendmsg/recvmsg now supported.
 *		Alan Cox	:	Locked down bind (see security list).
 *		Alan Cox	:	Loosened bind a little.
 *		Mike McLagan	:	ADD/DEL DLCI Ioctls
 *	Willy Konynenberg	:	Transparent proxying support.
 *		David S. Miller	:	New socket lookup architecture.
 *					Some other random speedups.
 *		Cyrus Durgin	:	Cleaned up file for kmod hacks.
 *		Andi Kleen	:	Fix inet_stream_connect TCP race.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/netfilter_ipv4.h>

#include <asm/uaccess.h>
#include <asm/system.h>

#include <linux/smp_lock.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <net/arp.h>
#include <net/route.h>
#include <net/tcp.h>
#include <net/udp.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/raw.h>
#include <net/icmp.h>
#include <net/ipip.h>
#include <net/inet_common.h>
#ifdef CONFIG_IP_MROUTE
#include <linux/mroute.h>
#endif
#ifdef CONFIG_BRIDGE
#include <net/br.h>
#endif
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif
#ifdef CONFIG_NET_RADIO
#include <linux/wireless.h>
#endif	/* CONFIG_NET_RADIO */

#define min(a,b)	((a)<(b)?(a):(b))

struct linux_mib net_statistics;

atomic_t inet_sock_nr;

extern int raw_get_info(char *, char **, off_t, int);
extern int snmp_get_info(char *, char **, off_t, int);
extern int netstat_get_info(char *, char **, off_t, int);
extern int afinet_get_info(char *, char **, off_t, int);
extern int tcp_get_info(char *, char **, off_t, int);
extern int udp_get_info(char *, char **, off_t, int);
extern void ip_mc_drop_socket(struct sock *sk);

#ifdef CONFIG_DLCI
extern int dlci_ioctl(unsigned int, void*);
#endif

#ifdef CONFIG_DLCI_MODULE
int (*dlci_ioctl_hook)(unsigned int, void *) = NULL;
#endif

/* New destruction routine */

void inet_sock_destruct(struct sock *sk)
{
	__skb_queue_purge(&sk->receive_queue);
	__skb_queue_purge(&sk->error_queue);

	if (sk->type == SOCK_STREAM && sk->state != TCP_CLOSE) {
		printk("Attempt to release TCP socket in state %d %p\n",
		       sk->state,
		       sk);
		return;
	}
	if (!sk->dead) {
		printk("Attempt to release alive inet socket %p\n", sk);
		return;
	}

	BUG_TRAP(atomic_read(&sk->rmem_alloc) == 0);
	BUG_TRAP(atomic_read(&sk->wmem_alloc) == 0);

	if (sk->protinfo.af_inet.opt)
		kfree(sk->protinfo.af_inet.opt);
	dst_release(sk->dst_cache);
	atomic_dec(&inet_sock_nr);
#ifdef INET_REFCNT_DEBUG
	printk(KERN_DEBUG "INET socket %p released, %d are still alive\n", sk, atomic_read(&inet_sock_nr));
#endif
}

void inet_sock_release(struct sock *sk)
{
	if (sk->prot->destroy)
		sk->prot->destroy(sk);

	/* Observation: when inet_sock_release is called, processes have
	   no access to socket. But net still has.
	   Step one, detach it from networking:

	   A. Remove from hash tables.
	 */

	sk->prot->unhash(sk);

	/* In this point socket cannot receive new packets,
	   but it is possible that some packets are in flight
	   because some CPU runs receiver and did hash table lookup
	   before we unhashed socket. They will achieve receive queue
	   and will be purged by socket destructor.

	   Also we still have packets pending on receive
	   queue and probably, our own packets waiting in device queues.
	   sock_destroy will drain receive queue, but transmitted
	   packets will delay socket destruction until the last reference
	   will be released.
	 */

	write_lock_irq(&sk->callback_lock);
	sk->dead=1;
	sk->socket = NULL;
	sk->sleep = NULL;
	write_unlock_irq(&sk->callback_lock);

#ifdef INET_REFCNT_DEBUG
	if (atomic_read(&sk->refcnt) != 1) {
		printk(KERN_DEBUG "Destruction inet %p delayed, c=%d\n", sk, atomic_read(&sk->refcnt));
	}
#endif
	sock_put(sk);
}


/*
 *	The routines beyond this point handle the behaviour of an AF_INET
 *	socket object. Mostly it punts to the subprotocols of IP to do
 *	the work.
 */
 

/*
 *	Set socket options on an inet socket.
 */
 
int inet_setsockopt(struct socket *sock, int level, int optname,
		    char *optval, int optlen)
{
	struct sock *sk=sock->sk;
	if (sk->prot->setsockopt==NULL)
		return -EOPNOTSUPP;
	return sk->prot->setsockopt(sk,level,optname,optval,optlen);
}

/*
 *	Get a socket option on an AF_INET socket.
 *
 *	FIX: POSIX 1003.1g is very ambiguous here. It states that
 *	asynchronous errors should be reported by getsockopt. We assume
 *	this means if you specify SO_ERROR (otherwise whats the point of it).
 */

int inet_getsockopt(struct socket *sock, int level, int optname,
		    char *optval, int *optlen)
{
	struct sock *sk=sock->sk;
	if (sk->prot->getsockopt==NULL)
		return -EOPNOTSUPP;
	return sk->prot->getsockopt(sk,level,optname,optval,optlen);
}

/*
 *	Automatically bind an unbound socket.
 */

static int inet_autobind(struct sock *sk)
{
	/* We may need to bind the socket. */
	lock_sock(sk);
	if (sk->num == 0) {
		if (sk->prot->get_port(sk, 0) != 0) {
			release_sock(sk);
			return -EAGAIN;
		}
		sk->sport = htons(sk->num);
		sk->prot->hash(sk);
	}
	release_sock(sk);
	return 0;
}

/* Listening INET sockets never sleep to wait for memory, so
 * it is completely silly to wake them up on queue space
 * available events.  So we hook them up to this dummy callback.
 */
static void inet_listen_write_space(struct sock *sk)
{
}

/*
 *	Move a socket into listening state.
 */
 
int inet_listen(struct socket *sock, int backlog)
{
	struct sock *sk = sock->sk;
	unsigned char old_state;
	int err;

	if (sock->state != SS_UNCONNECTED || sock->type != SOCK_STREAM)
		return -EINVAL;

	lock_sock(sk);
	old_state = sk->state;
	err = -EINVAL;
	if (!((1<<old_state)&(TCPF_CLOSE|TCPF_LISTEN)))
		goto out;

	/* Really, if the socket is already in listen state
	 * we can only allow the backlog to be adjusted.
	 */
	if (old_state != TCP_LISTEN) {
		sk->state = TCP_LISTEN;
		sk->ack_backlog = 0;
		if (sk->num == 0) {
			if (sk->prot->get_port(sk, 0) != 0) {
				sk->state = old_state;
				err = -EAGAIN;
				goto out;
			}
			sk->sport = htons(sk->num);
		} else {
			/* Not nice, but the simplest solution however */
			if (sk->prev)
				((struct tcp_bind_bucket*)sk->prev)->fastreuse = 0;
		}

		sk_dst_reset(sk);
		sk->prot->hash(sk);
		sk->socket->flags |= SO_ACCEPTCON;
		sk->write_space = inet_listen_write_space;
	}
	sk->max_ack_backlog = backlog;
	err = 0;

out:
	release_sock(sk);
	return err;
}

/*
 *	Create an inet socket.
 *
 *	FIXME: Gcc would generate much better code if we set the parameters
 *	up in in-memory structure order. Gcc68K even more so
 */

static int inet_create(struct socket *sock, int protocol)
{
	struct sock *sk;
	struct proto *prot;

	sock->state = SS_UNCONNECTED;
	sk = sk_alloc(PF_INET, GFP_KERNEL, 1);
	if (sk == NULL) 
		goto do_oom;

	switch (sock->type) {
	case SOCK_STREAM:
		if (protocol && protocol != IPPROTO_TCP)
			goto free_and_noproto;
		protocol = IPPROTO_TCP;
		if (ipv4_config.no_pmtu_disc)
			sk->protinfo.af_inet.pmtudisc = IP_PMTUDISC_DONT;
		else
			sk->protinfo.af_inet.pmtudisc = IP_PMTUDISC_WANT;
		prot = &tcp_prot;
		sock->ops = &inet_stream_ops;
		break;
	case SOCK_SEQPACKET:
		goto free_and_badtype;
	case SOCK_DGRAM:
		if (protocol && protocol != IPPROTO_UDP)
			goto free_and_noproto;
		protocol = IPPROTO_UDP;
		sk->no_check = UDP_CSUM_DEFAULT;
		sk->protinfo.af_inet.pmtudisc = IP_PMTUDISC_DONT;
		prot=&udp_prot;
		sock->ops = &inet_dgram_ops;
		break;
	case SOCK_RAW:
		if (!capable(CAP_NET_RAW))
			goto free_and_badperm;
		if (!protocol)
			goto free_and_noproto;
		prot = &raw_prot;
		sk->reuse = 1;
		sk->protinfo.af_inet.pmtudisc = IP_PMTUDISC_DONT;
		sk->num = protocol;
		sock->ops = &inet_dgram_ops;
		if (protocol == IPPROTO_RAW)
			sk->protinfo.af_inet.hdrincl = 1;
		break;
	default:
		goto free_and_badtype;
	}

	sock_init_data(sock,sk);

	sk->destruct = inet_sock_destruct;

	sk->zapped=0;
#ifdef CONFIG_TCP_NAGLE_OFF
	sk->nonagle = 1;
#endif  
	sk->family = PF_INET;
	sk->protocol = protocol;

	sk->prot = prot;
	sk->backlog_rcv = prot->backlog_rcv;

	sk->timer.data = (unsigned long)sk;
	sk->timer.function = &tcp_keepalive_timer;

	sk->protinfo.af_inet.ttl=ip_statistics.IpDefaultTTL;

	sk->protinfo.af_inet.mc_loop=1;
	sk->protinfo.af_inet.mc_ttl=1;
	sk->protinfo.af_inet.mc_index=0;
	sk->protinfo.af_inet.mc_list=NULL;

	atomic_inc(&inet_sock_nr);

	if (sk->num) {
		/* It assumes that any protocol which allows
		 * the user to assign a number at socket
		 * creation time automatically
		 * shares.
		 */
		sk->sport = htons(sk->num);

		/* Add to protocol hash chains. */
		sk->prot->hash(sk);
	}

	if (sk->prot->init) {
		int err = sk->prot->init(sk);
		if (err != 0) {
			sk->dead = 1;
			inet_sock_release(sk);
			return(err);
		}
	}
	return(0);

free_and_badtype:
	sk_free(sk);
	return -ESOCKTNOSUPPORT;

free_and_badperm:
	sk_free(sk);
	return -EPERM;

free_and_noproto:
	sk_free(sk);
	return -EPROTONOSUPPORT;

do_oom:
	return -ENOBUFS;
}


/*
 *	The peer socket should always be NULL (or else). When we call this
 *	function we are destroying the object and from then on nobody
 *	should refer to it.
 */
 
int inet_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		long timeout;

		/* Applications forget to leave groups before exiting */
		ip_mc_drop_socket(sk);

		/* If linger is set, we don't return until the close
		 * is complete.  Otherwise we return immediately. The
		 * actually closing is done the same either way.
		 *
		 * If the close is due to the process exiting, we never
		 * linger..
		 */
		timeout = 0;
		if (sk->linger && !(current->flags & PF_EXITING)) {
			timeout = HZ * sk->lingertime;
			if (!timeout)
				timeout = MAX_SCHEDULE_TIMEOUT;
		}
		sock->sk = NULL;
		sk->prot->close(sk, timeout);
	}
	return(0);
}

static int inet_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_in *addr=(struct sockaddr_in *)uaddr;
	struct sock *sk=sock->sk;
	unsigned short snum;
	int chk_addr_ret;
	int err;

	/* If the socket has its own bind function then use it. (RAW) */
	if(sk->prot->bind)
		return sk->prot->bind(sk, uaddr, addr_len);

	if (addr_len < sizeof(struct sockaddr_in))
		return -EINVAL;
		
	chk_addr_ret = inet_addr_type(addr->sin_addr.s_addr);
	if (addr->sin_addr.s_addr != 0 && chk_addr_ret != RTN_LOCAL &&
	    chk_addr_ret != RTN_MULTICAST && chk_addr_ret != RTN_BROADCAST) {
		return -EADDRNOTAVAIL;	/* Source address MUST be ours! */
	}

	snum = ntohs(addr->sin_port);
	if (snum && snum < PROT_SOCK && !capable(CAP_NET_BIND_SERVICE))
		return -EACCES;

	/*      We keep a pair of addresses. rcv_saddr is the one
	 *      used by hash lookups, and saddr is used for transmit.
	 *
	 *      In the BSD API these are the same except where it
	 *      would be illegal to use them (multicast/broadcast) in
	 *      which case the sending device address is used.
	 */
	lock_sock(sk);

	/* Check these errors (active socket, double bind). */
	err = -EINVAL;
	if ((sk->state != TCP_CLOSE)			||
	    (sk->num != 0))
		goto out;

	sk->rcv_saddr = sk->saddr = addr->sin_addr.s_addr;
	if (chk_addr_ret == RTN_MULTICAST || chk_addr_ret == RTN_BROADCAST)
		sk->saddr = 0;  /* Use device */

	/* Make sure we are allowed to bind here. */
	if (sk->prot->get_port(sk, snum) != 0) {
		sk->saddr = sk->rcv_saddr = 0;
		err = -EADDRINUSE;
		goto out;
	}

	sk->sport = htons(sk->num);
	sk->daddr = 0;
	sk->dport = 0;
	sk->prot->hash(sk);
	sk_dst_reset(sk);
	err = 0;
out:
	release_sock(sk);
	return err;
}

int inet_dgram_connect(struct socket *sock, struct sockaddr * uaddr,
		       int addr_len, int flags)
{
	struct sock *sk=sock->sk;

	if (uaddr->sa_family == AF_UNSPEC)
		return sk->prot->disconnect(sk, flags);

	if (sk->num==0 && inet_autobind(sk) != 0)
		return -EAGAIN;
	return sk->prot->connect(sk, (struct sockaddr *)uaddr, addr_len);
}

static void inet_wait_for_connect(struct sock *sk)
{
	DECLARE_WAITQUEUE(wait, current);

	__set_current_state(TASK_INTERRUPTIBLE);
	add_wait_queue(sk->sleep, &wait);

	while ((1<<sk->state)&(TCPF_SYN_SENT|TCPF_SYN_RECV)) {
		if (signal_pending(current))
			break;
		if (sk->err)
			break;
		release_sock(sk);
		schedule();
		lock_sock(sk);
		set_current_state(TASK_INTERRUPTIBLE);
	}
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(sk->sleep, &wait);
}

/*
 *	Connect to a remote host. There is regrettably still a little
 *	TCP 'magic' in here.
 */
 
int inet_stream_connect(struct socket *sock, struct sockaddr * uaddr,
			int addr_len, int flags)
{
	struct sock *sk=sock->sk;
	int err;

	if (uaddr->sa_family == AF_UNSPEC) {
		lock_sock(sk);
		err = sk->prot->disconnect(sk, flags);
		sock->state = err ? SS_DISCONNECTING : SS_UNCONNECTED;
		release_sock(sk);
		return err;
	}

	lock_sock(sk);
	switch (sock->state) {
	default:
		err = -EINVAL;
		goto out;
	case SS_CONNECTED:
		err = -EISCONN;
		goto out;
	case SS_CONNECTING:
		if (tcp_established(sk->state)) {
			sock->state = SS_CONNECTED;
			err = 0;
			goto out;
		}
		if (sk->err)
			goto sock_error;
		err = -EALREADY;
		if (flags & O_NONBLOCK)
			goto out;
		break;
	case SS_UNCONNECTED:
		err = sk->prot->connect(sk, uaddr, addr_len);
		if (err < 0)
			goto out;
  		sock->state = SS_CONNECTING;
	}

	if (sk->state > TCP_FIN_WAIT2)
		goto sock_error;

	err = -EINPROGRESS;
	if (!tcp_established(sk->state) && (flags & O_NONBLOCK))
		goto out;

	if ((1<<sk->state)&(TCPF_SYN_SENT|TCPF_SYN_RECV)) {
		inet_wait_for_connect(sk);
		err = -ERESTARTSYS;
		if (signal_pending(current))
			goto out;
	}

	if (sk->err && !tcp_established(sk->state))
		goto sock_error; 
	sock->state = SS_CONNECTED;
	err = 0;
out:
	release_sock(sk);
	return err;

sock_error:
	err = sock_error(sk) ? : -ECONNABORTED;
	sock->state = SS_UNCONNECTED;
	if (sk->prot->disconnect(sk, O_NONBLOCK))
		sock->state = SS_DISCONNECTING;
	release_sock(sk);

	return err;
}

/*
 *	Accept a pending connection. The TCP layer now gives BSD semantics.
 */

int inet_accept(struct socket *sock, struct socket *newsock, int flags)
{
	struct sock *sk1 = sock->sk;
	struct sock *sk2;
	int err = -EINVAL;

	if((sk2 = sk1->prot->accept(sk1,flags,&err)) == NULL)
		goto do_err;

	lock_sock(sk2);

	BUG_TRAP((1<<sk2->state)&(TCPF_ESTABLISHED|TCPF_CLOSE_WAIT|TCPF_CLOSE));

	write_lock_irq(&sk2->callback_lock);
	sk2->sleep = &newsock->wait;
	newsock->sk = sk2;
	sk2->socket = newsock;
	write_unlock_irq(&sk2->callback_lock);

	newsock->state = SS_CONNECTED;
	release_sock(sk2);
	return 0;

do_err:
	return err;
}


/*
 *	This does both peername and sockname.
 */
 
static int inet_getname(struct socket *sock, struct sockaddr *uaddr,
		 int *uaddr_len, int peer)
{
	struct sock *sk		= sock->sk;
	struct sockaddr_in *sin	= (struct sockaddr_in *)uaddr;
  
	sin->sin_family = AF_INET;
	if (peer) {
		if (!sk->dport) 
			return -ENOTCONN;
		sin->sin_port = sk->dport;
		sin->sin_addr.s_addr = sk->daddr;
	} else {
		__u32 addr = sk->rcv_saddr;
		if (!addr)
			addr = sk->saddr;
		sin->sin_port = sk->sport;
		sin->sin_addr.s_addr = addr;
	}
	*uaddr_len = sizeof(*sin);
	return(0);
}



int inet_recvmsg(struct socket *sock, struct msghdr *msg, int size,
		 int flags, struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;
	int addr_len = 0;
	int err;
	
	/* We may need to bind the socket. */
	/* It is pretty strange. I would return error in this case --ANK */
	if (sk->num==0 && inet_autobind(sk) != 0)
		return -EAGAIN;
	err = sk->prot->recvmsg(sk, msg, size, flags&MSG_DONTWAIT,
				flags&~MSG_DONTWAIT, &addr_len);
	if (err >= 0)
		msg->msg_namelen = addr_len;
	return err;
}


int inet_sendmsg(struct socket *sock, struct msghdr *msg, int size,
		 struct scm_cookie *scm)
{
	struct sock *sk = sock->sk;

	/* We may need to bind the socket. */
	if (sk->num==0 && inet_autobind(sk) != 0)
		return -EAGAIN;

	return sk->prot->sendmsg(sk, msg, size);
}

int inet_shutdown(struct socket *sock, int how)
{
	struct sock *sk = sock->sk;
	int err;

	/* This should really check to make sure
	 * the socket is a TCP socket. (WHY AC...)
	 */
	how++; /* maps 0->1 has the advantage of making bit 1 rcvs and
		       1->2 bit 2 snds.
		       2->3 */
	if ((how & ~SHUTDOWN_MASK) || how==0)	/* MAXINT->0 */
		return -EINVAL;
	if (!sk)
		return -ENOTCONN;

	lock_sock(sk);
	if (sock->state == SS_CONNECTING && tcp_established(sk->state))
		sock->state = SS_CONNECTED;
	err = -ENOTCONN;
	if (!tcp_connected(sk->state))
		goto out;
	sk->shutdown |= how;
	if (sk->prot->shutdown)
		sk->prot->shutdown(sk, how);
	/* Wake up anyone sleeping in poll. */
	sk->state_change(sk);
	err = 0;
out:
	release_sock(sk);
	return err;
}

unsigned int inet_poll(struct file * file, struct socket *sock, poll_table *wait)
{
	struct sock *sk = sock->sk;

	if (sk->prot->poll == NULL)
		return(0);
	return sk->prot->poll(file, sock, wait);
}

/*
 *	ioctl() calls you can issue on an INET socket. Most of these are
 *	device configuration and stuff and very rarely used. Some ioctls
 *	pass on to the socket itself.
 *
 *	NOTE: I like the idea of a module for the config stuff. ie ifconfig
 *	loads the devconfigure module does its configuring and unloads it.
 *	There's a good 20K of config code hanging around the kernel.
 */

static int inet_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;
	int err;
	int pid;

	switch(cmd) 
	{
		case FIOSETOWN:
		case SIOCSPGRP:
			err = get_user(pid, (int *) arg);
			if (err)
				return err; 
			if (current->pid != pid && current->pgrp != -pid && 
			    !capable(CAP_NET_ADMIN))
				return -EPERM;
			sk->proc = pid;
			return(0);
		case FIOGETOWN:
		case SIOCGPGRP:
			return put_user(sk->proc, (int *)arg);
		case SIOCGSTAMP:
			if(sk->stamp.tv_sec==0)
				return -ENOENT;
			err = copy_to_user((void *)arg,&sk->stamp,sizeof(struct timeval));
			if (err)
				err = -EFAULT;
			return err;
		case SIOCADDRT:
		case SIOCDELRT:
		case SIOCRTMSG:
			return(ip_rt_ioctl(cmd,(void *) arg));
		case SIOCDARP:
		case SIOCGARP:
		case SIOCSARP:
			return(arp_ioctl(cmd,(void *) arg));
		case SIOCGIFADDR:
		case SIOCSIFADDR:
		case SIOCGIFBRDADDR:
		case SIOCSIFBRDADDR:
		case SIOCGIFNETMASK:
		case SIOCSIFNETMASK:
		case SIOCGIFDSTADDR:
		case SIOCSIFDSTADDR:
		case SIOCSIFPFLAGS:	
		case SIOCGIFPFLAGS:	
		case SIOCSIFFLAGS:
			return(devinet_ioctl(cmd,(void *) arg));
		case SIOCGIFBR:
		case SIOCSIFBR:
#ifdef CONFIG_BRIDGE
			lock_kernel();
			err = br_ioctl(cmd,(void *) arg);
			unlock_kernel();
			return err;
#else
			return -ENOPKG;
#endif						
			
		case SIOCADDDLCI:
		case SIOCDELDLCI:
#ifdef CONFIG_DLCI
			lock_kernel();
			err = dlci_ioctl(cmd, (void *) arg);
			unlock_kernel();
			return err;
#endif

#ifdef CONFIG_DLCI_MODULE

#ifdef CONFIG_KMOD
			if (dlci_ioctl_hook == NULL)
				request_module("dlci");
#endif

			if (dlci_ioctl_hook) {
				lock_kernel();
				err = (*dlci_ioctl_hook)(cmd, (void *) arg);
				unlock_kernel();
				return err;
			}
#endif
			return -ENOPKG;

		default:
			if ((cmd >= SIOCDEVPRIVATE) &&
			    (cmd <= (SIOCDEVPRIVATE + 15)))
				return(dev_ioctl(cmd,(void *) arg));

#ifdef CONFIG_NET_RADIO
			if((cmd >= SIOCIWFIRST) && (cmd <= SIOCIWLAST))
				return(dev_ioctl(cmd,(void *) arg));
#endif

			if (sk->prot->ioctl==NULL || (err=sk->prot->ioctl(sk, cmd, arg))==-ENOIOCTLCMD)
				return(dev_ioctl(cmd,(void *) arg));		
			return err;
	}
	/*NOTREACHED*/
	return(0);
}

struct proto_ops inet_stream_ops = {
	PF_INET,

	inet_release,
	inet_bind,
	inet_stream_connect,
	sock_no_socketpair,
	inet_accept,
	inet_getname, 
	inet_poll,
	inet_ioctl,
	inet_listen,
	inet_shutdown,
	inet_setsockopt,
	inet_getsockopt,
	sock_no_fcntl,
	inet_sendmsg,
	inet_recvmsg,
	sock_no_mmap
};

struct proto_ops inet_dgram_ops = {
	PF_INET,

	inet_release,
	inet_bind,
	inet_dgram_connect,
	sock_no_socketpair,
	sock_no_accept,
	inet_getname, 
	datagram_poll,
	inet_ioctl,
	sock_no_listen,
	inet_shutdown,
	inet_setsockopt,
	inet_getsockopt,
	sock_no_fcntl,
	inet_sendmsg,
	inet_recvmsg,
	sock_no_mmap
};

struct net_proto_family inet_family_ops = {
	PF_INET,
	inet_create
};


extern void tcp_init(void);
extern void tcp_v4_init(struct net_proto_family *);


/*
 *	Called by socket.c on kernel startup.  
 */
 
void __init inet_proto_init(struct net_proto *pro)
{
	struct sk_buff *dummy_skb;
	struct inet_protocol *p;

	printk(KERN_INFO "NET4: Linux TCP/IP 1.0 for NET4.0\n");

	if (sizeof(struct inet_skb_parm) > sizeof(dummy_skb->cb))
	{
		printk(KERN_CRIT "inet_proto_init: panic\n");
		return;
	}

	/*
	 *	Tell SOCKET that we are alive... 
	 */
   
  	(void) sock_register(&inet_family_ops);

	/*
	 *	Add all the protocols. 
	 */

	printk(KERN_INFO "IP Protocols: ");
	for(p = inet_protocol_base; p != NULL;) 
	{
		struct inet_protocol *tmp = (struct inet_protocol *) p->next;
		inet_add_protocol(p);
		printk("%s%s",p->name,tmp?", ":"\n");
		p = tmp;
	}

	/*
	 *	Set the ARP module up
	 */

	arp_init();

  	/*
  	 *	Set the IP module up
  	 */

	ip_init();

	tcp_v4_init(&inet_family_ops);

	/* Setup TCP slab cache for open requests. */
	tcp_init();


	/*
	 *	Set the ICMP layer up
	 */

	icmp_init(&inet_family_ops);

	/* I wish inet_add_protocol had no constructor hook...
	   I had to move IPIP from net/ipv4/protocol.c :-( --ANK
	 */
#ifdef CONFIG_NET_IPIP
	ipip_init();
#endif
#ifdef CONFIG_NET_IPGRE
	ipgre_init();
#endif

	/*
	 *	Initialise the multicast router
	 */
#if defined(CONFIG_IP_MROUTE)
	ip_mr_init();
#endif

	/*
	 *	Create all the /proc entries.
	 */
#ifdef CONFIG_PROC_FS
	proc_net_create ("raw", 0, raw_get_info);
	proc_net_create ("netstat", 0, netstat_get_info);
	proc_net_create ("snmp", 0, snmp_get_info);
	proc_net_create ("sockstat", 0, afinet_get_info);
	proc_net_create ("tcp", 0, tcp_get_info);
	proc_net_create ("udp", 0, udp_get_info);
#endif		/* CONFIG_PROC_FS */
}
