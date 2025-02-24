/*****************************************************************************/

/*
 *      audio.c  --  USB Audio Class driver
 *
 *      Copyright (C) 1999
 *          Alan Cox (alan@lxorguk.ukuu.org.uk)
 *          Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *
 * 1999-09-07:  Alan Cox
 *              Parsing Audio descriptor patch
 * 1999-09-08:  Thomas Sailer
 *              Added OSS compatible data io functions; both parts of the
 *              driver remain to be glued together
 * 1999-09-10:  Thomas Sailer
 *              Beautified the driver. Added sample format conversions.
 *              Still not properly glued with the parsing code.
 *              The parsing code seems to have its problems btw,
 *              Since it parses all available configs but doesn't
 *              store which iface/altsetting belongs to which config.
 * 1999-09-20:  Thomas Sailer
 *              Threw out Alan's parsing code and implemented my own one.
 *              You cannot reasonnably linearly parse audio descriptors,
 *              especially the AudioClass descriptors have to be considered
 *              pointer lists. Mixer parsing untested, due to lack of device.
 *              First stab at synch pipe implementation, the Dallas USB DAC
 *              wants to use an Asynch out pipe. usb_audio_state now basically
 *              only contains lists of mixer and wave devices. We can therefore
 *              now have multiple mixer/wave devices per USB device.
 * 1999-10-31:  Thomas Sailer
 *              Audio can now be unloaded if it is not in use by any mixer
 *              or dsp client (formerly you had to disconnect the audio devices
 *              from the USB port)
 *              Finally, about three months after ordering, my "Maxxtro SPK222"
 *              speakers arrived, isn't disdata a great mail order company 8-)
 *              Parse class specific endpoint descriptor of the audiostreaming
 *              interfaces and take the endpoint attributes from there.
 *              Unbelievably, the Philips USB DAC has a sampling rate range
 *              of over a decade, yet does not support the sampling rate control!
 *              No wonder it sounds so bad, has very audible sampling rate
 *              conversion distortion. Don't try to listen to it using
 *              decent headphones!
 *              "Let's make things better" -> but please Philips start with your
 *              own stuff!!!!
 * 1999-11-02:  It takes the Philips boxes several seconds to acquire synchronisation
 *              that means they won't play short sounds. Should probably maintain
 *              the ISO datastream even if there's nothing to play.
 *              Fix counting the total_bytes counter, RealPlayer G2 depends on it.
 *
 *
 */

/*
 * Strategy:
 *
 * Alan Cox and Thomas Sailer are starting to dig at opposite ends and
 * are hoping to meet in the middle, just like tunnel diggers :)
 * Alan tackles the descriptor parsing, Thomas the actual data IO and the
 * OSS compatible interface.
 *
 * Data IO implementation issues
 *
 * A mmap'able ring buffer per direction is implemented, because
 * almost every OSS app expects it. It is however impractical to
 * transmit/receive USB data directly into and out of the ring buffer,
 * due to alignment and synchronisation issues. Instead, the ring buffer
 * feeds a constant time delay line that handles the USB issues.
 *
 * Now we first try to find an alternate setting that exactly matches
 * the sample format requested by the user. If we find one, we do not
 * need to perform any sample rate conversions. If there is no matching
 * altsetting, we choose the closest one and perform sample format
 * conversions. We never do sample rate conversion; these are too
 * expensive to be performed in the kernel.
 *
 * Current status:
 * - The IO code seems to work a couple of frames, but then gets
 *   UHCI into a "complaining" mode, i.e. uhci won't work again until
 *   removed and reloaded, it will not even notice disconnect/reconnect
 *   events.
 *   It seems to work more stably on OHCI-HCD.
 *
 * Generally: Due to the brokenness of the Audio Class spec
 * it seems generally impossible to write a generic Audio Class driver,
 * so a reasonable driver should implement the features that are actually
 * used.
 *
 * Parsing implementation issues
 *
 * One cannot reasonably parse the AudioClass descriptors linearly.
 * Therefore the current implementation features routines to look
 * for a specific descriptor in the descriptor list.
 *
 * How does the parsing work? First, all interfaces are searched
 * for an AudioControl class interface. If found, the config descriptor
 * that belongs to the current configuration is fetched from the device.
 * Then the HEADER descriptor is fetched. It contains a list of
 * all AudioStreaming and MIDIStreaming devices. This list is then walked,
 * and all AudioStreaming interfaces are classified into input and output
 * interfaces (according to the endpoint0 direction in altsetting1) (MIDIStreaming
 * is currently not supported). The input & output list is then used
 * to group inputs and outputs together and issued pairwise to the
 * AudioStreaming class parser. Finally, all OUTPUT_TERMINAL descriptors
 * are walked and issued to the mixer construction routine.
 *
 * The AudioStreaming parser simply enumerates all altsettings belonging
 * to the specified interface. It looks for AS_GENERAL and FORMAT_TYPE
 * class specific descriptors to extract the sample format/sample rate
 * data. Only sample format types PCM and PCM8 are supported right now, and
 * only FORMAT_TYPE_I is handled. The isochronous data endpoint needs to
 * be the first endpoint of the interface, and the optional synchronisation
 * isochronous endpoint the second one.
 *
 * Mixer construction works as follows: The various TERMINAL and UNIT
 * descriptors span a tree from the root (OUTPUT_TERMINAL) through the
 * intermediate nodes (UNITs) to the leaves (INPUT_TERMINAL). We walk
 * that tree in a depth first manner. FEATURE_UNITs may contribute volume,
 * bass and treble sliders to the mixer, MIXER_UNITs volume sliders.
 * The terminal type encoded in the INPUT_TERMINALs feeds a heuristic
 * to determine "meaningful" OSS slider numbers, however we will see
 * how well this works in practice. Other features are not used at the
 * moment, they seem less often used. Also, it seems difficult at least
 * to construct recording source switches from SELECTOR_UNITs, but
 * since there are not many USB ADC's available, we leave that for later.
 */

/*****************************************************************************/

#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/module.h>
#include <linux/sound.h>
#include <linux/soundcard.h>
#include <linux/list.h>
#include <linux/vmalloc.h>
#include <linux/wrapper.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/spinlock.h>

#include "usb.h"
#include "audio.h"

#define AUDIO_DEBUG 1

#define SND_DEV_DSP16   5 

/* --------------------------------------------------------------------- */

/*
 * Linked list of all audio devices...
 */
static struct list_head audiodevs = LIST_HEAD_INIT(audiodevs);
static DECLARE_MUTEX(open_sem);

/*
 * wait queue for processes wanting to open an USB audio device
 */
static DECLARE_WAIT_QUEUE_HEAD(open_wait);


#define MAXFORMATS        MAX_ALT
#define DMABUFSHIFT       17  /* 128k worth of DMA buffer */
#define NRSGBUF           (1U<<(DMABUFSHIFT-PAGE_SHIFT))

/*
 * This influences:
 * - Latency
 * - Interrupt rate
 * - Synchronisation behaviour
 * Don't touch this if you don't understand all of the above.
 */
#define DESCFRAMES  4

#define MIXFLG_STEREOIN   1
#define MIXFLG_STEREOOUT  2

struct mixerchannel {
	__u16 value;
	__u16 osschannel;  /* number of the OSS channel */
	__s16 minval, maxval;
	__u8 unitid;
	__u8 selector;
	__u8 chnum;
	__u8 flags;
};

struct audioformat {
	unsigned int format;
	unsigned int sratelo;
	unsigned int sratehi;
	unsigned char altsetting;
	unsigned char attributes;
};

struct dmabuf {
	/* buffer data format */
	unsigned int format;
	unsigned int srate;
	/* physical buffer */
	unsigned char *sgbuf[NRSGBUF];
	unsigned bufsize;
	unsigned numfrag;
	unsigned fragshift;
	unsigned wrptr, rdptr;
	unsigned total_bytes;
	int count;
	unsigned error; /* over/underrun */
	wait_queue_head_t wait;
	/* redundant, but makes calculations easier */
	unsigned fragsize;
	unsigned dmasize;
	/* OSS stuff */
	unsigned mapped:1;
	unsigned ready:1;
	unsigned ossfragshift;
	int ossmaxfrags;
	unsigned subdivision;
};

struct usb_audio_state;

#define FLG_NEXTID        1
#define FLG_ID0RUNNING    2
#define FLG_ID1RUNNING    4
#define FLG_SYNCNEXTID    8
#define FLG_SYNC0RUNNING 16
#define FLG_SYNC1RUNNING 32
#define FLG_RUNNING      64

struct usb_audiodev {
	struct list_head list;
	struct usb_audio_state *state;

        /* soundcore stuff */
        int dev_audio;

	/* wave stuff */
        mode_t open_mode;
	spinlock_t lock;         /* DMA buffer access spinlock */

	struct usbin {
		unsigned int interface;  /* Interface number */
		unsigned int format;     /* USB data format */
		unsigned int datapipe;   /* the data input pipe */
		unsigned int syncpipe;   /* the synchronisation pipe - 0 for anything but adaptive IN mode */
		unsigned int syncinterval;  /* P for adaptive IN mode, 0 otherwise */
		unsigned int freqn;      /* nominal sampling rate in USB format, i.e. fs/1000 in Q10.14 */
		unsigned int phase;      /* phase accumulator */
		unsigned int flags;      /* see FLG_ defines */

		struct usb_isoc_desc *dataiso[2];   /* ISO descriptors for the data endpoint */
		unsigned char *data[2];             /* data pages associated with the ISO descriptors */

		struct usb_isoc_desc *synciso[2];   /* ISO sync pipe descriptor if needed */
		unsigned char *syncdata[2];         /* data page for sync data */

		struct dmabuf dma;
	} usbin;

	struct usbout {
		unsigned int interface;  /* Interface number */
		unsigned int format;     /* USB data format */
		unsigned int datapipe;   /* the data input pipe */
		unsigned int syncpipe;   /* the synchronisation pipe - 0 for anything but asynchronous OUT mode */
		unsigned int syncinterval;  /* P for asynchronous OUT mode, 0 otherwise */
		unsigned int freqn;      /* nominal sampling rate in USB format, i.e. fs/1000 in Q10.14 */
		unsigned int freqm;      /* momentary sampling rate in USB format, i.e. fs/1000 in Q10.14 */
		unsigned int phase;      /* phase accumulator */
		unsigned int flags;      /* see FLG_ defines */

		struct usb_isoc_desc *dataiso[2];   /* ISO descriptors for the data endpoint */
		unsigned char *data[2];             /* data pages associated with the ISO descriptors */

		struct usb_isoc_desc *synciso[2];   /* ISO sync pipe descriptor if needed */
		unsigned char *syncdata[2];         /* data page for sync data */

		struct dmabuf dma;
	} usbout;


	unsigned int numfmtin, numfmtout;
	struct audioformat fmtin[MAXFORMATS];
	struct audioformat fmtout[MAXFORMATS];
};  

struct usb_mixerdev {
	struct list_head list;
	struct usb_audio_state *state;

        /* soundcore stuff */
        int dev_mixer;

	unsigned char iface;  /* interface number of the AudioControl interface */

	/* USB format descriptions */
        unsigned int numch, modcnt;

	/* mixch is last and gets allocated dynamically */
	struct mixerchannel ch[0];
};

struct usb_audio_state {
	struct list_head audiodev;

	/* USB device */
	struct usb_device *usbdev;

	struct list_head audiolist;
	struct list_head mixerlist;

	unsigned count;  /* usage counter; NOTE: the usb stack is also considered a user */
};

/* private audio format extensions */
#define AFMT_STEREO        0x80000000
#define AFMT_ISSTEREO(x)   ((x) & AFMT_STEREO)
#define AFMT_IS16BIT(x)    ((x) & (AFMT_S16_LE|AFMT_S16_BE|AFMT_U16_LE|AFMT_U16_BE))
#define AFMT_ISUNSIGNED(x) ((x) & (AFMT_U8|AFMT_U16_LE|AFMT_U16_BE))
#define AFMT_BYTESSHIFT(x) ((AFMT_ISSTEREO(x) ? 1 : 0) + (AFMT_IS16BIT(x) ? 1 : 0))
#define AFMT_BYTES(x)      (1<<AFMT_BYTESSHFIT(x))

/* --------------------------------------------------------------------- */

extern inline unsigned ld2(unsigned int x)
{
        unsigned r = 0;
        
        if (x >= 0x10000) {
                x >>= 16;
                r += 16;
        }
        if (x >= 0x100) {
                x >>= 8;
                r += 8;
        }
        if (x >= 0x10) {
                x >>= 4;
                r += 4;
        }
        if (x >= 4) {
                x >>= 2;
                r += 2;
        }
        if (x >= 2)
                r++;
        return r;
}

/* --------------------------------------------------------------------- */

/*
 * OSS compatible ring buffer management. The ring buffer may be mmap'ed into
 * an application address space.
 *
 * I first used the rvmalloc stuff copied from bttv. Alan Cox did not like it, so
 * we now use an array of pointers to a single page each. This saves us the
 * kernel page table manipulations, but we have to do a page table alike mechanism
 * (though only one indirection) in software.
 */

static void dmabuf_release(struct dmabuf *db)
{
	unsigned int nr;
	void *p;

	for(nr = 0; nr < NRSGBUF; nr++) {
		if (!(p = db->sgbuf[nr]))
			continue;
		mem_map_unreserve(MAP_NR(p));
		free_page((unsigned long)p);
		db->sgbuf[nr] = NULL;
	}
        db->mapped = db->ready = 0;
}

static int dmabuf_init(struct dmabuf *db)
{
        unsigned int nr, bytepersec, bufs;
	void *p;

	/* initialize some fields */
        db->rdptr = db->wrptr = db->total_bytes = db->count = db->error = 0;
	/* calculate required buffer size */
	bytepersec = db->srate << AFMT_BYTESSHIFT(db->format);
	bufs = 1U << DMABUFSHIFT;
        if (db->ossfragshift) {
                if ((1000 << db->ossfragshift) < bytepersec)
                        db->fragshift = ld2(bytepersec/1000);
                else
                        db->fragshift = db->ossfragshift;
        } else {
                db->fragshift = ld2(bytepersec/100/(db->subdivision ? db->subdivision : 1));
                if (db->fragshift < 3)
                        db->fragshift = 3;
        }
        db->numfrag = bufs >> db->fragshift;
        while (db->numfrag < 4 && db->fragshift > 3) {
                db->fragshift--;
                db->numfrag = bufs >> db->fragshift;
        }
        db->fragsize = 1 << db->fragshift;
        if (db->ossmaxfrags >= 4 && db->ossmaxfrags < db->numfrag)
                db->numfrag = db->ossmaxfrags;
        db->dmasize = db->numfrag << db->fragshift;
	for(nr = 0; nr < NRSGBUF; nr++) {
		if (!db->sgbuf[nr]) {
			p = (void *)get_free_page(GFP_KERNEL);
			if (!p)
				return -ENOMEM;
			db->sgbuf[nr] = p;
			mem_map_reserve(MAP_NR(p));
		}
		memset(db->sgbuf[nr], AFMT_ISUNSIGNED(db->format) ? 0x80 : 0, PAGE_SIZE);
		if ((nr << PAGE_SHIFT) >= db->dmasize)
			break;
	}
	db->bufsize = nr << PAGE_SHIFT;
        db->ready = 1;
	printk(KERN_DEBUG "dmabuf_init: bytepersec %d bufs %d ossfragshift %d ossmaxfrags %d "
	       "fragshift %d fragsize %d numfrag %d dmasize %d bufsize %d\n",
	       bytepersec, bufs, db->ossfragshift, db->ossmaxfrags, db->fragshift, db->fragsize,
	       db->numfrag, db->dmasize, db->bufsize);
        return 0;
}

static int dmabuf_mmap(struct dmabuf *db, unsigned long start, unsigned long size, pgprot_t prot)
{
	unsigned int nr;

	if (!db->ready || db->mapped || (start | size) & (PAGE_SIZE-1) || size > db->bufsize)
		return -EINVAL;
	size >>= PAGE_SHIFT;
	for(nr = 0; nr < size; nr++)
		if (!db->sgbuf[nr])
			return -EINVAL;
	db->mapped = 1;
	for(nr = 0; nr < size; nr++) {
		if (remap_page_range(start, virt_to_phys(db->sgbuf[nr]), PAGE_SIZE, prot))
			return -EAGAIN;
		start += PAGE_SIZE;
	}
	return 0;
}

