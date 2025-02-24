/* $Id: eicon_idi.c,v 1.24 1999/10/26 21:15:33 armin Exp $
 *
 * ISDN lowlevel-module for Eicon.Diehl active cards.
 *        IDI interface 
 *
 * Copyright 1998,99 by Armin Schindler (mac@melware.de)
 * Copyright 1999    Cytronics & Melware (info@melware.de)
 *
 * Thanks to	Deutsche Mailbox Saar-Lor-Lux GmbH
 *		for sponsoring and testing fax
 *		capabilities with Diva Server cards.
 *		(dor@deutschemailbox.de)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 *
 * $Log: eicon_idi.c,v $
 * Revision 1.24  1999/10/26 21:15:33  armin
 * using define for checking phone number len to avoid buffer overflow.
 *
 * Revision 1.23  1999/10/11 18:13:25  armin
 * Added fax capabilities for Eicon Diva Server cards.
 *
 * Revision 1.22  1999/10/08 22:09:33  armin
 * Some fixes of cards interface handling.
 * Bugfix of NULL pointer occurence.
 * Changed a few log outputs.
 *
 * Revision 1.21  1999/09/26 14:17:53  armin
 * Improved debug and log via readstat()
 *
 * Revision 1.20  1999/09/21 20:35:43  armin
 * added more error checking.
 *
 * Revision 1.19  1999/09/21 20:06:40  armin
 * Added pointer checks.
 *
 * Revision 1.18  1999/09/07 12:48:05  armin
 * Prepared for sub-address usage.
 *
 * Revision 1.17  1999/09/07 12:35:39  armin
 * Better checking and channel Id handling.
 *
 * Revision 1.16  1999/09/04 13:44:19  armin
 * Fix of V.42 analog Modem negotiation handling.
 *
 * Revision 1.15  1999/08/28 21:32:50  armin
 * Prepared for fax related functions.
 * Now compilable without errors/warnings.
 *
 * Revision 1.14  1999/08/28 20:24:40  armin
 * Corrected octet 3/3a in CPN/OAD information element.
 * Thanks to John Simpson <xfl23@dial.pipex.com>
 *
 * Revision 1.13  1999/08/22 20:26:44  calle
 * backported changes from kernel 2.3.14:
 * - several #include "config.h" gone, others come.
 * - "struct device" changed to "struct net_device" in 2.3.14, added a
 *   define in isdn_compat.h for older kernel versions.
 *
 * Revision 1.12  1999/08/18 20:16:59  armin
 * Added XLOG function for all cards.
 * Bugfix of alloc_skb NULL pointer.
 *
 * Revision 1.11  1999/07/25 15:12:03  armin
 * fix of some debug logs.
 * enabled ISA-cards option.
 *
 * Revision 1.10  1999/07/11 17:16:24  armin
 * Bugfixes in queue handling.
 * Added DSP-DTMF decoder functions.
 * Reorganized ack_handler.
 *
 * Revision 1.9  1999/03/29 11:19:42  armin
 * I/O stuff now in seperate file (eicon_io.c)
 * Old ISA type cards (S,SX,SCOM,Quadro,S2M) implemented.
 *
 * Revision 1.8  1999/03/02 12:37:43  armin
 * Added some important checks.
 * Analog Modem with DSP.
 * Channels will be added to Link-Level after loading firmware.
 *
 * Revision 1.7  1999/02/03 18:34:35  armin
 * Channel selection for outgoing calls w/o CHI.
 * Added channel # in debug messages.
 * L2 Transparent should work with 800 byte/packet now.
 *
 * Revision 1.6  1999/01/26 07:18:59  armin
 * Bug with wrong added CPN fixed.
 *
 * Revision 1.5  1999/01/24 20:14:11  armin
 * Changed and added debug stuff.
 * Better data sending. (still problems with tty's flip buffer)
 *
 * Revision 1.4  1999/01/10 18:46:05  armin
 * Bug with wrong values in HLC fixed.
 * Bytes to send are counted and limited now.
 *
 * Revision 1.3  1999/01/05 14:49:34  armin
 * Added experimental usage of full BC and HLC for
 * speech, 3.1kHz audio, fax gr.2/3
 *
 * Revision 1.2  1999/01/04 13:19:29  armin
 * Channel status with listen-request wrong - fixed.
 *
 * Revision 1.1  1999/01/01 18:09:41  armin
 * First checkin of new eicon driver.
 * DIVA-Server BRI/PCI and PRI/PCI are supported.
 * Old diehl code is obsolete.
 *
 *
 */

#include <linux/config.h>
#define __NO_VERSION__
#include "eicon.h"
#include "eicon_idi.h"
#include "eicon_dsp.h"

#undef EICON_FULL_SERVICE_OKTETT

char *eicon_idi_revision = "$Revision: 1.24 $";

eicon_manifbuf *manbuf;

static char BC_Speech[3] = 	{ 0x80, 0x90, 0xa3 };
static char BC_31khz[3] =  	{ 0x90, 0x90, 0xa3 };
static char BC_64k[2] =    	{ 0x88, 0x90 };
static char BC_video[3] =  	{ 0x91, 0x90, 0xa5 };

#ifdef EICON_FULL_SERVICE_OKTETT
/* 
static char HLC_telephony[2] =	{ 0x91, 0x81 }; 
*/
static char HLC_faxg3[2] =  	{ 0x91, 0x84 };
#endif

int eicon_idi_manage_assign(eicon_card *card);
int eicon_idi_manage_remove(eicon_card *card);
int idi_fill_in_T30(eicon_chan *chan, unsigned char *buffer);

int
idi_assign_req(eicon_REQ *reqbuf, int signet, eicon_chan *chan)
{
	int l = 0;
	int tmp;

	tmp = 0;
  if (!signet) {
	/* Signal Layer */
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = KEY;
	reqbuf->XBuffer.P[l++] = 3;
	reqbuf->XBuffer.P[l++] = 'I';
	reqbuf->XBuffer.P[l++] = '4';
	reqbuf->XBuffer.P[l++] = 'L';
	reqbuf->XBuffer.P[l++] = SHIFT|6;
	reqbuf->XBuffer.P[l++] = SIN;
	reqbuf->XBuffer.P[l++] = 2;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */
  }
  else {
	/* Network Layer */
	reqbuf->XBuffer.P[l++] = CAI;
	reqbuf->XBuffer.P[l++] = 1;
	reqbuf->XBuffer.P[l++] = chan->e.D3Id;
	reqbuf->XBuffer.P[l++] = LLC;
	reqbuf->XBuffer.P[l++] = 2;
	switch(chan->l2prot) {
		case ISDN_PROTO_L2_HDLC:
			reqbuf->XBuffer.P[l++] = 2;
			break;
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
			reqbuf->XBuffer.P[l++] = 5; 
			break;
		case ISDN_PROTO_L2_TRANS:
			reqbuf->XBuffer.P[l++] = 2;
			break;
		case ISDN_PROTO_L2_MODEM:
  			if (chan->fsm_state == EICON_STATE_IWAIT)
				reqbuf->XBuffer.P[l++] = 9; /* V.42 incoming */
			else
				reqbuf->XBuffer.P[l++] = 10; /* V.42 */
			break;
		case ISDN_PROTO_L2_FAX:
  			if (chan->fsm_state == EICON_STATE_IWAIT)
				reqbuf->XBuffer.P[l++] = 3; /* autoconnect on incoming */
			else
				reqbuf->XBuffer.P[l++] = 2;
			break;
		default:
			reqbuf->XBuffer.P[l++] = 1;
	}
	switch(chan->l3prot) {
		case ISDN_PROTO_L3_FAX:
#ifdef CONFIG_ISDN_TTY_FAX
			reqbuf->XBuffer.P[l++] = 6;
			reqbuf->XBuffer.P[l++] = NLC;
			tmp = idi_fill_in_T30(chan, &reqbuf->XBuffer.P[l+1]);
			reqbuf->XBuffer.P[l++] = tmp; 
			l += tmp;
			break;
#endif
		case ISDN_PROTO_L3_TRANS:
		default:
			reqbuf->XBuffer.P[l++] = 4;
	}
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->Req = ASSIGN;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 0x20;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 1; /* Net Entity */
  }
   return(0);
}

int
idi_put_req(eicon_REQ *reqbuf, int rq, int signet)
{
	reqbuf->Req = rq;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;
	reqbuf->XBuffer.length = 1;
	reqbuf->XBuffer.P[0] = 0;
	reqbuf->Reference = signet;
   return(0);
}

int
idi_call_res_req(eicon_REQ *reqbuf, eicon_chan *chan)
{
	int l = 9;
	reqbuf->Req = CALL_RES;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;
	reqbuf->XBuffer.P[0] = CAI;
	reqbuf->XBuffer.P[1] = 6;
	reqbuf->XBuffer.P[2] = 9;
	reqbuf->XBuffer.P[3] = 0;
	reqbuf->XBuffer.P[4] = 0;
	reqbuf->XBuffer.P[5] = 0;
	reqbuf->XBuffer.P[6] = 32;
	reqbuf->XBuffer.P[7] = 3;
	switch(chan->l2prot) {
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
		case ISDN_PROTO_L2_HDLC:
			reqbuf->XBuffer.P[1] = 1;
			reqbuf->XBuffer.P[2] = 0x05;
			l = 4;
			break;
		case ISDN_PROTO_L2_V11096:
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 5;
			reqbuf->XBuffer.P[4] = 0;
			break;
		case ISDN_PROTO_L2_V11019:
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 6;
			reqbuf->XBuffer.P[4] = 0;
			break;
		case ISDN_PROTO_L2_V11038:
			reqbuf->XBuffer.P[2] = 0x0d;
			reqbuf->XBuffer.P[3] = 7;
			reqbuf->XBuffer.P[4] = 0;
			break;
		case ISDN_PROTO_L2_MODEM:
			reqbuf->XBuffer.P[2] = 0x11;
			reqbuf->XBuffer.P[3] = 7;
			reqbuf->XBuffer.P[4] = 0;
			reqbuf->XBuffer.P[5] = 0;
			reqbuf->XBuffer.P[6] = 128;
			reqbuf->XBuffer.P[7] = 0;
			break;
		case ISDN_PROTO_L2_FAX:
			reqbuf->XBuffer.P[2] = 0x10;
			reqbuf->XBuffer.P[3] = 0;
			reqbuf->XBuffer.P[4] = 0;
			reqbuf->XBuffer.P[5] = 0;
			reqbuf->XBuffer.P[6] = 128;
			reqbuf->XBuffer.P[7] = 0;
			break;
		case ISDN_PROTO_L2_TRANS:
			switch(chan->l3prot) {
				case ISDN_PROTO_L3_TRANSDSP:
					reqbuf->XBuffer.P[2] = 22; /* DTMF, audio events on */
			}
			break;
	}
	reqbuf->XBuffer.P[8] = 0;
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */
	eicon_log(NULL, 8, "idi_req: Ch%d: Call_Res\n", chan->No);
   return(0);
}

int
idi_do_req(eicon_card *card, eicon_chan *chan, int cmd, int layer)
{
        struct sk_buff *skb;
        struct sk_buff *skb2;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
               	eicon_log(card, 1, "idi_err: Ch%d: alloc_skb failed in do_req()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM; 
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));
	eicon_log(card, 8, "idi_req: Ch%d: req %x (%s)\n", chan->No, cmd, (layer)?"Net":"Sig");
	if (layer) cmd |= 0x700;
	switch(cmd) {
		case ASSIGN:
		case ASSIGN|0x700:
			idi_assign_req(reqbuf, layer, chan);
			break;
		case REMOVE:
		case REMOVE|0x700:
			idi_put_req(reqbuf, REMOVE, layer);
			break;
		case INDICATE_REQ:
			idi_put_req(reqbuf, INDICATE_REQ, 0);
			break;
		case HANGUP:
			idi_put_req(reqbuf, HANGUP, 0);
			break;
		case REJECT:
			idi_put_req(reqbuf, REJECT, 0);
			break;
		case CALL_ALERT:
			idi_put_req(reqbuf, CALL_ALERT, 0);
			break;
		case CALL_RES:
			idi_call_res_req(reqbuf, chan);
			break;
		case IDI_N_CONNECT|0x700:
			idi_put_req(reqbuf, IDI_N_CONNECT, 1);
			break;
		case IDI_N_CONNECT_ACK|0x700:
			idi_put_req(reqbuf, IDI_N_CONNECT_ACK, 1);
			break;
		case IDI_N_DISC|0x700:
			idi_put_req(reqbuf, IDI_N_DISC, 1);
			break;
		case IDI_N_DISC_ACK|0x700:
			idi_put_req(reqbuf, IDI_N_DISC_ACK, 1);
			break;
		default:
			eicon_log(card, 1, "idi_req: Ch%d: Unknown request\n", chan->No);
			dev_kfree_skb(skb);
			dev_kfree_skb(skb2);
			return(-1);
	}

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	eicon_schedule_tx(card);
	return(0);
}

int
eicon_idi_listen_req(eicon_card *card, eicon_chan *chan)
{
	if ((!card) || (!chan))
		return 1;

	eicon_log(card, 16, "idi_req: Ch%d: Listen_Req eazmask=0x%x\n",chan->No, chan->eazmask);
	if (!chan->e.D3Id) {
		idi_do_req(card, chan, ASSIGN, 0); 
	}
	if (chan->fsm_state == EICON_STATE_NULL) {
		idi_do_req(card, chan, INDICATE_REQ, 0);
		chan->fsm_state = EICON_STATE_LISTEN;
	}
  return(0);
}

