
/*
 * DECnet       An implementation of the DECnet protocol suite for the LINUX
 *              operating system.  DECnet is implemented using the  BSD Socket
 *              interface as the means of communication with the user level.
 *
 *              DECnet Network Services Protocol (Input)
 *
 * Author:      Eduardo Marcelo Serrat <emserrat@geocities.com>
 *
 * Changes:
 *
 *    Steve Whitehouse:  Split into dn_nsp_in.c and dn_nsp_out.c from
 *                       original dn_nsp.c.
 *    Steve Whitehouse:  Updated to work with my new routing architecture.
 *    Steve Whitehouse:  Add changes from Eduardo Serrat's patches.
 *    Steve Whitehouse:  Put all ack handling code in a common routine.
 *    Steve Whitehouse:  Put other common bits into dn_nsp_rx()
 *    Steve Whitehouse:  More checks on skb->len to catch bogus packets
 *                       Fixed various race conditions and possible nasties.
 *    Steve Whitehouse:  Now handles returned conninit frames.
 *     David S. Miller:  New socket locking
 *    Steve Whitehouse:  Fixed lockup when socket filtering was enabled.
 */

/******************************************************************************
    (c) 1995-1998 E.M. Serrat		emserrat@geocities.com
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
*******************************************************************************/

#include <linux/config.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/string.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/netdevice.h>
#include <linux/inet.h>
#include <linux/route.h>
#include <net/sock.h>
#include <asm/segment.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/termios.h>      
#include <linux/interrupt.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/netfilter_decnet.h>
#include <net/neighbour.h>
#include <net/dst.h>
#include <net/dn_nsp.h>
#include <net/dn_dev.h>
#include <net/dn_route.h>
#include <net/dn_raw.h>


/*
 * For this function we've flipped the cross-subchannel bit
 * if the message is an otherdata or linkservice message. Thus
 * we can use it to work out what to update.
 */
static void dn_ack(struct sock *sk, struct sk_buff *skb, unsigned short ack)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	unsigned short type = ((ack >> 12) & 0x0003);
	int wakeup = 0;

	/* printk(KERN_DEBUG "dn_ack: %hd 0x%04hx\n", type, ack); */

	switch(type) {
		case 0: /* ACK - Data */
			if (after(ack, scp->ackrcv_dat)) {
				scp->ackrcv_dat = ack & 0x0fff;
				wakeup |= dn_nsp_check_xmit_queue(sk, skb, &scp->data_xmit_queue, ack);
			}
			break;
		case 1: /* NAK - Data */
			break;
		case 2: /* ACK - OtherData */
			if (after(ack, scp->ackrcv_oth)) {
				scp->ackrcv_oth = ack & 0x0fff;
				wakeup |= dn_nsp_check_xmit_queue(sk, skb, &scp->other_xmit_queue, ack);
			}
			break;
		case 3: /* NAK - OtherData */
			break;
	}

	if (wakeup && !sk->dead)
		sk->state_change(sk);
}

/*
 * This function is a universal ack processor.
 */
static int dn_process_ack(struct sock *sk, struct sk_buff *skb, int oth)
{
	unsigned short *ptr = (unsigned short *)skb->data;
	int len = 0;
	unsigned short ack;

	if (skb->len < 2)
		return len;

	if ((ack = dn_ntohs(*ptr)) & 0x8000) {
		skb_pull(skb, 2);
		ptr++;
		len += 2;
		if ((ack & 0x4000) == 0) {
			if (oth) 
				ack ^= 0x2000;
			dn_ack(sk, skb, ack);
		}
	}

	if (skb->len < 2)
		return len;

	if ((ack = dn_ntohs(*ptr)) & 0x8000) {
		skb_pull(skb, 2);
		len += 2;
		if ((ack & 0x4000) == 0) {
			if (oth) 
				ack ^= 0x2000;
			dn_ack(sk, skb, ack);
		}
	}

	return len;
}


/*
 * This function uses a slightly different lookup method
 * to find its sockets, since it searches on object name/number
 * rather than port numbers
 */