static void dmabuf_copyin(struct dmabuf *db, const void *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	db->total_bytes += size;
	for (;;) {
		if (size <= 0)
			return;
		pgrem = ((~db->wrptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - db->wrptr;
		if (pgrem > rem)
			pgrem = rem;
		memcpy((db->sgbuf[db->wrptr >> PAGE_SHIFT]) + (db->wrptr & (PAGE_SIZE-1)), buffer, pgrem);
		size -= pgrem;
		(char *)buffer += pgrem;
		db->wrptr += pgrem;
		if (db->wrptr >= db->dmasize)
			db->wrptr = 0;
	}
}

static void dmabuf_copyout(struct dmabuf *db, void *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	db->total_bytes += size;
	for (;;) {
		if (size <= 0)
			return;
		pgrem = ((~db->rdptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - db->rdptr;
		if (pgrem > rem)
			pgrem = rem;
		memcpy(buffer, (db->sgbuf[db->rdptr >> PAGE_SHIFT]) + (db->rdptr & (PAGE_SIZE-1)), pgrem);
		size -= pgrem;
		(char *)buffer += pgrem;
		db->rdptr += pgrem;
		if (db->rdptr >= db->dmasize)
			db->rdptr = 0;
	}
}

static int dmabuf_copyin_user(struct dmabuf *db, unsigned int ptr, const void *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	if (!db->ready || db->mapped)
		return -EINVAL;
	for (;;) {
		if (size <= 0)
			return 0;
		pgrem = ((~ptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - ptr;
		if (pgrem > rem)
			pgrem = rem;
		copy_from_user_ret((db->sgbuf[ptr >> PAGE_SHIFT]) + (ptr & (PAGE_SIZE-1)), buffer, pgrem, -EFAULT);
		size -= pgrem;
		(char *)buffer += pgrem;
		ptr += pgrem;
		if (ptr >= db->dmasize)
			ptr = 0;
	}
}

static int dmabuf_copyout_user(struct dmabuf *db, unsigned int ptr, void *buffer, unsigned int size)
{
	unsigned int pgrem, rem;

	if (!db->ready || db->mapped)
		return -EINVAL;
	for (;;) {
		if (size <= 0)
			return 0;
		pgrem = ((~ptr) & (PAGE_SIZE-1)) + 1;
		if (pgrem > size)
			pgrem = size;
		rem = db->dmasize - ptr;
		if (pgrem > rem)
			pgrem = rem;
		copy_to_user_ret(buffer, (db->sgbuf[ptr >> PAGE_SHIFT]) + (ptr & (PAGE_SIZE-1)), pgrem, -EFAULT);
		size -= pgrem;
		(char *)buffer += pgrem;
		ptr += pgrem;
		if (ptr >= db->dmasize)
			ptr = 0;
	}
}

/* --------------------------------------------------------------------- */
/*
 * USB I/O code. We do sample format conversion if necessary
 */

static void usbin_stop(struct usb_audiodev *as)
{
	struct usbin *u = &as->usbin;
        unsigned long flags;
	unsigned int i;

        spin_lock_irqsave(&as->lock, flags);
	u->flags &= ~FLG_RUNNING;
	i = u->flags;
        spin_unlock_irqrestore(&as->lock, flags);
	while (i & (FLG_ID0RUNNING|FLG_ID1RUNNING|FLG_SYNC0RUNNING|FLG_SYNC1RUNNING)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current)) {
			if (i & FLG_ID0RUNNING)
				usb_kill_isoc(u->dataiso[0]);
			if (i & FLG_ID1RUNNING)
				usb_kill_isoc(u->dataiso[1]);
			if (i & FLG_SYNC0RUNNING)
				usb_kill_isoc(u->synciso[0]);
			if (i & FLG_SYNC1RUNNING)
				usb_kill_isoc(u->synciso[1]);
			break;
		}
		spin_lock_irqsave(&as->lock, flags);
		i = u->flags;
		spin_unlock_irqrestore(&as->lock, flags);
	}
	set_current_state(TASK_RUNNING);
	if (u->dataiso[0])
		usb_free_isoc(u->dataiso[0]);
	if (u->dataiso[1])
		usb_free_isoc(u->dataiso[1]);
	if (u->synciso[0])
		usb_free_isoc(u->synciso[0]);
	if (u->synciso[1])
		usb_free_isoc(u->synciso[1]);
	u->dataiso[0] = u->dataiso[1] = u->synciso[0] = u->synciso[1] = NULL;
}

static void usbin_release(struct usb_audiodev *as)
{
	struct usbin *u = &as->usbin;

	usbin_stop(as);
	if (u->data[0])
		free_page((unsigned long)u->data[0]);
	if (u->data[1])
		free_page((unsigned long)u->data[1]);
	if (u->syncdata[0])
		free_page((unsigned long)u->syncdata[0]);
	if (u->syncdata[1])
		free_page((unsigned long)u->syncdata[1]);
	u->data[0] = u->data[1] = u->syncdata[0] = u->syncdata[1] = NULL;
}

static void usbin_convert(struct usbin *u, unsigned char *buffer, unsigned int samples)
{
	union {
		__s16 s[64];
		unsigned char b[0];
	} tmp;
	unsigned int scnt, maxs, ufmtsh, dfmtsh, cnt, i;
	__s16 *sp, *sp2, s;
	unsigned char *bp;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	maxs = (AFMT_ISSTEREO(u->dma.format | u->format)) ? 32 : 64;
	while (samples > 0) {
		scnt = samples;
		if (scnt > maxs)
			scnt = maxs;
		cnt = scnt;
		if (AFMT_ISSTEREO(u->format))
			cnt <<= 1;
		sp = tmp.s + cnt;
		switch (u->format & ~AFMT_STEREO) {
		case AFMT_U8:
			for (bp = buffer+cnt, i = 0; i < cnt; i++) {
				bp--;
				sp--;
				*sp = (*bp ^ 0x80) << 8;
			}
			break;

		case AFMT_S8:
			for (bp = buffer+cnt, i = 0; i < cnt; i++) {
				bp--;
				sp--;
				*sp = *bp << 8;
			}
			break;

		case AFMT_U16_LE:
			for (bp = buffer+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = (bp[0] | (bp[1] << 8)) ^ 0x8000;
			}
			break;

		case AFMT_U16_BE:
			for (bp = buffer+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = (bp[1] | (bp[0] << 8)) ^ 0x8000;
			}
			break;

		case AFMT_S16_LE:
			for (bp = buffer+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = bp[0] | (bp[1] << 8);
			}
			break;

		case AFMT_S16_BE:
			for (bp = buffer+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = bp[1] | (bp[0] << 8);
			}
			break;
		}
		if (!AFMT_ISSTEREO(u->format) && AFMT_ISSTEREO(u->dma.format)) {
			/* expand from mono to stereo */
			for (sp = tmp.s+scnt, sp2 = tmp.s+2*scnt, i = 0; i < scnt; i++) {
				sp--;
				sp2 -= 2;
				sp2[0] = sp2[1] = sp[0];
			}
		}
		if (AFMT_ISSTEREO(u->format) && !AFMT_ISSTEREO(u->dma.format)) {
			/* contract from stereo to mono */
			for (sp = sp2 = tmp.s, i = 0; i < scnt; i++, sp++, sp2 += 2)
				sp[0] = (sp2[0] + sp2[1]) >> 1;
		}
		cnt = scnt;
		if (AFMT_ISSTEREO(u->dma.format))
			cnt <<= 1;
		sp = tmp.s;
		bp = tmp.b;
		switch (u->dma.format & ~AFMT_STEREO) {
		case AFMT_U8:
			for (i = 0; i < cnt; i++, sp++, bp++)
				*bp = (*sp >> 8) ^ 0x80;
			break;

		case AFMT_S8:
			for (i = 0; i < cnt; i++, sp++, bp++)
				*bp = *sp >> 8;
			break;

		case AFMT_U16_LE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[0] = s;
				bp[1] = (s >> 8) ^ 0x80;
			}
			break;

		case AFMT_U16_BE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[1] = s;
				bp[0] = (s >> 8) ^ 0x80;
			}
			break;

		case AFMT_S16_LE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[0] = s;
				bp[1] = s >> 8;
			}
			break;

		case AFMT_S16_BE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[1] = s;
				bp[0] = s >> 8;
			}
			break;
		}
		dmabuf_copyin(&u->dma, tmp.b, scnt << dfmtsh);
		buffer += scnt << ufmtsh;
		samples -= scnt;
	}
}		