unsigned char
idi_si2bc(int si1, int si2, char *bc, char *hlc)
{
  hlc[0] = 0;
  switch(si1) {
	case 1:
		bc[0] = 0x90;		/* 3,1 kHz audio */
		bc[1] = 0x90;		/* 64 kbit/s */
		bc[2] = 0xa3;		/* G.711 A-law */
#ifdef EICON_FULL_SERVICE_OKTETT
		if (si2 == 1) {
			bc[0] = 0x80;	/* Speech */
			hlc[0] = 0x02;	/* hlc len */
			hlc[1] = 0x91;	/* first hic */
			hlc[2] = 0x81;	/* Telephony */
		}
#endif
		return(3);
	case 2:
		bc[0] = 0x90;		/* 3,1 kHz audio */
		bc[1] = 0x90;		/* 64 kbit/s */
		bc[2] = 0xa3;		/* G.711 A-law */
#ifdef EICON_FULL_SERVICE_OKTETT
		if (si2 == 2) {
			hlc[0] = 0x02;	/* hlc len */
			hlc[1] = 0x91;	/* first hic */
			hlc[2] = 0x84;	/* Fax Gr.2/3 */
		}
#endif
		return(3);
	case 5:
	case 7:
	default:
		bc[0] = 0x88;
		bc[1] = 0x90;
		return(2);
  }
 return (0);
}

int
idi_hangup(eicon_card *card, eicon_chan *chan)
{
	if ((!card) || (!chan))
		return 1;

	if ((chan->fsm_state == EICON_STATE_ACTIVE) ||
	    (chan->fsm_state == EICON_STATE_WMCONN)) {
  		if (chan->e.B2Id) idi_do_req(card, chan, IDI_N_DISC, 1);
	}
	if (chan->e.B2Id) idi_do_req(card, chan, REMOVE, 1);
	if (chan->fsm_state != EICON_STATE_NULL) {
		idi_do_req(card, chan, HANGUP, 0);
		chan->fsm_state = EICON_STATE_NULL;
	}
	eicon_log(card, 8, "idi_req: Ch%d: Hangup\n", chan->No);
#ifdef CONFIG_ISDN_TTY_FAX
	chan->fax = 0;
#endif
  return(0);
}

int
idi_connect_res(eicon_card *card, eicon_chan *chan)
{
	if ((!card) || (!chan))
		return 1;

	chan->fsm_state = EICON_STATE_IWAIT;
	idi_do_req(card, chan, CALL_RES, 0);
	
	/* check if old NetID has been removed */
	if (chan->e.B2Id) {
		eicon_log(card, 1, "eicon: Ch%d: old net_id %x still exist, removing.\n",
			chan->No, chan->e.B2Id);
		idi_do_req(card, chan, REMOVE, 1);
	}

	idi_do_req(card, chan, ASSIGN, 1);
	return(0);
}

int
idi_connect_req(eicon_card *card, eicon_chan *chan, char *phone,
                    char *eazmsn, int si1, int si2)
{
	int l = 0;
	int i;
	unsigned char tmp;
	unsigned char *sub, *sp;
	unsigned char bc[5];
	unsigned char hlc[5];
        struct sk_buff *skb;
        struct sk_buff *skb2;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

	if ((!card) || (!chan))
		return 1;

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
               	eicon_log(card, 1, "idi_err: Ch%d: alloc_skb failed in connect_req()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM; 
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));
	reqbuf->Req = CALL_REQ;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;

	sub = NULL;
	sp = phone;
	while (*sp) {
		if (*sp == '.') {
			sub = sp + 1;
			*sp = 0;
		} else
			sp++;
	}
	reqbuf->XBuffer.P[l++] = CPN;
	reqbuf->XBuffer.P[l++] = strlen(phone) + 1;
	reqbuf->XBuffer.P[l++] = 0x81;
	for(i=0; i<strlen(phone);i++) 
		reqbuf->XBuffer.P[l++] = phone[i] & 0x7f;
	if (sub) {
		reqbuf->XBuffer.P[l++] = DSA;
		reqbuf->XBuffer.P[l++] = strlen(sub) + 2;
		reqbuf->XBuffer.P[l++] = 0x80; /* NSAP coded */
		reqbuf->XBuffer.P[l++] = 0x50; /* local IDI format */
		while (*sub)
			reqbuf->XBuffer.P[l++] = *sub++ & 0x7f;
	}

	sub = NULL;
	sp = eazmsn;
	while (*sp) {
		if (*sp == '.') {
			sub = sp + 1;
			*sp = 0;
		} else
			sp++;
	}
	reqbuf->XBuffer.P[l++] = OAD;
	reqbuf->XBuffer.P[l++] = strlen(eazmsn) + 2;
	reqbuf->XBuffer.P[l++] = 0x01;
	reqbuf->XBuffer.P[l++] = 0x80;
	for(i=0; i<strlen(eazmsn);i++) 
		reqbuf->XBuffer.P[l++] = eazmsn[i] & 0x7f;
	if (sub) {
		reqbuf->XBuffer.P[l++] = OSA;
		reqbuf->XBuffer.P[l++] = strlen(sub) + 2;
		reqbuf->XBuffer.P[l++] = 0x80; /* NSAP coded */
		reqbuf->XBuffer.P[l++] = 0x50; /* local IDI format */
		while (*sub)
			reqbuf->XBuffer.P[l++] = *sub++ & 0x7f;
	}

	if ((tmp = idi_si2bc(si1, si2, bc, hlc)) > 0) {
		reqbuf->XBuffer.P[l++] = BC;
		reqbuf->XBuffer.P[l++] = tmp;
		for(i=0; i<tmp;i++) 
			reqbuf->XBuffer.P[l++] = bc[i];
		if ((tmp=hlc[0])) {
			reqbuf->XBuffer.P[l++] = HLC;
			reqbuf->XBuffer.P[l++] = tmp;
			for(i=1; i<=tmp;i++) 
				reqbuf->XBuffer.P[l++] = hlc[i];
		}
	}

        reqbuf->XBuffer.P[l++] = CAI;
        reqbuf->XBuffer.P[l++] = 6;
        reqbuf->XBuffer.P[l++] = 0x09;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 0;
	reqbuf->XBuffer.P[l++] = 32;
	reqbuf->XBuffer.P[l++] = 3;
        switch(chan->l2prot) {
		case ISDN_PROTO_L2_X75I:
		case ISDN_PROTO_L2_X75UI:
		case ISDN_PROTO_L2_X75BUI:
                case ISDN_PROTO_L2_HDLC:
                        reqbuf->XBuffer.P[l-6] = 5;
                        reqbuf->XBuffer.P[l-7] = 1;
			l -= 5; 
                        break;
                case ISDN_PROTO_L2_V11096:
                        reqbuf->XBuffer.P[l-7] = 3;
                        reqbuf->XBuffer.P[l-6] = 0x0d;
                        reqbuf->XBuffer.P[l-5] = 5;
                        reqbuf->XBuffer.P[l-4] = 0;
                        l -= 3;
                        break;
                case ISDN_PROTO_L2_V11019:
                        reqbuf->XBuffer.P[l-7] = 3;
                        reqbuf->XBuffer.P[l-6] = 0x0d;
                        reqbuf->XBuffer.P[l-5] = 6;
                        reqbuf->XBuffer.P[l-4] = 0;
                        l -= 3;
                        break;
                case ISDN_PROTO_L2_V11038:
                        reqbuf->XBuffer.P[l-7] = 3;
                        reqbuf->XBuffer.P[l-6] = 0x0d;
                        reqbuf->XBuffer.P[l-5] = 7;
                        reqbuf->XBuffer.P[l-4] = 0;
                        l -= 3;
                        break;
                case ISDN_PROTO_L2_MODEM:
			reqbuf->XBuffer.P[l-6] = 0x11;
			reqbuf->XBuffer.P[l-5] = 7;
			reqbuf->XBuffer.P[l-4] = 0;
			reqbuf->XBuffer.P[l-3] = 0;
			reqbuf->XBuffer.P[l-2] = 128;
			reqbuf->XBuffer.P[l-1] = 0;
                        break;
                case ISDN_PROTO_L2_FAX:
			reqbuf->XBuffer.P[l-6] = 0x10;
			reqbuf->XBuffer.P[l-5] = 0;
			reqbuf->XBuffer.P[l-4] = 0;
			reqbuf->XBuffer.P[l-3] = 0;
			reqbuf->XBuffer.P[l-2] = 128;
			reqbuf->XBuffer.P[l-1] = 0;
                        break;
		case ISDN_PROTO_L2_TRANS:
			switch(chan->l3prot) {
				case ISDN_PROTO_L3_TRANSDSP:
					reqbuf->XBuffer.P[l-6] = 22; /* DTMF, audio events on */
			}
			break;
        }
	
	reqbuf->XBuffer.P[l++] = 0; /* end */
	reqbuf->XBuffer.length = l;
	reqbuf->Reference = 0; /* Sig Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2); 
	eicon_schedule_tx(card);

	eicon_log(card, 8, "idi_req: Ch%d: Conn_Req %s -> %s\n",chan->No, eazmsn, phone);
   return(0);
}


void
idi_IndParse(eicon_card *ccard, eicon_chan *chan, idi_ind_message *message, unsigned char *buffer, int len)
{
	int i,j;
	int pos = 0;
	int codeset = 0;
	int wlen = 0;
	int lock = 0;
	__u8 w;
	__u16 code;
	isdn_ctrl cmd;

	memset(message, 0, sizeof(idi_ind_message));

	if ((!len) || (!buffer[pos])) return;

  while(pos <= len) {
	w = buffer[pos++];
	if (!w) return;
	if (w & 0x80) {
		wlen = 0;
	}
	else {
		wlen = buffer[pos++];
	}

	if (pos > len) return;

	if (lock & 0x80) lock &= 0x7f;
	else codeset = lock;

	if((w&0xf0) == SHIFT) {
		codeset = w;
		if(!(codeset & 0x08)) lock = codeset & 7;
		codeset &= 7;
		lock |= 0x80;
	}
	else {
		if (w==ESC && wlen >=2) {
			code = buffer[pos++]|0x800;
			wlen--;
		}
		else code = w;
		code |= (codeset<<8);

		if (pos + wlen > len) {
			eicon_log(ccard, 1, "idi_err: Ch%d: IElen %d of %x exceeds Ind_Length (+%d)\n", chan->No, 
					wlen, code, (pos + wlen) - len);
			return;
		}

		switch(code) {
			case OAD:
				if (wlen > sizeof(message->oad)) {
					pos += wlen;
					break;
				}
				j = 1;
				if (wlen) {
					message->plan = buffer[pos++];
					if (message->plan &0x80) 
						message->screen = 0;
					else {
						message->screen = buffer[pos++];
						j = 2;
					}
				}
				for(i=0; i < wlen-j; i++) 
					message->oad[i] = buffer[pos++];
				eicon_log(ccard, 2, "idi_inf: Ch%d: OAD=(0x%02x,0x%02x) %s\n", chan->No, 
					message->plan, message->screen, message->oad);
				break;
			case RDN:
				if (wlen > sizeof(message->rdn)) {
					pos += wlen;
					break;
				}
				j = 1;
				if (wlen) {
					if (!(buffer[pos++] & 0x80)) {
						pos++; 
						j = 2;
					}
				}
				for(i=0; i < wlen-j; i++) 
					message->rdn[i] = buffer[pos++];
				eicon_log(ccard, 2, "idi_inf: Ch%d: RDN= %s\n", chan->No, 
						message->rdn);
				break;
			case CPN:
				if (wlen > sizeof(message->cpn)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->cpn[i] = buffer[pos++];
				eicon_log(ccard, 2, "idi_inf: Ch%d: CPN=(0x%02x) %s\n", chan->No,
					(__u8)message->cpn[0], message->cpn + 1);
				break;
			case DSA:
				if (wlen > sizeof(message->dsa)) {
					pos += wlen;
					break;
				}
				pos += 2;
				for(i=0; i < wlen-2; i++) 
					message->dsa[i] = buffer[pos++];
				eicon_log(ccard, 2, "idi_inf: Ch%d: DSA=%s\n", chan->No, message->dsa);
				break;
			case OSA:
				if (wlen > sizeof(message->osa)) {
					pos += wlen;
					break;
				}
				pos += 2;
				for(i=0; i < wlen-2; i++) 
					message->osa[i] = buffer[pos++];
				eicon_log(ccard, 2, "idi_inf: Ch%d: OSA=%s\n", chan->No, message->osa);
				break;
			case BC:
				if (wlen > sizeof(message->bc)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->bc[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: BC = 0x%02x 0x%02x 0x%02x\n", chan->No,
					message->bc[0],message->bc[1],message->bc[2]);
				break;
			case 0x800|BC:
				if (wlen > sizeof(message->e_bc)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->e_bc[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: ESC/BC=%d\n", chan->No, message->bc[0]);
				break;
			case LLC:
				if (wlen > sizeof(message->llc)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->llc[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: LLC=%d %d %d %d\n", chan->No, message->llc[0],
					message->llc[1],message->llc[2],message->llc[3]);
				break;
			case HLC:
				if (wlen > sizeof(message->hlc)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->hlc[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: HLC=%x %x %x %x %x\n", chan->No,
					message->hlc[0], message->hlc[1],
					message->hlc[2], message->hlc[3], message->hlc[4]);
				break;
			case DSP:
			case 0x600|DSP:
				if (wlen > sizeof(message->display)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->display[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: Display: %s\n", chan->No,
					message->display);
				break;
			case 0x600|KEY:
				if (wlen > sizeof(message->keypad)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->keypad[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: Keypad: %s\n", chan->No,
					message->keypad);
				break;
			case NI:
			case 0x600|NI:
				if (wlen) {
					switch(buffer[pos] & 127) {
						case 0:
							eicon_log(ccard, 4, "idi_inf: Ch%d: User suspended.\n", chan->No);
							break;
						case 1:
							eicon_log(ccard, 4, "idi_inf: Ch%d: User resumed.\n", chan->No);
							break;
						case 2:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Bearer service change.\n", chan->No);
							break;
						default:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Unknown Notification %x.\n", 
									chan->No, buffer[pos] & 127);
					}
					pos += wlen;
				}
				break;
			case PI:
			case 0x600|PI:
				if (wlen > 1) {
					switch(buffer[pos+1] & 127) {
						case 1:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Call is not end-to-end ISDN.\n", chan->No);
							break;
						case 2:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Destination address is non ISDN.\n", chan->No);
							break;
						case 3:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Origination address is non ISDN.\n", chan->No);
							break;
						case 4:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Call has returned to the ISDN.\n", chan->No);
							break;
						case 5:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Interworking has occurred.\n", chan->No);
							break;
						case 8:
							eicon_log(ccard, 4, "idi_inf: Ch%d: In-band information available.\n", chan->No);
							break;
						default:
							eicon_log(ccard, 4, "idi_inf: Ch%d: Unknown Progress %x.\n", 
									chan->No, buffer[pos+1] & 127);
					}
				}
				pos += wlen;
				break;
			case CAU:
				if (wlen > sizeof(message->cau)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->cau[i] = buffer[pos++];
				memcpy(&chan->cause, &message->cau, 2);
				eicon_log(ccard, 4, "idi_inf: Ch%d: CAU=%d %d\n", chan->No,
					message->cau[0],message->cau[1]);
				break;
			case 0x800|CAU:
				if (wlen > sizeof(message->e_cau)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->e_cau[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: ECAU=%d %d\n", chan->No,
					message->e_cau[0],message->e_cau[1]);
				break;
			case 0x800|CHI:
				if (wlen > sizeof(message->e_chi)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->e_chi[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: ESC/CHI=%d\n", chan->No,
					message->e_cau[0]);
				break;
			case 0x800|0x7a:
				pos ++;
				message->e_mt=buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: EMT=0x%x\n", chan->No, message->e_mt);
				break;
			case DT:
				if (wlen > sizeof(message->dt)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->dt[i] = buffer[pos++];
				eicon_log(ccard, 4, "idi_inf: Ch%d: DT: %02d.%02d.%02d %02d:%02d:%02d\n", chan->No,
					message->dt[2], message->dt[1], message->dt[0],
					message->dt[3], message->dt[4], message->dt[5]);
				break;
			case 0x600|SIN:
				if (wlen > sizeof(message->sin)) {
					pos += wlen;
					break;
				}
				for(i=0; i < wlen; i++) 
					message->sin[i] = buffer[pos++];
				eicon_log(ccard, 2, "idi_inf: Ch%d: SIN=%d %d\n", chan->No,
					message->sin[0],message->sin[1]);
				break;
			case 0x600|CPS:
				eicon_log(ccard, 2, "idi_inf: Ch%d: Called Party Status in ind\n", chan->No);
				pos += wlen;
				break;
			case 0x600|CIF:
				for (i = 0; i < wlen; i++)
					if (buffer[pos + i] != '0') break;
				memcpy(&cmd.parm.num, &buffer[pos + i], wlen - i);
				cmd.parm.num[wlen - i] = 0;
				eicon_log(ccard, 2, "idi_inf: Ch%d: CIF=%s\n", chan->No, cmd.parm.num);
				pos += wlen;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_CINF;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break;
			case 0x600|DATE:
				eicon_log(ccard, 2, "idi_inf: Ch%d: Date in ind\n", chan->No);
				pos += wlen;
				break;
			case 0xa1: 
				eicon_log(ccard, 2, "idi_inf: Ch%d: Sending Complete in ind.\n", chan->No);
				pos += wlen;
				break;
			case 0xe08: 
			case 0xe7a: 
			case 0xe04: 
			case 0xe00: 
				/* *** TODO *** */
			case CHA:
				/* Charge advice */
			case FTY:
			case 0x600|FTY:
			case CHI:
			case 0x800:
				/* Not yet interested in this */
				pos += wlen;
				break;
			case 0x880:
				/* Managment Information Element */
				if (!manbuf) {
					eicon_log(ccard, 1, "idi_err: manbuf not allocated\n");
				}
				else {
					memcpy(&manbuf->data[manbuf->pos], &buffer[pos], wlen);
					manbuf->length[manbuf->count] = wlen;
					manbuf->count++;
					manbuf->pos += wlen;
				}
				pos += wlen;
				break;
			default:
				pos += wlen;
				eicon_log(ccard, 6, "idi_inf: Ch%d: unknown information element 0x%x in ind, len:%x\n", 
					chan->No, code, wlen);
		}
	}
  }
}

