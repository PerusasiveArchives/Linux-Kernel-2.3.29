/* $Id: isdn_tty.c,v 1.80 1999/11/07 13:34:30 armin Exp $

 * Linux ISDN subsystem, tty functions and AT-command emulator (linklevel).
 *
 * Copyright 1994-1999  by Fritz Elfert (fritz@isdn4linux.de)
 * Copyright 1995,96    by Thinking Objects Software GmbH Wuerzburg
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
 * $Log: isdn_tty.c,v $
 * Revision 1.80  1999/11/07 13:34:30  armin
 * Fixed AT command line editor
 *
 * Revision 1.79  1999/10/29 18:35:08  armin
 * Check number len in isdn_get_msnstr() to avoid buffer overflow.
 *
 * Revision 1.78  1999/10/28 23:03:51  armin
 * Bugfix: now freeing channel on modem_hup() even when
 * usage on ttyI has changed and error-report for
 * AT-commands on wrong channel-state.
 *
 * Revision 1.77  1999/10/26 21:13:14  armin
 * using define for checking phone number len in isdn_tty_getdial()
 *
 * Revision 1.76  1999/10/11 22:16:26  keil
 * Suspend/Resume is possible without explicit ID too
 *
 * Revision 1.75  1999/10/08 18:59:32  armin
 * Bugfix of too small MSN buffer and checking phone number
 * in isdn_tty_getdial()
 *
 * Revision 1.74  1999/09/04 06:20:04  keil
 * Changes from kernel set_current_state()
 *
 * Revision 1.73  1999/08/28 21:56:27  keil
 * misplaced #endif caused ttyI crash in 2.3.X
 *
 * Revision 1.72  1999/07/31 12:59:45  armin
 * Added tty fax capabilities.
 *
 * Revision 1.71  1999/07/27 10:34:34  armin
 * Fixed last change. Did not compile with AUDIO support off.
 *
 * Revision 1.70  1999/07/25 16:17:58  keil
 * Fix Suspend/Resume
 *
 * Revision 1.69  1999/07/25 12:56:15  armin
 * isdn_tty_at_cout() now queues the message if online and
 * data is in queue or flip buffer is full.
 * needed for fax connections.
 *
 * Revision 1.68  1999/07/11 17:51:51  armin
 * Bugfix, "-" was missing for AT&L settings.
 *
 * Revision 1.67  1999/07/11 17:14:12  armin
 * Added new layer 2 and 3 protocols for Fax and DSP functions.
 * Moved "Add CPN to RING message" to new register S23,
 * "Display message" is now correct on register S13 bit 7.
 * New audio command AT+VDD implemented (deactivate DTMF decoder and
 * activate possible existing hardware/DSP decoder).
 * Moved some tty defines to .h file.
 * Made whitespace possible in AT command line.
 * Some AT-emulator output bugfixes.
 * First Fax G3 implementations.
 *
 * Revision 1.66  1999/07/07 10:13:46  detabc
 * remove unused messages
 *
 * Revision 1.65  1999/07/04 21:01:59  werner
 * Added support for keypad and display (ported from 2.0)
 *
 * Revision 1.64  1999/07/01 08:30:00  keil
 * compatibility to 2.3 kernel
 *
 * Revision 1.63  1999/04/12 12:33:39  fritz
 * Changes from 2.0 tree.
 *
 * Revision 1.62  1999/03/02 12:04:48  armin
 * -added ISDN_STAT_ADDCH to increase supported channels after
 *  register_isdn().
 * -ttyI now goes on-hook on ATZ when B-Ch is connected.
 * -added timer-function for register S7 (Wait for Carrier).
 * -analog modem (ISDN_PROTO_L2_MODEM) implementations.
 * -on L2_MODEM a string will be appended to the CONNECT-Message,
 *  which is provided by the HL-Driver in parm.num in ISDN_STAT_BCONN.
 * -variable "dialing" used for ATA also, for interrupting call
 *  establishment and register S7.
 *
 * Revision 1.61  1999/01/27 22:53:11  he
 * minor updates (spellings, jiffies wrap around in isdn_tty)
 *
 * Revision 1.60  1998/11/15 23:57:32  keil
 * changes for 2.1.127
 *
 * Revision 1.59  1998/08/20 13:50:15  keil
 * More support for hybrid modem (not working yet)
 *
 * Revision 1.58  1998/07/26 18:48:45  armin
 * Added silence detection in voice receive mode.
 *
 * Revision 1.57  1998/06/26 15:12:36  fritz
 * Added handling of STAT_ICALL with incomplete CPN.
 * Added AT&L for ttyI emulator.
 * Added more locking stuff in tty_write.
 *
 * Revision 1.56  1998/06/18 23:31:51  fritz
 * Replaced cli()/restore_flags() in isdn_tty_write() by locking.
 * Removed direct-senddown feature in isdn_tty_write because it will
 * never succeed with locking and is useless anyway.
 *
 * Revision 1.55  1998/06/17 19:50:55  he
 * merged with 2.1.10[34] (cosmetics and udelay() -> mdelay())
 * brute force fix to avoid Ugh's in isdn_tty_write()
 * cleaned up some dead code
 *
 *
 *
 * Revision 1.52  1998/03/19 13:18:21  keil
 * Start of a CAPI like interface for supplementary Service
 * first service: SUSPEND
 *
 *
 * Revision 1.49  1998/03/08 00:01:59  fritz
 * Bugfix: Lowlevel module usage and channel usage were not
 *         reset on NO DCHANNEL.
 *
 * Revision 1.48  1998/03/07 12:28:15  tsbogend
 * fixed kernel unaligned traps on Linux/Alpha
 *
 * Revision 1.47  1998/02/22 19:44:14  fritz
 * Bugfixes and improvements regarding V.110, V.110 now running.
 *
 * Revision 1.46  1998/02/20 17:23:08  fritz
 * Changes for recent kernels.
 * Merged in contributions by Thomas Pfeiffer (V.110 T.70+ Extended FAX stuff)
 * Added symbolic constants for Modem-Registers.
 *
 * Revision 1.45  1998/01/31 22:07:49  keil
 * changes for newer kernels
 *
 * Revision 1.44  1998/01/31 19:30:02  calle
 * Merged changes from and for 2.1.82, not tested only compiled ...
 *
 * Revision 1.43  1997/10/09 21:29:04  fritz
 * New HL<->LL interface:
 *   New BSENT callback with nr. of bytes included.
 *   Sending without ACK.
 *   New L1 error status (not yet in use).
 *   Cleaned up obsolete structures.
 * Implemented Cisco-SLARP.
 * Changed local net-interface data to be dynamically allocated.
 * Removed old 2.0 compatibility stuff.
 *
 * Revision 1.42  1997/10/01 09:20:49  fritz
 * Removed old compatibility stuff for 2.0.X kernels.
 * From now on, this code is for 2.1.X ONLY!
 * Old stuff is still in the separate branch.
 *
 * Revision 1.41  1997/05/27 15:17:31  fritz
 * Added changes for recent 2.1.x kernels:
 *   changed return type of isdn_close
 *   queue_task_* -> queue_task
 *   clear/set_bit -> test_and_... where apropriate.
 *   changed type of hard_header_cache parameter.
 *
 * Revision 1.40  1997/03/24 22:55:27  fritz
 * Added debug code for status callbacks.
 *
 * Revision 1.39  1997/03/21 18:25:56  fritz
 * Corrected CTS handling.
 *
 * Revision 1.38  1997/03/07 12:13:35  fritz
 * Bugfix: Send audio in adpcm format was broken.
 * Bugfix: CTS handling was wrong.
 *
 * Revision 1.37  1997/03/07 01:37:34  fritz
 * Bugfix: Did not compile with CONFIG_ISDN_AUDIO disabled.
 * Bugfix: isdn_tty_tint() did not handle lowlevel errors correctly.
 * Bugfix: conversion was wrong when sending ulaw audio.
 * Added proper ifdef's for CONFIG_ISDN_AUDIO
 *
 * Revision 1.36  1997/03/04 21:41:55  fritz
 * Fix: Excessive stack usage of isdn_tty_senddown()
 *      and isdn_tty_end_vrx() could lead to problems.
 *
 * Revision 1.35  1997/03/02 19:05:52  fritz
 * Bugfix: Avoid recursion.
 *
 * Revision 1.34  1997/03/02 14:29:22  fritz
 * More ttyI related cleanup.
 *
 * Revision 1.33  1997/02/28 02:32:45  fritz
 * Cleanup: Moved some tty related stuff from isdn_common.c
 *          to isdn_tty.c
 * Bugfix:  Bisync protocol did not behave like documented.
 *
 * Revision 1.32  1997/02/23 15:43:03  fritz
 * Small change in handling of incoming calls
 * documented in newest version of ttyI.4
 *
 * Revision 1.31  1997/02/21 13:05:57  fritz
 * Bugfix: Remote hangup did not set location-info on ttyI's
 *
 * Revision 1.30  1997/02/18 09:41:05  fritz
 * Added support for bitwise access to modem registers (ATSx.y=n, ATSx.y?).
 * Beautified output of AT&V.
 *
 * Revision 1.29  1997/02/16 12:11:51  fritz
 * Added S13,Bit4 option.
 *
 * Revision 1.28  1997/02/10 22:07:08  fritz
 * Added 2 modem registers for numbering plan and screening info.
 *
 * Revision 1.27  1997/02/10 21:31:14  fritz
 * Changed setup-interface (incoming and outgoing).
 *
 * Revision 1.26  1997/02/10 20:12:48  fritz
 * Changed interface for reporting incoming calls.
 *
 * Revision 1.25  1997/02/03 23:04:30  fritz
 * Reformatted according CodingStyle.
 * skb->free stuff replaced by macro.
 * Finished full-duplex audio.
 *
 * Revision 1.24  1997/01/14 01:32:42  fritz
 * Changed audio receive not to rely on skb->users and skb->lock.
 * Added ATI2 and related variables.
 * Started adding full-duplex audio capability.
 *
 * Revision 1.23  1996/10/22 23:14:02  fritz
 * Changes for compatibility to 2.0.X and 2.1.X kernels.
 *
 * Revision 1.22  1996/10/19 18:56:43  fritz
 * ATZ did not change the xmitbuf size.
 *
 * Revision 1.21  1996/06/24 17:40:28  fritz
 * Bugfix: Did not compile without CONFIG_ISDN_AUDIO
 *
 * Revision 1.20  1996/06/15 14:59:39  fritz
 * Fixed isdn_tty_tint() to handle partially sent
 * sk_buffs.
 *
 * Revision 1.19  1996/06/12 15:53:56  fritz
 * Bugfix: AT+VTX and AT+VRX could be executed without
 *         having a connection.
 *         Missing check for NULL tty in isdn_tty_flush_buffer().
 *
 * Revision 1.18  1996/06/07 11:17:33  tsbogend
 * added missing #ifdef CONFIG_ISDN_AUDIO to make compiling without
 * audio support possible
 *
 * Revision 1.17  1996/06/06 14:55:47  fritz
 * Changed to support DTMF decoding on audio playback also.
 * Bugfix: Added check for invalid info->isdn_driver in
 *         isdn_tty_senddown().
 * Clear ncarrier flag on last close() of a tty.
 *
 * Revision 1.16  1996/06/05 02:24:12  fritz
 * Added DTMF decoder for audio mode.
 *
 * Revision 1.15  1996/06/03 20:35:01  fritz
 * Fixed typos.
 *
 * Revision 1.14  1996/06/03 20:12:19  fritz
 * Fixed typos.
 * Added call to write_wakeup via isdn_tty_flush_buffer()
 * in isdn_tty_modem_hup().
 *
 * Revision 1.13  1996/05/31 01:33:29  fritz
 * Changed buffering due to bad performance with mgetty.
 * Now sk_buff is delayed allocated in isdn_tty_senddown
 * using xmit_buff like in standard serial driver.
 * Fixed module locking.
 * Added DLE-DC4 handling in voice mode.
 *
 * Revision 1.12  1996/05/19 01:34:40  fritz
 * Bugfix: ATS returned error.
 *         Register 20 made readonly.
 *
 * Revision 1.11  1996/05/18 01:37:03  fritz
 * Added spelling corrections and some minor changes
 * to stay in sync with kernel.
 *
 * Revision 1.10  1996/05/17 03:51:49  fritz
 * Changed DLE handling for audio receive.
 *
 * Revision 1.9  1996/05/11 21:52:07  fritz
 * Changed queue management to use sk_buffs.
 *
 * Revision 1.8  1996/05/10 08:49:43  fritz
 * Checkin before major changes of tty-code.
 *
 * Revision 1.7  1996/05/07 09:15:09  fritz
 * Reorganized and general cleanup.
 * Bugfixes:
 *  - Audio-transmit working now.
 *  - "NO CARRIER" now reported, when hanging up with DTR low.
 *  - Corrected CTS handling.
 *
 * Revision 1.6  1996/05/02 03:59:25  fritz
 * Bugfixes:
 *  - On dialout, layer-2 setup had been incomplete
 *    when using new auto-layer2 feature.
 *  - On hangup, "NO CARRIER" message sometimes missing.
 *
 * Revision 1.5  1996/04/30 21:05:25  fritz
 * Test commit
 *
 * Revision 1.4  1996/04/20 16:39:54  fritz
 * Changed all io to go through generic routines in isdn_common.c
 * Fixed a real ugly bug in modem-emulator: 'ATA' had been accepted
 * even when a call has been cancelled from the remote machine.
 *
 * Revision 1.3  1996/02/11 02:12:32  fritz
 * Bugfixes according to similar fixes in standard serial.c of kernel.
 *
 * Revision 1.2  1996/01/22 05:12:25  fritz
 * replaced my_atoi by simple_strtoul
 *
 * Revision 1.1  1996/01/09 04:13:18  fritz
 * Initial revision
 *
 */
#undef ISDN_TTY_STAT_DEBUG

#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>
#include <linux/isdn.h>
#include "isdn_common.h"
#include "isdn_tty.h"
#ifdef CONFIG_ISDN_AUDIO
#include "isdn_audio.h"
#define VBUF 0x3e0
#define VBUFX (VBUF/16)
#endif

#define FIX_FILE_TRANSFER

/* Prototypes */

static int isdn_tty_edit_at(const char *, int, modem_info *, int);
static void isdn_tty_check_esc(const u_char *, u_char, int, int *, int *, int);
static void isdn_tty_modem_reset_regs(modem_info *, int);
static void isdn_tty_cmd_ATA(modem_info *);
static void isdn_tty_flush_buffer(struct tty_struct *);
static void isdn_tty_modem_result(int, modem_info *);
#ifdef CONFIG_ISDN_AUDIO
static int isdn_tty_countDLE(unsigned char *, int);
#endif

/* Leave this unchanged unless you know what you do! */
#define MODEM_PARANOIA_CHECK
#define MODEM_DO_RESTART

static char *isdn_ttyname_ttyI = "ttyI";
static char *isdn_ttyname_cui = "cui";
static int bit2si[8] =
{1, 5, 7, 7, 7, 7, 7, 7};
static int si2bit[8] =
{4, 1, 4, 4, 4, 4, 4, 4};

char *isdn_tty_revision = "$Revision: 1.80 $";


/* isdn_tty_try_read() is called from within isdn_tty_rcv_skb()
 * to stuff incoming data directly into a tty's flip-buffer. This
 * is done to speed up tty-receiving if the receive-queue is empty.
 * This routine MUST be called with interrupts off.
 * Return:
 *  1 = Success
 *  0 = Failure, data has to be buffered and later processed by
 *      isdn_tty_readmodem().
 */
static int
isdn_tty_try_read(modem_info * info, struct sk_buff *skb)
{
	int c;
	int len;
	struct tty_struct *tty;

	if (info->online) {
		if ((tty = info->tty)) {
			if (info->mcr & UART_MCR_RTS) {
				c = TTY_FLIPBUF_SIZE - tty->flip.count;
				len = skb->len
#ifdef CONFIG_ISDN_AUDIO
					+ ISDN_AUDIO_SKB_DLECOUNT(skb)
#endif
					;
				if (c >= len) {
#ifdef CONFIG_ISDN_AUDIO
					if (ISDN_AUDIO_SKB_DLECOUNT(skb))
						while (skb->len--) {
							if (*skb->data == DLE)
								tty_insert_flip_char(tty, DLE, 0);
							tty_insert_flip_char(tty, *skb->data++, 0);
					} else {
#endif
						memcpy(tty->flip.char_buf_ptr,
						       skb->data, len);
						tty->flip.count += len;
						tty->flip.char_buf_ptr += len;
						memset(tty->flip.flag_buf_ptr, 0, len);
						tty->flip.flag_buf_ptr += len;
#ifdef CONFIG_ISDN_AUDIO
					}
#endif
					if (info->emu.mdmreg[REG_CPPP] & BIT_CPPP)
						tty->flip.flag_buf_ptr[len - 1] = 0xff;
					queue_task(&tty->flip.tqueue, &tq_timer);
					kfree_skb(skb);
					return 1;
				}
			}
		}
	}
	return 0;
}

/* isdn_tty_readmodem() is called periodically from within timer-interrupt.
 * It tries getting received data from the receive queue an stuff it into
 * the tty's flip-buffer.
 */
