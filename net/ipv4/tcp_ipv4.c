/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Implementation of the Transmission Control Protocol(TCP).
 *
 * Version:	$Id: tcp_ipv4.c,v 1.189 1999/09/07 02:31:33 davem Exp $
 *
 *		IPv4 specific functions
 *
 *
 *		code split from:
 *		linux/ipv4/tcp.c
 *		linux/ipv4/tcp_input.c
 *		linux/ipv4/tcp_output.c
 *
 *		See tcp.c for author information
 *
 *	This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 */

/*
 * Changes:
 *		David S. Miller	:	New socket lookup architecture.
 *					This code is dedicated to John Dyson.
 *		David S. Miller :	Change semantics of established hash,
 *					half is devoted to TIME_WAIT sockets
 *					and the rest go in the other half.
 *		Andi Kleen :		Add support for syncookies and fixed
 *					some bugs: ip options weren't passed to
 *					the TCP layer, missed a check for an ACK bit.
 *		Andi Kleen :		Implemented fast path mtu discovery.
 *	     				Fixed many serious bugs in the
 *					open_request handling and moved
 *					most of it into the af independent code.
 *					Added tail drop and some other bugfixes.
 *					Added new listen sematics.
 *		Mike McLagan	:	Routing by source
 *	Juan Jose Ciarlante:		ip_dynaddr bits
 *		Andi Kleen:		various fixes.
 *	Vitaly E. Lavrov	:	Transparent proxy revived after year coma.
 *	Andi Kleen		:	Fix new listen.
 *	Andi Kleen		:	Fix accept error reporting.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/random.h>
#include <linux/init.h>
#include <linux/ipsec.h>

#include <net/icmp.h>
#include <net/tcp.h>
#include <net/ipv6.h>
#include <net/inet_common.h>

#include <linux/inet.h>
#include <linux/stddef.h>

extern int sysctl_tcp_timestamps;
extern int sysctl_tcp_window_scaling;
extern int sysctl_tcp_sack;
extern int sysctl_tcp_syncookies;
extern int sysctl_tcp_tw_recycle;
extern int sysctl_ip_dynaddr;
extern __u32 sysctl_wmem_max;
extern __u32 sysctl_rmem_max;

/* Check TCP sequence numbers in ICMP packets. */
#define ICMP_MIN_LENGTH 8

/* Socket used for sending RSTs */ 	
struct inode tcp_inode;
struct socket *tcp_socket=&tcp_inode.u.socket_i;

static void tcp_v4_send_reset(struct sk_buff *skb);

void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb);

/* This is for sockets with full identity only.  Sockets here will always
 * be without wildcards and will have the following invariant:
 *          TCP_ESTABLISHED <= sk->state < TCP_CLOSE
 *
 * First half of the table is for sockets not in TIME_WAIT, second half
 * is for TIME_WAIT sockets only.
 */
struct tcp_ehash_bucket *tcp_ehash = NULL;

/* Ok, let's try this, I give up, we do need a local binding
 * TCP hash as well as the others for fast bind/connect.
 */
struct tcp_bind_hashbucket *tcp_bhash = NULL;

int tcp_bhash_size = 0;
int tcp_ehash_size = 0;

/* All sockets in TCP_LISTEN state will be in here.  This is the only table
 * where wildcard'd TCP sockets can exist.  Hash function here is just local
 * port number.
 */
struct sock *tcp_listening_hash[TCP_LHTABLE_SIZE] = { NULL, };
char __tcp_clean_cacheline_pad[(SMP_CACHE_BYTES -
				(((sizeof(void *) * (TCP_LHTABLE_SIZE + 2)) +
				  (sizeof(int) * 2)) % SMP_CACHE_BYTES))] = { 0, };

rwlock_t tcp_lhash_lock = RW_LOCK_UNLOCKED;
atomic_t tcp_lhash_users = ATOMIC_INIT(0);
DECLARE_WAIT_QUEUE_HEAD(tcp_lhash_wait);

spinlock_t tcp_portalloc_lock = SPIN_LOCK_UNLOCKED;

/*
 * This array holds the first and last local port number.
 * For high-usage systems, use sysctl to change this to
 * 32768-61000
 */
int sysctl_local_port_range[2] = { 1024, 4999 };
int tcp_port_rover = (1024 - 1);

static __inline__ int tcp_hashfn(__u32 laddr, __u16 lport,
				 __u32 faddr, __u16 fport)
{
	int h = ((laddr ^ lport) ^ (faddr ^ fport));
	h ^= h>>16;
	h ^= h>>8;
	return h & (tcp_ehash_size - 1);
}

static __inline__ int tcp_sk_hashfn(struct sock *sk)
{
	__u32 laddr = sk->rcv_saddr;
	__u16 lport = sk->num;
	__u32 faddr = sk->daddr;
	__u16 fport = sk->dport;

	return tcp_hashfn(laddr, lport, faddr, fport);
}

/* Allocate and initialize a new TCP local port bind bucket.
 * The bindhash mutex for snum's hash chain must be held here.
 */
struct tcp_bind_bucket *tcp_bucket_create(struct tcp_bind_hashbucket *head,
					  unsigned short snum)
{
	struct tcp_bind_bucket *tb;

	tb = kmem_cache_alloc(tcp_bucket_cachep, SLAB_ATOMIC);
	if(tb != NULL) {
		tb->port = snum;
		tb->fastreuse = 0;
		tb->owners = NULL;
		if((tb->next = head->chain) != NULL)
			tb->next->pprev = &tb->next;
		head->chain = tb;
		tb->pprev = &head->chain;
	}
	return tb;
}

/* Caller must disable local BH processing. */
static __inline__ void __tcp_inherit_port(struct sock *sk, struct sock *child)
{
	struct tcp_bind_hashbucket *head = &tcp_bhash[tcp_bhashfn(child->num)];
	struct tcp_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = (struct tcp_bind_bucket *)sk->prev;
	if ((child->bind_next = tb->owners) != NULL)
		tb->owners->bind_pprev = &child->bind_next;
	tb->owners = child;
	child->bind_pprev = &tb->owners;
	child->prev = (struct sock *) tb;
	spin_unlock(&head->lock);
}

__inline__ void tcp_inherit_port(struct sock *sk, struct sock *child)
{
	local_bh_disable();
	__tcp_inherit_port(sk, child);
	local_bh_enable();
}

/* Obtain a reference to a local port for the given sock,
 * if snum is zero it means select any available local port.
 */
static int tcp_v4_get_port(struct sock *sk, unsigned short snum)
{
	struct tcp_bind_hashbucket *head;
	struct tcp_bind_bucket *tb;
	int ret;

	local_bh_disable();
	if (snum == 0) {
		int low = sysctl_local_port_range[0];
		int high = sysctl_local_port_range[1];
		int remaining = (high - low) + 1;
		int rover;

		spin_lock(&tcp_portalloc_lock);
		rover = tcp_port_rover;
		do {	rover++;
			if ((rover < low) || (rover > high))
				rover = low;
			head = &tcp_bhash[tcp_bhashfn(rover)];
			spin_lock(&head->lock);
			for (tb = head->chain; tb; tb = tb->next)
				if (tb->port == rover)
					goto next;
			break;
		next:
			spin_unlock(&head->lock);
		} while (--remaining > 0);
		tcp_port_rover = rover;
		spin_unlock(&tcp_portalloc_lock);

		/* Exhausted local port range during search? */
		ret = 1;
		if (remaining <= 0)
			goto fail;

		/* OK, here is the one we will use.  HEAD is
		 * non-NULL and we hold it's mutex.
		 */
		snum = rover;
		tb = NULL;
	} else {
		head = &tcp_bhash[tcp_bhashfn(snum)];
		spin_lock(&head->lock);
		for (tb = head->chain; tb != NULL; tb = tb->next)
			if (tb->port == snum)
				break;
	}
	if (tb != NULL && tb->owners != NULL) {
		if (tb->fastreuse != 0 && sk->reuse != 0) {
			goto success;
		} else {
			struct sock *sk2 = tb->owners;
			int sk_reuse = sk->reuse;

			for( ; sk2 != NULL; sk2 = sk2->bind_next) {
				if (sk->bound_dev_if == sk2->bound_dev_if) {
					if (!sk_reuse	||
					    !sk2->reuse	||
					    sk2->state == TCP_LISTEN) {
						if (!sk2->rcv_saddr	||
						    !sk->rcv_saddr	||
						    (sk2->rcv_saddr == sk->rcv_saddr))
							break;
					}
				}
			}
			/* If we found a conflict, fail. */
			ret = 1;
			if (sk2 != NULL)
				goto fail_unlock;
		}
	}
	ret = 1;
	if (tb == NULL &&
	    (tb = tcp_bucket_create(head, snum)) == NULL)
			goto fail_unlock;
	if (tb->owners == NULL) {
		if (sk->reuse && sk->state != TCP_LISTEN)
			tb->fastreuse = 1;
		else
			tb->fastreuse = 0;
	} else if (tb->fastreuse &&
		   ((sk->reuse == 0) || (sk->state == TCP_LISTEN)))
		tb->fastreuse = 0;
success:
	sk->num = snum;
	if ((sk->bind_next = tb->owners) != NULL)
		tb->owners->bind_pprev = &sk->bind_next;
	tb->owners = sk;
	sk->bind_pprev = &tb->owners;
	sk->prev = (struct sock *) tb;
	ret = 0;

fail_unlock:
	spin_unlock(&head->lock);
fail:
	local_bh_enable();
	return ret;
}