void
idi_bc2si(unsigned char *bc, unsigned char *hlc, unsigned char *si1, unsigned char *si2)
{
  si1[0] = 0;
  si2[0] = 0;
  if (memcmp(bc, BC_Speech, 3) == 0) {		/* Speech */
	si1[0] = 1;
#ifdef EICON_FULL_SERVICE_OKTETT
	si2[0] = 1;
#endif
  }
  if (memcmp(bc, BC_31khz, 3) == 0) {		/* 3.1kHz audio */
	si1[0] = 1;
#ifdef EICON_FULL_SERVICE_OKTETT
	si2[0] = 2;
  	if (memcmp(hlc, HLC_faxg3, 2) == 0) {	/* Fax Gr.2/3 */
		si1[0] = 2;
	}
#endif
  }
  if (memcmp(bc, BC_64k, 2) == 0) {		/* unrestricted 64 kbits */
	si1[0] = 7;
  }
  if (memcmp(bc, BC_video, 3) == 0) {		/* video */
	si1[0] = 4;
  }
}

/********************* FAX stuff ***************************/

#ifdef CONFIG_ISDN_TTY_FAX

int
idi_fill_in_T30(eicon_chan *chan, unsigned char *buffer)
{
	eicon_t30_s	*t30 = (eicon_t30_s *) buffer;

	if (!chan->fax) {
		eicon_log(NULL, 1,"idi_T30: fill_in with NULL fax struct, ERROR\n");
		return 0;
	}
	memset(t30, 0, sizeof(eicon_t30_s));
	t30->station_id_len = EICON_FAXID_LEN;
	memcpy(&t30->station_id[0], &chan->fax->id[0], EICON_FAXID_LEN);
	t30->resolution = chan->fax->resolution;
	t30->rate = chan->fax->rate + 1;	/* eicon rate starts with 1 */
	t30->format = T30_FORMAT_SFF;
	t30->pages_low = 0;
	t30->pages_high = 0;
	t30->atf = 1;				/* optimised for AT+F command set */
	t30->code = 0;
	t30->feature_bits_low = 0;
	t30->feature_bits_high = 0;
	t30->control_bits_low = 0;
	t30->control_bits_high = 0;

	if (chan->fax->nbc) {
		/* set compression by DCC value */
  	  switch(chan->fax->compression) {
		case (0):	/* 1-D modified */
			break;
		case (1):	/* 2-D modified Read */
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_2D_CODING;
			t30->feature_bits_low |= T30_FEATURE_BIT_2D_CODING;
			break;
		case (2):	/* 2-D uncompressed */
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_UNCOMPR;
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_2D_CODING;
			t30->feature_bits_low |= T30_FEATURE_BIT_UNCOMPR_ENABLED;
			t30->feature_bits_low |= T30_FEATURE_BIT_2D_CODING;
			break;
		case (3):	/* 2-D modified Read */
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_T6_CODING;
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_2D_CODING;
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_UNCOMPR;
			t30->feature_bits_low |= T30_FEATURE_BIT_UNCOMPR_ENABLED;
			t30->feature_bits_low |= T30_FEATURE_BIT_T6_CODING;
			t30->feature_bits_low |= T30_FEATURE_BIT_2D_CODING;
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_ECM;
			t30->feature_bits_low |= T30_FEATURE_BIT_ECM;
			break;
	  }
	} else {
		/* set compression to best */
		t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_T6_CODING;
		t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_2D_CODING;
		t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_UNCOMPR;
		t30->feature_bits_low |= T30_FEATURE_BIT_UNCOMPR_ENABLED;
		t30->feature_bits_low |= T30_FEATURE_BIT_T6_CODING;
		t30->feature_bits_low |= T30_FEATURE_BIT_2D_CODING;
		t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_ECM;
		t30->feature_bits_low |= T30_FEATURE_BIT_ECM;
	}
	switch(chan->fax->ecm) {
		case (0):	/* disable ECM */
			break;
		case (1):
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_ECM;
			t30->control_bits_low |= T30_CONTROL_BIT_ECM_64_BYTES;
			t30->feature_bits_low |= T30_FEATURE_BIT_ECM;
			t30->feature_bits_low |= T30_FEATURE_BIT_ECM_64_BYTES;
			break;
		case (2):
			t30->control_bits_low |= T30_CONTROL_BIT_ENABLE_ECM;
			t30->feature_bits_low |= T30_FEATURE_BIT_ECM;
			break;
	}

	if (DebugVar & 128) {
		char st[40];
		eicon_log(NULL, 128, "sT30:code = %x\n", t30->code);
		eicon_log(NULL, 128, "sT30:rate = %x\n", t30->rate);
		eicon_log(NULL, 128, "sT30:res  = %x\n", t30->resolution);
		eicon_log(NULL, 128, "sT30:format = %x\n", t30->format);
		eicon_log(NULL, 128, "sT30:pages_low = %x\n", t30->pages_low);
		eicon_log(NULL, 128, "sT30:pages_high = %x\n", t30->pages_high);
		eicon_log(NULL, 128, "sT30:atf  = %x\n", t30->atf);
		eicon_log(NULL, 128, "sT30:control_bits_low = %x\n", t30->control_bits_low);
		eicon_log(NULL, 128, "sT30:control_bits_high = %x\n", t30->control_bits_high);
		eicon_log(NULL, 128, "sT30:feature_bits_low = %x\n", t30->feature_bits_low);
		eicon_log(NULL, 128, "sT30:feature_bits_high = %x\n", t30->feature_bits_high);
		//eicon_log(NULL, 128, "sT30:universal_5 = %x\n", t30->universal_5);
		//eicon_log(NULL, 128, "sT30:universal_6 = %x\n", t30->universal_6);
		//eicon_log(NULL, 128, "sT30:universal_7 = %x\n", t30->universal_7);
		eicon_log(NULL, 128, "sT30:station_id_len = %x\n", t30->station_id_len);
		eicon_log(NULL, 128, "sT30:head_line_len = %x\n", t30->head_line_len);
		strncpy(st, t30->station_id, t30->station_id_len);
		st[t30->station_id_len] = 0;
		eicon_log(NULL, 128, "sT30:station_id = <%s>\n", st);
	}
	return(sizeof(eicon_t30_s));
}

