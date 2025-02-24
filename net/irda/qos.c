/*********************************************************************
 *                
 *                
 * Filename:      qos.c
 * Version:       1.0
 * Description:   IrLAP QoS parameter negotiation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Tue Sep  9 00:00:26 1997
 * Modified at:   Tue Nov 16 09:50:19 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 * 
 *     This program is distributed in the hope that it will be useful,
 *     but WITHOUT ANY WARRANTY; without even the implied warranty of
 *     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *     GNU General Public License for more details.
 * 
 *     You should have received a copy of the GNU General Public License 
 *     along with this program; if not, write to the Free Software 
 *     Foundation, Inc., 59 Temple Place, Suite 330, Boston, 
 *     MA 02111-1307 USA
 *     
 ********************************************************************/

#include <linux/config.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/parameters.h>
#include <net/irda/qos.h>
#include <net/irda/irlap.h>
#ifdef CONFIG_IRDA_COMPRESSION
#include <net/irda/irlap_comp.h>
#include "../../drivers/net/zlib.h"

#define CI_BZIP2  27 /* Random pick */
#endif

static int irlap_param_baud_rate(void *instance, param_t *param, int get);
static int irlap_param_link_disconnect(void *instance, param_t *parm, int get);
static int irlap_param_max_turn_time(void *instance, param_t *param, int get);
static int irlap_param_data_size(void *instance, param_t *param, int get);
static int irlap_param_window_size(void *instance, param_t *param, int get);
static int irlap_param_additional_bofs(void *instance, param_t *parm, int get);
static int irlap_param_min_turn_time(void *instance, param_t *param, int get);

__u32 min_turn_time[]  = { 10000, 5000, 1000, 500, 100, 50, 10, 0 }; /* us */
__u32 baud_rates[]     = { 2400, 9600, 19200, 38400, 57600, 115200, 576000, 
			   1152000, 4000000, 16000000 };            /* bps */
__u32 data_size[]      = { 64, 128, 256, 512, 1024, 2048 };         /* bytes */
__u32 add_bofs[]       = { 48, 24, 12, 5, 3, 2, 1, 0 };             /* bytes */
__u32 max_turn_time[]  = { 500, 250, 100, 50 };                     /* ms */
__u32 link_disc_time[] = { 3, 8, 12, 16, 20, 25, 30, 40 };          /* secs */

#ifdef CONFIG_IRDA_COMPRESSION
__u32 compression[] = { CI_BZIP2, CI_DEFLATE, CI_DEFLATE_DRAFT };
#endif

static pi_minor_info_t pi_minor_call_table_type_0[] = {
	{ NULL, 0 },
/* 01 */{ irlap_param_baud_rate,       PV_INTEGER | PV_LITTLE_ENDIAN },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
	{ NULL, 0 },
/* 08 */{ irlap_param_link_disconnect, PV_INT_8_BITS }
};

static pi_minor_info_t pi_minor_call_table_type_1[] = {
	{ NULL, 0 },
	{ NULL, 0 },
/* 82 */{ irlap_param_max_turn_time,   PV_INT_8_BITS },
/* 83 */{ irlap_param_data_size,       PV_INT_8_BITS },
/* 84 */{ irlap_param_window_size,     PV_INT_8_BITS },
/* 85 */{ irlap_param_additional_bofs, PV_INT_8_BITS },
/* 86 */{ irlap_param_min_turn_time,   PV_INT_8_BITS },
};

static pi_major_info_t pi_major_call_table[] = {
	{ pi_minor_call_table_type_0, 9 },
	{ pi_minor_call_table_type_1, 7 },
};

static pi_param_info_t irlap_param_info = { pi_major_call_table, 2, 0x7f, 7 };

/*
 * Function irda_qos_compute_intersection (qos, new)
 *
 *    Compute the intersection of the old QoS capabilites with new ones
 *
 */
void irda_qos_compute_intersection(struct qos_info *qos, struct qos_info *new)
{
	ASSERT(qos != NULL, return;);
	ASSERT(new != NULL, return;);

	/* Apply */
	qos->baud_rate.bits       &= new->baud_rate.bits;
	qos->window_size.bits     &= new->window_size.bits;
	qos->min_turn_time.bits   &= new->min_turn_time.bits;
	qos->max_turn_time.bits   &= new->max_turn_time.bits;
	qos->data_size.bits       &= new->data_size.bits;
	qos->link_disc_time.bits  &= new->link_disc_time.bits;
	qos->additional_bofs.bits &= new->additional_bofs.bits;

#ifdef CONFIG_IRDA_COMPRESSION
	qos->compression.bits     &= new->compression.bits;
#endif

	irda_qos_bits_to_value(qos);
}

/*
 * Function irda_init_max_qos_capabilies (qos)
 *
 *    The purpose of this function is for layers and drivers to be able to
 *    set the maximum QoS possible and then "and in" their own limitations
 * 
 */