static struct sock *dn_find_listener(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct nsp_conn_init_msg *msg = (struct nsp_conn_init_msg *)skb->data;
	struct sockaddr_dn addr;
	unsigned char type = 0;

	memset(&addr, 0, sizeof(struct sockaddr_dn));

	cb->src_port = msg->srcaddr;
	cb->dst_port = msg->dstaddr;
	cb->services = msg->services;
	cb->info     = msg->info;
	cb->segsize  = dn_ntohs(msg->segsize);

	skb_pull(skb, sizeof(*msg));

	/* printk(KERN_DEBUG "username2sockaddr 1\n"); */
	if (dn_username2sockaddr(skb->data, skb->len, &addr, &type) < 0)
		goto err_out;

	if (type > 1)
		goto err_out;

	/* printk(KERN_DEBUG "looking for listener...\n"); */
	return dn_sklist_find_listener(&addr);
err_out:
	return NULL;
}

static void dn_nsp_conn_init(struct sock *sk, struct sk_buff *skb)
{
	/* printk(KERN_DEBUG "checking backlog...\n"); */
	if (sk->ack_backlog >= sk->max_ack_backlog) {
		kfree_skb(skb);
		return;
	}

	/* printk(KERN_DEBUG "waking up socket...\n"); */
	sk->ack_backlog++;
	skb_queue_tail(&sk->receive_queue, skb);
	sk->state_change(sk);
}

static void dn_nsp_conn_conf(struct sock *sk, struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct dn_scp *scp = &sk->protinfo.dn;

	if (skb->len < 3)
		goto out;

	cb->services = *skb->data;
	cb->info = *(skb->data+1);
	skb_pull(skb, 2);
	cb->segsize = dn_ntohs(*(__u16 *)skb->data);
	skb_pull(skb, 2);

	/*
	 * FIXME: Check out services and info fields to check that
	 * we can talk to this kind of node.
	 */

	if ((scp->state == DN_CI) || (scp->state == DN_CD)) {
		scp->persist = 0;
                scp->addrrem = cb->src_port;
                sk->state = TCP_ESTABLISHED;
                scp->state = DN_RUN;

		if (scp->mss > cb->segsize)
			scp->mss = cb->segsize;
		if (scp->mss < 230)
			scp->mss = 230;

		if (skb->len > 0) {
			unsigned char dlen = *skb->data;
			if ((dlen <= 16) && (dlen <= skb->len)) {
				scp->conndata_in.opt_optl = dlen;
				memcpy(scp->conndata_in.opt_data, skb->data + 1, dlen);
			}
		}
                dn_nsp_send_lnk(sk, DN_NOCHANGE);
                if (!sk->dead)
                	sk->state_change(sk);
        }

out:
        kfree_skb(skb);
}

static void dn_nsp_conn_ack(struct sock *sk, struct sk_buff *skb)
{
	struct dn_scp *scp = &sk->protinfo.dn;

	if (scp->state == DN_CI) {
		scp->state = DN_CD;
		scp->persist = 0;
	}

	kfree_skb(skb);
}

static void dn_nsp_disc_init(struct sock *sk, struct sk_buff *skb)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned short reason;

	/* printk(KERN_DEBUG "DECnet: discinit %d\n", skb->len); */

	if (skb->len < 2)
		goto out;

	reason = dn_ntohs(*(__u16 *)skb->data);
	skb_pull(skb, 2);

	scp->discdata_in.opt_status = reason;
	scp->discdata_in.opt_optl   = 0;
	memset(scp->discdata_in.opt_data, 0, 16);

	if (skb->len > 0) {
		unsigned char dlen = *skb->data;
		if ((dlen <= 16) && (dlen <= skb->len)) {
			scp->discdata_in.opt_optl = dlen;
			memcpy(scp->discdata_in.opt_data, skb->data + 1, dlen);
		}
	}

	scp->addrrem = cb->src_port;
	sk->state    = TCP_CLOSE;

	/* printk(KERN_DEBUG "DECnet: discinit\n"); */

	switch(scp->state) {
		case DN_CI:
		case DN_CD:
			scp->state = DN_RJ;
			break;
		case DN_RUN:
			sk->shutdown |= SHUTDOWN_MASK;
			scp->state = DN_DN;
			break;
		case DN_DI:
			scp->state = DN_DIC;
			break;
	}

	if (!sk->dead)
		sk->state_change(sk);

	dn_destroy_sock(sk);

