/*
 * $Id: capidev.h,v 1.4 1999/07/01 15:26:32 calle Exp $
 *
 * CAPI 2.0 Interface for Linux
 *
 * (c) Copyright 1996 by Carsten Paeth (calle@calle.in-berlin.de)
 *
 * $Log: capidev.h,v $
 * Revision 1.4  1999/07/01 15:26:32  calle
 * complete new version (I love it):
 * + new hardware independed "capi_driver" interface that will make it easy to:
 *   - support other controllers with CAPI-2.0 (i.e. USB Controller)
 *   - write a CAPI-2.0 for the passive cards
 *   - support serial link CAPI-2.0 boxes.
 * + wrote "capi_driver" for all supported cards.
 * + "capi_driver" (supported cards) now have to be configured with
 *   make menuconfig, in the past all supported cards where included
 *   at once.
 * + new and better informations in /proc/capi/
 * + new ioctl to switch trace of capi messages per controller
 *   using "avmcapictrl trace [contr] on|off|...."
 * + complete testcircle with all supported cards and also the
 *   PCMCIA cards (now patch for pcmcia-cs-3.0.13 needed) done.
 *
 * Revision 1.3  1999/07/01 08:22:58  keil
 * compatibility macros now in <linux/isdn_compat.h>
 *
 * Revision 1.2  1999/06/21 15:24:13  calle
 * extend information in /proc.
 *
 * Revision 1.1  1997/03/04 21:50:30  calle
 * Frirst version in isdn4linux
 *
 * Revision 2.2  1997/02/12 09:31:39  calle
 * new version
 *
 * Revision 1.1  1997/01/31 10:32:20  calle
 * Initial revision
 *
 */

struct capidev {
	int is_open;
	int is_registered;
	__u16 applid;
	struct sk_buff_head recv_queue;
	wait_queue_head_t recv_wait;
	__u16 errcode;
	/* Statistic */
	unsigned long nopen;
	unsigned long nrecvctlpkt;
	unsigned long nrecvdatapkt;
	unsigned long nsentctlpkt;
	unsigned long nsentdatapkt;
};

#define CAPI_MAXMINOR	CAPI_MAXAPPL