static int usbin_prepare_desc(struct usbin *u, struct usb_isoc_desc *id)
{
	unsigned int i, maxsize;

	maxsize = (u->freqn + 0x3fff) >> (14 - AFMT_BYTESSHIFT(u->format));
	printk(KERN_DEBUG "usbin_prepare_desc: maxsize %d freq 0x%x format 0x%x\n", maxsize, u->freqn, u->format);
	for (i = 0; i < DESCFRAMES; i++)
		id->frames[i].frame_length = maxsize;
	return 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 * convert sample format on the fly if necessary
 */
static int usbin_retire_desc(struct usbin *u, struct usb_isoc_desc *id)
{
	unsigned int i, ufmtsh, dfmtsh, err = 0, cnt, scnt, dmafree, maxsize;
	unsigned char *cp = id->data;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	maxsize = (u->freqn + 0x3fff) >> (14 - ufmtsh);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	for (i = 0; i < DESCFRAMES; i++, cp += maxsize) {
		if (id->frames[i].frame_status) {
			printk(KERN_DEBUG "usbin_retire_desc: frame %u status %d\n", i, id->frames[i].frame_status);
			continue;
		}
		scnt = id->frames[i].frame_length >> ufmtsh;
		if (!scnt)
			continue;
		cnt = scnt << dfmtsh;
		if (!u->dma.mapped) {
			dmafree = u->dma.dmasize - u->dma.count;
			if (cnt > dmafree) {
				scnt = dmafree >> dfmtsh;
				cnt = scnt << dfmtsh;
				err++;
			}
		}
		u->dma.count += cnt;
		if (u->format == u->dma.format) {
			/* we do not need format conversion */
			dmabuf_copyin(&u->dma, cp, cnt);
		} else {
			/* we need sampling format conversion */
			usbin_convert(u, cp, scnt);
		}
	}
	if (err)
		u->dma.error++;
	if (u->dma.count >= (signed)u->dma.fragsize)
		wake_up(&u->dma.wait);
	return err ? -1 : 0;
}

static int usbin_completed(int status, void *__buffer, int rval, void *dev_id)
{
#if 1
        struct usb_isoc_desc *id = (struct usb_isoc_desc *)dev_id;
        struct usb_audiodev *as = (struct usb_audiodev *)id->context;
#else
        struct usb_audiodev *as = (struct usb_audiodev *)dev_id;
        struct usb_isoc_desc *id;
#endif
	struct usbin *u = &as->usbin;
	unsigned long flags;
	unsigned int next, idmask;

#if 0
	printk(KERN_DEBUG "usbin_completed: status %d rval %d flags 0x%x\n", status, rval, u->flags);
#endif
	spin_lock_irqsave(&as->lock, flags);
	next = !(u->flags & FLG_NEXTID);
	idmask = FLG_ID1RUNNING >> next;
	u->flags = (u->flags & ~(FLG_NEXTID | idmask)) | next;
	id = u->dataiso[!next];
	if (!usbin_retire_desc(u, id) &&
	    u->flags & FLG_RUNNING &&
	    !usbin_prepare_desc(u, id) && 
	    !usb_run_isoc(id, u->dataiso[next])) {
		u->flags |= idmask;
	} else {
		u->flags &= ~FLG_RUNNING;
		printk(KERN_DEBUG "usbin_completed: descriptor not restarted\n");
	}
	if (!(u->flags & idmask)) {
		printk(KERN_DEBUG "usbin_completed: killing id\n");
		usb_kill_isoc(id);
		printk(KERN_DEBUG "usbin_completed: id killed\n");
		wake_up(&u->dma.wait);
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

/*
 * we output sync data
 */
static int usbin_sync_prepare_desc(struct usbin *u, struct usb_isoc_desc *id)
{
	unsigned char *cp = id->data;
	unsigned int i;

	for (i = 0; i < DESCFRAMES; i++, cp += 3) {
		id->frames[i].frame_length = 3;
		cp[0] = u->freqn;
		cp[1] = u->freqn >> 8;
		cp[2] = u->freqn >> 16;
	}
	return 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 */
static int usbin_sync_retire_desc(struct usbin *u, struct usb_isoc_desc *id)
{
	unsigned int i;

	for (i = 0; i < DESCFRAMES; i++) {
		if (id->frames[i].frame_status) {
			printk(KERN_DEBUG "usbin_sync_retire_desc: frame %u status %d\n", i, id->frames[i].frame_status);
			continue;
		}
	}
	return 0;
}

static int usbin_sync_completed(int status, void *__buffer, int rval, void *dev_id)
{
#if 1
        struct usb_isoc_desc *id = (struct usb_isoc_desc *)dev_id;
        struct usb_audiodev *as = (struct usb_audiodev *)id->context;
#else
        struct usb_audiodev *as = (struct usb_audiodev *)dev_id;
        struct usb_isoc_desc *id;
#endif
	struct usbin *u = &as->usbin;
	unsigned long flags;
	unsigned int next, idmask;

#if 0
	printk(KERN_DEBUG "usbin_sync_completed: status %d rval %d flags 0x%x\n", status, rval, u->flags);
#endif
	spin_lock_irqsave(&as->lock, flags);
	next = !(u->flags & FLG_SYNCNEXTID);
	idmask = FLG_SYNC1RUNNING >> next;
	u->flags = (u->flags & ~(FLG_SYNCNEXTID | idmask)) | ((-next) & FLG_SYNCNEXTID);
	id = u->synciso[!next];
	if (!usbin_sync_retire_desc(u, id) &&
	    u->flags & FLG_RUNNING &&
	    !usbin_sync_prepare_desc(u, id) && 
	    !usb_run_isoc(id, u->synciso[next])) {
		u->flags |= idmask;
	} else {
		u->flags &= ~FLG_RUNNING;
		printk(KERN_DEBUG "usbin_sync_completed: descriptor not restarted\n");
	}
	if (!(u->flags & idmask)) {
		printk(KERN_DEBUG "usbin_sync_completed: killing id\n");
		usb_kill_isoc(id);
		printk(KERN_DEBUG "usbin_sync_completed: id killed\n");
		wake_up(&u->dma.wait);
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

static void usbin_start(struct usb_audiodev *as)
{
	struct usb_device *dev = as->state->usbdev;
	struct usbin *u = &as->usbin;
        struct usb_isoc_desc *id;
	unsigned long flags;
	unsigned int which, i;

#if 0
	printk(KERN_DEBUG "usbin_start: device %d ufmt 0x%08x dfmt 0x%08x srate %d\n",
	       dev->devnum, u->format, u->dma.format, u->dma.srate);
#endif
	/* allocate USB storage if not already done */
	/* UHCI wants the data to be page aligned - this is silly */
	if (!u->data[0])
		u->data[0] = (void *)get_free_page(GFP_KERNEL);
	if (!u->data[1])
		u->data[1] = (void *)get_free_page(GFP_KERNEL);
	if (!u->dataiso[0] && usb_init_isoc(dev, u->datapipe, DESCFRAMES, as, u->dataiso+0)) {
		printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
		       dev->devnum, u->datapipe);
		u->dataiso[0] = NULL;
	}
	if (!u->dataiso[1] && usb_init_isoc(dev, u->datapipe, DESCFRAMES, as, u->dataiso+1)) {
		printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
		       dev->devnum, u->datapipe);
		u->dataiso[1] = NULL;
	}
	if (u->syncpipe) {
		if (!u->syncdata[0])
			u->syncdata[0] = (void *)get_free_page(GFP_KERNEL);
		if (!u->syncdata[1])
			u->syncdata[1] = (void *)get_free_page(GFP_KERNEL);
		if (!u->synciso[0] && usb_init_isoc(dev, u->syncpipe, DESCFRAMES, as, u->synciso+0)) {
			printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
			       dev->devnum, u->syncpipe);
			u->synciso[0] = NULL;
		}
		if (!u->synciso[1] && usb_init_isoc(dev, u->syncpipe, DESCFRAMES, as, u->synciso+1)) {
			printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
			       dev->devnum, u->syncpipe);
			u->synciso[1] = NULL;
		}
	}
	if (!u->data[0] || !u->data[1] || !u->dataiso[0] || !u->dataiso[1] ||
	    (u->syncpipe && (!u->syncdata[0] || !u->syncdata[1] || !u->synciso[0] || !u->synciso[1]))) {
		printk(KERN_ERR "usbaudio: cannot start playback device %d\n", dev->devnum);
		return;
	}
	spin_lock_irqsave(&as->lock, flags);
	if (!(u->flags & FLG_RUNNING)) {
		u->freqn = ((u->dma.srate << 11) + 62) / 125; /* this will overflow at approx 2MSPS */
		u->phase = 0;
	}
       	u->flags |= FLG_RUNNING;
	if (!(u->flags & (FLG_ID0RUNNING|FLG_ID1RUNNING))) {
		id = u->dataiso[0];
		id->start_type = START_ASAP;
		id->start_frame = 0;
		id->callback_frames = /*0*/DESCFRAMES;
		id->callback_fn = usbin_completed;
		id->data = u->data[0];
		id->buf_size = PAGE_SIZE;
		u->flags &= ~FLG_NEXTID;
		if (!usbin_prepare_desc(u, id) && !usb_run_isoc(id, NULL))
			u->flags |= FLG_ID0RUNNING;
		else
			u->flags &= ~FLG_RUNNING;
	}
	i = u->flags & (FLG_ID0RUNNING|FLG_ID1RUNNING);
	if (u->flags & FLG_RUNNING && (i == FLG_ID0RUNNING || i == FLG_ID1RUNNING)) {
		which = !(u->flags & FLG_ID1RUNNING);
		id = u->dataiso[which];
		id->callback_frames = /*0*/DESCFRAMES;
		id->callback_fn = usbin_completed;
		id->data = u->data[which];
		id->buf_size = PAGE_SIZE;
		if (!usbin_prepare_desc(u, id) && !usb_run_isoc(id, u->dataiso[!which]))
			u->flags |= FLG_ID0RUNNING << which;
		else
			u->flags &= ~FLG_RUNNING;
	}
	if (u->syncpipe) {
		if (!(u->flags & (FLG_SYNC0RUNNING|FLG_SYNC1RUNNING))) {
			id = u->synciso[0];
			id->start_type = START_ASAP;
			id->start_frame = 0;
			id->callback_frames = /*0*/DESCFRAMES;
			id->callback_fn = usbin_sync_completed;
			id->data = u->syncdata[0];
			id->buf_size = PAGE_SIZE;
			u->flags &= ~FLG_SYNCNEXTID;
			if (!usbin_sync_prepare_desc(u, id) && !usb_run_isoc(id, NULL))
				u->flags |= FLG_SYNC0RUNNING;
			else
				u->flags &= ~FLG_RUNNING;
		}
		i = u->flags & (FLG_SYNC0RUNNING|FLG_SYNC1RUNNING);
		if (u->flags & FLG_RUNNING && (i == FLG_SYNC0RUNNING || i == FLG_SYNC1RUNNING)) {
			which = !(u->flags & FLG_SYNC1RUNNING);
			id = u->synciso[which];
			id->callback_frames = /*0*/DESCFRAMES;
			id->callback_fn = usbin_sync_completed;
			id->data = u->syncdata[which];
			id->buf_size = PAGE_SIZE;
			if (!usbin_sync_prepare_desc(u, id) && !usb_run_isoc(id, u->synciso[!which]))
				u->flags |= FLG_SYNC0RUNNING << which;
			else
				u->flags &= ~FLG_RUNNING;
		}
	}
        spin_unlock_irqrestore(&as->lock, flags);
}

static void usbout_stop(struct usb_audiodev *as)
{
	struct usbout *u = &as->usbout;
        unsigned long flags;
	unsigned int i;

printk(KERN_DEBUG "usb_audio: usbout_stop (1) flags 0x%04x\n", u->flags);
        spin_lock_irqsave(&as->lock, flags);
	u->flags &= ~FLG_RUNNING;
	i = u->flags;
        spin_unlock_irqrestore(&as->lock, flags);
printk(KERN_DEBUG "usb_audio: usbout_stop (2) flags 0x%04x\n", i);
	while (i & (FLG_ID0RUNNING|FLG_ID1RUNNING|FLG_SYNC0RUNNING|FLG_SYNC1RUNNING)) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(1);
		if (signal_pending(current)) {
			if (i & FLG_ID0RUNNING)
				usb_kill_isoc(u->dataiso[0]);
			if (i & FLG_ID1RUNNING)
				usb_kill_isoc(u->dataiso[1]);
			if (i & FLG_SYNC0RUNNING)
				usb_kill_isoc(u->synciso[0]);
			if (i & FLG_SYNC1RUNNING)
				usb_kill_isoc(u->synciso[1]);
			break;
		}
		spin_lock_irqsave(&as->lock, flags);
		i = u->flags;
		spin_unlock_irqrestore(&as->lock, flags);
printk(KERN_DEBUG "usb_audio: usbout_stop (3) flags 0x%04x\n", i);
	}
	set_current_state(TASK_RUNNING);
	if (u->dataiso[0])
		usb_free_isoc(u->dataiso[0]);
	if (u->dataiso[1])
		usb_free_isoc(u->dataiso[1]);
	if (u->synciso[0])
		usb_free_isoc(u->synciso[0]);
	if (u->synciso[1])
		usb_free_isoc(u->synciso[1]);
	u->dataiso[0] = u->dataiso[1] = u->synciso[0] = u->synciso[1] = NULL;
}

static void usbout_release(struct usb_audiodev *as)
{
	struct usbout *u = &as->usbout;

	usbout_stop(as);
	if (u->data[0])
		free_page((unsigned long)u->data[0]);
	if (u->data[1])
		free_page((unsigned long)u->data[1]);
	if (u->syncdata[0])
		free_page((unsigned long)u->syncdata[0]);
	if (u->syncdata[1])
		free_page((unsigned long)u->syncdata[1]);
	u->data[0] = u->data[1] = u->syncdata[0] = u->syncdata[1] = NULL;
}

static void usbout_convert(struct usbout *u, unsigned char *buffer, unsigned int samples)
{
	union {
		__s16 s[64];
		unsigned char b[0];
	} tmp;
	unsigned int scnt, maxs, ufmtsh, dfmtsh, cnt, i;
	__s16 *sp, *sp2, s;
	unsigned char *bp;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	maxs = (AFMT_ISSTEREO(u->dma.format | u->format)) ? 32 : 64;
	while (samples > 0) {
		scnt = samples;
		if (scnt > maxs)
			scnt = maxs;
		cnt = scnt;
		if (AFMT_ISSTEREO(u->dma.format))
			cnt <<= 1;
		dmabuf_copyout(&u->dma, tmp.b, scnt << dfmtsh);
		sp = tmp.s + cnt;
		switch (u->dma.format & ~AFMT_STEREO) {
		case AFMT_U8:
			for (bp = tmp.b+cnt, i = 0; i < cnt; i++) {
				bp--;
				sp--;
				*sp = (*bp ^ 0x80) << 8;
			}
			break;

		case AFMT_S8:
			for (bp = tmp.b+cnt, i = 0; i < cnt; i++) {
				bp--;
				sp--;
				*sp = *bp << 8;
			}
			break;

		case AFMT_U16_LE:
			for (bp = tmp.b+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = (bp[0] | (bp[1] << 8)) ^ 0x8000;
			}
			break;

		case AFMT_U16_BE:
			for (bp = tmp.b+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = (bp[1] | (bp[0] << 8)) ^ 0x8000;
			}
			break;

		case AFMT_S16_LE:
			for (bp = tmp.b+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = bp[0] | (bp[1] << 8);
			}
			break;

		case AFMT_S16_BE:
			for (bp = tmp.b+2*cnt, i = 0; i < cnt; i++) {
				bp -= 2;
				sp--;
				*sp = bp[1] | (bp[0] << 8);
			}
			break;
		}
		if (!AFMT_ISSTEREO(u->dma.format) && AFMT_ISSTEREO(u->format)) {
			/* expand from mono to stereo */
			for (sp = tmp.s+scnt, sp2 = tmp.s+2*scnt, i = 0; i < scnt; i++) {
				sp--;
				sp2 -= 2;
				sp2[0] = sp2[1] = sp[0];
			}
		}
		if (AFMT_ISSTEREO(u->dma.format) && !AFMT_ISSTEREO(u->format)) {
			/* contract from stereo to mono */
			for (sp = sp2 = tmp.s, i = 0; i < scnt; i++, sp++, sp2 += 2)
				sp[0] = (sp2[0] + sp2[1]) >> 1;
		}
		cnt = scnt;
		if (AFMT_ISSTEREO(u->format))
			cnt <<= 1;
		sp = tmp.s;
		bp = buffer;
		switch (u->format & ~AFMT_STEREO) {
		case AFMT_U8:
			for (i = 0; i < cnt; i++, sp++, bp++)
				*bp = (*sp >> 8) ^ 0x80;
			break;

		case AFMT_S8:
			for (i = 0; i < cnt; i++, sp++, bp++)
				*bp = *sp >> 8;
			break;

		case AFMT_U16_LE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[0] = s;
				bp[1] = (s >> 8) ^ 0x80;
			}
			break;

		case AFMT_U16_BE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[1] = s;
				bp[0] = (s >> 8) ^ 0x80;
			}
			break;

		case AFMT_S16_LE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[0] = s;
				bp[1] = s >> 8;
			}
			break;

		case AFMT_S16_BE:
			for (i = 0; i < cnt; i++, sp++, bp += 2) {
				s = *sp;
				bp[1] = s;
				bp[0] = s >> 8;
			}
			break;
		}
		buffer += scnt << ufmtsh;
		samples -= scnt;
	}
}		

static int usbout_prepare_desc(struct usbout *u, struct usb_isoc_desc *id)
{
	unsigned int i, ufmtsh, dfmtsh, err = 0, cnt, scnt;
	unsigned char *cp = id->data;

	ufmtsh = AFMT_BYTESSHIFT(u->format);
	dfmtsh = AFMT_BYTESSHIFT(u->dma.format);
	for (i = 0; i < DESCFRAMES; i++) {
		u->phase = (u->phase & 0x3fff) + u->freqm;
		scnt = u->phase >> 14;
		if (!scnt) {
			id->frames[i].frame_length = 0;
			continue;
		}
		cnt = scnt << dfmtsh;
		if (!u->dma.mapped) {
			if (cnt > u->dma.count) {
				scnt = u->dma.count >> dfmtsh;
				cnt = scnt << dfmtsh;
				err++;
			}
			u->dma.count -= cnt;
		} else
			u->dma.count += cnt;
		if (u->format == u->dma.format) {
			/* we do not need format conversion */
			dmabuf_copyout(&u->dma, cp, cnt);
		} else {
			/* we need sampling format conversion */
			usbout_convert(u, cp, scnt);
		}
		cnt = scnt << ufmtsh;
		id->frames[i].frame_length = cnt;
		cp += cnt;
	}
	if (err)
		u->dma.error++;
	if (u->dma.mapped) {
		if (u->dma.count >= (signed)u->dma.fragsize)
			wake_up(&u->dma.wait);
	} else {
		if ((signed)u->dma.dmasize >= u->dma.count + (signed)u->dma.fragsize)
			wake_up(&u->dma.wait);
	}
	return err ? -1 : 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 */
static int usbout_retire_desc(struct usbout *u, struct usb_isoc_desc *id)
{
	unsigned int i;

	for (i = 0; i < DESCFRAMES; i++) {
		if (id->frames[i].frame_status) {
			printk(KERN_DEBUG "usbout_retire_desc: frame %u status %d\n", i, id->frames[i].frame_status);
			continue;
		}
	}
	return 0;
}

static int usbout_completed(int status, void *__buffer, int rval, void *dev_id)
{
#if 1
        struct usb_isoc_desc *id = (struct usb_isoc_desc *)dev_id;
        struct usb_audiodev *as = (struct usb_audiodev *)id->context;
#else
        struct usb_audiodev *as = (struct usb_audiodev *)dev_id;
	struct usb_isoc_desc *id;
#endif
	struct usbout *u = &as->usbout;
	unsigned long flags;
	unsigned int next, idmask;

#if 0
	printk(KERN_DEBUG "usbout_completed: status %d rval %d flags 0x%x\n", status, rval, u->flags);
#endif
	spin_lock_irqsave(&as->lock, flags);
	next = !(u->flags & FLG_NEXTID);
	idmask = FLG_ID1RUNNING >> next;
	u->flags = (u->flags & ~(FLG_NEXTID | idmask)) | next;
	id = u->dataiso[!next];
	if (!usbout_retire_desc(u, id) &&
	    u->flags & FLG_RUNNING &&
	    !usbout_prepare_desc(u, id) && 
	    !usb_run_isoc(id, u->dataiso[next])) {
		u->flags |= idmask;
	} else {
		u->flags &= ~FLG_RUNNING;
		printk(KERN_DEBUG "usbout_completed: descriptor not restarted\n");
	}
	if (!(u->flags & idmask)) {
		printk(KERN_DEBUG "usbout_completed: killing id\n");
		usb_kill_isoc(id);
		printk(KERN_DEBUG "usbout_completed: id killed\n");
		wake_up(&u->dma.wait);
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

static int usbout_sync_prepare_desc(struct usbout *u, struct usb_isoc_desc *id)
{
	unsigned int i;

	for (i = 0; i < DESCFRAMES; i++)
		id->frames[i].frame_length = 3;
	return 0;
}

/*
 * return value: 0 if descriptor should be restarted, -1 otherwise
 */
static int usbout_sync_retire_desc(struct usbout *u, struct usb_isoc_desc *id)
{
	unsigned char *cp = id->data;
	unsigned int i, f;

	for (i = 0; i < DESCFRAMES; i++, cp += 3) {
		if (id->frames[i].frame_status) {
			printk(KERN_DEBUG "usbout_sync_retire_desc: frame %u status %d\n", i, id->frames[i].frame_status);
			continue;
		}
		if (id->frames[i].frame_length < 3) {
			printk(KERN_DEBUG "usbout_sync_retire_desc: frame %u length %d\n", i, id->frames[i].frame_length);
			continue;
		}
		f = cp[0] | (cp[1] << 8) | (cp[2] << 16);
		if (abs(f - u->freqn) > (u->freqn >> 3)) {
			printk(KERN_WARNING "usbout_sync_retire_desc: requested frequency %u (nominal %u) out of range!\n", f, u->freqn);
			continue;
		}
		u->freqm = f;
	}
	return 0;
}

static int usbout_sync_completed(int status, void *__buffer, int rval, void *dev_id)
{
#if 1
        struct usb_isoc_desc *id = (struct usb_isoc_desc *)dev_id;
        struct usb_audiodev *as = (struct usb_audiodev *)id->context;
#else
        struct usb_audiodev *as = (struct usb_audiodev *)dev_id;
	struct usb_isoc_desc *id;
#endif
	struct usbout *u = &as->usbout;
	unsigned long flags;
	unsigned int next, idmask;

#if 0
	printk(KERN_DEBUG "usbout_sync_completed: status %d rval %d flags 0x%x\n", status, rval, u->flags);
#endif
	spin_lock_irqsave(&as->lock, flags);
	next = !(u->flags & FLG_SYNCNEXTID);
	idmask = FLG_SYNC1RUNNING >> next;
	u->flags = (u->flags & ~(FLG_SYNCNEXTID | idmask)) | ((-next) & FLG_SYNCNEXTID);
	id = u->synciso[!next];
	if (!usbout_sync_retire_desc(u, id) &&
	    u->flags & FLG_RUNNING &&
	    !usbout_sync_prepare_desc(u, id) && 
	    !usb_run_isoc(id, u->synciso[next])) {
		u->flags |= idmask;
	} else {
		u->flags &= ~FLG_RUNNING;
		printk(KERN_DEBUG "usbout_sync_completed: descriptor not restarted\n");
	}
	if (!(u->flags & idmask)) {
		printk(KERN_DEBUG "usbout_sync_completed: killing id\n");
		usb_kill_isoc(id);
		printk(KERN_DEBUG "usbout_sync_completed: id killed\n");
		wake_up(&u->dma.wait);
	}
	spin_unlock_irqrestore(&as->lock, flags);
	return 0;
}

static void usbout_start(struct usb_audiodev *as)
{
	struct usb_device *dev = as->state->usbdev;
	struct usbout *u = &as->usbout;
        struct usb_isoc_desc *id;
	unsigned long flags;
	unsigned int which, i;

#if 0
	printk(KERN_DEBUG "usbout_start: device %d ufmt 0x%08x dfmt 0x%08x srate %d\n",
	       dev->devnum, u->format, u->dma.format, u->dma.srate);
#endif
	/* allocate USB storage if not already done */
	/* UHCI wants the data to be page aligned - this is silly */
	if (!u->data[0])
		u->data[0] = (void *)get_free_page(GFP_KERNEL);
	if (!u->data[1])
		u->data[1] = (void *)get_free_page(GFP_KERNEL);
	if (!u->dataiso[0] && usb_init_isoc(dev, u->datapipe, DESCFRAMES, as, u->dataiso+0)) {
		printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
		       dev->devnum, u->datapipe);
		u->dataiso[0] = NULL;
	}
	if (!u->dataiso[1] && usb_init_isoc(dev, u->datapipe, DESCFRAMES, as, u->dataiso+1)) {
		printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
		       dev->devnum, u->datapipe);
		u->dataiso[1] = NULL;
	}
	if (u->syncpipe) {
		if (!u->syncdata[0])
			u->syncdata[0] = (void *)get_free_page(GFP_KERNEL);
		if (!u->syncdata[1])
			u->syncdata[1] = (void *)get_free_page(GFP_KERNEL);
		if (!u->synciso[0] && usb_init_isoc(dev, u->syncpipe, DESCFRAMES, as, u->synciso+0)) {
			printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
			       dev->devnum, u->syncpipe);
			u->synciso[0] = NULL;
		}
		if (!u->synciso[1] && usb_init_isoc(dev, u->syncpipe, DESCFRAMES, as, u->synciso+1)) {
			printk(KERN_ERR "usbaudio: cannot init isoc descriptor device %d pipe 0x%08x\n", 
			       dev->devnum, u->syncpipe);
			u->synciso[1] = NULL;
		}
	}
	if (!u->data[0] || !u->data[1] || !u->dataiso[0] || !u->dataiso[1] ||
	    (u->syncpipe && (!u->syncdata[0] || !u->syncdata[1] || !u->synciso[0] || !u->synciso[1]))) {
		printk(KERN_ERR "usbaudio: cannot start playback device %d\n", dev->devnum);
		return;
	}
	spin_lock_irqsave(&as->lock, flags);
	if (!(u->flags & FLG_RUNNING)) {
		u->freqn = u->freqm = ((u->dma.srate << 11) + 62) / 125; /* this will overflow at approx 2MSPS */
		u->phase = 0;
	}
       	u->flags |= FLG_RUNNING;
	if (!(u->flags & (FLG_ID0RUNNING|FLG_ID1RUNNING))) {
		id = u->dataiso[0];
		id->start_type = START_ASAP;
		id->start_frame = 0;
		id->callback_frames = /*0*/DESCFRAMES;
		id->callback_fn = usbout_completed;
		id->data = u->data[0];
		id->buf_size = PAGE_SIZE;
		u->flags &= ~FLG_NEXTID;
		if (!usbout_prepare_desc(u, id) && !usb_run_isoc(id, NULL))
			u->flags |= FLG_ID0RUNNING;
		else
			u->flags &= ~FLG_RUNNING;
	}
	i = u->flags & (FLG_ID0RUNNING|FLG_ID1RUNNING);
	if (u->flags & FLG_RUNNING && (i == FLG_ID0RUNNING || i == FLG_ID1RUNNING)) {
		which = !(u->flags & FLG_ID1RUNNING);
		id = u->dataiso[which];
		id->callback_frames = /*0*/DESCFRAMES;
		id->callback_fn = usbout_completed;
		id->data = u->data[which];
		id->buf_size = PAGE_SIZE;
		if (!usbout_prepare_desc(u, id) && !usb_run_isoc(id, u->dataiso[!which]))
			u->flags |= FLG_ID0RUNNING << which;
		else
			u->flags &= ~FLG_RUNNING;
	}
	if (u->syncpipe) {
		if (!(u->flags & (FLG_SYNC0RUNNING|FLG_SYNC1RUNNING))) {
			id = u->synciso[0];
			id->start_type = START_ASAP;
			id->start_frame = 0;
			id->callback_frames = /*0*/DESCFRAMES;
			id->callback_fn = usbout_sync_completed;
			id->data = u->syncdata[0];
			id->buf_size = PAGE_SIZE;
			u->flags &= ~FLG_NEXTID;
			if (!usbout_sync_prepare_desc(u, id) && !usb_run_isoc(id, NULL))
				u->flags |= FLG_SYNC0RUNNING;
			else
				u->flags &= ~FLG_RUNNING;
		}
		i = u->flags & (FLG_SYNC0RUNNING|FLG_SYNC1RUNNING);
		if (u->flags & FLG_RUNNING && (i == FLG_SYNC0RUNNING || i == FLG_SYNC1RUNNING)) {
			which = !(u->flags & FLG_SYNC1RUNNING);
			id = u->synciso[which];
			id->callback_frames = /*0*/DESCFRAMES;
			id->callback_fn = usbout_sync_completed;
			id->data = u->syncdata[which];
			id->buf_size = PAGE_SIZE;
			if (!usbout_sync_prepare_desc(u, id) && !usb_run_isoc(id, u->synciso[!which]))
				u->flags |= FLG_SYNC0RUNNING << which;
			else
				u->flags &= ~FLG_RUNNING;
		}
	}
        spin_unlock_irqrestore(&as->lock, flags);
}