out:
	kfree_skb(skb);
}

/*
 * disc_conf messages are also called no_resources or no_link
 * messages depending upon the "reason" field.
 */
static void dn_nsp_disc_conf(struct sock *sk, struct sk_buff *skb)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	unsigned short reason;

	if (skb->len != 2)
		goto out;

	reason = dn_ntohs(*(__u16 *)skb->data);

	sk->state = TCP_CLOSE;

	switch(scp->state) {
		case DN_CI:
			scp->state = DN_NR;
			break;
		case DN_DR:
			if (reason == NSP_REASON_DC)
				scp->state = DN_DRC;
			if (reason == NSP_REASON_NL)
				scp->state = DN_CN;
			break;
		case DN_DI:
			scp->state = DN_DIC;
			break;
		case DN_RUN:
			sk->shutdown |= SHUTDOWN_MASK;
		case DN_CC:
			scp->state = DN_CN;
	}

	if (!sk->dead)
		sk->state_change(sk);

	dn_destroy_sock(sk);

out:
	kfree_skb(skb);
}

static void dn_nsp_linkservice(struct sock *sk, struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	unsigned short segnum;
	unsigned char lsflags;
	char fcval;

	if (skb->len != 4)
		goto out;

	cb->segnum = segnum = dn_ntohs(*(__u16 *)skb->data);
	skb_pull(skb, 2);
	lsflags = *(unsigned char *)skb->data;
	skb_pull(skb, 1);
	fcval = *(char *)skb->data;

	if (lsflags & 0xf0)
		goto out;

	if (((sk->protinfo.dn.numoth_rcv + 1) & 0x0FFF) == (segnum & 0x0FFF)) {
        	sk->protinfo.dn.numoth_rcv += 1;        
                switch(lsflags & 0x03) {
                	case 0x00:      
                        	break;
                        case 0x01:      
                                sk->protinfo.dn.flowrem_sw = DN_DONTSEND;
                                break;
                        case 0x02:      
                                sk->protinfo.dn.flowrem_sw = DN_SEND;
				dn_nsp_output(sk);
				if (!sk->dead)
					sk->state_change(sk);
                }
                
        }

	dn_nsp_send_oth_ack(sk);

out:
	kfree_skb(skb);
}

/*
 * Copy of sock_queue_rcv_skb (from sock.h) with out
 * bh_lock_sock() (its already held when this is called) which
 * also allows data and other data to be queued to a socket.
 */
static __inline__ int dn_queue_skb(struct sock *sk, struct sk_buff *skb, int sig, struct sk_buff_head *queue)
{
#ifdef CONFIG_FILTER
	struct sk_filter *filter;
#endif
	unsigned long flags;

        /* Cast skb->rcvbuf to unsigned... It's pointless, but reduces
           number of warnings when compiling with -W --ANK
         */
        if (atomic_read(&sk->rmem_alloc) + skb->truesize >= (unsigned)sk->rcvbuf
)
                return -ENOMEM;

#ifdef CONFIG_FILTER
        if (sk->filter) {
		int err = 0;
                if ((filter = sk->filter) != NULL && sk_filter(skb, sk->filter))
                        err = -EPERM;  /* Toss packet */
		if (err)
			return err;
        }
#endif /* CONFIG_FILTER */

        skb_set_owner_r(skb, sk);
        skb_queue_tail(queue, skb);

	read_lock_irqsave(&sk->callback_lock, flags);
        if (!sk->dead) {
		struct socket *sock = sk->socket;
		wake_up_interruptible(sk->sleep);
		if (!(sock->flags & SO_WAITDATA) && sock->fasync_list)
			kill_fasync(sock->fasync_list, sig, 
				    (sig == SIGURG) ? POLL_PRI : POLL_IN);
	}
	read_unlock_irqrestore(&sk->callback_lock, flags);

        return 0;
}