void irda_init_max_qos_capabilies(struct qos_info *qos)
{
	/* 
	 *  These are the maximum supported values as specified on pages
	 *  39-43 in IrLAP
	 */

	/* LSB is first byte, MSB is second byte */
	qos->baud_rate.bits     = 0x01ff; 

	qos->window_size.bits     = 0x7f;
	qos->min_turn_time.bits   = 0xff;
	qos->max_turn_time.bits   = 0x0f;
	qos->data_size.bits       = 0x3f;
	qos->link_disc_time.bits  = 0xff;
	qos->additional_bofs.bits = 0xff;

#ifdef CONFIG_IRDA_COMPRESSION	
	qos->compression.bits     = 0x03;
#endif
}

/*
 * Function irlap_negotiate (qos_device, qos_session, skb)
 *
 *    Negotiate QoS values, not really that much negotiation :-)
 *    We just set the QoS capabilities for the peer station
 *
 */
int irlap_qos_negotiate(struct irlap_cb *self, struct sk_buff *skb) 
{
	int ret;
#ifdef CONFIG_IRDA_COMPRESSION
	int comp_seen = FALSE;
#endif
	ret = irda_param_extract_all(self, skb->data, skb->len, 
				     &irlap_param_info);
	
#ifdef CONFIG_IRDA_COMPRESSION
	if (!comp_seen) {
		IRDA_DEBUG( 4, __FUNCTION__ "(), Compression not seen!\n");
		self->qos_tx.compression.bits = 0x00;
		self->qos_rx.compression.bits = 0x00;
	}
#endif

	/* Convert the negotiated bits to values */
	irda_qos_bits_to_value(&self->qos_tx);
	irda_qos_bits_to_value(&self->qos_rx);
		
	IRDA_DEBUG(2, "Setting BAUD_RATE to %d bps.\n", 
	      self->qos_tx.baud_rate.value);
	IRDA_DEBUG(2, "Setting DATA_SIZE to %d bytes\n",
	      self->qos_tx.data_size.value);
	IRDA_DEBUG(2, "Setting WINDOW_SIZE to %d\n", 
	      self->qos_tx.window_size.value);
	IRDA_DEBUG(2, "Setting XBOFS to %d\n", 
	      self->qos_tx.additional_bofs.value);
	IRDA_DEBUG(2, "Setting MAX_TURN_TIME to %d ms.\n",
	      self->qos_tx.max_turn_time.value);
	IRDA_DEBUG(2, "Setting MIN_TURN_TIME to %d usecs.\n",
	      self->qos_tx.min_turn_time.value);
	IRDA_DEBUG(2, "Setting LINK_DISC to %d secs.\n", 
	      self->qos_tx.link_disc_time.value);
#ifdef CONFIG_IRDA_COMPRESSION
	IRDA_DEBUG(2, "Setting COMPRESSION to %d\n", 
	      self->qos_tx.compression.value);
#endif	
	return ret;
}

/*
 * Function irlap_insert_negotiation_params (qos, fp)
 *
 *    Insert QoS negotiaion pararameters into frame
 *
 */
int irlap_insert_qos_negotiation_params(struct irlap_cb *self, 
					struct sk_buff *skb)
{
	int ret;