/* send fax struct */
int
idi_send_edata(eicon_card *card, eicon_chan *chan)
{
	struct sk_buff *skb;
	struct sk_buff *skb2;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

	if ((chan->fsm_state == EICON_STATE_NULL) || (chan->fsm_state == EICON_STATE_LISTEN)) {
		eicon_log(card, 1, "idi_snd: Ch%d: send edata on state %d !\n", chan->No, chan->fsm_state);
		return -ENODEV;
	}
	eicon_log(card, 128, "idi_snd: Ch%d: edata (fax)\n", chan->No);

	skb = alloc_skb(sizeof(eicon_REQ) + sizeof(eicon_t30_s), GFP_ATOMIC);
	skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

	if ((!skb) || (!skb2)) {
		eicon_log(card, 1, "idi_err: Ch%d: alloc_skb failed in send_edata()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
		return -ENOMEM;
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, sizeof(eicon_t30_s) + sizeof(eicon_REQ));

	reqbuf->Req = IDI_N_EDATA;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;

	reqbuf->XBuffer.length = idi_fill_in_T30(chan, reqbuf->XBuffer.P);
	reqbuf->Reference = 1; /* Net Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2);
	eicon_schedule_tx(card);
	return (0);
}

void
idi_parse_edata(eicon_card *ccard, eicon_chan *chan, unsigned char *buffer, int len)
{
	eicon_t30_s *p = (eicon_t30_s *)buffer;
	int i;

	if (DebugVar & 128) {
		char st[40];
		eicon_log(ccard, 128, "rT30:len %d , size %d\n", len, sizeof(eicon_t30_s));
		eicon_log(ccard, 128, "rT30:code = %x\n", p->code);
		eicon_log(ccard, 128, "rT30:rate = %x\n", p->rate);
		eicon_log(ccard, 128, "rT30:res  = %x\n", p->resolution);
		eicon_log(ccard, 128, "rT30:format = %x\n", p->format);
		eicon_log(ccard, 128, "rT30:pages_low = %x\n", p->pages_low);
		eicon_log(ccard, 128, "rT30:pages_high = %x\n", p->pages_high);
		eicon_log(ccard, 128, "rT30:atf  = %x\n", p->atf);
		eicon_log(ccard, 128, "rT30:control_bits_low = %x\n", p->control_bits_low);
		eicon_log(ccard, 128, "rT30:control_bits_high = %x\n", p->control_bits_high);
		eicon_log(ccard, 128, "rT30:feature_bits_low = %x\n", p->feature_bits_low);
		eicon_log(ccard, 128, "rT30:feature_bits_high = %x\n", p->feature_bits_high);
		//eicon_log(ccard, 128, "rT30:universal_5 = %x\n", p->universal_5);
		//eicon_log(ccard, 128, "rT30:universal_6 = %x\n", p->universal_6);
		//eicon_log(ccard, 128, "rT30:universal_7 = %x\n", p->universal_7);
		eicon_log(ccard, 128, "rT30:station_id_len = %x\n", p->station_id_len);
		eicon_log(ccard, 128, "rT30:head_line_len = %x\n", p->head_line_len);
		strncpy(st, p->station_id, p->station_id_len);
		st[p->station_id_len] = 0;
		eicon_log(ccard, 128, "rT30:station_id = <%s>\n", st);
	}
	if (!chan->fax) {
		eicon_log(ccard, 1, "idi_edata: parse to NULL fax struct, ERROR\n");
		return;
	}
	chan->fax->code = p->code;
	i = (p->station_id_len < FAXIDLEN) ? p->station_id_len : (FAXIDLEN - 1);
	memcpy(chan->fax->r_id, p->station_id, i);
	chan->fax->r_id[i] = 0;
	chan->fax->r_resolution = p->resolution;
	chan->fax->r_rate = p->rate - 1;
	chan->fax->r_binary = 0; /* no binary support */
	chan->fax->r_width = 0;
	chan->fax->r_length = 2;
	chan->fax->r_scantime = 0;
	chan->fax->r_compression = 0;
	chan->fax->r_ecm = 0;
	if (p->feature_bits_low & T30_FEATURE_BIT_2D_CODING) {
		chan->fax->r_compression = 1;
		if (p->feature_bits_low & T30_FEATURE_BIT_UNCOMPR_ENABLED) {
			chan->fax->r_compression = 2;
		}
	}
	if (p->feature_bits_low & T30_FEATURE_BIT_T6_CODING) {
		chan->fax->r_compression = 3;
	}

	if (p->feature_bits_low & T30_FEATURE_BIT_ECM) {
		chan->fax->r_ecm = 2;
		if (p->feature_bits_low & T30_FEATURE_BIT_ECM_64_BYTES)
			chan->fax->r_ecm = 1;
	}
}

void
idi_fax_send_header(eicon_card *card, eicon_chan *chan, int header)
{
	static __u16 wd2sff[] = {
		1728, 2048, 2432, 1216, 864
	};
	static __u16 ln2sff[2][3] = {
		{ 1143, 1401, 0 } , { 2287, 2802, 0 }
	};
	struct sk_buff *skb;
	eicon_sff_dochead *doc;
	eicon_sff_pagehead *page;
	u_char *docp;

	if (!chan->fax) {
		eicon_log(card, 1, "idi_fax: send head with NULL fax struct, ERROR\n");
		return;
	}
	if (header == 2) { /* DocHeader + PageHeader */
		skb = alloc_skb(sizeof(eicon_sff_dochead) + sizeof(eicon_sff_pagehead), GFP_ATOMIC);
	} else {
		skb = alloc_skb(sizeof(eicon_sff_pagehead), GFP_ATOMIC);
	}
	if (!skb) {
		eicon_log(card, 1, "idi_err: Ch%d: alloc_skb failed in fax_send_header()\n", chan->No);
		return;
	}

	if (header == 2) { /* DocHeader + PageHeader */
		docp = skb_put(skb, sizeof(eicon_sff_dochead) + sizeof(eicon_sff_pagehead));
		doc = (eicon_sff_dochead *) docp;
		page = (eicon_sff_pagehead *) (docp + sizeof(eicon_sff_dochead));
		memset(docp, 0,sizeof(eicon_sff_dochead)  + sizeof(eicon_sff_pagehead));
		doc->id = 0x66666653;
		doc->version = 0x01;
		doc->off1pagehead = sizeof(eicon_sff_dochead);
	} else {
		page = (eicon_sff_pagehead *)skb_put(skb, sizeof(eicon_sff_pagehead));
		memset(page, 0, sizeof(eicon_sff_pagehead));
	}

	switch(header) {
		case 1:	/* PageHeaderEnd */
			page->pageheadid = 254;
			page->pageheadlen = 0; 
			break;
		case 0: /* PageHeader */
		case 2: /* DocHeader + PageHeader */
			page->pageheadid = 254;
			page->pageheadlen = sizeof(eicon_sff_pagehead) - 2;
			page->resvert = chan->fax->resolution;
			page->reshoriz = 0; /* always 203 dpi */
			page->coding = 0; /* always 1D */
			page->linelength = wd2sff[chan->fax->width];
			page->pagelength = ln2sff[chan->fax->resolution][chan->fax->length]; 
			eicon_log(card, 128, "sSFF-Head: linelength = %d\n", page->linelength);
			eicon_log(card, 128, "sSFF-Head: pagelength = %d\n", page->pagelength);
			break;
	}
	idi_send_data(card, chan, 0, skb, 0);
}

void
idi_fax_cmd(eicon_card *card, eicon_chan *chan) 
{
	isdn_ctrl cmd;

	if ((!card) || (!chan))
		return;

	if (!chan->fax) {
		eicon_log(card, 1, "idi_fax: cmd with NULL fax struct, ERROR\n");
		return;
	}
	switch (chan->fax->code) {
		case ISDN_TTY_FAX_DT:
			if (chan->fax->phase == ISDN_FAX_PHASE_B) {
				idi_send_edata(card, chan);
				break;
			}
			if (chan->fax->phase == ISDN_FAX_PHASE_D) {
				idi_send_edata(card, chan);
				break;
			}
			break;

		case ISDN_TTY_FAX_DR:
			if (chan->fax->phase == ISDN_FAX_PHASE_B) {
				idi_send_edata(card, chan);

				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_FAXIND;
				cmd.arg = chan->No;
				chan->fax->r_code = ISDN_TTY_FAX_CFR;
				card->interface.statcallb(&cmd);

				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_FAXIND;
				cmd.arg = chan->No;
				chan->fax->r_code = ISDN_TTY_FAX_RID;
				card->interface.statcallb(&cmd);

				/* telling 1-D compression */
				chan->fax->r_compression = 0;
				cmd.driver = card->myid;
				cmd.command = ISDN_STAT_FAXIND;
				cmd.arg = chan->No;
				chan->fax->r_code = ISDN_TTY_FAX_DCS;
				card->interface.statcallb(&cmd);

				chan->fax2.NextObject = FAX_OBJECT_DOCU;
				chan->fax2.PrevObject = FAX_OBJECT_DOCU;

				break;
			}
			if (chan->fax->phase == ISDN_FAX_PHASE_D) {
				idi_send_edata(card, chan);
				break;
			}
			break;

		case ISDN_TTY_FAX_ET:
				switch(chan->fax->fet) {
					case 0:
					case 1:
						idi_fax_send_header(card, chan, 0);
						break;
					case 2:
						idi_fax_send_header(card, chan, 1);
						break;
				}
			break;
	}
}

void
idi_edata_rcveop(eicon_card *card, eicon_chan *chan)
{
	isdn_ctrl cmd;

	if (!chan->fax) {
		eicon_log(card, 1, "idi_edata: rcveop with NULL fax struct, ERROR\n");
		return;
	}
	cmd.driver = card->myid;
	cmd.command = ISDN_STAT_FAXIND;
	cmd.arg = chan->No;
	chan->fax->r_code = ISDN_TTY_FAX_ET;
	card->interface.statcallb(&cmd);
}

void
idi_reset_fax_stat(eicon_chan *chan)
{
	chan->fax2.LineLen = 0;
	chan->fax2.LineData = 0;
	chan->fax2.LineDataLen = 0;
	chan->fax2.NullByteExist = 0;
	chan->fax2.Dle = 0;
	chan->fax2.PageCount = 0;
	chan->fax2.Eop = 0;
}

void
idi_edata_action(eicon_card *ccard, eicon_chan *chan, char *buffer, int len)
{
	isdn_ctrl cmd;

	if (!chan->fax) {
		eicon_log(ccard, 1, "idi_edata: action with NULL fax struct, ERROR\n");
		return;
	}
	if (chan->fax->direction == ISDN_TTY_FAX_CONN_OUT) {
		idi_parse_edata(ccard, chan, buffer, len);

		if (chan->fax->phase == ISDN_FAX_PHASE_A) {
			idi_reset_fax_stat(chan);

			chan->fsm_state = EICON_STATE_ACTIVE;
			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_BCONN;
			cmd.arg = chan->No;
			ccard->interface.statcallb(&cmd);

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_FCON;
			ccard->interface.statcallb(&cmd);

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_RID;
			ccard->interface.statcallb(&cmd);

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_DIS;
			ccard->interface.statcallb(&cmd);

			if (chan->fax->r_compression != 0) {
			/* telling fake compression in second DIS message */
				chan->fax->r_compression = 0;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_FAXIND;
				cmd.arg = chan->No;
				chan->fax->r_code = ISDN_TTY_FAX_DIS;
				ccard->interface.statcallb(&cmd);
			}

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_SENT; /* OK message */
			ccard->interface.statcallb(&cmd);
		} else
		if (chan->fax->phase == ISDN_FAX_PHASE_D) {

			if ((chan->fax->code == EDATA_T30_MCF) &&
			    (chan->fax->fet != 2)) {
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_FAXIND;
				cmd.arg = chan->No;
				chan->fax->r_code = ISDN_TTY_FAX_PTS;
				ccard->interface.statcallb(&cmd);
			}

			switch(chan->fax->fet) {
				case 0:	/* new page */
					/* stay in phase D , wait on cmd +FDT */
					break;
				case 1:	/* new document */
					/* link-level switch to phase B */
					break;
				case 2:	/* session end */
				default:
					/* idi_send_edata(ccard, chan); */
					break;
			}
		}
	}

	if (chan->fax->direction == ISDN_TTY_FAX_CONN_IN) {
		idi_parse_edata(ccard, chan, buffer, len);

		if ((chan->fax->code == EDATA_T30_DCS) &&
		    (chan->fax->phase == ISDN_FAX_PHASE_A)) {
			idi_reset_fax_stat(chan);

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_BCONN;
			cmd.arg = chan->No;
			ccard->interface.statcallb(&cmd);

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_FCON_I;
			ccard->interface.statcallb(&cmd);
		} else
		if ((chan->fax->code == EDATA_T30_TRAIN_OK) &&
		    (chan->fax->phase == ISDN_FAX_PHASE_A)) {
			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_RID;
			ccard->interface.statcallb(&cmd);

			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_TRAIN_OK;
			ccard->interface.statcallb(&cmd);
		} else
		if ((chan->fax->code == EDATA_T30_TRAIN_OK) &&
		    (chan->fax->phase == ISDN_FAX_PHASE_B)) {
			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_FAXIND;
			cmd.arg = chan->No;
			chan->fax->r_code = ISDN_TTY_FAX_TRAIN_OK;
			ccard->interface.statcallb(&cmd);
		} else
		if (chan->fax->phase == ISDN_FAX_PHASE_C) {
			switch(chan->fax->code) {
				case EDATA_T30_TRAIN_OK:
					idi_send_edata(ccard, chan);
					break;
				case EDATA_T30_MPS:
					chan->fax->fet = 0;
					idi_edata_rcveop(ccard, chan);
					break;
				case EDATA_T30_EOM:
					chan->fax->fet = 1;
					idi_edata_rcveop(ccard, chan);
					break;
				case EDATA_T30_EOP:
					chan->fax->fet = 2;
					idi_edata_rcveop(ccard, chan);
					break;
			}
		}
	}
}

void
fax_put_rcv(eicon_card *ccard, eicon_chan *chan, u_char *Data, int len)
{
	struct sk_buff *skb;
	
        skb = alloc_skb(len + MAX_HEADER_LEN, GFP_ATOMIC);
	if (!skb) {
		eicon_log(ccard, 1, "idi_err: Ch%d: alloc_skb failed in fax_put_rcv()\n", chan->No);
		return;
	}
	skb_reserve(skb, MAX_HEADER_LEN);
	memcpy(skb_put(skb, len), Data, len);
	ccard->interface.rcvcallb_skb(ccard->myid, chan->No, skb);
}

void
idi_faxdata_rcv(eicon_card *ccard, eicon_chan *chan, struct sk_buff *skb)
{
	eicon_OBJBUFFER InBuf;
	eicon_OBJBUFFER LineBuf;
	unsigned int Length = 0;
	unsigned int aLength = 0;
	unsigned int ObjectSize = 0;
	unsigned int ObjHeadLen = 0;
	unsigned int ObjDataLen = 0;
	__u8 Recordtype;
	__u8 PageHeaderLen;	
	__u8 Event;
	eicon_sff_pagehead *ob_page;

	__u16 Cl2Eol = 0x8000;

#	define EVENT_NONE	0
#	define EVENT_NEEDDATA	1

	if (!chan->fax) {
		eicon_log(ccard, 1, "idi_fax: rcvdata with NULL fax struct, ERROR\n");
		return;
	}


	
	if (chan->fax->direction == ISDN_TTY_FAX_CONN_IN) {
		InBuf.Data = skb->data;
		InBuf.Size = skb->len;
		InBuf.Len  = 0;
		InBuf.Next = InBuf.Data;
		LineBuf.Data = chan->fax2.abLine;
		LineBuf.Size = sizeof(chan->fax2.abLine);
		LineBuf.Len  = chan->fax2.LineLen;
		LineBuf.Next = LineBuf.Data + LineBuf.Len;

		Event = EVENT_NONE;
		while (Event == EVENT_NONE) {
			switch(chan->fax2.NextObject) {
				case FAX_OBJECT_DOCU:
						Length = LineBuf.Len + (InBuf.Size - InBuf.Len);
						if (Length < sizeof(eicon_sff_dochead)) {
							Event = EVENT_NEEDDATA;
							break;
						}
						ObjectSize = sizeof(eicon_sff_dochead);
						Length = ObjectSize;
						if (LineBuf.Len < Length) {
							Length -= LineBuf.Len;
							LineBuf.Len = 0;
							LineBuf.Next = LineBuf.Data;
							InBuf.Len += Length;
							InBuf.Next += Length;
						} else {
							LineBuf.Len -= Length;
							LineBuf.Next = LineBuf.Data + LineBuf.Len;
							memmove(LineBuf.Data, LineBuf.Data + Length, LineBuf.Len);
						}
						chan->fax2.PrevObject = FAX_OBJECT_DOCU;
						chan->fax2.NextObject = FAX_OBJECT_PAGE;
					break;

				case FAX_OBJECT_PAGE:
						Length = LineBuf.Len + (InBuf.Size - InBuf.Len);
						if (Length < 2) {
							Event = EVENT_NEEDDATA;
							break;
						}
						if (LineBuf.Len == 0) {
							*LineBuf.Next++ = *InBuf.Next++;
							LineBuf.Len++;
							InBuf.Len++;
						}
						if (LineBuf.Len == 1) {
							*LineBuf.Next++ = *InBuf.Next++;
							LineBuf.Len++;
							InBuf.Len++;
						}
						PageHeaderLen = *(LineBuf.Data + 1);
						ObjectSize = (PageHeaderLen == 0) ? 2 : sizeof(eicon_sff_pagehead);
						if (Length < ObjectSize) {
							Event = EVENT_NEEDDATA;
							break;
						}
						Length = ObjectSize;
						/* extract page dimensions */
						if (LineBuf.Len < Length) {
							aLength = Length - LineBuf.Len;
							memcpy(LineBuf.Next, InBuf.Next, aLength);
							LineBuf.Next += aLength;
							InBuf.Next += aLength;
							LineBuf.Len += aLength;
							InBuf.Len += aLength;
						}
						if (Length > 2) {
							ob_page = (eicon_sff_pagehead *)LineBuf.Data;
							switch(ob_page->linelength) {
								case 2048:
									chan->fax->r_width = 1;
									break;
								case 2432:
									chan->fax->r_width = 2;
									break;
								case 1216:
									chan->fax->r_width = 3;
									break;
								case 864:
									chan->fax->r_width = 4;
									break;
								case 1728:
								default:
									chan->fax->r_width = 0;
							}
							switch(ob_page->pagelength) {
								case 1143:
								case 2287:
									chan->fax->r_length = 0;
									break;
								case 1401:
								case 2802:
									chan->fax->r_length = 1;
									break;
								default:
									chan->fax->r_length = 2;
							}
							eicon_log(ccard, 128, "rSFF-Head: linelength = %d\n", ob_page->linelength);
							eicon_log(ccard, 128, "rSFF-Head: pagelength = %d\n", ob_page->pagelength);
						}
						LineBuf.Len -= Length;
						LineBuf.Next = LineBuf.Data + LineBuf.Len;
						memmove(LineBuf.Data, LineBuf.Data + Length, LineBuf.Len);

						chan->fax2.PrevObject = FAX_OBJECT_PAGE;
						chan->fax2.NextObject = FAX_OBJECT_LINE;
					break;

				case FAX_OBJECT_LINE:
						Length = LineBuf.Len + (InBuf.Size - InBuf.Len);
						if (Length < 1) {
							Event = EVENT_NEEDDATA;
							break;
						}
						if (LineBuf.Len == 0) {
							*LineBuf.Next++ = *InBuf.Next++;
							LineBuf.Len++;
							InBuf.Len++;
						}
						Recordtype = *LineBuf.Data;
						if (Recordtype == 0) {
							/* recordtype pixel row (2 byte length) */
							ObjHeadLen = 3;
							if (Length < ObjHeadLen) {
								Event = EVENT_NEEDDATA;
								break;
							}
							while (LineBuf.Len < ObjHeadLen) {
								*LineBuf.Next++ = *InBuf.Next++;
								LineBuf.Len++;
								InBuf.Len++;
							}
							ObjDataLen = *((__u16*) (LineBuf.Data + 1));
							ObjectSize = ObjHeadLen + ObjDataLen;
							if (Length < ObjectSize) {
								Event = EVENT_NEEDDATA;
								break;
							}
						} else
						if ((Recordtype >= 1) && (Recordtype <= 216)) {
							/* recordtype pixel row (1 byte length) */
							ObjHeadLen = 1;
							ObjDataLen = Recordtype;
							ObjectSize = ObjHeadLen + ObjDataLen;
							if (Length < ObjectSize) {
								Event = EVENT_NEEDDATA;
								break;
							}
						} else
						if ((Recordtype >= 217) && (Recordtype <= 253)) {
							/* recordtype empty lines */
							ObjHeadLen = 1;
							ObjDataLen = 0;
							ObjectSize = ObjHeadLen + ObjDataLen;
							LineBuf.Len--;
							LineBuf.Next = LineBuf.Data + LineBuf.Len;
							memmove(LineBuf.Data, LineBuf.Data + 1, LineBuf.Len);
							break;
						} else
						if (Recordtype == 254) {
							/* recordtype page header */
							chan->fax2.PrevObject = FAX_OBJECT_LINE;
							chan->fax2.NextObject = FAX_OBJECT_PAGE;
							break;
						} else {
							/* recordtype user information */
							ObjHeadLen = 2;
							if (Length < ObjHeadLen) {
								Event = EVENT_NEEDDATA;
								break;
							}
							while (LineBuf.Len < ObjHeadLen) {
								*LineBuf.Next++ = *InBuf.Next++;
								LineBuf.Len++;
								InBuf.Len++;
							}
							ObjDataLen = *(LineBuf.Data + 1);
							ObjectSize = ObjHeadLen + ObjDataLen;
							if (ObjDataLen == 0) {
								/* illegal line coding */
								LineBuf.Len -= ObjHeadLen;
								LineBuf.Next = LineBuf.Data + LineBuf.Len;
								memmove(LineBuf.Data, LineBuf.Data + ObjHeadLen, LineBuf.Len);
								break;
							} else {
								/* user information */
								if (Length < ObjectSize) {
									Event = EVENT_NEEDDATA;
									break;
								}
								Length = ObjectSize;
								if (LineBuf.Len < Length) {
									Length -= LineBuf.Len;
									LineBuf.Len = 0;
									LineBuf.Next = LineBuf.Data;
									InBuf.Len += Length;
									InBuf.Next += Length;
								} else {
									LineBuf.Len -= Length;
									LineBuf.Next = LineBuf.Data + LineBuf.Len;
									memmove(LineBuf.Data, LineBuf.Data + Length, LineBuf.Len);
								}
							}
							break;	
						}
						Length = ObjectSize;
						if (LineBuf.Len > ObjHeadLen) {
							fax_put_rcv(ccard, chan, LineBuf.Data + ObjHeadLen,
									(LineBuf.Len - ObjHeadLen));
						}
						Length -= LineBuf.Len;
						LineBuf.Len = 0;
						LineBuf.Next = LineBuf.Data;
						if (Length > 0) {
							fax_put_rcv(ccard, chan, InBuf.Next, Length);
							InBuf.Len += Length;
							InBuf.Next += Length;
						}
						fax_put_rcv(ccard, chan, (__u8 *)&Cl2Eol, sizeof(Cl2Eol));
					break;
			} /* end of switch (chan->fax2.NextObject) */
		} /* end of while (Event==EVENT_NONE) */
		if (InBuf.Len < InBuf.Size) {
			Length = InBuf.Size - InBuf.Len;
			if ((LineBuf.Len + Length) > LineBuf.Size) {
				eicon_log(ccard, 1, "idi_fax: Ch%d: %d bytes dropping, small buffer\n", chan->No,
					Length);
				} else {
					memcpy(LineBuf.Next, InBuf.Next, Length);
					LineBuf.Len += Length;
				}
		}
		chan->fax2.LineLen = LineBuf.Len;
	} else { /* CONN_OUT */
		/* On CONN_OUT we do not need incoming data, drop it */
		/* maybe later for polling */
	}

#	undef EVENT_NONE
#	undef EVENT_NEEDDATA

	return;
}

int
idi_fax_send_outbuf(eicon_card *ccard, eicon_chan *chan, eicon_OBJBUFFER *OutBuf)
{
	struct sk_buff *skb;

	skb = alloc_skb(OutBuf->Len, GFP_ATOMIC);
	if (!skb) {
		eicon_log(ccard, 1, "idi_err: Ch%d: alloc_skb failed in fax_send_outbuf()\n", chan->No);
		return(-1);
	}
	memcpy(skb_put(skb, OutBuf->Len), OutBuf->Data, OutBuf->Len);

	OutBuf->Len = 0;
	OutBuf->Next = OutBuf->Data;

	return(idi_send_data(ccard, chan, 0, skb, 1));
}

int
idi_faxdata_send(eicon_card *ccard, eicon_chan *chan, struct sk_buff *skb)
{
	isdn_ctrl cmd;
	eicon_OBJBUFFER InBuf;
	__u8 InData;
	__u8 InMask;
	eicon_OBJBUFFER OutBuf;
	eicon_OBJBUFFER LineBuf;
	__u32 LineData;
	unsigned int LineDataLen;
	__u8 Byte;
	__u8 Event;
	int ret = 1;

#	define EVENT_NONE	0
#	define EVENT_EOD	1
#	define EVENT_EOL 	2
#	define EVENT_EOP 	3

	if ((!ccard) || (!chan))
		return -1;

	if (!chan->fax) {
		eicon_log(ccard, 1, "idi_fax: senddata with NULL fax struct, ERROR\n");
		return -1;
	}

	if (chan->fax->direction == ISDN_TTY_FAX_CONN_IN) {
		/* Simply ignore any data written in data mode when receiving a fax.    */
		/* This is not completely correct because only XON's should come here.  */
        	dev_kfree_skb(skb);
		return 1;
	}

	if (chan->fax->phase != ISDN_FAX_PHASE_C) {
        	dev_kfree_skb(skb);
		return 1;
	}

        if (chan->queued + skb->len > 1200)
                return 0;

	InBuf.Data = skb->data;
	InBuf.Size = skb->len;
	InBuf.Len  = 0;
	InBuf.Next = InBuf.Data;
	InData = 0;
	InMask = 0;

	LineBuf.Data = chan->fax2.abLine;
	LineBuf.Size = sizeof(chan->fax2.abLine);
	LineBuf.Len  = chan->fax2.LineLen;
	LineBuf.Next = LineBuf.Data + LineBuf.Len;
	LineData = chan->fax2.LineData;
	LineDataLen = chan->fax2.LineDataLen;

	OutBuf.Data = chan->fax2.abFrame;
	OutBuf.Size = sizeof(chan->fax2.abFrame);
	OutBuf.Len = 0;
	OutBuf.Next = OutBuf.Data;

	Event = EVENT_NONE;

	chan->fax2.Eop = 0;

	for (;;) {
	  for (;;) {
		if (InMask == 0) {
			if (InBuf.Len >= InBuf.Size) {
				Event = EVENT_EOD;
				break;
			}
			if ((chan->fax2.Dle != _DLE_) && *InBuf.Next == _DLE_) {
				chan->fax2.Dle = _DLE_;
				InBuf.Next++;
				InBuf.Len++;
				if (InBuf.Len >= InBuf.Size) {
					Event = EVENT_EOD;
					break;
				}
			}
			if (chan->fax2.Dle == _DLE_) {
				chan->fax2.Dle = 0;
				if (*InBuf.Next == _ETX_) {
					Event = EVENT_EOP;
					break;
				} else
				if (*InBuf.Next == _DLE_) {
					/* do nothing */
				} else {
					eicon_log(ccard, 1,
						"idi_err: Ch%d: unknown DLE escape %02x found\n",
							chan->No, *InBuf.Next);
					InBuf.Next++;
					InBuf.Len++;
					if (InBuf.Len >= InBuf.Size) {
						Event = EVENT_EOD;
						break;
					}
				}
			}
			InBuf.Len++;
			InData = *InBuf.Next++;
			InMask = (chan->fax->bor) ? 0x80 : 0x01;
		}
		while (InMask) {
			LineData >>= 1;
			LineDataLen++;
			if (InData & InMask)
				LineData |= 0x80000000;
			if (chan->fax->bor)
				InMask >>= 1;
			else
				InMask <<= 1;

			if ((LineDataLen >= T4_EOL_BITSIZE) &&
			   ((LineData & T4_EOL_MASK_DWORD) == T4_EOL_DWORD)) {
				Event = EVENT_EOL;
				if (LineDataLen > T4_EOL_BITSIZE) {
					Byte = (__u8)
						((LineData & ~T4_EOL_MASK_DWORD) >>
						(32 - LineDataLen));
					if (Byte == 0) {
						if (! chan->fax2.NullByteExist) {
							chan->fax2.NullBytesPos = LineBuf.Len;
							chan->fax2.NullByteExist = 1;
						}
					} else {
						chan->fax2.NullByteExist = 0;
					}
					if (LineBuf.Len < LineBuf.Size) {
						*LineBuf.Next++  = Byte;
						LineBuf.Len++;
					}
				}
				LineDataLen = 0;
				break;
			}
			if (LineDataLen >= T4_EOL_BITSIZE + 8) {
				Byte = (__u8)
					((LineData & ~T4_EOL_MASK_DWORD) >>
					(32 - T4_EOL_BITSIZE - 8));
				LineData &= T4_EOL_MASK_DWORD;
				LineDataLen = T4_EOL_BITSIZE;
				if (Byte == 0) {
					if (! chan->fax2.NullByteExist) {
						chan->fax2.NullBytesPos = LineBuf.Len;
						chan->fax2.NullByteExist = 1;
					}
				} else {
					chan->fax2.NullByteExist = 0;
				}
				if (LineBuf.Len < LineBuf.Size) {
					*LineBuf.Next++  = Byte; 
					LineBuf.Len++;
				}
			}
		}
		if (Event != EVENT_NONE)
			break;
	  }

		if ((Event != EVENT_EOL) && (Event != EVENT_EOP))
			break;

		if ((Event == EVENT_EOP) && (LineDataLen > 0)) {
			LineData >>= 32 - LineDataLen;
			LineDataLen = 0;
			while (LineData != 0) {
				Byte = (__u8) LineData;
				LineData >>= 8;
				if (Byte == 0) {
					if (! chan->fax2.NullByteExist) {
						chan->fax2.NullBytesPos = LineBuf.Len;
						chan->fax2.NullByteExist = 1;
					}
				} else {
					chan->fax2.NullByteExist = 0;
				}
				if (LineBuf.Len < LineBuf.Size) {
					*LineBuf.Next++  = Byte;
					LineBuf.Len++;
				}
				
			}
		}
		if (chan->fax2.NullByteExist) {
			if (chan->fax2.NullBytesPos == 0) {
				LineBuf.Len = 0;
			} else {
				LineBuf.Len = chan->fax2.NullBytesPos + 1;
			}
		}
		if (LineBuf.Len > 0) {
			if (OutBuf.Len + LineBuf.Len + SFF_LEN_FLD_SIZE > OutBuf.Size) {
				ret = idi_fax_send_outbuf(ccard, chan, &OutBuf);
			}
			if (LineBuf.Len <= 216) {
				*OutBuf.Next++ = (__u8) LineBuf.Len;
				OutBuf.Len++;
			} else {
				*OutBuf.Next++ = 0;
				*((__u16 *) OutBuf.Next)++ = (__u16) LineBuf.Len;
				OutBuf.Len += 3;
			}
			memcpy(OutBuf.Next, LineBuf.Data, LineBuf.Len);
			OutBuf.Next += LineBuf.Len;
			OutBuf.Len  += LineBuf.Len;
		}
		LineBuf.Len = 0;
		LineBuf.Next = LineBuf.Data;
		chan->fax2.NullByteExist = 0;
		if (Event == EVENT_EOP)
			break;

		Event = EVENT_NONE;
	}

	if (Event == EVENT_EOP) {
		chan->fax2.Eop = 1;
		chan->fax2.PageCount++;
		cmd.driver = ccard->myid;
		cmd.command = ISDN_STAT_FAXIND;
		cmd.arg = chan->No;
		chan->fax->r_code = ISDN_TTY_FAX_EOP;
		ccard->interface.statcallb(&cmd);
	}
	if (OutBuf.Len > 0) {
		ret = idi_fax_send_outbuf(ccard, chan, &OutBuf);
	}

	chan->fax2.LineLen = LineBuf.Len;
	chan->fax2.LineData = LineData;
	chan->fax2.LineDataLen = LineDataLen;

#	undef EVENT_NONE
#	undef EVENT_EOD
#	undef EVENT_EOL
#	undef EVENT_EOP

	if (ret >= 0)
	        dev_kfree_skb(skb);
	if (ret == 0)
		ret = 1;
	return(ret);
}

void
idi_fax_hangup(eicon_card *ccard, eicon_chan *chan)
{
	isdn_ctrl cmd;

	if (!chan->fax) {
		eicon_log(ccard, 1, "idi_fax: hangup with NULL fax struct, ERROR\n");
		return;
	}
	if ((chan->fax->direction == ISDN_TTY_FAX_CONN_OUT) &&
	    (chan->fax->code == 0)) {
		cmd.driver = ccard->myid;
		cmd.command = ISDN_STAT_FAXIND;
		cmd.arg = chan->No;
		chan->fax->r_code = ISDN_TTY_FAX_PTS;
		ccard->interface.statcallb(&cmd);
	}
	if ((chan->fax->code > 1) && (chan->fax->code < 120))
		chan->fax->code += 120;
	chan->fax->r_code = ISDN_TTY_FAX_HNG;
	cmd.driver = ccard->myid;
	cmd.command = ISDN_STAT_FAXIND;
	cmd.arg = chan->No;
	ccard->interface.statcallb(&cmd);
}

#endif	/******** FAX ********/

int
idi_send_udata(eicon_card *card, eicon_chan *chan, int UReq, u_char *buffer, int len)
{
	struct sk_buff *skb;
	struct sk_buff *skb2;
	eicon_REQ *reqbuf;
	eicon_chan_ptr *chan2;

	if ((chan->fsm_state == EICON_STATE_NULL) || (chan->fsm_state == EICON_STATE_LISTEN)) {
		eicon_log(card, 1, "idi_snd: Ch%d: send udata on state %d !\n", chan->No, chan->fsm_state);
		return -ENODEV;
	}
	eicon_log(card, 8, "idi_snd: Ch%d: udata 0x%x: %d %d %d %d\n", chan->No,
			UReq, buffer[0], buffer[1], buffer[2], buffer[3]);

	skb = alloc_skb(sizeof(eicon_REQ) + len + 1, GFP_ATOMIC);
	skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

	if ((!skb) || (!skb2)) {
		eicon_log(card, 1, "idi_err: Ch%d: alloc_skb failed in send_udata()\n", chan->No);
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
		return -ENOMEM;
	}

	chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
	chan2->ptr = chan;

	reqbuf = (eicon_REQ *)skb_put(skb, 1 + len + sizeof(eicon_REQ));

	reqbuf->Req = IDI_N_UDATA;
	reqbuf->ReqCh = 0;
	reqbuf->ReqId = 1;

	reqbuf->XBuffer.length = len + 1;
	reqbuf->XBuffer.P[0] = UReq;
	memcpy(&reqbuf->XBuffer.P[1], buffer, len);
	reqbuf->Reference = 1; /* Net Entity */

	skb_queue_tail(&chan->e.X, skb);
	skb_queue_tail(&card->sndq, skb2);
	eicon_schedule_tx(card);
	return (0);
}

void
idi_audio_cmd(eicon_card *ccard, eicon_chan *chan, int cmd, u_char *value)
{
	u_char buf[6];
	struct enable_dtmf_s *dtmf_buf = (struct enable_dtmf_s *)buf;

	if ((!ccard) || (!chan))
		return;

	memset(buf, 0, 6);
	switch(cmd) {
		case ISDN_AUDIO_SETDD:
			if (value[0]) {
				dtmf_buf->tone = (__u16) (value[1] * 5);
				dtmf_buf->gap = (__u16) (value[1] * 5);
				idi_send_udata(ccard, chan,
					DSP_UDATA_REQUEST_ENABLE_DTMF_RECEIVER,
					buf, 4);
			} else {
				idi_send_udata(ccard, chan,
					DSP_UDATA_REQUEST_DISABLE_DTMF_RECEIVER,
					buf, 0);
			}
			break;
	}
}

void
idi_parse_udata(eicon_card *ccard, eicon_chan *chan, unsigned char *buffer, int len)
{
	isdn_ctrl cmd;
	eicon_dsp_ind *p = (eicon_dsp_ind *) (&buffer[1]);
        static char *connmsg[] =
        {"", "V.21", "V.23", "V.22", "V.22bis", "V.32bis", "V.34",
         "V.8", "Bell 212A", "Bell 103", "V.29 Leased", "V.33 Leased", "V.90",
         "V.21 CH2", "V.27ter", "V.29", "V.33", "V.17"};
	static u_char dtmf_code[] = {
	'1','4','7','*','2','5','8','0','3','6','9','#','A','B','C','D'
	};

	if ((!ccard) || (!chan))
		return;

	switch (buffer[0]) {
		case DSP_UDATA_INDICATION_SYNC:
			eicon_log(ccard, 16, "idi_ind: Ch%d: UDATA_SYNC time %d\n", chan->No, p->time);
			break;
		case DSP_UDATA_INDICATION_DCD_OFF:
			eicon_log(ccard, 8, "idi_ind: Ch%d: UDATA_DCD_OFF time %d\n", chan->No, p->time);
			break;
		case DSP_UDATA_INDICATION_DCD_ON:
			if ((chan->l2prot == ISDN_PROTO_L2_MODEM) &&
			    (chan->fsm_state == EICON_STATE_WMCONN)) {
				chan->fsm_state = EICON_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				sprintf(cmd.parm.num, "%d/%s", p->speed, connmsg[p->norm]);
				ccard->interface.statcallb(&cmd);
			}
			eicon_log(ccard, 8, "idi_ind: Ch%d: UDATA_DCD_ON time %d\n", chan->No, p->time);
			eicon_log(ccard, 8, "idi_ind: Ch%d: %d %d %d %d\n", chan->No,
				p->norm, p->options, p->speed, p->delay); 
			break;
		case DSP_UDATA_INDICATION_CTS_OFF:
			eicon_log(ccard, 8, "idi_ind: Ch%d: UDATA_CTS_OFF time %d\n", chan->No, p->time);
			break;
		case DSP_UDATA_INDICATION_CTS_ON:
			eicon_log(ccard, 8, "idi_ind: Ch%d: UDATA_CTS_ON time %d\n", chan->No, p->time);
			eicon_log(ccard, 8, "idi_ind: Ch%d: %d %d %d %d\n", chan->No,
				p->norm, p->options, p->speed, p->delay); 
			break;
		case DSP_UDATA_INDICATION_DISCONNECT:
			eicon_log(ccard, 8, "idi_ind: Ch%d: UDATA_DISCONNECT cause %d\n", chan->No, buffer[1]);
			break;
		case DSP_UDATA_INDICATION_DTMF_DIGITS_RECEIVED:
			eicon_log(ccard, 8, "idi_ind: Ch%d: UDATA_DTMF_REC '%c'\n", chan->No,
				dtmf_code[buffer[1]]);
			cmd.driver = ccard->myid;
			cmd.command = ISDN_STAT_AUDIO;
			cmd.parm.num[0] = ISDN_AUDIO_DTMF;
			cmd.parm.num[1] = dtmf_code[buffer[1]];
			cmd.arg = chan->No;
			ccard->interface.statcallb(&cmd);
			break;
		default:
			eicon_log(ccard, 8, "idi_ind: Ch%d: UNHANDLED UDATA Indication 0x%02x\n", chan->No, buffer[0]);
	}
}

void
idi_handle_ind(eicon_card *ccard, struct sk_buff *skb)
{
	int tmp;
	char tnum[64];
	int dlev;
	int free_buff;
	ulong flags;
	struct sk_buff *skb2;
        eicon_IND *ind = (eicon_IND *)skb->data;
	eicon_chan *chan;
	idi_ind_message message;
	isdn_ctrl cmd;

	if (!ccard) {
		eicon_log(ccard, 1, "idi_err: Ch??: null card in handle_ind\n");
  		dev_kfree_skb(skb);
		return;
	}

	if ((chan = ccard->IdTable[ind->IndId]) == NULL) {
		eicon_log(ccard, 1, "idi_err: Ch??: null chan in handle_ind\n");
  		dev_kfree_skb(skb);
		return;
	}
	
	if (ind->Ind != 8)
		dlev = 144;
	else
		dlev = 128;

       	eicon_log(ccard, dlev, "idi_hdl: Ch%d: Ind=%d Id=%x Ch=%d MInd=%d MLen=%d Len=%d\n", chan->No,
	        ind->Ind,ind->IndId,ind->IndCh,ind->MInd,ind->MLength,ind->RBuffer.length);

	free_buff = 1;
	/* Signal Layer */
	if (chan->e.D3Id == ind->IndId) {
		idi_IndParse(ccard, chan, &message, ind->RBuffer.P, ind->RBuffer.length);
		switch(ind->Ind) {
			case HANGUP:
				eicon_log(ccard, 8, "idi_ind: Ch%d: Hangup\n", chan->No);
		                while((skb2 = skb_dequeue(&chan->e.X))) {
					dev_kfree_skb(skb2);
				}
				save_flags(flags);
				cli();
				chan->queued = 0;
				chan->waitq = 0;
				chan->waitpq = 0;
				restore_flags(flags);
				if (message.e_cau[0] & 0x7f) {
					cmd.driver = ccard->myid;
					cmd.arg = chan->No;
					sprintf(cmd.parm.num,"E%02x%02x", 
						chan->cause[0]&0x7f, message.e_cau[0]&0x7f); 
					cmd.command = ISDN_STAT_CAUSE;
					ccard->interface.statcallb(&cmd);
				}
				chan->cause[0] = 0; 
#ifdef CONFIG_ISDN_TTY_FAX
				if (!chan->e.B2Id)
					chan->fax = 0;
#endif
				if (((chan->fsm_state == EICON_STATE_ACTIVE) ||
				    (chan->fsm_state == EICON_STATE_WMCONN)) ||
				    ((chan->l2prot == ISDN_PROTO_L2_FAX) &&
				    (chan->fsm_state == EICON_STATE_OBWAIT))) {
					chan->fsm_state = EICON_STATE_NULL;
				} else {
					if (chan->e.B2Id)
						idi_do_req(ccard, chan, REMOVE, 1);
					chan->fsm_state = EICON_STATE_NULL;
					cmd.driver = ccard->myid;
					cmd.arg = chan->No;
					cmd.command = ISDN_STAT_DHUP;
					ccard->interface.statcallb(&cmd);
					eicon_idi_listen_req(ccard, chan);
				}
				break;
			case INDICATE_IND:
				eicon_log(ccard, 8, "idi_ind: Ch%d: Indicate_Ind\n", chan->No);
				if (chan->fsm_state != EICON_STATE_LISTEN) {
					eicon_log(ccard, 1, "idi_err: Ch%d: Incoming call on wrong state (%d).\n",
						chan->No, chan->fsm_state);
					idi_do_req(ccard, chan, HANGUP, 0);
					break;
				}
				chan->fsm_state = EICON_STATE_ICALL;
				idi_bc2si(message.bc, message.hlc, &chan->si1, &chan->si2);
				strcpy(chan->cpn, message.cpn + 1);
				strcpy(chan->oad, message.oad);
				strcpy(chan->dsa, message.dsa);
				strcpy(chan->osa, message.osa);
				chan->plan = message.plan;
				chan->screen = message.screen;
				try_stat_icall_again: 
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_ICALL;
				cmd.arg = chan->No;
				cmd.parm.setup.si1 = chan->si1;
				cmd.parm.setup.si2 = chan->si2;
				strcpy(tnum, chan->cpn);
				if (strlen(chan->dsa)) {
					strcat(tnum, ".");
					strcat(tnum, chan->dsa);
				}
				tnum[ISDN_MSNLEN - 1] = 0;
				strcpy(cmd.parm.setup.eazmsn, tnum);
				strcpy(tnum, chan->oad);
				if (strlen(chan->osa)) {
					strcat(tnum, ".");
					strcat(tnum, chan->osa);
				}
				tnum[ISDN_MSNLEN - 1] = 0;
				strcpy(cmd.parm.setup.phone, tnum);
				cmd.parm.setup.plan = chan->plan;
				cmd.parm.setup.screen = chan->screen;
				tmp = ccard->interface.statcallb(&cmd);
				switch(tmp) {
					case 0: /* no user responding */
						idi_do_req(ccard, chan, HANGUP, 0);
						chan->fsm_state = EICON_STATE_NULL;
						break;
					case 1: /* alert */
						eicon_log(ccard, 8, "idi_req: Ch%d: Call Alert\n", chan->No);
						if ((chan->fsm_state == EICON_STATE_ICALL) || (chan->fsm_state == EICON_STATE_ICALLW)) {
							chan->fsm_state = EICON_STATE_ICALL;
							idi_do_req(ccard, chan, CALL_ALERT, 0);
						}
						break;
					case 2: /* reject */
						eicon_log(ccard, 8, "idi_req: Ch%d: Call Reject\n", chan->No);
						idi_do_req(ccard, chan, REJECT, 0);
						break;
					case 3: /* incomplete number */
						eicon_log(ccard, 8, "idi_req: Ch%d: Incomplete Number\n", chan->No);
					        switch(ccard->type) {
					                case EICON_CTYPE_MAESTRAP:
					                case EICON_CTYPE_S2M:
								/* TODO (other protocols) */
								chan->fsm_state = EICON_STATE_ICALLW;
								break;
							default:
								idi_do_req(ccard, chan, HANGUP, 0);
						}
						break;
				}
				break;
			case INFO_IND:
				eicon_log(ccard, 8, "idi_ind: Ch%d: Info_Ind\n", chan->No);
				if ((chan->fsm_state == EICON_STATE_ICALLW) &&
				    (message.cpn[0])) {
					strcat(chan->cpn, message.cpn + 1);
					goto try_stat_icall_again;
				}
				break;
			case CALL_IND:
				eicon_log(ccard, 8, "idi_ind: Ch%d: Call_Ind\n", chan->No);
				if ((chan->fsm_state == EICON_STATE_ICALL) || (chan->fsm_state == EICON_STATE_IWAIT)) {
					chan->fsm_state = EICON_STATE_IBWAIT;
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_DCONN;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
					switch(chan->l2prot) {
						case ISDN_PROTO_L2_FAX:
#ifdef CONFIG_ISDN_TTY_FAX
							if (chan->fax)
								chan->fax->phase = ISDN_FAX_PHASE_A;
#endif
							break;
						case ISDN_PROTO_L2_MODEM:
							/* do nothing, wait for connect */
							break;
						default:
							idi_do_req(ccard, chan, IDI_N_CONNECT, 1);
					}
				} else
					idi_hangup(ccard, chan);
				break;
			case CALL_CON:
				eicon_log(ccard, 8, "idi_ind: Ch%d: Call_Con\n", chan->No);
				if (chan->fsm_state == EICON_STATE_OCALL) {
					chan->fsm_state = EICON_STATE_OBWAIT;
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_DCONN;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);

					/* check if old NetID has been removed */
					if (chan->e.B2Id) {
						eicon_log(ccard, 1, "eicon: Ch%d: old net_id %x still exist, removing.\n",
							chan->No, chan->e.B2Id);
						idi_do_req(ccard, chan, REMOVE, 1);
					}

					idi_do_req(ccard, chan, ASSIGN, 1); 
					idi_do_req(ccard, chan, IDI_N_CONNECT, 1);
#ifdef CONFIG_ISDN_TTY_FAX
					if (chan->l2prot == ISDN_PROTO_L2_FAX) {
						if (chan->fax)
							chan->fax->phase = ISDN_FAX_PHASE_A;
					}
#endif
				} else
				idi_hangup(ccard, chan);
				break;
			case AOC_IND:
				eicon_log(ccard, 8, "idi_ind: Ch%d: Advice of Charge\n", chan->No);
				break;
			default:
				eicon_log(ccard, 8, "idi_ind: Ch%d: UNHANDLED SigIndication 0x%02x\n", chan->No, ind->Ind);
		}
	}
	/* Network Layer */
	else if (chan->e.B2Id == ind->IndId) {

		if (chan->No == ccard->nchannels) {
			/* Management Indication */
			idi_IndParse(ccard, chan, &message, ind->RBuffer.P, ind->RBuffer.length);
			chan->fsm_state = 1;
		} 
		else
		switch(ind->Ind) {
			case IDI_N_CONNECT_ACK:
				eicon_log(ccard, 16, "idi_ind: Ch%d: N_Connect_Ack\n", chan->No);
				if (chan->l2prot == ISDN_PROTO_L2_MODEM) {
					chan->fsm_state = EICON_STATE_WMCONN;
					break;
				}
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
#ifdef CONFIG_ISDN_TTY_FAX
					chan->fsm_state = EICON_STATE_ACTIVE;
					idi_parse_edata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
					if (chan->fax) {
						if (chan->fax->phase == ISDN_FAX_PHASE_B) {
							idi_fax_send_header(ccard, chan, 2);
							cmd.driver = ccard->myid;
							cmd.command = ISDN_STAT_FAXIND;
							cmd.arg = chan->No;
							chan->fax->r_code = ISDN_TTY_FAX_DCS;
							ccard->interface.statcallb(&cmd);
						}
					}
					else {
						eicon_log(ccard, 1, "idi_ind: N_CONNECT_ACK with NULL fax struct, ERROR\n");
					}
#endif
					break;
				}
				chan->fsm_state = EICON_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break; 
			case IDI_N_CONNECT:
				eicon_log(ccard, 16,"idi_ind: Ch%d: N_Connect\n", chan->No);
				if (chan->e.B2Id) idi_do_req(ccard, chan, IDI_N_CONNECT_ACK, 1);
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
					break;
				}
				if (chan->l2prot == ISDN_PROTO_L2_MODEM) {
					chan->fsm_state = EICON_STATE_WMCONN;
					break;
				}
				chan->fsm_state = EICON_STATE_ACTIVE;
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_BCONN;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
				break; 
			case IDI_N_DISC:
				eicon_log(ccard, 16, "idi_ind: Ch%d: N_DISC\n", chan->No);
				if (chan->e.B2Id) {
		                	while((skb2 = skb_dequeue(&chan->e.X))) {
						dev_kfree_skb(skb2);
					}
					idi_do_req(ccard, chan, IDI_N_DISC_ACK, 1);
					idi_do_req(ccard, chan, REMOVE, 1);
				}
#ifdef CONFIG_ISDN_TTY_FAX
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
					idi_parse_edata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
					idi_fax_hangup(ccard, chan);
				}