void
isdn_tty_readmodem(void)
{
	int resched = 0;
	int midx;
	int i;
	int c;
	int r;
	ulong flags;
	struct tty_struct *tty;
	modem_info *info;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		if ((midx = dev->m_idx[i]) >= 0) {
			info = &dev->mdm.info[midx];
			if (info->online) {
				r = 0;
#ifdef CONFIG_ISDN_AUDIO
				isdn_audio_eval_dtmf(info);
				if ((info->vonline & 1) && (info->emu.vpar[1]))
					isdn_audio_eval_silence(info);
#endif
				if ((tty = info->tty)) {
					if (info->mcr & UART_MCR_RTS) {
						c = TTY_FLIPBUF_SIZE - tty->flip.count;
						if (c > 0) {
							save_flags(flags);
							cli();
							r = isdn_readbchan(info->isdn_driver, info->isdn_channel,
									   tty->flip.char_buf_ptr,
									   tty->flip.flag_buf_ptr, c, 0);
							/* CISCO AsyncPPP Hack */
							if (!(info->emu.mdmreg[REG_CPPP] & BIT_CPPP))
								memset(tty->flip.flag_buf_ptr, 0, r);
							tty->flip.count += r;
							tty->flip.flag_buf_ptr += r;
							tty->flip.char_buf_ptr += r;
							if (r)
								queue_task(&tty->flip.tqueue, &tq_timer);
							restore_flags(flags);
						}
					} else
						r = 1;
				} else
					r = 1;
				if (r) {
					info->rcvsched = 0;
					resched = 1;
				} else
					info->rcvsched = 1;
			}
		}
	}
	if (!resched)
		isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 0);
}

int
isdn_tty_rcv_skb(int i, int di, int channel, struct sk_buff *skb)
{
	ulong flags;
	int midx;
#ifdef CONFIG_ISDN_AUDIO
	int ifmt;
#endif
	modem_info *info;

	if ((midx = dev->m_idx[i]) < 0) {
		/* if midx is invalid, packet is not for tty */
		return 0;
	}
	info = &dev->mdm.info[midx];
#ifdef CONFIG_ISDN_AUDIO
	ifmt = 1;
	
	if ((info->vonline) && (!info->emu.vpar[4]))
		isdn_audio_calc_dtmf(info, skb->data, skb->len, ifmt);
	if ((info->vonline & 1) && (info->emu.vpar[1]))
		isdn_audio_calc_silence(info, skb->data, skb->len, ifmt);
#endif
	if ((info->online < 2)
#ifdef CONFIG_ISDN_AUDIO
	    && (!(info->vonline & 1))
#endif
		) {
		/* If Modem not listening, drop data */
		kfree_skb(skb);
		return 1;
	}
	if (info->emu.mdmreg[REG_T70] & BIT_T70) {
		if (info->emu.mdmreg[REG_T70] & BIT_T70_EXT) {
			/* T.70 decoding: throw away the T.70 header (2 or 4 bytes)   */
			if (skb->data[0] == 3) /* pure data packet -> 4 byte headers  */
				skb_pull(skb, 4);
			else
				if (skb->data[0] == 1) /* keepalive packet -> 2 byte hdr  */
					skb_pull(skb, 2);
		} else
			/* T.70 decoding: Simply throw away the T.70 header (4 bytes) */
			if ((skb->data[0] == 1) && ((skb->data[1] == 0) || (skb->data[1] == 1)))
				skb_pull(skb, 4);
	}
#ifdef CONFIG_ISDN_AUDIO
	if (skb_headroom(skb) < sizeof(isdn_audio_skb)) {
		printk(KERN_WARNING
		       "isdn_audio: insufficient skb_headroom, dropping\n");
		kfree_skb(skb);
		return 1;
	}
	ISDN_AUDIO_SKB_DLECOUNT(skb) = 0;
	ISDN_AUDIO_SKB_LOCK(skb) = 0;
	if (info->vonline & 1) {
		/* voice conversion/compression */
		switch (info->emu.vpar[3]) {
			case 2:
			case 3:
			case 4:
				/* adpcm
				 * Since compressed data takes less
				 * space, we can overwrite the buffer.
				 */
				skb_trim(skb, isdn_audio_xlaw2adpcm(info->adpcmr,
								    ifmt,
								    skb->data,
								    skb->data,
								    skb->len));
				break;
			case 5:
				/* a-law */
				if (!ifmt)
					isdn_audio_ulaw2alaw(skb->data, skb->len);
				break;
			case 6:
				/* u-law */
				if (ifmt)
					isdn_audio_alaw2ulaw(skb->data, skb->len);
				break;
		}
		ISDN_AUDIO_SKB_DLECOUNT(skb) =
			isdn_tty_countDLE(skb->data, skb->len);
	}
#ifdef CONFIG_ISDN_TTY_FAX
	else {
		if (info->faxonline & 2) {
			isdn_tty_fax_bitorder(info, skb);
			ISDN_AUDIO_SKB_DLECOUNT(skb) =
				isdn_tty_countDLE(skb->data, skb->len);
		}
	}
#endif
#endif
	/* Try to deliver directly via tty-flip-buf if queue is empty */
	save_flags(flags);
	cli();
	if (skb_queue_empty(&dev->drv[di]->rpqueue[channel]))
		if (isdn_tty_try_read(info, skb)) {
			restore_flags(flags);
			return 1;
		}
	/* Direct deliver failed or queue wasn't empty.
	 * Queue up for later dequeueing via timer-irq.
	 */
	__skb_queue_tail(&dev->drv[di]->rpqueue[channel], skb);
	dev->drv[di]->rcvcount[channel] +=
		(skb->len
#ifdef CONFIG_ISDN_AUDIO
		 + ISDN_AUDIO_SKB_DLECOUNT(skb)
#endif
			);
	restore_flags(flags);
	/* Schedule dequeuing */
	if ((dev->modempoll) && (info->rcvsched))
		isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 1);
	return 1;
}

void
isdn_tty_cleanup_xmit(modem_info * info)
{
	struct sk_buff *skb;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (skb_queue_len(&info->xmit_queue))
		while ((skb = skb_dequeue(&info->xmit_queue)))
			kfree_skb(skb);
#ifdef CONFIG_ISDN_AUDIO
	if (skb_queue_len(&info->dtmf_queue))
		while ((skb = skb_dequeue(&info->dtmf_queue)))
			kfree_skb(skb);
#endif
	restore_flags(flags);
}

static void
isdn_tty_tint(modem_info * info)
{
	struct sk_buff *skb = skb_dequeue(&info->xmit_queue);
	int len,
	 slen;

	if (!skb)
		return;
	len = skb->len;
	if ((slen = isdn_writebuf_skb_stub(info->isdn_driver,
					   info->isdn_channel, 1, skb)) == len) {
		struct tty_struct *tty = info->tty;
		info->send_outstanding++;
		info->msr &= ~UART_MSR_CTS;
		info->lsr &= ~UART_LSR_TEMT;
		if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
		    tty->ldisc.write_wakeup)
			(tty->ldisc.write_wakeup) (tty);
		wake_up_interruptible(&tty->write_wait);
		return;
	}
	if (slen < 0) {
		/* Error: no channel, already shutdown, or wrong parameter */
		dev_kfree_skb(skb);
		return;
	}
	skb_queue_head(&info->xmit_queue, skb);
}

#ifdef CONFIG_ISDN_AUDIO
static int
isdn_tty_countDLE(unsigned char *buf, int len)
{
	int count = 0;

	while (len--)
		if (*buf++ == DLE)
			count++;
	return count;
}

/* This routine is called from within isdn_tty_write() to perform
 * DLE-decoding when sending audio-data.
 */
static int
isdn_tty_handleDLEdown(modem_info * info, atemu * m, int len)
{
	unsigned char *p = &info->xmit_buf[info->xmit_count];
	int count = 0;

	while (len > 0) {
		if (m->lastDLE) {
			m->lastDLE = 0;
			switch (*p) {
				case DLE:
					/* Escape code */
					if (len > 1)
						memmove(p, p + 1, len - 1);
					p--;
					count++;
					break;
				case ETX:
					/* End of data */
					info->vonline |= 4;
					return count;
				case DC4:
					/* Abort RX */
					info->vonline &= ~1;
#ifdef ISDN_DEBUG_MODEM_VOICE
					printk(KERN_DEBUG
					       "DLEdown: got DLE-DC4, send DLE-ETX on ttyI%d\n",
					       info->line);
#endif
					isdn_tty_at_cout("\020\003", info);
					if (!info->vonline) {
#ifdef ISDN_DEBUG_MODEM_VOICE
						printk(KERN_DEBUG
						       "DLEdown: send VCON on ttyI%d\n",
						       info->line);
#endif
						isdn_tty_at_cout("\r\nVCON\r\n", info);
					}
					/* Fall through */
				case 'q':
				case 's':
					/* Silence */
					if (len > 1)
						memmove(p, p + 1, len - 1);
					p--;
					break;
			}
		} else {
			if (*p == DLE)
				m->lastDLE = 1;
			else
				count++;
		}
		p++;
		len--;
	}
	if (len < 0) {
		printk(KERN_WARNING "isdn_tty: len<0 in DLEdown\n");
		return 0;
	}
	return count;
}

/* This routine is called from within isdn_tty_write() when receiving
 * audio-data. It interrupts receiving, if an character other than
 * ^S or ^Q is sent.
 */
static int
isdn_tty_end_vrx(const char *buf, int c, int from_user)
{
	char ch;

	while (c--) {
		if (from_user)
			get_user(ch, buf);
		else
			ch = *buf;
		if ((ch != 0x11) && (ch != 0x13))
			return 1;
		buf++;
	}
	return 0;
}

static int voice_cf[7] =
{0, 0, 4, 3, 2, 0, 0};

#endif                          /* CONFIG_ISDN_AUDIO */

/* isdn_tty_senddown() is called either directly from within isdn_tty_write()
 * or via timer-interrupt from within isdn_tty_modem_xmit(). It pulls
 * outgoing data from the tty's xmit-buffer, handles voice-decompression or
 * T.70 if necessary, and finally queues it up for sending via isdn_tty_tint.
 */
static void
isdn_tty_senddown(modem_info * info)
{
	int buflen;
	int skb_res;
#ifdef CONFIG_ISDN_AUDIO
	int audio_len;
#endif
	struct sk_buff *skb;

#ifdef CONFIG_ISDN_AUDIO
	if (info->vonline & 4) {
		info->vonline &= ~6;
		if (!info->vonline) {
#ifdef ISDN_DEBUG_MODEM_VOICE
			printk(KERN_DEBUG
			       "senddown: send VCON on ttyI%d\n",
			       info->line);
#endif
			isdn_tty_at_cout("\r\nVCON\r\n", info);
		}
	}
#endif
	if (!(buflen = info->xmit_count))
		return;
 	if ((info->emu.mdmreg[REG_CTS] & BIT_CTS) != 0)
		info->msr &= ~UART_MSR_CTS;
	info->lsr &= ~UART_LSR_TEMT;	
	/* info->xmit_count is modified here and in isdn_tty_write().
	 * So we return here if isdn_tty_write() is in the
	 * critical section.
	 */
	atomic_inc(&info->xmit_lock);
	if (!(atomic_dec_and_test(&info->xmit_lock)))
		return;
	if (info->isdn_driver < 0) {
		info->xmit_count = 0;
		return;
	}
	skb_res = dev->drv[info->isdn_driver]->interface->hl_hdrlen + 4;
#ifdef CONFIG_ISDN_AUDIO
	if (info->vonline & 2)
		audio_len = buflen * voice_cf[info->emu.vpar[3]];
	else
		audio_len = 0;
	skb = dev_alloc_skb(skb_res + buflen + audio_len);
#else
	skb = dev_alloc_skb(skb_res + buflen);
#endif
	if (!skb) {
		printk(KERN_WARNING
		       "isdn_tty: Out of memory in ttyI%d senddown\n",
		       info->line);
		return;
	}
	skb_reserve(skb, skb_res);
	memcpy(skb_put(skb, buflen), info->xmit_buf, buflen);
	info->xmit_count = 0;
#ifdef CONFIG_ISDN_AUDIO
	if (info->vonline & 2) {
		/* For now, ifmt is fixed to 1 (alaw), since this
		 * is used with ISDN everywhere in the world, except
		 * US, Canada and Japan.
		 * Later, when US-ISDN protocols are implemented,
		 * this setting will depend on the D-channel protocol.
		 */
		int ifmt = 1;

		/* voice conversion/decompression */
		switch (info->emu.vpar[3]) {
			case 2:
			case 3:
			case 4:
				/* adpcm, compatible to ZyXel 1496 modem
				 * with ROM revision 6.01
				 */
				audio_len = isdn_audio_adpcm2xlaw(info->adpcms,
								  ifmt,
								  skb->data,
						    skb_put(skb, audio_len),
								  buflen);
				skb_pull(skb, buflen);
				skb_trim(skb, audio_len);
				break;
			case 5:
				/* a-law */
				if (!ifmt)
					isdn_audio_alaw2ulaw(skb->data,
							     buflen);
				break;
			case 6:
				/* u-law */
				if (ifmt)
					isdn_audio_ulaw2alaw(skb->data,
							     buflen);
				break;
		}
	}
#endif                          /* CONFIG_ISDN_AUDIO */
	if (info->emu.mdmreg[REG_T70] & BIT_T70) {
		/* Add T.70 simplified header */
		if (info->emu.mdmreg[REG_T70] & BIT_T70_EXT)
			memcpy(skb_push(skb, 2), "\1\0", 2);
		else
			memcpy(skb_push(skb, 4), "\1\0\1\0", 4);
	}
	skb_queue_tail(&info->xmit_queue, skb);
}

/************************************************************
 *
 * Modem-functions
 *
 * mostly "stolen" from original Linux-serial.c and friends.
 *
 ************************************************************/

/* The next routine is called once from within timer-interrupt
 * triggered within isdn_tty_modem_ncarrier(). It calls
 * isdn_tty_modem_result() to stuff a "NO CARRIER" Message
 * into the tty's flip-buffer.
 */
static void
isdn_tty_modem_do_ncarrier(unsigned long data)
{
	modem_info *info = (modem_info *) data;
	isdn_tty_modem_result(3, info);
}

/* Next routine is called, whenever the DTR-signal is raised.
 * It checks the ncarrier-flag, and triggers the above routine
 * when necessary. The ncarrier-flag is set, whenever DTR goes
 * low.
 */
static void
isdn_tty_modem_ncarrier(modem_info * info)
{
	if (info->ncarrier) {
		info->nc_timer.expires = jiffies + HZ;
		info->nc_timer.function = isdn_tty_modem_do_ncarrier;
		info->nc_timer.data = (unsigned long) info;
		add_timer(&info->nc_timer);
	}
}

/*
 * return the usage calculated by si and layer 2 protocol
 */
int
isdn_calc_usage(int si, int l2)
{
	int usg = ISDN_USAGE_MODEM;

#ifdef CONFIG_ISDN_AUDIO
	if (si == 1) {
		switch(l2) {
			case ISDN_PROTO_L2_MODEM: 
				usg = ISDN_USAGE_MODEM;
				break;
#ifdef CONFIG_ISDN_TTY_FAX
			case ISDN_PROTO_L2_FAX: 
				usg = ISDN_USAGE_FAX;
				break;
#endif
			case ISDN_PROTO_L2_TRANS: 
			default:
				usg = ISDN_USAGE_VOICE;
				break;
		}
	}
#endif
	return(usg);
}

/* isdn_tty_dial() performs dialing of a tty an the necessary
 * setup of the lower levels before that.
 */
static void
isdn_tty_dial(char *n, modem_info * info, atemu * m)
{
	int usg = ISDN_USAGE_MODEM;
	int si = 7;
	int l2 = m->mdmreg[REG_L2PROT];
	isdn_ctrl cmd;
	ulong flags;
	int i;
	int j;

	for (j = 7; j >= 0; j--)
		if (m->mdmreg[REG_SI1] & (1 << j)) {
			si = bit2si[j];
			break;
		}
	usg = isdn_calc_usage(si, l2);
#ifdef CONFIG_ISDN_AUDIO
	if ((si == 1) && 
		(l2 != ISDN_PROTO_L2_MODEM)
#ifdef CONFIG_ISDN_TTY_FAX
		&& (l2 != ISDN_PROTO_L2_FAX)
#endif
		) {
		l2 = ISDN_PROTO_L2_TRANS;
		usg = ISDN_USAGE_VOICE;
	}
#endif
	m->mdmreg[REG_SI1I] = si2bit[si];
	save_flags(flags);
	cli();
	i = isdn_get_free_channel(usg, l2, m->mdmreg[REG_L3PROT], -1, -1);
	if (i < 0) {
		restore_flags(flags);
		isdn_tty_modem_result(6, info);
	} else {
		info->isdn_driver = dev->drvmap[i];
		info->isdn_channel = dev->chanmap[i];
		info->drv_index = i;
		dev->m_idx[i] = info->line;
		dev->usage[i] |= ISDN_USAGE_OUTGOING;
		info->last_dir = 1;
		strcpy(info->last_num, n);
		isdn_info_update();
		restore_flags(flags);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		cmd.command = ISDN_CMD_CLREAZ;
		isdn_command(&cmd);
		strcpy(cmd.parm.num, isdn_map_eaz2msn(m->msn, info->isdn_driver));
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETEAZ;
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL2;
		info->last_l2 = l2;
		cmd.arg = info->isdn_channel + (l2 << 8);
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL3;
		cmd.arg = info->isdn_channel + (m->mdmreg[REG_L3PROT] << 8);
#ifdef CONFIG_ISDN_TTY_FAX
		if (l2 == ISDN_PROTO_L2_FAX) {
			cmd.parm.fax = info->fax;
			info->fax->direction = ISDN_TTY_FAX_CONN_OUT;
		}
#endif
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		sprintf(cmd.parm.setup.phone, "%s", n);
		sprintf(cmd.parm.setup.eazmsn, "%s",
			isdn_map_eaz2msn(m->msn, info->isdn_driver));
		cmd.parm.setup.si1 = si;
		cmd.parm.setup.si2 = m->mdmreg[REG_SI2];
		cmd.command = ISDN_CMD_DIAL;
		info->dialing = 1;
		info->emu.carrierwait = 0;
		strcpy(dev->num[i], n);
		isdn_info_update();
		isdn_command(&cmd);
		isdn_timer_ctrl(ISDN_TIMER_CARRIER, 1);
	}
}