/* Get rid of any references to a local port held by the
 * given sock.
 */
__inline__ void __tcp_put_port(struct sock *sk)
{
	struct tcp_bind_hashbucket *head = &tcp_bhash[tcp_bhashfn(sk->num)];
	struct tcp_bind_bucket *tb;

	spin_lock(&head->lock);
	tb = (struct tcp_bind_bucket *) sk->prev;
	if (sk->bind_next)
		sk->bind_next->bind_pprev = sk->bind_pprev;
	*(sk->bind_pprev) = sk->bind_next;
	sk->prev = NULL;
	if (tb->owners == NULL) {
		if (tb->next)
			tb->next->pprev = tb->pprev;
		*(tb->pprev) = tb->next;
		kmem_cache_free(tcp_bucket_cachep, tb);
	}
	spin_unlock(&head->lock);
}

void tcp_put_port(struct sock *sk)
{
	local_bh_disable();
	__tcp_put_port(sk);
	local_bh_enable();
}

#ifdef CONFIG_TCP_TW_RECYCLE
/*
   Very stupid pseudo-"algoritm". If the approach will be successful
   (and it will!), we have to make it more reasonable.
   Now it eats lots of CPU, when we are tough on ports.

   Apparently, it should be hash table indexed by daddr/dport.

   How does it work? We allow to truncate time-wait state, if:
   1. PAWS works on it.
   2. timewait bucket did not receive data for timeout:
      - initially timeout := 2*RTO, so that if our ACK to first
        transmitted peer's FIN is lost, we will see first retransmit.
      - if we receive anything, the timout is increased exponentially
        to follow normal TCP backoff pattern.
      It is important that minimal RTO (HZ/5) > minimal timestamp
      step (1ms).
   3. When creating new socket, we inherit sequence number
      and ts_recent of time-wait bucket, increasinf them a bit.

   These two conditions guarantee, that data will not be corrupted
   both by retransmitted and by delayed segments. They do not guarantee
   that peer will leave LAST-ACK/CLOSING state gracefully, it will be
   reset sometimes, namely, when more than two our ACKs to its FINs are lost.
   This reset is harmless and even good.
 */

int tcp_v4_tw_recycle(struct sock *sk, u32 daddr, u16 dport)
{
	static int tw_rover;

	struct tcp_tw_bucket *tw;
	struct tcp_bind_hashbucket *head;
	struct tcp_bind_bucket *tb;

	int low = sysctl_local_port_range[0];
	int high = sysctl_local_port_range[1];
	unsigned long now = jiffies;
	int i, rover;

	rover = tw_rover;

	local_bh_disable();
	for (i=0; i<tcp_bhash_size; i++, rover++) {
		rover &= (tcp_bhash_size-1);
		head = &tcp_bhash[rover];

		spin_lock(&head->lock);
		for (tb = head->chain; tb; tb = tb->next) {
			tw = (struct tcp_tw_bucket*)tb->owners;

			if (tw->state != TCP_TIME_WAIT ||
			    tw->dport != dport ||
			    tw->daddr != daddr ||
			    tw->rcv_saddr != sk->rcv_saddr ||
			    tb->port < low ||
			    tb->port >= high ||
			    !TCP_INET_FAMILY(tw->family) ||
			    tw->ts_recent_stamp == 0 ||
			    (long)(now - tw->ttd) <= 0)
				continue;
			tw_rover = rover;
			goto hit;
		}
		spin_unlock(&head->lock);
	}
	local_bh_enable();
	tw_rover = rover;
	return -EAGAIN;

hit:
	sk->num = tw->num;
	if ((sk->bind_next = tb->owners) != NULL)
		tb->owners->bind_pprev = &sk->bind_next;
	tb->owners = sk;
	sk->bind_pprev = &tb->owners;
	sk->prev = (struct sock *) tb;
	spin_unlock_bh(&head->lock);
	return 0;
}
#endif


void tcp_listen_wlock(void)
{
	write_lock(&tcp_lhash_lock);

	if (atomic_read(&tcp_lhash_users)) {
		DECLARE_WAITQUEUE(wait, current);

		add_wait_queue(&tcp_lhash_wait, &wait);
		for (;;) {
			set_current_state(TASK_UNINTERRUPTIBLE);
			if (atomic_read(&tcp_lhash_users) == 0)
				break;
			write_unlock_bh(&tcp_lhash_lock);
			schedule();
			write_lock_bh(&tcp_lhash_lock);
		}

		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&tcp_lhash_wait, &wait);
	}
}

