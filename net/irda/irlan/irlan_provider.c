/*********************************************************************
 *                
 * Filename:      irlan_provider.c
 * Version:       0.9
 * Description:   IrDA LAN Access Protocol Implementation
 * Status:        Experimental.
 * Author:        Dag Brattli <dagb@cs.uit.no>
 * Created at:    Sun Aug 31 20:14:37 1997
 * Modified at:   Sat Oct 30 12:52:10 1999
 * Modified by:   Dag Brattli <dagb@cs.uit.no>
 * Sources:       skeleton.c by Donald Becker <becker@CESDIS.gsfc.nasa.gov>
 *                slip.c by Laurence Culhane,   <loz@holmes.demon.co.uk>
 *                          Fred N. van Kempen, <waltje@uwalt.nl.mugnet.org>
 * 
 *     Copyright (c) 1998-1999 Dag Brattli <dagb@cs.uit.no>, 
 *     All Rights Reserved.
 *     
 *     This program is free software; you can redistribute it and/or 
 *     modify it under the terms of the GNU General Public License as 
 *     published by the Free Software Foundation; either version 2 of 
 *     the License, or (at your option) any later version.
 *
 *     Neither Dag Brattli nor University of Troms� admit liability nor
 *     provide warranty for any of this software. This material is 
 *     provided "AS-IS" and at no charge.
 *
 ********************************************************************/

#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/init.h>
#include <linux/random.h>

#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/byteorder.h>

#include <net/irda/irda.h>
#include <net/irda/irttp.h>
#include <net/irda/irlmp.h>
#include <net/irda/irias_object.h>
#include <net/irda/iriap.h>
#include <net/irda/timer.h>

#include <net/irda/irlan_common.h>
#include <net/irda/irlan_eth.h>
#include <net/irda/irlan_event.h>
#include <net/irda/irlan_provider.h>
#include <net/irda/irlan_filter.h>
#include <net/irda/irlan_client.h>

static void irlan_provider_connect_indication(void *instance, void *sap, 
					      struct qos_info *qos, 
					      __u32 max_sdu_size,
					      __u8 max_header_size,
					      struct sk_buff *skb);

/*
 * Function irlan_provider_control_data_indication (handle, skb)
 *
 *    This function gets the data that is received on the control channel
 *
 */
static int irlan_provider_data_indication(void *instance, void *sap, 
					  struct sk_buff *skb) 
{
	struct irlan_cb *self;
	__u8 code;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) instance;

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRLAN_MAGIC, return -1;);

	ASSERT(skb != NULL, return -1;);

	code = skb->data[0];
	switch(code) {
	case CMD_GET_PROVIDER_INFO:
		IRDA_DEBUG(4, "Got GET_PROVIDER_INFO command!\n");
		irlan_do_provider_event(self, IRLAN_GET_INFO_CMD, skb); 
		break;

	case CMD_GET_MEDIA_CHAR:
		IRDA_DEBUG(4, "Got GET_MEDIA_CHAR command!\n");
		irlan_do_provider_event(self, IRLAN_GET_MEDIA_CMD, skb); 
		break;
	case CMD_OPEN_DATA_CHANNEL:
		IRDA_DEBUG(4, "Got OPEN_DATA_CHANNEL command!\n");
		irlan_do_provider_event(self, IRLAN_OPEN_DATA_CMD, skb); 
		break;
	case CMD_FILTER_OPERATION:
		IRDA_DEBUG(4, "Got FILTER_OPERATION command!\n");
		irlan_do_provider_event(self, IRLAN_FILTER_CONFIG_CMD, skb);
		break;
	case CMD_RECONNECT_DATA_CHAN:
		IRDA_DEBUG(2, __FUNCTION__"(), Got RECONNECT_DATA_CHAN command\n");
		IRDA_DEBUG(2, __FUNCTION__"(), NOT IMPLEMENTED\n");
		break;
	case CMD_CLOSE_DATA_CHAN:
		IRDA_DEBUG(2, "Got CLOSE_DATA_CHAN command!\n");
		IRDA_DEBUG(2, __FUNCTION__"(), NOT IMPLEMENTED\n");
		break;
	default:
		IRDA_DEBUG(2, __FUNCTION__ "(), Unknown command!\n");
		break;
	}
	return 0;
}