/* isdn_tty_hangup() disassociates a tty from the real
 * ISDN-line (hangup). The usage-status is cleared
 * and some cleanup is done also.
 */
void
isdn_tty_modem_hup(modem_info * info, int local)
{
	isdn_ctrl cmd;

	if (!info)
		return;
#ifdef ISDN_DEBUG_MODEM_HUP
	printk(KERN_DEBUG "Mhup ttyI%d\n", info->line);
#endif
	info->rcvsched = 0;
	isdn_tty_flush_buffer(info->tty);
	if (info->online) {
		info->last_lhup = local;
		info->online = 0;
		/* NO CARRIER message */
		isdn_tty_modem_result(3, info);
	}
#ifdef CONFIG_ISDN_AUDIO
	info->vonline = 0;
#ifdef CONFIG_ISDN_TTY_FAX
	info->faxonline = 0;
	info->fax->phase = ISDN_FAX_PHASE_IDLE;
#endif
	info->emu.vpar[4] = 0;
	info->emu.vpar[5] = 8;
	if (info->dtmf_state) {
		kfree(info->dtmf_state);
		info->dtmf_state = NULL;
	}
	if (info->silence_state) {
		kfree(info->silence_state);
		info->silence_state = NULL;
	}
	if (info->adpcms) {
		kfree(info->adpcms);
		info->adpcms = NULL;
	}
	if (info->adpcmr) {
		kfree(info->adpcmr);
		info->adpcmr = NULL;
	}
#endif
	if ((info->msr & UART_MSR_RI) &&
		(info->emu.mdmreg[REG_RUNG] & BIT_RUNG))
		isdn_tty_modem_result(12, info);
	info->msr &= ~(UART_MSR_DCD | UART_MSR_RI);
	info->lsr |= UART_LSR_TEMT;
	if (info->isdn_driver >= 0) {
		if (local) {
			cmd.driver = info->isdn_driver;
			cmd.command = ISDN_CMD_HANGUP;
			cmd.arg = info->isdn_channel;
			isdn_command(&cmd);
		}
		isdn_all_eaz(info->isdn_driver, info->isdn_channel);
		info->emu.mdmreg[REG_RINGCNT] = 0;
		isdn_free_channel(info->isdn_driver, info->isdn_channel, 0);
	}
	info->isdn_driver = -1;
	info->isdn_channel = -1;
	if (info->drv_index >= 0) {
		dev->m_idx[info->drv_index] = -1;
		info->drv_index = -1;
	}
}

/*
 * Begin of a CAPI like interface, currently used only for 
 * supplementary service (CAPI 2.0 part III)
 */
#include "avmb1/capicmd.h"  /* this should be moved in a common place */

int
isdn_tty_capi_facility(capi_msg *cm) {
	return(-1); /* dummy */
}

/* isdn_tty_suspend() tries to suspend the current tty connection
 */
static void
isdn_tty_suspend(char *id, modem_info * info, atemu * m)
{
	isdn_ctrl cmd;
	
	int l;

	if (!info)
		return;

#ifdef ISDN_DEBUG_MODEM_SERVICES
	printk(KERN_DEBUG "Msusp ttyI%d\n", info->line);
#endif
	l = strlen(id);
	if ((info->isdn_driver >= 0)) {
		cmd.parm.cmsg.Length = l+18;
		cmd.parm.cmsg.Command = CAPI_FACILITY;
		cmd.parm.cmsg.Subcommand = CAPI_REQ;
		cmd.parm.cmsg.adr.Controller = info->isdn_driver + 1;
		cmd.parm.cmsg.para[0] = 3; /* 16 bit 0x0003 suplementary service */
		cmd.parm.cmsg.para[1] = 0;
		cmd.parm.cmsg.para[2] = l + 3;
		cmd.parm.cmsg.para[3] = 4; /* 16 bit 0x0004 Suspend */
		cmd.parm.cmsg.para[4] = 0;
		cmd.parm.cmsg.para[5] = l;
		strncpy(&cmd.parm.cmsg.para[6], id, l);
		cmd.command = CAPI_PUT_MESSAGE;
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		isdn_command(&cmd);
	}
}

/* isdn_tty_resume() tries to resume a suspended call
 * setup of the lower levels before that. unfortunatly here is no
 * checking for compatibility of used protocols implemented by Q931
 * It does the same things like isdn_tty_dial, the last command
 * is different, may be we can merge it.
 */

static void
isdn_tty_resume(char *id, modem_info * info, atemu * m)
{
	int usg = ISDN_USAGE_MODEM;
	int si = 7;
	int l2 = m->mdmreg[REG_L2PROT];
	isdn_ctrl cmd;
	ulong flags;
	int i;
	int j;
	int l;

	l = strlen(id);
	for (j = 7; j >= 0; j--)
		if (m->mdmreg[REG_SI1] & (1 << j)) {
			si = bit2si[j];
			break;
		}
	usg = isdn_calc_usage(si, l2);
#ifdef CONFIG_ISDN_AUDIO
	if ((si == 1) && 
		(l2 != ISDN_PROTO_L2_MODEM)
#ifdef CONFIG_ISDN_TTY_FAX
		&& (l2 != ISDN_PROTO_L2_FAX)
#endif
		) {
		l2 = ISDN_PROTO_L2_TRANS;
		usg = ISDN_USAGE_VOICE;
	}
#endif
	m->mdmreg[REG_SI1I] = si2bit[si];
	save_flags(flags);
	cli();
	i = isdn_get_free_channel(usg, l2, m->mdmreg[REG_L3PROT], -1, -1);
	if (i < 0) {
		restore_flags(flags);
		isdn_tty_modem_result(6, info);
	} else {
		info->isdn_driver = dev->drvmap[i];
		info->isdn_channel = dev->chanmap[i];
		info->drv_index = i;
		dev->m_idx[i] = info->line;
		dev->usage[i] |= ISDN_USAGE_OUTGOING;
		info->last_dir = 1;
//		strcpy(info->last_num, n);
		isdn_info_update();
		restore_flags(flags);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		cmd.command = ISDN_CMD_CLREAZ;
		isdn_command(&cmd);
		strcpy(cmd.parm.num, isdn_map_eaz2msn(m->msn, info->isdn_driver));
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETEAZ;
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL2;
		info->last_l2 = l2;
		cmd.arg = info->isdn_channel + (l2 << 8);
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL3;
		cmd.arg = info->isdn_channel + (m->mdmreg[REG_L3PROT] << 8);
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		cmd.parm.cmsg.Length = l+18;
		cmd.parm.cmsg.Command = CAPI_FACILITY;
		cmd.parm.cmsg.Subcommand = CAPI_REQ;
		cmd.parm.cmsg.adr.Controller = info->isdn_driver + 1;
		cmd.parm.cmsg.para[0] = 3; /* 16 bit 0x0003 suplementary service */
		cmd.parm.cmsg.para[1] = 0;
		cmd.parm.cmsg.para[2] = l+3;
		cmd.parm.cmsg.para[3] = 5; /* 16 bit 0x0005 Resume */
		cmd.parm.cmsg.para[4] = 0;
		cmd.parm.cmsg.para[5] = l;
		strncpy(&cmd.parm.cmsg.para[6], id, l);
		cmd.command =CAPI_PUT_MESSAGE;
		info->dialing = 1;
//		strcpy(dev->num[i], n);
		isdn_info_update();
		isdn_command(&cmd);
		isdn_timer_ctrl(ISDN_TIMER_CARRIER, 1);
	}
}

/* isdn_tty_send_msg() sends a message to a HL driver
 * This is used for hybrid modem cards to send AT commands to it
 */

static void
isdn_tty_send_msg(modem_info * info, atemu * m, char *msg)
{
	int usg = ISDN_USAGE_MODEM;
	int si = 7;
	int l2 = m->mdmreg[REG_L2PROT];
	isdn_ctrl cmd;
	ulong flags;
	int i;
	int j;
	int l;

	l = strlen(msg);
	if (!l) {
		isdn_tty_modem_result(4, info);
		return;
	}
	for (j = 7; j >= 0; j--)
		if (m->mdmreg[REG_SI1] & (1 << j)) {
			si = bit2si[j];
			break;
		}
	usg = isdn_calc_usage(si, l2);
#ifdef CONFIG_ISDN_AUDIO
	if ((si == 1) && 
		(l2 != ISDN_PROTO_L2_MODEM)
#ifdef CONFIG_ISDN_TTY_FAX
		&& (l2 != ISDN_PROTO_L2_FAX)
#endif
		) {
		l2 = ISDN_PROTO_L2_TRANS;
		usg = ISDN_USAGE_VOICE;
	}
#endif
	m->mdmreg[REG_SI1I] = si2bit[si];
	save_flags(flags);
	cli();
	i = isdn_get_free_channel(usg, l2, m->mdmreg[REG_L3PROT], -1, -1);
	if (i < 0) {
		restore_flags(flags);
		isdn_tty_modem_result(6, info);
	} else {
		info->isdn_driver = dev->drvmap[i];
		info->isdn_channel = dev->chanmap[i];
		info->drv_index = i;
		dev->m_idx[i] = info->line;
		dev->usage[i] |= ISDN_USAGE_OUTGOING;
		info->last_dir = 1;
		isdn_info_update();
		restore_flags(flags);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		cmd.command = ISDN_CMD_CLREAZ;
		isdn_command(&cmd);
		strcpy(cmd.parm.num, isdn_map_eaz2msn(m->msn, info->isdn_driver));
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETEAZ;
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL2;
		info->last_l2 = l2;
		cmd.arg = info->isdn_channel + (l2 << 8);
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL3;
		cmd.arg = info->isdn_channel + (m->mdmreg[REG_L3PROT] << 8);
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		cmd.parm.cmsg.Length = l+14;
		cmd.parm.cmsg.Command = CAPI_MANUFACTURER;
		cmd.parm.cmsg.Subcommand = CAPI_REQ;
		cmd.parm.cmsg.adr.Controller = info->isdn_driver + 1;
		cmd.parm.cmsg.para[0] = l+1;
		strncpy(&cmd.parm.cmsg.para[1], msg, l);
		cmd.parm.cmsg.para[l+1] = 0xd;
		cmd.command =CAPI_PUT_MESSAGE;
/*		info->dialing = 1;
		strcpy(dev->num[i], n);
		isdn_info_update();
*/
		isdn_command(&cmd);
	}
}

static inline int
isdn_tty_paranoia_check(modem_info * info, kdev_t device, const char *routine)
{
#ifdef MODEM_PARANOIA_CHECK
	if (!info) {
		printk(KERN_WARNING "isdn_tty: null info_struct for (%d, %d) in %s\n",
		       MAJOR(device), MINOR(device), routine);
		return 1;
	}
	if (info->magic != ISDN_ASYNC_MAGIC) {
		printk(KERN_WARNING "isdn_tty: bad magic for modem struct (%d, %d) in %s\n",
		       MAJOR(device), MINOR(device), routine);
		return 1;
	}
#endif
	return 0;
}

/*
 * This routine is called to set the UART divisor registers to match
 * the specified baud rate for a serial port.
 */
static void
isdn_tty_change_speed(modem_info * info)
{
	uint cflag,
	 cval,
	 fcr,
	 quot;
	int i;

	if (!info->tty || !info->tty->termios)
		return;
	cflag = info->tty->termios->c_cflag;

	quot = i = cflag & CBAUD;
	if (i & CBAUDEX) {
		i &= ~CBAUDEX;
		if (i < 1 || i > 2)
			info->tty->termios->c_cflag &= ~CBAUDEX;
		else
			i += 15;
	}
	if (quot) {
		info->mcr |= UART_MCR_DTR;
		isdn_tty_modem_ncarrier(info);
	} else {
		info->mcr &= ~UART_MCR_DTR;
		if (info->emu.mdmreg[REG_DTRHUP] & BIT_DTRHUP) {
#ifdef ISDN_DEBUG_MODEM_HUP
			printk(KERN_DEBUG "Mhup in changespeed\n");
#endif
			if (info->online)
				info->ncarrier = 1;
			isdn_tty_modem_reset_regs(info, 0);
			isdn_tty_modem_hup(info, 1);
		}
		return;
	}
	/* byte size and parity */
	cval = cflag & (CSIZE | CSTOPB);
	cval >>= 4;
	if (cflag & PARENB)
		cval |= UART_LCR_PARITY;
	if (!(cflag & PARODD))
		cval |= UART_LCR_EPAR;
	fcr = 0;

	/* CTS flow control flag and modem status interrupts */
	if (cflag & CRTSCTS) {
		info->flags |= ISDN_ASYNC_CTS_FLOW;
	} else
		info->flags &= ~ISDN_ASYNC_CTS_FLOW;
	if (cflag & CLOCAL)
		info->flags &= ~ISDN_ASYNC_CHECK_CD;
	else {
		info->flags |= ISDN_ASYNC_CHECK_CD;
	}
}

static int
isdn_tty_startup(modem_info * info)
{
	ulong flags;

	if (info->flags & ISDN_ASYNC_INITIALIZED)
		return 0;
	save_flags(flags);
	cli();
	isdn_MOD_INC_USE_COUNT();
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "starting up ttyi%d ...\n", info->line);
#endif
	/*
	 * Now, initialize the UART
	 */
	info->mcr = UART_MCR_DTR | UART_MCR_RTS | UART_MCR_OUT2;
	if (info->tty)
		clear_bit(TTY_IO_ERROR, &info->tty->flags);
	/*
	 * and set the speed of the serial port
	 */
	isdn_tty_change_speed(info);

	info->flags |= ISDN_ASYNC_INITIALIZED;
	info->msr |= (UART_MSR_DSR | UART_MSR_CTS);
	info->send_outstanding = 0;
	restore_flags(flags);
	return 0;
}

/*
 * This routine will shutdown a serial port; interrupts are disabled, and
 * DTR is dropped if the hangup on close termio flag is on.
 */
static void
isdn_tty_shutdown(modem_info * info)
{
	ulong flags;

	if (!(info->flags & ISDN_ASYNC_INITIALIZED))
		return;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "Shutting down isdnmodem port %d ....\n", info->line);
#endif
	save_flags(flags);
	cli();                  /* Disable interrupts */
	isdn_MOD_DEC_USE_COUNT();
	info->msr &= ~UART_MSR_RI;
	if (!info->tty || (info->tty->termios->c_cflag & HUPCL)) {
		info->mcr &= ~(UART_MCR_DTR | UART_MCR_RTS);
		if (info->emu.mdmreg[REG_DTRHUP] & BIT_DTRHUP) {
			isdn_tty_modem_reset_regs(info, 0);
#ifdef ISDN_DEBUG_MODEM_HUP
			printk(KERN_DEBUG "Mhup in isdn_tty_shutdown\n");
#endif
			isdn_tty_modem_hup(info, 1);
		}
	}
	if (info->tty)
		set_bit(TTY_IO_ERROR, &info->tty->flags);

	info->flags &= ~ISDN_ASYNC_INITIALIZED;
	restore_flags(flags);
}

/* isdn_tty_write() is the main send-routine. It is called from the upper
 * levels within the kernel to perform sending data. Depending on the
 * online-flag it either directs output to the at-command-interpreter or
 * to the lower level. Additional tasks done here:
 *  - If online, check for escape-sequence (+++)
 *  - If sending audio-data, call isdn_tty_DLEdown() to parse DLE-codes.
 *  - If receiving audio-data, call isdn_tty_end_vrx() to abort if needed.
 *  - If dialing, abort dial.
 */