/* --------------------------------------------------------------------- */

static unsigned int find_format(struct audioformat *afp, unsigned int nr, unsigned int fmt)
{
	unsigned int i;

	/* first find an exact match */
	for (i = 0; i < nr; i++)
		if (afp[i].format == fmt)
			return i;
	/* second find a match with the same stereo/mono and 8bit/16bit property */
	for (i = 0; i < nr; i++)
		if (!AFMT_ISSTEREO(afp[i].format) == !AFMT_ISSTEREO(fmt) &&
		    !AFMT_IS16BIT(afp[i].format) == !AFMT_IS16BIT(fmt))
			return i;
	/* third find a match with the same number of channels */
	for (i = 0; i < nr; i++)
		if (!AFMT_ISSTEREO(afp[i].format) == !AFMT_ISSTEREO(fmt))
			return i;
	/* return anything */
	return 0;
}

static int set_format_in(struct usb_audiodev *as)
{
        struct usb_device *dev = as->state->usbdev;
        struct usb_config_descriptor *config = dev->actconfig;
        struct usb_interface_descriptor *alts;
        struct usb_interface *iface;	
	struct usbin *u = &as->usbin;
	struct dmabuf *d = &u->dma;
	struct audioformat *fmt;
	unsigned int fmtnr, ep;
	unsigned char data[3];
	int ret;

	if (u->interface < 0 || u->interface >= config->bNumInterfaces)
		return 0;
	iface = &config->interface[u->interface];
	fmtnr = find_format(as->fmtin, as->numfmtin, d->format);
	fmt = as->fmtin + fmtnr;
	alts = &iface->altsetting[fmt->altsetting];
	u->format = fmt->format;
	u->datapipe = usb_rcvisocpipe(dev, alts->endpoint[0].bEndpointAddress & 0xf);
	u->syncpipe = u->syncinterval = 0;
	if ((alts->endpoint[0].bmAttributes & 0x0c) == 0x08) {
		if (alts->bNumEndpoints < 2 ||
		    alts->endpoint[1].bmAttributes != 0x01 ||
		    alts->endpoint[1].bSynchAddress != 0 ||
		    alts->endpoint[1].bEndpointAddress != (alts->endpoint[0].bSynchAddress & 0x7f)) {
			printk(KERN_ERR "usbaudio: device %d interface %d altsetting %d invalid synch pipe\n",
			       dev->devnum, u->interface, fmt->altsetting);
			return -1;
		}
		u->syncpipe = usb_sndisocpipe(dev, alts->endpoint[1].bEndpointAddress & 0xf);
		u->syncinterval = alts->endpoint[1].bRefresh;
	}
	if (d->srate < fmt->sratelo)
		d->srate = fmt->sratelo;
	if (d->srate > fmt->sratehi)
		d->srate = fmt->sratehi;
	if (usb_set_interface(dev, u->interface, fmt->altsetting) < 0) {
		printk(KERN_WARNING "usbaudio: usb_set_interface failed, device %d interface %d altsetting %d\n",
		       dev->devnum, u->interface, fmt->altsetting);
		return -1;
	}
	if (fmt->sratelo == fmt->sratehi)
		return 0;
	ep = usb_pipeendpoint(u->datapipe) | (u->datapipe & USB_DIR_IN);
	/* if endpoint has pitch control, enable it */
	if (fmt->attributes & 0x02) {
		data[0] = 1;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   PITCH_CONTROL << 8, ep, data, 1, HZ)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set output pitch control device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
	}
	/* if endpoint has sampling rate control, set it */
	if (fmt->attributes & 0x01) {
		data[0] = d->srate;
		data[1] = d->srate >> 8;
		data[2] = d->srate >> 16;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, HZ)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set input sampling frequency device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
		if ((ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_IN,
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, HZ)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to get input sampling frequency device %d interface %u endpoint 0x%x\n",
			       ret, dev->devnum, u->interface, ep);
			return -1;
		}
		printk(KERN_DEBUG "usbaudio: set_format_in: device %d interface %d altsetting %d srate req: %u real %u\n",
		       dev->devnum, u->interface, fmt->altsetting, d->srate, data[0] | (data[1] << 8) | (data[2] << 16));
		d->srate = data[0] | (data[1] << 8) | (data[2] << 16);
	}
	return 0;
}

static int set_format_out(struct usb_audiodev *as)
{
        struct usb_device *dev = as->state->usbdev;
        struct usb_config_descriptor *config = dev->actconfig;
        struct usb_interface_descriptor *alts;
        struct usb_interface *iface;	
	struct usbout *u = &as->usbout;
	struct dmabuf *d = &u->dma;
	struct audioformat *fmt;
	unsigned int fmtnr, ep;
	unsigned char data[3];
	int ret;

	if (u->interface < 0 || u->interface >= config->bNumInterfaces)
		return 0;
	iface = &config->interface[u->interface];
	fmtnr = find_format(as->fmtout, as->numfmtout, d->format);
	fmt = as->fmtout + fmtnr;
	u->format = fmt->format;
	alts = &iface->altsetting[fmt->altsetting];
	u->datapipe = usb_sndisocpipe(dev, alts->endpoint[0].bEndpointAddress & 0xf);
	u->syncpipe = u->syncinterval = 0;
	if ((alts->endpoint[0].bmAttributes & 0x0c) == 0x04) {
		if (alts->bNumEndpoints < 2 ||
		    alts->endpoint[1].bmAttributes != 0x01 ||
		    alts->endpoint[1].bSynchAddress != 0 ||
		    alts->endpoint[1].bEndpointAddress != (alts->endpoint[0].bSynchAddress | 0x80)) {
			printk(KERN_ERR "usbaudio: device %d interface %d altsetting %d invalid synch pipe\n",
			       dev->devnum, u->interface, fmt->altsetting);
			return -1;
		}
		u->syncpipe = usb_rcvisocpipe(dev, alts->endpoint[1].bEndpointAddress & 0xf);
		u->syncinterval = alts->endpoint[1].bRefresh;
	}
	if (d->srate < fmt->sratelo)
		d->srate = fmt->sratelo;
	if (d->srate > fmt->sratehi)
		d->srate = fmt->sratehi;
	if (usb_set_interface(dev, u->interface, fmt->altsetting) < 0) {
		printk(KERN_WARNING "usbaudio: usb_set_interface failed, device %d interface %d altsetting %d\n",
		       dev->devnum, u->interface, fmt->altsetting);
		return -1;
	}
	if (fmt->sratelo == fmt->sratehi)
		return 0;
	ep = usb_pipeendpoint(u->datapipe) | (u->datapipe & USB_DIR_IN);
	/* if endpoint has pitch control, enable it */
	if (fmt->attributes & 0x02) {
		data[0] = 1;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   PITCH_CONTROL << 8, ep, data, 1, HZ)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set output pitch control device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
	}
	/* if endpoint has sampling rate control, set it */
	if (fmt->attributes & 0x01) {
		data[0] = d->srate;
		data[1] = d->srate >> 8;
		data[2] = d->srate >> 16;
		if ((ret = usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_OUT, 
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, HZ)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to set output sampling frequency device %d interface %u endpoint 0x%x to %u\n",
			       ret, dev->devnum, u->interface, ep, d->srate);
			return -1;
		}
		if ((ret = usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_TYPE_CLASS|USB_RECIP_ENDPOINT|USB_DIR_IN,
					   SAMPLING_FREQ_CONTROL << 8, ep, data, 3, HZ)) < 0) {
			printk(KERN_ERR "usbaudio: failure (error %d) to get output sampling frequency device %d interface %u endpoint 0x%x\n",
			       ret, dev->devnum, u->interface, ep);
			return -1;
		}
		printk(KERN_DEBUG "usbaudio: set_format_out: device %d interface %d altsetting %d srate req: %u real %u\n",
		       dev->devnum, u->interface, fmt->altsetting, d->srate, data[0] | (data[1] << 8) | (data[2] << 16));
		d->srate = data[0] | (data[1] << 8) | (data[2] << 16);
	}
	return 0;
}

static int set_format(struct usb_audiodev *s, unsigned int fmode, unsigned int fmt, unsigned int srate)
{
	int ret1 = 0, ret2 = 0;

	if (!(fmode & (FMODE_READ|FMODE_WRITE)))
		return -EINVAL;
	if (fmode & FMODE_READ) {
		usbin_stop(s);
		s->usbin.dma.ready = 0;
		if (fmt == AFMT_QUERY)
			fmt = s->usbin.dma.format;
		else
			s->usbin.dma.format = fmt;
		if (!srate)
			srate = s->usbin.dma.srate;
		else
			s->usbin.dma.srate = srate;
	}
	if (fmode & FMODE_WRITE) {
		usbout_stop(s);
		s->usbout.dma.ready = 0;
		if (fmt == AFMT_QUERY)
			fmt = s->usbout.dma.format;
		else
			s->usbout.dma.format = fmt;
		if (!srate)
			srate = s->usbout.dma.srate;
		else
			s->usbout.dma.srate = srate;
	}
	if (fmode & FMODE_READ)
		ret1 = set_format_in(s);
	if (fmode & FMODE_WRITE)
		ret2 = set_format_out(s);
	return ret1 ? ret1 : ret2;
}

/* --------------------------------------------------------------------- */

static int wrmixer(struct usb_mixerdev *ms, unsigned mixch, unsigned value)
{
	struct usb_device *dev = ms->state->usbdev;
	unsigned char data[2];
	struct mixerchannel *ch;
	int v1, v2, v3;

	if (mixch >= ms->numch)
		return -1;
	ch = &ms->ch[mixch];
	v3 = ch->maxval - ch->minval;
	v1 = value & 0xff;
	v2 = (value >> 8) & 0xff;
	if (v1 > 100)
		v1 = 100;
	if (v2 > 100)
		v2 = 100;
	if (!(ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
		v2 = v1;
	ch->value = v1 | (v2 << 8);
	v1 = (v1 * v3) / 100 + ch->minval;
	v2 = (v2 * v3) / 100 + ch->minval;
        switch (ch->selector) {
        case 0:  /* mixer unit request */
		data[0] = v1;
		data[1] = v1 >> 8;
                if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
                                    (ch->chnum << 8) | 1, ms->iface | (ch->unitid << 8), data, 2, HZ) < 0)
                        goto err;
		if (!(ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
			return 0;
		data[0] = v2;
		data[1] = v2 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    ((ch->chnum + !!(ch->flags & MIXFLG_STEREOIN)) << 8) | (1 + !!(ch->flags & MIXFLG_STEREOOUT)),
				    ms->iface | (ch->unitid << 8), data, 2, HZ) < 0)
                        goto err;
		return 0;

                /* various feature unit controls */
        case VOLUME_CONTROL:
		data[0] = v1;
		data[1] = v1 >> 8;
                if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
                                    (ch->selector << 8) | ch->chnum, ms->iface | (ch->unitid << 8), data, 2, HZ) < 0)
                        goto err;
		if (ch->chnum == 0)
			return 0;
		data[0] = v2;
		data[1] = v2 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->selector << 8) | (ch->chnum + 1), ms->iface | (ch->unitid << 8), data, 2, HZ) < 0)
			goto err;
		return 0;
                
        case BASS_CONTROL:
        case MID_CONTROL:
        case TREBLE_CONTROL:
		data[0] = v1 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
                                    (ch->selector << 8) | ch->chnum, ms->iface | (ch->unitid << 8), data, 1, HZ) < 0)
                        goto err;
		if (ch->chnum == 0)
			return 0;
		data[0] = v2 >> 8;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
				    (ch->selector << 8) | (ch->chnum + 1), ms->iface | (ch->unitid << 8), data, 1, HZ) < 0)
			goto err;
		return 0;
               
        default:
                return -1;
        }
        return 0;

 err:
        printk(KERN_ERR "usbaudio: mixer request device %u if %u unit %u ch %u selector %u failed\n", 
               dev->devnum, ms->iface, ch->unitid, ch->chnum, ch->selector);
	return -1;
}

