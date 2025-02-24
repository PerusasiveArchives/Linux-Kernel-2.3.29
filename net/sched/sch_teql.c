/* net/sched/sch_teql.c	"True" (or "trivial") link equalizer.
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 * Authors:	Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */

#include <linux/module.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/in.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/notifier.h>
#include <linux/init.h>
#include <net/ip.h>
#include <net/route.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/pkt_sched.h>

/*
   How to setup it.
   ----------------

   After loading this module you will find a new device teqlN
   and new qdisc with the same name. To join a slave to the equalizer
   you should just set this qdisc on a device f.e.

   # tc qdisc add dev eth0 root teql0
   # tc qdisc add dev eth1 root teql0

   That's all. Full PnP 8)

   Applicability.
   --------------

   1. Slave devices MUST be active devices, i.e., they must raise the tbusy
      signal and generate EOI events. If you want to equalize virtual devices
      like tunnels, use a normal eql device.
   2. This device puts no limitations on physical slave characteristics
      f.e. it will equalize 9600baud line and 100Mb ethernet perfectly :-)
      Certainly, large difference in link speeds will make the resulting
      eqalized link unusable, because of huge packet reordering.
      I estimate an upper useful difference as ~10 times.
   3. If the slave requires address resolution, only protocols using
      neighbour cache (IPv4/IPv6) will work over the equalized link.
      Other protocols are still allowed to use the slave device directly,
      which will not break load balancing, though native slave
      traffic will have the highest priority.  */

struct teql_master
{
	struct Qdisc_ops qops;
	struct net_device dev;
	struct Qdisc *slaves;
	struct net_device_stats stats;
	char name[IFNAMSIZ];
};

struct teql_sched_data
{
	struct Qdisc *next;
	struct teql_master *m;
	struct neighbour *ncache;
	struct sk_buff_head q;
};

#define NEXT_SLAVE(q) (((struct teql_sched_data*)((q)->data))->next)

#define FMASK (IFF_BROADCAST|IFF_POINTOPOINT|IFF_BROADCAST)

/* "teql*" qdisc routines */

static int
teql_enqueue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct net_device *dev = sch->dev;
	struct teql_sched_data *q = (struct teql_sched_data *)sch->data;

	__skb_queue_tail(&q->q, skb);
	if (q->q.qlen <= dev->tx_queue_len) {
		sch->stats.bytes += skb->len;
		sch->stats.packets++;
		return 0;
	}

	__skb_unlink(skb, &q->q);
	kfree_skb(skb);
	sch->stats.drops++;
	return NET_XMIT_DROP;
}

static int
teql_requeue(struct sk_buff *skb, struct Qdisc* sch)
{
	struct teql_sched_data *q = (struct teql_sched_data *)sch->data;

	__skb_queue_head(&q->q, skb);
	return 0;
}

static struct sk_buff *
teql_dequeue(struct Qdisc* sch)
{
	struct teql_sched_data *dat = (struct teql_sched_data *)sch->data;
	struct sk_buff *skb;

	skb = __skb_dequeue(&dat->q);
	if (skb == NULL) {
		struct net_device *m = dat->m->dev.qdisc->dev;
		if (m) {
			dat->m->slaves = sch;
			spin_lock(&m->queue_lock);
			m->tbusy = 0;
			qdisc_restart(m);
			spin_unlock(&m->queue_lock);
		}
	}
	sch->q.qlen = dat->q.qlen + dat->m->dev.qdisc->q.qlen;
	return skb;
}

static __inline__ void
teql_neigh_release(struct neighbour *n)
{
	if (n)
		neigh_release(n);
}

static void
teql_reset(struct Qdisc* sch)
{
	struct teql_sched_data *dat = (struct teql_sched_data *)sch->data;

	skb_queue_purge(&dat->q);
	sch->q.qlen = 0;
	teql_neigh_release(xchg(&dat->ncache, NULL));
}

static void
teql_destroy(struct Qdisc* sch)
{
	struct Qdisc *q, *prev;
	struct teql_sched_data *dat = (struct teql_sched_data *)sch->data;
	struct teql_master *master = dat->m;

	if ((prev = master->slaves) != NULL) {
		do {
			q = NEXT_SLAVE(prev);
			if (q == sch) {
				NEXT_SLAVE(prev) = NEXT_SLAVE(q);
				if (q == master->slaves) {
					master->slaves = NEXT_SLAVE(q);
					if (q == master->slaves) {
						master->slaves = NULL;
						spin_lock_bh(&master->dev.queue_lock);
						qdisc_reset(master->dev.qdisc);
						spin_unlock_bh(&master->dev.queue_lock);
					}
				}
				skb_queue_purge(&dat->q);
				teql_neigh_release(xchg(&dat->ncache, NULL));
				break;
			}
				
		} while ((prev = q) != master->slaves);
	}

	MOD_DEC_USE_COUNT;
}