#endif
				save_flags(flags);
				cli();
				chan->queued = 0;
				chan->waitq = 0;
				chan->waitpq = 0;
				restore_flags(flags);
				idi_do_req(ccard, chan, HANGUP, 0);
				if (chan->fsm_state == EICON_STATE_ACTIVE) {
					cmd.driver = ccard->myid;
					cmd.command = ISDN_STAT_BHUP;
					cmd.arg = chan->No;
					ccard->interface.statcallb(&cmd);
					chan->fsm_state = EICON_STATE_NULL;
				}
#ifdef CONFIG_ISDN_TTY_FAX
				chan->fax = 0;
#endif
				break; 
			case IDI_N_DISC_ACK:
				eicon_log(ccard, 16, "idi_ind: Ch%d: N_DISC_ACK\n", chan->No);
#ifdef CONFIG_ISDN_TTY_FAX
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
					idi_parse_edata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
					idi_fax_hangup(ccard, chan);
				}
#endif
				break; 
			case IDI_N_DATA_ACK:
				eicon_log(ccard, 16, "idi_ind: Ch%d: N_DATA_ACK\n", chan->No);
				break;
			case IDI_N_DATA:
				skb_pull(skb, sizeof(eicon_IND) - 1);
				eicon_log(ccard, 128, "idi_rcv: Ch%d: %d bytes\n", chan->No, skb->len);
				if (chan->l2prot == ISDN_PROTO_L2_FAX) {
#ifdef CONFIG_ISDN_TTY_FAX
					idi_faxdata_rcv(ccard, chan, skb);
#endif
				} else {
					ccard->interface.rcvcallb_skb(ccard->myid, chan->No, skb);
					free_buff = 0; 
				}
				break; 
			case IDI_N_UDATA:
				idi_parse_udata(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
				break; 
#ifdef CONFIG_ISDN_TTY_FAX
			case IDI_N_EDATA:
				idi_edata_action(ccard, chan, ind->RBuffer.P, ind->RBuffer.length);
				break; 
#endif
			default:
				eicon_log(ccard, 8, "idi_ind: Ch%d: UNHANDLED NetIndication 0x%02x\n", chan->No, ind->Ind);
		}
	}
	else {
		eicon_log(ccard, 1, "idi_ind: Ch%d: Ind is neither SIG nor NET !\n", chan->No);
	}
   if (free_buff)
	dev_kfree_skb(skb);
}