/* --------------------------------------------------------------------- */

/*
 * should be called with open_sem hold, so that no new processes
 * look at the audio device to be destroyed
 */

static void release(struct usb_audio_state *s)
{
	struct usb_audiodev *as;
	struct usb_mixerdev *ms;

	s->count--;
	if (s->count) {
		up(&open_sem);
		return;
	}
	up(&open_sem);
        wake_up(&open_wait);
	while (!list_empty(&s->audiolist)) {
		as = list_entry(s->audiolist.next, struct usb_audiodev, list);
		list_del(&as->list);
		usbin_release(as);
		usbout_release(as);
		dmabuf_release(&as->usbin.dma);
		dmabuf_release(&as->usbout.dma);
		kfree(as);
	}
	while (!list_empty(&s->mixerlist)) {
		ms = list_entry(s->mixerlist.next, struct usb_mixerdev, list);
		list_del(&ms->list);
		kfree(ms);
	}
	kfree(s);
}

extern inline int prog_dmabuf_in(struct usb_audiodev *as)
{
        usbin_stop(as);
        return dmabuf_init(&as->usbin.dma);
}

extern inline int prog_dmabuf_out(struct usb_audiodev *as)
{
        usbout_stop(as);
        return dmabuf_init(&as->usbout.dma);
}

/* --------------------------------------------------------------------- */

static loff_t usb_audio_llseek(struct file *file, loff_t offset, int origin)
{
        return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int usb_audio_open_mixdev(struct inode *inode, struct file *file)
{
        int minor = MINOR(inode->i_rdev);
	struct list_head *devs, *mdevs;
	struct usb_mixerdev *ms;
        struct usb_audio_state *s;

        down(&open_sem);
	for (devs = audiodevs.next; devs != &audiodevs; devs = devs->next) {
		s = list_entry(devs, struct usb_audio_state, audiodev);
		for (mdevs = s->mixerlist.next; mdevs != &s->mixerlist; mdevs = mdevs->next) {
			ms = list_entry(mdevs, struct usb_mixerdev, list);
			if (ms->dev_mixer == minor)
				goto mixer_found;
		}
	}
	up(&open_sem);
	return -ENODEV;

 mixer_found:
	if (!s->usbdev) {
		up(&open_sem);
		return -EIO;
	}
        file->private_data = ms;
	s->count++;
	MOD_INC_USE_COUNT;
	up(&open_sem);
        return 0;
}

static int usb_audio_release_mixdev(struct inode *inode, struct file *file)
{
        struct usb_mixerdev *ms = (struct usb_mixerdev *)file->private_data;
	struct usb_audio_state *s = ms->state;

	down(&open_sem);
	release(s);
        MOD_DEC_USE_COUNT;
        return 0;
}

static int usb_audio_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct usb_mixerdev *ms = (struct usb_mixerdev *)file->private_data;
        int i, j, val;

        if (cmd == SOUND_MIXER_INFO) {
                mixer_info info;
                strncpy(info.id, "USB_AUDIO", sizeof(info.id));
                strncpy(info.name, "USB Audio Class Driver", sizeof(info.name));
                info.modify_counter = ms->modcnt;
                if (copy_to_user((void *)arg, &info, sizeof(info)))
                        return -EFAULT;
                return 0;
        }
        if (cmd == SOUND_OLD_MIXER_INFO) {
                _old_mixer_info info;
                strncpy(info.id, "USB_AUDIO", sizeof(info.id));
                strncpy(info.name, "USB Audio Class Driver", sizeof(info.name));
                if (copy_to_user((void *)arg, &info, sizeof(info)))
                        return -EFAULT;
                return 0;
        }
        if (cmd == OSS_GETVERSION)
                return put_user(SOUND_VERSION, (int *)arg);
        if (_IOC_TYPE(cmd) != 'M' || _IOC_SIZE(cmd) != sizeof(int))
                return -EINVAL;
        if (_IOC_DIR(cmd) == _IOC_READ) {
                switch (_IOC_NR(cmd)) {
                case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
			/* don't know how to handle this yet */
                        return put_user(0, (int *)arg);
                        
                case SOUND_MIXER_DEVMASK: /* Arg contains a bit for each supported device */
                        for (val = i = 0; i < ms->numch; i++)
				val |= 1 << ms->ch[i].osschannel;
                        return put_user(val, (int *)arg);

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			/* don't know how to handle this yet */
                        return put_user(0, (int *)arg);
                        
                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
                        for (val = i = 0; i < ms->numch; i++)
				if (ms->ch[i].flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT))
					val |= 1 << ms->ch[i].osschannel;
                        return put_user(val, (int *)arg);
                        
                case SOUND_MIXER_CAPS:
                        return put_user(0, (int *)arg);

                default:
                        i = _IOC_NR(cmd);
                        if (i >= SOUND_MIXER_NRDEVICES)
                                return -EINVAL;
			for (j = 0; j < ms->numch; j++) {
				if (ms->ch[j].osschannel == i) {
					return put_user(ms->ch[j].value, (int *)arg);
				}
			}
			return -EINVAL;
                }
        }
        if (_IOC_DIR(cmd) != (_IOC_READ|_IOC_WRITE)) 
                return -EINVAL;
        ms->modcnt++;
        switch (_IOC_NR(cmd)) {
        case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */
                get_user_ret(val, (int *)arg, -EFAULT);
                /* set recording source: val */
                return 0;

        default:
                i = _IOC_NR(cmd);
                if (i >= SOUND_MIXER_NRDEVICES)
                        return -EINVAL;
		for (j = 0; j < ms->numch && ms->ch[j].osschannel != i; j++);
		if (j >= ms->numch)
			return -EINVAL;
                get_user_ret(val, (int *)arg, -EFAULT);
		if (wrmixer(ms, j, val))
			return -EIO;
                return put_user(ms->ch[j].value, (int *)arg);
        }
}

static /*const*/ struct file_operations usb_mixer_fops = {
        &usb_audio_llseek,
        NULL,  /* read */
        NULL,  /* write */
        NULL,  /* readdir */
        NULL,  /* poll */
        &usb_audio_ioctl_mixdev,
        NULL,  /* mmap */
        &usb_audio_open_mixdev,
        NULL,   /* flush */
        &usb_audio_release_mixdev,
        NULL,  /* fsync */
        NULL,  /* fasync */
        NULL,  /* check_media_change */
        NULL,  /* revalidate */
        NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static int drain_out(struct usb_audiodev *as, int nonblock)
{
        DECLARE_WAITQUEUE(wait, current);
        unsigned long flags;
        int count, tmo;
        
        if (as->usbout.dma.mapped || !as->usbout.dma.ready)
                return 0;
        add_wait_queue(&as->usbout.dma.wait, &wait);
        for (;;) {
		__set_current_state(TASK_INTERRUPTIBLE);
		spin_lock_irqsave(&as->lock, flags);
                count = as->usbout.dma.count;
                spin_unlock_irqrestore(&as->lock, flags);
                if (count <= 0)
                        break;
                if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&as->usbout.dma.wait, &wait);
                        set_current_state(TASK_RUNNING);
                        return -EBUSY;
                }
                tmo = 3 * HZ * count / as->usbout.dma.srate;
		tmo >>= AFMT_BYTESSHIFT(as->usbout.dma.format);
                if (!schedule_timeout(tmo + 1)) {
                        printk(KERN_DEBUG "usbaudio: dma timed out??\n");
			break;
		}
        }
        remove_wait_queue(&as->usbout.dma.wait, &wait);
        set_current_state(TASK_RUNNING);
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */

static ssize_t usb_audio_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
        struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
        ssize_t ret = 0;
        unsigned long flags;
        unsigned int ptr;
        int cnt, err;

        if (ppos != &file->f_pos)
                return -ESPIPE;
        if (as->usbin.dma.mapped)
                return -ENXIO;
        if (!as->usbin.dma.ready && (ret = prog_dmabuf_in(as)))
                return ret;
        if (!access_ok(VERIFY_WRITE, buffer, count))
                return -EFAULT;
	add_wait_queue(&as->usbin.dma.wait, &wait);
	while (count > 0) {
		spin_lock_irqsave(&as->lock, flags);
		ptr = as->usbin.dma.rdptr;
		cnt = as->usbin.dma.count;
		/* set task state early to avoid wakeup races */
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
		spin_unlock_irqrestore(&as->lock, flags);
		if (cnt > count)
			cnt = count;
                if (cnt <= 0) {
                        usbin_start(as);
                        if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
                        if (signal_pending(current)) {
                                if (!ret)
                                        ret = -ERESTARTSYS;
                                break;
			}
			continue;
                }
		if ((err = dmabuf_copyout_user(&as->usbin.dma, ptr, buffer, cnt))) {
			if (!ret)
				ret = err;
			break;
		}
		ptr += cnt;
		if (ptr >= as->usbin.dma.dmasize)
			ptr -= as->usbin.dma.dmasize;
		spin_lock_irqsave(&as->lock, flags);
		as->usbin.dma.rdptr = ptr;
		as->usbin.dma.count -= cnt;
		spin_unlock_irqrestore(&as->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
	}
        __set_current_state(TASK_RUNNING);
        remove_wait_queue(&as->usbin.dma.wait, &wait);
	return ret;
}

static ssize_t usb_audio_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
        struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	DECLARE_WAITQUEUE(wait, current);
        ssize_t ret = 0;
        unsigned long flags;
        unsigned int ptr;
        int cnt, err;

        if (ppos != &file->f_pos)
                return -ESPIPE;
        if (as->usbout.dma.mapped)
                return -ENXIO;
        if (!as->usbout.dma.ready && (ret = prog_dmabuf_out(as)))
                return ret;
        if (!access_ok(VERIFY_READ, buffer, count))
                return -EFAULT;
	add_wait_queue(&as->usbout.dma.wait, &wait);
        while (count > 0) {
#if 0
		printk(KERN_DEBUG "usb_audio_write: count %u dma: count %u rdptr %u wrptr %u dmasize %u fragsize %u flags 0x%02x taskst 0x%x\n",
		       count, as->usbout.dma.count, as->usbout.dma.rdptr, as->usbout.dma.wrptr, as->usbout.dma.dmasize, as->usbout.dma.fragsize,
		       as->usbout.flags, current->state);
#endif
                spin_lock_irqsave(&as->lock, flags);
                if (as->usbout.dma.count < 0) {
                        as->usbout.dma.count = 0;
                        as->usbout.dma.rdptr = as->usbout.dma.wrptr;
                }
                ptr = as->usbout.dma.wrptr;
                cnt = as->usbout.dma.dmasize - as->usbout.dma.count;
		/* set task state early to avoid wakeup races */
		if (cnt <= 0)
			__set_current_state(TASK_INTERRUPTIBLE);
                spin_unlock_irqrestore(&as->lock, flags);
                if (cnt > count)
                        cnt = count;
                if (cnt <= 0) {
                        usbout_start(as);
                        if (file->f_flags & O_NONBLOCK) {
				if (!ret)
					ret = -EAGAIN;
				break;
			}
			schedule();
                        if (signal_pending(current)) {
                                if (!ret)
                                        ret = -ERESTARTSYS;
                                break;
			}
			continue;
		}
		if ((err = dmabuf_copyin_user(&as->usbout.dma, ptr, buffer, cnt))) {
			if (!ret)
				ret = err;
			break;
		}
		ptr += cnt;
		if (ptr >= as->usbout.dma.dmasize)
			ptr -= as->usbout.dma.dmasize;
                spin_lock_irqsave(&as->lock, flags);
                as->usbout.dma.wrptr = ptr;
                as->usbout.dma.count += cnt;
                spin_unlock_irqrestore(&as->lock, flags);
                count -= cnt;
                buffer += cnt;
                ret += cnt;
                usbout_start(as);
        }
        __set_current_state(TASK_RUNNING);
        remove_wait_queue(&as->usbout.dma.wait, &wait);
	return ret;
}

static unsigned int usb_audio_poll(struct file *file, struct poll_table_struct *wait)
{
        struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
        unsigned long flags;
        unsigned int mask = 0;

        if (file->f_mode & FMODE_WRITE) {
		if (!as->usbout.dma.ready)
			prog_dmabuf_out(as);
                poll_wait(file, &as->usbout.dma.wait, wait);
	}
        if (file->f_mode & FMODE_READ) {
		if (!as->usbin.dma.ready)
			prog_dmabuf_in(as);
                poll_wait(file, &as->usbin.dma.wait, wait);
	}
        spin_lock_irqsave(&as->lock, flags);
        if (file->f_mode & FMODE_READ) {
                if (as->usbin.dma.count >= (signed)as->usbin.dma.fragsize)
                        mask |= POLLIN | POLLRDNORM;
        }
        if (file->f_mode & FMODE_WRITE) {
                if (as->usbout.dma.mapped) {
                        if (as->usbout.dma.count >= (signed)as->usbout.dma.fragsize) 
                                mask |= POLLOUT | POLLWRNORM;
                } else {
                        if ((signed)as->usbout.dma.dmasize >= as->usbout.dma.count + (signed)as->usbout.dma.fragsize)
                                mask |= POLLOUT | POLLWRNORM;
                }
        }
        spin_unlock_irqrestore(&as->lock, flags);
        return mask;
}

static int usb_audio_mmap(struct file *file, struct vm_area_struct *vma)
{
        struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
        struct dmabuf *db;
        int ret;

        if (vma->vm_flags & VM_WRITE) {
                if ((ret = prog_dmabuf_out(as)) != 0)
                        return ret;
                db = &as->usbout.dma;
        } else if (vma->vm_flags & VM_READ) {
                if ((ret = prog_dmabuf_in(as)) != 0)
                        return ret;
                db = &as->usbin.dma;
        } else
                return -EINVAL;
        if (vma->vm_pgoff != 0)
                return -EINVAL;
	return dmabuf_mmap(db,  vma->vm_start, vma->vm_end - vma->vm_start, vma->vm_page_prot);
}