	/* Insert data rate */
	ret = irda_param_insert(self, PI_BAUD_RATE, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert max turnaround time */
	ret = irda_param_insert(self, PI_MAX_TURN_TIME, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert data size */
	ret = irda_param_insert(self, PI_DATA_SIZE, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert window size */
	ret = irda_param_insert(self, PI_WINDOW_SIZE, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert additional BOFs */
	ret = irda_param_insert(self, PI_ADD_BOFS, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert minimum turnaround time */
	ret = irda_param_insert(self, PI_MIN_TURN_TIME, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	/* Insert link disconnect/threshold time */
	ret = irda_param_insert(self, PI_LINK_DISC, skb->tail, 
				skb_tailroom(skb), &irlap_param_info);
	if (ret < 0)
		return ret;
	skb_put(skb, ret);

	return 0;
}

/*
 * Function irlap_param_baud_rate (instance, param, get)
 *
 *    Negotiate data-rate
 *
 */
static int irlap_param_baud_rate(void *instance, param_t *param, int get)
{
	__u16 final;

	struct irlap_cb *self = (struct irlap_cb *) instance;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);

	if (get) {
		param->pv.i = self->qos_rx.baud_rate.bits;
		IRDA_DEBUG(2, __FUNCTION__ "(), baud rate = 0x%02x\n", param->pv.i);		
	} else {
		/* 
		 *  Stations must agree on baud rate, so calculate
		 *  intersection 
		 */
		IRDA_DEBUG(2, "Requested BAUD_RATE: 0x%04x\n", param->pv.s);
		final = param->pv.s & self->qos_rx.baud_rate.bits;

		IRDA_DEBUG(2, "Final BAUD_RATE: 0x%04x\n", final);
		self->qos_tx.baud_rate.bits = final;
		self->qos_rx.baud_rate.bits = final;
	}

	return 0;
}

/*
 * Function irlap_param_link_disconnect (instance, param, get)
 *
 *    Negotiate link disconnect/threshold time. 
 *
 */
static int irlap_param_link_disconnect(void *instance, param_t *param, int get)
{
	__u16 final;
	
	struct irlap_cb *self = (struct irlap_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);
	
	if (get)
		param->pv.b = self->qos_rx.link_disc_time.bits;
	else {
		/*  
		 *  Stations must agree on link disconnect/threshold 
		 *  time.
		 */
		IRDA_DEBUG(2, "LINK_DISC: %02x\n", param->pv.b);
		final = param->pv.b & self->qos_rx.link_disc_time.bits;

		IRDA_DEBUG(2, "Final LINK_DISC: %02x\n", final);
		self->qos_tx.link_disc_time.bits = final;
		self->qos_rx.link_disc_time.bits = final;
	}
	return 0;
}

/*
 * Function irlap_param_max_turn_time (instance, param, get)
 *
 *    Negotiate the maximum turnaround time. This is a type 1 parameter and
 *    will be negotiated independently for each station
 *
 */
static int irlap_param_max_turn_time(void *instance, param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);
	
	if (get)
		param->pv.b = self->qos_rx.max_turn_time.bits;
	else
		self->qos_tx.max_turn_time.bits = param->pv.b;

	return 0;
}

/*
 * Function irlap_param_data_size (instance, param, get)
 *
 *    Negotiate the data size. This is a type 1 parameter and
 *    will be negotiated independently for each station
 *
 */
static int irlap_param_data_size(void *instance, param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);
	
	if (get)
		param->pv.b = self->qos_rx.data_size.bits;
	else
		self->qos_tx.data_size.bits = param->pv.b;

	return 0;
}

/*
 * Function irlap_param_window_size (instance, param, get)
 *
 *    Negotiate the window size. This is a type 1 parameter and
 *    will be negotiated independently for each station
 *
 */
static int irlap_param_window_size(void *instance, param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);
	
	if (get)
		param->pv.b = self->qos_rx.window_size.bits;
	else
		self->qos_tx.window_size.bits = param->pv.b;

	return 0;
}

/*
 * Function irlap_param_additional_bofs (instance, param, get)
 *
 *    Negotiate additional BOF characters. This is a type 1 parameter and
 *    will be negotiated independently for each station.
 */
static int irlap_param_additional_bofs(void *instance, param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);
	
	if (get)
		param->pv.b = self->qos_rx.additional_bofs.bits;
	else
		self->qos_tx.additional_bofs.bits = param->pv.b;

	return 0;
}

/*
 * Function irlap_param_min_turn_time (instance, param, get)
 *
 *    Negotiate the minimum turn around time. This is a type 1 parameter and
 *    will be negotiated independently for each station
 */
static int irlap_param_min_turn_time(void *instance, param_t *param, int get)
{
	struct irlap_cb *self = (struct irlap_cb *) instance;
	
	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == LAP_MAGIC, return -1;);
	
	if (get)
		param->pv.b = self->qos_rx.min_turn_time.bits;
	else
		self->qos_tx.min_turn_time.bits = param->pv.b;

	return 0;
}

__u32 byte_value(__u8 byte, __u32 *array) 
{
	int index;

	ASSERT(array != NULL, return -1;);

	index = msb_index(byte);

	return index_value(index, array);
}

/*
 * Function msb_index (word)
 *
 *    Returns index to most significant bit (MSB) in word
 *
 */
int msb_index (__u16 word) 
{
	__u16 msb = 0x8000;
	int index = 15;   /* Current MSB */
	
	while (msb) {
		if (word & msb)
			break;   /* Found it! */
		msb >>=1;
		index--;
	}
	
	return index;
}

/*
 * Function value_index (value, array)
 *
 *    Returns the index to the value in the specified array
 */
int value_index(__u32 value, __u32 *array) 
{
	int i;
	
	for (i=0;i<8;i++)
		if (array[i] == value)
			break;
	return i;
}

/*
 * Function index_value (index, array)
 *
 *    Returns value to index in array, easy!
 *
 */
__u32 index_value(int index, __u32 *array) 
{
	return array[index];
}

void irda_qos_bits_to_value(struct qos_info *qos)
{
	int index;

	ASSERT(qos != NULL, return;);
	
	index = msb_index(qos->baud_rate.bits);
	qos->baud_rate.value = baud_rates[index];

	index = msb_index(qos->data_size.bits);
	qos->data_size.value = data_size[index];

	index = msb_index(qos->window_size.bits);
	qos->window_size.value = index+1;

	index = msb_index(qos->min_turn_time.bits);
	qos->min_turn_time.value = min_turn_time[index];
	
	index = msb_index(qos->max_turn_time.bits);
	qos->max_turn_time.value = max_turn_time[index];

	index = msb_index(qos->link_disc_time.bits);
	qos->link_disc_time.value = link_disc_time[index];
	
	index = msb_index(qos->additional_bofs.bits);
	qos->additional_bofs.value = add_bofs[index];

#ifdef CONFIG_IRDA_COMPRESSION
	index = msb_index(qos->compression.bits);
	if (index >= 0)
		qos->compression.value = compression[index];
	else 
		qos->compression.value = 0;
#endif
}







