/*
 *	AX.25 release 037
 *
 *	This code REQUIRES 2.1.15 or higher/ NET3.038
 *
 *	This module:
 *		This module is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Most of this code is based on the SDL diagrams published in the 7th
 *	ARRL Computer Networking Conference papers. The diagrams have mistakes
 *	in them, but are mostly correct. Before you modify the code could you
 *	read the SDL diagrams as the code is not obvious and probably very
 *	easy to break;
 *
 *	History
 *	AX.25 028a	Jonathan(G4KLX)	New state machine based on SDL diagrams.
 *	AX.25 029	Alan(GW4PTS)	Switched to KA9Q constant names.
 *			Jonathan(G4KLX)	Only poll when window is full.
 *	AX.25 030	Jonathan(G4KLX)	Added fragmentation to ax25_output.
 *					Added support for extended AX.25.
 *	AX.25 031	Joerg(DL1BKE)	Added DAMA support
 *			Joerg(DL1BKE)	Modified fragmenter to fragment vanilla 
 *					AX.25 I-Frames. Added PACLEN parameter.
 *			Joerg(DL1BKE)	Fixed a problem with buffer allocation
 *					for fragments.
 *	AX.25 037	Jonathan(G4KLX)	New timer architecture.
 *			Joerg(DL1BKE)	Fixed DAMA Slave mode: will work
 *					on non-DAMA interfaces like AX25L2V2
 *					again (this behaviour is _required_).
 *			Joerg(DL1BKE)	ax25_check_iframes_acked() returns a 
 *					value now (for DAMA n2count handling)
 */

#include <linux/config.h>
#if defined(CONFIG_AX25) || defined(CONFIG_AX25_MODULE)
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
#include <net/ax25.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/netfilter.h>
#include <net/sock.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <linux/fcntl.h>
#include <linux/mm.h>
#include <linux/interrupt.h>

ax25_cb *ax25_send_frame(struct sk_buff *skb, int paclen, ax25_address *src, ax25_address *dest, ax25_digi *digi, struct net_device *dev)
{
	ax25_dev *ax25_dev;
	ax25_cb *ax25;

	/*
	 * Take the default packet length for the device if zero is
	 * specified.
	 */
	if (paclen == 0) {
		if ((ax25_dev = ax25_dev_ax25dev(dev)) == NULL)
			return NULL;

		paclen = ax25_dev->values[AX25_VALUES_PACLEN];
	}

	/*
	 * Look for an existing connection.
	 */
	if ((ax25 = ax25_find_cb(src, dest, digi, dev)) != NULL) {
		ax25_output(ax25, paclen, skb);
		return ax25;		/* It already existed */
	}

	if ((ax25_dev = ax25_dev_ax25dev(dev)) == NULL)
		return NULL;

	if ((ax25 = ax25_create_cb()) == NULL)
		return NULL;

	ax25_fillin_cb(ax25, ax25_dev);

	ax25->source_addr = *src;
	ax25->dest_addr   = *dest;

	if (digi != NULL) {
		if ((ax25->digipeat = kmalloc(sizeof(ax25_digi), GFP_ATOMIC)) == NULL) {
			ax25_free_cb(ax25);
			return NULL;
		}
		*ax25->digipeat = *digi;
	}

	switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
		case AX25_PROTO_STD_SIMPLEX:
		case AX25_PROTO_STD_DUPLEX:
			ax25_std_establish_data_link(ax25);
			break;

#ifdef CONFIG_AX25_DAMA_SLAVE
		case AX25_PROTO_DAMA_SLAVE:
			if (ax25_dev->dama.slave)
				ax25_ds_establish_data_link(ax25);
			else
				ax25_std_establish_data_link(ax25);
			break;
#endif
	}

	ax25_insert_socket(ax25);

	ax25->state = AX25_STATE_1;

	ax25_start_heartbeat(ax25);

	ax25_output(ax25, paclen, skb);

	return ax25;			/* We had to create it */
}

/*
 *	All outgoing AX.25 I frames pass via this routine. Therefore this is
 *	where the fragmentation of frames takes place. If fragment is set to
 *	zero then we are not allowed to do fragmentation, even if the frame
 *	is too large.
 */