static int usb_audio_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
        struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	struct usb_audio_state *s = as->state;
        unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
        int val, val2, mapped, ret;

	if (!s->usbdev)
		return -EIO;
        mapped = ((file->f_mode & FMODE_WRITE) && as->usbout.dma.mapped) ||
                ((file->f_mode & FMODE_READ) && as->usbin.dma.mapped);
        switch (cmd) {
        case OSS_GETVERSION:
                return put_user(SOUND_VERSION, (int *)arg);

        case SNDCTL_DSP_SYNC:
                if (file->f_mode & FMODE_WRITE)
                        return drain_out(as, 0/*file->f_flags & O_NONBLOCK*/);
                return 0;

        case SNDCTL_DSP_SETDUPLEX:
                return 0;

        case SNDCTL_DSP_GETCAPS:
                return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER | 
				DSP_CAP_MMAP | DSP_CAP_BATCH, (int *)arg);

        case SNDCTL_DSP_RESET:
                if (file->f_mode & FMODE_WRITE) {
                        usbout_stop(as);
                        as->usbout.dma.rdptr = as->usbout.dma.wrptr = as->usbout.dma.count = as->usbout.dma.total_bytes = 0;
                }
                if (file->f_mode & FMODE_READ) {
                        usbin_stop(as);
                        as->usbin.dma.rdptr = as->usbin.dma.wrptr = as->usbin.dma.count = as->usbin.dma.total_bytes = 0;
                }
                return 0;

        case SNDCTL_DSP_SPEED:
                get_user_ret(val, (int *)arg, -EFAULT);
                if (val >= 0) {
                        if (val < 4000)
                                val = 4000;
                        if (val > 100000)
                                val = 100000;
			if (set_format(as, file->f_mode, AFMT_QUERY, val))
				return -EIO;
                }
                return put_user((file->f_mode & FMODE_READ) ? as->usbin.dma.srate : as->usbout.dma.srate, (int *)arg);

        case SNDCTL_DSP_STEREO:
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		if (set_format(as, file->f_mode, val2 | AFMT_STEREO, 0))
			return -EIO;
                return 0;

        case SNDCTL_DSP_CHANNELS:
                get_user_ret(val, (int *)arg, -EFAULT);
                if (val != 0) {
			val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
			if (val == 1)
				val2 &= ~AFMT_STEREO;
			else
				val2 |= AFMT_STEREO;
			if (set_format(as, file->f_mode, val2, 0))
				return -EIO;
		}
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
                return put_user(AFMT_ISSTEREO(val2) ? 2 : 1, (int *)arg);

        case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_U8 | AFMT_U16_LE | AFMT_U16_BE |
				AFMT_S8 | AFMT_S16_LE | AFMT_S16_BE, (int *)arg);

        case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			if (hweight32(val) != 1)
				return -EINVAL;
			if (!(val & (AFMT_U8 | AFMT_U16_LE | AFMT_U16_BE |
				     AFMT_S8 | AFMT_S16_LE | AFMT_S16_BE)))
				return -EINVAL;
			val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
			val |= val2 & AFMT_STEREO;
			if (set_format(as, file->f_mode, val, 0))
				return -EIO;
                }
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
                return put_user(val2 & ~AFMT_STEREO, (int *)arg);

        case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
                val = 0;
                if (file->f_mode & FMODE_READ && as->usbin.flags & FLG_RUNNING) 
                        val |= PCM_ENABLE_INPUT;
                if (file->f_mode & FMODE_WRITE && as->usbout.flags & FLG_RUNNING) 
                        val |= PCM_ENABLE_OUTPUT;
                return put_user(val, (int *)arg);

        case SNDCTL_DSP_SETTRIGGER:
                get_user_ret(val, (int *)arg, -EFAULT);
                if (file->f_mode & FMODE_READ) {
                        if (val & PCM_ENABLE_INPUT) {
                                if (!as->usbin.dma.ready && (ret = prog_dmabuf_in(as)))
                                        return ret;
                                usbin_start(as);
                        } else
                                usbin_stop(as);
                }
                if (file->f_mode & FMODE_WRITE) {
                        if (val & PCM_ENABLE_OUTPUT) {
                                if (!as->usbout.dma.ready && (ret = prog_dmabuf_out(as)))
                                        return ret;
                                usbout_start(as);
                        } else
                                usbout_stop(as);
                }
                return 0;

        case SNDCTL_DSP_GETOSPACE:
                if (!(file->f_mode & FMODE_WRITE))
                        return -EINVAL;
                if (!(as->usbout.flags & FLG_RUNNING) && (val = prog_dmabuf_out(as)) != 0)
                        return val;
                spin_lock_irqsave(&as->lock, flags);
                abinfo.fragsize = as->usbout.dma.fragsize;
                abinfo.bytes = as->usbout.dma.dmasize - as->usbout.dma.count;
                abinfo.fragstotal = as->usbout.dma.numfrag;
                abinfo.fragments = abinfo.bytes >> as->usbout.dma.fragshift;      
                spin_unlock_irqrestore(&as->lock, flags);
                return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

        case SNDCTL_DSP_GETISPACE:
                if (!(file->f_mode & FMODE_READ))
                        return -EINVAL;
                if (!(as->usbin.flags & FLG_RUNNING) && (val = prog_dmabuf_in(as)) != 0)
                        return val;
                spin_lock_irqsave(&as->lock, flags);
                abinfo.fragsize = as->usbin.dma.fragsize;
                abinfo.bytes = as->usbin.dma.count;
                abinfo.fragstotal = as->usbin.dma.numfrag;
                abinfo.fragments = abinfo.bytes >> as->usbin.dma.fragshift;      
                spin_unlock_irqrestore(&as->lock, flags);
                return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
                
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
                if (!(file->f_mode & FMODE_WRITE))
                        return -EINVAL;
                spin_lock_irqsave(&as->lock, flags);
                val = as->usbout.dma.count;
                spin_unlock_irqrestore(&as->lock, flags);
                return put_user(val, (int *)arg);

        case SNDCTL_DSP_GETIPTR:
                if (!(file->f_mode & FMODE_READ))
                        return -EINVAL;
                spin_lock_irqsave(&as->lock, flags);
                cinfo.bytes = as->usbin.dma.total_bytes;
                cinfo.blocks = as->usbin.dma.count >> as->usbin.dma.fragshift;
                cinfo.ptr = as->usbin.dma.wrptr;
                if (as->usbin.dma.mapped)
                        as->usbin.dma.count &= as->usbin.dma.fragsize-1;
                spin_unlock_irqrestore(&as->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETOPTR:
                if (!(file->f_mode & FMODE_WRITE))
                        return -EINVAL;
                spin_lock_irqsave(&as->lock, flags);
                cinfo.bytes = as->usbout.dma.total_bytes;
                cinfo.blocks = as->usbout.dma.count >> as->usbout.dma.fragshift;
                cinfo.ptr = as->usbout.dma.rdptr;
                if (as->usbout.dma.mapped)
                        as->usbout.dma.count &= as->usbout.dma.fragsize-1;
                spin_unlock_irqrestore(&as->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

       case SNDCTL_DSP_GETBLKSIZE:
                if (file->f_mode & FMODE_WRITE) {
                        if ((val = prog_dmabuf_out(as)))
                                return val;
                        return put_user(as->usbout.dma.fragsize, (int *)arg);
                }
                if ((val = prog_dmabuf_in(as)))
                        return val;
                return put_user(as->usbin.dma.fragsize, (int *)arg);

        case SNDCTL_DSP_SETFRAGMENT:
                get_user_ret(val, (int *)arg, -EFAULT);
                if (file->f_mode & FMODE_READ) {
                        as->usbin.dma.ossfragshift = val & 0xffff;
                        as->usbin.dma.ossmaxfrags = (val >> 16) & 0xffff;
                        if (as->usbin.dma.ossfragshift < 4)
                                as->usbin.dma.ossfragshift = 4;
                        if (as->usbin.dma.ossfragshift > 15)
                                as->usbin.dma.ossfragshift = 15;
                        if (as->usbin.dma.ossmaxfrags < 4)
                                as->usbin.dma.ossmaxfrags = 4;
                }
                if (file->f_mode & FMODE_WRITE) {
                        as->usbout.dma.ossfragshift = val & 0xffff;
                        as->usbout.dma.ossmaxfrags = (val >> 16) & 0xffff;
                        if (as->usbout.dma.ossfragshift < 4)
                                as->usbout.dma.ossfragshift = 4;
                        if (as->usbout.dma.ossfragshift > 15)
                                as->usbout.dma.ossfragshift = 15;
                        if (as->usbout.dma.ossmaxfrags < 4)
                                as->usbout.dma.ossmaxfrags = 4;
                }
                return 0;

        case SNDCTL_DSP_SUBDIVIDE:
                if ((file->f_mode & FMODE_READ && as->usbin.dma.subdivision) ||
                    (file->f_mode & FMODE_WRITE && as->usbout.dma.subdivision))
                        return -EINVAL;
                get_user_ret(val, (int *)arg, -EFAULT);
                if (val != 1 && val != 2 && val != 4)
                        return -EINVAL;
                if (file->f_mode & FMODE_READ)
                        as->usbin.dma.subdivision = val;
                if (file->f_mode & FMODE_WRITE)
                        as->usbout.dma.subdivision = val;
                return 0;

        case SOUND_PCM_READ_RATE:
                return put_user((file->f_mode & FMODE_READ) ? as->usbin.dma.srate : as->usbout.dma.srate, (int *)arg);

        case SOUND_PCM_READ_CHANNELS:
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
                return put_user(AFMT_ISSTEREO(val2) ? 2 : 1, (int *)arg);

        case SOUND_PCM_READ_BITS:
		val2 = (file->f_mode & FMODE_READ) ? as->usbin.dma.format : as->usbout.dma.format;
		return put_user(AFMT_IS16BIT(val2) ? 16 : 8, (int *)arg);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
        }
        return -ENOIOCTLCMD;
}

static int usb_audio_open(struct inode *inode, struct file *file)
{
        int minor = MINOR(inode->i_rdev);
	DECLARE_WAITQUEUE(wait, current);
	struct list_head *devs, *adevs;
	struct usb_audiodev *as;
        struct usb_audio_state *s;

	for (;;) {
		down(&open_sem);
		for (devs = audiodevs.next; devs != &audiodevs; devs = devs->next) {
			s = list_entry(devs, struct usb_audio_state, audiodev);
			for (adevs = s->audiolist.next; adevs != &s->audiolist; adevs = adevs->next) {
				as = list_entry(adevs, struct usb_audiodev, list);
				if (!((as->dev_audio ^ minor) & ~0xf))
					goto device_found;
			}
		}
		up(&open_sem);
		return -ENODEV;

	device_found:
		if (!s->usbdev) {
			up(&open_sem);
			return -EIO;
		}
		/* wait for device to become free */
		if (!(as->open_mode & file->f_mode))
			break;
		if (file->f_flags & O_NONBLOCK) {
			up(&open_sem);
                        return -EBUSY;
                }
		__set_current_state(TASK_INTERRUPTIBLE);
		add_wait_queue(&open_wait, &wait);
                up(&open_sem);
		schedule();
		__set_current_state(TASK_RUNNING);
		remove_wait_queue(&open_wait, &wait);
                if (signal_pending(current))
                        return -ERESTARTSYS;
        }
	if (file->f_mode & FMODE_READ)
                as->usbin.dma.ossfragshift = as->usbin.dma.ossmaxfrags = as->usbin.dma.subdivision = 0;
	if (file->f_mode & FMODE_WRITE)
                as->usbout.dma.ossfragshift = as->usbout.dma.ossmaxfrags = as->usbout.dma.subdivision = 0;
	if (set_format(as, file->f_mode, ((minor & 0xf) == SND_DEV_DSP16) ? AFMT_S16_LE : AFMT_U8 /* AFMT_ULAW */, 8000)) {
		up(&open_sem);
		return -EIO;
	}
        file->private_data = as;
        as->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);
	s->count++;
	MOD_INC_USE_COUNT;
        up(&open_sem);
        return 0;
}

static int usb_audio_release(struct inode *inode, struct file *file)
{
        struct usb_audiodev *as = (struct usb_audiodev *)file->private_data;
	struct usb_audio_state *s = as->state;

        if (file->f_mode & FMODE_WRITE)
                drain_out(as, file->f_flags & O_NONBLOCK);
        down(&open_sem);
        if (file->f_mode & FMODE_WRITE) {
                usbout_stop(as);
		if (s->usbdev)
			usb_set_interface(s->usbdev, as->usbout.interface, 0);
                dmabuf_release(&as->usbout.dma);
		usbout_release(as);
        }
        if (file->f_mode & FMODE_READ) {
                usbin_stop(as);
		if (s->usbdev)
			usb_set_interface(s->usbdev, as->usbin.interface, 0);
                dmabuf_release(&as->usbin.dma);
		usbin_release(as);
        }
        as->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	release(s);
        wake_up(&open_wait);
        MOD_DEC_USE_COUNT;
        return 0;
}

static /*const*/ struct file_operations usb_audio_fops = {
        &usb_audio_llseek,
        &usb_audio_read,
        &usb_audio_write,
        NULL,  /* readdir */
        &usb_audio_poll,
        &usb_audio_ioctl,
        &usb_audio_mmap,
        &usb_audio_open,
        NULL,   /* flush */
        &usb_audio_release,
        NULL,  /* fsync */
        NULL,  /* fasync */
        NULL,  /* check_media_change */
        NULL,  /* revalidate */
        NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

/*
 *	TO DO in order to get to the point of building an OSS interface
 *	structure, let alone playing music..
 *
 *	Use kmalloc/kfree for the descriptors we build
 *	Write the descriptor->OSS convertor code
 *	Figure how we deal with mixers
 *	Check alternate configurations. For now assume we will find one
 *	zero bandwidth (idle) config and one or more live one pers interface.
 */

static void * usb_audio_probe(struct usb_device *dev, unsigned int ifnum);
static void usb_audio_disconnect(struct usb_device *dev, void *ptr);

static struct usb_driver usb_audio_driver = {
	"audio",
	usb_audio_probe,
	usb_audio_disconnect,
	/*{ NULL, NULL }, */ LIST_HEAD_INIT(usb_audio_driver.driver_list), 
	NULL,
	0
};


#if 0
static int usb_audio_irq(int state, void *buffer, int len, void *dev_id)
{
#if 0
	struct usb_audio_device *aud = (struct usb_audio_device *)dev_id;

	printk("irq on %p\n", aud);
#endif

	return 1;
}
#endif

static void *find_descriptor(void *descstart, unsigned int desclen, void *after, 
			     u8 dtype, int iface, int altsetting)
{
	u8 *p, *end, *next;
	int ifc = -1, as = -1;

	p = descstart;
	end = p + desclen;
	for (; p < end;) {
		if (p[0] < 2)
			return NULL;
		next = p + p[0];
		if (next > end)
			return NULL;
		if (p[1] == USB_DT_INTERFACE) {
			/* minimum length of interface descriptor */
			if (p[0] < 9)
				return NULL;
			ifc = p[2];
			as = p[3];
		}
		if (p[1] == dtype && (!after || (void *)p > after) &&
		    (iface == -1 || iface == ifc) && (altsetting == -1 || altsetting == as)) {
			return p;
		}
		p = next;
	}
	return NULL;
}

static void *find_csinterface_descriptor(void *descstart, unsigned int desclen, void *after, u8 dsubtype, int iface, int altsetting)
{
	unsigned char *p;

	p = find_descriptor(descstart, desclen, after, USB_DT_CS_INTERFACE, iface, altsetting);
	while (p) {
		if (p[0] >= 3 && p[2] == dsubtype)
			return p;
		p = find_descriptor(descstart, desclen, p, USB_DT_CS_INTERFACE, iface, altsetting);
	}
	return NULL;
}

static void *find_audiocontrol_unit(void *descstart, unsigned int desclen, void *after, u8 unit, int iface)
{
	unsigned char *p;

	p = find_descriptor(descstart, desclen, after, USB_DT_CS_INTERFACE, iface, -1);
	while (p) {
		if (p[0] >= 4 && p[2] >= INPUT_TERMINAL && p[2] <= EXTENSION_UNIT && p[3] == unit)
			return p;
		p = find_descriptor(descstart, desclen, p, USB_DT_CS_INTERFACE, iface, -1);
	}
	return NULL;
}

static void usb_audio_parsestreaming(struct usb_audio_state *s, unsigned char *buffer, unsigned int buflen, int asifin, int asifout)
{
	struct usb_device *dev = s->usbdev;
	struct usb_audiodev *as;
	struct usb_config_descriptor *config = dev->actconfig;
	struct usb_interface_descriptor *alts;
	struct usb_interface *iface;
	struct audioformat *fp;
	unsigned char *fmt, *csep;
	unsigned int i, j, k, format;

	if (!(as = kmalloc(sizeof(struct usb_audiodev), GFP_KERNEL)))
		return;
	memset(as, 0, sizeof(struct usb_audiodev));
	init_waitqueue_head(&as->usbin.dma.wait);
	init_waitqueue_head(&as->usbout.dma.wait);
	spin_lock_init(&as->lock);
	as->state = s;
	as->usbin.interface = asifin;
	as->usbout.interface = asifout;
	/* search for input formats */
	if (asifin >= 0) {
		iface = &config->interface[asifin];
		for (i = 0; i < iface->num_altsetting; i++) {
			alts = &iface->altsetting[i];
			if (alts->bInterfaceClass != USB_CLASS_AUDIO || alts->bInterfaceSubClass != 2)
				continue;
			if (alts->bNumEndpoints < 1) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u does not have an endpoint\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if ((alts->endpoint[0].bmAttributes & 0x03) != 0x01 ||
			    !(alts->endpoint[0].bEndpointAddress & 0x80)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u first endpoint not isochronous in\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, AS_GENERAL, asifin, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (fmt[0] < 7 || fmt[6] != 0 || (fmt[5] != 1 && fmt[5] != 2)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u format not supported\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			format = (fmt[5] == 2) ? (AFMT_U16_LE | AFMT_U8) : (AFMT_S16_LE | AFMT_S8);
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, FORMAT_TYPE, asifin, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (fmt[0] < 8+3*(fmt[7] ? fmt[7] : 2) || fmt[3] != 1) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not supported\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (fmt[4] < 1 || fmt[4] > 2 || fmt[5] < 1 || fmt[5] > 2) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u unsupported channels %u framesize %u\n", 
				       dev->devnum, asifin, i, fmt[4], fmt[5]);
				continue;
			}
			csep = find_descriptor(buffer, buflen, NULL, USB_DT_CS_ENDPOINT, asifin, i);
			if (!csep || csep[0] < 7 || csep[2] != EP_GENERAL) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u no or invalid class specific endpoint descriptor\n", 
				       dev->devnum, asifin, i);
				continue;
			}
			if (as->numfmtin >= MAXFORMATS)
				continue;
			fp = &as->fmtin[as->numfmtin++];
			if (fmt[5] == 2)
				format &= (AFMT_U16_LE | AFMT_S16_LE);
			else
				format &= (AFMT_U8 | AFMT_S8);
			if (fmt[4] == 2)
				format |= AFMT_STEREO;
			fp->format = format;
			fp->altsetting = i;
			fp->sratelo = fp->sratehi = fmt[8] | (fmt[9] << 8) | (fmt[10] << 16);
			for (j = fmt[7] ? (fmt[7]-1) : 1; j > 0; j--) {
				k = fmt[8+3*j] | (fmt[9+3*j] << 8) | (fmt[10+3*j] << 16);
				if (k > fp->sratehi)
					fp->sratehi = k;
				if (k < fp->sratelo)
					fp->sratelo = k;
			}
			fp->attributes = csep[3];
			printk(KERN_INFO "usbaudio: device %u interface %u altsetting %u: format 0x%08x sratelo %u sratehi %u attributes 0x%02x\n", 
			       dev->devnum, asifin, i, fp->format, fp->sratelo, fp->sratehi, fp->attributes);
		}
	}
	/* search for output formats */
	if (asifout >= 0) {
		iface = &config->interface[asifout];
		for (i = 0; i < iface->num_altsetting; i++) {
			alts = &iface->altsetting[i];
			if (alts->bInterfaceClass != USB_CLASS_AUDIO || alts->bInterfaceSubClass != 2)
				continue;
			if (alts->bNumEndpoints < 1) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u does not have an endpoint\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if ((alts->endpoint[0].bmAttributes & 0x03) != 0x01 ||
			    (alts->endpoint[0].bEndpointAddress & 0x80)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u first endpoint not isochronous out\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, AS_GENERAL, asifout, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (fmt[0] < 7 || fmt[6] != 0 || (fmt[5] != 1 && fmt[5] != 2)) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u format not supported\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			format = (fmt[5] == 2) ? (AFMT_U16_LE | AFMT_U8) : (AFMT_S16_LE | AFMT_S8);
			fmt = find_csinterface_descriptor(buffer, buflen, NULL, FORMAT_TYPE, asifout, i);
			if (!fmt) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not found\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (fmt[0] < 8+3*(fmt[7] ? fmt[7] : 2) || fmt[3] != 1) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u FORMAT_TYPE descriptor not supported\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (fmt[4] < 1 || fmt[4] > 2 || fmt[5] < 1 || fmt[5] > 2) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u unsupported channels %u framesize %u\n", 
				       dev->devnum, asifout, i, fmt[4], fmt[5]);
				continue;
			}
			csep = find_descriptor(buffer, buflen, NULL, USB_DT_CS_ENDPOINT, asifout, i);
			if (!csep || csep[0] < 7 || csep[2] != EP_GENERAL) {
				printk(KERN_ERR "usbaudio: device %u interface %u altsetting %u no or invalid class specific endpoint descriptor\n", 
				       dev->devnum, asifout, i);
				continue;
			}
			if (as->numfmtout >= MAXFORMATS)
				continue;
			fp = &as->fmtout[as->numfmtout++];
			if (fmt[5] == 2)
				format &= (AFMT_U16_LE | AFMT_S16_LE);
			else
				format &= (AFMT_U8 | AFMT_S8);
			if (fmt[4] == 2)
				format |= AFMT_STEREO;
			fp->format = format;
			fp->altsetting = i;
			fp->sratelo = fp->sratehi = fmt[8] | (fmt[9] << 8) | (fmt[10] << 16);
			for (j = fmt[7] ? (fmt[7]-1) : 1; j > 0; j--) {
				k = fmt[8+3*j] | (fmt[9+3*j] << 8) | (fmt[10+3*j] << 16);
				if (k > fp->sratehi)
					fp->sratehi = k;
				if (k < fp->sratelo)
					fp->sratelo = k;
			}
			fp->attributes = csep[3];
			printk(KERN_INFO "usbaudio: device %u interface %u altsetting %u: format 0x%08x sratelo %u sratehi %u attributes 0x%02x\n", 
			       dev->devnum, asifout, i, fp->format, fp->sratelo, fp->sratehi, fp->attributes);
		}
	}
	if (as->numfmtin == 0 && as->numfmtout == 0) {
		kfree(as);
		return;
	}
	if ((as->dev_audio = register_sound_dsp(&usb_audio_fops, -1)) < 0) {
		printk(KERN_ERR "usbaudio: cannot register dsp\n");
		kfree(as);
		return;
	}
	/* everything successful */
	list_add_tail(&as->list, &s->audiolist);
}