int
idi_handle_ack_ok(eicon_card *ccard, eicon_chan *chan, eicon_RC *ack)
{
	ulong flags;
	isdn_ctrl cmd;

	if (ack->RcId != ((chan->e.ReqCh) ? chan->e.B2Id : chan->e.D3Id)) {
		/* I dont know why this happens, just ignoring this RC */
		eicon_log(ccard, 16, "idi_ack: Ch%d: RcId %d not equal to last %d\n", chan->No, 
			ack->RcId, (chan->e.ReqCh) ? chan->e.B2Id : chan->e.D3Id);
		return 1;
	}

	/* Management Interface */	
	if (chan->No == ccard->nchannels) {
		/* Managementinterface: changing state */
		if (chan->e.Req == 0x04)
			chan->fsm_state = 1;
	}

	/* Remove an Id */
	if (chan->e.Req == REMOVE) {
		if (ack->Reference != chan->e.ref) {
			eicon_log(ccard, 16, "idi_ack: Ch%d: Rc-Ref %d not equal to stored %d\n", chan->No,
				ack->Reference, chan->e.ref);
			return 0;
		}
		save_flags(flags);
		cli();
		ccard->IdTable[ack->RcId] = NULL;
		eicon_log(ccard, 16, "idi_ack: Ch%d: Removed : Id=%x Ch=%d (%s)\n", chan->No,
			ack->RcId, ack->RcCh, (chan->e.ReqCh)? "Net":"Sig");
		if (!chan->e.ReqCh) 
			chan->e.D3Id = 0;
		else
			chan->e.B2Id = 0;
		restore_flags(flags);
		return 1;
	}

	/* Signal layer */
	if (!chan->e.ReqCh) {
		eicon_log(ccard, 16, "idi_ack: Ch%d: RC OK Id=%x Ch=%d (ref:%d)\n", chan->No,
			ack->RcId, ack->RcCh, ack->Reference);
	} else {
	/* Network layer */
		switch(chan->e.Req & 0x0f) {
			case IDI_N_MDATA:
			case IDI_N_DATA:
				if ((chan->e.Req & 0x0f) == IDI_N_DATA) {
					if (chan->queued) {
						cmd.driver = ccard->myid;
						cmd.command = ISDN_STAT_BSENT;
						cmd.arg = chan->No;
						cmd.parm.length = chan->waitpq;
						ccard->interface.statcallb(&cmd);
					}
					save_flags(flags);
					cli();
					chan->waitpq = 0;
					restore_flags(flags);
#ifdef CONFIG_ISDN_TTY_FAX
					if (chan->l2prot == ISDN_PROTO_L2_FAX) {
						if (((chan->queued - chan->waitq) < 1) &&
						    (chan->fax2.Eop)) {
							chan->fax2.Eop = 0;
							if (chan->fax) {
								cmd.driver = ccard->myid;
								cmd.command = ISDN_STAT_FAXIND;
								cmd.arg = chan->No;
								chan->fax->r_code = ISDN_TTY_FAX_SENT;
								ccard->interface.statcallb(&cmd);
							}
							else {
								eicon_log(ccard, 1, "idi_ack: Sent with NULL fax struct, ERROR\n");
							}
						}
					}
#endif
				}
				save_flags(flags);
				cli();
				chan->queued -= chan->waitq;
				if (chan->queued < 0) chan->queued = 0;
				restore_flags(flags);
				break;
			default:
				eicon_log(ccard, 16, "idi_ack: Ch%d: RC OK Id=%x Ch=%d (ref:%d)\n", chan->No,
					ack->RcId, ack->RcCh, ack->Reference);
		}
	}
	return 1;
}