static int
isdn_tty_write(struct tty_struct *tty, int from_user, const u_char * buf, int count)
{
	int c;
	int total = 0;
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_write"))
		return 0;
	if (!tty)
		return 0;
	if (from_user)
		down(&info->write_sem);
	/* See isdn_tty_senddown() */
	atomic_inc(&info->xmit_lock);
	while (1) {
		c = MIN(count, info->xmit_size - info->xmit_count);
		if (info->isdn_driver >= 0)
			c = MIN(c, dev->drv[info->isdn_driver]->maxbufsize);
		if (c <= 0)
			break;
		if ((info->online > 1)
#ifdef CONFIG_ISDN_AUDIO
		    || (info->vonline & 3)
#endif
			) {
			atemu *m = &info->emu;

#ifdef CONFIG_ISDN_AUDIO
			if (!info->vonline)
#endif
				isdn_tty_check_esc(buf, m->mdmreg[REG_ESC], c,
						   &(m->pluscount),
						   &(m->lastplus),
						   from_user);
			if (from_user)
				copy_from_user(&(info->xmit_buf[info->xmit_count]), buf, c);
			else
				memcpy(&(info->xmit_buf[info->xmit_count]), buf, c);
#ifdef CONFIG_ISDN_AUDIO
			if (info->vonline) {
				int cc = isdn_tty_handleDLEdown(info, m, c);
				if (info->vonline & 2) {
					if (!cc) {
						/* If DLE decoding results in zero-transmit, but
						 * c originally was non-zero, do a wakeup.
						 */
						if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
						 tty->ldisc.write_wakeup)
							(tty->ldisc.write_wakeup) (tty);
						wake_up_interruptible(&tty->write_wait);
						info->msr |= UART_MSR_CTS;
						info->lsr |= UART_LSR_TEMT;
					}
					info->xmit_count += cc;
				}
				if ((info->vonline & 3) == 1) {
					/* Do NOT handle Ctrl-Q or Ctrl-S
					 * when in full-duplex audio mode.
					 */
					if (isdn_tty_end_vrx(buf, c, from_user)) {
						info->vonline &= ~1;
#ifdef ISDN_DEBUG_MODEM_VOICE
						printk(KERN_DEBUG
						       "got !^Q/^S, send DLE-ETX,VCON on ttyI%d\n",
						       info->line);
#endif
						isdn_tty_at_cout("\020\003\r\nVCON\r\n", info);
					}
				}
			} else
#endif
				info->xmit_count += c;
		} else {
			info->msr |= UART_MSR_CTS;
			info->lsr |= UART_LSR_TEMT;
			if (info->dialing) {
				info->dialing = 0;
#ifdef ISDN_DEBUG_MODEM_HUP
				printk(KERN_DEBUG "Mhup in isdn_tty_write\n");
#endif
				isdn_tty_modem_result(3, info);
				isdn_tty_modem_hup(info, 1);
			} else
				c = isdn_tty_edit_at(buf, c, info, from_user);
		}
		buf += c;
		count -= c;
		total += c;
	}
	if ((info->xmit_count) || (skb_queue_len(&info->xmit_queue)))
		isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, 1);
	atomic_dec(&info->xmit_lock);
	if (from_user)
		up(&info->write_sem);
	return total;
}

static int
isdn_tty_write_room(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;
	int ret;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_write_room"))
		return 0;
	if (!info->online)
		return info->xmit_size;
	ret = info->xmit_size - info->xmit_count;
	return (ret < 0) ? 0 : ret;
}

static int
isdn_tty_chars_in_buffer(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_chars_in_buffer"))
		return 0;
	if (!info->online)
		return 0;
	return (info->xmit_count);
}

static void
isdn_tty_flush_buffer(struct tty_struct *tty)
{
	modem_info *info;
	unsigned long flags;

	save_flags(flags);
	cli();
	if (!tty) {
		restore_flags(flags);
		return;
	}
	info = (modem_info *) tty->driver_data;
	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_flush_buffer")) {
		restore_flags(flags);
		return;
	}
	isdn_tty_cleanup_xmit(info);
	info->xmit_count = 0;
	restore_flags(flags);
	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup) (tty);
}

static void
isdn_tty_flush_chars(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_flush_chars"))
		return;
	if ((info->xmit_count) || (skb_queue_len(&info->xmit_queue)))
		isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, 1);
}

/*
 * ------------------------------------------------------------
 * isdn_tty_throttle()
 *
 * This routine is called by the upper-layer tty layer to signal that
 * incoming characters should be throttled.
 * ------------------------------------------------------------
 */
static void
isdn_tty_throttle(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_throttle"))
		return;
	if (I_IXOFF(tty))
		info->x_char = STOP_CHAR(tty);
	info->mcr &= ~UART_MCR_RTS;
}

static void
isdn_tty_unthrottle(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_unthrottle"))
		return;
	if (I_IXOFF(tty)) {
		if (info->x_char)
			info->x_char = 0;
		else
			info->x_char = START_CHAR(tty);
	}
	info->mcr |= UART_MCR_RTS;
}

/*
 * ------------------------------------------------------------
 * isdn_tty_ioctl() and friends
 * ------------------------------------------------------------
 */

/*
 * isdn_tty_get_lsr_info - get line status register info
 *
 * Purpose: Let user call ioctl() to get info when the UART physically
 *          is emptied.  On bus types like RS485, the transmitter must
 *          release the bus after transmitting. This must be done when
 *          the transmit shift register is empty, not be done when the
 *          transmit holding register is empty.  This functionality
 *          allows RS485 driver to be written in user space.
 */
static int
isdn_tty_get_lsr_info(modem_info * info, uint * value)
{
	u_char status;
	uint result;
	ulong flags;

	save_flags(flags);
	cli();
	status = info->lsr;
	restore_flags(flags);
	result = ((status & UART_LSR_TEMT) ? TIOCSER_TEMT : 0);
	put_user(result, (uint *) value);
	return 0;
}


static int
isdn_tty_get_modem_info(modem_info * info, uint * value)
{
	u_char control,
	 status;
	uint result;
	ulong flags;

	control = info->mcr;
	save_flags(flags);
	cli();
	status = info->msr;
	restore_flags(flags);
	result = ((control & UART_MCR_RTS) ? TIOCM_RTS : 0)
	    | ((control & UART_MCR_DTR) ? TIOCM_DTR : 0)
	    | ((status & UART_MSR_DCD) ? TIOCM_CAR : 0)
	    | ((status & UART_MSR_RI) ? TIOCM_RNG : 0)
	    | ((status & UART_MSR_DSR) ? TIOCM_DSR : 0)
	    | ((status & UART_MSR_CTS) ? TIOCM_CTS : 0);
	put_user(result, (uint *) value);
	return 0;
}

static int
isdn_tty_set_modem_info(modem_info * info, uint cmd, uint * value)
{
	uint arg;
	int pre_dtr;

	get_user(arg, (uint *) value);
	switch (cmd) {
		case TIOCMBIS:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCMBIS\n", info->line);
#endif
			if (arg & TIOCM_RTS) {
				info->mcr |= UART_MCR_RTS;
			}
			if (arg & TIOCM_DTR) {
				info->mcr |= UART_MCR_DTR;
				isdn_tty_modem_ncarrier(info);
			}
			break;
		case TIOCMBIC:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCMBIC\n", info->line);
#endif
			if (arg & TIOCM_RTS) {
				info->mcr &= ~UART_MCR_RTS;
			}
			if (arg & TIOCM_DTR) {
				info->mcr &= ~UART_MCR_DTR;
				if (info->emu.mdmreg[REG_DTRHUP] & BIT_DTRHUP) {
					isdn_tty_modem_reset_regs(info, 0);
#ifdef ISDN_DEBUG_MODEM_HUP
					printk(KERN_DEBUG "Mhup in TIOCMBIC\n");
#endif
					if (info->online)
						info->ncarrier = 1;
					isdn_tty_modem_hup(info, 1);
				}
			}
			break;
		case TIOCMSET:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCMSET\n", info->line);
#endif
			pre_dtr = (info->mcr & UART_MCR_DTR);
			info->mcr = ((info->mcr & ~(UART_MCR_RTS | UART_MCR_DTR))
				 | ((arg & TIOCM_RTS) ? UART_MCR_RTS : 0)
			       | ((arg & TIOCM_DTR) ? UART_MCR_DTR : 0));
			if (pre_dtr |= (info->mcr & UART_MCR_DTR)) {
				if (!(info->mcr & UART_MCR_DTR)) {
					if (info->emu.mdmreg[REG_DTRHUP] & BIT_DTRHUP) {
						isdn_tty_modem_reset_regs(info, 0);
#ifdef ISDN_DEBUG_MODEM_HUP
						printk(KERN_DEBUG "Mhup in TIOCMSET\n");
#endif
						if (info->online)
							info->ncarrier = 1;
						isdn_tty_modem_hup(info, 1);
					}
				} else
					isdn_tty_modem_ncarrier(info);
			}
			break;
		default:
			return -EINVAL;
	}
	return 0;
}

static int
isdn_tty_ioctl(struct tty_struct *tty, struct file *file,
	       uint cmd, ulong arg)
{
	modem_info *info = (modem_info *) tty->driver_data;
	int error;
	int retval;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_ioctl"))
		return -ENODEV;
	if (tty->flags & (1 << TTY_IO_ERROR))
		return -EIO;
	switch (cmd) {
		case TCSBRK:   /* SVID version: non-zero arg --> no break */
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TCSBRK\n", info->line);
#endif
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			return 0;
		case TCSBRKP:  /* support for POSIX tcsendbreak() */
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TCSBRKP\n", info->line);
#endif
			retval = tty_check_change(tty);
			if (retval)
				return retval;
			tty_wait_until_sent(tty, 0);
			return 0;
		case TIOCGSOFTCAR:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCGSOFTCAR\n", info->line);
#endif
			error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(long));
			if (error)
				return error;
			put_user(C_CLOCAL(tty) ? 1 : 0, (ulong *) arg);
			return 0;
		case TIOCSSOFTCAR:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCSSOFTCAR\n", info->line);
#endif
			error = verify_area(VERIFY_READ, (void *) arg, sizeof(long));
			if (error)
				return error;
			get_user(arg, (ulong *) arg);
			tty->termios->c_cflag =
			    ((tty->termios->c_cflag & ~CLOCAL) |
			     (arg ? CLOCAL : 0));
			return 0;
		case TIOCMGET:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCMGET\n", info->line);
#endif
			error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(uint));
			if (error)
				return error;
			return isdn_tty_get_modem_info(info, (uint *) arg);
		case TIOCMBIS:
		case TIOCMBIC:
		case TIOCMSET:
			error = verify_area(VERIFY_READ, (void *) arg, sizeof(uint));
			if (error)
				return error;
			return isdn_tty_set_modem_info(info, cmd, (uint *) arg);
		case TIOCSERGETLSR:	/* Get line status register */
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "ttyI%d ioctl TIOCSERGETLSR\n", info->line);
#endif
			error = verify_area(VERIFY_WRITE, (void *) arg, sizeof(uint));
			if (error)
				return error;
			else
				return isdn_tty_get_lsr_info(info, (uint *) arg);
		default:
#ifdef ISDN_DEBUG_MODEM_IOCTL
			printk(KERN_DEBUG "UNKNOWN ioctl 0x%08x on ttyi%d\n", cmd, info->line);
#endif
			return -ENOIOCTLCMD;
	}
	return 0;
}

static void
isdn_tty_set_termios(struct tty_struct *tty, struct termios *old_termios)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (!old_termios)
		isdn_tty_change_speed(info);
	else {
		if (tty->termios->c_cflag == old_termios->c_cflag)
			return;
		isdn_tty_change_speed(info);
		if ((old_termios->c_cflag & CRTSCTS) &&
		    !(tty->termios->c_cflag & CRTSCTS)) {
			tty->hw_stopped = 0;
		}
	}
}

/*
 * ------------------------------------------------------------
 * isdn_tty_open() and friends
 * ------------------------------------------------------------
 */
static int
isdn_tty_block_til_ready(struct tty_struct *tty, struct file *filp, modem_info * info)
{
	DECLARE_WAITQUEUE(wait, NULL);
	int do_clocal = 0;
	unsigned long flags;
	int retval;

	/*
	 * If the device is in the middle of being closed, then block
	 * until it's done, and then try again.
	 */
	if (tty_hung_up_p(filp) ||
	    (info->flags & ISDN_ASYNC_CLOSING)) {
		if (info->flags & ISDN_ASYNC_CLOSING)
			interruptible_sleep_on(&info->close_wait);
#ifdef MODEM_DO_RESTART
		if (info->flags & ISDN_ASYNC_HUP_NOTIFY)
			return -EAGAIN;
		else
			return -ERESTARTSYS;
#else
		return -EAGAIN;
#endif
	}
	/*
	 * If this is a callout device, then just make sure the normal
	 * device isn't being used.
	 */
	if (tty->driver.subtype == ISDN_SERIAL_TYPE_CALLOUT) {
		if (info->flags & ISDN_ASYNC_NORMAL_ACTIVE)
			return -EBUSY;
		if ((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ISDN_ASYNC_SESSION_LOCKOUT) &&
		    (info->session != current->session))
			return -EBUSY;
		if ((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    (info->flags & ISDN_ASYNC_PGRP_LOCKOUT) &&
		    (info->pgrp != current->pgrp))
			return -EBUSY;
		info->flags |= ISDN_ASYNC_CALLOUT_ACTIVE;
		return 0;
	}
	/*
	 * If non-blocking mode is set, then make the check up front
	 * and then exit.
	 */
	if ((filp->f_flags & O_NONBLOCK) ||
	    (tty->flags & (1 << TTY_IO_ERROR))) {
		if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE)
			return -EBUSY;
		info->flags |= ISDN_ASYNC_NORMAL_ACTIVE;
		return 0;
	}
	if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) {
		if (info->normal_termios.c_cflag & CLOCAL)
			do_clocal = 1;
	} else {
		if (tty->termios->c_cflag & CLOCAL)
			do_clocal = 1;
	}
	/*
	 * Block waiting for the carrier detect and the line to become
	 * free (i.e., not in use by the callout).  While we are in
	 * this loop, info->count is dropped by one, so that
	 * isdn_tty_close() knows when to free things.  We restore it upon
	 * exit, either normal or abnormal.
	 */
	retval = 0;
	add_wait_queue(&info->open_wait, &wait);
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_block_til_ready before block: ttyi%d, count = %d\n",
	       info->line, info->count);
#endif
	save_flags(flags);
	cli();
	if (!(tty_hung_up_p(filp)))
		info->count--;
	restore_flags(flags);
	info->blocked_open++;
	while (1) {
		set_current_state(TASK_INTERRUPTIBLE);
		if (tty_hung_up_p(filp) ||
		    !(info->flags & ISDN_ASYNC_INITIALIZED)) {
#ifdef MODEM_DO_RESTART
			if (info->flags & ISDN_ASYNC_HUP_NOTIFY)
				retval = -EAGAIN;
			else
				retval = -ERESTARTSYS;
#else
			retval = -EAGAIN;
#endif
			break;
		}
		if (!(info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		    !(info->flags & ISDN_ASYNC_CLOSING) &&
		    (do_clocal || (info->msr & UART_MSR_DCD))) {
			break;
		}
		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			break;
		}
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_block_til_ready blocking: ttyi%d, count = %d\n",
		       info->line, info->count);
#endif
		schedule();
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&info->open_wait, &wait);
	if (!tty_hung_up_p(filp))
		info->count++;
	info->blocked_open--;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_block_til_ready after blocking: ttyi%d, count = %d\n",
	       info->line, info->count);
#endif
	if (retval)
		return retval;
	info->flags |= ISDN_ASYNC_NORMAL_ACTIVE;
	return 0;
}

/*
 * This routine is called whenever a serial port is opened.  It
 * enables interrupts for a serial port, linking in its async structure into
 * the IRQ chain.   It also performs the serial-specific
 * initialization for the tty structure.
 */
static int
isdn_tty_open(struct tty_struct *tty, struct file *filp)
{
	modem_info *info;
	int retval,
	 line;

	line = MINOR(tty->device) - tty->driver.minor_start;
	if (line < 0 || line > ISDN_MAX_CHANNELS)
		return -ENODEV;
	info = &dev->mdm.info[line];
	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_open"))
		return -ENODEV;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open %s%d, count = %d\n", tty->driver.name,
	       info->line, info->count);
#endif
	info->count++;
	tty->driver_data = info;
	info->tty = tty;
	/*
	 * Start up serial port
	 */
	retval = isdn_tty_startup(info);
	if (retval) {
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_open return after startup\n");
#endif
		return retval;
	}
	retval = isdn_tty_block_til_ready(tty, filp, info);
	if (retval) {
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_open return after isdn_tty_block_til_ready \n");
#endif
		return retval;
	}
	if ((info->count == 1) && (info->flags & ISDN_ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == ISDN_SERIAL_TYPE_NORMAL)
			*tty->termios = info->normal_termios;
		else
			*tty->termios = info->callout_termios;
		isdn_tty_change_speed(info);
	}
	info->session = current->session;
	info->pgrp = current->pgrp;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open ttyi%d successful...\n", info->line);
#endif
	dev->modempoll++;
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_open normal exit\n");
#endif
	return 0;
}