void ax25_output(ax25_cb *ax25, int paclen, struct sk_buff *skb)
{
	struct sk_buff *skbn;
	unsigned char *p;
	int frontlen, len, fragno, ka9qfrag, first = 1;
	long flags;

	if ((skb->len - 1) > paclen) {
		if (*skb->data == AX25_P_TEXT) {
			skb_pull(skb, 1); /* skip PID */
			ka9qfrag = 0;
		} else {
			paclen -= 2;	/* Allow for fragment control info */
			ka9qfrag = 1;
		}

		fragno = skb->len / paclen;
		if (skb->len % paclen == 0) fragno--;

		frontlen = skb_headroom(skb);	/* Address space + CTRL */

		while (skb->len > 0) {
			save_flags(flags); 
			cli();

			if ((skbn = alloc_skb(paclen + 2 + frontlen, GFP_ATOMIC)) == NULL) {
				restore_flags(flags);
				printk(KERN_CRIT "AX.25: ax25_output - out of memory\n");
				return;
			}

			if (skb->sk != NULL)
				skb_set_owner_w(skbn, skb->sk);
			
			restore_flags(flags);
			
			len = (paclen > skb->len) ? skb->len : paclen;

			if (ka9qfrag == 1) {
				skb_reserve(skbn, frontlen + 2);

				memcpy(skb_put(skbn, len), skb->data, len);
				p = skb_push(skbn, 2);

				*p++ = AX25_P_SEGMENT;

				*p = fragno--;
				if (first) {
					*p |= AX25_SEG_FIRST;
					first = 0;
				}
			} else {
				skb_reserve(skbn, frontlen + 1);
				memcpy(skb_put(skbn, len), skb->data, len);
				p = skb_push(skbn, 1);
				*p = AX25_P_TEXT;
			}

			skb_pull(skb, len);
			skb_queue_tail(&ax25->write_queue, skbn); /* Throw it on the queue */
		}

		kfree_skb(skb);
	} else {
		skb_queue_tail(&ax25->write_queue, skb);	  /* Throw it on the queue */
	}

	switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
		case AX25_PROTO_STD_SIMPLEX:
		case AX25_PROTO_STD_DUPLEX:
			ax25_kick(ax25);
			break;

#ifdef CONFIG_AX25_DAMA_SLAVE
		/* 
		 * A DAMA slave is _required_ to work as normal AX.25L2V2
		 * if no DAMA master is available.
		 */
		case AX25_PROTO_DAMA_SLAVE:
			if (!ax25->ax25_dev->dama.slave) ax25_kick(ax25);
			break;
#endif
	}
}

/* 
 *  This procedure is passed a buffer descriptor for an iframe. It builds
 *  the rest of the control part of the frame and then writes it out.
 */
static void ax25_send_iframe(ax25_cb *ax25, struct sk_buff *skb, int poll_bit)
{
	unsigned char *frame;

	if (skb == NULL)
		return;

	skb->nh.raw = skb->data;

	if (ax25->modulus == AX25_MODULUS) {
		frame = skb_push(skb, 1);

		*frame = AX25_I;
		*frame |= (poll_bit) ? AX25_PF : 0;
		*frame |= (ax25->vr << 5);
		*frame |= (ax25->vs << 1);
	} else {
		frame = skb_push(skb, 2);

		frame[0] = AX25_I;
		frame[0] |= (ax25->vs << 1);
		frame[1] = (poll_bit) ? AX25_EPF : 0;
		frame[1] |= (ax25->vr << 1);
	}

	ax25_start_idletimer(ax25);

	ax25_transmit_buffer(ax25, skb, AX25_COMMAND);
}