static int teql_qdisc_init(struct Qdisc *sch, struct rtattr *opt)
{
	struct net_device *dev = sch->dev;
	struct teql_master *m = (struct teql_master*)sch->ops;
	struct teql_sched_data *q = (struct teql_sched_data *)sch->data;

	if (dev->hard_header_len > m->dev.hard_header_len)
		return -EINVAL;

	if (&m->dev == dev)
		return -ELOOP;

	q->m = m;

	skb_queue_head_init(&q->q);

	if (m->slaves) {
		if (m->dev.flags & IFF_UP) {
			if ((m->dev.flags&IFF_POINTOPOINT && !(dev->flags&IFF_POINTOPOINT))
			    || (m->dev.flags&IFF_BROADCAST && !(dev->flags&IFF_BROADCAST))
			    || (m->dev.flags&IFF_MULTICAST && !(dev->flags&IFF_MULTICAST))
			    || dev->mtu < m->dev.mtu)
				return -EINVAL;
		} else {
			if (!(dev->flags&IFF_POINTOPOINT))
				m->dev.flags &= ~IFF_POINTOPOINT;
			if (!(dev->flags&IFF_BROADCAST))
				m->dev.flags &= ~IFF_BROADCAST;
			if (!(dev->flags&IFF_MULTICAST))
				m->dev.flags &= ~IFF_MULTICAST;
			if (dev->mtu < m->dev.mtu)
				m->dev.mtu = dev->mtu;
		}
		q->next = NEXT_SLAVE(m->slaves);
		NEXT_SLAVE(m->slaves) = sch;
	} else {
		q->next = sch;
		m->slaves = sch;
		m->dev.mtu = dev->mtu;
		m->dev.flags = (m->dev.flags&~FMASK)|(dev->flags&FMASK);
	}
	
	MOD_INC_USE_COUNT;
	return 0;
}

/* "teql*" netdevice routines */

static int
__teql_resolve(struct sk_buff *skb, struct sk_buff *skb_res, struct net_device *dev)
{
	struct teql_sched_data *q = (void*)dev->qdisc->data;
	struct neighbour *mn = skb->dst->neighbour;
	struct neighbour *n = q->ncache;

	if (mn->tbl == NULL)
		return -EINVAL;
	if (n && n->tbl == mn->tbl &&
	    memcmp(n->primary_key, mn->primary_key, mn->tbl->key_len) == 0) {
		atomic_inc(&n->refcnt);
	} else {
		n = __neigh_lookup(mn->tbl, mn->primary_key, dev, 1);
		if (n == NULL)
			return -ENOBUFS;
	}
	if (neigh_event_send(n, skb_res) == 0) {
		int err;
		read_lock(&n->lock);
		err = dev->hard_header(skb, dev, ntohs(skb->protocol), n->ha, NULL, skb->len);
		read_unlock(&n->lock);
		if (err < 0) {
			neigh_release(n);
			return -EINVAL;
		}
		teql_neigh_release(xchg(&q->ncache, n));
		return 0;
	}
	neigh_release(n);
	return (skb_res == NULL) ? -EAGAIN : 1;
}

static __inline__ int
teql_resolve(struct sk_buff *skb, struct sk_buff *skb_res, struct net_device *dev)
{
	if (dev->hard_header == NULL ||
	    skb->dst == NULL ||
	    skb->dst->neighbour == NULL)
		return 0;
	return __teql_resolve(skb, skb_res, dev);
}

static int teql_master_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct teql_master *master = (void*)dev->priv;
	struct Qdisc *start, *q;
	int busy;
	int nores;
	int len = skb->len;
	struct sk_buff *skb_res = NULL;

	dev->tbusy = 1;

	start = master->slaves;

restart:
	nores = 0;
	busy = 0;

	if ((q = start) == NULL)
		goto drop;

	do {
		struct net_device *slave = q->dev;
		
		if (slave->qdisc_sleeping != q)
			continue;
		if (slave->tbusy) {
			busy = 1;
			continue;
		}

		if (!qdisc_on_runqueue(q))
			qdisc_run(q);

		switch (teql_resolve(skb, skb_res, slave)) {
		case 0:
			if (spin_trylock(&slave->xmit_lock)) {
				slave->xmit_lock_owner = smp_processor_id();
				if (slave->hard_start_xmit(skb, slave) == 0) {
					slave->xmit_lock_owner = -1;
					spin_unlock(&slave->xmit_lock);
					master->slaves = NEXT_SLAVE(q);
					dev->tbusy = 0;
					master->stats.tx_packets++;
					master->stats.tx_bytes += len;
					return 0;
				}
				slave->xmit_lock_owner = -1;
				spin_unlock(&slave->xmit_lock);
			}
			if (dev->tbusy)
				busy = 1;
			break;
		case 1:
			master->slaves = NEXT_SLAVE(q);
			dev->tbusy = 0;
			return 0;
		default:
			nores = 1;
			break;
		}
		__skb_pull(skb, skb->nh.raw - skb->data);
	} while ((q = NEXT_SLAVE(q)) != start);

	if (nores && skb_res == NULL) {
		skb_res = skb;
		goto restart;
	}

	dev->tbusy = busy;
	if (busy)
		return 1;
	master->stats.tx_errors++;