/*
 * Function irlan_provider_connect_indication (handle, skb, priv)
 *
 *    Got connection from peer IrLAN layer
 *
 */
static void irlan_provider_connect_indication(void *instance, void *sap, 
					      struct qos_info *qos,
					      __u32 max_sdu_size, 
					      __u8 max_header_size,
					       struct sk_buff *skb)
{
	struct irlan_cb *self, *new;
	struct tsap_cb *tsap;
	__u32 saddr, daddr;

	IRDA_DEBUG(0, __FUNCTION__ "()\n");
	
	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;
	
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);
	
	self->provider.max_sdu_size = max_sdu_size;
	self->provider.max_header_size = max_header_size;

	ASSERT(tsap == self->provider.tsap_ctrl,return;);
	ASSERT(self->provider.state == IRLAN_IDLE, return;);

	daddr = irttp_get_daddr(tsap);
	saddr = irttp_get_saddr(tsap);

	/* Check if we already dealing with this client or peer */
	new = (struct irlan_cb *) hashbin_find(irlan, daddr, NULL);
      	if (new) {
		ASSERT(new->magic == IRLAN_MAGIC, return;);
		IRDA_DEBUG(0, __FUNCTION__ "(), found instance!\n");

		/* Update saddr, since client may have moved to a new link */
		new->saddr = saddr;
		IRDA_DEBUG(2, __FUNCTION__ "(), saddr=%08x\n", new->saddr);

		/* Make sure that any old provider control TSAP is removed */
		if ((new != self) && new->provider.tsap_ctrl) {
			irttp_disconnect_request(new->provider.tsap_ctrl, 
						 NULL, P_NORMAL);
			irttp_close_tsap(new->provider.tsap_ctrl);
			new->provider.tsap_ctrl = NULL;
		}
	} else {
		/* This must be the master instance, so start a new instance */
		IRDA_DEBUG(0, __FUNCTION__ "(), starting new provider!\n");

		new = irlan_open(saddr, daddr, TRUE); 
	}

	/*  
	 * Check if the connection came in on the master server, or the
	 * slave server. If it came on the slave, then everything is
	 * really, OK (reconnect), if not we need to dup the connection and
	 * hand it over to the slave.  
	 */
	if (new != self) {
				
		/* Now attach up the new "socket" */
		new->provider.tsap_ctrl = irttp_dup(self->provider.tsap_ctrl, 
						    new);
		if (!new->provider.tsap_ctrl) {
			IRDA_DEBUG(0, __FUNCTION__ "(), dup failed!\n");
			return;
		}
		
		/* new->stsap_sel = new->tsap->stsap_sel; */
		new->dtsap_sel_ctrl = new->provider.tsap_ctrl->dtsap_sel;

		/* Clean up the original one to keep it in listen state */
		self->provider.tsap_ctrl->dtsap_sel = LSAP_ANY;
		self->provider.tsap_ctrl->lsap->dlsap_sel = LSAP_ANY;
		self->provider.tsap_ctrl->lsap->lsap_state = LSAP_DISCONNECTED;
		
		/* 
		 * Use the new instance from here instead of the master
		 * struct! 
		 */
		self = new;
	}
	/* Check if network device has been registered */
	if (!self->netdev_registered)
		irlan_register_netdev(self);
	
	irlan_do_provider_event(self, IRLAN_CONNECT_INDICATION, NULL);

	/*  
	 * If we are in peer mode, the client may not have got the discovery
	 * indication it needs to make progress. If the client is still in 
	 * IDLE state, we must kick it to 
	 */
	if ((self->provider.access_type == ACCESS_PEER) && 
	    (self->client.state == IRLAN_IDLE)) {
		irlan_client_wakeup(self, self->saddr, self->daddr);
	}
}