struct consmixstate {
	struct usb_audio_state *s;
	unsigned char *buffer;
	unsigned int buflen;
	unsigned int ctrlif;
	struct mixerchannel mixch[SOUND_MIXER_NRDEVICES];
	unsigned int nrmixch;
	unsigned int mixchmask;
	unsigned long unitbitmap[32/sizeof(unsigned long)];
	/* return values */
	unsigned int nrchannels;
	unsigned int termtype;
	unsigned int chconfig;
};

static struct mixerchannel *getmixchannel(struct consmixstate *state, unsigned int nr)
{
	struct mixerchannel *c;

	if (nr >= SOUND_MIXER_NRDEVICES) {
		printk(KERN_ERR "usbaudio: invalid OSS mixer channel %u\n", nr);
		return NULL;
	}
	if (!(state->mixchmask & (1 << nr))) {
		printk(KERN_WARNING "usbaudio: OSS mixer channel %u already in use\n", nr);
		return NULL;
	}
	c = &state->mixch[state->nrmixch++];
	c->osschannel = nr;
	state->mixchmask &= ~(1 << nr);
	return c;
}

static unsigned int getvolchannel(struct consmixstate *state)
{
	unsigned int u;

	if ((state->termtype & 0xff00) == 0x0000 && !(state->mixchmask & SOUND_MASK_VOLUME))
		return SOUND_MIXER_VOLUME;
	if ((state->termtype & 0xff00) == 0x0100) {
		if (state->mixchmask & SOUND_MASK_PCM)
			return SOUND_MIXER_PCM;
		if (state->mixchmask & SOUND_MASK_ALTPCM)
			return SOUND_MIXER_ALTPCM;
	}
	if ((state->termtype & 0xff00) == 0x0200 && (state->mixchmask & SOUND_MASK_MIC))
		return SOUND_MIXER_MIC;
	if ((state->termtype & 0xff00) == 0x0300 && (state->mixchmask & SOUND_MASK_SPEAKER))
		return SOUND_MIXER_SPEAKER;
	if ((state->termtype & 0xff00) == 0x0300 && (state->mixchmask & SOUND_MASK_SPEAKER))
		return SOUND_MIXER_SPEAKER;
	if ((state->termtype & 0xff00) == 0x0500) {
		if (state->mixchmask & SOUND_MASK_PHONEIN)
			return SOUND_MIXER_PHONEIN;
		if (state->mixchmask & SOUND_MASK_PHONEOUT)
			return SOUND_MIXER_PHONEOUT;
	}
	if (state->termtype >= 0x710 && state->termtype <= 0x711 && (state->mixchmask & SOUND_MASK_RADIO))
		return SOUND_MIXER_RADIO;
	if (state->termtype >= 0x709 && state->termtype <= 0x70f && (state->mixchmask & SOUND_MASK_VIDEO))
		return SOUND_MIXER_VIDEO;
	u = ffs(state->mixchmask & (SOUND_MASK_LINE | SOUND_MASK_CD | SOUND_MASK_LINE1 | SOUND_MASK_LINE2 | SOUND_MASK_LINE3 |
				    SOUND_MASK_DIGITAL1 | SOUND_MASK_DIGITAL2 | SOUND_MASK_DIGITAL3));
	return u-1;
}

static void prepmixch(struct consmixstate *state)
{
	struct usb_device *dev = state->s->usbdev;
	struct mixerchannel *ch;
	unsigned char buf[2];
	__s16 v1;
	unsigned int v2, v3;

	if (!state->nrmixch || state->nrmixch > SOUND_MIXER_NRDEVICES)
		return;
	ch = &state->mixch[state->nrmixch-1];
	switch (ch->selector) {
	case 0:  /* mixer unit request */
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MIN, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->chnum << 8) | 1, state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
		ch->minval = buf[0] | (buf[1] << 8);
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MAX, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->chnum << 8) | 1, state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
		ch->maxval = buf[0] | (buf[1] << 8);
		v2 = ch->maxval - ch->minval;
		if (!v2)
			v2 = 1;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->chnum << 8) | 1, state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
		v1 = buf[0] | (buf[1] << 8);
		v3 = v1 - ch->minval;
		v3 = 100 * v3 / v2;
		if (v3 > 100)
			v3 = 100;
		ch->value = v3;
		if (ch->flags & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)) {
			if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
					    ((ch->chnum + !!(ch->flags & MIXFLG_STEREOIN)) << 8) | (1 + !!(ch->flags & MIXFLG_STEREOOUT)),
					    state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
			v1 = buf[0] | (buf[1] << 8);
			v3 = v1 - ch->minval;
			v3 = 100 * v3 / v2;
			if (v3 > 100)
				v3 = 100;
		}
		ch->value |= v3 << 8;
		break;

		/* various feature unit controls */
	case VOLUME_CONTROL:
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MIN, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
		ch->minval = buf[0] | (buf[1] << 8);
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MAX, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
		ch->maxval = buf[0] | (buf[1] << 8);
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
			goto err;
		v1 = buf[0] | (buf[1] << 8);
		v2 = ch->maxval - ch->minval;
		v3 = v1 - ch->minval;
		if (!v2)
			v2 = 1;
		v3 = 100 * v3 / v2;
		if (v3 > 100)
			v3 = 100;
		ch->value = v3;
		if (ch->chnum != 0) {
			if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
					    (ch->selector << 8) | (ch->chnum + 1), state->ctrlif | (ch->unitid << 8), buf, 2, HZ) < 0)
				goto err;
			v1 = buf[0] | (buf[1] << 8);
			v3 = v1 - ch->minval;
			v3 = 100 * v3 / v2;
			if (v3 > 100)
				v3 = 100;
		}
		ch->value |= v3 << 8;
		break;
		
	case BASS_CONTROL:
	case MID_CONTROL:
	case TREBLE_CONTROL:
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MIN, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 1, HZ) < 0)
			goto err;
		ch->minval = buf[0] << 8;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_MAX, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 1, HZ) < 0)
			goto err;
		ch->maxval = buf[0] << 8;
		if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
				    (ch->selector << 8) | ch->chnum, state->ctrlif | (ch->unitid << 8), buf, 1, HZ) < 0)
			goto err;
		v1 = buf[0] << 8;
		v2 = ch->maxval - ch->minval;
		v3 = v1 - ch->minval;
		if (!v2)
			v2 = 1;
		v3 = 100 * v3 / v2;
		if (v3 > 100)
			v3 = 100;
		ch->value = v3;
		if (ch->chnum != 0) {
			if (usb_control_msg(dev, usb_rcvctrlpipe(dev, 0), GET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_IN,
					    (ch->selector << 8) | (ch->chnum + 1), state->ctrlif | (ch->unitid << 8), buf, 1, HZ) < 0)
				goto err;
			v1 = buf[0] << 8;
			v3 = v1 - ch->minval;
			v3 = 100 * v3 / v2;
			if (v3 > 100)
				v3 = 100;
		}
		ch->value |= v3 << 8;
		break;
		
	default:
		goto err;
	}
	return;

 err:
	printk(KERN_ERR "usbaudio: mixer request device %u if %u unit %u ch %u selector %u failed\n", 
	       dev->devnum, state->ctrlif, ch->unitid, ch->chnum, ch->selector);
	if (state->nrmixch)
		state->nrmixch--;
}


static void usb_audio_recurseunit(struct consmixstate *state, unsigned char unitid);

extern inline int checkmixbmap(unsigned char *bmap, unsigned char flg, unsigned int inidx, unsigned int numoch)
{
	unsigned int idx;

	idx = inidx*numoch;
	if (!(bmap[-(idx >> 3)] & (0x80 >> (idx & 7))))
		return 0;
	if (!(flg & (MIXFLG_STEREOIN | MIXFLG_STEREOOUT)))
		return 1;
	idx = (inidx+!!(flg & MIXFLG_STEREOIN))*numoch+!!(flg & MIXFLG_STEREOOUT);
	if (!(bmap[-(idx >> 3)] & (0x80 >> (idx & 7))))
		return 0;
	return 1;
}

static void usb_audio_mixerunit(struct consmixstate *state, unsigned char *mixer)
{
	unsigned int nroutch = mixer[5+mixer[4]];
	unsigned int chidx[SOUND_MIXER_NRDEVICES+1];
	unsigned int termt[SOUND_MIXER_NRDEVICES];
	unsigned char flg = (nroutch >= 2) ? MIXFLG_STEREOOUT : 0;
	unsigned char *bmap = &mixer[9+mixer[4]];
	unsigned int bmapsize;
	struct mixerchannel *ch;
	unsigned int i;

	if (!mixer[4]) {
		printk(KERN_ERR "usbaudio: unit %u invalid MIXER_UNIT descriptor\n", mixer[3]);
		return;
	}
	if (mixer[4] > SOUND_MIXER_NRDEVICES) {
		printk(KERN_ERR "usbaudio: mixer unit %u: too many input pins\n", mixer[3]);
		return;
	}
	chidx[0] = 0;
	for (i = 0; i < mixer[4]; i++) {
		usb_audio_recurseunit(state, mixer[5+i]);
		chidx[i+1] = chidx[i] + state->nrchannels;
		termt[i] = state->termtype;
	}
	state->termtype = 0;
	state->chconfig = mixer[6+mixer[4]] | (mixer[7+mixer[4]] << 8);
	bmapsize = (nroutch * chidx[mixer[4]] + 7) >> 3;
	bmap += bmapsize - 1;
	if (mixer[0] < 10+mixer[4]+bmapsize) {
		printk(KERN_ERR "usbaudio: unit %u invalid MIXER_UNIT descriptor (bitmap too small)\n", mixer[3]);
		return;
	}
	for (i = 0; i < mixer[4]; i++) {
		state->termtype = termt[i];
		if (chidx[i+1]-chidx[i] >= 2) {
			flg |= MIXFLG_STEREOIN;
			if (checkmixbmap(bmap, flg, chidx[i], nroutch)) {
				ch = getmixchannel(state, getvolchannel(state));
				if (ch) {
					ch->unitid = mixer[3];
					ch->selector = 0;
					ch->chnum = chidx[i]+1;
					ch->flags = flg;
					prepmixch(state);
				}
				continue;
			}
		}
		flg &= ~MIXFLG_STEREOIN;
		if (checkmixbmap(bmap, flg, chidx[i], nroutch)) {
			ch = getmixchannel(state, getvolchannel(state));
			if (ch) {
				ch->unitid = mixer[3];
				ch->selector = 0;
				ch->chnum = chidx[i]+1;
				ch->flags = flg;
				prepmixch(state);
			}
		}
	}	
	state->termtype = 0;
}

static void usb_audio_selectorunit(struct consmixstate *state, unsigned char *selector)
{
	unsigned int chnum, i;

	if (!selector[4]) {
		printk(KERN_ERR "usbaudio: unit %u invalid SELECTOR_UNIT descriptor\n", selector[3]);
		return;
	}
	usb_audio_recurseunit(state, selector[5]);
	chnum = state->nrchannels;
	for (i = 1; i < selector[4]; i++) {
		usb_audio_recurseunit(state, selector[5+i]);
		if (chnum != state->nrchannels) {
			printk(KERN_ERR "usbaudio: selector unit %u: input pins with varying channel numbers\n", selector[3]);
			state->termtype = 0;
			state->chconfig = 0;
			state->nrchannels = 0;
			return;
		}
	}
	state->termtype = 0;
	state->chconfig = 0;
}

/* in the future we might try to handle 3D etc. effect units */

static void usb_audio_processingunit(struct consmixstate *state, unsigned char *proc)
{
	unsigned int i;

	for (i = 0; i < proc[6]; i++)
		usb_audio_recurseunit(state, proc[7+i]);
	state->nrchannels = proc[7+proc[6]];
	state->termtype = 0;
	state->chconfig = proc[8+proc[6]] | (proc[9+proc[6]] << 8);
}