static void
isdn_tty_close(struct tty_struct *tty, struct file *filp)
{
	modem_info *info = (modem_info *) tty->driver_data;
	ulong flags;
	ulong timeout;

	if (!info || isdn_tty_paranoia_check(info, tty->device, "isdn_tty_close"))
		return;
	save_flags(flags);
	cli();
	if (tty_hung_up_p(filp)) {
		restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_close return after tty_hung_up_p\n");
#endif
		return;
	}
	if ((tty->count == 1) && (info->count != 1)) {
		/*
		 * Uh, oh.  tty->count is 1, which means that the tty
		 * structure will be freed.  Info->count should always
		 * be one in these conditions.  If it's greater than
		 * one, we've got real problems, since it means the
		 * serial port won't be shutdown.
		 */
		printk(KERN_ERR "isdn_tty_close: bad port count; tty->count is 1, "
		       "info->count is %d\n", info->count);
		info->count = 1;
	}
	if (--info->count < 0) {
		printk(KERN_ERR "isdn_tty_close: bad port count for ttyi%d: %d\n",
		       info->line, info->count);
		info->count = 0;
	}
	if (info->count) {
		restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
		printk(KERN_DEBUG "isdn_tty_close after info->count != 0\n");
#endif
		return;
	}
	info->flags |= ISDN_ASYNC_CLOSING;
	/*
	 * Save the termios structure, since this port may have
	 * separate termios for callout and dialin.
	 */
	if (info->flags & ISDN_ASYNC_NORMAL_ACTIVE)
		info->normal_termios = *tty->termios;
	if (info->flags & ISDN_ASYNC_CALLOUT_ACTIVE)
		info->callout_termios = *tty->termios;

	tty->closing = 1;
	/*
	 * At this point we stop accepting input.  To do this, we
	 * disable the receive line status interrupts, and tell the
	 * interrupt driver to stop checking the data ready bit in the
	 * line status register.
	 */
	if (info->flags & ISDN_ASYNC_INITIALIZED) {
		tty_wait_until_sent(tty, 3000);	/* 30 seconds timeout */
		/*
		 * Before we drop DTR, make sure the UART transmitter
		 * has completely drained; this is especially
		 * important if there is a transmit FIFO!
		 */
		timeout = jiffies + HZ;
		while (!(info->lsr & UART_LSR_TEMT)) {
			set_current_state(TASK_INTERRUPTIBLE);
			schedule_timeout(20);
			if (time_after(jiffies,timeout))
				break;
		}
	}
	dev->modempoll--;
	isdn_tty_shutdown(info);
	if (tty->driver.flush_buffer)
		tty->driver.flush_buffer(tty);
	if (tty->ldisc.flush_buffer)
		tty->ldisc.flush_buffer(tty);
	info->tty = 0;
	info->ncarrier = 0;
	tty->closing = 0;
	if (info->blocked_open) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(50);
		wake_up_interruptible(&info->open_wait);
	}
	info->flags &= ~(ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE |
			 ISDN_ASYNC_CLOSING);
	wake_up_interruptible(&info->close_wait);
	restore_flags(flags);
#ifdef ISDN_DEBUG_MODEM_OPEN
	printk(KERN_DEBUG "isdn_tty_close normal exit\n");
#endif
}

/*
 * isdn_tty_hangup() --- called by tty_hangup() when a hangup is signaled.
 */
static void
isdn_tty_hangup(struct tty_struct *tty)
{
	modem_info *info = (modem_info *) tty->driver_data;

	if (isdn_tty_paranoia_check(info, tty->device, "isdn_tty_hangup"))
		return;
	isdn_tty_shutdown(info);
	info->count = 0;
	info->flags &= ~(ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE);
	info->tty = 0;
	wake_up_interruptible(&info->open_wait);
}

/* This routine initializes all emulator-data.
 */
static void
isdn_tty_reset_profile(atemu * m)
{
	m->profile[0] = 0;
	m->profile[1] = 0;
	m->profile[2] = 43;
	m->profile[3] = 13;
	m->profile[4] = 10;
	m->profile[5] = 8;
	m->profile[6] = 3;
	m->profile[7] = 60;
	m->profile[8] = 2;
	m->profile[9] = 6;
	m->profile[10] = 7;
	m->profile[11] = 70;
	m->profile[12] = 0x45;
	m->profile[13] = 4;
	m->profile[14] = ISDN_PROTO_L2_X75I;
	m->profile[15] = ISDN_PROTO_L3_TRANS;
	m->profile[16] = ISDN_SERIAL_XMIT_SIZE / 16;
	m->profile[17] = ISDN_MODEM_WINSIZE;
	m->profile[18] = 4;
	m->profile[19] = 0;
	m->profile[20] = 0;
	m->profile[23] = 0;
	m->pmsn[0] = '\0';
	m->plmsn[0] = '\0';
}

#ifdef CONFIG_ISDN_AUDIO
static void
isdn_tty_modem_reset_vpar(atemu * m)
{
	m->vpar[0] = 2;         /* Voice-device            (2 = phone line) */
	m->vpar[1] = 0;         /* Silence detection level (0 = none      ) */
	m->vpar[2] = 70;        /* Silence interval        (7 sec.        ) */
	m->vpar[3] = 2;         /* Compression type        (1 = ADPCM-2   ) */
	m->vpar[4] = 0;         /* DTMF detection level    (0 = softcode  ) */
	m->vpar[5] = 8;         /* DTMF interval           (8 * 5 ms.     ) */
}
#endif

#ifdef CONFIG_ISDN_TTY_FAX
static void
isdn_tty_modem_reset_faxpar(modem_info * info)
{
	T30_s *f = info->fax;

	f->code = 0;
	f->phase = ISDN_FAX_PHASE_IDLE;
	f->direction = 0;
	f->resolution = 1;	/* fine */
	f->rate = 5;		/* 14400 bit/s */
	f->width = 0;
	f->length = 0;
	f->compression = 0;
	f->ecm = 0;
	f->binary = 0;
	f->scantime = 0;
	memset(&f->id[0], 32, FAXIDLEN - 1);
	f->id[FAXIDLEN - 1] = 0;
	f->badlin = 0;
	f->badmul = 0;
	f->bor = 0;
	f->nbc = 0;
	f->cq = 0;
	f->cr = 0;
	f->ctcrty = 0;
	f->minsp = 0;
	f->phcto = 30;
	f->rel = 0;
	memset(&f->pollid[0], 32, FAXIDLEN - 1);
	f->pollid[FAXIDLEN - 1] = 0;
}
#endif

static void
isdn_tty_modem_reset_regs(modem_info * info, int force)
{
	atemu *m = &info->emu;
	if ((m->mdmreg[REG_DTRR] & BIT_DTRR) || force) {
		memcpy(m->mdmreg, m->profile, ISDN_MODEM_ANZREG);
		memcpy(m->msn, m->pmsn, ISDN_MSNLEN);
		memcpy(m->lmsn, m->plmsn, ISDN_LMSNLEN);
		info->xmit_size = m->mdmreg[REG_PSIZE] * 16;
	}
#ifdef CONFIG_ISDN_AUDIO
	isdn_tty_modem_reset_vpar(m);
#endif
#ifdef CONFIG_ISDN_TTY_FAX
	isdn_tty_modem_reset_faxpar(info);
#endif
	m->mdmcmdl = 0;
}

static void
modem_write_profile(atemu * m)
{
	memcpy(m->profile, m->mdmreg, ISDN_MODEM_ANZREG);
	memcpy(m->pmsn, m->msn, ISDN_MSNLEN);
	memcpy(m->plmsn, m->lmsn, ISDN_LMSNLEN);
	if (dev->profd)
		send_sig(SIGIO, dev->profd, 1);
}

int
isdn_tty_modem_init(void)
{
	modem *m;
	int i;
	modem_info *info;

	m = &dev->mdm;
	memset(&m->tty_modem, 0, sizeof(struct tty_driver));
	m->tty_modem.magic = TTY_DRIVER_MAGIC;
	m->tty_modem.name = isdn_ttyname_ttyI;
	m->tty_modem.major = ISDN_TTY_MAJOR;
	m->tty_modem.minor_start = 0;
	m->tty_modem.num = ISDN_MAX_CHANNELS;
	m->tty_modem.type = TTY_DRIVER_TYPE_SERIAL;
	m->tty_modem.subtype = ISDN_SERIAL_TYPE_NORMAL;
	m->tty_modem.init_termios = tty_std_termios;
	m->tty_modem.init_termios.c_cflag = B9600 | CS8 | CREAD | HUPCL | CLOCAL;
	m->tty_modem.flags = TTY_DRIVER_REAL_RAW;
	m->tty_modem.refcount = &m->refcount;
	m->tty_modem.table = m->modem_table;
	m->tty_modem.termios = m->modem_termios;
	m->tty_modem.termios_locked = m->modem_termios_locked;
	m->tty_modem.open = isdn_tty_open;
	m->tty_modem.close = isdn_tty_close;
	m->tty_modem.write = isdn_tty_write;
	m->tty_modem.put_char = NULL;
	m->tty_modem.flush_chars = isdn_tty_flush_chars;
	m->tty_modem.write_room = isdn_tty_write_room;
	m->tty_modem.chars_in_buffer = isdn_tty_chars_in_buffer;
	m->tty_modem.flush_buffer = isdn_tty_flush_buffer;
	m->tty_modem.ioctl = isdn_tty_ioctl;
	m->tty_modem.throttle = isdn_tty_throttle;
	m->tty_modem.unthrottle = isdn_tty_unthrottle;
	m->tty_modem.set_termios = isdn_tty_set_termios;
	m->tty_modem.stop = NULL;
	m->tty_modem.start = NULL;
	m->tty_modem.hangup = isdn_tty_hangup;
	m->tty_modem.driver_name = "isdn_tty";
	/*
	 * The callout device is just like normal device except for
	 * major number and the subtype code.
	 */
	m->cua_modem = m->tty_modem;
	m->cua_modem.name = isdn_ttyname_cui;
	m->cua_modem.major = ISDN_TTYAUX_MAJOR;
	m->tty_modem.minor_start = 0;
	m->cua_modem.subtype = ISDN_SERIAL_TYPE_CALLOUT;

	if (tty_register_driver(&m->tty_modem)) {
		printk(KERN_WARNING "isdn_tty: Couldn't register modem-device\n");
		return -1;
	}
	if (tty_register_driver(&m->cua_modem)) {
		printk(KERN_WARNING "isdn_tty: Couldn't register modem-callout-device\n");
		return -2;
	}
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		info = &m->info[i];
#ifdef CONFIG_ISDN_TTY_FAX
		if (!(info->fax = kmalloc(sizeof(T30_s), GFP_KERNEL))) {
			printk(KERN_ERR "Could not allocate fax t30-buffer\n");
			return -3;
		}
#endif
		init_MUTEX(&info->write_sem);
		sprintf(info->last_cause, "0000");
		sprintf(info->last_num, "none");
		info->last_dir = 0;
		info->last_lhup = 1;
		info->last_l2 = -1;
		info->last_si = 0;
		isdn_tty_reset_profile(&info->emu);
		isdn_tty_modem_reset_regs(info, 1);
		info->magic = ISDN_ASYNC_MAGIC;
		info->line = i;
		info->tty = 0;
		info->x_char = 0;
		info->count = 0;
		info->blocked_open = 0;
		info->callout_termios = m->cua_modem.init_termios;
		info->normal_termios = m->tty_modem.init_termios;
		init_waitqueue_head(&info->open_wait);
		init_waitqueue_head(&info->close_wait);
		info->isdn_driver = -1;
		info->isdn_channel = -1;
		info->drv_index = -1;
		info->xmit_size = ISDN_SERIAL_XMIT_SIZE;
		skb_queue_head_init(&info->xmit_queue);
#ifdef CONFIG_ISDN_AUDIO
		skb_queue_head_init(&info->dtmf_queue);
#endif
		if (!(info->xmit_buf = kmalloc(ISDN_SERIAL_XMIT_MAX + 5, GFP_KERNEL))) {
			printk(KERN_ERR "Could not allocate modem xmit-buffer\n");
			return -3;
		}
		/* Make room for T.70 header */
		info->xmit_buf += 4;
	}
	return 0;
}

static int
isdn_tty_match_icall(char *cid, atemu *emu, int di)
{
#ifdef ISDN_DEBUG_MODEM_ICALL
	printk(KERN_DEBUG "m_fi: msn=%s lmsn=%s mmsn=%s mreg[SI1]=%d mreg[SI2]=%d\n",
	       emu->msn, emu->lmsn, isdn_map_eaz2msn(emu->msn, di),
	       emu->mdmreg[REG_SI1], emu->mdmreg[REG_SI2]);
#endif
	if (strlen(emu->lmsn)) {
		char *p = emu->lmsn;
		char *q;
		int  tmp;
		int  ret = 0;

		while (1) {
			if ((q = strchr(p, ';')))
				*q = '\0';
			if ((tmp = isdn_wildmat(cid, isdn_map_eaz2msn(p, di))) > ret)
				ret = tmp;
#ifdef ISDN_DEBUG_MODEM_ICALL
			printk(KERN_DEBUG "m_fi: lmsnX=%s mmsn=%s -> tmp=%d\n",
			       p, isdn_map_eaz2msn(emu->msn, di), tmp);
#endif
			if (q) {
				*q = ';';
				p = q;
				p++;
			}
			if (!tmp)
				return 0;
			if (!q)
				break;
		}
		return ret;
	} else {
		int tmp;
		tmp = isdn_wildmat(cid, isdn_map_eaz2msn(emu->msn, di));
#ifdef ISDN_DEBUG_MODEM_ICALL
			printk(KERN_DEBUG "m_fi: mmsn=%s -> tmp=%d\n",
			       isdn_map_eaz2msn(emu->msn, di), tmp);
#endif
		return tmp;
	}
}

/*
 * An incoming call-request has arrived.
 * Search the tty-devices for an appropriate device and bind
 * it to the ISDN-Channel.
 * Return:
 *
 *  0 = No matching device found.
 *  1 = A matching device found.
 *  3 = No match found, but eventually would match, if
 *      CID is longer.
 */
int
isdn_tty_find_icall(int di, int ch, setup_parm setup)
{
	char *eaz;
	int i;
	int wret;
	int idx;
	int si1;
	int si2;
	char nr[32];
	ulong flags;

	if (!setup.phone[0]) {
		nr[0] = '0';
		nr[1] = '\0';
		printk(KERN_INFO "isdn_tty: Incoming call without OAD, assuming '0'\n");
	} else
		strcpy(nr, setup.phone);
	si1 = (int) setup.si1;
	si2 = (int) setup.si2;
	if (!setup.eazmsn[0]) {
		printk(KERN_WARNING "isdn_tty: Incoming call without CPN, assuming '0'\n");
		eaz = "0";
	} else
		eaz = setup.eazmsn;
#ifdef ISDN_DEBUG_MODEM_ICALL
	printk(KERN_DEBUG "m_fi: eaz=%s si1=%d si2=%d\n", eaz, si1, si2);
#endif
	wret = 0;
	save_flags(flags);
	cli();
	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		modem_info *info = &dev->mdm.info[i];

		if ((info->emu.mdmreg[REG_SI1] & si2bit[si1]) &&  /* SI1 is matching */
		    (info->emu.mdmreg[REG_SI2] == si2))	{         /* SI2 is matching */
			idx = isdn_dc2minor(di, ch);
#ifdef ISDN_DEBUG_MODEM_ICALL
			printk(KERN_DEBUG "m_fi: match1 wret=%d\n", wret);
			printk(KERN_DEBUG "m_fi: idx=%d flags=%08lx drv=%d ch=%d usg=%d\n", idx,
			       info->flags, info->isdn_driver, info->isdn_channel,
			       dev->usage[idx]);
#endif
			if (
#ifndef FIX_FILE_TRANSFER
				(info->flags & ISDN_ASYNC_NORMAL_ACTIVE) &&
#endif
				(info->isdn_driver == -1) &&
				(info->isdn_channel == -1) &&
				(USG_NONE(dev->usage[idx]))) {
				int matchret;

				if ((matchret = isdn_tty_match_icall(eaz, &info->emu, di)) > wret)
					wret = matchret;
				if (!matchret) {                  /* EAZ is matching */
					info->isdn_driver = di;
					info->isdn_channel = ch;
					info->drv_index = idx;
					dev->m_idx[idx] = info->line;
					dev->usage[idx] &= ISDN_USAGE_EXCLUSIVE;
					dev->usage[idx] |= isdn_calc_usage(si1, info->emu.mdmreg[REG_L2PROT]); 
					strcpy(dev->num[idx], nr);
					strcpy(info->emu.cpn, eaz);
					info->emu.mdmreg[REG_SI1I] = si2bit[si1];
					info->emu.mdmreg[REG_PLAN] = setup.plan;
					info->emu.mdmreg[REG_SCREEN] = setup.screen;
					isdn_info_update();
					restore_flags(flags);
					printk(KERN_INFO "isdn_tty: call from %s, -> RING on ttyI%d\n", nr,
					       info->line);
					info->msr |= UART_MSR_RI;
					isdn_tty_modem_result(2, info);
					isdn_timer_ctrl(ISDN_TIMER_MODEMRING, 1);
					return 1;
				}
			}
		}
	}
	restore_flags(flags);
	printk(KERN_INFO "isdn_tty: call from %s -> %s %s\n", nr, eaz,
	       ((dev->drv[di]->flags & DRV_FLAG_REJBUS) && (wret != 2))? "rejected" : "ignored");
	return (wret == 2)?3:0;
}

#define TTY_IS_ACTIVE(info) \
	(info->flags & (ISDN_ASYNC_NORMAL_ACTIVE | ISDN_ASYNC_CALLOUT_ACTIVE))