/*
 * Function irlan_provider_connect_response (handle)
 *
 *    Accept incomming connection
 *
 */
void irlan_provider_connect_response(struct irlan_cb *self,
				     struct tsap_cb *tsap)
{
	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	/* Just accept */
	irttp_connect_response(tsap, IRLAN_MTU, NULL);

	/* Check if network device has been registered */
	if (!self->netdev_registered)
		irlan_register_netdev(self);
		
}

void irlan_provider_disconnect_indication(void *instance, void *sap, 
					  LM_REASON reason, 
					  struct sk_buff *userdata) 
{
	struct irlan_cb *self;
	struct tsap_cb *tsap;

	IRDA_DEBUG(4, __FUNCTION__ "(), reason=%d\n", reason);
	
	self = (struct irlan_cb *) instance;
	tsap = (struct tsap_cb *) sap;

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);	
	ASSERT(tsap != NULL, return;);
	ASSERT(tsap->magic == TTP_TSAP_MAGIC, return;);
	
	ASSERT(tsap == self->provider.tsap_ctrl, return;);
	
	irlan_do_provider_event(self, IRLAN_LMP_DISCONNECT, NULL);
}

/*
 * Function irlan_parse_open_data_cmd (self, skb)
 *
 *    
 *
 */
int irlan_parse_open_data_cmd(struct irlan_cb *self, struct sk_buff *skb)
{
	int ret;
	
	ret = irlan_provider_parse_command(self, CMD_OPEN_DATA_CHANNEL, skb);

	/* Open data channel */
	irlan_open_data_tsap(self);

	return ret;
}

/*
 * Function parse_command (skb)
 *
 *    Extract all parameters from received buffer, then feed them to 
 *    check_params for parsing
 *
 */
int irlan_provider_parse_command(struct irlan_cb *self, int cmd,
				 struct sk_buff *skb) 
{
	__u8 *frame;
	__u8 *ptr;
	int count;
	__u16 val_len;
	int i;
	char *name;
        char *value;
	int ret = RSP_SUCCESS;
	
	ASSERT(skb != NULL, return -RSP_PROTOCOL_ERROR;);
	
	IRDA_DEBUG(4, __FUNCTION__ "(), skb->len=%d\n", (int)skb->len);

	ASSERT(self != NULL, return -RSP_PROTOCOL_ERROR;);
	ASSERT(self->magic == IRLAN_MAGIC, return -RSP_PROTOCOL_ERROR;);
	
	if (!skb)
		return -RSP_PROTOCOL_ERROR;

	frame = skb->data;

	name = kmalloc(255, GFP_ATOMIC);
	if (!name)
		return -RSP_INSUFFICIENT_RESOURCES;
	value = kmalloc(1016, GFP_ATOMIC);
	if (!value) {
		kfree(name);
		return -RSP_INSUFFICIENT_RESOURCES;
	}

	/* How many parameters? */
	count = frame[1];

	IRDA_DEBUG(4, "Got %d parameters\n", count);
	
	ptr = frame+2;
	
	/* For all parameters */
 	for (i=0; i<count;i++) {
		ret = irlan_extract_param(ptr, name, value, &val_len);
		if (ret < 0) {
			IRDA_DEBUG(2, __FUNCTION__ "(), IrLAN, Error!\n");
			break;
		}
		ptr+=ret;
		ret = RSP_SUCCESS;
		irlan_check_command_param(self, name, value);
	}
	/* Cleanup */
	kfree(name);
	kfree(value);

	return ret;
}

/*
 * Function irlan_provider_send_reply (self, info)
 *
 *    Send reply to query to peer IrLAN layer
 *
 */