static void dn_nsp_otherdata(struct sock *sk, struct sk_buff *skb)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	unsigned short segnum;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	int queued = 0;

	if (skb->len < 2)
		goto out;

	cb->segnum = segnum = dn_ntohs(*(__u16 *)skb->data);
	skb_pull(skb, 2);

	if (((sk->protinfo.dn.numoth_rcv + 1) & 0x0fff) == (segnum & 0x0fff)) {

		if (dn_queue_skb(sk, skb, SIGURG, &scp->other_receive_queue) == 0) {
			sk->protinfo.dn.numoth_rcv++;
			scp->other_report = 0;
			queued = 1;
		}
	}

	dn_nsp_send_oth_ack(sk);
out:
	if (!queued)
		kfree_skb(skb);
}

static void dn_nsp_data(struct sock *sk, struct sk_buff *skb)
{
	int queued = 0;
	unsigned short segnum;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct dn_scp *scp = &sk->protinfo.dn;

	if (skb->len < 2)
		goto out;

	cb->segnum = segnum = dn_ntohs(*(__u16 *)skb->data);
	skb_pull(skb, 2);

	if (((sk->protinfo.dn.numdat_rcv + 1) & 0x0FFF) == 
                     (segnum & 0x0FFF)) {

                if (dn_queue_skb(sk, skb, SIGIO, &sk->receive_queue) == 0) {
			sk->protinfo.dn.numdat_rcv++;
                	queued = 1;
                }

		if ((scp->flowloc_sw == DN_SEND) && dn_congested(sk)) {
			scp->flowloc_sw = DN_DONTSEND;
			dn_nsp_send_lnk(sk, DN_DONTSEND);
		}
        }

	dn_nsp_send_data_ack(sk);
out:
	if (!queued)
		kfree_skb(skb);
}

/*
 * If one of our conninit messages is returned, this function
 * deals with it. It puts the socket into the NO_COMMUNICATION
 * state.
 */
static void dn_returned_conn_init(struct sock *sk, struct sk_buff *skb)
{
	struct dn_scp *scp = &sk->protinfo.dn;

	if (scp->state == DN_CI) {
		scp->state = DN_NC;
		sk->state = TCP_CLOSE;
		if (!sk->dead)
			sk->state_change(sk);
	}

	kfree_skb(skb);
}

static int dn_nsp_rx_packet(struct sk_buff *skb)
{
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;
	struct sock *sk = NULL;
	unsigned char *ptr = (unsigned char *)skb->data;

	skb->h.raw    = skb->data;
	cb->nsp_flags = *ptr++;

	if (decnet_debug_level & 2)
		printk(KERN_DEBUG "dn_nsp_rx: Message type 0x%02x\n", (int)cb->nsp_flags);

#ifdef CONFIG_DECNET_RAW
	dn_raw_rx_nsp(skb);
#endif /* CONFIG_DECNET_RAW */

	if (skb->len < 2) 
		goto free_out;

	if (cb->nsp_flags & 0x83) 
		goto free_out;

	/*
	 * Returned packets...
	 * Swap src & dst and look up in the normal way.
	 */
	if (cb->rt_flags & DN_RT_F_RTS) {
		unsigned short tmp = cb->dst_port;
		cb->dst_port = cb->src_port;
		cb->src_port = tmp;
		tmp = cb->dst;
		cb->dst = cb->src;
		cb->src = tmp;
		sk = dn_find_by_skb(skb);
		goto got_it;
	}

	/*
	 * Filter out conninits and useless packet types
	 */
	if ((cb->nsp_flags & 0x0c) == 0x08) {
		switch(cb->nsp_flags & 0x70) {
			case 0x00: /* NOP */
			case 0x70: /* Reserved */
			case 0x50: /* Reserved, Phase II node init */
				goto free_out;
			case 0x10:
			case 0x60:
				sk = dn_find_listener(skb);
				goto got_it;
		}
	}

	if (skb->len < 3)
		goto free_out;

	/*
	 * Grab the destination address.
	 */
	cb->dst_port = *(unsigned short *)ptr;
	cb->src_port = 0;
	ptr += 2;

	/*
	 * If not a connack, grab the source address too.
	 */
	if (skb->len >= 5) {
		cb->src_port = *(unsigned short *)ptr;
		ptr += 2;
		skb_pull(skb, 5);
	}

	/*
	 * Find the socket to which this skb is destined.
	 */
	sk = dn_find_by_skb(skb);
got_it:
	if (sk != NULL) {
		struct dn_scp *scp = &sk->protinfo.dn;
		int ret;
		/* printk(KERN_DEBUG "dn_nsp_rx: Found a socket\n"); */

		/* Reset backoff */
		scp->nsp_rxtshift = 0;

		bh_lock_sock(sk);
		ret = 0;
		if (sk->lock.users == 0)
			ret = dn_nsp_backlog_rcv(sk, skb);
		else
			sk_add_backlog(sk, skb);
		bh_unlock_sock(sk);
		sock_put(sk);

		return ret;
	}
	return 1;

free_out:
	kfree_skb(skb);
	return 0;
}