int
isdn_tty_stat_callback(int i, isdn_ctrl * c)
{
	int mi;
	modem_info *info;
	char *e;

	if (i < 0)
		return 0;
	if ((mi = dev->m_idx[i]) >= 0) {
		info = &dev->mdm.info[mi];
		switch (c->command) {
                        case ISDN_STAT_CINF:
                                printk(KERN_DEBUG "CHARGEINFO on ttyI%d: %ld %s\n", info->line, c->arg, c->parm.num);
                                info->emu.charge = (unsigned) simple_strtoul(c->parm.num, &e, 10);
                                if (e == (char *)c->parm.num)
					info->emu.charge = 0;
				
                                break;			
			case ISDN_STAT_BSENT:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_BSENT ttyI%d\n", info->line);
#endif
				if ((info->isdn_driver == c->driver) &&
				    (info->isdn_channel == c->arg)) {
					info->msr |= UART_MSR_CTS;
					if (info->send_outstanding)
						if (!(--info->send_outstanding))
							info->lsr |= UART_LSR_TEMT;
					isdn_tty_tint(info);
					return 1;
				}
				break;
			case ISDN_STAT_CAUSE:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_CAUSE ttyI%d\n", info->line);
#endif
				/* Signal cause to tty-device */
				strncpy(info->last_cause, c->parm.num, 5);
				return 1;
			case ISDN_STAT_DISPLAY:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_DISPLAY ttyI%d\n", info->line);
#endif
				/* Signal display to tty-device */
				if ((info->emu.mdmreg[REG_DISPLAY] & BIT_DISPLAY) && 
					!(info->emu.mdmreg[REG_RESPNUM] & BIT_RESPNUM)) {
				  isdn_tty_at_cout("\r\n", info);
				  isdn_tty_at_cout("DISPLAY: ", info);
				  isdn_tty_at_cout(c->parm.display, info);
				  isdn_tty_at_cout("\r\n", info);
				}
				return 1;
			case ISDN_STAT_DCONN:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_DCONN ttyI%d\n", info->line);
#endif
				if (TTY_IS_ACTIVE(info)) {
					if (info->dialing == 1) {
						info->dialing = 2;
						return 1;
					}
				}
				break;
			case ISDN_STAT_DHUP:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_DHUP ttyI%d\n", info->line);
#endif
				if (TTY_IS_ACTIVE(info)) {
					if (info->dialing == 1) 
						isdn_tty_modem_result(7, info);
					if (info->dialing > 1) 
						isdn_tty_modem_result(3, info);
					info->dialing = 0;
#ifdef ISDN_DEBUG_MODEM_HUP
					printk(KERN_DEBUG "Mhup in ISDN_STAT_DHUP\n");
#endif
					isdn_tty_modem_hup(info, 0);
					return 1;
				}
				break;
			case ISDN_STAT_BCONN:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_BCONN ttyI%d\n", info->line);
#endif
				/* Schedule CONNECT-Message to any tty
				 * waiting for it and
				 * set DCD-bit of its modem-status.
				 */
				if (TTY_IS_ACTIVE(info)) {
					info->msr |= UART_MSR_DCD;
					info->emu.charge = 0;
					if (info->dialing & 0xf)
						info->last_dir = 1;
					else
						info->last_dir = 0;
					info->dialing = 0;
					info->rcvsched = 1;
					if (USG_MODEM(dev->usage[i])) {
						if (info->emu.mdmreg[REG_L2PROT] == ISDN_PROTO_L2_MODEM) {
							strcpy(info->emu.connmsg, c->parm.num);
							isdn_tty_modem_result(1, info);
						}
						else
							isdn_tty_modem_result(5, info);
					}
					if (USG_VOICE(dev->usage[i]))
						isdn_tty_modem_result(11, info);
					return 1;
				}
				break;
			case ISDN_STAT_BHUP:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_BHUP ttyI%d\n", info->line);
#endif
				if (TTY_IS_ACTIVE(info)) {
#ifdef ISDN_DEBUG_MODEM_HUP
					printk(KERN_DEBUG "Mhup in ISDN_STAT_BHUP\n");
#endif
					isdn_tty_modem_hup(info, 0);
					return 1;
				}
				break;
			case ISDN_STAT_NODCH:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_NODCH ttyI%d\n", info->line);
#endif
				if (TTY_IS_ACTIVE(info)) {
					if (info->dialing) {
						info->dialing = 0;
						info->last_l2 = -1;
						info->last_si = 0;
						sprintf(info->last_cause, "0000");
						isdn_tty_modem_result(6, info);
					}
					isdn_tty_modem_hup(info, 0);
					return 1;
				}
				break;
			case ISDN_STAT_UNLOAD:
#ifdef ISDN_TTY_STAT_DEBUG
				printk(KERN_DEBUG "tty_STAT_UNLOAD ttyI%d\n", info->line);
#endif
				for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
					info = &dev->mdm.info[i];
					if (info->isdn_driver == c->driver) {
						if (info->online)
							isdn_tty_modem_hup(info, 1);
					}
				}
				return 1;
#ifdef CONFIG_ISDN_TTY_FAX
			case ISDN_STAT_FAXIND:
				if (TTY_IS_ACTIVE(info)) {
					isdn_tty_fax_command(info); 
				}
				break;
#endif
#ifdef CONFIG_ISDN_AUDIO
			case ISDN_STAT_AUDIO:
				if (TTY_IS_ACTIVE(info)) {
					switch(c->parm.num[0]) {
						case ISDN_AUDIO_DTMF:
							if (info->vonline) {
								isdn_audio_put_dle_code(info,
									c->parm.num[1]);
							}
							break;
					}
				}
				break;
#endif
		}
	}
	return 0;
}

/*********************************************************************
 Modem-Emulator-Routines
 *********************************************************************/

#define cmdchar(c) ((c>=' ')&&(c<=0x7f))

/*
 * Put a message from the AT-emulator into receive-buffer of tty,
 * convert CR, LF, and BS to values in modem-registers 3, 4 and 5.
 */
void
isdn_tty_at_cout(char *msg, modem_info * info)
{
	struct tty_struct *tty;
	atemu *m = &info->emu;
	char *p;
	char c;
	ulong flags;
	struct sk_buff *skb = 0;
	char *sp = 0;

	if (!msg) {
		printk(KERN_WARNING "isdn_tty: Null-Message in isdn_tty_at_cout\n");
		return;
	}
	save_flags(flags);
	cli();
	tty = info->tty;
	if ((info->flags & ISDN_ASYNC_CLOSING) || (!tty)) {
		restore_flags(flags);
		return;
	}

	/* use queue instead of direct flip, if online and */
	/* data is in queue or flip buffer is full */
	if ((info->online) && (((tty->flip.count + strlen(msg)) >= TTY_FLIPBUF_SIZE) ||
	    (!skb_queue_empty(&dev->drv[info->isdn_driver]->rpqueue[info->isdn_channel])))) {
		skb = alloc_skb(strlen(msg)
#ifdef CONFIG_ISDN_AUDIO
			+ sizeof(isdn_audio_skb)
#endif
			, GFP_ATOMIC);
		if (!skb) {
			restore_flags(flags);
			return;
		}
#ifdef CONFIG_ISDN_AUDIO
		skb_reserve(skb, sizeof(isdn_audio_skb));
#endif
		sp = skb_put(skb, strlen(msg));
#ifdef CONFIG_ISDN_AUDIO
		ISDN_AUDIO_SKB_DLECOUNT(skb) = 0;
		ISDN_AUDIO_SKB_LOCK(skb) = 0;
#endif
	}

	for (p = msg; *p; p++) {
		switch (*p) {
			case '\r':
				c = m->mdmreg[REG_CR];
				break;
			case '\n':
				c = m->mdmreg[REG_LF];
				break;
			case '\b':
				c = m->mdmreg[REG_BS];
				break;
			default:
				c = *p;
		}
		if (skb) {
			*sp++ = c;
		} else {
			if (tty->flip.count >= TTY_FLIPBUF_SIZE)
				break;
			tty_insert_flip_char(tty, c, 0);
		}
	}
	if (skb) {
		__skb_queue_tail(&dev->drv[info->isdn_driver]->rpqueue[info->isdn_channel], skb);
		dev->drv[info->isdn_driver]->rcvcount[info->isdn_channel] += skb->len;
		restore_flags(flags);
		/* Schedule dequeuing */
		if ((dev->modempoll) && (info->rcvsched))
			isdn_timer_ctrl(ISDN_TIMER_MODEMREAD, 1);

	} else {
		restore_flags(flags);
		queue_task(&tty->flip.tqueue, &tq_timer);
	}
}

/*
 * Perform ATH Hangup
 */
static void
isdn_tty_on_hook(modem_info * info)
{
	if (info->isdn_channel >= 0) {
#ifdef ISDN_DEBUG_MODEM_HUP
		printk(KERN_DEBUG "Mhup in isdn_tty_on_hook\n");
#endif
		isdn_tty_modem_hup(info, 1);
	}
}

static void
isdn_tty_off_hook(void)
{
	printk(KERN_DEBUG "isdn_tty_off_hook\n");
}

#define PLUSWAIT1 (HZ/2)        /* 0.5 sec. */
#define PLUSWAIT2 (HZ*3/2)      /* 1.5 sec */

/*
 * Check Buffer for Modem-escape-sequence, activate timer-callback to
 * isdn_tty_modem_escape() if sequence found.
 *
 * Parameters:
 *   p          pointer to databuffer
 *   plus       escape-character
 *   count      length of buffer
 *   pluscount  count of valid escape-characters so far
 *   lastplus   timestamp of last character
 */
static void
isdn_tty_check_esc(const u_char * p, u_char plus, int count, int *pluscount,
		   int *lastplus, int from_user)
{
	char cbuf[3];

	if (plus > 127)
		return;
	if (count > 3) {
		p += count - 3;
		count = 3;
		*pluscount = 0;
	}
	if (from_user) {
		copy_from_user(cbuf, p, count);
		p = cbuf;
	}
	while (count > 0) {
		if (*(p++) == plus) {
			if ((*pluscount)++) {
				/* Time since last '+' > 0.5 sec. ? */
				if ((jiffies - *lastplus) > PLUSWAIT1)
					*pluscount = 1;
			} else {
				/* Time since last non-'+' < 1.5 sec. ? */
				if ((jiffies - *lastplus) < PLUSWAIT2)
					*pluscount = 0;
			}
			if ((*pluscount == 3) && (count = 1))
				isdn_timer_ctrl(ISDN_TIMER_MODEMPLUS, 1);
			if (*pluscount > 3)
				*pluscount = 1;
		} else
			*pluscount = 0;
		*lastplus = jiffies;
		count--;
	}
}

/*
 * Return result of AT-emulator to tty-receive-buffer, depending on
 * modem-register 12, bit 0 and 1.
 * For CONNECT-messages also switch to online-mode.
 * For RING-message handle auto-ATA if register 0 != 0
 */
static void
isdn_tty_modem_result(int code, modem_info * info)
{
	atemu *m = &info->emu;
	static char *msg[] =
	{"OK", "CONNECT", "RING", "NO CARRIER", "ERROR",
	 "CONNECT 64000", "NO DIALTONE", "BUSY", "NO ANSWER",
	 "RINGING", "NO MSN/EAZ", "VCON", "RUNG"};
	ulong flags;
	char s[ISDN_MSNLEN+10];

	switch (code) {
		case 2:
			m->mdmreg[REG_RINGCNT]++;	/* RING */
			if (m->mdmreg[REG_RINGCNT] == m->mdmreg[REG_RINGATA])
				/* Automatically accept incoming call */
				isdn_tty_cmd_ATA(info);
			break;
		case 3:
			/* NO CARRIER */
#ifdef ISDN_DEBUG_MODEM_HUP
			printk(KERN_DEBUG "modem_result: NO CARRIER %d %d\n",
			       (info->flags & ISDN_ASYNC_CLOSING),
			       (!info->tty));
#endif
			save_flags(flags);
			cli();
			m->mdmreg[REG_RINGCNT] = 0;
			del_timer(&info->nc_timer);
			info->ncarrier = 0;
			if ((info->flags & ISDN_ASYNC_CLOSING) || (!info->tty)) {
				restore_flags(flags);
				return;
			}
			restore_flags(flags);
#ifdef CONFIG_ISDN_AUDIO
			if (info->vonline & 1) {
#ifdef ISDN_DEBUG_MODEM_VOICE
				printk(KERN_DEBUG "res3: send DLE-ETX on ttyI%d\n",
				       info->line);
#endif
				/* voice-recording, add DLE-ETX */
				isdn_tty_at_cout("\020\003", info);
			}
			if (info->vonline & 2) {
#ifdef ISDN_DEBUG_MODEM_VOICE
				printk(KERN_DEBUG "res3: send DLE-DC4 on ttyI%d\n",
				       info->line);
#endif
				/* voice-playing, add DLE-DC4 */
				isdn_tty_at_cout("\020\024", info);
			}
#endif
			break;
		case 1:
		case 5:
			sprintf(info->last_cause, "0000");
			if (!info->online)
				info->online = 2;
			break;
		case 11:
#ifdef ISDN_DEBUG_MODEM_VOICE
			printk(KERN_DEBUG "res3: send VCON on ttyI%d\n",
			       info->line);
#endif
			sprintf(info->last_cause, "0000");
			if (!info->online)
				info->online = 1;
			break;
	}
	if (m->mdmreg[REG_RESP] & BIT_RESP) {
		/* Show results */
		isdn_tty_at_cout("\r\n", info);
		if (m->mdmreg[REG_RESPNUM] & BIT_RESPNUM) {
			/* Show numeric results */
			sprintf(s, "%d", code);
			isdn_tty_at_cout(s, info);
		} else {
			if ((code == 2) &&
				(m->mdmreg[REG_RUNG] & BIT_RUNG) &&
				(m->mdmreg[REG_RINGCNT] > 1))
				return;
			if ((code == 2) && (!(m->mdmreg[REG_CIDONCE] & BIT_CIDONCE))) {
				isdn_tty_at_cout("CALLER NUMBER: ", info);
				isdn_tty_at_cout(dev->num[info->drv_index], info);
				isdn_tty_at_cout("\r\n", info);
			}
			isdn_tty_at_cout(msg[code], info);
			switch (code) {
				case 1:
					switch (m->mdmreg[REG_L2PROT]) {
						case ISDN_PROTO_L2_MODEM:
							isdn_tty_at_cout(" ", info);
							isdn_tty_at_cout(m->connmsg, info);
							break;
					}
					break;
				case 2:
					/* Append CPN, if enabled */
					if ((m->mdmreg[REG_CPN] & BIT_CPN)) {
						sprintf(s, "/%s", m->cpn);
						isdn_tty_at_cout(s, info);
					}
					/* Print CID only once, _after_ 1.st RING */
					if ((m->mdmreg[REG_CIDONCE] & BIT_CIDONCE) &&
					    (m->mdmreg[REG_RINGCNT] == 1)) {
						isdn_tty_at_cout("\r\n", info);
						isdn_tty_at_cout("CALLER NUMBER: ", info);
						isdn_tty_at_cout(dev->num[info->drv_index], info);
					}
					break;
				case 3:
				case 6:
				case 7:
				case 8:
					m->mdmreg[REG_RINGCNT] = 0;
					/* Append Cause-Message if enabled */
					if (m->mdmreg[REG_RESPXT] & BIT_RESPXT) {
						sprintf(s, "/%s", info->last_cause);
						isdn_tty_at_cout(s, info);
					}
					break;
				case 5:
					/* Append Protocol to CONNECT message */
					switch (m->mdmreg[REG_L2PROT]) {
						case ISDN_PROTO_L2_X75I:
						case ISDN_PROTO_L2_X75UI:
						case ISDN_PROTO_L2_X75BUI:
							isdn_tty_at_cout("/X.75", info);
							break;
						case ISDN_PROTO_L2_HDLC:
							isdn_tty_at_cout("/HDLC", info);
							break;
						case ISDN_PROTO_L2_V11096:
							isdn_tty_at_cout("/V110/9600", info);
							break;
						case ISDN_PROTO_L2_V11019:
							isdn_tty_at_cout("/V110/19200", info);
							break;
						case ISDN_PROTO_L2_V11038:
							isdn_tty_at_cout("/V110/38400", info);
							break;
					}
					if (m->mdmreg[REG_T70] & BIT_T70) {
						isdn_tty_at_cout("/T.70", info);
						if (m->mdmreg[REG_T70] & BIT_T70_EXT)
							isdn_tty_at_cout("+", info);
					}
					break;
			}
		}
		isdn_tty_at_cout("\r\n", info);
	}
	if (code == 3) {
		save_flags(flags);
		cli();
		if ((info->flags & ISDN_ASYNC_CLOSING) || (!info->tty)) {
			restore_flags(flags);
			return;
		}
		if (info->tty->ldisc.flush_buffer)
			info->tty->ldisc.flush_buffer(info->tty);
		if ((info->flags & ISDN_ASYNC_CHECK_CD) &&
		    (!((info->flags & ISDN_ASYNC_CALLOUT_ACTIVE) &&
		       (info->flags & ISDN_ASYNC_CALLOUT_NOHUP)))) {
			tty_hangup(info->tty);
		}
		restore_flags(flags);
	}
}

/*
 * Display a modem-register-value.
 */
static void
isdn_tty_show_profile(int ridx, modem_info * info)
{
	char v[6];

	sprintf(v, "\r\n%d", info->emu.mdmreg[ridx]);
	isdn_tty_at_cout(v, info);
}

/*
 * Get MSN-string from char-pointer, set pointer to end of number
 */
static void
isdn_tty_get_msnstr(char *n, char **p)
{
	int limit = ISDN_MSNLEN - 1;

	while (((*p[0] >= '0' && *p[0] <= '9') ||
	       /* Why a comma ??? */
	       (*p[0] == ',')) &&
		(limit--))
		*n++ = *p[0]++;
	*n = '\0';
}

/*
 * Get phone-number from modem-commandbuffer
 */