static void usb_audio_featureunit(struct consmixstate *state, unsigned char *ftr)
{
	struct usb_device *dev = state->s->usbdev;
	struct mixerchannel *ch;
	unsigned short chftr, mchftr;
	unsigned char data[1];

	usb_audio_recurseunit(state, ftr[4]);
	if (state->nrchannels == 0) {
		printk(KERN_ERR "usbaudio: feature unit %u source has no channels\n", ftr[3]);
		return;
	}
	if (state->nrchannels > 2)
		printk(KERN_WARNING "usbaudio: feature unit %u: OSS mixer interface does not support more than 2 channels\n", ftr[3]);
	if (ftr[0] < 7+ftr[5]*(1+state->nrchannels)) {
		printk(KERN_ERR "usbaudio: unit %u: invalid FEATURE_UNIT descriptor\n", ftr[3]);
		return;
	}
	mchftr = ftr[6];
	chftr = ftr[6+ftr[5]];
	if (state->nrchannels > 1)
		chftr &= ftr[6+2*ftr[5]];
	/* volume control */
	if (chftr & 2) {
		ch = getmixchannel(state, getvolchannel(state));
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = VOLUME_CONTROL;
			ch->chnum = 1;
			ch->flags = MIXFLG_STEREOIN | MIXFLG_STEREOOUT;
			prepmixch(state);
		}
	} else if (mchftr & 2) {
		ch = getmixchannel(state, getvolchannel(state));
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = VOLUME_CONTROL;
			ch->chnum = 0;
			ch->flags = 0;
			prepmixch(state);
		}
	}
	/* bass control */
	if (chftr & 4) {
		ch = getmixchannel(state, SOUND_MIXER_BASS);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = BASS_CONTROL;
			ch->chnum = 1;
			ch->flags = MIXFLG_STEREOIN | MIXFLG_STEREOOUT;
			prepmixch(state);
		}
	} else if (mchftr & 4) {
		ch = getmixchannel(state, SOUND_MIXER_BASS);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = BASS_CONTROL;
			ch->chnum = 0;
			ch->flags = 0;
			prepmixch(state);
		}
	}
	/* treble control */
	if (chftr & 16) {
		ch = getmixchannel(state, SOUND_MIXER_TREBLE);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = TREBLE_CONTROL;
			ch->chnum = 1;
			ch->flags = MIXFLG_STEREOIN | MIXFLG_STEREOOUT;
			prepmixch(state);
		}
	} else if (mchftr & 16) {
		ch = getmixchannel(state, SOUND_MIXER_TREBLE);
		if (ch) {
			ch->unitid = ftr[3];
			ch->selector = TREBLE_CONTROL;
			ch->chnum = 0;
			ch->flags = 0;
			prepmixch(state);
		}
	}
#if 0
	/* if there are mute controls, unmute them */
	/* does not seem to be necessary, and the Dallas chip does not seem to support the "all" channel (255) */
	if ((chftr & 1) || (mchftr & 1)) {
		printk(KERN_DEBUG "usbaudio: unmuting feature unit %u interface %u\n", ftr[3], state->ctrlif);
		data[0] = 0;
		if (usb_control_msg(dev, usb_sndctrlpipe(dev, 0), SET_CUR, USB_RECIP_INTERFACE | USB_TYPE_CLASS | USB_DIR_OUT,
                                    (MUTE_CONTROL << 8) | 0xff, state->ctrlif | (ftr[3] << 8), data, 1, HZ) < 0)
			printk(KERN_WARNING "usbaudio: failure to unmute feature unit %u interface %u\n", ftr[3], state->ctrlif);
 	}
#endif
}

static void usb_audio_recurseunit(struct consmixstate *state, unsigned char unitid)
{
	unsigned char *p1;
	unsigned int i, j;

	if (test_and_set_bit(unitid, &state->unitbitmap)) {
		printk(KERN_ERR "usbaudio: mixer path recursion detected, unit %d!\n", unitid);
		return;
	}
	p1 = find_audiocontrol_unit(state->buffer, state->buflen, NULL, unitid, state->ctrlif);
	if (!p1) {
		printk(KERN_ERR "usbaudio: unit %d not found!\n", unitid);
		return;
	}
	state->nrchannels = 0;
	state->termtype = 0;
	state->chconfig = 0;
	switch (p1[2]) {
	case INPUT_TERMINAL:
		if (p1[0] < 12) {
			printk(KERN_ERR "usbaudio: unit %u: invalid INPUT_TERMINAL descriptor\n", unitid);
			return;
		}
		state->nrchannels = p1[7];
		state->termtype = p1[4] | (p1[5] << 8);
		state->chconfig = p1[8] | (p1[9] << 8);
		return;

	case MIXER_UNIT:
		if (p1[0] < 10 || p1[0] < 10+p1[4]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid MIXER_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_mixerunit(state, p1);
		return;

	case SELECTOR_UNIT:
		if (p1[0] < 6 || p1[0] < 6+p1[4]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid SELECTOR_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_selectorunit(state, p1);
		return;

	case FEATURE_UNIT:
		if (p1[0] < 7 || p1[0] < 7+p1[5]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid FEATURE_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_featureunit(state, p1);
		return;		

	case PROCESSING_UNIT:
		if (p1[0] < 13 || p1[0] < 13+p1[6] || p1[0] < 13+p1[6]+p1[11+p1[6]] || p1[0] < 13+p1[6]+p1[11+p1[6]]+p1[13+p1[6]+p1[11+p1[6]]]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid PROCESSING_UNIT descriptor\n", unitid);
			return;
		}
		usb_audio_processingunit(state, p1);
		return;		

	case EXTENSION_UNIT:
		if (p1[0] < 13 || p1[0] < 13+p1[6] || p1[0] < 13+p1[6]+p1[11+p1[6]]) {
			printk(KERN_ERR "usbaudio: unit %u: invalid EXTENSION_UNIT descriptor\n", unitid);
			return;
		}
		for (j = i = 0; i < p1[6]; i++) {
			usb_audio_recurseunit(state, p1[7+i]);
			if (!i)
				j = state->termtype;
			else if (j != state->termtype)
				j = 0;
		}
		state->nrchannels = p1[7+p1[6]];
		state->chconfig = p1[8+p1[6]] | (p1[9+p1[6]] << 8);
		state->termtype = j;
		return;

	default:
		printk(KERN_ERR "usbaudio: unit %u: unexpected type 0x%02x\n", unitid, p1[2]);
		return;
	}
}

static void usb_audio_constructmixer(struct usb_audio_state *s, unsigned char *buffer, unsigned int buflen, unsigned int ctrlif, unsigned char *oterm)
{
	struct usb_mixerdev *ms;
	struct consmixstate state;

	memset(&state, 0, sizeof(state));
	state.s = s;
	state.nrmixch = 0;
	state.mixchmask = ~0;
	state.buffer = buffer;
	state.buflen = buflen;
	state.ctrlif = ctrlif;
	set_bit(oterm[3], &state.unitbitmap);  /* mark terminal ID as visited */
	printk(KERN_INFO "usbaudio: constructing mixer for Terminal %u type 0x%04x\n",
	       oterm[3], oterm[4] | (oterm[5] << 8));
	usb_audio_recurseunit(&state, oterm[7]);
	if (!state.nrmixch) {
		printk(KERN_INFO "usbaudio: no mixer controls found for Terminal %u\n", oterm[3]);
		return;
	}
	if (!(ms = kmalloc(sizeof(struct usb_mixerdev)+state.nrmixch*sizeof(struct mixerchannel), GFP_KERNEL)))
		return;
	memset(ms, 0, sizeof(struct usb_mixerdev));
	memcpy(&ms->ch, &state.mixch, state.nrmixch*sizeof(struct mixerchannel));
	ms->state = s;
	ms->iface = ctrlif;
	ms->numch = state.nrmixch;
	if ((ms->dev_mixer = register_sound_mixer(&usb_mixer_fops, -1)) < 0) {
		printk(KERN_ERR "usbaudio: cannot register mixer\n");
		kfree(ms);
		return;
	}
	list_add_tail(&ms->list, &s->mixerlist);
}

static void * usb_audio_parsecontrol(struct usb_device *dev, unsigned char *buffer, unsigned int buflen, unsigned int ctrlif)
{
	struct usb_audio_state *s;
	struct usb_config_descriptor *config = dev->actconfig;
	struct usb_interface *iface;
	unsigned char ifin[USB_MAXINTERFACES], ifout[USB_MAXINTERFACES];
	unsigned char *p1;
	unsigned int i, j, numifin = 0, numifout = 0;

	if (!(s = kmalloc(sizeof(struct usb_audio_state), GFP_KERNEL)))
		return NULL;
	memset(s, 0, sizeof(struct usb_audio_state));
	INIT_LIST_HEAD(&s->audiolist);
	INIT_LIST_HEAD(&s->mixerlist);
	s->usbdev = dev;
	s->count = 1;
	/* find audiocontrol interface */
	if (!(p1 = find_csinterface_descriptor(buffer, buflen, NULL, HEADER, ctrlif, -1))) {
		printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u no HEADER found\n",
		       dev->devnum, ctrlif);
		goto ret;
	}
	if (p1[0] < 8 + p1[7]) {
		printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u HEADER error\n",
		       dev->devnum, ctrlif);
		goto ret;
	}
	if (!p1[7])
		printk(KERN_INFO "usbaudio: device %d audiocontrol interface %u has no AudioStreaming and MidiStreaming interfaces\n",
		       dev->devnum, ctrlif);
	for (i = 0; i < p1[7]; i++) {
		j = p1[8+i];
		if (j >= config->bNumInterfaces) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u does not exist\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		iface = &config->interface[j];
		if (iface->altsetting[0].bInterfaceClass != USB_CLASS_AUDIO) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u is not an AudioClass interface\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (iface->altsetting[0].bInterfaceSubClass == 3) {
			printk(KERN_INFO "usbaudio: device %d audiocontrol interface %u interface %u MIDIStreaming not supported\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (iface->altsetting[0].bInterfaceSubClass != 2) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u invalid AudioClass subtype\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		if (iface->num_altsetting < 2 ||
		    iface->altsetting[0].bNumEndpoints > 0) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u altsetting 0 not zero bandwidth\n",
			       dev->devnum, ctrlif);
			continue;
		}
		if (iface->altsetting[1].bNumEndpoints < 1) {
			printk(KERN_ERR "usbaudio: device %d audiocontrol interface %u interface %u has no endpoint\n",
			       dev->devnum, ctrlif, j);
			continue;
		}
		/* note: this requires the data endpoint to be ep0 and the optional sync
		   ep to be ep1, which seems to be the case */
		if (iface->altsetting[1].endpoint[0].bEndpointAddress & USB_DIR_IN) {
			if (numifin < USB_MAXINTERFACES) {
				ifin[numifin++] = j;
				usb_driver_claim_interface(&usb_audio_driver, iface, s);
			}
		} else {
			if (numifout < USB_MAXINTERFACES) {
				ifout[numifout++] = j;
				usb_driver_claim_interface(&usb_audio_driver, iface, s);
			}
		}
	}
	printk(KERN_INFO "usbaudio: device %d audiocontrol interface %u has %u input and %u output AudioStreaming interfaces\n",
	       dev->devnum, ctrlif, numifin, numifout);
	for (i = 0; i < numifin && i < numifout; i++)
		usb_audio_parsestreaming(s, buffer, buflen, ifin[i], ifout[i]);
	for (j = i; j < numifin; j++)
		usb_audio_parsestreaming(s, buffer, buflen, ifin[i], -1);
	for (j = i; j < numifout; j++)
		usb_audio_parsestreaming(s, buffer, buflen, -1, ifout[i]);
	/* now walk through all OUTPUT_TERMINAL descriptors to search for mixers */
	p1 = find_csinterface_descriptor(buffer, buflen, NULL, OUTPUT_TERMINAL, ctrlif, -1);
	while (p1) {
		if (p1[0] >= 9)
			usb_audio_constructmixer(s, buffer, buflen, ctrlif, p1);
		p1 = find_csinterface_descriptor(buffer, buflen, p1, OUTPUT_TERMINAL, ctrlif, -1);
	}

 ret:
	if (list_empty(&s->audiolist) && list_empty(&s->mixerlist)) {
		kfree(s);
		return NULL;
	}
	/* everything successful */
	down(&open_sem);
	list_add_tail(&s->audiodev, &audiodevs);
	up(&open_sem);
	return s;
}

/* we only care for the currently active configuration */

static void * usb_audio_probe(struct usb_device *dev, unsigned int ifnum)
{
	struct usb_config_descriptor *config = dev->actconfig;	
	unsigned char *buffer;
	unsigned char buf[8];
	unsigned int i, buflen;
	int ret;

	for (i = 0; i < config->bNumInterfaces; i++)
		if (config->interface[i].altsetting[0].bInterfaceClass == USB_CLASS_AUDIO &&
		    config->interface[i].altsetting[0].bInterfaceSubClass == 1)  /* audiocontrol interface found */
			goto audioctrlfound;
	printk(KERN_DEBUG "usbaudio: vendor id 0x%04x, product id 0x%04x contains no AudioControl interface\n",
	       dev->descriptor.idVendor, dev->descriptor.idProduct);
	return NULL;

 audioctrlfound:
	/* find which configuration number is active */
	for (i = 0; i < dev->descriptor.bNumConfigurations; i++)
		if (dev->config+i == config)
			goto configfound;
	printk(KERN_ERR "usbaudio: cannot find active configuration number of device %d\n", dev->devnum);
	return NULL;

 configfound:
	ret = usb_get_descriptor(dev, USB_DT_CONFIG, i, buf, 8);
	if (ret) {
		printk(KERN_ERR "usbaudio: cannot get first 8 bytes of config descriptor %d of device %d\n", i, dev->devnum);
		return NULL;
	}
	if (buf[1] != USB_DT_CONFIG || buf[0] < 9) {
		printk(KERN_ERR "usbaudio: invalid config descriptor %d of device %d\n", i, dev->devnum);
		return NULL;
	}
	buflen = buf[2] | (buf[3] << 8);
	if (!(buffer = kmalloc(buflen, GFP_KERNEL)))
		return NULL;
	ret = usb_get_descriptor(dev, USB_DT_CONFIG, i, buffer, buflen);
	if (ret) {
		kfree(buffer);
		printk(KERN_ERR "usbaudio: cannot get config descriptor %d of device %d\n", i, dev->devnum);
		return NULL;
	}
	/* find first audio control interface; we currently cannot handle more than one */
	for (i = 0; i < config->bNumInterfaces; i++) {
		if (config->interface[i].altsetting[0].bInterfaceClass != USB_CLASS_AUDIO ||
		    config->interface[i].altsetting[0].bInterfaceSubClass != 1)
			continue;
		/* audiocontrol interface found */
		return usb_audio_parsecontrol(dev, buffer, buflen, i);
	}
	return NULL;
}


/* a revoke facility would make things simpler */

static void usb_audio_disconnect(struct usb_device *dev, void *ptr)
{
	struct usb_audio_state *s = (struct usb_audio_state *)ptr;
	struct list_head *list;
	struct usb_audiodev *as;
	struct usb_mixerdev *ms;

        down(&open_sem);
	list_del(&s->audiodev);
	INIT_LIST_HEAD(&s->audiodev);
	s->usbdev = NULL;
	/* deregister all audio and mixer devices, so no new processes can open this device */
	for(list = s->audiolist.next; list != &s->audiolist; list = list->next) {
		as = list_entry(list, struct usb_audiodev, list);
		if (as->dev_audio >= 0)
			unregister_sound_dsp(as->dev_audio);
		as->dev_audio = -1;
	}
	for(list = s->mixerlist.next; list != &s->mixerlist; list = list->next) {
		ms = list_entry(list, struct usb_mixerdev, list);
		if (ms->dev_mixer >= 0)
			unregister_sound_mixer(ms->dev_mixer);
	}
#if 0
	if(aud->irq_handle)
		usb_release_irq(dev, aud->irq_handle, aud->irqpipe);
	aud->irq_handle = NULL;
#endif
	release(s);
        wake_up(&open_wait);
}

int usb_audio_init(void)
{
	return usb_register(&usb_audio_driver);
}

#ifdef MODULE
int init_module(void)
{
	return usb_audio_init();
}

void cleanup_module(void)
{
	usb_deregister(&usb_audio_driver);
}

#endif