int dn_nsp_rx(struct sk_buff *skb)
{
	return NF_HOOK(PF_DECnet, NF_DN_LOCAL_IN, skb, skb->rx_dev, NULL, dn_nsp_rx_packet);
}

/*
 * This is the main receive routine for sockets. It is called
 * from the above when the socket is not busy, and also from
 * sock_release() when there is a backlog queued up.
 */
int dn_nsp_backlog_rcv(struct sock *sk, struct sk_buff *skb)
{
	struct dn_scp *scp = &sk->protinfo.dn;
	struct dn_skb_cb *cb = (struct dn_skb_cb *)skb->cb;

	if (cb->rt_flags & DN_RT_F_RTS) {
		dn_returned_conn_init(sk, skb);
		return 0;
	}

	/*
	 * Control packet.
	 */
	if ((cb->nsp_flags & 0x0c) == 0x08) {
		/* printk(KERN_DEBUG "control type\n"); */
		switch(cb->nsp_flags & 0x70) {
			case 0x10:
			case 0x60:
				dn_nsp_conn_init(sk, skb);
				break;
			case 0x20:
				dn_nsp_conn_conf(sk, skb);
				break;
			case 0x30:
				dn_nsp_disc_init(sk, skb);
				break;
			case 0x40:      
				dn_nsp_disc_conf(sk, skb);
				break;
		}

	} else if (cb->nsp_flags == 0x24) {
		/*
		 * Special for connacks, 'cos they don't have
		 * ack data or ack otherdata info.
		 */
		dn_nsp_conn_ack(sk, skb);
	} else {
		int other = 1;

		if ((cb->nsp_flags & 0x1c) == 0)
			other = 0;
		if (cb->nsp_flags == 0x04)
			other = 0;

		/*
		 * Read out ack data here, this applies equally
		 * to data, other data, link serivce and both
		 * ack data and ack otherdata.
		 */
		dn_process_ack(sk, skb, other);

		/*
		 * If we've some sort of data here then call a
		 * suitable routine for dealing with it, otherwise
		 * the packet is an ack and can be discarded. All
		 * data frames can also kick a CC socket into RUN.
		 */
		if ((cb->nsp_flags & 0x0c) == 0) {

			if ((scp->state == DN_CC) && !sk->dead) {
				scp->state = DN_RUN;
				sk->state = TCP_ESTABLISHED;
				sk->state_change(sk);
			}

			if (scp->state != DN_RUN)
				goto free_out;

			switch(cb->nsp_flags) {
				case 0x10: /* LS */
					dn_nsp_linkservice(sk, skb);
					break;
				case 0x30: /* OD */
					dn_nsp_otherdata(sk, skb);
					break;
				default:
					dn_nsp_data(sk, skb);
			}

		} else { /* Ack, chuck it out here */
free_out:
			kfree_skb(skb);
		}
	}

	return 0;
}