static void
isdn_tty_getdial(char *p, char *q,int cnt)
{
	int first = 1;
	int limit = ISDN_MSNLEN - 1;	/* MUST match the size of interface var to avoid
					buffer overflow */

	while (strchr(" 0123456789,#.*WPTS-", *p) && *p && --cnt>0) {
		if ((*p >= '0' && *p <= '9') || ((*p == 'S') && first) ||
		    (*p == '*') || (*p == '#')) {
			*q++ = *p;
			limit--;
		}
		if(!limit)
			break;
		p++;
		first = 0;
	}
	*q = 0;
}

#define PARSE_ERROR { isdn_tty_modem_result(4, info); return; }
#define PARSE_ERROR1 { isdn_tty_modem_result(4, info); return 1; }

static void
isdn_tty_report(modem_info * info)
{
	atemu *m = &info->emu;
	char s[80];

	isdn_tty_at_cout("\r\nStatistics of last connection:\r\n\r\n", info);
	sprintf(s, "    Remote Number:    %s\r\n", info->last_num);
	isdn_tty_at_cout(s, info);
	sprintf(s, "    Direction:        %s\r\n", info->last_dir ? "outgoing" : "incoming");
	isdn_tty_at_cout(s, info);
	isdn_tty_at_cout("    Layer-2 Protocol: ", info);
	switch (info->last_l2) {
		case ISDN_PROTO_L2_X75I:
			isdn_tty_at_cout("X.75i", info);
			break;
		case ISDN_PROTO_L2_X75UI:
			isdn_tty_at_cout("X.75ui", info);
			break;
		case ISDN_PROTO_L2_X75BUI:
			isdn_tty_at_cout("X.75bui", info);
			break;
		case ISDN_PROTO_L2_HDLC:
			isdn_tty_at_cout("HDLC", info);
			break;
		case ISDN_PROTO_L2_V11096:
			isdn_tty_at_cout("V.110 9600 Baud", info);
			break;
		case ISDN_PROTO_L2_V11019:
			isdn_tty_at_cout("V.110 19200 Baud", info);
			break;
		case ISDN_PROTO_L2_V11038:
			isdn_tty_at_cout("V.110 38400 Baud", info);
			break;
		case ISDN_PROTO_L2_TRANS:
			isdn_tty_at_cout("transparent", info);
			break;
		case ISDN_PROTO_L2_MODEM:
			isdn_tty_at_cout("modem", info);
			break;
		case ISDN_PROTO_L2_FAX:
			isdn_tty_at_cout("fax", info);
			break;
		default:
			isdn_tty_at_cout("unknown", info);
			break;
	}
	if (m->mdmreg[REG_T70] & BIT_T70) {
		isdn_tty_at_cout("/T.70", info);
		if (m->mdmreg[REG_T70] & BIT_T70_EXT)
			isdn_tty_at_cout("+", info);
	}
	isdn_tty_at_cout("\r\n", info);
	isdn_tty_at_cout("    Service:          ", info);
	switch (info->last_si) {
		case 1:
			isdn_tty_at_cout("audio\r\n", info);
			break;
		case 5:
			isdn_tty_at_cout("btx\r\n", info);
			break;
		case 7:
			isdn_tty_at_cout("data\r\n", info);
			break;
		default:
			sprintf(s, "%d\r\n", info->last_si);
			isdn_tty_at_cout(s, info);
			break;
	}
	sprintf(s, "    Hangup location:  %s\r\n", info->last_lhup ? "local" : "remote");
	isdn_tty_at_cout(s, info);
	sprintf(s, "    Last cause:       %s\r\n", info->last_cause);
	isdn_tty_at_cout(s, info);
}

/*
 * Parse AT&.. commands.
 */
