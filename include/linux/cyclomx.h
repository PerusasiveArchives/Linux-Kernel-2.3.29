/*
* cyclomx.h	CYCLOM X Multiprotocol WAN Link Driver.
*		User-level API definitions.
*
* Author:	Arnaldo Carvalho de Melo <acme@conectiva.com.br>
*
* Copyright:	(c) 1998, 1999 Arnaldo Carvalho de Melo
*
* Based on wanpipe.h by Gene Kozin <genek@compuserve.com>
*
*		This program is free software; you can redistribute it and/or
*		modify it under the terms of the GNU General Public License
*		as published by the Free Software Foundation; either version
*		2 of the License, or (at your option) any later version.
* ============================================================================
* 1999/05/19	acme		wait_queue_head_t wait_stats(support for 2.3.*)
* 1999/01/03	acme		judicious use of data types
* Dec 27, 1998	Arnaldo		cleanup: PACKED not needed
* Aug 08, 1998	Arnaldo		Version 0.0.1
*/
#ifndef	_CYCLOMX_H
#define	_CYCLOMX_H

#include <linux/config.h>
#include <linux/wanrouter.h>
#include <linux/spinlock.h>

#ifdef	__KERNEL__
/* Kernel Interface */

#include <linux/cycx_drv.h>	/* CYCLOM X support module API definitions */
#include <linux/cycx_cfm.h>	/* CYCLOM X firmware module definitions */
#ifdef CONFIG_CYCLOMX_X25
#include <linux/cycx_x25.h>
#endif

#ifndef	min
#define min(a,b) (((a)<(b))?(a):(b))
#endif
#ifndef	max
#define max(a,b) (((a)>(b))?(a):(b))
#endif

#define	is_digit(ch) (((ch)>=(unsigned)'0'&&(ch)<=(unsigned)'9')?1:0)

/* Adapter Data Space.
 * This structure is needed because we handle multiple cards, otherwise
 * static data would do it.
 */
typedef struct cycx {
	char devname[WAN_DRVNAME_SZ+1];	/* card name */
	cycxhw_t hw;			/* hardware configuration */
	wan_device_t wandev;		/* WAN device data space */
	u32 open_cnt;			/* number of open interfaces */
	u32 state_tick;			/* link state timestamp */
	spinlock_t lock;
	char in_isr;			/* interrupt-in-service flag */
	char buff_int_mode_unbusy;      /* flag for carrying out dev_tint */
#if (LINUX_VERSION_CODE >= 0x20300)
	wait_queue_head_t wait_stats;  /* to wait for the STATS indication */
#else
	struct wait_queue* wait_stats;  /* to wait for the STATS indication */
#endif
	u32 mbox;			/* -> mailbox */
	void (*isr)(struct cycx* card);	/* interrupt service routine */
	int (*exec)(struct cycx* card, void* u_cmd, void* u_data);
	union {
#ifdef CONFIG_CYCLOMX_X25
		struct { /* X.25 specific data */
			u32 lo_pvc;
			u32 hi_pvc;
			u32 lo_svc;
			u32 hi_svc;
			TX25Stats stats;
			spinlock_t lock;
			u32 connection_keys;
		} x;
#endif
	} u;
} cycx_t;

/* Public Functions */
void cyclomx_open      (cycx_t *card);			/* cycx_main.c */
void cyclomx_close     (cycx_t *card);			/* cycx_main.c */
void cyclomx_set_state (cycx_t *card, int state);	/* cycx_main.c */

#ifdef CONFIG_CYCLOMX_X25
int cyx_init (cycx_t *card, wandev_conf_t *conf);	/* cycx_x25.c */
#endif
#endif	/* __KERNEL__ */
#endif	/* _CYCLOMX_H */