static __inline__ void __tcp_v4_hash(struct sock *sk)
{
	struct sock **skp;
	rwlock_t *lock;

	BUG_TRAP(sk->pprev==NULL);
	if(sk->state == TCP_LISTEN) {
		skp = &tcp_listening_hash[tcp_sk_listen_hashfn(sk)];
		lock = &tcp_lhash_lock;
		tcp_listen_wlock();
	} else {
		skp = &tcp_ehash[(sk->hashent = tcp_sk_hashfn(sk))].chain;
		lock = &tcp_ehash[sk->hashent].lock;
		write_lock(lock);
	}
	if((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	sk->prot->inuse++;
	if(sk->prot->highestinuse < sk->prot->inuse)
		sk->prot->highestinuse = sk->prot->inuse;
	write_unlock(lock);
}

static void tcp_v4_hash(struct sock *sk)
{
	if (sk->state != TCP_CLOSE) {
		local_bh_disable();
		__tcp_v4_hash(sk);
		local_bh_enable();
	}
}

void tcp_unhash(struct sock *sk)
{
	rwlock_t *lock;

	if (sk->state == TCP_LISTEN) {
		local_bh_disable();
		tcp_listen_wlock();
		lock = &tcp_lhash_lock;
	} else {
		struct tcp_ehash_bucket *head = &tcp_ehash[sk->hashent];
		lock = &head->lock;
		write_lock_bh(&head->lock);
	}

	if(sk->pprev) {
		if(sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
		sk->prot->inuse--;
	}
	write_unlock_bh(lock);
}

/* Don't inline this cruft.  Here are some nice properties to
 * exploit here.  The BSD API does not allow a listening TCP
 * to specify the remote port nor the remote address for the
 * connection.  So always assume those are both wildcarded
 * during the search since they can never be otherwise.
 */
static struct sock *__tcp_v4_lookup_listener(struct sock *sk, u32 daddr, unsigned short hnum, int dif)
{
	struct sock *result = NULL;
	int score, hiscore;

	hiscore=0;
	for(; sk; sk = sk->next) {
		if(sk->num == hnum) {
			__u32 rcv_saddr = sk->rcv_saddr;

			score = 1;
			if(rcv_saddr) {
				if (rcv_saddr != daddr)
					continue;
				score++;
			}
			if (sk->bound_dev_if) {
				if (sk->bound_dev_if != dif)
					continue;
				score++;
			}
			if (score == 3)
				return sk;
			if (score > hiscore) {
				hiscore = score;
				result = sk;
			}
		}
	}
	return result;
}

/* Optimize the common listener case. */
__inline__ struct sock *tcp_v4_lookup_listener(u32 daddr, unsigned short hnum, int dif)
{
	struct sock *sk;

	read_lock(&tcp_lhash_lock);
	sk = tcp_listening_hash[tcp_lhashfn(hnum)];
	if (sk) {
		if (sk->num == hnum && sk->next == NULL)
			goto sherry_cache;
		sk = __tcp_v4_lookup_listener(sk, daddr, hnum, dif);
	}
	if (sk) {
sherry_cache:
		sock_hold(sk);
	}
	read_unlock(&tcp_lhash_lock);
	return sk;
}

/* Sockets in TCP_CLOSE state are _always_ taken out of the hash, so
 * we need not check it for TCP lookups anymore, thanks Alexey. -DaveM
 *
 * Local BH must be disabled here.
 */
static inline struct sock *__tcp_v4_lookup(u32 saddr, u16 sport,
					   u32 daddr, u16 hnum, int dif)
{
	struct tcp_ehash_bucket *head;
	TCP_V4_ADDR_COOKIE(acookie, saddr, daddr)
	__u32 ports = TCP_COMBINED_PORTS(sport, hnum);
	struct sock *sk;
	int hash;

	/* Optimize here for direct hit, only listening connections can
	 * have wildcards anyways.
	 */
	hash = tcp_hashfn(daddr, hnum, saddr, sport);
	head = &tcp_ehash[hash];
	read_lock(&head->lock);
	for(sk = head->chain; sk; sk = sk->next) {
		if(TCP_IPV4_MATCH(sk, acookie, saddr, daddr, ports, dif))
			goto hit; /* You sunk my battleship! */
	}

	/* Must check for a TIME_WAIT'er before going to listener hash. */
	for(sk = (head + tcp_ehash_size)->chain; sk; sk = sk->next)
		if(TCP_IPV4_MATCH(sk, acookie, saddr, daddr, ports, dif))
			goto hit;
	read_unlock(&head->lock);

	return tcp_v4_lookup_listener(daddr, hnum, dif);

hit:
	sock_hold(sk);
	read_unlock(&head->lock);
	return sk;
}

__inline__ struct sock *tcp_v4_lookup(u32 saddr, u16 sport, u32 daddr, u16 dport, int dif)
{
	struct sock *sk;

	local_bh_disable();
	sk = __tcp_v4_lookup(saddr, sport, daddr, ntohs(dport), dif);
	local_bh_enable();

	return sk;
}

static inline __u32 tcp_v4_init_sequence(struct sock *sk, struct sk_buff *skb)
{
	return secure_tcp_sequence_number(sk->saddr, sk->daddr,
					  skb->h.th->dest,
					  skb->h.th->source);
}

static int tcp_v4_check_established(struct sock *sk)
{
	u32 daddr = sk->rcv_saddr;
	u32 saddr = sk->daddr;
	int dif = sk->bound_dev_if;
	TCP_V4_ADDR_COOKIE(acookie, saddr, daddr)
	__u32 ports = TCP_COMBINED_PORTS(sk->dport, sk->num);
	int hash = tcp_hashfn(daddr, sk->num, saddr, sk->dport);
	struct tcp_ehash_bucket *head = &tcp_ehash[hash];
	struct sock *sk2, **skp;
#ifdef CONFIG_TCP_TW_RECYCLE
	struct tcp_tw_bucket *tw;
#endif

	write_lock_bh(&head->lock);

	/* Check TIME-WAIT sockets first. */
	for(skp = &(head + tcp_ehash_size)->chain; (sk2=*skp) != NULL;
	    skp = &sk2->next) {
#ifdef CONFIG_TCP_TW_RECYCLE
		tw = (struct tcp_tw_bucket*)sk2;
#endif

		if(TCP_IPV4_MATCH(sk2, acookie, saddr, daddr, ports, dif)) {
#ifdef CONFIG_TCP_TW_RECYCLE
			struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

			/* With PAWS, it is safe from the viewpoint
			   of data integrity. Even without PAWS it
			   is safe provided sequence spaces do not
			   overlap i.e. at data rates <= 80Mbit/sec.

			   Actually, the idea is close to VJ's (rfc1332)
			   one, only timestamp cache is held not per host,
			   but per port pair and TW bucket is used
			   as state holder.
			 */
			if (sysctl_tcp_tw_recycle && tw->ts_recent_stamp) {
				if ((tp->write_seq = tw->snd_nxt + 2) == 0)
					tp->write_seq = 1;
				tp->ts_recent = tw->ts_recent;
				tp->ts_recent_stamp = tw->ts_recent_stamp;
				sock_hold(sk2);
				skp = &head->chain;
				goto unique;
			} else
#endif
			goto not_unique;
		}
	}
#ifdef CONFIG_TCP_TW_RECYCLE
	tw = NULL;
#endif

	/* And established part... */
	for(skp = &head->chain; (sk2=*skp)!=NULL; skp = &sk2->next) {
		if(TCP_IPV4_MATCH(sk2, acookie, saddr, daddr, ports, dif))
			goto not_unique;
	}

#ifdef CONFIG_TCP_TW_RECYCLE
unique:
#endif
	BUG_TRAP(sk->pprev==NULL);
	if ((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;

	*skp = sk;
	sk->pprev = skp;
	sk->prot->inuse++;
	if(sk->prot->highestinuse < sk->prot->inuse)
		sk->prot->highestinuse = sk->prot->inuse;
	write_unlock_bh(&head->lock);

#ifdef CONFIG_TCP_TW_RECYCLE
	if (tw) {
		/* Silly. Should hash-dance instead... */
		local_bh_disable();
		tcp_tw_deschedule(tw);
		tcp_timewait_kill(tw);
		local_bh_enable();

		tcp_tw_put(tw);
	}
#endif
	return 0;

not_unique:
	write_unlock_bh(&head->lock);
	return -EADDRNOTAVAIL;
}

/* Hash SYN-SENT socket to established hash table after
 * checking that it is unique. Note, that without kernel lock
 * we MUST make these two operations atomically.
 *
 * Optimization: if it is bound and tcp_bind_bucket has the only
 * owner (us), we need not to scan established bucket.
 */

int tcp_v4_hash_connecting(struct sock *sk)
{
	unsigned short snum = sk->num;
	struct tcp_bind_hashbucket *head = &tcp_bhash[tcp_bhashfn(snum)];
	struct tcp_bind_bucket *tb = (struct tcp_bind_bucket *)sk->prev;

	spin_lock_bh(&head->lock);
	if (tb->owners == sk && sk->bind_next == NULL) {
		__tcp_v4_hash(sk);
		spin_unlock_bh(&head->lock);
		return 0;
	} else {
		spin_unlock_bh(&head->lock);

		/* No definite answer... Walk to established hash table */
		return tcp_v4_check_established(sk);
	}
}

/* This will initiate an outgoing connection. */
int tcp_v4_connect(struct sock *sk, struct sockaddr *uaddr, int addr_len)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);
	struct sockaddr_in *usin = (struct sockaddr_in *) uaddr;
	struct sk_buff *buff;
	struct rtable *rt;
	u32 daddr, nexthop;
	int tmp;
	int err;

	if (sk->state != TCP_CLOSE) 
		return(-EISCONN);

	if (addr_len < sizeof(struct sockaddr_in))
		return(-EINVAL);

	if (usin->sin_family != AF_INET)
		return(-EAFNOSUPPORT);

	nexthop = daddr = usin->sin_addr.s_addr;
	if (sk->protinfo.af_inet.opt && sk->protinfo.af_inet.opt->srr) {
		if (daddr == 0)
			return -EINVAL;
		nexthop = sk->protinfo.af_inet.opt->faddr;
	}

	tmp = ip_route_connect(&rt, nexthop, sk->saddr,
			       RT_TOS(sk->protinfo.af_inet.tos)|RTO_CONN|sk->localroute, sk->bound_dev_if);
	if (tmp < 0)
		return tmp;

	if (rt->rt_flags&(RTCF_MULTICAST|RTCF_BROADCAST)) {
		ip_rt_put(rt);
		return -ENETUNREACH;
	}

	__sk_dst_set(sk, &rt->u.dst);

	if (!sk->protinfo.af_inet.opt || !sk->protinfo.af_inet.opt->srr)
		daddr = rt->rt_dst;

	err = -ENOBUFS;
	buff = sock_wmalloc(sk, (MAX_HEADER + sk->prot->max_header),
			    0, GFP_KERNEL);

	if (buff == NULL)
		goto failure;

	if (!sk->saddr)
		sk->saddr = rt->rt_src;
	sk->rcv_saddr = sk->saddr;

	if (!sk->num) {
		if (sk->prot->get_port(sk, 0)
#ifdef CONFIG_TCP_TW_RECYCLE
		    && (!sysctl_tcp_tw_recycle ||
			tcp_v4_tw_recycle(sk, daddr, usin->sin_port))
#endif
		    ) {
			kfree_skb(buff);
			err = -EAGAIN;
			goto failure;
		}
		sk->sport = htons(sk->num);
	}
#ifdef CONFIG_TCP_TW_RECYCLE
	else if (tp->ts_recent_stamp && sk->daddr != daddr) {
		/* Reset inherited state */
		tp->ts_recent = 0;
		tp->ts_recent_stamp = 0;
		tp->write_seq = 0;
	}
#endif

	sk->dport = usin->sin_port;
	sk->daddr = daddr;

	if (!tp->write_seq)
		tp->write_seq = secure_tcp_sequence_number(sk->saddr, sk->daddr,
							   sk->sport, usin->sin_port);

	tp->ext_header_len = 0;
	if (sk->protinfo.af_inet.opt)
		tp->ext_header_len = sk->protinfo.af_inet.opt->optlen;

	tp->mss_clamp = 536;

	err = tcp_connect(sk, buff);
	if (err == 0)
		return 0;

failure:
	__sk_dst_reset(sk);
	sk->dport = 0;
	return err;
}

static int tcp_v4_sendmsg(struct sock *sk, struct msghdr *msg, int len)
{
	int retval = -EINVAL;

	lock_sock(sk);

	/* Do sanity checking for sendmsg/sendto/send. */
	if (msg->msg_flags & ~(MSG_OOB|MSG_DONTROUTE|MSG_DONTWAIT|MSG_NOSIGNAL))
		goto out;
	if (msg->msg_name) {
		struct sockaddr_in *addr=(struct sockaddr_in *)msg->msg_name;

		if (msg->msg_namelen < sizeof(*addr))
			goto out;
		if (addr->sin_family && addr->sin_family != AF_INET)
			goto out;
		retval = -ENOTCONN;
		if(sk->state == TCP_CLOSE)
			goto out;
		retval = -EISCONN;
		if (addr->sin_port != sk->dport)
			goto out;
		if (addr->sin_addr.s_addr != sk->daddr)
			goto out;
	}
	retval = tcp_do_sendmsg(sk, msg);

out:
	release_sock(sk);
	return retval;
}


/*
 * Do a linear search in the socket open_request list. 
 * This should be replaced with a global hash table.
 */
static struct open_request *tcp_v4_search_req(struct tcp_opt *tp, 
					      struct iphdr *iph,
					      struct tcphdr *th,
					      struct open_request **prevp)
{
	struct open_request *req, *prev;  
	__u16 rport = th->source; 

	/*	assumption: the socket is not in use.
	 *	as we checked the user count on tcp_rcv and we're
	 *	running from a soft interrupt.
	 */
	prev = (struct open_request *) (&tp->syn_wait_queue); 
	for (req = prev->dl_next; req; req = req->dl_next) {
		if (req->af.v4_req.rmt_addr == iph->saddr &&
		    req->af.v4_req.loc_addr == iph->daddr &&
		    req->rmt_port == rport &&
		    TCP_INET_FAMILY(req->class->family)) {
			if (req->sk) {
				/* Weird case: connection was established
				   and then killed by RST before user accepted
				   it. This connection is dead, but we cannot
				   kill openreq to avoid blocking in accept().

				   accept() will collect this garbage,
				   but such reqs must be ignored, when talking
				   to network.
				 */
				bh_lock_sock(req->sk);
				BUG_TRAP(req->sk->lock.users==0);
				if (req->sk->state == TCP_CLOSE) {
					bh_unlock_sock(req->sk);
					prev = req;
					continue;
				}
			}
			*prevp = prev;
			return req; 
		}
		prev = req; 
	}
	return NULL; 
}


/* 
 * This routine does path mtu discovery as defined in RFC1191.
 */
static inline void do_pmtu_discovery(struct sock *sk, struct iphdr *ip, unsigned mtu)
{
	struct dst_entry *dst;
	struct tcp_opt *tp = &sk->tp_pinfo.af_tcp;

	/* We are not interested in TCP_LISTEN and open_requests (SYN-ACKs
	 * send out by Linux are always <576bytes so they should go through
	 * unfragmented).
	 */
	if (sk->state == TCP_LISTEN)
		return; 

	/* We don't check in the destentry if pmtu discovery is forbidden
	 * on this route. We just assume that no packet_to_big packets
	 * are send back when pmtu discovery is not active.
     	 * There is a small race when the user changes this flag in the
	 * route, but I think that's acceptable.
	 */
	if ((dst = __sk_dst_check(sk, 0)) == NULL)
		return;

	ip_rt_update_pmtu(dst, mtu);

	/* Something is about to be wrong... Remember soft error
	 * for the case, if this connection will not able to recover.
	 */
	if (mtu < dst->pmtu && ip_dont_fragment(sk, dst))
		sk->err_soft = EMSGSIZE;

	if (sk->protinfo.af_inet.pmtudisc != IP_PMTUDISC_DONT &&
	    tp->pmtu_cookie > dst->pmtu) {
		tcp_sync_mss(sk, dst->pmtu);

		/* Resend the TCP packet because it's  
		 * clear that the old packet has been
		 * dropped. This is the new "fast" path mtu
		 * discovery.
		 */
		tcp_simple_retransmit(sk);
	} /* else let the usual retransmit timer handle it */
}

/*
 * This routine is called by the ICMP module when it gets some
 * sort of error condition.  If err < 0 then the socket should
 * be closed and the error returned to the user.  If err > 0
 * it's just the icmp type << 8 | icmp code.  After adjustment
 * header points to the first 8 bytes of the tcp header.  We need
 * to find the appropriate port.
 *
 * The locking strategy used here is very "optimistic". When
 * someone else accesses the socket the ICMP is just dropped
 * and for some paths there is no check at all.
 * A more general error queue to queue errors for later handling
 * is probably better.
 *
 */

void tcp_v4_err(struct sk_buff *skb, unsigned char *dp, int len)
{
	struct iphdr *iph = (struct iphdr*)dp;
	struct tcphdr *th; 
	struct tcp_opt *tp;
	int type = skb->h.icmph->type;
	int code = skb->h.icmph->code;
#if ICMP_MIN_LENGTH < 14
	int no_flags = 0;
#else
#define no_flags 0
#endif
	struct sock *sk;
	__u32 seq;
	int err;

	if (len < (iph->ihl << 2) + ICMP_MIN_LENGTH) { 
		icmp_statistics.IcmpInErrors++; 
		return;
	}
#if ICMP_MIN_LENGTH < 14
	if (len < (iph->ihl << 2) + 14)
		no_flags = 1;
#endif

	th = (struct tcphdr*)(dp+(iph->ihl<<2));

	sk = tcp_v4_lookup(iph->daddr, th->dest, iph->saddr, th->source, skb->dev->ifindex);
	if (sk == NULL) {
		icmp_statistics.IcmpInErrors++;
		return;
	}
	if (sk->state == TCP_TIME_WAIT) {
		tcp_tw_put((struct tcp_tw_bucket*)sk);
		return;
	}

	bh_lock_sock(sk);
	/* If too many ICMPs get dropped on busy
	 * servers this needs to be solved differently.
	 */
	if (sk->lock.users != 0)
		net_statistics.LockDroppedIcmps++;

	tp = &sk->tp_pinfo.af_tcp;
	seq = ntohl(th->seq);
	if (sk->state != TCP_LISTEN && !between(seq, tp->snd_una, tp->snd_nxt)) {
		net_statistics.OutOfWindowIcmps++;
		goto out;
	}

	switch (type) {
	case ICMP_SOURCE_QUENCH:
#ifndef OLD_SOURCE_QUENCH /* This is deprecated */
		if (sk->lock.users == 0) {
			tp->snd_ssthresh = tcp_recalc_ssthresh(tp);
			tp->snd_cwnd = tp->snd_ssthresh;
			tp->snd_cwnd_cnt = 0;
			tp->high_seq = tp->snd_nxt;
		}
#endif
		goto out;
	case ICMP_PARAMETERPROB:
		err = EPROTO;
		break; 
	case ICMP_DEST_UNREACH:
		if (code > NR_ICMP_UNREACH)
			goto out;

		if (code == ICMP_FRAG_NEEDED) { /* PMTU discovery (RFC1191) */
			if (sk->lock.users == 0)
				do_pmtu_discovery(sk, iph, ntohs(skb->h.icmph->un.frag.mtu));
			goto out;
		}

		err = icmp_err_convert[code].errno;
		break;
	case ICMP_TIME_EXCEEDED:
		err = EHOSTUNREACH;
		break;
	default:
		goto out;
	}

	switch (sk->state) {
		struct open_request *req, *prev;
	case TCP_LISTEN:
		if (sk->lock.users != 0)
			goto out;

		/* The final ACK of the handshake should be already 
		 * handled in the new socket context, not here.
		 * Strictly speaking - an ICMP error for the final
		 * ACK should set the opening flag, but that is too
		 * complicated right now. 
		 */ 
		if (!no_flags && !th->syn && !th->ack)
			goto out;

		req = tcp_v4_search_req(tp, iph, th, &prev); 
		if (!req)
			goto out;

		if (req->sk) {
			struct sock *nsk = req->sk;

			/* 
			 * Already in ESTABLISHED and a big socket is created,
			 * set error code there.
			 * The error will _not_ be reported in the accept(),
			 * but only with the next operation on the socket after
			 * accept. 
			 */
			sock_hold(nsk);
			bh_unlock_sock(sk);
			sock_put(sk);
			sk = nsk;

			BUG_TRAP(sk->lock.users == 0);
			tp = &sk->tp_pinfo.af_tcp;
			if (!between(seq, tp->snd_una, tp->snd_nxt)) {
				net_statistics.OutOfWindowIcmps++;
				goto out;
			}
		} else {
			if (seq != req->snt_isn) {
				net_statistics.OutOfWindowIcmps++;
				goto out;
			}

			/* 
			 * Still in SYN_RECV, just remove it silently.
			 * There is no good way to pass the error to the newly
			 * created socket, and POSIX does not want network
			 * errors returned from accept(). 
			 */ 
			tp->syn_backlog--;
			tcp_synq_unlink(tp, req, prev);
			tcp_dec_slow_timer(TCP_SLT_SYNACK);
			req->class->destructor(req);
			tcp_openreq_free(req);
			goto out;
		}
		break;
	case TCP_SYN_SENT:
	case TCP_SYN_RECV:  /* Cannot happen.
			       It can f.e. if SYNs crossed.
			     */ 
		if (!no_flags && !th->syn)
			goto out;
		if (sk->lock.users == 0) {
			tcp_statistics.TcpAttemptFails++;
			sk->err = err;
			/* Wake people up to see the error (see connect in sock.c) */
			sk->error_report(sk);

			tcp_set_state(sk, TCP_CLOSE);
			tcp_done(sk);
		} else {
			sk->err_soft = err;
		}
		goto out;
	}

	/* If we've already connected we will keep trying
	 * until we time out, or the user gives up.
	 *
	 * rfc1122 4.2.3.9 allows to consider as hard errors
	 * only PROTO_UNREACH and PORT_UNREACH (well, FRAG_FAILED too,
	 * but it is obsoleted by pmtu discovery).
	 *
	 * Note, that in modern internet, where routing is unreliable
	 * and in each dark corner broken firewalls sit, sending random
	 * errors ordered by their masters even this two messages finally lose
	 * their original sense (even Linux sends invalid PORT_UNREACHs)
	 *
	 * Now we are in compliance with RFCs.
	 *							--ANK (980905)
	 */

	if (sk->lock.users == 0 && sk->protinfo.af_inet.recverr) {
		sk->err = err;
		sk->error_report(sk);
	} else	{ /* Only an error on timeout */
		sk->err_soft = err;
	}

out:
	bh_unlock_sock(sk);
	sock_put(sk);
}

/* This routine computes an IPv4 TCP checksum. */
void tcp_v4_send_check(struct sock *sk, struct tcphdr *th, int len, 
		       struct sk_buff *skb)
{
	th->check = 0;
	th->check = tcp_v4_check(th, len, sk->saddr, sk->daddr,
				 csum_partial((char *)th, th->doff<<2, skb->csum));
}

/*
 *	This routine will send an RST to the other tcp.
 *
 *	Someone asks: why I NEVER use socket parameters (TOS, TTL etc.)
 *		      for reset.
 *	Answer: if a packet caused RST, it is not for a socket
 *		existing in our system, if it is matched to a socket,
 *		it is just duplicate segment or bug in other side's TCP.
 *		So that we build reply only basing on parameters
 *		arrived with segment.
 *	Exception: precedence violation. We do not implement it in any case.
 */

static void tcp_v4_send_reset(struct sk_buff *skb)
{
	struct tcphdr *th = skb->h.th;
	struct tcphdr rth;
	struct ip_reply_arg arg;

	/* Never send a reset in response to a reset. */
	if (th->rst)
		return;

	if (((struct rtable*)skb->dst)->rt_type != RTN_LOCAL)
		return;

	/* Swap the send and the receive. */
	memset(&rth, 0, sizeof(struct tcphdr)); 
	rth.dest = th->source;
	rth.source = th->dest; 
	rth.doff = sizeof(struct tcphdr)/4;
	rth.rst = 1;

	if (th->ack) {
		rth.seq = th->ack_seq;
	} else {
		rth.ack = 1;
		rth.ack_seq = htonl(ntohl(th->seq) + th->syn + th->fin
				    + skb->len - (th->doff<<2));
	}

	memset(&arg, 0, sizeof arg); 
	arg.iov[0].iov_base = (unsigned char *)&rth; 
	arg.iov[0].iov_len  = sizeof rth;
	arg.csum = csum_tcpudp_nofold(skb->nh.iph->daddr, 
				      skb->nh.iph->saddr, /*XXX*/
				      sizeof(struct tcphdr),
				      IPPROTO_TCP,
				      0); 
	arg.n_iov = 1;
	arg.csumoffset = offsetof(struct tcphdr, check) / 2; 

	ip_send_reply(tcp_socket->sk, skb, &arg, sizeof rth);

	tcp_statistics.TcpOutSegs++;
	tcp_statistics.TcpOutRsts++;
}

/* The code following below sending ACKs in SYN-RECV and TIME-WAIT states
   outside socket context is ugly, certainly. What can I do?
 */

static void tcp_v4_send_ack(struct sk_buff *skb, u32 seq, u32 ack, u32 win, u32 ts)
{
	struct tcphdr *th = skb->h.th;
	struct {
		struct tcphdr th;
		u32 tsopt[3];
	} rep;
	struct ip_reply_arg arg;

	memset(&rep.th, 0, sizeof(struct tcphdr));
	memset(&arg, 0, sizeof arg);

	arg.iov[0].iov_base = (unsigned char *)&rep; 
	arg.iov[0].iov_len  = sizeof(rep.th);
	arg.n_iov = 1;
	if (ts) {
		rep.tsopt[0] = __constant_htonl((TCPOPT_NOP << 24) |
						(TCPOPT_NOP << 16) |
						(TCPOPT_TIMESTAMP << 8) |
						TCPOLEN_TIMESTAMP);
		rep.tsopt[1] = htonl(tcp_time_stamp);
		rep.tsopt[2] = htonl(ts);
		arg.iov[0].iov_len = sizeof(rep);
	}

	/* Swap the send and the receive. */
	rep.th.dest = th->source;
	rep.th.source = th->dest; 
	rep.th.doff = arg.iov[0].iov_len/4;
	rep.th.seq = htonl(seq);
	rep.th.ack_seq = htonl(ack);
	rep.th.ack = 1;
	rep.th.window = htons(win);

	arg.csum = csum_tcpudp_nofold(skb->nh.iph->daddr, 
				      skb->nh.iph->saddr, /*XXX*/
				      arg.iov[0].iov_len,
				      IPPROTO_TCP,
				      0);
	arg.csumoffset = offsetof(struct tcphdr, check) / 2; 

	ip_send_reply(tcp_socket->sk, skb, &arg, arg.iov[0].iov_len);

	tcp_statistics.TcpOutSegs++;
}

static void tcp_v4_timewait_ack(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_tw_bucket *tw = (struct tcp_tw_bucket *)sk;

	tcp_v4_send_ack(skb, tw->snd_nxt, tw->rcv_nxt, 0, tw->ts_recent);

	tcp_tw_put(tw);
}

static void tcp_v4_or_send_ack(struct sk_buff *skb, struct open_request *req)
{
	tcp_v4_send_ack(skb, req->snt_isn+1, req->rcv_isn+1, req->rcv_wnd, req->ts_recent);
}

/*
 *	Send a SYN-ACK after having received an ACK. 
 *	This still operates on a open_request only, not on a big
 *	socket.
 */ 
static void tcp_v4_send_synack(struct sock *sk, struct open_request *req)
{
	struct rtable *rt;
	struct ip_options *opt;
	struct sk_buff * skb;

	/* First, grab a route. */
	opt = req->af.v4_req.opt;
	if(ip_route_output(&rt, ((opt && opt->srr) ?
				 opt->faddr :
				 req->af.v4_req.rmt_addr),
			   req->af.v4_req.loc_addr,
			   RT_TOS(sk->protinfo.af_inet.tos) | RTO_CONN | sk->localroute,
			   sk->bound_dev_if)) {
		ip_statistics.IpOutNoRoutes++;
		return;
	}
	if(opt && opt->is_strictroute && rt->rt_dst != rt->rt_gateway) {
		ip_rt_put(rt);
		ip_statistics.IpOutNoRoutes++;
		return;
	}

	skb = tcp_make_synack(sk, &rt->u.dst, req);

	if (skb) {
		struct tcphdr *th = skb->h.th;

		th->check = tcp_v4_check(th, skb->len,
					 req->af.v4_req.loc_addr, req->af.v4_req.rmt_addr,
					 csum_partial((char *)th, skb->len, skb->csum));

		ip_build_and_send_pkt(skb, sk, req->af.v4_req.loc_addr,
				      req->af.v4_req.rmt_addr, req->af.v4_req.opt);
	}
	ip_rt_put(rt);
}

/*
 *	IPv4 open_request destructor.
 */ 
static void tcp_v4_or_free(struct open_request *req)
{
	if(!req->sk && req->af.v4_req.opt)
		kfree_s(req->af.v4_req.opt, optlength(req->af.v4_req.opt));
}

static inline void syn_flood_warning(struct sk_buff *skb)
{
	static unsigned long warntime;
	
	if (jiffies - warntime > HZ*60) {
		warntime = jiffies;
		printk(KERN_INFO 
		       "possible SYN flooding on port %d. Sending cookies.\n",  
		       ntohs(skb->h.th->dest));
	}
}

/* 
 * Save and compile IPv4 options into the open_request if needed. 
 */
static inline struct ip_options * 
tcp_v4_save_options(struct sock *sk, struct sk_buff *skb)
{
	struct ip_options *opt = &(IPCB(skb)->opt);
	struct ip_options *dopt = NULL; 

	if (opt && opt->optlen) {
		int opt_size = optlength(opt); 
		dopt = kmalloc(opt_size, GFP_ATOMIC);
		if (dopt) {
			if (ip_options_echo(dopt, skb)) {
				kfree_s(dopt, opt_size);
				dopt = NULL;
			}
		}
	}
	return dopt;
}

/* 
 * Maximum number of SYN_RECV sockets in queue per LISTEN socket.
 * One SYN_RECV socket costs about 80bytes on a 32bit machine.
 * It would be better to replace it with a global counter for all sockets
 * but then some measure against one socket starving all other sockets
 * would be needed.
 */
int sysctl_max_syn_backlog = 128; 

struct or_calltable or_ipv4 = {
	PF_INET,
	tcp_v4_send_synack,
	tcp_v4_or_send_ack,
	tcp_v4_or_free,
	tcp_v4_send_reset
};

#define BACKLOG(sk) ((sk)->tp_pinfo.af_tcp.syn_backlog) /* lvalue! */
#define BACKLOGMAX(sk) sysctl_max_syn_backlog

int tcp_v4_conn_request(struct sock *sk, struct sk_buff *skb)
{
	struct tcp_opt tp;
	struct open_request *req;
	struct tcphdr *th = skb->h.th;
	__u32 saddr = skb->nh.iph->saddr;
	__u32 daddr = skb->nh.iph->daddr;
	__u32 isn = TCP_SKB_CB(skb)->when;
#ifdef CONFIG_SYN_COOKIES
	int want_cookie = 0;
#else
#define want_cookie 0 /* Argh, why doesn't gcc optimize this :( */
#endif

	/* Never answer to SYNs send to broadcast or multicast */
	if (((struct rtable *)skb->dst)->rt_flags & 
	    (RTCF_BROADCAST|RTCF_MULTICAST))
		goto drop; 

	/* XXX: Check against a global syn pool counter. */
	if (BACKLOG(sk) > BACKLOGMAX(sk)) {
#ifdef CONFIG_SYN_COOKIES
		if (sysctl_tcp_syncookies && !isn) {
			syn_flood_warning(skb);
			want_cookie = 1; 
		} else
#endif
		goto drop;
	} else { 
		if (isn == 0)
			isn = tcp_v4_init_sequence(sk, skb);
		BACKLOG(sk)++;
	}

	req = tcp_openreq_alloc();
	if (req == NULL) {
		goto dropbacklog;
	}

	req->rcv_wnd = 0;		/* So that tcp_send_synack() knows! */

	req->rcv_isn = TCP_SKB_CB(skb)->seq;
 	tp.tstamp_ok = tp.sack_ok = tp.wscale_ok = tp.snd_wscale = 0;

	tp.mss_clamp = 536;
	tp.user_mss = sk->tp_pinfo.af_tcp.user_mss;

	tcp_parse_options(NULL, th, &tp, want_cookie);

	req->mss = tp.mss_clamp;
	req->ts_recent = tp.saw_tstamp ? tp.rcv_tsval : 0;
	req->tstamp_ok = tp.tstamp_ok;
	req->sack_ok = tp.sack_ok;
	req->snd_wscale = tp.snd_wscale;
	req->wscale_ok = tp.wscale_ok;
	req->rmt_port = th->source;
	req->af.v4_req.loc_addr = daddr;
	req->af.v4_req.rmt_addr = saddr;

	/* Note that we ignore the isn passed from the TIME_WAIT
	 * state here. That's the price we pay for cookies.
	 *
	 * RED-PEN. The price is high... Then we cannot kill TIME-WAIT
	 * and should reject connection attempt, duplicates with random
	 * sequence number can corrupt data. Right?
	 * I disabled sending cookie to request matching to a timewait
	 * bucket.
	 */
	if (want_cookie)
		isn = cookie_v4_init_sequence(sk, skb, &req->mss);

	req->snt_isn = isn;

	req->af.v4_req.opt = tcp_v4_save_options(sk, skb);

	req->class = &or_ipv4;
	req->retrans = 0;
	req->sk = NULL;

	tcp_v4_send_synack(sk, req);

	if (want_cookie) {
		if (req->af.v4_req.opt)
			kfree(req->af.v4_req.opt);
		tcp_v4_or_free(req); 
	   	tcp_openreq_free(req); 
	} else {
		req->expires = jiffies + TCP_TIMEOUT_INIT;
		tcp_inc_slow_timer(TCP_SLT_SYNACK);
		tcp_synq_queue(&sk->tp_pinfo.af_tcp, req);
	}

	return 0;

dropbacklog:
	if (!want_cookie) 
		BACKLOG(sk)--;
drop:
	tcp_statistics.TcpAttemptFails++;
	return 0;
}


/* 
 * The three way handshake has completed - we got a valid synack - 
 * now create the new socket. 
 */
struct sock * tcp_v4_syn_recv_sock(struct sock *sk, struct sk_buff *skb,
				   struct open_request *req,
				   struct dst_entry *dst)
{
	struct ip_options *opt = req->af.v4_req.opt;
	struct tcp_opt *newtp;
	struct sock *newsk;

	if (sk->ack_backlog > sk->max_ack_backlog)
		goto exit; /* head drop */
	if (dst == NULL) { 
		struct rtable *rt;
		
		if (ip_route_output(&rt,
			opt && opt->srr ? opt->faddr : req->af.v4_req.rmt_addr,
			req->af.v4_req.loc_addr, sk->protinfo.af_inet.tos|RTO_CONN, 0))
			return NULL;
	        dst = &rt->u.dst;
	}

	newsk = tcp_create_openreq_child(sk, req, skb);
	if (!newsk)
		goto exit;

	sk->tp_pinfo.af_tcp.syn_backlog--;
	sk->ack_backlog++;

	newsk->dst_cache = dst;

	newtp = &(newsk->tp_pinfo.af_tcp);
	newsk->daddr = req->af.v4_req.rmt_addr;
	newsk->saddr = req->af.v4_req.loc_addr;
	newsk->rcv_saddr = req->af.v4_req.loc_addr;
	newsk->protinfo.af_inet.opt = req->af.v4_req.opt;
	newsk->protinfo.af_inet.mc_index = ((struct rtable*)skb->dst)->rt_iif;
	newsk->protinfo.af_inet.mc_ttl = skb->nh.iph->ttl;
	newtp->ext_header_len = 0;
	if (newsk->protinfo.af_inet.opt)
		newtp->ext_header_len = newsk->protinfo.af_inet.opt->optlen;

	tcp_sync_mss(newsk, dst->pmtu);
	tcp_initialize_rcv_mss(newsk);

	if (newsk->rcvbuf < (3 * (dst->advmss+40+MAX_HEADER+15)))
		newsk->rcvbuf = min ((3 * (dst->advmss+40+MAX_HEADER+15)), sysctl_rmem_max);
	if (newsk->sndbuf < (3 * (newtp->mss_clamp+40+MAX_HEADER+15)))
		newsk->sndbuf = min ((3 * (newtp->mss_clamp+40+MAX_HEADER+15)), sysctl_wmem_max);

	bh_lock_sock(newsk);
 
	__tcp_v4_hash(newsk);
	__tcp_inherit_port(sk, newsk);

	return newsk;

exit:
	dst_release(dst);
	return NULL;
}


static struct sock *tcp_v4_hnd_req(struct sock *sk,struct sk_buff *skb)
{
	struct open_request *req, *prev;
	struct tcphdr *th = skb->h.th;
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	/* Find possible connection requests. */
	req = tcp_v4_search_req(tp, skb->nh.iph, th, &prev);
	if (req)
		return tcp_check_req(sk, skb, req, prev);

#ifdef CONFIG_SYN_COOKIES
	if (!th->rst && (th->syn || th->ack))
		sk = cookie_v4_check(sk, skb, &(IPCB(skb)->opt));
#endif
	return sk;
}

static int tcp_csum_verify(struct sk_buff *skb)
{
	switch (skb->ip_summed) {
	case CHECKSUM_NONE:
		skb->csum = csum_partial((char *)skb->h.th, skb->len, 0);
	case CHECKSUM_HW:
		if (tcp_v4_check(skb->h.th,skb->len,skb->nh.iph->saddr,skb->nh.iph->daddr,skb->csum)) {
			NETDEBUG(printk(KERN_DEBUG "TCPv4 bad checksum "
					"from %d.%d.%d.%d:%04x to %d.%d.%d.%d:%04x, "
					"len=%d/%d\n",
					NIPQUAD(skb->nh.iph->saddr),
					ntohs(skb->h.th->source), 
					NIPQUAD(skb->nh.iph->daddr),
					ntohs(skb->h.th->dest),
					skb->len,
					ntohs(skb->nh.iph->tot_len)));
			return 1;
		}
		skb->ip_summed = CHECKSUM_UNNECESSARY;
	default:
		/* CHECKSUM_UNNECESSARY */
	}
	return 0;
}


/* The socket must have it's spinlock held when we get
 * here.
 *
 * We have a potential double-lock case here, so even when
 * doing backlog processing we use the BH locking scheme.
 * This is because we cannot sleep with the original spinlock
 * held.
 */
int tcp_v4_do_rcv(struct sock *sk, struct sk_buff *skb)
{
#ifdef CONFIG_FILTER
	struct sk_filter *filter = sk->filter;
	if (filter && sk_filter(skb, filter))
		goto discard;
#endif /* CONFIG_FILTER */

	/* 
	 * This doesn't check if the socket has enough room for the packet.
	 * Either process the packet _without_ queueing it and then free it,
	 * or do the check later.
	 */
	skb_set_owner_r(skb, sk);

	if (sk->state == TCP_ESTABLISHED) { /* Fast path */
		/* Ready to move deeper ... */
		if (tcp_csum_verify(skb))
			goto csum_err;
		if (tcp_rcv_established(sk, skb, skb->h.th, skb->len))
			goto reset;
		return 0; 
	} 

	if (tcp_csum_verify(skb))
		goto csum_err;

	if (sk->state == TCP_LISTEN) { 
		struct sock *nsk;

		nsk = tcp_v4_hnd_req(sk, skb);
		if (!nsk)
			goto discard;

		/*
		 * Queue it on the new socket if the new socket is active,
		 * otherwise we just shortcircuit this and continue with
		 * the new socket..
		 */
		if (nsk != sk) {
			int ret;
			int state = nsk->state;

			skb_orphan(skb);

			BUG_TRAP(nsk->lock.users == 0);
			skb_set_owner_r(skb, nsk);
			ret = tcp_rcv_state_process(nsk, skb, skb->h.th, skb->len);

			/* Wakeup parent, send SIGIO, if this packet changed
			   socket state from SYN-RECV.

			   It still looks ugly, however it is much better
			   than miracleous double wakeup in syn_recv_sock()
			   and tcp_rcv_state_process().
			 */
			if (state == TCP_SYN_RECV && nsk->state != state)
				sk->data_ready(sk, 0);

			bh_unlock_sock(nsk);
			if (ret)
				goto reset;
			return 0;
		}
	}
	
	if (tcp_rcv_state_process(sk, skb, skb->h.th, skb->len))
		goto reset;
	return 0;

reset:
	tcp_v4_send_reset(skb);
discard:
	kfree_skb(skb);
	/* Be careful here. If this function gets more complicated and
	 * gcc suffers from register pressure on the x86, sk (in %ebx) 
	 * might be destroyed here. This current version compiles correctly,
	 * but you have been warned.
	 */
	return 0;

csum_err:
	tcp_statistics.TcpInErrs++;
	goto discard;
}

/*
 *	From tcp_input.c
 */

int tcp_v4_rcv(struct sk_buff *skb, unsigned short len)
{
	struct tcphdr *th;
	struct sock *sk;
	int ret;

	if (skb->pkt_type!=PACKET_HOST)
		goto discard_it;

	th = skb->h.th;

	/* Pull up the IP header. */
	__skb_pull(skb, skb->h.raw - skb->data);

	/* Count it even if it's bad */
	tcp_statistics.TcpInSegs++;

	if (len < sizeof(struct tcphdr))
		goto bad_packet;

	TCP_SKB_CB(skb)->seq = ntohl(th->seq);
	TCP_SKB_CB(skb)->end_seq = (TCP_SKB_CB(skb)->seq + th->syn + th->fin +
				    len - th->doff*4);
	TCP_SKB_CB(skb)->ack_seq = ntohl(th->ack_seq);
	TCP_SKB_CB(skb)->when = 0;
	skb->used = 0;

	sk = __tcp_v4_lookup(skb->nh.iph->saddr, th->source,
			     skb->nh.iph->daddr, ntohs(th->dest), skb->dev->ifindex);

	if (!sk)
		goto no_tcp_socket;

process:
	if(!ipsec_sk_policy(sk,skb))
		goto discard_and_relse;

	if (sk->state == TCP_TIME_WAIT)
		goto do_time_wait;

	bh_lock_sock(sk);
	ret = 0;
	if (!sk->lock.users)
		ret = tcp_v4_do_rcv(sk, skb);
	else
		sk_add_backlog(sk, skb);
	bh_unlock_sock(sk);

	sock_put(sk);

	return ret;

no_tcp_socket:
	if (tcp_csum_verify(skb)) {
bad_packet:
		tcp_statistics.TcpInErrs++;
	} else {
		tcp_v4_send_reset(skb);
	}

discard_it:
	/* Discard frame. */
	kfree_skb(skb);
  	return 0;

discard_and_relse:
	sock_put(sk);
	goto discard_it;

do_time_wait:
	if (tcp_csum_verify(skb)) {
		tcp_statistics.TcpInErrs++;
		goto discard_and_relse;
	}
	switch(tcp_timewait_state_process((struct tcp_tw_bucket *)sk,
					  skb, th, skb->len)) {
	case TCP_TW_SYN:
	{
		struct sock *sk2;

		sk2 = tcp_v4_lookup_listener(skb->nh.iph->daddr, ntohs(th->dest), skb->dev->ifindex);
		if (sk2 != NULL) {
			tcp_tw_deschedule((struct tcp_tw_bucket *)sk);
			tcp_timewait_kill((struct tcp_tw_bucket *)sk);
			tcp_tw_put((struct tcp_tw_bucket *)sk);
			sk = sk2;
			goto process;
		}
		/* Fall through to ACK */
	}
	case TCP_TW_ACK:
		tcp_v4_timewait_ack(sk, skb);
		break;
	case TCP_TW_RST:
		goto no_tcp_socket;
	case TCP_TW_SUCCESS:
	}
	goto discard_it;
}

static void __tcp_v4_rehash(struct sock *sk)
{
	struct tcp_ehash_bucket *oldhead = &tcp_ehash[sk->hashent];
	struct tcp_ehash_bucket *head = &tcp_ehash[(sk->hashent = tcp_sk_hashfn(sk))];
	struct sock **skp = &head->chain;

	write_lock_bh(&oldhead->lock);
	if(sk->pprev) {
		if(sk->next)
			sk->next->pprev = sk->pprev;
		*sk->pprev = sk->next;
		sk->pprev = NULL;
	}
	write_unlock(&oldhead->lock);
	write_lock(&head->lock);
	if((sk->next = *skp) != NULL)
		(*skp)->pprev = &sk->next;
	*skp = sk;
	sk->pprev = skp;
	write_unlock_bh(&head->lock);
}

int tcp_v4_rebuild_header(struct sock *sk)
{
	struct rtable *rt = (struct rtable *)__sk_dst_get(sk);
	__u32 new_saddr;
        int want_rewrite = sysctl_ip_dynaddr && sk->state == TCP_SYN_SENT;

	if(rt == NULL)
		return 0;

	/* Force route checking if want_rewrite.
	 * The idea is good, the implementation is disguisting.
	 * Well, if I made bind on this socket, you cannot randomly ovewrite
	 * its source address. --ANK
	 */
	if (want_rewrite) {
		int tmp;
		struct rtable *new_rt;
		__u32 old_saddr = rt->rt_src;

		/* Query new route using another rt buffer */
		tmp = ip_route_connect(&new_rt, rt->rt_dst, 0,
					RT_TOS(sk->protinfo.af_inet.tos)|sk->localroute,
					sk->bound_dev_if);

		/* Only useful if different source addrs */
		if (tmp == 0) {
			/*
			 *	Only useful if different source addrs
			 */
			if (new_rt->rt_src != old_saddr ) {
				__sk_dst_set(sk, &new_rt->u.dst);
				rt = new_rt;
				goto do_rewrite;
			}
			dst_release(&new_rt->u.dst);
		}
	}
	if (rt->u.dst.obsolete) {
		int err;
		err = ip_route_output(&rt, rt->rt_dst, rt->rt_src, rt->key.tos|RTO_CONN, rt->key.oif);
		if (err) {
			sk->err_soft=-err;
			sk->error_report(sk);
			return -1;
		}
		__sk_dst_set(sk, &rt->u.dst);
	}

	return 0;

do_rewrite:
	new_saddr = rt->rt_src;
                
	/* Ouch!, this should not happen. */
	if (!sk->saddr || !sk->rcv_saddr) {
		printk(KERN_WARNING "tcp_v4_rebuild_header(): not valid sock addrs: "
		       "saddr=%08lX rcv_saddr=%08lX\n",
		       ntohl(sk->saddr), 
		       ntohl(sk->rcv_saddr));
		return 0;
	}

	if (new_saddr != sk->saddr) {
		if (sysctl_ip_dynaddr > 1) {
			printk(KERN_INFO "tcp_v4_rebuild_header(): shifting sk->saddr "
			       "from %d.%d.%d.%d to %d.%d.%d.%d\n",
			       NIPQUAD(sk->saddr), 
			       NIPQUAD(new_saddr));
		}

		sk->saddr = new_saddr;
		sk->rcv_saddr = new_saddr;

		/* XXX The only one ugly spot where we need to
		 * XXX really change the sockets identity after
		 * XXX it has entered the hashes. -DaveM
		 *
		 * Besides that, it does not check for connetion
		 * uniqueness. Wait for troubles.
		 */
		__tcp_v4_rehash(sk);
	} 
        
	return 0;
}

static void v4_addr2sockaddr(struct sock *sk, struct sockaddr * uaddr)
{
	struct sockaddr_in *sin = (struct sockaddr_in *) uaddr;

	sin->sin_family		= AF_INET;
	sin->sin_addr.s_addr	= sk->daddr;
	sin->sin_port		= sk->dport;
}

struct tcp_func ipv4_specific = {
	ip_queue_xmit,
	tcp_v4_send_check,
	tcp_v4_rebuild_header,
	tcp_v4_conn_request,
	tcp_v4_syn_recv_sock,
	tcp_v4_hash_connecting,
	sizeof(struct iphdr),

	ip_setsockopt,
	ip_getsockopt,
	v4_addr2sockaddr,
	sizeof(struct sockaddr_in)
};

/* NOTE: A lot of things set to zero explicitly by call to
 *       sk_alloc() so need not be done here.
 */
static int tcp_v4_init_sock(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	skb_queue_head_init(&tp->out_of_order_queue);
	tcp_init_xmit_timers(sk);

	tp->rto  = TCP_TIMEOUT_INIT;
	tp->mdev = TCP_TIMEOUT_INIT;
      
	/* So many TCP implementations out there (incorrectly) count the
	 * initial SYN frame in their delayed-ACK and congestion control
	 * algorithms that we must have the following bandaid to talk
	 * efficiently to them.  -DaveM
	 */
	tp->snd_cwnd = 2;

	/* See draft-stevens-tcpca-spec-01 for discussion of the
	 * initialization of these values.
	 */
	tp->snd_cwnd_cnt = 0;
	tp->snd_ssthresh = 0x7fffffff;	/* Infinity */
	tp->snd_cwnd_clamp = ~0;
	tp->mss_cache = 536;

	sk->state = TCP_CLOSE;
	sk->max_ack_backlog = SOMAXCONN;

	sk->write_space = tcp_write_space; 

	/* Init SYN queue. */
	tcp_synq_init(tp);

	sk->tp_pinfo.af_tcp.af_specific = &ipv4_specific;

	return 0;
}

static int tcp_v4_destroy_sock(struct sock *sk)
{
	struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

	tcp_clear_xmit_timers(sk);

	/* Cleanup up the write buffer. */
  	__skb_queue_purge(&sk->write_queue);

	/* Cleans up our, hopefuly empty, out_of_order_queue. */
  	__skb_queue_purge(&tp->out_of_order_queue);

	/* Clean up a referenced TCP bind bucket, this only happens if a
	 * port is allocated for a socket, but it never fully connects.
	 */
	if(sk->prev != NULL)
		tcp_put_port(sk);

	return 0;
}

/* Proc filesystem TCP sock list dumping. */
static void get_openreq(struct sock *sk, struct open_request *req, char *tmpbuf, int i)
{
	sprintf(tmpbuf, "%4d: %08lX:%04X %08lX:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5d %8d %u %d %p",
		i,
		(long unsigned int)req->af.v4_req.loc_addr,
		ntohs(sk->sport),
		(long unsigned int)req->af.v4_req.rmt_addr,
		ntohs(req->rmt_port),
		TCP_SYN_RECV,
		0,0, /* could print option size, but that is af dependent. */
		1,   /* timers active (only the expire timer) */  
		(unsigned long)(req->expires - jiffies), 
		req->retrans,
		sk->socket ? sk->socket->inode->i_uid : 0,
		0,  /* non standard timer */  
		0, /* open_requests have no inode */
		atomic_read(&sk->refcnt),
		req
		); 
}

static void get_tcp_sock(struct sock *sp, char *tmpbuf, int i)
{
	unsigned int dest, src;
	__u16 destp, srcp;
	int timer_active, timer_active1, timer_active2;
	unsigned long timer_expires;
	struct tcp_opt *tp = &sp->tp_pinfo.af_tcp;

	dest  = sp->daddr;
	src   = sp->rcv_saddr;
	destp = ntohs(sp->dport);
	srcp  = ntohs(sp->sport);
	timer_active1 = tp->retransmit_timer.prev != NULL;
	timer_active2 = sp->timer.prev != NULL;
	timer_active	= 0;
	timer_expires	= (unsigned) -1;
	if (timer_active1 && tp->retransmit_timer.expires < timer_expires) {
		timer_active	= 1;
		timer_expires	= tp->retransmit_timer.expires;
	}
	if (timer_active2 && sp->timer.expires < timer_expires) {
		timer_active	= 2;
		timer_expires	= sp->timer.expires;
	}
	if(timer_active == 0)
		timer_expires = jiffies;

	sprintf(tmpbuf, "%4d: %08X:%04X %08X:%04X"
		" %02X %08X:%08X %02X:%08lX %08X %5d %8d %ld %d %p",
		i, src, srcp, dest, destp, sp->state, 
		tp->write_seq-tp->snd_una, tp->rcv_nxt-tp->copied_seq,
		timer_active, timer_expires-jiffies,
		tp->retransmits,
		sp->socket ? sp->socket->inode->i_uid : 0,
		0,
		sp->socket ? sp->socket->inode->i_ino : 0,
		atomic_read(&sp->refcnt), sp);
}

static void get_timewait_sock(struct tcp_tw_bucket *tw, char *tmpbuf, int i)
{
	unsigned int dest, src;
	__u16 destp, srcp;
	int slot_dist;

	dest  = tw->daddr;
	src   = tw->rcv_saddr;
	destp = ntohs(tw->dport);
	srcp  = ntohs(tw->sport);

	slot_dist = tw->death_slot;
	if(slot_dist > tcp_tw_death_row_slot)
		slot_dist = (TCP_TWKILL_SLOTS - slot_dist) + tcp_tw_death_row_slot;
	else
		slot_dist = tcp_tw_death_row_slot - slot_dist;

	sprintf(tmpbuf, "%4d: %08X:%04X %08X:%04X"
		" %02X %08X:%08X %02X:%08X %08X %5d %8d %d %d %p",
		i, src, srcp, dest, destp, TCP_TIME_WAIT, 0, 0,
		3, slot_dist * TCP_TWKILL_PERIOD, 0, 0, 0, 0,
		atomic_read(&tw->refcnt), tw);
}

int tcp_get_info(char *buffer, char **start, off_t offset, int length)
{
	int len = 0, num = 0, i;
	off_t begin, pos = 0;
	char tmpbuf[129];

	if (offset < 128)
		len += sprintf(buffer, "%-127s\n",
			       "  sl  local_address rem_address   st tx_queue "
			       "rx_queue tr tm->when retrnsmt   uid  timeout inode");

	pos = 128;

	/* First, walk listening socket table. */
	tcp_listen_lock();
	for(i = 0; i < TCP_LHTABLE_SIZE; i++) {
		struct sock *sk = tcp_listening_hash[i];

		for (sk = tcp_listening_hash[i]; sk; sk = sk->next, num++) {
			struct open_request *req;
			struct tcp_opt *tp = &(sk->tp_pinfo.af_tcp);

			if (!TCP_INET_FAMILY(sk->family))
				goto skip_listen;

			pos += 128;
			if (pos >= offset) {
				get_tcp_sock(sk, tmpbuf, num);
				len += sprintf(buffer+len, "%-127s\n", tmpbuf);
				if (len >= length) {
					tcp_listen_unlock();
					goto out_no_bh;
				}
			}

skip_listen:
			lock_sock(sk);
			for (req = tp->syn_wait_queue; req; req = req->dl_next, num++) {
				if (req->sk)
					continue;
				if (!TCP_INET_FAMILY(req->class->family))
					continue;

				pos += 128;
				if (pos < offset)
					continue;
				get_openreq(sk, req, tmpbuf, num);
				len += sprintf(buffer+len, "%-127s\n", tmpbuf);
				if(len >= length) {
					tcp_listen_unlock();
					release_sock(sk);
					goto out_no_bh;
				}
			}
			release_sock(sk);
		}
	}
	tcp_listen_unlock();

	local_bh_disable();

	/* Next, walk established hash chain. */
	for (i = 0; i < tcp_ehash_size; i++) {
		struct tcp_ehash_bucket *head = &tcp_ehash[i];
		struct sock *sk;
		struct tcp_tw_bucket *tw;

		read_lock(&head->lock);
		for(sk = head->chain; sk; sk = sk->next, num++) {
			if (!TCP_INET_FAMILY(sk->family))
				continue;
			pos += 128;
			if (pos < offset)
				continue;
			get_tcp_sock(sk, tmpbuf, num);
			len += sprintf(buffer+len, "%-127s\n", tmpbuf);
			if(len >= length) {
				read_unlock(&head->lock);
				goto out;
			}
		}
		for (tw = (struct tcp_tw_bucket *)tcp_ehash[i+tcp_ehash_size].chain;
		     tw != NULL;
		     tw = (struct tcp_tw_bucket *)tw->next, num++) {
			if (!TCP_INET_FAMILY(tw->family))
				continue;
			pos += 128;
			if (pos < offset)
				continue;
			get_timewait_sock(tw, tmpbuf, num);
			len += sprintf(buffer+len, "%-127s\n", tmpbuf);
			if(len >= length) {
				read_unlock(&head->lock);
				goto out;
			}
		}
		read_unlock(&head->lock);
	}

out:
	local_bh_enable();
out_no_bh:

	begin = len - (pos - offset);
	*start = buffer + begin;
	len -= begin;
	if(len > length)
		len = length;
	if (len < 0)
		len = 0; 
	return len;
}

struct proto tcp_prot = {
	tcp_close,			/* close */
	tcp_v4_connect,			/* connect */
	tcp_disconnect,			/* disconnect */
	tcp_accept,			/* accept */
	NULL,				/* retransmit */
	tcp_write_wakeup,		/* write_wakeup */
	tcp_read_wakeup,		/* read_wakeup */
	tcp_poll,			/* poll */
	tcp_ioctl,			/* ioctl */
	tcp_v4_init_sock,		/* init */
	tcp_v4_destroy_sock,		/* destroy */
	tcp_shutdown,			/* shutdown */
	tcp_setsockopt,			/* setsockopt */
	tcp_getsockopt,			/* getsockopt */
	tcp_v4_sendmsg,			/* sendmsg */
	tcp_recvmsg,			/* recvmsg */
	NULL,				/* bind */
	tcp_v4_do_rcv,			/* backlog_rcv */
	tcp_v4_hash,			/* hash */
	tcp_unhash,			/* unhash */
	tcp_v4_get_port,		/* get_port */
	128,				/* max_header */
	0,				/* retransmits */
	"TCP",				/* name */
	0,				/* inuse */
	0				/* highestinuse */
};



void __init tcp_v4_init(struct net_proto_family *ops)
{
	int err;

	tcp_inode.i_mode = S_IFSOCK;
	tcp_inode.i_sock = 1;
	tcp_inode.i_uid = 0;
	tcp_inode.i_gid = 0;
	init_waitqueue_head(&tcp_inode.i_wait);
	init_waitqueue_head(&tcp_inode.u.socket_i.wait);

	tcp_socket->inode = &tcp_inode;
	tcp_socket->state = SS_UNCONNECTED;
	tcp_socket->type=SOCK_RAW;

	if ((err=ops->create(tcp_socket, IPPROTO_TCP))<0)
		panic("Failed to create the TCP control socket.\n");
	tcp_socket->sk->allocation=GFP_ATOMIC;
	tcp_socket->sk->protinfo.af_inet.ttl = MAXTTL;

	/* Unhash it so that IP input processing does not even
	 * see it, we do not wish this socket to see incoming
	 * packets.
	 */
	tcp_socket->sk->prot->unhash(tcp_socket->sk);
}