void
idi_handle_ack(eicon_card *ccard, struct sk_buff *skb)
{
	int j;
	ulong flags;
        eicon_RC *ack = (eicon_RC *)skb->data;
	eicon_chan *chan;
	isdn_ctrl cmd;
	int dCh = -1;

	if (!ccard) {
		eicon_log(ccard, 1, "idi_err: Ch??: null card in handle_ack\n");
		dev_kfree_skb(skb);
		return;
	}

	save_flags(flags);
	cli();
	if ((chan = ccard->IdTable[ack->RcId]) != NULL)
		dCh = chan->No;
	restore_flags(flags);

	switch (ack->Rc) {
		case OK_FC:
		case N_FLOW_CONTROL:
		case ASSIGN_RC:
			eicon_log(ccard, 1, "idi_ack: Ch%d: unhandled RC 0x%x\n",
				dCh, ack->Rc);
			break;
		case READY_INT:
		case TIMER_INT:
			/* we do nothing here */
			break;

		case OK:
			if (!chan) {
				eicon_log(ccard, 1, "idi_ack: Ch%d: OK on chan without Id\n", dCh);
				break;
			}
			if (!idi_handle_ack_ok(ccard, chan, ack))
				chan = NULL;
			break;

		case ASSIGN_OK:
			if (chan) {
				eicon_log(ccard, 1, "idi_ack: Ch%d: ASSIGN-OK on chan already assigned (%x,%x)\n",
					chan->No, chan->e.D3Id, chan->e.B2Id);
			}
			save_flags(flags);
			cli();
			for(j = 0; j < ccard->nchannels + 1; j++) {
				if ((ccard->bch[j].e.ref == ack->Reference) &&
					(ccard->bch[j].e.Req == ASSIGN)) {
					if (!ccard->bch[j].e.ReqCh) 
						ccard->bch[j].e.D3Id  = ack->RcId;
					else
						ccard->bch[j].e.B2Id  = ack->RcId;
					ccard->IdTable[ack->RcId] = &ccard->bch[j];
					chan = &ccard->bch[j];
					eicon_log(ccard, 16, "idi_ack: Ch%d: Id %x assigned (%s)\n", j, 
						ack->RcId, (ccard->bch[j].e.ReqCh)? "Net":"Sig");
					break;
				}
			}		
			restore_flags(flags);
			if (j > ccard->nchannels) {
				eicon_log(ccard, 24, "idi_ack: Ch??: ref %d not found for Id %d\n", 
					ack->Reference, ack->RcId);
			}
			break;

		case OUT_OF_RESOURCES:
		case UNKNOWN_COMMAND:
		case WRONG_COMMAND:
		case WRONG_ID:
		case WRONG_CH:
		case UNKNOWN_IE:
		case WRONG_IE:
		default:
			if (!chan) {
				eicon_log(ccard, 1, "idi_ack: Ch%d: Not OK !! on chan without Id\n", dCh);
				break;
			} else
			switch (chan->e.Req) {
				case 12:	/* Alert */
					eicon_log(ccard, 2, "eicon_err: Ch%d: Alert Not OK : Rc=%d Id=%x Ch=%d\n",
						dCh, ack->Rc, ack->RcId, ack->RcCh);
					break;
				default:
					eicon_log(ccard, 1, "eicon_err: Ch%d: Ack Not OK !!: Rc=%d Id=%x Ch=%d Req=%d\n",
						dCh, ack->Rc, ack->RcId, ack->RcCh, chan->e.Req);
			}
			if (dCh == ccard->nchannels) { /* Management */
				chan->fsm_state = 2;
			} else if (dCh >= 0) {
					/* any other channel */
					/* card reports error: we hangup */
				idi_hangup(ccard, chan);
				cmd.driver = ccard->myid;
				cmd.command = ISDN_STAT_DHUP;
				cmd.arg = chan->No;
				ccard->interface.statcallb(&cmd);
			}
	}
	save_flags(flags);
	cli();
	if (chan) {
		chan->e.ref = 0;
		chan->e.busy = 0;
	}
	restore_flags(flags);
	dev_kfree_skb(skb);
	eicon_schedule_tx(ccard);
}