void irlan_provider_send_reply(struct irlan_cb *self, int command, 
			       int ret_code)
{
	struct sk_buff *skb;

	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return;);
	ASSERT(self->magic == IRLAN_MAGIC, return;);

	skb = dev_alloc_skb(128);
	if (!skb)
		return;

	/* Reserve space for TTP, LMP, and LAP header */
	skb_reserve(skb, self->provider.max_header_size);
	skb_put(skb, 2);
       
	switch (command) {
	case CMD_GET_PROVIDER_INFO:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x02; /* 2 parameters */
		switch (self->media) {
		case MEDIA_802_3:
			irlan_insert_string_param(skb, "MEDIA", "802.3");
			break;
		case MEDIA_802_5:
			irlan_insert_string_param(skb, "MEDIA", "802.5");
			break;
		default:
			IRDA_DEBUG(2, __FUNCTION__ "(), unknown media type!\n");
			break;
		}
		irlan_insert_short_param(skb, "IRLAN_VER", 0x0101);
		break;

	case CMD_GET_MEDIA_CHAR:
		skb->data[0] = 0x00; /* Success */
		skb->data[1] = 0x05; /* 5 parameters */
		irlan_insert_string_param(skb, "FILTER_TYPE", "DIRECTED");
		irlan_insert_string_param(skb, "FILTER_TYPE", "BROADCAST");
		irlan_insert_string_param(skb, "FILTER_TYPE", "MULTICAST");

		switch (self->provider.access_type) {
		case ACCESS_DIRECT:
			irlan_insert_string_param(skb, "ACCESS_TYPE", "DIRECT");
			break;
		case ACCESS_PEER:
			irlan_insert_string_param(skb, "ACCESS_TYPE", "PEER");
			break;
		case ACCESS_HOSTED:
			irlan_insert_string_param(skb, "ACCESS_TYPE", "HOSTED");
			break;
		default:
			IRDA_DEBUG(2, __FUNCTION__ "(), Unknown access type\n");
			break;
		}
		irlan_insert_short_param(skb, "MAX_FRAME", 0x05ee);
		break;
	case CMD_OPEN_DATA_CHANNEL:
		skb->data[0] = 0x00; /* Success */
		if (self->provider.send_arb_val) {
			skb->data[1] = 0x03; /* 3 parameters */
			irlan_insert_short_param(skb, "CON_ARB", 
						 self->provider.send_arb_val);
		} else
			skb->data[1] = 0x02; /* 2 parameters */
		irlan_insert_byte_param(skb, "DATA_CHAN", self->stsap_sel_data);
		irlan_insert_array_param(skb, "RECONNECT_KEY", "LINUX RULES!",
					 12);
		break;
	case CMD_FILTER_OPERATION:
		handle_filter_request(self, skb);
		break;
	default:
		IRDA_DEBUG(2, __FUNCTION__ "(), Unknown command!\n");
		break;
	}

	irttp_data_request(self->provider.tsap_ctrl, skb);
}

/*
 * Function irlan_provider_register(void)
 *
 *    Register provider support so we can accept incomming connections.
 * 
 */
int irlan_provider_open_ctrl_tsap(struct irlan_cb *self)
{
	struct tsap_cb *tsap;
	notify_t notify;
	
	IRDA_DEBUG(4, __FUNCTION__ "()\n");

	ASSERT(self != NULL, return -1;);
	ASSERT(self->magic == IRLAN_MAGIC, return -1;);

	/* Check if already open */
	if (self->provider.tsap_ctrl)
		return -1;
	
	/*
	 *  First register well known control TSAP
	 */
	irda_notify_init(&notify);
	notify.data_indication       = irlan_provider_data_indication;
	notify.connect_indication    = irlan_provider_connect_indication;
	notify.disconnect_indication = irlan_provider_disconnect_indication;
	notify.instance = self;
	strncpy(notify.name, "IrLAN ctrl (p)", 16);

	tsap = irttp_open_tsap(LSAP_ANY, 1, &notify);
	if (!tsap) {
		IRDA_DEBUG(2, __FUNCTION__ "(), Got no tsap!\n");
		return -1;
	}
	self->provider.tsap_ctrl = tsap;

	/* Register with LM-IAS */
	irlan_ias_register(self, tsap->stsap_sel);

	return 0;
}