void ax25_kick(ax25_cb *ax25)
{
	struct sk_buff *skb, *skbn;
	int last = 1;
	unsigned short start, end, next;

	if (ax25->state != AX25_STATE_3 && ax25->state != AX25_STATE_4)
		return;

	if (ax25->condition & AX25_COND_PEER_RX_BUSY)
		return;

	if (skb_peek(&ax25->write_queue) == NULL)
		return;

	start = (skb_peek(&ax25->ack_queue) == NULL) ? ax25->va : ax25->vs;
	end   = (ax25->va + ax25->window) % ax25->modulus;

	if (start == end)
		return;

	ax25->vs = start;

	/*
	 * Transmit data until either we're out of data to send or
	 * the window is full. Send a poll on the final I frame if
	 * the window is filled.
	 */

	/*
	 * Dequeue the frame and copy it.
	 */
	skb  = skb_dequeue(&ax25->write_queue);

	do {
		if ((skbn = skb_clone(skb, GFP_ATOMIC)) == NULL) {
			skb_queue_head(&ax25->write_queue, skb);
			break;
		}

		if (skb->sk != NULL)
			skb_set_owner_w(skbn, skb->sk);

		next = (ax25->vs + 1) % ax25->modulus;
		last = (next == end);

		/*
		 * Transmit the frame copy.
		 * bke 960114: do not set the Poll bit on the last frame
		 * in DAMA mode.
		 */
		switch (ax25->ax25_dev->values[AX25_VALUES_PROTOCOL]) {
			case AX25_PROTO_STD_SIMPLEX:
			case AX25_PROTO_STD_DUPLEX:
				ax25_send_iframe(ax25, skbn, (last) ? AX25_POLLON : AX25_POLLOFF);
				break;

#ifdef CONFIG_AX25_DAMA_SLAVE
			case AX25_PROTO_DAMA_SLAVE:
				ax25_send_iframe(ax25, skbn, AX25_POLLOFF);
				break;
#endif
		}

		ax25->vs = next;

		/*
		 * Requeue the original data frame.
		 */
		skb_queue_tail(&ax25->ack_queue, skb);

	} while (!last && (skb = skb_dequeue(&ax25->write_queue)) != NULL);

	ax25->condition &= ~AX25_COND_ACK_PENDING;

	if (!ax25_t1timer_running(ax25)) {
		ax25_stop_t3timer(ax25);
		ax25_calculate_t1(ax25);
		ax25_start_t1timer(ax25);
	}
}

void ax25_transmit_buffer(ax25_cb *ax25, struct sk_buff *skb, int type)
{
	struct sk_buff *skbn;
	unsigned char *ptr;
	int headroom;

	if (ax25->ax25_dev == NULL) {
		ax25_disconnect(ax25, ENETUNREACH);
		return;
	}

	headroom = ax25_addr_size(ax25->digipeat);

	if (skb_headroom(skb) < headroom) {
		if ((skbn = skb_realloc_headroom(skb, headroom)) == NULL) {
			printk(KERN_CRIT "AX.25: ax25_transmit_buffer - out of memory\n");
			kfree_skb(skb);
			return;
		}

		if (skb->sk != NULL)
			skb_set_owner_w(skbn, skb->sk);

		kfree_skb(skb);
		skb = skbn;
	}

	ptr = skb_push(skb, headroom);

	ax25_addr_build(ptr, &ax25->source_addr, &ax25->dest_addr, ax25->digipeat, type, ax25->modulus);

	skb->dev = ax25->ax25_dev->dev;

	ax25_queue_xmit(skb);
}

/*
 *	A small shim to dev_queue_xmit to add the KISS control byte, and do
 *	any packet forwarding in operation.
 */
void ax25_queue_xmit(struct sk_buff *skb)
{
	unsigned char *ptr;

	skb->protocol = htons(ETH_P_AX25);
	skb->dev      = ax25_fwd_dev(skb->dev);

	ptr  = skb_push(skb, 1);
	*ptr = 0x00;			/* KISS */

	dev_queue_xmit(skb);
}

int ax25_check_iframes_acked(ax25_cb *ax25, unsigned short nr)
{
	if (ax25->vs == nr) {
		ax25_frames_acked(ax25, nr);
		ax25_calculate_rtt(ax25);
		ax25_stop_t1timer(ax25);
		ax25_start_t3timer(ax25);
		return 1;
	} else {
		if (ax25->va != nr) {
			ax25_frames_acked(ax25, nr);
			ax25_calculate_t1(ax25);
			ax25_start_t1timer(ax25);
			return 1;
		}
	}
	return 0;
}

#endif