drop:
	master->stats.tx_dropped++;
	dev_kfree_skb(skb);
	return 0;
}

static int teql_master_open(struct net_device *dev)
{
	struct Qdisc * q;
	struct teql_master *m = (void*)dev->priv;
	int mtu = 0xFFFE;
	unsigned flags = IFF_NOARP|IFF_MULTICAST;

	if (m->slaves == NULL)
		return -EUNATCH;

	flags = FMASK;

	q = m->slaves;
	do {
		struct net_device *slave = q->dev;

		if (slave == NULL)
			return -EUNATCH;

		if (slave->mtu < mtu)
			mtu = slave->mtu;
		if (slave->hard_header_len > LL_MAX_HEADER)
			return -EINVAL;

		/* If all the slaves are BROADCAST, master is BROADCAST
		   If all the slaves are PtP, master is PtP
		   Otherwise, master is NBMA.
		 */
		if (!(slave->flags&IFF_POINTOPOINT))
			flags &= ~IFF_POINTOPOINT;
		if (!(slave->flags&IFF_BROADCAST))
			flags &= ~IFF_BROADCAST;
		if (!(slave->flags&IFF_MULTICAST))
			flags &= ~IFF_MULTICAST;
	} while ((q = NEXT_SLAVE(q)) != m->slaves);

	m->dev.mtu = mtu;
	m->dev.flags = (m->dev.flags&~FMASK) | flags;
	m->dev.tbusy = 0;
	MOD_INC_USE_COUNT;
	return 0;
}

static int teql_master_close(struct net_device *dev)
{
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct net_device_stats *teql_master_stats(struct net_device *dev)
{
	struct teql_master *m = (void*)dev->priv;
	return &m->stats;
}

static int teql_master_mtu(struct net_device *dev, int new_mtu)
{
	struct teql_master *m = (void*)dev->priv;
	struct Qdisc *q;

	if (new_mtu < 68)
		return -EINVAL;

	q = m->slaves;
	if (q) {
		do {
			if (new_mtu > q->dev->mtu)
				return -EINVAL;
		} while ((q=NEXT_SLAVE(q)) != m->slaves);
	}

	dev->mtu = new_mtu;
	return 0;
}

static int teql_master_init(struct net_device *dev)
{
	dev->open		= teql_master_open;
	dev->hard_start_xmit	= teql_master_xmit;
	dev->stop		= teql_master_close;
	dev->get_stats		= teql_master_stats;
	dev->change_mtu		= teql_master_mtu;
	dev->type		= 0;
	dev->mtu		= 1500;
	dev->tx_queue_len	= 100;
	dev->flags		= IFF_NOARP;
	dev->hard_header_len	= LL_MAX_HEADER;
	return 0;
}

static struct teql_master the_master = {
{
	NULL,
	NULL,
	"",
	sizeof(struct teql_sched_data),

	teql_enqueue,
	teql_dequeue,
	teql_requeue,
	NULL,

	teql_qdisc_init,
	teql_reset,
	teql_destroy,
	NULL,
},};


#ifdef MODULE
int init_module(void)
#else
int __init teql_init(void)
#endif
{
	int err;

	rtnl_lock();

	the_master.dev.priv = (void*)&the_master;
	the_master.dev.name = (void*)&the_master.name;
	err = dev_alloc_name(&the_master.dev, "teql%d");
	if (err < 0)
		return err;
	memcpy(the_master.qops.id, the_master.name, IFNAMSIZ);
	the_master.dev.init = teql_master_init;

	err = register_netdevice(&the_master.dev);
	if (err == 0) {
		err = register_qdisc(&the_master.qops);
		if (err)
			unregister_netdevice(&the_master.dev);
	}
	rtnl_unlock();
	return err;
}

#ifdef MODULE
void cleanup_module(void) 
{
	rtnl_lock();
	unregister_qdisc(&the_master.qops);
	unregister_netdevice(&the_master.dev);
	rtnl_unlock();
}
#endif