int
idi_send_data(eicon_card *card, eicon_chan *chan, int ack, struct sk_buff *skb, int que)
{
        struct sk_buff *xmit_skb;
        struct sk_buff *skb2;
        eicon_REQ *reqbuf;
        eicon_chan_ptr *chan2;
        int len, plen = 0, offset = 0;
	unsigned long flags;

	if ((!card) || (!chan)) {
		eicon_log(card, 1, "idi_err: Ch??: null card/chan in send_data\n");
		return -1;
	}

        if (chan->fsm_state != EICON_STATE_ACTIVE) {
		eicon_log(card, 1, "idi_snd: Ch%d: send bytes on state %d !\n", chan->No, chan->fsm_state);
                return -ENODEV;
	}

        len = skb->len;
	if (len > EICON_MAX_QUEUE)	/* too much for the shared memory */
		return -1;
        if (!len)
                return 0;
	if (chan->queued + len > EICON_MAX_QUEUE)
		return 0;

	eicon_log(card, 128, "idi_snd: Ch%d: %d bytes\n", chan->No, len);

	save_flags(flags);
	cli();
	while(offset < len) {

		plen = ((len - offset) > 270) ? 270 : len - offset;

	        xmit_skb = alloc_skb(plen + sizeof(eicon_REQ), GFP_ATOMIC);
        	skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

	        if ((!xmit_skb) || (!skb2)) {
			restore_flags(flags);
        	        eicon_log(card, 1, "idi_err: Ch%d: alloc_skb failed in send_data()\n", chan->No);
			if (xmit_skb) 
				dev_kfree_skb(skb);
			if (skb2) 
				dev_kfree_skb(skb2);
                	return -ENOMEM;
	        }

	        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        	chan2->ptr = chan;

	        reqbuf = (eicon_REQ *)skb_put(xmit_skb, plen + sizeof(eicon_REQ));
		if (((len - offset) > 270) &&
			(chan->l2prot != ISDN_PROTO_L2_TRANS)) {
		        reqbuf->Req = IDI_N_MDATA;
		} else {
		        reqbuf->Req = IDI_N_DATA;
			if (ack) reqbuf->Req |= N_D_BIT;
		}	
        	reqbuf->ReqCh = 0;
	        reqbuf->ReqId = 1;
		memcpy(&reqbuf->XBuffer.P, skb->data + offset, plen);
		reqbuf->XBuffer.length = plen;
		reqbuf->Reference = 1; /* Net Entity */

		skb_queue_tail(&chan->e.X, xmit_skb);
		skb_queue_tail(&card->sndq, skb2); 

		offset += plen;
	}
	if (que)
		chan->queued += len;
	restore_flags(flags);
	eicon_schedule_tx(card);
        dev_kfree_skb(skb);
        return len;
}


int
eicon_idi_manage_assign(eicon_card *card)
{
        struct sk_buff *skb;
        struct sk_buff *skb2;
        eicon_REQ  *reqbuf;
        eicon_chan     *chan;
        eicon_chan_ptr *chan2;

        chan = &(card->bch[card->nchannels]);

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
		eicon_log(card, 1, "idi_err: alloc_skb failed in manage_assign()\n");
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM;
        }

        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        chan2->ptr = chan;

        reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));

        reqbuf->XBuffer.P[0] = 0;
        reqbuf->Req = ASSIGN;
        reqbuf->ReqCh = 0;
        reqbuf->ReqId = 0xe0;
        reqbuf->XBuffer.length = 1;
        reqbuf->Reference = 2; /* Man Entity */

        skb_queue_tail(&chan->e.X, skb);
        skb_queue_tail(&card->sndq, skb2);
        eicon_schedule_tx(card);
        return(0);
}


int
eicon_idi_manage_remove(eicon_card *card)
{
        struct sk_buff *skb;
        struct sk_buff *skb2;
        eicon_REQ  *reqbuf;
        eicon_chan     *chan;
        eicon_chan_ptr *chan2;

        chan = &(card->bch[card->nchannels]);

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
               	eicon_log(card, 1, "idi_err: alloc_skb failed in manage_remove()\n");
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
                return -ENOMEM;
        }

        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        chan2->ptr = chan;

        reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));

        reqbuf->Req = REMOVE;
        reqbuf->ReqCh = 0;
        reqbuf->ReqId = 1;
        reqbuf->XBuffer.length = 0;
        reqbuf->Reference = 2; /* Man Entity */

        skb_queue_tail(&chan->e.X, skb);
        skb_queue_tail(&card->sndq, skb2);
        eicon_schedule_tx(card);
        return(0);
}


int
eicon_idi_manage(eicon_card *card, eicon_manifbuf *mb)
{
	int l = 0;
	int ret = 0;
	int timeout;
	int i;
        struct sk_buff *skb;
        struct sk_buff *skb2;
        eicon_REQ  *reqbuf;
        eicon_chan     *chan;
        eicon_chan_ptr *chan2;

        chan = &(card->bch[card->nchannels]);

	if (chan->e.D3Id)
		return -EBUSY;
	chan->e.D3Id = 1;
	while((skb2 = skb_dequeue(&chan->e.X)))
		dev_kfree_skb(skb2);
	chan->e.busy = 0;
 
	if ((ret = eicon_idi_manage_assign(card))) {
		chan->e.D3Id = 0;
		return(ret); 
	}

        timeout = jiffies + 50;
        while (timeout > jiffies) {
                if (chan->e.B2Id) break;
                SLEEP(10);
        }
        if (!chan->e.B2Id) {
		chan->e.D3Id = 0;
		return -EIO;
	}

	chan->fsm_state = 0;

	if (!(manbuf = kmalloc(sizeof(eicon_manifbuf), GFP_KERNEL))) {
               	eicon_log(card, 1, "idi_err: alloc_manifbuf failed\n");
		chan->e.D3Id = 0;
		return -ENOMEM;
	}
	if (copy_from_user(manbuf, mb, sizeof(eicon_manifbuf))) {
		kfree(manbuf);
		chan->e.D3Id = 0;
		return -EFAULT;
	}

        skb = alloc_skb(270 + sizeof(eicon_REQ), GFP_ATOMIC);
        skb2 = alloc_skb(sizeof(eicon_chan_ptr), GFP_ATOMIC);

        if ((!skb) || (!skb2)) {
               	eicon_log(card, 1, "idi_err_manif: alloc_skb failed in manage()\n");
		if (skb) 
			dev_kfree_skb(skb);
		if (skb2) 
			dev_kfree_skb(skb2);
		kfree(manbuf);
		chan->e.D3Id = 0;
                return -ENOMEM;
        }

        chan2 = (eicon_chan_ptr *)skb_put(skb2, sizeof(eicon_chan_ptr));
        chan2->ptr = chan;

        reqbuf = (eicon_REQ *)skb_put(skb, 270 + sizeof(eicon_REQ));

        reqbuf->XBuffer.P[l++] = ESC;
        reqbuf->XBuffer.P[l++] = 6;
        reqbuf->XBuffer.P[l++] = 0x80;
	for (i = 0; i < manbuf->length[0]; i++)
	        reqbuf->XBuffer.P[l++] = manbuf->data[i];
        reqbuf->XBuffer.P[1] = manbuf->length[0] + 1;

        reqbuf->XBuffer.P[l++] = 0;
        reqbuf->Req = (manbuf->count) ? manbuf->count : 0x02; /* Request */
        reqbuf->ReqCh = 0;
        reqbuf->ReqId = 1;
        reqbuf->XBuffer.length = l;
        reqbuf->Reference = 2; /* Man Entity */

        skb_queue_tail(&chan->e.X, skb);
        skb_queue_tail(&card->sndq, skb2);

	manbuf->count = 0;
	manbuf->pos = 0;

        eicon_schedule_tx(card);

        timeout = jiffies + 50;
        while (timeout > jiffies) {
                if (chan->fsm_state) break;
                SLEEP(10);
        }
        if ((!chan->fsm_state) || (chan->fsm_state == 2)) {
		eicon_idi_manage_remove(card);
		kfree(manbuf);
		chan->e.D3Id = 0;
		return -EIO;
	}

	if ((ret = eicon_idi_manage_remove(card))) {
		kfree(manbuf);
		chan->e.D3Id = 0;
		return(ret);
	}

	if (copy_to_user(mb, manbuf, sizeof(eicon_manifbuf))) {
		kfree(manbuf);
		chan->e.D3Id = 0;
		return -EFAULT;
	}

	kfree(manbuf);
	chan->e.D3Id = 0;
  return(0);
}