static int
isdn_tty_cmd_ATand(char **p, modem_info * info)
{
	atemu *m = &info->emu;
	int i;
	char rb[100];

#define MAXRB (sizeof(rb) - 1)

	switch (*p[0]) {
		case 'B':
			/* &B - Set Buffersize */
			p[0]++;
			i = isdn_getnum(p);
			if ((i < 0) || (i > ISDN_SERIAL_XMIT_MAX))
				PARSE_ERROR1;
#ifdef CONFIG_ISDN_AUDIO
			if ((m->mdmreg[REG_SI1] & 1) && (i > VBUF))
				PARSE_ERROR1;
#endif
			m->mdmreg[REG_PSIZE] = i / 16;
			info->xmit_size = m->mdmreg[REG_PSIZE] * 16;
			switch (m->mdmreg[REG_L2PROT]) {
				case ISDN_PROTO_L2_V11096:
				case ISDN_PROTO_L2_V11019:
				case ISDN_PROTO_L2_V11038:
					info->xmit_size /= 10;		
			}
			break;
		case 'D':
			/* &D - Set DCD-Low-behavior */
			p[0]++;
			switch (isdn_getnum(p)) {
				case 0:
					m->mdmreg[REG_DTRHUP] &= ~BIT_DTRHUP;
					m->mdmreg[REG_DTRR] &= ~BIT_DTRR;
					break;
				case 2:
					m->mdmreg[REG_DTRHUP] |= BIT_DTRHUP;
					m->mdmreg[REG_DTRR] &= ~BIT_DTRR;
					break;
				case 3:
					m->mdmreg[REG_DTRHUP] |= BIT_DTRHUP;
					m->mdmreg[REG_DTRR] |= BIT_DTRR;
					break;
				default:
					PARSE_ERROR1
			}
			break;
		case 'E':
			/* &E -Set EAZ/MSN */
			p[0]++;
			isdn_tty_get_msnstr(m->msn, p);
			break;
		case 'F':
			/* &F -Set Factory-Defaults */
			p[0]++;
			if (info->msr & UART_MSR_DCD)
				PARSE_ERROR1;
			isdn_tty_reset_profile(m);
			isdn_tty_modem_reset_regs(info, 1);
			break;
		case 'L':
			/* &L -Set Numbers to listen on */
			p[0]++;
			i = 0;
			while ((strchr("0123456789,-*[]?;", *p[0])) &&
			       (i < ISDN_LMSNLEN) && *p[0])
				m->lmsn[i++] = *p[0]++;
			m->lmsn[i] = '\0';
			break;
		case 'R':
			/* &R - Set V.110 bitrate adaption */
			p[0]++;
			i = isdn_getnum(p);
			switch (i) {
				case 0:
					/* Switch off V.110, back to X.75 */
					m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_X75I;
					m->mdmreg[REG_SI2] = 0;
					info->xmit_size = m->mdmreg[REG_PSIZE] * 16;
					break;
				case 9600:
					m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_V11096;
					m->mdmreg[REG_SI2] = 197;
					info->xmit_size = m->mdmreg[REG_PSIZE] * 16 / 10;
					break;
				case 19200:
					m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_V11019;
					m->mdmreg[REG_SI2] = 199;
					info->xmit_size = m->mdmreg[REG_PSIZE] * 16 / 10;
					break;
				case 38400:
					m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_V11038;
					m->mdmreg[REG_SI2] = 198; /* no existing standard for this */
					info->xmit_size = m->mdmreg[REG_PSIZE] * 16 / 10;
					break;
				default:
					PARSE_ERROR1;
			}
			/* Switch off T.70 */
			m->mdmreg[REG_T70] &= ~(BIT_T70 | BIT_T70_EXT);
			/* Set Service 7 */
			m->mdmreg[REG_SI1] |= 4;
			break;
		case 'S':
			/* &S - Set Windowsize */
			p[0]++;
			i = isdn_getnum(p);
			if ((i > 0) && (i < 9))
				m->mdmreg[REG_WSIZE] = i;
			else
				PARSE_ERROR1;
			break;
		case 'V':
			/* &V - Show registers */
			p[0]++;
			isdn_tty_at_cout("\r\n", info);
			for (i = 0; i < ISDN_MODEM_ANZREG; i++) {
				sprintf(rb, "S%02d=%03d%s", i,
					m->mdmreg[i], ((i + 1) % 10) ? " " : "\r\n");
				isdn_tty_at_cout(rb, info);
			}
			sprintf(rb, "\r\nEAZ/MSN: %.50s\r\n",
				strlen(m->msn) ? m->msn : "None");
			isdn_tty_at_cout(rb, info);
			if (strlen(m->lmsn)) {
				isdn_tty_at_cout("\r\nListen: ", info);
				isdn_tty_at_cout(m->lmsn, info);
				isdn_tty_at_cout("\r\n", info);
			}
			break;
		case 'W':
			/* &W - Write Profile */
			p[0]++;
			switch (*p[0]) {
				case '0':
					p[0]++;
					modem_write_profile(m);
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		case 'X':
			/* &X - Switch to BTX-Mode and T.70 */
			p[0]++;
			switch (isdn_getnum(p)) {
				case 0:
					m->mdmreg[REG_T70] &= ~(BIT_T70 | BIT_T70_EXT);
					info->xmit_size = m->mdmreg[REG_PSIZE] * 16;
					break;
				case 1:
					m->mdmreg[REG_T70] |= BIT_T70;
					m->mdmreg[REG_T70] &= ~BIT_T70_EXT;
					m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_X75I;
					info->xmit_size = 112;
					m->mdmreg[REG_SI1] = 4;
					m->mdmreg[REG_SI2] = 0;
					break;
				case 2:
					m->mdmreg[REG_T70] |= (BIT_T70 | BIT_T70_EXT);
					m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_X75I;
					info->xmit_size = 112;
					m->mdmreg[REG_SI1] = 4;
					m->mdmreg[REG_SI2] = 0;
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		default:
			PARSE_ERROR1;
	}
	return 0;
}

static int
isdn_tty_check_ats(int mreg, int mval, modem_info * info, atemu * m)
{
	/* Some plausibility checks */
	switch (mreg) {
		case REG_L2PROT:
			if (mval > ISDN_PROTO_L2_MAX)
				return 1;
			break;
		case REG_PSIZE:
			if ((mval * 16) > ISDN_SERIAL_XMIT_MAX)
				return 1;
#ifdef CONFIG_ISDN_AUDIO
			if ((m->mdmreg[REG_SI1] & 1) && (mval > VBUFX))
				return 1;
#endif
			info->xmit_size = mval * 16;
			switch (m->mdmreg[REG_L2PROT]) {
				case ISDN_PROTO_L2_V11096:
				case ISDN_PROTO_L2_V11019:
				case ISDN_PROTO_L2_V11038:
					info->xmit_size /= 10;		
			}
			break;
		case REG_SI1I:
		case REG_PLAN:
		case REG_SCREEN:
			/* readonly registers */
			return 1;
	}
	return 0;
}

/*
 * Perform ATS command
 */
static int
isdn_tty_cmd_ATS(char **p, modem_info * info)
{
	atemu *m = &info->emu;
	int bitpos;
	int mreg;
	int mval;
	int bval;

	mreg = isdn_getnum(p);
	if (mreg < 0 || mreg >= ISDN_MODEM_ANZREG)
		PARSE_ERROR1;
	switch (*p[0]) {
		case '=':
			p[0]++;
			mval = isdn_getnum(p);
			if (mval < 0 || mval > 255)
				PARSE_ERROR1;
			if (isdn_tty_check_ats(mreg, mval, info, m))
				PARSE_ERROR1;
			m->mdmreg[mreg] = mval;
			break;
		case '.':
			/* Set/Clear a single bit */
			p[0]++;
			bitpos = isdn_getnum(p);
			if ((bitpos < 0) || (bitpos > 7))
				PARSE_ERROR1;
			switch (*p[0]) {
				case '=':
					p[0]++;
					bval = isdn_getnum(p);
					if (bval < 0 || bval > 1)
						PARSE_ERROR1;
					if (bval)
						mval = m->mdmreg[mreg] | (1 << bitpos);
					else
						mval = m->mdmreg[mreg] & ~(1 << bitpos);
					if (isdn_tty_check_ats(mreg, mval, info, m))
						PARSE_ERROR1;
					m->mdmreg[mreg] = mval;
					break;
				case '?':
					p[0]++;
					isdn_tty_at_cout("\r\n", info);
					isdn_tty_at_cout((m->mdmreg[mreg] & (1 << bitpos)) ? "1" : "0",
							 info);
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		case '?':
			p[0]++;
			isdn_tty_show_profile(mreg, info);
			break;
		default:
			PARSE_ERROR1;
			break;
	}
	return 0;
}

/*
 * Perform ATA command
 */
static void
isdn_tty_cmd_ATA(modem_info * info)
{
	atemu *m = &info->emu;
	isdn_ctrl cmd;
	int l2;

	if (info->msr & UART_MSR_RI) {
		/* Accept incoming call */
		info->last_dir = 0;
		strcpy(info->last_num, dev->num[info->drv_index]);
		m->mdmreg[REG_RINGCNT] = 0;
		info->msr &= ~UART_MSR_RI;
		l2 = m->mdmreg[REG_L2PROT];
#ifdef CONFIG_ISDN_AUDIO
		/* If more than one bit set in reg18, autoselect Layer2 */
		if ((m->mdmreg[REG_SI1] & m->mdmreg[REG_SI1I]) != m->mdmreg[REG_SI1]) {
			if (m->mdmreg[REG_SI1I] == 1) {
				if ((l2 != ISDN_PROTO_L2_MODEM) && (l2 != ISDN_PROTO_L2_FAX))
					l2 = ISDN_PROTO_L2_TRANS;
			} else
				l2 = ISDN_PROTO_L2_X75I;
		}
#endif
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL2;
		cmd.arg = info->isdn_channel + (l2 << 8);
		info->last_l2 = l2;
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.command = ISDN_CMD_SETL3;
		cmd.arg = info->isdn_channel + (m->mdmreg[REG_L3PROT] << 8);
#ifdef CONFIG_ISDN_TTY_FAX
		if (l2 == ISDN_PROTO_L2_FAX) {
			cmd.parm.fax = info->fax;
			info->fax->direction = ISDN_TTY_FAX_CONN_IN;
		}
#endif
		isdn_command(&cmd);
		cmd.driver = info->isdn_driver;
		cmd.arg = info->isdn_channel;
		cmd.command = ISDN_CMD_ACCEPTD;
		info->dialing = 16;
		info->emu.carrierwait = 0;
		isdn_command(&cmd);
		isdn_timer_ctrl(ISDN_TIMER_CARRIER, 1);
	} else
		isdn_tty_modem_result(8, info);
}

#ifdef CONFIG_ISDN_AUDIO
/*
 * Parse AT+F.. commands
 */
static int
isdn_tty_cmd_PLUSF(char **p, modem_info * info)
{
	atemu *m = &info->emu;
	char rs[20];

	if (!strncmp(p[0], "CLASS", 5)) {
		p[0] += 5;
		switch (*p[0]) {
			case '?':
				p[0]++;
				sprintf(rs, "\r\n%d",
					(m->mdmreg[REG_SI1] & 1) ? 8 : 0);
#ifdef CONFIG_ISDN_TTY_FAX
				if (m->mdmreg[REG_L2PROT] == ISDN_PROTO_L2_FAX)
				sprintf(rs, "\r\n2");
#endif
				isdn_tty_at_cout(rs, info);
				break;
			case '=':
				p[0]++;
				switch (*p[0]) {
					case '0':
						p[0]++;
						m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_X75I;
						m->mdmreg[REG_L3PROT] = ISDN_PROTO_L3_TRANS;
						m->mdmreg[REG_SI1] = 4;
						info->xmit_size =
						    m->mdmreg[REG_PSIZE] * 16;
						break;
#ifdef CONFIG_ISDN_TTY_FAX
					case '2':
						p[0]++;
						m->mdmreg[REG_SI1] = 1;
						m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_FAX;
						m->mdmreg[REG_L3PROT] = ISDN_PROTO_L3_FAX;
						info->xmit_size =
						    m->mdmreg[REG_PSIZE] * 16;
						break;
#endif
					case '8':
						p[0]++;
						/* L2 will change on dialout with si=1 */
						m->mdmreg[REG_L2PROT] = ISDN_PROTO_L2_X75I;
						m->mdmreg[REG_L3PROT] = ISDN_PROTO_L3_TRANS;
						m->mdmreg[REG_SI1] = 5;
						info->xmit_size = VBUF;
						break;
					case '?':
						p[0]++;
#ifdef CONFIG_ISDN_TTY_FAX
						isdn_tty_at_cout("\r\n0,2,8", info);
#else
						isdn_tty_at_cout("\r\n0,8", info);
#endif
						break;
					default:
						PARSE_ERROR1;
				}
				break;
			default:
				PARSE_ERROR1;
		}
		return 0;
	}
#ifdef CONFIG_ISDN_TTY_FAX
	return (isdn_tty_cmd_PLUSF_FAX(p, info));
#else
	PARSE_ERROR1;
#endif
}

/*
 * Parse AT+V.. commands
 */
static int
isdn_tty_cmd_PLUSV(char **p, modem_info * info)
{
	atemu *m = &info->emu;
	isdn_ctrl cmd;
	static char *vcmd[] =
	{"NH", "IP", "LS", "RX", "SD", "SM", "TX", "DD", NULL};
	int i;
	int par1;
	int par2;
	char rs[20];

	i = 0;
	while (vcmd[i]) {
		if (!strncmp(vcmd[i], p[0], 2)) {
			p[0] += 2;
			break;
		}
		i++;
	}
	switch (i) {
		case 0:
			/* AT+VNH - Auto hangup feature */
			switch (*p[0]) {
				case '?':
					p[0]++;
					isdn_tty_at_cout("\r\n1", info);
					break;
				case '=':
					p[0]++;
					switch (*p[0]) {
						case '1':
							p[0]++;
							break;
						case '?':
							p[0]++;
							isdn_tty_at_cout("\r\n1", info);
							break;
						default:
							PARSE_ERROR1;
					}
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		case 1:
			/* AT+VIP - Reset all voice parameters */
			isdn_tty_modem_reset_vpar(m);
			break;
		case 2:
			/* AT+VLS - Select device, accept incoming call */
			switch (*p[0]) {
				case '?':
					p[0]++;
					sprintf(rs, "\r\n%d", m->vpar[0]);
					isdn_tty_at_cout(rs, info);
					break;
				case '=':
					p[0]++;
					switch (*p[0]) {
						case '0':
							p[0]++;
							m->vpar[0] = 0;
							break;
						case '2':
							p[0]++;
							m->vpar[0] = 2;
							break;
						case '?':
							p[0]++;
							isdn_tty_at_cout("\r\n0,2", info);
							break;
						default:
							PARSE_ERROR1;
					}
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		case 3:
			/* AT+VRX - Start recording */
			if (!m->vpar[0])
				PARSE_ERROR1;
			if (info->online != 1) {
				isdn_tty_modem_result(8, info);
				return 1;
			}
			info->dtmf_state = isdn_audio_dtmf_init(info->dtmf_state);
			if (!info->dtmf_state) {
				printk(KERN_WARNING "isdn_tty: Couldn't malloc dtmf state\n");
				PARSE_ERROR1;
			}
			info->silence_state = isdn_audio_silence_init(info->silence_state);
			if (!info->silence_state) {
				printk(KERN_WARNING "isdn_tty: Couldn't malloc silence state\n");
				PARSE_ERROR1;
			}
			if (m->vpar[3] < 5) {
				info->adpcmr = isdn_audio_adpcm_init(info->adpcmr, m->vpar[3]);
				if (!info->adpcmr) {
					printk(KERN_WARNING "isdn_tty: Couldn't malloc adpcm state\n");
					PARSE_ERROR1;
				}
			}
#ifdef ISDN_DEBUG_AT
			printk(KERN_DEBUG "AT: +VRX\n");
#endif
			info->vonline |= 1;
			isdn_tty_modem_result(1, info);
			return 0;
			break;
		case 4:
			/* AT+VSD - Silence detection */
			switch (*p[0]) {
				case '?':
					p[0]++;
					sprintf(rs, "\r\n<%d>,<%d>",
						m->vpar[1],
						m->vpar[2]);
					isdn_tty_at_cout(rs, info);
					break;
				case '=':
					p[0]++;
					if ((*p[0]>='0') && (*p[0]<='9')) {
						par1 = isdn_getnum(p);
						if ((par1 < 0) || (par1 > 31))
							PARSE_ERROR1;
						if (*p[0] != ',')
							PARSE_ERROR1;
						p[0]++;
						par2 = isdn_getnum(p);
						if ((par2 < 0) || (par2 > 255))
							PARSE_ERROR1;
						m->vpar[1] = par1;
						m->vpar[2] = par2;
						break;
					} else 
					if (*p[0] == '?') {
						p[0]++;
						isdn_tty_at_cout("\r\n<0-31>,<0-255>",
							   info);
						break;
					} else
					PARSE_ERROR1;
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		case 5:
			/* AT+VSM - Select compression */
			switch (*p[0]) {
				case '?':
					p[0]++;
					sprintf(rs, "\r\n<%d>,<%d><8000>",
						m->vpar[3],
						m->vpar[1]);
					isdn_tty_at_cout(rs, info);
					break;
				case '=':
					p[0]++;
					switch (*p[0]) {
						case '2':
						case '3':
						case '4':
						case '5':
						case '6':
							par1 = isdn_getnum(p);
							if ((par1 < 2) || (par1 > 6))
								PARSE_ERROR1;
							m->vpar[3] = par1;
							break;
						case '?':
							p[0]++;
							isdn_tty_at_cout("\r\n2;ADPCM;2;0;(8000)\r\n",
								   info);
							isdn_tty_at_cout("3;ADPCM;3;0;(8000)\r\n",
								   info);
							isdn_tty_at_cout("4;ADPCM;4;0;(8000)\r\n",
								   info);
							isdn_tty_at_cout("5;ALAW;8;0;(8000)\r\n",
								   info);
							isdn_tty_at_cout("6;ULAW;8;0;(8000)\r\n",
								   info);
							break;
						default:
							PARSE_ERROR1;
					}
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		case 6:
			/* AT+VTX - Start sending */
			if (!m->vpar[0])
				PARSE_ERROR1;
			if (info->online != 1) {
				isdn_tty_modem_result(8, info);
				return 1;
			}
			info->dtmf_state = isdn_audio_dtmf_init(info->dtmf_state);
			if (!info->dtmf_state) {
				printk(KERN_WARNING "isdn_tty: Couldn't malloc dtmf state\n");
				PARSE_ERROR1;
			}
			if (m->vpar[3] < 5) {
				info->adpcms = isdn_audio_adpcm_init(info->adpcms, m->vpar[3]);
				if (!info->adpcms) {
					printk(KERN_WARNING "isdn_tty: Couldn't malloc adpcm state\n");
					PARSE_ERROR1;
				}
			}
#ifdef ISDN_DEBUG_AT
			printk(KERN_DEBUG "AT: +VTX\n");
#endif
			m->lastDLE = 0;
			info->vonline |= 2;
			isdn_tty_modem_result(1, info);
			return 0;
			break;
		case 7:
			/* AT+VDD - DTMF detection */
			switch (*p[0]) {
				case '?':
					p[0]++;
					sprintf(rs, "\r\n<%d>,<%d>",
						m->vpar[4],
						m->vpar[5]);
					isdn_tty_at_cout(rs, info);
					break;
				case '=':
					p[0]++;
					if ((*p[0]>='0') && (*p[0]<='9')) {
						if (info->online != 1)
							PARSE_ERROR1;
						par1 = isdn_getnum(p);
						if ((par1 < 0) || (par1 > 15))
							PARSE_ERROR1;
						if (*p[0] != ',')
							PARSE_ERROR1;
						p[0]++;
						par2 = isdn_getnum(p);
						if ((par2 < 0) || (par2 > 255))
							PARSE_ERROR1;
						m->vpar[4] = par1;
						m->vpar[5] = par2;
						cmd.driver = info->isdn_driver;
						cmd.command = ISDN_CMD_AUDIO;
						cmd.arg = info->isdn_channel + (ISDN_AUDIO_SETDD << 8);
						cmd.parm.num[0] = par1;
						cmd.parm.num[1] = par2;
						isdn_command(&cmd);
						break;
					} else
					if (*p[0] == '?') {
						p[0]++;
						isdn_tty_at_cout("\r\n<0-15>,<0-255>",
							info);
						break;
					} else
					PARSE_ERROR1;
					break;
				default:
					PARSE_ERROR1;
			}
			break;
		default:
			PARSE_ERROR1;
	}
	return 0;
}
#endif                          /* CONFIG_ISDN_AUDIO */

/*
 * Parse and perform an AT-command-line.
 */
static void
isdn_tty_parse_at(modem_info * info)
{
	atemu *m = &info->emu;
	char *p;
	char ds[40];

#ifdef ISDN_DEBUG_AT
	printk(KERN_DEBUG "AT: '%s'\n", m->mdmcmd);
#endif
	for (p = &m->mdmcmd[2]; *p;) {
		switch (*p) {
			case ' ':
				p++;
				break;
			case 'A':
				/* A - Accept incoming call */
				p++;
				isdn_tty_cmd_ATA(info);
				return;
				break;
			case 'D':
				/* D - Dial */
				if (info->msr & UART_MSR_DCD)
					PARSE_ERROR;
				if (info->msr & UART_MSR_RI) {
					isdn_tty_modem_result(3, info);
					return;
				}
				isdn_tty_getdial(++p, ds, sizeof ds);
				p += strlen(p);
				if (!strlen(m->msn))
					isdn_tty_modem_result(10, info);
				else if (strlen(ds))
					isdn_tty_dial(ds, info, m);
				else
					PARSE_ERROR;
				return;
			case 'E':
				/* E - Turn Echo on/off */
				p++;
				switch (isdn_getnum(&p)) {
					case 0:
						m->mdmreg[REG_ECHO] &= ~BIT_ECHO;
						break;
					case 1:
						m->mdmreg[REG_ECHO] |= BIT_ECHO;
						break;
					default:
						PARSE_ERROR;
				}
				break;
			case 'H':
				/* H - On/Off-hook */
				p++;
				switch (*p) {
					case '0':
						p++;
						isdn_tty_on_hook(info);
						break;
					case '1':
						p++;
						isdn_tty_off_hook();
						break;
					default:
						isdn_tty_on_hook(info);
						break;
				}
				break;
			case 'I':
				/* I - Information */
				p++;
				isdn_tty_at_cout("\r\nLinux ISDN", info);
				switch (*p) {
					case '0':
					case '1':
						p++;
						break;
					case '2':
						p++;
						isdn_tty_report(info);
						break;
					case '3':
                                                p++;
                                                sprintf(ds, "\r\n%d", info->emu.charge);
                                                isdn_tty_at_cout(ds, info);
                                                break;
					default:
				}
				break;
			case 'O':
				/* O - Go online */
				p++;
				if (info->msr & UART_MSR_DCD)
					/* if B-Channel is up */
					isdn_tty_modem_result((m->mdmreg[REG_L2PROT] == ISDN_PROTO_L2_MODEM) ? 1:5, info);
				else
					isdn_tty_modem_result(3, info);
				return;
			case 'Q':
				/* Q - Turn Emulator messages on/off */
				p++;
				switch (isdn_getnum(&p)) {
					case 0:
						m->mdmreg[REG_RESP] |= BIT_RESP;
						break;
					case 1:
						m->mdmreg[REG_RESP] &= ~BIT_RESP;
						break;
					default:
						PARSE_ERROR;
				}
				break;
			case 'S':
				/* S - Set/Get Register */
				p++;
				if (isdn_tty_cmd_ATS(&p, info))
					return;
				break;
			case 'V':
				/* V - Numeric or ASCII Emulator-messages */
				p++;
				switch (isdn_getnum(&p)) {
					case 0:
						m->mdmreg[REG_RESP] |= BIT_RESPNUM;
						break;
					case 1:
						m->mdmreg[REG_RESP] &= ~BIT_RESPNUM;
						break;
					default:
						PARSE_ERROR;
				}
				break;
			case 'Z':
				/* Z - Load Registers from Profile */
				p++;
				if (info->msr & UART_MSR_DCD) {
					info->online = 0;
					isdn_tty_on_hook(info);
				}
				isdn_tty_modem_reset_regs(info, 1);
				break;
			case '+':
				p++;
				switch (*p) {
#ifdef CONFIG_ISDN_AUDIO
					case 'F':
						p++;
						if (isdn_tty_cmd_PLUSF(&p, info))
							return;
						break;
					case 'V':
						if ((!(m->mdmreg[REG_SI1] & 1)) ||
							(m->mdmreg[REG_L2PROT] == ISDN_PROTO_L2_MODEM))
							PARSE_ERROR;
						p++;
						if (isdn_tty_cmd_PLUSV(&p, info))
							return;
						break;
#endif                          /* CONFIG_ISDN_AUDIO */
					case 'S':	/* SUSPEND */
						p++;
						isdn_tty_get_msnstr(ds, &p);
						isdn_tty_suspend(ds, info, m);
						break;
					case 'R':	/* RESUME */
						p++;
						isdn_tty_get_msnstr(ds, &p);
						isdn_tty_resume(ds, info, m);
						break;
					case 'M':	/* MESSAGE */
						p++;
						isdn_tty_send_msg(info, m, p);
						break;
					default:
						PARSE_ERROR;
				}
				break;
			case '&':
				p++;
				if (isdn_tty_cmd_ATand(&p, info))
					return;
				break;
			default:
				PARSE_ERROR;
		}
	}
#ifdef CONFIG_ISDN_AUDIO
	if (!info->vonline)
#endif
		isdn_tty_modem_result(0, info);
}

/* Need own toupper() because standard-toupper is not available
 * within modules.
 */
#define my_toupper(c) (((c>='a')&&(c<='z'))?(c&0xdf):c)

/*
 * Perform line-editing of AT-commands
 *
 * Parameters:
 *   p        inputbuffer
 *   count    length of buffer
 *   channel  index to line (minor-device)
 *   user     flag: buffer is in userspace
 */
static int
isdn_tty_edit_at(const char *p, int count, modem_info * info, int user)
{
	atemu *m = &info->emu;
	int total = 0;
	u_char c;
	char eb[2];
	int cnt;

	for (cnt = count; cnt > 0; p++, cnt--) {
		if (user)
			get_user(c, p);
		else
			c = *p;
		total++;
		if (c == m->mdmreg[REG_CR] || c == m->mdmreg[REG_LF]) {
			/* Separator (CR or LF) */
			m->mdmcmd[m->mdmcmdl] = 0;
			if (m->mdmreg[REG_ECHO] & BIT_ECHO) {
				eb[0] = c;
				eb[1] = 0;
				isdn_tty_at_cout(eb, info);
			}
			if ((m->mdmcmdl >= 2) && (!(strncmp(m->mdmcmd, "AT", 2))))
				isdn_tty_parse_at(info);
			m->mdmcmdl = 0;
			continue;
		}
		if (c == m->mdmreg[REG_BS] && m->mdmreg[REG_BS] < 128) {
			/* Backspace-Function */
			if ((m->mdmcmdl > 2) || (!m->mdmcmdl)) {
				if (m->mdmcmdl)
					m->mdmcmdl--;
				if (m->mdmreg[REG_ECHO] & BIT_ECHO)
					isdn_tty_at_cout("\b", info);
			}
			continue;
		}
		if (cmdchar(c)) {
			if (m->mdmreg[REG_ECHO] & BIT_ECHO) {
				eb[0] = c;
				eb[1] = 0;
				isdn_tty_at_cout(eb, info);
			}
			if (m->mdmcmdl < 255) {
				c = my_toupper(c);
				switch (m->mdmcmdl) {
					case 1:
						if (c == 'T') {
							m->mdmcmd[m->mdmcmdl] = c;
							m->mdmcmd[++m->mdmcmdl] = 0;
							break;
						} else
							m->mdmcmdl = 0;
						/* Fall through, check for 'A' */
					case 0:
						if (c == 'A') {
							m->mdmcmd[m->mdmcmdl] = c;
							m->mdmcmd[++m->mdmcmdl] = 0;
						}
						break;
					default:
						m->mdmcmd[m->mdmcmdl] = c;
						m->mdmcmd[++m->mdmcmdl] = 0;
				}
			}
		}
	}
	return total;
}

/*
 * Switch all modem-channels who are online and got a valid
 * escape-sequence 1.5 seconds ago, to command-mode.
 * This function is called every second via timer-interrupt from within
 * timer-dispatcher isdn_timer_function()
 */
void
isdn_tty_modem_escape(void)
{
	int ton = 0;
	int i;
	int midx;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++)
		if (USG_MODEM(dev->usage[i]))
			if ((midx = dev->m_idx[i]) >= 0) {
				modem_info *info = &dev->mdm.info[midx];
				if (info->online) {
					ton = 1;
					if ((info->emu.pluscount == 3) &&
					    ((jiffies - info->emu.lastplus) > PLUSWAIT2)) {
						info->emu.pluscount = 0;
						info->online = 0;
						isdn_tty_modem_result(0, info);
					}
				}
			}
	isdn_timer_ctrl(ISDN_TIMER_MODEMPLUS, ton);
}

/*
 * Put a RING-message to all modem-channels who have the RI-bit set.
 * This function is called every second via timer-interrupt from within
 * timer-dispatcher isdn_timer_function()
 */
void
isdn_tty_modem_ring(void)
{
	int ton = 0;
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		modem_info *info = &dev->mdm.info[i];
		if (info->msr & UART_MSR_RI) {
			ton = 1;
			isdn_tty_modem_result(2, info);
		}
	}
	isdn_timer_ctrl(ISDN_TIMER_MODEMRING, ton);
}

/*
 * For all online tty's, try sending data to
 * the lower levels.
 */
void
isdn_tty_modem_xmit(void)
{
	int ton = 1;
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		modem_info *info = &dev->mdm.info[i];
		if (info->online) {
			ton = 1;
			isdn_tty_senddown(info);
			isdn_tty_tint(info);
		}
	}
	isdn_timer_ctrl(ISDN_TIMER_MODEMXMIT, ton);
}

/*
 * Check all channels if we have a 'no carrier' timeout.
 * Timeout value is set by Register S7.
 */
void
isdn_tty_carrier_timeout(void)
{
	int ton = 0;
	int i;

	for (i = 0; i < ISDN_MAX_CHANNELS; i++) {
		modem_info *info = &dev->mdm.info[i];
		if (info->dialing) {
			if (info->emu.carrierwait++ > info->emu.mdmreg[REG_WAITC]) {
				info->dialing = 0;
				isdn_tty_modem_result(3, info);
				isdn_tty_modem_hup(info, 1);
			}
			else
				ton = 1;
		}
	}
	isdn_timer_ctrl(ISDN_TIMER_CARRIER, ton);
}
