/*****************************************************************************
 *
 *      ESS Maestro/Maestro-2/Maestro-2E driver for Linux 2.2.x
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *	(c) Copyright 1999	 Alan Cox <alan.cox@linux.org>
 *
 *	Based heavily on SonicVibes.c:
 *      Copyright (C) 1998-1999  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *
 *	Heavily modified by Zach Brown <zab@redhat.com> based on lunch
 *	with ESS engineers.  Many thanks to Howard Kim for providing 
 *	contacts and hardware.  Honorable mention goes to Eric 
 *	Brombaugh for all sorts of things.  Best regards to the 
 *	proprietors of Hack Central for fine lodging.
 *
 *  Supported devices:
 *  /dev/dsp0-7    standard /dev/dsp device, (mostly) OSS compatible
 *  /dev/mixer  standard /dev/mixer device, (mostly) OSS compatible
 *
 *  Hardware Description
 *
 *	A working Maestro setup contains the Maestro chip wired to a 
 *	codec or 2.  In the Maestro we have the APUs, the ASP, and the
 *	Wavecache.  The APUs can be though of as virtual audio routing
 *	channels.  They can take data from a number of sources and perform
 *	basic encodings of the data.  The wavecache is a storehouse for
 *	PCM data.  Typically it deals with PCI and interracts with the
 *	APUs.  The ASP is a wacky DSP like device that ESS is loth
 *	to release docs on.  Thankfully it isn't required on the Maestro
 *	until you start doing insane things like FM emulation and surround
 *	encoding.  The codecs are almost always AC-97 compliant codecs, 
 *	but it appears that early Maestros may have had PT101 (an ESS
 *	part?) wired to them.  The only real difference in the Maestro
 *	families is external goop like docking capability, memory for
 *	the ASP, and initialization differences.
 *
 *  Driver Operation
 *
 *	We only drive the APU/Wavecache as typical DACs and drive the
 *	mixers in the codecs.  There are 64 APUs.  We assign 6 to each
 *	/dev/dsp? device.  2 channels for output, and 4 channels for
 *	input.
 *
 *	For output we maintain a ring buffer of data that we are DMAing
 *	to the card.  In mono operation this is nice and easy.  When
 *	we receive data we tack it onto the ring buffer and make sure
 *	the APU assigned to it is playing over the data.  When we fill
 *	the ring buffer we put the client to sleep until there is
 *	room again.  Easy.
 *
 *	However, this starts to stink when we use stereo.  The APUs
 *	supposedly can decode LRLR packed stereo data, but it
 *	doesn't work.  So we're forced to use dual mono APUs walking over
 *	mono encoded data.  This requires us to split the input from
 *	the client and complicates the buffer maths tremendously.  Ick.
 *
 *	This also pollutes the recording paths as well.  We have to use
 *	2 L/R incoming APUs that are fixed at 16bit/48khz.  We then pipe
 * 	these through 2 rate converion apus that mix them down to the
 * 	requested frequency and write them to memory through the wavecache.
 *	We also apparently need a 512byte region thats used as temp space 
 *	between the incoming APUs and the rate converters.
 *
 *	The wavecache makes our life even more fun.  First off, it can
 *	only address the first 28 bits of PCI address space, making it
 *	useless on quite a few architectures.  Secondly, its insane.
 *	It claims to fetch from 4 regions of PCI space, each 4 meg in length.
 *	But that doesn't really work.  You can only use 1 region.  So all our
 *	allocations have to be in 4meg of each other.  Booo.  Hiss.
 *	So we have a module parameter, dsps_order, that is the order of
 *	the number of dsps to provide.  All their buffer space is allocated
 *	on open time.  The sonicvibes OSS routines we inherited really want
 *	power of 2 buffers, so we have all those next to each other, then
 *	512 byte regions for the recording wavecaches.  This ends up
 *	wasting quite a bit of memory.  The only fixes I can see would be 
 *	getting a kernel allocator that could work in zones, or figuring out
 *	just how to coerce the WP into doing what we want.
 *
 *	The indirection of the various registers means we have to spinlock
 *	nearly all register accesses.  We have the main register indirection
 *	like the wave cache, maestro registers, etc.  Then we have beasts
 *	like the APU interface that is indirect registers gotten at through
 *	the main maestro indirection.  Ouch.  We spinlock around the actual
 *	ports on a per card basis.  This means spinlock activity at each IO
 *	operation, but the only IO operation clusters are in non critical 
 *	paths and it makes the code far easier to follow.  Interrupts are
 *	blocked while holding the locks because the int handler has to
 *	get at some of them :(.  The mixer interface doesn't, however.
 *	We also have an OSS state lock that is thrown around in a few
 *	places.
 *
 *	This driver has brute force APM suspend support.  We catch suspend
 *	notifications and stop all work being done on the chip.  Any people
 *	that try between this shutdown and the real suspend operation will
 *	be put to sleep.  When we resume we restore our software state on
 *	the chip and wake up the people that were using it.  The code thats
 *	being used now is quite dirty and assumes we're on a uni-processor
 *	machine.  Much of it will need to be cleaned up for SMP ACPI or 
 *	similar.
 *	
 * History
 *  v0.10 - Oct 28 1999 - Zach Brown <zab@redhat.com>
 *	aha, so, sometimes the WP writes a status word to offset 0
 *	  from one of the PCMBARs.  rearrange allocation accordingly..
 *	Jeroen Hoogervorst submits 7500 fix out of nowhere.  yay.  :)
 *  v0.09 - Oct 23 1999 - Zach Brown <zab@redhat.com>
 *	added APM support.
 *	re-order something such that some 2Es now work.  Magic!
 *	new codec reset routine.  made some codecs come to life.
 *	fix clear_advance, sync some control with ESS.
 *	now write to all base regs to be paranoid.
 *  v0.08 - Oct 20 1999 - Zach Brown <zab@redhat.com>
 *	Fix initial buflen bug.  I am so smart.  also smp compiling..
 *	I owe Eric yet another beer: fixed recmask, igain, 
 *	  muting, and adc sync consistency.  Go Team.
 *  v0.07 - Oct 4 1999 - Zach Brown <zab@redhat.com>
 *	tweak adc/dac, formating, and stuff to allow full duplex
 *	allocate dsps memory at open() so we can fit in the wavecache window
 *	fix wavecache braindamage.  again.  no more scribbling?
 *	fix ess 1921 codec bug on some laptops.
 *	fix dumb pci scanning bug
 *	started 2.3 cleanup, redid spinlocks, little cleanups
 *  v0.06 - Sep 20 1999 - Zach Brown <zab@redhat.com>
 *	fix wavecache thinkos.  limit to 1 /dev/dsp.
 *	eric is wearing his thinking toque this week.
 *		spotted apu mode bugs and gain ramping problem
 *	don't touch weird mixer regs, make recmask optional
 *	fixed igain inversion, defaults for mixers, clean up rec_start
 *	make mono recording work.
 *	report subsystem stuff, please send reports.
 *	littles: parallel out, amp now
 *  v0.05 - Sep 17 1999 - Zach Brown <zab@redhat.com>
 *	merged and fixed up Eric's initial recording code
 *	munged format handling to catch misuse, needs rewrite.
 *	revert ring bus init, fixup shared int, add pci busmaster setting
 *	fix mixer oss interface, fix mic mute and recmask
 *	mask off unsupported mixers, reset with all 1s, modularize defaults
 *	make sure bob is running while we need it
 *	got rid of device limit, initial minimal apm hooks
 *	pull out dead code/includes, only allow multimedia/audio maestros
 *  v0.04 - Sep 01 1999 - Zach Brown <zab@redhat.com>
 *	copied memory leak fix from sonicvibes driver
 *	different ac97 reset, play with 2.0 ac97, simplify ring bus setup
 *	bob freq code, region sanity, jitter sync fix; all from Eric 
 *
 * TODO
 *	some people get indir reg timeouts?
 *	anyone have a pt101 codec?
 *	mmap(), but beware stereo encoding nastiness.
 *	actually post pci writes
 *	fix bob frequency
 *	do smart things with ac97 2.0 bits.
 *	ugh.. non aligned writes in the middle of a data stream.. ugh
 *	sort out 0x34->0x36 crap in init
 *	docking and dual codecs and 978?
 *	pcm_sync?
 *	actually use LRLR
 *
 *	it also would be fun to have a mode that would not use pci dma at all
 *	but would copy into the wavecache on board memory and use that 
 *	on architectures that don't like the maestro's pci dma ickiness.
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/version.h>
#include <linux/module.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,3,0)

 #define DECLARE_WAITQUEUE(QUEUE,INIT) struct wait_queue QUEUE = {INIT, NULL}
 #define wait_queue_head_t struct wait_queue *
 #define SILLY_PCI_BASE_ADDRESS(PCIDEV) (PCIDEV->base_address[0] & PCI_BASE_ADDRESS_IO_MASK)
 #define SILLY_INIT_SEM(SEM) SEM=MUTEX;
 #define init_waitqueue_head init_waitqueue
 #define SILLY_MAKE_INIT(FUNC) __initfunc(FUNC)

#else

 #define SILLY_PCI_BASE_ADDRESS(PCIDEV) (PCIDEV->resource[0].start)
 #define SILLY_INIT_SEM(SEM) init_MUTEX(&SEM)
 #define SILLY_MAKE_INIT(FUNC) __init FUNC

#endif

#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/sound.h>
#include <linux/malloc.h>
#include <linux/soundcard.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <asm/hardirq.h>

#ifdef CONFIG_APM
#include <linux/apm_bios.h>
static int maestro_apm_callback(apm_event_t ae);
static int in_suspend=0;
wait_queue_head_t suspend_queue;
static void check_suspend(void);
#define CHECK_SUSPEND check_suspend();
#else
#define CHECK_SUSPEND
#endif

#include "maestro.h"

/* --------------------------------------------------------------------- */

#define M_DEBUG 1

#ifdef M_DEBUG
static int debug=0;
static int dsps_order=0;
#define M_printk(args...) {if (debug) printk(args);}
#else
#define M_printk(x)
#endif

/* --------------------------------------------------------------------- */
#define DRIVER_VERSION "0.10"

#ifndef PCI_VENDOR_ESS
#define PCI_VENDOR_ESS			0x125D
#define PCI_DEVICE_ID_ESS_ESS1968	0x1968		/* Maestro 2	*/
#define PCI_DEVICE_ID_ESS_ESS1978      	0x1978		/* Maestro 2E	*/

#define PCI_VENDOR_ESS_OLD		0x1285		/* Platform Tech, 
						the people the maestro 
						was bought from */
#define PCI_DEVICE_ID_ESS_ESS0100	0x0100		/* maestro 1 */
#endif /* PCI_VENDOR_ESS */

#define ESS_CHAN_HARD		0x100


/* changed so that I could actually find all the
	references and fix them up.  its a little more readable now. */
#define ESS_FMT_STEREO	0x01
#define ESS_FMT_16BIT	0x02
#define ESS_FMT_MASK	0x03
#define ESS_DAC_SHIFT	0   
#define ESS_ADC_SHIFT	4

#define ESS_STATE_MAGIC		0x125D1968
#define ESS_CARD_MAGIC		0x19283746

#define DAC_RUNNING		1
#define ADC_RUNNING		2

#define MAX_DSP_ORDER	3
#define MAX_DSPS	(1<<3)
#define NR_DSPS		(1<<dsps_order)
#define NR_IDRS		32

#define SND_DEV_DSP16   5 

#define NR_APUS		64
#define NR_APU_REGS	16

static const unsigned sample_size[] = { 1, 2, 2, 4 };
static const unsigned sample_shift[] = { 0, 1, 1, 2 };

enum card_types_t {
	TYPE_MAESTRO,
	TYPE_MAESTRO2,
	TYPE_MAESTRO2E
};

static const char *card_names[]={
	[TYPE_MAESTRO] = "ESS Maestro",
	[TYPE_MAESTRO2] = "ESS Maestro 2",
	[TYPE_MAESTRO2E] = "ESS Maestro 2E"
};


/* --------------------------------------------------------------------- */

struct ess_state {
	unsigned int magic;
	/* FIXME: we probably want submixers in here, but only one record pair */
	u8 apu[6];		/* l/r output, l/r intput converters, l/r input apus */
	u8 apu_mode[6];		/* Running mode for this APU */
	u8 apu_pan[6];		/* Panning setup for this APU */
	u32 apu_base[6];	/* base address for this apu */
	struct ess_card *card;	/* Card info */
	/* wave stuff */
	unsigned int rateadc, ratedac;
	unsigned char fmt, enable;

	int index;

	/* this locks around the oss state in the driver */
	spinlock_t lock;
	/* only let 1 be opening at a time */
	struct semaphore open_sem;
	wait_queue_head_t open_wait;
	mode_t open_mode;

	/* soundcore stuff */
	int dev_audio;

	struct dmabuf {
		void *rawbuf;
		unsigned buforder;
		unsigned numfrag;
		unsigned fragshift;
		/* XXX zab - swptr only in here so that it can be referenced by
			clear_advance, as far as I can tell :( */
		unsigned hwptr, swptr;
		unsigned total_bytes;
		int count;
		unsigned error; /* over/underrun */
		wait_queue_head_t wait;
		/* redundant, but makes calculations easier */
		unsigned fragsize;
		unsigned dmasize;
		unsigned fragsamples;
		/* OSS stuff */
		unsigned mapped:1;
		unsigned ready:1;	/* our oss buffers are ready to go */
		unsigned endcleared:1;
		unsigned ossfragshift;
		int ossmaxfrags;
		unsigned subdivision;
		u16 base;		/* Offset for ptr */
	} dma_dac, dma_adc;

	/* pointer to each dsp?s piece of the apu->src buffer page */
	void *mixbuf;
};
	
struct ess_card {
	unsigned int magic;

	/* We keep maestro cards in a linked list */
	struct ess_card *next;

	int dev_mixer;

	int card_type;

	/* as most of this is static,
		perhaps it should be a pointer to a global struct */
	struct mixer_goo {
		int modcnt;
		int supported_mixers;
		int stereo_mixers;
		int record_sources;
		/* the caller must guarantee arg sanity before calling these */
/*		int (*read_mixer)(struct ess_card *card, int index);*/
		void (*write_mixer)(struct ess_card *card,int mixer, unsigned int left,unsigned int right);
		int (*recmask_io)(struct ess_card *card,int rw,int mask);
		unsigned int mixer_state[SOUND_MIXER_NRDEVICES];
	} mix;
	
	struct ess_state channels[MAX_DSPS];
	u16 maestro_map[NR_IDRS];	/* Register map */
#ifdef CONFIG_APM
	/* we have to store this junk so that we can come back from a
		suspend */
	u16 apu_map[NR_APUS][NR_APU_REGS];	/* contents of apu regs */
#endif

	/* this locks around the physical registers on the card */
	spinlock_t lock;

	/* memory for this card.. wavecache limited :(*/
	void *dmapages;
	int dmaorder;

	/* hardware resources */
	struct pci_dev pcidev; /* uck.. */
	u32 iobase;
	u32 irq;

	int bob_freq;
	char dsps_open;
};

static unsigned 
ld2(unsigned int x)
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

static struct ess_card *devs = NULL;

/* --------------------------------------------------------------------- */


/*
 *	ESS Maestro AC97 codec programming interface.
 */
	 
static void maestro_ac97_set(int io, u8 cmd, u16 val)
{
	int i;
	/*
	 *	Wait for the codec bus to be free 
	 */

	CHECK_SUSPEND;
	 
	for(i=0;i<10000;i++)
	{
		if(!(inb(io+ESS_AC97_INDEX)&1)) 
			break;
	}
	/*
	 *	Write the bus
	 */ 
	outw(val, io+ESS_AC97_DATA);
	mdelay(1);
	outb(cmd, io+ESS_AC97_INDEX);
	mdelay(1);
}

static u16 maestro_ac97_get(int io, u8 cmd)
{
	int sanity=10000;
	u16 data;
	int i;
	
	CHECK_SUSPEND;
	/*
	 *	Wait for the codec bus to be free 
	 */
	 
	for(i=0;i<10000;i++)
	{
		if(!(inb(io+ESS_AC97_INDEX)&1))
			break;
	}

	outb(cmd|0x80, io+ESS_AC97_INDEX);
	mdelay(1);
	
	while(inb(io+ESS_AC97_INDEX)&1)
	{
		sanity--;
		if(!sanity)
		{
			printk(KERN_ERR "maestro: ac97 codec timeout reading 0x%x.\n",cmd);
			return 0;
		}
	}
	data=inw(io+ESS_AC97_DATA);
	mdelay(1);
	return data;
}

/* OSS interface to the ac97s.. */

#define AC97_STEREO_MASK (SOUND_MASK_VOLUME|\
	SOUND_MASK_PCM|SOUND_MASK_LINE|SOUND_MASK_CD|\
	SOUND_MASK_VIDEO|SOUND_MASK_LINE1|SOUND_MASK_IGAIN)

#define AC97_SUPPORTED_MASK (AC97_STEREO_MASK | \
	SOUND_MASK_BASS|SOUND_MASK_TREBLE|SOUND_MASK_MIC|\
	SOUND_MASK_SPEAKER)

#define AC97_RECORD_MASK (SOUND_MASK_MIC|\
	SOUND_MASK_CD| SOUND_MASK_VIDEO| SOUND_MASK_LINE1| SOUND_MASK_LINE|\
	SOUND_MASK_PHONEIN)

#define supported_mixer(CARD,FOO) ( CARD->mix.supported_mixers & (1<<FOO) )

/* this table has default mixer values for all OSS mixers.
	be sure to fill it in if you add oss mixers
	to anyone's supported mixer defines */

 unsigned int mixer_defaults[SOUND_MIXER_NRDEVICES] = {
	[SOUND_MIXER_VOLUME] =          0x3232,
	[SOUND_MIXER_BASS] =            0x3232,
	[SOUND_MIXER_TREBLE] =          0x3232,
	[SOUND_MIXER_SPEAKER] =         0x3232,
	[SOUND_MIXER_MIC] =     0x3232,
	[SOUND_MIXER_LINE] =    0x3232,
	[SOUND_MIXER_CD] =      0x3232,
	[SOUND_MIXER_VIDEO] =   0x3232,
	[SOUND_MIXER_LINE1] =   0x3232,
	[SOUND_MIXER_PCM] =             0x3232,
	[SOUND_MIXER_IGAIN] =           0x3232
};
	
static struct ac97_mixer_hw {
	unsigned char offset;
	int scale;
} ac97_hw[SOUND_MIXER_NRDEVICES]= {
	[SOUND_MIXER_VOLUME]	=	{0x02,63},
	[SOUND_MIXER_BASS]	=	{0x08,15},
	[SOUND_MIXER_TREBLE]	=	{0x08,15},
	[SOUND_MIXER_SPEAKER]	=	{0x0a,15},
	[SOUND_MIXER_MIC]	=	{0x0e,31},
	[SOUND_MIXER_LINE]	=	{0x10,31},
	[SOUND_MIXER_CD]	=	{0x12,31},
	[SOUND_MIXER_VIDEO]	=	{0x14,31},
	[SOUND_MIXER_LINE1]	=	{0x16,31},
	[SOUND_MIXER_PCM]	=	{0x18,31},
	[SOUND_MIXER_IGAIN]	=	{0x1c,15}
};

#if 0 /* *shrug* removed simply because we never used it.
		feel free to implement again if needed */

/* reads the given OSS mixer from the ac97
	the caller must have insured that the ac97 knows
	about that given mixer, and should be holding a
	spinlock for the card */
static int ac97_read_mixer(struct ess_card *card, int mixer) 
{
	u16 val;
	int ret=0;
	struct ac97_mixer_hw *mh = &ac97_hw[mixer];

	val = maestro_ac97_get(card->iobase , mh->offset);

	if(AC97_STEREO_MASK & (1<<mixer)) {
		/* nice stereo mixers .. */
		int left,right;

		left = (val >> 8)  & 0x7f;
		right = val  & 0x7f;

		if (mixer == SOUND_MIXER_IGAIN) {
			right = (right * 100) / mh->scale;
			left = (left * 100) / mh->scale;
		else {
			right = 100 - ((right * 100) / mh->scale);
			left = 100 - ((left * 100) / mh->scale);
		}

		ret = left | (right << 8);
	} else if (mixer == SOUND_MIXER_SPEAKER) {
		ret = 100 - ((((val & 0x1e)>>1) * 100) / mh->scale);
	} else if (mixer == SOUND_MIXER_MIC) {
		ret = 100 - (((val & 0x1f) * 100) / mh->scale);
	/*  the low bit is optional in the tone sliders and masking
		it lets is avoid the 0xf 'bypass'.. */
	} else if (mixer == SOUND_MIXER_BASS) {
		ret = 100 - ((((val >> 8) & 0xe) * 100) / mh->scale);
	} else if (mixer == SOUND_MIXER_TREBLE) {
		ret = 100 - (((val & 0xe) * 100) / mh->scale);
	}

	M_printk("read mixer %d (0x%x) %x -> %x\n",mixer,mh->offset,val,ret);

	return ret;
}
#endif

/* write the OSS encoded volume to the given OSS encoded mixer,
	again caller's job to make sure all is well in arg land,
	call with spinlock held */
static void ac97_write_mixer(struct ess_card *card,int mixer, unsigned int left, unsigned int right)
{
	u16 val=0;
	struct ac97_mixer_hw *mh = &ac97_hw[mixer];

	M_printk("wrote mixer %d (0x%x) %d,%d",mixer,mh->offset,left,right);

	if(AC97_STEREO_MASK & (1<<mixer)) {
		/* stereo mixers, mute them if we can */

		if (mixer == SOUND_MIXER_IGAIN) {
			/* igain's slider is reversed.. */
			right = (right * mh->scale) / 100;
			left = (left * mh->scale) / 100;
			if ((left == 0) && (right == 0))
				val |= 0x8000; 
		} else {
			right = ((100 - right) * mh->scale) / 100;
			left = ((100 - left) * mh->scale) / 100;
			if((left == mh->scale) && (right == mh->scale))
				val |= 0x8000;
		}

		val |= (left << 8) | right;

	} else if (mixer == SOUND_MIXER_SPEAKER) {
		val = (((100 - left) * mh->scale) / 100) << 1;
	} else if (mixer == SOUND_MIXER_MIC) {
		val = maestro_ac97_get(card->iobase , mh->offset) & ~0x801f;
		val |= (((100 - left) * mh->scale) / 100);
	/*  the low bit is optional in the tone sliders and masking
		it lets is avoid the 0xf 'bypass'.. */
	} else if (mixer == SOUND_MIXER_BASS) {
		val = maestro_ac97_get(card->iobase , mh->offset) & ~0x0f00;
		val |= ((((100 - left) * mh->scale) / 100) << 8) & 0x0e00;
	} else if (mixer == SOUND_MIXER_TREBLE)  {
		val = maestro_ac97_get(card->iobase , mh->offset) & ~0x000f;
		val |= (((100 - left) * mh->scale) / 100) & 0x000e;
	}

	maestro_ac97_set(card->iobase , mh->offset, val);
	
	M_printk(" -> %x\n",val);
}

/* the following tables allow us to go from 
	OSS <-> ac97 quickly. */

enum ac97_recsettings {
	AC97_REC_MIC=0,
	AC97_REC_CD,
	AC97_REC_VIDEO,
	AC97_REC_AUX,
	AC97_REC_LINE,
	AC97_REC_STEREO, /* combination of all enabled outputs..  */
	AC97_REC_MONO,        /*.. or the mono equivalent */
	AC97_REC_PHONE        
};

static unsigned int ac97_oss_mask[] = {
	[AC97_REC_MIC] = SOUND_MASK_MIC, 
	[AC97_REC_CD] = SOUND_MASK_CD, 
	[AC97_REC_VIDEO] = SOUND_MASK_VIDEO, 
	[AC97_REC_AUX] = SOUND_MASK_LINE1, 
	[AC97_REC_LINE] = SOUND_MASK_LINE, 
	[AC97_REC_PHONE] = SOUND_MASK_PHONEIN
};

/* indexed by bit position */
static unsigned int ac97_oss_rm[] = {
	[SOUND_MIXER_MIC] = AC97_REC_MIC,
	[SOUND_MIXER_CD] = AC97_REC_CD,
	[SOUND_MIXER_VIDEO] = AC97_REC_VIDEO,
	[SOUND_MIXER_LINE1] = AC97_REC_AUX,
	[SOUND_MIXER_LINE] = AC97_REC_LINE,
	[SOUND_MIXER_PHONEIN] = AC97_REC_PHONE
};
	
/* read or write the recmask 
	the ac97 can really have left and right recording
	inputs independantly set, but OSS doesn't seem to 
	want us to express that to the user. 
	the caller guarantees that we have a supported bit set,
	and they must be holding the card's spinlock */
static int 
ac97_recmask_io(struct ess_card *card, int read, int mask) 
{
	unsigned int val = ac97_oss_mask[ maestro_ac97_get(card->iobase, 0x1a) & 0x7 ];

	if (read) return val;

	/* oss can have many inputs, maestro cant.  try
		to pick the 'new' one */

	if (mask != val) mask &= ~val;

	val = ffs(mask) - 1; 
	val = ac97_oss_rm[val];
	val |= val << 8;  /* set both channels */

	M_printk("maestro: setting ac97 recmask to 0x%x\n",val);

	maestro_ac97_set(card->iobase,0x1a,val);

	return 0;
};

/*
 *	The Maestro can be wired to a standard AC97 compliant codec
 *	(see www.intel.com for the pdf's on this), or to a PT101 codec
 *	which appears to be the ES1918 (data sheet on the esstech.com.tw site)
 *
 *	The PT101 setup is untested.
 */
 
static u16 maestro_ac97_init(struct ess_card *card, int iobase)
{
	u16 vend1, vend2, caps;

	card->mix.supported_mixers = AC97_SUPPORTED_MASK;
	card->mix.stereo_mixers = AC97_STEREO_MASK;
	card->mix.record_sources = AC97_RECORD_MASK;
/*	card->mix.read_mixer = ac97_read_mixer;*/
	card->mix.write_mixer = ac97_write_mixer;
	card->mix.recmask_io = ac97_recmask_io;

	vend1 = maestro_ac97_get(iobase, 0x7c);
	vend2 = maestro_ac97_get(iobase, 0x7e);

	caps = maestro_ac97_get(iobase, 0x00);

	printk(KERN_INFO "maestro: AC97 Codec detected: v: 0x%2x%2x caps: 0x%x pwr: 0x%x\n",
		vend1,vend2,caps,maestro_ac97_get(iobase,0x26) & 0xf);

	if (! (caps & 0x4) ) {
		/* no bass/treble nobs */
		card->mix.supported_mixers &= ~(SOUND_MASK_BASS|SOUND_MASK_TREBLE);
	}

	/* XXX endianness, dork head. */
	/* vendor specifc bits.. */
	switch ((long)(vend1 << 16) | vend2) {
	case 0x545200ff:	/* TriTech */
		/* no idea what this does */
		maestro_ac97_set(iobase,0x2a,0x0001);
		maestro_ac97_set(iobase,0x2c,0x0000);
		maestro_ac97_set(iobase,0x2c,0xffff);
		break;
	case 0x83847609:	/* ESS 1921 */
		/* writing to 0xe (mic) or 0x1a (recmask) seems
			to hang this codec */
		card->mix.supported_mixers &= ~(SOUND_MASK_MIC);
		card->mix.record_sources = 0;
		card->mix.recmask_io = NULL;
#if 0	/* don't ask.  I have yet to see what these actually do. */
		maestro_ac97_set(iobase,0x76,0xABBA); /* o/~ Take a chance on me o/~ */
		udelay(20);
		maestro_ac97_set(iobase,0x78,0x3002);
		udelay(20);
		maestro_ac97_set(iobase,0x78,0x3802);
		udelay(20);
#endif
		break;
	default: break;
	}

	maestro_ac97_set(iobase, 0x1E, 0x0404);
	/* null misc stuff */
	maestro_ac97_set(iobase, 0x20, 0x0000);

	return 0;
}

static u16 maestro_pt101_init(struct ess_card *card,int iobase)
{
	printk(KERN_INFO "maestro: PT101 Codec detected, initializing but _not_ installing mixer device.\n");
	/* who knows.. */
	maestro_ac97_set(iobase, 0x2A, 0x0001);
	maestro_ac97_set(iobase, 0x2C, 0x0000);
	maestro_ac97_set(iobase, 0x2C, 0xFFFF);
	maestro_ac97_set(iobase, 0x10, 0x9F1F);
	maestro_ac97_set(iobase, 0x12, 0x0808);
	maestro_ac97_set(iobase, 0x14, 0x9F1F);
	maestro_ac97_set(iobase, 0x16, 0x9F1F);
	maestro_ac97_set(iobase, 0x18, 0x0404);
	maestro_ac97_set(iobase, 0x1A, 0x0000);
	maestro_ac97_set(iobase, 0x1C, 0x0000);
	maestro_ac97_set(iobase, 0x02, 0x0404);
	maestro_ac97_set(iobase, 0x04, 0x0808);
	maestro_ac97_set(iobase, 0x0C, 0x801F);
	maestro_ac97_set(iobase, 0x0E, 0x801F);
	return 0;
}

/* this is very magic, and very slow.. */
static void 
maestro_ac97_reset(int ioaddr, struct pci_dev *pcidev)
{
	u16 save_68;
	u16 w;

	outw( inw(ioaddr + 0x38) & 0xfffc, ioaddr + 0x38);
	outw( inw(ioaddr + 0x3a) & 0xfffc, ioaddr + 0x3a);
	outw( inw(ioaddr + 0x3c) & 0xfffc, ioaddr + 0x3c);

	/* reset the first codec */
	outw(0x0000,  ioaddr+0x36);
	save_68 = inw(ioaddr+0x68);
	pci_read_config_word(pcidev, 0x58, &w);	/* something magical with gpio and bus arb. */
	if( w & 0x1)
		save_68 |= 0x10;
	outw(0xfffe, ioaddr + 0x64);	/* tickly gpio 0.. */
	outw(0x0001, ioaddr + 0x68);
	outw(0x0000, ioaddr + 0x60);
	udelay(20);
	outw(0x0001, ioaddr + 0x60);
	mdelay(20);

	outw(save_68 | 0x1, ioaddr + 0x68);	/* now restore .. */
	outw( (inw(ioaddr + 0x38) & 0xfffc)|0x1, ioaddr + 0x38);
	outw( (inw(ioaddr + 0x3a) & 0xfffc)|0x1, ioaddr + 0x3a);
	outw( (inw(ioaddr + 0x3c) & 0xfffc)|0x1, ioaddr + 0x3c);

	/* now the second codec */
	outw(0x0000,  ioaddr+0x36);
	outw(0xfff7, ioaddr + 0x64);
	save_68 = inw(ioaddr+0x68);
	outw(0x0009, ioaddr + 0x68);
	outw(0x0001, ioaddr + 0x60);
	udelay(20);
	outw(0x0009, ioaddr + 0x60);
	mdelay(500);	/* .. ouch.. */
	outw( inw(ioaddr + 0x38) & 0xfffc, ioaddr + 0x38);
	outw( inw(ioaddr + 0x3a) & 0xfffc, ioaddr + 0x3a);
	outw( inw(ioaddr + 0x3c) & 0xfffc, ioaddr + 0x3c);

#if 0 /* the loop here needs to be much better if we want it.. */
	M_printk("trying software reset\n");
	/* try and do a software reset */
	outb(0x80|0x7c, ioaddr + 0x30);
	for (w=0; ; w++) {
		if ((inw(ioaddr+ 0x30) & 1) == 0) {
			if(inb(ioaddr + 0x32) !=0) break;

			outb(0x80|0x7d, ioaddr + 0x30);
			if (((inw(ioaddr+ 0x30) & 1) == 0) && (inb(ioaddr + 0x32) !=0)) break;
			outb(0x80|0x7f, ioaddr + 0x30);
			if (((inw(ioaddr+ 0x30) & 1) == 0) && (inb(ioaddr + 0x32) !=0)) break;
		}

		if( w > 10000) {
			outb( inb(ioaddr + 0x37) | 0x08, ioaddr + 0x37);  /* do a software reset */
			mdelay(500); /* oh my.. */
			outb( inb(ioaddr + 0x37) & ~0x08, ioaddr + 0x37);  
			udelay(1);
			outw( 0x80, ioaddr+0x30);
			for(w = 0 ; w < 10000; w++) {
				if((inw(ioaddr + 0x30) & 1) ==0) break;
			}
		}
	}
#endif
}
/*
 *	Indirect register access. Not all registers are readable so we
 *	need to keep register state ourselves
 */
 
#define WRITEABLE_MAP	0xEFFFFF
#define READABLE_MAP	0x64003F

/*
 *	The Maestro engineers were a little indirection happy. These indirected
 *	registers themselves include indirect registers at another layer
 */

static void __maestro_write(struct ess_card *card, u16 reg, u16 data)
{
	long ioaddr = card->iobase;

	outw(reg, ioaddr+0x02);
	outw(data, ioaddr+0x00);
	if( reg >= NR_IDRS) printk("maestro: IDR %d out of bounds!\n",reg);
	else card->maestro_map[reg]=data;

}
 
static void maestro_write(struct ess_state *s, u16 reg, u16 data)
{
	unsigned long flags;

	CHECK_SUSPEND;
	spin_lock_irqsave(&s->card->lock,flags);

	__maestro_write(s->card,reg,data);

	spin_unlock_irqrestore(&s->card->lock,flags);
}

static u16 __maestro_read(struct ess_card *card, u16 reg)
{
	long ioaddr = card->iobase;

	outw(reg, ioaddr+0x02);
	return card->maestro_map[reg]=inw(ioaddr+0x00);
}

static u16 maestro_read(struct ess_state *s, u16 reg)
{
	if(READABLE_MAP & (1<<reg))
	{
		unsigned long flags;
		CHECK_SUSPEND;
		spin_lock_irqsave(&s->card->lock,flags);

		__maestro_read(s->card,reg);

		spin_unlock_irqrestore(&s->card->lock,flags);
	}
	return s->card->maestro_map[reg];
}

/*
 *	These routines handle accessing the second level indirections to the
 *	wave ram.
 */

/*
 *	The register names are the ones ESS uses (see 104T31.ZIP)
 */
 
#define IDR0_DATA_PORT		0x00
#define IDR1_CRAM_POINTER	0x01
#define IDR2_CRAM_DATA		0x02
#define IDR3_WAVE_DATA		0x03
#define IDR4_WAVE_PTR_LOW	0x04
#define IDR5_WAVE_PTR_HI	0x05
#define IDR6_TIMER_CTRL		0x06
#define IDR7_WAVE_ROMRAM	0x07

static void apu_index_set(struct ess_card *card, u16 index)
{
	int i;
	__maestro_write(card, IDR1_CRAM_POINTER, index);
	for(i=0;i<1000;i++)
		if(__maestro_read(card, IDR1_CRAM_POINTER)==index)
			return;
	printk(KERN_WARNING "maestro: APU register select failed.\n");
}

static void apu_data_set(struct ess_card *card, u16 data)
{
	int i;
	for(i=0;i<1000;i++)
	{
		if(__maestro_read(card, IDR0_DATA_PORT)==data)
			return;
		__maestro_write(card, IDR0_DATA_PORT, data);
	}
}

/*
 *	This is the public interface for APU manipulation. It handles the
 *	interlock to avoid two APU writes in parallel etc. Don't diddle
 *	directly with the stuff above.
 */

static void apu_set_register(struct ess_state *s, u16 channel, u8 reg, u16 data)
{
	unsigned long flags;
	
	CHECK_SUSPEND;

	if(channel&ESS_CHAN_HARD)
		channel&=~ESS_CHAN_HARD;
	else
	{
		if(channel>5)
			printk("BAD CHANNEL %d.\n",channel);
		else
			channel = s->apu[channel];
#ifdef	CONFIG_APM
		/* store based on real hardware apu/reg */
		s->card->apu_map[channel][reg]=data;
#endif
	}
	reg|=(channel<<4);
	
	/* hooray for double indirection!! */
	spin_lock_irqsave(&s->card->lock,flags);

	apu_index_set(s->card, reg);
	apu_data_set(s->card, data);

	spin_unlock_irqrestore(&s->card->lock,flags);
}

static u16 apu_get_register(struct ess_state *s, u16 channel, u8 reg)
{
	unsigned long flags;
	u16 v;
	
	CHECK_SUSPEND;

	if(channel&ESS_CHAN_HARD)
		channel&=~ESS_CHAN_HARD;
	else
		channel = s->apu[channel];

	reg|=(channel<<4);
	
	spin_lock_irqsave(&s->card->lock,flags);

	apu_index_set(s->card, reg);
	v=__maestro_read(s->card, IDR0_DATA_PORT);

	spin_unlock_irqrestore(&s->card->lock,flags);
	return v;
}


/*
 *	The wavecache buffers between the APUs and
 *	pci bus mastering
 */
 
static void wave_set_register(struct ess_state *s, u16 reg, u16 value)
{
	long ioaddr = s->card->iobase;
	unsigned long flags;
	CHECK_SUSPEND;
	
	spin_lock_irqsave(&s->card->lock,flags);

	outw(reg, ioaddr+0x10);
	outw(value, ioaddr+0x12);

	spin_unlock_irqrestore(&s->card->lock,flags);
}

static u16 wave_get_register(struct ess_state *s, u16 reg)
{
	long ioaddr = s->card->iobase;
	unsigned long flags;
	u16 value;
	CHECK_SUSPEND;
	
	spin_lock_irqsave(&s->card->lock,flags);
	outw(reg, ioaddr+0x10);
	value=inw(ioaddr+0x12);
	spin_unlock_irqrestore(&s->card->lock,flags);
	
	return value;
}

static void sound_reset(int ioaddr)
{
	outw(0x2000, 0x18+ioaddr);
	udelay(1);
	outw(0x0000, 0x18+ioaddr);
	udelay(1);
}

/* sets the play formats of these apus, should be passed the already shifted format */
static void set_apu_fmt(struct ess_state *s, int apu, int mode)
{
	if(mode&ESS_FMT_16BIT) { 
		s->apu_mode[apu] = 0x10;
		s->apu_mode[apu+1] = 0x10;
	} else {
		s->apu_mode[apu] = 0x30;
		s->apu_mode[apu+1] = 0x30;
	}
}

/* this only fixes the output apu mode to be later set by start_dac and
	company.  output apu modes are set in ess_rec_setup */
static void set_fmt(struct ess_state *s, unsigned char mask, unsigned char data)
{
	s->fmt = (s->fmt & mask) | data;
	set_apu_fmt(s, 0, (s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK);
}

static u16 compute_rate(u32 freq)
{
	if(freq==48000)
		return 0xFFFF;
	freq<<=16;
	freq/=48000;
	return freq;
}

static void set_dac_rate(struct ess_state *s, unsigned rate)
{
	u32 freq;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	s->ratedac = rate;

	if(!((s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_16BIT))
		rate >>= 1; /* who knows */

/*	M_printk("computing dac rate %d with mode %d\n",rate,s->fmt);*/

	freq = compute_rate(rate);
	
	/* Load the frequency, turn on 6dB */
	apu_set_register(s, 0, 2,(apu_get_register(s, 0, 2)&0x00FF)|
		( ((freq&0xFF)<<8)|0x10 ));
	apu_set_register(s, 0, 3, freq>>8);
	apu_set_register(s, 1, 2,(apu_get_register(s, 1, 2)&0x00FF)|
		( ((freq&0xFF)<<8)|0x10 ));
	apu_set_register(s, 1, 3, freq>>8);
}

static void set_adc_rate(struct ess_state *s, unsigned rate)
{
	u32 freq;

	if (rate > 48000)
		rate = 48000;
	if (rate < 4000)
		rate = 4000;

	s->rateadc = rate;

	freq = compute_rate(rate);
	
	/* Load the frequency, turn on 6dB */
	apu_set_register(s, 2, 2,(apu_get_register(s, 2, 2)&0x00FF)|
		( ((freq&0xFF)<<8)|0x10 ));
	apu_set_register(s, 2, 3, freq>>8);
	apu_set_register(s, 3, 2,(apu_get_register(s, 3, 2)&0x00FF)|
		( ((freq&0xFF)<<8)|0x10 ));
	apu_set_register(s, 3, 3, freq>>8);

	/* fix mixer rate at 48khz.  and its _must_ be 0x10000. */
	freq = 0x10000;

	apu_set_register(s, 4, 2,(apu_get_register(s, 4, 2)&0x00FF)|
		( ((freq&0xFF)<<8)|0x10 ));
	apu_set_register(s, 4, 3, freq>>8);
	apu_set_register(s, 5, 2,(apu_get_register(s, 5, 2)&0x00FF)|
		( ((freq&0xFF)<<8)|0x10 ));
	apu_set_register(s, 5, 3, freq>>8);
}

/* Stop our host of recording apus */
extern inline void stop_adc(struct ess_state *s)
{
	/* XXX lets hope we don't have to lock around this */
	if (! (s->enable & ADC_RUNNING)) return;

	s->enable &= ~ADC_RUNNING;
	apu_set_register(s, 2, 0, apu_get_register(s, 2, 0)&0xFF0F);
	apu_set_register(s, 3, 0, apu_get_register(s, 3, 0)&0xFF0F);
	apu_set_register(s, 4, 0, apu_get_register(s, 2, 0)&0xFF0F);
	apu_set_register(s, 5, 0, apu_get_register(s, 3, 0)&0xFF0F);
}	

/* stop output apus */
extern inline void stop_dac(struct ess_state *s)
{
	/* XXX have to lock around this? */
	if (! (s->enable & DAC_RUNNING)) return;

	s->enable &= ~DAC_RUNNING;
	apu_set_register(s, 0, 0, apu_get_register(s, 0, 0)&0xFF0F);
	apu_set_register(s, 1, 0, apu_get_register(s, 1, 0)&0xFF0F);
}	

static void start_dac(struct ess_state *s)
{
	/* XXX locks? */
	if (	(s->dma_dac.mapped || s->dma_dac.count > 0) && 
		s->dma_dac.ready &&
		(! (s->enable & DAC_RUNNING)) ) {

		s->enable |= DAC_RUNNING;

		apu_set_register(s, 0, 0, 
			(apu_get_register(s, 0, 0)&0xFF0F)|s->apu_mode[0]);

		if((s->fmt >> ESS_DAC_SHIFT)  & ESS_FMT_STEREO) 
			apu_set_register(s, 1, 0, 
				(apu_get_register(s, 1, 0)&0xFF0F)|s->apu_mode[1]);
	}
}	

static void start_adc(struct ess_state *s)
{
	/* XXX locks? */
	if ((s->dma_adc.mapped || s->dma_adc.count < (signed)(s->dma_adc.dmasize - 2*s->dma_adc.fragsize)) 
	    && s->dma_adc.ready && (! (s->enable & ADC_RUNNING)) ) {

		s->enable |= ADC_RUNNING;
		apu_set_register(s, 2, 0, 
			(apu_get_register(s, 2, 0)&0xFF0F)|s->apu_mode[2]);
		apu_set_register(s, 4, 0, 
			(apu_get_register(s, 4, 0)&0xFF0F)|s->apu_mode[4]);

		if( s->fmt & (ESS_FMT_STEREO << ESS_ADC_SHIFT)) {
			apu_set_register(s, 3, 0, 
				(apu_get_register(s, 3, 0)&0xFF0F)|s->apu_mode[3]);
			apu_set_register(s, 5, 0, 
				(apu_get_register(s, 5, 0)&0xFF0F)|s->apu_mode[5]);
		}
			
	}
}	


/*
 *	Native play back driver 
 */

/* the mode passed should be already shifted and masked */
static void 
ess_play_setup(struct ess_state *ess, int mode, u32 rate, void *buffer, int size)
{
	u32 pa;
	u32 tmpval;
	int high_apu = 0;
	int channel;

	M_printk("mode=%d rate=%d buf=%p len=%d.\n",
		mode, rate, buffer, size);
		
	/* all maestro sizes are in 16bit words */
	size >>=1;

	/* we're given the full size of the buffer, but
	in stereo each channel will only play its half */
	if(mode&ESS_FMT_STEREO) {
		size >>=1; 
		high_apu++;
	}
	
	for(channel=0; channel <= high_apu; channel++)
	{
		int i;
		
		if(!channel) 
			pa = virt_to_bus(buffer);
		else
		/* right channel plays its split half.
			*2 accomodates for rampant shifting earlier */
			pa = virt_to_bus(buffer + size*2);

		/* set the wavecache control reg */
		tmpval = (pa - 0x10) & 0xFFF8;
		if(!(mode & 2)) tmpval |= 4; /* 8bit */ 
		ess->apu_base[channel]=tmpval;
		wave_set_register(ess, ess->apu[channel]<<3, tmpval);
		
		pa -= virt_to_bus(ess->card->dmapages);
		pa>>=1; /* words */
		
		/* base offset of dma calcs when reading the pointer
			on this left one */
		if(!channel) ess->dma_dac.base = pa&0xFFFF;
		
		pa|=0x00400000;			/* System RAM */
		
		/* Begin loading the APU */		
		for(i=0;i<15;i++)		/* clear all PBRs */
			apu_set_register(ess, channel, i, 0x0000);
			
/* XXX think about endianess when writing these registers */
		M_printk("maestro: ess_play_setup: APU[%d] pa = 0x%x\n", ess->apu[channel], pa);
		/* Load the buffer into the wave engine */
		apu_set_register(ess, channel, 4, ((pa>>16)&0xFF)<<8);
		apu_set_register(ess, channel, 5, pa&0xFFFF);
		apu_set_register(ess, channel, 6, (pa+size)&0xFFFF);
		/* setting loop == sample len */
		apu_set_register(ess, channel, 7, size);
		
		/* clear effects/env.. */
		apu_set_register(ess, channel, 8, 0x0000);
		/* set amp now to 0xd0 (?), low byte is 'amplitude dest'? */
		apu_set_register(ess, channel, 9, 0xD000);

		/* clear routing stuff */
		apu_set_register(ess, channel, 11, 0x0000);
		/* dma on, no envelopes, filter to all 1s) */
		apu_set_register(ess, channel, 0, 0x400F);
		
		if(mode&ESS_FMT_STEREO)
			/* set panning: left or right */
			apu_set_register(ess, channel, 10, 0x8F00 | (channel ? 0x10 : 0));
		else
			apu_set_register(ess, channel, 10, 0x8F08);

		if(mode&ESS_FMT_16BIT)
			ess->apu_mode[channel]=0x10;
		else
			ess->apu_mode[channel]=0x30;
	}
	
	/* clear WP interupts */
	outw(1, ess->card->iobase+0x04);
	/* enable WP ints */
	outw(inw(ess->card->iobase+0x18)|4, ess->card->iobase+0x18);

	/* go team! */
	set_dac_rate(ess,rate);
	start_dac(ess);
}

/*
 *	Native record driver 
 */

/* again, passed mode is alrady shifted/masked */
static void 
ess_rec_setup(struct ess_state *ess, int mode, u32 rate, void *buffer, int size)
{
	int apu_step = 2;
	int channel;

	M_printk("maestro: ess_rec_setup: mode=%d rate=%d buf=0x%p len=%d.\n",
		mode, rate, buffer, size);
		
	/* all maestro sizes are in 16bit words */
	size >>=1;

	/* we're given the full size of the buffer, but
	in stereo each channel will only use its half */
	if(mode&ESS_FMT_STEREO) {
		size >>=1; 
		apu_step = 1;
	}
	
	/* APU assignments: 2 = mono/left SRC
	                    3 = right SRC
	                    4 = mono/left Input Mixer
	                    5 = right Input Mixer */
	for(channel=2;channel<6;channel+=apu_step)
	{
		int i;
		int bsize, route;
		u32 pa;
		u32 tmpval;

		/* data seems to flow from the codec, through an apu into
			the 'mixbuf' bit of page, then through the SRC apu
			and out to the real 'buffer'.  ok.  sure.  */
		
		if(channel & 0x04) {
			/* ok, we're an input mixer going from adc
				through the mixbuf to the other apus */

			if(!(channel & 0x01)) { 
				pa = virt_to_bus(ess->mixbuf);
			} else {
				pa = virt_to_bus(ess->mixbuf + (PAGE_SIZE >> 4));
			}

			/* we source from a 'magic' apu */
			bsize = PAGE_SIZE >> 5;	/* half of this channels alloc, in words */
			route = 0x14 + (channel - 4); /* parallel in crap, see maestro reg 0xC [8-11] */
			ess->apu_mode[channel] = 0x90;  /* Input Mixer */

		} else {  
			/* we're a rate converter taking
				input from the input apus and outputing it to
				system memory */
			if(!(channel & 0x01))  {
				pa = virt_to_bus(buffer);
			} else {
				/* right channel records its split half.
				*2 accomodates for rampant shifting earlier */
				pa = virt_to_bus(buffer + size*2);
			}

			ess->apu_mode[channel] = 0xB0;  /* Sample Rate Converter */

			bsize = size; 
			/* get input from inputing apu */
			route = channel + 2;
		}

		M_printk("maestro: ess_rec_setup: getting pa 0x%x from %d\n",pa,channel);
		
		/* set the wavecache control reg */
		tmpval = (pa - 0x10) & 0xFFF8;
		ess->apu_base[channel]=tmpval;
		wave_set_register(ess, ess->apu[channel]<<3, tmpval);
		
		pa -= virt_to_bus(ess->card->dmapages);
		pa>>=1; /* words */
		
		/* base offset of dma calcs when reading the pointer
			on this left one */
		if(channel==2) ess->dma_adc.base = pa&0xFFFF;

		pa|=0x00400000;			/* bit 22 -> System RAM */

		M_printk("maestro: ess_rec_setup: APU[%d] pa = 0x%x size = 0x%x route = 0x%x\n", 
			ess->apu[channel], pa, bsize, route);
		
		/* Begin loading the APU */		
		for(i=0;i<15;i++)		/* clear all PBRs */
			apu_set_register(ess, channel, i, 0x0000);
			
		apu_set_register(ess, channel, 0, 0x400F);

		/* need to enable subgroups.. and we should probably
			have different groups for different /dev/dsps..  */
 		apu_set_register(ess, channel, 2, 0x8);
				
		/* Load the buffer into the wave engine */
		apu_set_register(ess, channel, 4, ((pa>>16)&0xFF)<<8);
		/* XXX reg is little endian.. */
		apu_set_register(ess, channel, 5, pa&0xFFFF);
		apu_set_register(ess, channel, 6, (pa+bsize)&0xFFFF);
		apu_set_register(ess, channel, 7, bsize);
				
		/* clear effects/env.. */
		apu_set_register(ess, channel, 8, 0x00F0);
		
		/* amplitude now?  sure.  why not.  */
		apu_set_register(ess, channel, 9, 0x0000);

		/* set filter tune, radius, polar pan */
		apu_set_register(ess, channel, 10, 0x8F08);

		/* route input */
		apu_set_register(ess, channel, 11, route);
	}
	
	/* clear WP interupts */
	outw(1, ess->card->iobase+0x04);
	/* enable WP ints */
	outw(inw(ess->card->iobase+0x18)|4, ess->card->iobase+0x18);

	/* let 'er rip */
	set_adc_rate(ess,rate);
	start_adc(ess);
}
/* --------------------------------------------------------------------- */

static void set_dmaa(struct ess_state *s, unsigned int addr, unsigned int count)
{
	M_printk("set_dmaa??\n");
}

static void set_dmac(struct ess_state *s, unsigned int addr, unsigned int count)
{
	M_printk("set_dmac??\n");
}

/* Playback pointer */
extern __inline__ unsigned get_dmaa(struct ess_state *s)
{
	int offset;

	offset = apu_get_register(s,0,5);

/*	M_printk("dmaa: offset: %d, base: %d\n",offset,s->dma_dac.base); */
	
	offset-=s->dma_dac.base;

	return (offset&0xFFFE)<<1; /* hardware is in words */
}

/* Record pointer */
extern __inline__ unsigned get_dmac(struct ess_state *s)
{
	int offset;

	offset = apu_get_register(s,2,5);

/*	M_printk("dmac: offset: %d, base: %d\n",offset,s->dma_adc.base); */
	
	/* The offset is an address not a position relative to base */
	offset-=s->dma_adc.base;
	
	return (offset&0xFFFE)<<1; /* hardware is in words */
}

/*
 *	Meet Bob, the timer...
 */

static void ess_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static void stop_bob(struct ess_state *s)
{
	/* Mask IDR 11,17 */
	maestro_write(s,  0x11, maestro_read(s, 0x11)&~1);
	maestro_write(s,  0x17, maestro_read(s, 0x17)&~1);
}

/* eventually we could be clever and limit bob ints
	to the frequency at which our smallest duration
	chunks may expire */
#define ESS_SYSCLK	50000000
static void start_bob(struct ess_state *s)
{
	int prescale;
	int divide;
	
	/* XXX make freq selector much smarter, see calc_bob_rate */
	int freq = 150;		/* requested frequency - calculate what we want here. */
	
	/* compute ideal interrupt frequency for buffer size & play rate */
	/* first, find best prescaler value to match freq */
	for(prescale=5;prescale<12;prescale++)
		if(freq > (ESS_SYSCLK>>(prescale+9)))
			break;
			
	/* next, back off prescaler whilst getting divider into optimum range */
	divide=1;
	while((prescale > 5) && (divide<32))
	{
		prescale--;
		divide <<=1;
	}
	divide>>=1;
	
	/* now fine-tune the divider for best match */
	for(;divide<31;divide++)
		if(freq >= ((ESS_SYSCLK>>(prescale+9))/(divide+1)))
			break;
	
	/* divide = 0 is illegal, but don't let prescale = 4! */
	if(divide == 0)
	{
		divide++;
		if(prescale>5)
			prescale--;
	}

	maestro_write(s, 6, 0x9000 | (prescale<<5) | divide); /* set reg */
	
	/* Now set IDR 11/17 */
	maestro_write(s, 0x11, maestro_read(s, 0x11)|1);
	maestro_write(s, 0x17, maestro_read(s, 0x17)|1);
}
/* --------------------------------------------------------------------- */

/* this quickly calculates the frequency needed for bob
	and sets it if its different than what bob is
	currently running at.  its called often so 
	needs to be fairly quick. */
#define BOB_MIN 50
#define BOB_MAX 400
static void calc_bob_rate(struct ess_state *s) {
#if 0 /* this thing tries to set the frequency of bob such that
	there are 2 interrupts / buffer walked by the dac/adc.  That
	is probably very wrong for people who actually care about 
	mid buffer positioning.  it should be calculated as bytes/interrupt
	and that needs to be decided :)  so for now just use the static 150
	in start_bob.*/

	unsigned int dac_rate=2,adc_rate=1,newrate;
	static int israte=-1;

	if (s->dma_dac.fragsize == 0) dac_rate = BOB_MIN;
	else  {
		dac_rate =	(2 * s->ratedac * sample_size[(s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK]) /
				(s->dma_dac.fragsize) ;
	}
		
	if (s->dma_adc.fragsize == 0) adc_rate = BOB_MIN;
	else {
		adc_rate =	(2 * s->rateadc * sample_size[(s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK]) /
				(s->dma_adc.fragsize) ;
	}

	if(dac_rate > adc_rate) newrate = adc_rate;
	else newrate=dac_rate;

	if(newrate > BOB_MAX) newrate = BOB_MAX;
	else {
		if(newrate < BOB_MIN) 
			newrate = BOB_MIN;
	}

	if( israte != newrate) {
		printk("dac: %d  adc: %d rate: %d\n",dac_rate,adc_rate,israte);
		israte=newrate;
	}
#endif

}

static int 
prog_dmabuf(struct ess_state *s, unsigned rec)
{
	struct dmabuf *db = rec ? &s->dma_adc : &s->dma_dac;
	unsigned rate = rec ? s->rateadc : s->ratedac;
	unsigned bytepersec;
	unsigned bufs;
	unsigned char fmt;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	fmt = s->fmt;
	if (rec) {
		stop_adc(s);
		fmt >>= ESS_ADC_SHIFT;
	} else {
		stop_dac(s);
		fmt >>= ESS_DAC_SHIFT;
	}
	spin_unlock_irqrestore(&s->lock, flags);
	fmt &= ESS_FMT_MASK;

	db->hwptr = db->swptr = db->total_bytes = db->count = db->error = db->endcleared = 0;

	bytepersec = rate << sample_shift[fmt];
	bufs = PAGE_SIZE << db->buforder;
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
	db->fragsamples = db->fragsize >> sample_shift[fmt];
	db->dmasize = db->numfrag << db->fragshift;

	M_printk("maestro: setup oss: numfrag: %d fragsize: %d dmasize: %d\n",db->numfrag,db->fragsize,db->dmasize);

	memset(db->rawbuf, (fmt & ESS_FMT_16BIT) ? 0 : 0x80, db->dmasize);

	spin_lock_irqsave(&s->lock, flags);
	if (rec) {
		ess_rec_setup(s, fmt, s->rateadc, 
			db->rawbuf, db->numfrag << db->fragshift);
	} else {
		ess_play_setup(s, fmt, s->ratedac, 
			db->rawbuf, db->numfrag << db->fragshift);
	}
	spin_unlock_irqrestore(&s->lock, flags);
	db->ready = 1;

	return 0;
}

static __inline__ void 
clear_advance(struct ess_state *s)
{
	unsigned char c = ((s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_16BIT) ? 0 : 0x80;
	unsigned char *buf = s->dma_dac.rawbuf;
	unsigned bsize = s->dma_dac.dmasize;
	/* swptr is always in bytes as read from an apu.. */
	unsigned bptr = s->dma_dac.swptr;
	unsigned len = s->dma_dac.fragsize;
	int i=1;

	if((s->fmt >> ESS_DAC_SHIFT)  & ESS_FMT_STEREO) {
		i++;
		bsize >>=1;
	}
		
	for ( ;i; i-- , buf += bsize) {

		if (bptr + len > bsize) {
			unsigned x = bsize - bptr;
			memset(buf + bptr, c, x);
			/* account for wrapping? */
			bptr = 0;
			len -= x;
		}
		memset(buf + bptr, c, len);
	}
}

/* call with spinlock held! */
static void 
ess_update_ptr(struct ess_state *s)
{
	unsigned hwptr;
	int diff;

	/* update ADC pointer */
	if (s->dma_adc.ready) {
		/* oh boy should this all be re-written.  everything in the current code paths think
		that the various counters/pointers are expressed in bytes to the user but we have
		two apus doing stereo stuff so we fix it up here.. it propogates to all the various
		counters from here.  Notice that this means that mono recording is very very
		broken right now.  */
		if ( s->fmt & (ESS_FMT_STEREO << ESS_ADC_SHIFT)) {
			hwptr = (get_dmac(s)*2) % s->dma_adc.dmasize;
		} else {
			hwptr = get_dmac(s) % s->dma_adc.dmasize;
		}
		diff = (s->dma_adc.dmasize + hwptr - s->dma_adc.hwptr) % s->dma_adc.dmasize;
		s->dma_adc.hwptr = hwptr;
		s->dma_adc.total_bytes += diff;
		s->dma_adc.count += diff;
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize) 
			wake_up(&s->dma_adc.wait);
		if (!s->dma_adc.mapped) {
			if (s->dma_adc.count > (signed)(s->dma_adc.dmasize - ((3 * s->dma_adc.fragsize) >> 1))) {
				/* FILL ME 
				wrindir(s, SV_CIENABLE, s->enable); */
				stop_adc(s); 
				/* brute force everyone back in sync, sigh */
				s->dma_adc.count = 0;
				s->dma_adc.swptr = 0;
				s->dma_adc.hwptr = 0;
				s->dma_adc.error++;
			}
		}
	}
	/* update DAC pointer */
	if (s->dma_dac.ready) {
		/* this is so gross.  */
		hwptr = (/*s->dma_dac.dmasize -*/ get_dmaa(s)) % s->dma_dac.dmasize; 
		diff = (s->dma_dac.dmasize + hwptr - s->dma_dac.hwptr) % s->dma_dac.dmasize;
/*		M_printk("updating dac: hwptr: %d diff: %d\n",hwptr,diff);*/
		s->dma_dac.hwptr = hwptr;
		s->dma_dac.total_bytes += diff;
		if (s->dma_dac.mapped) {
			s->dma_dac.count += diff;
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) {
				wake_up(&s->dma_dac.wait);
			}
		} else {
			s->dma_dac.count -= diff;
/*			M_printk("maestro: ess_update_ptr: diff: %d, count: %d\n", diff, s->dma_dac.count); */
			if (s->dma_dac.count <= 0) {
				/* FILL ME 
				wrindir(s, SV_CIENABLE, s->enable); */
				/* XXX how on earth can calling this with the lock held work.. */
				stop_dac(s);
				/* brute force everyone back in sync, sigh */
				s->dma_dac.count = 0; 
				s->dma_dac.swptr = 0; 
				s->dma_dac.hwptr = 0; 
				s->dma_dac.error++;
			} else if (s->dma_dac.count <= (signed)s->dma_dac.fragsize && !s->dma_dac.endcleared) {
				clear_advance(s);
				s->dma_dac.endcleared = 1;
			}
			if (s->dma_dac.count + (signed)s->dma_dac.fragsize <= (signed)s->dma_dac.dmasize) {
				wake_up(&s->dma_dac.wait);
			}
		}
	}
}

static void 
ess_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
        struct ess_state *s;
        struct ess_card *c = (struct ess_card *)dev_id;
	int i;
	u32 event;

	if ( ! (event = inb(c->iobase+0x1A)) ) return;

	outw(inw(c->iobase+4)&1, c->iobase+4);

/*	M_printk("maestro int: %x\n",event);*/

	if(event&(1<<6))
	{
		/* XXX if we have a hw volume control int enable
			all the ints?  doesn't make sense.. */
		event = inw(c->iobase+0x18);
		outb(0xFF, c->iobase+0x1A);
	}
	else
	{
		/* else ack 'em all, i imagine */
		outb(0xFF, c->iobase+0x1A);
	}
		
	/*
	 *	Update the pointers for all APU's we are running.
	 */
	for(i=0;i<NR_DSPS;i++)
	{
		s=&c->channels[i];
		if(s->dev_audio == -1)
			break;
		spin_lock(&s->lock);
		ess_update_ptr(s);
		spin_unlock(&s->lock);
	}
}


/* --------------------------------------------------------------------- */

static const char invalid_magic[] = KERN_CRIT "maestro: invalid magic value in %s\n";

#define VALIDATE_MAGIC(FOO,MAG)                         \
({                                                \
	if (!(FOO) || (FOO)->magic != MAG) { \
		printk(invalid_magic,__FUNCTION__);            \
		return -ENXIO;                    \
	}                                         \
})

#define VALIDATE_STATE(a) VALIDATE_MAGIC(a,ESS_STATE_MAGIC)
#define VALIDATE_CARD(a) VALIDATE_MAGIC(a,ESS_CARD_MAGIC)

static void set_mixer(struct ess_card *card,unsigned int mixer, unsigned int val ) 
{
	unsigned int left,right;
	/* cleanse input a little */
	right = ((val >> 8)  & 0xff) ;
	left = (val  & 0xff) ;

	if(right > 100) right = 100;
	if(left > 100) left = 100;

	card->mix.mixer_state[mixer]=(right << 8) | left;
	card->mix.write_mixer(card,mixer,left,right);
}

static void
mixer_push_state(struct ess_card *card)
{
	int i;
	for(i = 0 ; i < SOUND_MIXER_NRDEVICES ; i++) {
		if( ! supported_mixer(card,i)) continue;

		set_mixer(card,i,card->mix.mixer_state[i]);
	}
}

static int mixer_ioctl(struct ess_card *card, unsigned int cmd, unsigned long arg)
{
	int i, val=0;

	VALIDATE_CARD(card);
        if (cmd == SOUND_MIXER_INFO) {
		mixer_info info;
		strncpy(info.id, card_names[card->card_type], sizeof(info.id));
		strncpy(info.name,card_names[card->card_type],sizeof(info.name));
		info.modify_counter = card->mix.modcnt;
		if (copy_to_user((void *)arg, &info, sizeof(info)))
			return -EFAULT;
		return 0;
	}
	if (cmd == SOUND_OLD_MIXER_INFO) {
		_old_mixer_info info;
		strncpy(info.id, card_names[card->card_type], sizeof(info.id));
		strncpy(info.name,card_names[card->card_type],sizeof(info.name));
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
                case SOUND_MIXER_RECSRC: /* give them the current record source */

			if(!card->mix.recmask_io) {
				val = 0;
			} else {
				spin_lock(&card->lock);
				val = card->mix.recmask_io(card,1,0);
				spin_unlock(&card->lock);
			}
			break;
			
                case SOUND_MIXER_DEVMASK: /* give them the supported mixers */
			val = card->mix.supported_mixers;
			break;

                case SOUND_MIXER_RECMASK: /* Arg contains a bit for each supported recording source */
			val = card->mix.record_sources;
			break;
			
                case SOUND_MIXER_STEREODEVS: /* Mixer channels supporting stereo */
			val = card->mix.stereo_mixers;
			break;
			
                case SOUND_MIXER_CAPS:
			val = SOUND_CAP_EXCL_INPUT;
			break;

		default: /* read a specific mixer */
			i = _IOC_NR(cmd);

			if ( ! supported_mixer(card,i)) 
				return -EINVAL;

			/* do we ever want to touch the hardware? */
/*			spin_lock(&card->lock);
			val = card->mix.read_mixer(card,i);
			spin_unlock(&card->lock);*/

			val = card->mix.mixer_state[i];
/*			M_printk("returned 0x%x for mixer %d\n",val,i);*/

			break;
		}
		return put_user(val,(int *)arg);
	}
	
        if (_IOC_DIR(cmd) != (_IOC_WRITE|_IOC_READ))
		return -EINVAL;
	
	card->mix.modcnt++;

	get_user_ret(val, (int *)arg, -EFAULT);

	switch (_IOC_NR(cmd)) {
	case SOUND_MIXER_RECSRC: /* Arg contains a bit for each recording source */

		if (!card->mix.recmask_io) return -EINVAL;
		if(!val) return 0;
		if(! (val &= card->mix.record_sources)) return -EINVAL;

		spin_lock(&card->lock);
		card->mix.recmask_io(card,0,val);
		spin_unlock(&card->lock);
		return 0;

	default:
		i = _IOC_NR(cmd);

		if ( ! supported_mixer(card,i)) 
			return -EINVAL;

		spin_lock(&card->lock);
		set_mixer(card,i,val);
		spin_unlock(&card->lock);

		return 0;
	}
}

/* --------------------------------------------------------------------- */

static loff_t ess_llseek(struct file *file, loff_t offset, int origin)
{
	return -ESPIPE;
}

/* --------------------------------------------------------------------- */

static int ess_open_mixdev(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct ess_card *card = devs;

	while (card && card->dev_mixer != minor)
		card = card->next;
	if (!card)
		return -ENODEV;

	file->private_data = card;
	MOD_INC_USE_COUNT;
	return 0;
}

static int ess_release_mixdev(struct inode *inode, struct file *file)
{
	struct ess_card *card = (struct ess_card *)file->private_data;

	VALIDATE_CARD(card);
	
	MOD_DEC_USE_COUNT;
	return 0;
}

static int ess_ioctl_mixdev(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ess_card *card = (struct ess_card *)file->private_data;

	VALIDATE_CARD(card);

	return mixer_ioctl(card, cmd, arg);
}

static /*const*/ struct file_operations ess_mixer_fops = {
	&ess_llseek,
	NULL,  /* read */
	NULL,  /* write */
	NULL,  /* readdir */
	NULL,  /* poll */
	&ess_ioctl_mixdev,
	NULL,  /* mmap */
	&ess_open_mixdev,
	NULL,	/* flush */
	&ess_release_mixdev,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

/* --------------------------------------------------------------------- */

static int drain_dac(struct ess_state *s, int nonblock)
{
	DECLARE_WAITQUEUE(wait,current);
	unsigned long flags;
	int count;
	signed long tmo;

	if (s->dma_dac.mapped || !s->dma_dac.ready)
		return 0;
	current->state = TASK_INTERRUPTIBLE;
        add_wait_queue(&s->dma_dac.wait, &wait);
        for (;;) {
		/* XXX uhm.. questionable locking*/
                spin_lock_irqsave(&s->lock, flags);
		count = s->dma_dac.count;
                spin_unlock_irqrestore(&s->lock, flags);
		if (count <= 0)
			break;
		if (signal_pending(current))
                        break;
                if (nonblock) {
                        remove_wait_queue(&s->dma_dac.wait, &wait);
			current->state = TASK_RUNNING;
                        return -EBUSY;
                }
		tmo = (count * HZ) / s->ratedac;
		tmo >>= sample_shift[(s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK];
		/* XXX this is just broken.  someone is waking us up alot, or schedule_timeout is broken.
			or something.  who cares. - zach */
		if (!schedule_timeout(tmo ? tmo : 1) && tmo)
			M_printk(KERN_DEBUG "maestro: dma timed out?? %ld\n",jiffies);
        }
        remove_wait_queue(&s->dma_dac.wait, &wait);
	current->state = TASK_RUNNING;
        if (signal_pending(current))
                return -ERESTARTSYS;
        return 0;
}

/* --------------------------------------------------------------------- */
/* Zach sez: "god this is gross.." */
static int 
comb_stereo(unsigned char *real_buffer,unsigned char  *tmp_buffer, int offset, 
	int count, int bufsize)
{  
	/* No such thing as stereo recording, so we
	use dual input mixers.  which means we have to 
	combine mono to stereo buffer.  yuck. 

	but we don't have to be able to work a byte at a time..*/

	unsigned char *so,*left,*right;
	int i;

	so = tmp_buffer;
	left = real_buffer + offset;
	right = real_buffer + bufsize/2 + offset;

/*	M_printk("comb_stereo writing %d to %p from %p and %p, offset: %d size: %d\n",count/2, tmp_buffer,left,right,offset,bufsize);*/

	for(i=count/4; i ; i--) {
		(*(so+2)) = *(right++);
		(*(so+3)) = *(right++);
		(*so) = *(left++);
		(*(so+1)) = *(left++);
		so+=4;
	}

	return 0;
}

/* in this loop, dma_adc.count signifies the amount of data thats waiting
	to be copied to the user's buffer.  it is filled by the interrupt
	handler and drained by this loop. */
static ssize_t 
ess_read(struct file *file, char *buffer, size_t count, loff_t *ppos)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	int cnt;
	unsigned char *combbuf = NULL;
	
	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_adc.mapped)
		return -ENXIO;
	if (!s->dma_adc.ready && (ret = prog_dmabuf(s, 1)))
		return ret;
	if (!access_ok(VERIFY_WRITE, buffer, count))
		return -EFAULT;
	if(!(combbuf = kmalloc(count,GFP_KERNEL)))
		return -ENOMEM;
	ret = 0;

	calc_bob_rate(s);

	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);
		/* remember, all these things are expressed in bytes to be
			sent to the user.. hence the evil / 2 down below */
		swptr = s->dma_adc.swptr;
		cnt = s->dma_adc.dmasize-swptr;
		if (s->dma_adc.count < cnt)
			cnt = s->dma_adc.count;
		spin_unlock_irqrestore(&s->lock, flags);

		if (cnt > count)
			cnt = count;

		if ( cnt > 0 ) cnt &= ~3;

		if (cnt <= 0) {
			start_adc(s);
			if (file->f_flags & O_NONBLOCK) 
			{
				ret = ret ? ret : -EAGAIN;
				goto rec_return_free;
			}
			if (!interruptible_sleep_on_timeout(&s->dma_adc.wait, HZ)) {
#ifdef CONFIG_APM
				if(! in_suspend)
#endif
				printk(KERN_DEBUG "maestro: read: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_adc.dmasize, s->dma_adc.fragsize, s->dma_adc.count, 
				       s->dma_adc.hwptr, s->dma_adc.swptr);
				stop_adc(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmac(s, virt_to_bus(s->dma_adc.rawbuf), s->dma_adc.numfrag << s->dma_adc.fragshift);
				/* program enhanced mode registers */
				/* FILL ME */
/*				wrindir(s, SV_CIDMACBASECOUNT1, (s->dma_adc.fragsamples-1) >> 8);
				wrindir(s, SV_CIDMACBASECOUNT0, s->dma_adc.fragsamples-1); */
				s->dma_adc.count = s->dma_adc.hwptr = s->dma_adc.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (signal_pending(current)) 
			{
				ret = ret ? ret : -ERESTARTSYS;
				goto rec_return_free;
			}
			continue;
		}
	
		if(s->fmt & (ESS_FMT_STEREO << ESS_ADC_SHIFT)) {
			/* swptr/2 so that we know the real offset in each apu's buffer */
			comb_stereo(s->dma_adc.rawbuf,combbuf,swptr/2,cnt,s->dma_adc.dmasize);
			if (copy_to_user(buffer, combbuf, cnt)) {
				ret = ret ? ret : -EFAULT;
				goto rec_return_free;
			}
		} else  {
			if (copy_to_user(buffer, s->dma_adc.rawbuf + swptr, cnt)) {
				ret = ret ? ret : -EFAULT;
				goto rec_return_free;
			}
		}

		swptr = (swptr + cnt) % s->dma_adc.dmasize;
		spin_lock_irqsave(&s->lock, flags);
		s->dma_adc.swptr = swptr;
		s->dma_adc.count -= cnt;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_adc(s);
	}

rec_return_free:
	if(combbuf) kfree(combbuf);
	return ret;
}

/* god this is gross..*/
/* again, the mode passed is shifted/masked */
static int 
split_stereo(unsigned char *real_buffer,unsigned char  *tmp_buffer, int offset, 
	int count, int bufsize, int mode)
{  
	/* oh, bother.	stereo decoding APU's don't work in 16bit so we
	use dual linear decoders.  which means we have to hack up stereo
	buffer's we're given.  yuck.  */

	unsigned char *so,*left,*right;
	int i;

	so = tmp_buffer;
	left = real_buffer + offset;
	right = real_buffer + bufsize/2 + offset;

	if(mode & ESS_FMT_16BIT) {
		for(i=count/4; i ; i--) {
			*(right++) = (*(so+2));
			*(right++) = (*(so+3));
			*(left++) = (*so);
			*(left++) = (*(so+1));
			so+=4;
		}
	} else {
		for(i=count/2; i ; i--) {
			*(right++) = (*(so+1));
			*(left++) = (*so);
			so+=2;
		}
	}

	return 0;
}

static ssize_t 
ess_write(struct file *file, const char *buffer, size_t count, loff_t *ppos)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	ssize_t ret;
	unsigned long flags;
	unsigned swptr;
	unsigned char *splitbuf = NULL;
	int cnt;
	int mode = (s->fmt >> ESS_DAC_SHIFT) & ESS_FMT_MASK;
	
	VALIDATE_STATE(s);
	if (ppos != &file->f_pos)
		return -ESPIPE;
	if (s->dma_dac.mapped)
		return -ENXIO;
	if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
		return ret;
	if (!access_ok(VERIFY_READ, buffer, count))
		return -EFAULT;
	/* XXX be more clever than this.. */
	if (!(splitbuf = kmalloc(count,GFP_KERNEL)))
		return -ENOMEM; 
	ret = 0;

	calc_bob_rate(s);

	while (count > 0) {
		spin_lock_irqsave(&s->lock, flags);

		if (s->dma_dac.count < 0) {
			s->dma_dac.count = 0;
			s->dma_dac.swptr = s->dma_dac.hwptr;
		}
		swptr = s->dma_dac.swptr;

		if(mode & ESS_FMT_STEREO) {
			/* in stereo we have the 'dual' buffers.. */
			cnt = ((s->dma_dac.dmasize/2)-swptr)*2;
		} else {
			cnt = s->dma_dac.dmasize-swptr;
		}
		if (s->dma_dac.count + cnt > s->dma_dac.dmasize)
			cnt = s->dma_dac.dmasize - s->dma_dac.count;

		spin_unlock_irqrestore(&s->lock, flags);

		if (cnt > count)
			cnt = count;

		/* our goofball stereo splitter can only deal in mults of 4 */
		if (cnt > 0) 
			cnt &= ~3;

		if (cnt <= 0) {
			start_dac(s);
			if (file->f_flags & O_NONBLOCK) {
				if(!ret) ret = -EAGAIN;
				goto return_free;
			}
			if (!interruptible_sleep_on_timeout(&s->dma_dac.wait, HZ)) {
#ifdef CONFIG_APM
				if(! in_suspend)
#endif
				printk(KERN_DEBUG "maestro: write: chip lockup? dmasz %u fragsz %u count %i hwptr %u swptr %u\n",
				       s->dma_dac.dmasize, s->dma_dac.fragsize, s->dma_dac.count, 
				       s->dma_dac.hwptr, s->dma_dac.swptr);
				stop_dac(s);
				spin_lock_irqsave(&s->lock, flags);
				set_dmaa(s, virt_to_bus(s->dma_dac.rawbuf), s->dma_dac.numfrag << s->dma_dac.fragshift);
				/* program enhanced mode registers */
/*				wrindir(s, SV_CIDMAABASECOUNT1, (s->dma_dac.fragsamples-1) >> 8);
				wrindir(s, SV_CIDMAABASECOUNT0, s->dma_dac.fragsamples-1); */
				/* FILL ME */
				s->dma_dac.count = s->dma_dac.hwptr = s->dma_dac.swptr = 0;
				spin_unlock_irqrestore(&s->lock, flags);
			}
			if (signal_pending(current)) {
				if (!ret) ret = -ERESTARTSYS;
				goto return_free;
			}
			continue;
		}
		if(mode & ESS_FMT_STEREO) {
			if (copy_from_user(splitbuf, buffer, cnt)) {
				if (!ret) ret = -EFAULT;
				goto return_free;
			}
			split_stereo(s->dma_dac.rawbuf,splitbuf,swptr,cnt,s->dma_dac.dmasize,
				mode);
		} else {
			if (copy_from_user(s->dma_dac.rawbuf + swptr, buffer, cnt)) {
				if (!ret) ret = -EFAULT;
				goto return_free;
			}
		}

		if(mode & ESS_FMT_STEREO) {
			/* again with the weird pointer magic */
			swptr = (swptr + (cnt/2)) % (s->dma_dac.dmasize/2);
		} else {
			swptr = (swptr + cnt) % s->dma_dac.dmasize;
		}
		spin_lock_irqsave(&s->lock, flags);
		s->dma_dac.swptr = swptr;
		s->dma_dac.count += cnt;
		s->dma_dac.endcleared = 0;
		spin_unlock_irqrestore(&s->lock, flags);
		count -= cnt;
		buffer += cnt;
		ret += cnt;
		start_dac(s);
	}
return_free:
	if (splitbuf) kfree(splitbuf);
	return ret;
}

static unsigned int ess_poll(struct file *file, struct poll_table_struct *wait)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	unsigned long flags;
	unsigned int mask = 0;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		poll_wait(file, &s->dma_dac.wait, wait);
	if (file->f_mode & FMODE_READ)
		poll_wait(file, &s->dma_adc.wait, wait);
	spin_lock_irqsave(&s->lock, flags);
	ess_update_ptr(s);
	if (file->f_mode & FMODE_READ) {
		if (s->dma_adc.count >= (signed)s->dma_adc.fragsize)
			mask |= POLLIN | POLLRDNORM;
	}
	if (file->f_mode & FMODE_WRITE) {
		if (s->dma_dac.mapped) {
			if (s->dma_dac.count >= (signed)s->dma_dac.fragsize) 
				mask |= POLLOUT | POLLWRNORM;
		} else {
			if ((signed)s->dma_dac.dmasize >= s->dma_dac.count + (signed)s->dma_dac.fragsize)
				mask |= POLLOUT | POLLWRNORM;
		}
	}
	spin_unlock_irqrestore(&s->lock, flags);
	return mask;
}

/* this needs to be fixed to deal with the dual apus/buffers */
#if 0
static int ess_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	struct dmabuf *db;
	int ret;
	unsigned long size;

	VALIDATE_STATE(s);
	if (vma->vm_flags & VM_WRITE) {
		if ((ret = prog_dmabuf(s, 1)) != 0)
			return ret;
		db = &s->dma_dac;
	} else if (vma->vm_flags & VM_READ) {
		if ((ret = prog_dmabuf(s, 0)) != 0)
			return ret;
		db = &s->dma_adc;
	} else 
		return -EINVAL;
	if (vma->vm_pgofft != 0)
		return -EINVAL;
	size = vma->vm_end - vma->vm_start;
	if (size > (PAGE_SIZE << db->buforder))
		return -EINVAL;
	if (remap_page_range(vma->vm_start, virt_to_phys(db->rawbuf), size, vma->vm_page_prot))
		return -EAGAIN;
	db->mapped = 1;
	return 0;
}
#endif

static int ess_ioctl(struct inode *inode, struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ess_state *s = (struct ess_state *)file->private_data;
	unsigned long flags;
        audio_buf_info abinfo;
        count_info cinfo;
	int val, mapped, ret;
	unsigned char fmtm, fmtd;

/*	printk("maestro: ess_ioctl: cmd %d\n", cmd);*/
	
	VALIDATE_STATE(s);
        mapped = ((file->f_mode & FMODE_WRITE) && s->dma_dac.mapped) ||
		((file->f_mode & FMODE_READ) && s->dma_adc.mapped);
	switch (cmd) {
	case OSS_GETVERSION:
		return put_user(SOUND_VERSION, (int *)arg);

	case SNDCTL_DSP_SYNC:
		if (file->f_mode & FMODE_WRITE)
			return drain_dac(s, file->f_flags & O_NONBLOCK);
		return 0;
		
	case SNDCTL_DSP_SETDUPLEX:
		/* XXX fix */
		return 0;

	case SNDCTL_DSP_GETCAPS:
		return put_user(DSP_CAP_DUPLEX | DSP_CAP_REALTIME | DSP_CAP_TRIGGER /*| DSP_CAP_MMAP*/, (int *)arg);
		
        case SNDCTL_DSP_RESET:
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			synchronize_irq();
			s->dma_dac.swptr = s->dma_dac.hwptr = s->dma_dac.count = s->dma_dac.total_bytes = 0;
		}
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			synchronize_irq();
			s->dma_adc.swptr = s->dma_adc.hwptr = s->dma_adc.count = s->dma_adc.total_bytes = 0;
		}
		return 0;

        case SNDCTL_DSP_SPEED:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val >= 0) {
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				set_adc_rate(s, val);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				set_dac_rate(s, val);
			}
		}
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);
		
        case SNDCTL_DSP_STEREO:
		get_user_ret(val, (int *)arg, -EFAULT);
		fmtd = 0;
		fmtm = ~0;
		if (file->f_mode & FMODE_READ) {
			stop_adc(s);
			s->dma_adc.ready = 0;
			if (val)
				fmtd |= ESS_FMT_STEREO << ESS_ADC_SHIFT;
			else
				fmtm &= ~(ESS_FMT_STEREO << ESS_ADC_SHIFT);
		}
		if (file->f_mode & FMODE_WRITE) {
			stop_dac(s);
			s->dma_dac.ready = 0;
			if (val)
				fmtd |= ESS_FMT_STEREO << ESS_DAC_SHIFT;
			else
				fmtm &= ~(ESS_FMT_STEREO << ESS_DAC_SHIFT);
		}
		set_fmt(s, fmtm, fmtd);
		return 0;

        case SNDCTL_DSP_CHANNELS:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 0) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
				if (val >= 2)
					fmtd |= ESS_FMT_STEREO << ESS_ADC_SHIFT;
				else
					fmtm &= ~(ESS_FMT_STEREO << ESS_ADC_SHIFT);
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val >= 2)
					fmtd |= ESS_FMT_STEREO << ESS_DAC_SHIFT;
				else
					fmtm &= ~(ESS_FMT_STEREO << ESS_DAC_SHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_STEREO << ESS_ADC_SHIFT) 
					   : (ESS_FMT_STEREO << ESS_DAC_SHIFT))) ? 2 : 1, (int *)arg);
		
	case SNDCTL_DSP_GETFMTS: /* Returns a mask */
                return put_user(AFMT_S8|AFMT_S16_LE, (int *)arg);
		
	case SNDCTL_DSP_SETFMT: /* Selects ONE fmt*/
		get_user_ret(val, (int *)arg, -EFAULT);
		if (val != AFMT_QUERY) {
			fmtd = 0;
			fmtm = ~0;
			if (file->f_mode & FMODE_READ) {
				stop_adc(s);
				s->dma_adc.ready = 0;
	/* fixed at 16bit for now */
				fmtd |= ESS_FMT_16BIT << ESS_ADC_SHIFT;
#if 0
				if (val == AFMT_S16_LE)
					fmtd |= ESS_FMT_16BIT << ESS_ADC_SHIFT;
				else
					fmtm &= ~(ESS_FMT_16BIT << ESS_ADC_SHIFT);
#endif
			}
			if (file->f_mode & FMODE_WRITE) {
				stop_dac(s);
				s->dma_dac.ready = 0;
				if (val == AFMT_S16_LE)
					fmtd |= ESS_FMT_16BIT << ESS_DAC_SHIFT;
				else
					fmtm &= ~(ESS_FMT_16BIT << ESS_DAC_SHIFT);
			}
			set_fmt(s, fmtm, fmtd);
		}
 		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? 
			(ESS_FMT_16BIT << ESS_ADC_SHIFT) 
			: (ESS_FMT_16BIT << ESS_DAC_SHIFT))) ? 
				AFMT_S16_LE : 
				AFMT_S8, 
			(int *)arg);
		
	case SNDCTL_DSP_POST:
                return 0;

        case SNDCTL_DSP_GETTRIGGER:
		val = 0;
		if ((file->f_mode & FMODE_READ) && (s->enable & ADC_RUNNING))
			val |= PCM_ENABLE_INPUT;
		if ((file->f_mode & FMODE_WRITE) && (s->enable & DAC_RUNNING)) 
			val |= PCM_ENABLE_OUTPUT;
		return put_user(val, (int *)arg);
		
	case SNDCTL_DSP_SETTRIGGER:
		get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			if (val & PCM_ENABLE_INPUT) {
				if (!s->dma_adc.ready && (ret =  prog_dmabuf(s, 1)))
					return ret;
				start_adc(s);
			} else
				stop_adc(s);
		}
		if (file->f_mode & FMODE_WRITE) {
			if (val & PCM_ENABLE_OUTPUT) {
				if (!s->dma_dac.ready && (ret = prog_dmabuf(s, 0)))
					return ret;
				start_dac(s);
			} else
				stop_dac(s);
		}
		return 0;

	case SNDCTL_DSP_GETOSPACE:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		if (!(s->enable & DAC_RUNNING) && (val = prog_dmabuf(s, 0)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
		abinfo.fragsize = s->dma_dac.fragsize;
                abinfo.bytes = s->dma_dac.dmasize - s->dma_dac.count;
                abinfo.fragstotal = s->dma_dac.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_dac.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;

	case SNDCTL_DSP_GETISPACE:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		if (!(s->enable & ADC_RUNNING) && (val = prog_dmabuf(s, 1)) != 0)
			return val;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
		abinfo.fragsize = s->dma_adc.fragsize;
                abinfo.bytes = s->dma_adc.count;
                abinfo.fragstotal = s->dma_adc.numfrag;
                abinfo.fragments = abinfo.bytes >> s->dma_adc.fragshift;      
		spin_unlock_irqrestore(&s->lock, flags);
		return copy_to_user((void *)arg, &abinfo, sizeof(abinfo)) ? -EFAULT : 0;
		
        case SNDCTL_DSP_NONBLOCK:
                file->f_flags |= O_NONBLOCK;
                return 0;

        case SNDCTL_DSP_GETODELAY:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
                val = s->dma_dac.count;
		spin_unlock_irqrestore(&s->lock, flags);
		return put_user(val, (int *)arg);

        case SNDCTL_DSP_GETIPTR:
		if (!(file->f_mode & FMODE_READ))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
                cinfo.bytes = s->dma_adc.total_bytes;
                cinfo.blocks = s->dma_adc.count >> s->dma_adc.fragshift;
                cinfo.ptr = s->dma_adc.hwptr;
		if (s->dma_adc.mapped)
			s->dma_adc.count &= s->dma_adc.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETOPTR:
		if (!(file->f_mode & FMODE_WRITE))
			return -EINVAL;
		spin_lock_irqsave(&s->lock, flags);
		ess_update_ptr(s);
                cinfo.bytes = s->dma_dac.total_bytes;
                cinfo.blocks = s->dma_dac.count >> s->dma_dac.fragshift;
                cinfo.ptr = s->dma_dac.hwptr;
		if (s->dma_dac.mapped)
			s->dma_dac.count &= s->dma_dac.fragsize-1;
		spin_unlock_irqrestore(&s->lock, flags);
                return copy_to_user((void *)arg, &cinfo, sizeof(cinfo));

        case SNDCTL_DSP_GETBLKSIZE:
		if (file->f_mode & FMODE_WRITE) {
			if ((val = prog_dmabuf(s, 0)))
				return val;
			return put_user(s->dma_dac.fragsize, (int *)arg);
		}
		if ((val = prog_dmabuf(s, 1)))
			return val;
		return put_user(s->dma_adc.fragsize, (int *)arg);

        case SNDCTL_DSP_SETFRAGMENT:
                get_user_ret(val, (int *)arg, -EFAULT);
		if (file->f_mode & FMODE_READ) {
			s->dma_adc.ossfragshift = val & 0xffff;
			s->dma_adc.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_adc.ossfragshift < 4)
				s->dma_adc.ossfragshift = 4;
			if (s->dma_adc.ossfragshift > 15)
				s->dma_adc.ossfragshift = 15;
			if (s->dma_adc.ossmaxfrags < 4)
				s->dma_adc.ossmaxfrags = 4;
		}
		if (file->f_mode & FMODE_WRITE) {
			s->dma_dac.ossfragshift = val & 0xffff;
			s->dma_dac.ossmaxfrags = (val >> 16) & 0xffff;
			if (s->dma_dac.ossfragshift < 4)
				s->dma_dac.ossfragshift = 4;
			if (s->dma_dac.ossfragshift > 15)
				s->dma_dac.ossfragshift = 15;
			if (s->dma_dac.ossmaxfrags < 4)
				s->dma_dac.ossmaxfrags = 4;
		}
		return 0;

        case SNDCTL_DSP_SUBDIVIDE:
		if ((file->f_mode & FMODE_READ && s->dma_adc.subdivision) ||
		    (file->f_mode & FMODE_WRITE && s->dma_dac.subdivision))
			return -EINVAL;
                get_user_ret(val, (int *)arg, -EFAULT);
		if (val != 1 && val != 2 && val != 4)
			return -EINVAL;
		if (file->f_mode & FMODE_READ)
			s->dma_adc.subdivision = val;
		if (file->f_mode & FMODE_WRITE)
			s->dma_dac.subdivision = val;
		return 0;

        case SOUND_PCM_READ_RATE:
		return put_user((file->f_mode & FMODE_READ) ? s->rateadc : s->ratedac, (int *)arg);

        case SOUND_PCM_READ_CHANNELS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_STEREO << ESS_ADC_SHIFT) 
					   : (ESS_FMT_STEREO << ESS_DAC_SHIFT))) ? 2 : 1, (int *)arg);

        case SOUND_PCM_READ_BITS:
		return put_user((s->fmt & ((file->f_mode & FMODE_READ) ? (ESS_FMT_16BIT << ESS_ADC_SHIFT) 
					   : (ESS_FMT_16BIT << ESS_DAC_SHIFT))) ? 16 : 8, (int *)arg);

        case SOUND_PCM_WRITE_FILTER:
        case SNDCTL_DSP_SETSYNCRO:
        case SOUND_PCM_READ_FILTER:
                return -EINVAL;
		
	}
	return -EINVAL;
}

static void
set_base_registers(struct ess_state *s,void *vaddr)
{
	unsigned long packed_phys = virt_to_bus(vaddr)>>12;
	wave_set_register(s, 0x01FC , packed_phys);
	wave_set_register(s, 0x01FD , packed_phys);
	wave_set_register(s, 0x01FE , packed_phys);
	wave_set_register(s, 0x01FF , packed_phys);
}

/* we allocate a large power of two for all our memory.
	this is cut up into (not to scale :):
	|silly fifo word	| 512byte mixbuf per adc	| dac/adc * channels |
*/
static int
allocate_buffers(struct ess_state *s)
{
	void *rawbuf=NULL;
	int order,i;
	unsigned long mapend,map;

	/* alloc as big a chunk as we can */
	for (order = (dsps_order + (15-PAGE_SHIFT) + 1); order >= (dsps_order + 2 + 1); order--)
		if((rawbuf = (void *)__get_free_pages(GFP_KERNEL|GFP_DMA, order)))
			break;

	if (!rawbuf)
		return 1;

	M_printk("maestro: allocated %ld (%d) bytes at %p\n",PAGE_SIZE<<order,order, rawbuf);

	if ((virt_to_bus(rawbuf) + (PAGE_SIZE << order) - 1) & ~((1<<28)-1))  {
		printk(KERN_ERR "maestro: DMA buffer beyond 256MB! busaddr 0x%lx  size %ld\n",
			virt_to_bus(rawbuf), PAGE_SIZE << order);
		kfree(rawbuf);
		return 1;
	}

	s->card->dmapages = rawbuf;
	s->card->dmaorder = order;

	/* play bufs are in the same first region as record bufs */
	set_base_registers(s,rawbuf);

	M_printk("maestro: writing %lx (%lx) to the wp\n",virt_to_bus(rawbuf),
		((virt_to_bus(rawbuf))&0xFFE00000)>>12);

	for(i=0;i<NR_DSPS;i++) {
		struct ess_state *ess = &s->card->channels[i];

		if(ess->dev_audio == -1)
			continue;

		ess->dma_dac.ready = s->dma_dac.mapped = 0;
		ess->dma_adc.ready = s->dma_adc.mapped = 0;
		ess->dma_adc.buforder = ess->dma_dac.buforder = order - 1 - dsps_order - 1;

		/* offset dac and adc buffers starting half way through and then at each [da][ad]c's
			order's intervals.. */
		ess->dma_dac.rawbuf = rawbuf + (PAGE_SIZE<<(order-1)) + (i * ( PAGE_SIZE << (ess->dma_dac.buforder + 1 )));
		ess->dma_adc.rawbuf = ess->dma_dac.rawbuf + ( PAGE_SIZE << ess->dma_dac.buforder);
		/* offset mixbuf by a mixbuf so that the lame status fifo can
			happily scribble away.. */ 
		ess->mixbuf = rawbuf + (512 * (i+1));

		M_printk("maestro: setup apu %d: %p %p %p\n",i,ess->dma_dac.rawbuf,
			ess->dma_adc.rawbuf, ess->mixbuf);

	}

	/* now mark the pages as reserved; otherwise remap_page_range doesn't do what we want */
	mapend = MAP_NR(rawbuf + (PAGE_SIZE << order) - 1);
	for (map = MAP_NR(rawbuf); map <= mapend; map++) {
		set_bit(PG_reserved, &mem_map[map].flags);
	}

	return 0;
} 
static void
free_buffers(struct ess_state *s)
{
	unsigned long map, mapend;

	s->dma_dac.rawbuf = s->dma_adc.rawbuf = NULL;
	s->dma_dac.mapped = s->dma_adc.mapped = 0;
	s->dma_dac.ready = s->dma_adc.ready = 0;

	M_printk("maestro: freeing %p\n",s->card->dmapages);
	/* undo marking the pages as reserved */

	mapend = MAP_NR(s->card->dmapages + (PAGE_SIZE << s->card->dmaorder) - 1);
	for (map = MAP_NR(s->card->dmapages); map <= mapend; map++)
		clear_bit(PG_reserved, &mem_map[map].flags);    

	free_pages((unsigned long)s->card->dmapages,s->card->dmaorder);
	s->card->dmapages = NULL;
}

static int 
ess_open(struct inode *inode, struct file *file)
{
	int minor = MINOR(inode->i_rdev);
	struct ess_card *c = devs;
	struct ess_state *s = NULL, *sp;
	int i;
	unsigned char fmtm = ~0, fmts = 0;

	/*
	 *	Scan the cards and find the channel. We only
	 *	do this at open time so it is ok
	 */
	 
	while (c!=NULL)
	{
		for(i=0;i<NR_DSPS;i++)
		{
			sp=&c->channels[i];
			if(sp->dev_audio < 0)
				continue;
			if((sp->dev_audio ^ minor) & ~0xf)
				continue;
			s=sp;
		}
		c=c->next;
	}
		
	if (!s)
		return -ENODEV;
		
       	VALIDATE_STATE(s);
	file->private_data = s;
	/* wait for device to become free */
	down(&s->open_sem);
	while (s->open_mode & file->f_mode) {
		if (file->f_flags & O_NONBLOCK) {
			up(&s->open_sem);
			return -EWOULDBLOCK;
		}
		up(&s->open_sem);
		interruptible_sleep_on(&s->open_wait);
		if (signal_pending(current))
			return -ERESTARTSYS;
		down(&s->open_sem);
	}

	/* under semaphore.. */
	if ((s->card->dmapages==NULL) && allocate_buffers(s)) {
		up(&s->open_sem);
		return -ENOMEM;
	}

	if (file->f_mode & FMODE_READ) {
/*
		fmtm &= ~((ESS_FMT_STEREO | ESS_FMT_16BIT) << ESS_ADC_SHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= ESS_FMT_16BIT << ESS_ADC_SHIFT; */

		fmtm &= ~((ESS_FMT_STEREO|ESS_FMT_16BIT) << ESS_ADC_SHIFT);
		fmts = (ESS_FMT_STEREO|ESS_FMT_16BIT) << ESS_ADC_SHIFT;

		s->dma_adc.ossfragshift = s->dma_adc.ossmaxfrags = s->dma_adc.subdivision = 0;
		set_adc_rate(s, 8000);
	}
	if (file->f_mode & FMODE_WRITE) {
		fmtm &= ~((ESS_FMT_STEREO | ESS_FMT_16BIT) << ESS_DAC_SHIFT);
		if ((minor & 0xf) == SND_DEV_DSP16)
			fmts |= ESS_FMT_16BIT << ESS_DAC_SHIFT;

		s->dma_dac.ossfragshift = s->dma_dac.ossmaxfrags = s->dma_dac.subdivision = 0;
		set_dac_rate(s, 8000);
	}
	set_fmt(s, fmtm, fmts);
	s->open_mode |= file->f_mode & (FMODE_READ | FMODE_WRITE);

	/* we're covered by the open_sem */
	if( ! s->card->dsps_open )  {
		start_bob(s);
	}
	s->card->dsps_open++;
	M_printk("maestro: open, %d bobs now\n",s->card->dsps_open);

	up(&s->open_sem);
	MOD_INC_USE_COUNT;
	return 0;
}

static int 
ess_release(struct inode *inode, struct file *file)
{
	struct ess_state *s = (struct ess_state *)file->private_data;

	VALIDATE_STATE(s);
	if (file->f_mode & FMODE_WRITE)
		drain_dac(s, file->f_flags & O_NONBLOCK);
	down(&s->open_sem);
	if (file->f_mode & FMODE_WRITE) {
		stop_dac(s);
	}
	if (file->f_mode & FMODE_READ) {
		stop_adc(s);
	}
		
	s->open_mode &= (~file->f_mode) & (FMODE_READ|FMODE_WRITE);
	/* we're covered by the open_sem */
	M_printk("maestro: %d dsps now alive\n",s->card->dsps_open-1);
	if( --s->card->dsps_open <= 0) {
		stop_bob(s);
		free_buffers(s);
	}
	up(&s->open_sem);
	wake_up(&s->open_wait);
	MOD_DEC_USE_COUNT;
	return 0;
}

static struct file_operations ess_audio_fops = {
	&ess_llseek,
	&ess_read,
	&ess_write,
	NULL,  /* readdir */
	&ess_poll,
	&ess_ioctl,
	NULL,	/* XXX &ess_mmap, */
	&ess_open,
	NULL,	/* flush */
	&ess_release,
	NULL,  /* fsync */
	NULL,  /* fasync */
	NULL,  /* check_media_change */
	NULL,  /* revalidate */
	NULL,  /* lock */
};

static int
maestro_config(struct ess_card *card) 
{
	struct pci_dev *pcidev = &card->pcidev;
	struct ess_state *ess = &card->channels[0];
	int apu,iobase  = card->iobase;
	u16 w;
	u32 n;

	/*
	 *	Disable ACPI
	 */
	 
	pci_write_config_dword(pcidev, 0x54, 0x00000000);
	pci_write_config_dword(pcidev, 0x56, 0x00000000);
	
	/*
	 *	Use TDMA for now. TDMA works on all boards, so while its
	 *	not the most efficient its the simplest.
	 */ 
	 
	pci_read_config_word(pcidev, 0x50, &w);

	/* Clear DMA bits */
	w&=~(1<<10|1<<9|1<<8);

	/* TDMA on */
	w|= (1<<8);

	/*
	 *	Some of these are undocumented bits
	 */
	 
	w&=~(1<<13)|(1<<14);		/* PIC Snoop mode bits */
	w&=~(1<<11);			/* Safeguard off */
	w|= (1<<7);			/* Posted write */
	w|= (1<<6);			/* ISA timing on */
	/* XXX huh?  claims to be reserved.. */
	w&=~(1<<5);			/* Don't swap left/right */
	w&=~(1<<1);			/* Subtractive decode off */
	
	pci_write_config_word(pcidev, 0x50, w);
	
	pci_read_config_word(pcidev, 0x52, &w);
	w&=~(1<<15);		/* Turn off internal clock multiplier */
	/* XXX how do we know which to use? */
	w&=~(1<<14);		/* External clock */
	
	w&=~(1<<7);		/* HWV off */
	w&=~(1<<6);		/* Debounce off */
	w&=~(1<<5);		/* GPIO 4:5 */
	w|= (1<<4);             /* Disconnect from the CHI.  Enabling this in made a dell 7500 work. */
	w&=~(1<<3);		/* IDMA off (undocumented) */
	w&=~(1<<2);		/* MIDI fix off (undoc) */
	w&=~(1<<1);		/* reserved, always write 0 */
	w&=~(1<<0);		/* IRQ to ISA off (undoc) */
	pci_write_config_word(pcidev, 0x52, w);

	/*
	 *	DDMA off
	 */

	pci_read_config_word(pcidev, 0x60, &w);
	w&=~(1<<0);
	pci_write_config_word(pcidev, 0x60, w);
	
	/*
	 *	Legacy mode
	 */

	pci_read_config_word(pcidev, 0x40, &w);
	w|=(1<<15);	/* legacy decode off */
	w&=~(1<<14);	/* Disable SIRQ */
	w&=~(0x1f);	/* disable mpu irq/io, game port, fm, SB */
	 
	pci_write_config_word(pcidev, 0x40, w);

	/* stake our claim on the iospace */
	request_region(iobase, 256, card_names[card->card_type]);
	
	sound_reset(iobase);

	/*
	 *	Ring Bus Setup
	 */

	/* setup usual 0x34 stuff.. 0x36 may be chip specific */
        outw(0xC090, iobase+0x34); /* direct sound, stereo */
        udelay(20);
        outw(0x3000, iobase+0x36); /* direct sound, stereo */
        udelay(20);


	/*
	 *	Reset the CODEC
	 */
	 
	maestro_ac97_reset(iobase,pcidev);
	
	/*
	 *	Ring Bus Setup
	 */
	 	 
	n=inl(iobase+0x34);
	n&=~0xF000;
	n|=12<<12;		/* Direct Sound, Stereo */
	outl(n, iobase+0x34);

	n=inl(iobase+0x34);
	n&=~0x0F00;		/* Modem off */
	outl(n, iobase+0x34);

	n=inl(iobase+0x34);
	n&=~0x00F0;
	n|=9<<4;		/* DAC, Stereo */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n&=~0x000F;		/* ASSP off */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n|=(1<<29);		/* Enable ring bus */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n|=(1<<28);		/* Enable serial bus */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n&=~0x00F00000;		/* MIC off */
	outl(n, iobase+0x34);
	
	n=inl(iobase+0x34);
	n&=~0x000F0000;		/* I2S off */
	outl(n, iobase+0x34);
	

	w=inw(iobase+0x18);
	w&=~(1<<7);		/* ClkRun off */
	outw(w, iobase+0x18);

	w=inw(iobase+0x18);
	w&=~(1<<6);		/* Harpo off */
	outw(w, iobase+0x18);
	
	w=inw(iobase+0x18);
	w&=~(1<<4);		/* ASSP irq off */
	outw(w, iobase+0x18);
	
	w=inw(iobase+0x18);
	w&=~(1<<3);		/* ISDN irq off */
	outw(w, iobase+0x18);
	
	w=inw(iobase+0x18);
	w|=(1<<2);		/* Direct Sound IRQ on */
	outw(w, iobase+0x18);

	w=inw(iobase+0x18);
	w&=~(1<<1);		/* MPU401 IRQ off */
	outw(w, iobase+0x18);

	w=inw(iobase+0x18);
	w|=(1<<0);		/* SB IRQ on */
	outw(w, iobase+0x18);

	/* it appears some maestros (dell 7500) only work if these are set,
		regardless of wether we use the assp or not. */

	outb(0, iobase+0xA4); 
	outb(3, iobase+0xA2); 
	outb(0, iobase+0xA6);
	
	for(apu=0;apu<16;apu++)
	{
		/* Write 0 into the buffer area 0x1E0->1EF */
		outw(0x01E0+apu, 0x10+iobase);
		outw(0x0000, 0x12+iobase);
	
		/*
		 * The 1.10 test program seem to write 0 into the buffer area
		 * 0x1D0-0x1DF too.
		 */
		outw(0x01D0+apu, 0x10+iobase);
		outw(0x0000, 0x12+iobase);
	}

#if 1
	wave_set_register(ess, IDR7_WAVE_ROMRAM, 
		(wave_get_register(ess, IDR7_WAVE_ROMRAM)&0xFF00));
	wave_set_register(ess, IDR7_WAVE_ROMRAM,
		wave_get_register(ess, IDR7_WAVE_ROMRAM)|0x100);
	wave_set_register(ess, IDR7_WAVE_ROMRAM,
		wave_get_register(ess, IDR7_WAVE_ROMRAM)&~0x200);
	wave_set_register(ess, IDR7_WAVE_ROMRAM,
		wave_get_register(ess, IDR7_WAVE_ROMRAM)|~0x400);
#else		
	maestro_write(ess, IDR7_WAVE_ROMRAM, 
		(maestro_read(ess, IDR7_WAVE_ROMRAM)&0xFF00));
	maestro_write(ess, IDR7_WAVE_ROMRAM,
		maestro_read(ess, IDR7_WAVE_ROMRAM)|0x100);
	maestro_write(ess, IDR7_WAVE_ROMRAM,
		maestro_read(ess, IDR7_WAVE_ROMRAM)&~0x200);
	maestro_write(ess, IDR7_WAVE_ROMRAM,
		maestro_read(ess, IDR7_WAVE_ROMRAM)|0x400);
#endif
	
	maestro_write(ess, IDR2_CRAM_DATA, 0x0000);
	maestro_write(ess, 0x08, 0xB004);
	/* Now back to the DirectSound stuff */
	maestro_write(ess, 0x09, 0x001B);
	maestro_write(ess, 0x0A, 0x8000);
	maestro_write(ess, 0x0B, 0x3F37);
	maestro_write(ess, 0x0C, 0x0098);
	
	/* parallel out ?? */
	maestro_write(ess, 0x0C, 
		(maestro_read(ess, 0x0C)&~0xF000)|0x8000); 
	/* parallel in, has something to do with recording :) */
	maestro_write(ess, 0x0C, 
		(maestro_read(ess, 0x0C)&~0x0F00)|0x0500);

	maestro_write(ess, 0x0D, 0x7632);
			
	/* Wave cache control on - test off, sg off, 
		enable, enable extra chans 1Mb */

	outw(inw(0x14+iobase)|(1<<8),0x14+iobase);
	outw(inw(0x14+iobase)&0xFE03,0x14+iobase);
	outw((inw(0x14+iobase)&0xFFFC), 0x14+iobase);
	outw(inw(0x14+iobase)|(1<<7),0x14+iobase);

	outw(0xA1A0, 0x14+iobase);      /* 0300 ? */

	/* Now clear the APU control ram */	
	for(apu=0;apu<NR_APUS;apu++)
	{
		for(w=0;w<NR_APU_REGS;w++)
			apu_set_register(ess, apu|ESS_CHAN_HARD, w, 0);
		
	}

	return 0;
	
}


static int 
maestro_install(struct pci_dev *pcidev, int card_type)
{
	u32 n;
	int iobase;
	int i;
	struct ess_card *card;
	struct ess_state *ess;
	int num = 0;

	/* don't pick up weird modem maestros */
	if(((pcidev->class >> 8) & 0xffff) != PCI_CLASS_MULTIMEDIA_AUDIO)
		return 0;
			
	iobase = SILLY_PCI_BASE_ADDRESS(pcidev); 

	if(check_region(iobase, 256))
	{
		printk(KERN_WARNING "maestro: can't allocate 256 bytes I/O at 0x%4.4x\n", iobase);
		return 0;
	}

	/* this was tripping up some machines */
	if(pcidev->irq == 0) 
	{
		printk(KERN_WARNING "maestro: pci subsystem reports irq 0, this might not be correct.\n");
	}

	/* just to be sure */
	pci_set_master(pcidev);

	card = kmalloc(sizeof(struct ess_card), GFP_KERNEL);
	if(card == NULL)
	{
		printk(KERN_WARNING "maestro: out of memory\n");
		return 0;
	}
	
	memset(card, 0, sizeof(*card));
	memcpy(&card->pcidev,pcidev,sizeof(card->pcidev));

#ifdef CONFIG_APM
	if (apm_register_callback(maestro_apm_callback)) {
		printk(KERN_WARNING "maestro: apm suspend might not work.\n");
	}
#endif

	card->iobase = iobase;
	card->card_type = card_type;
	card->irq = pcidev->irq;
	card->next = devs;
	card->magic = ESS_CARD_MAGIC;
	spin_lock_init(&card->lock);
	devs = card;
	
	/* init our groups of 6 apus */
	for(i=0;i<NR_DSPS;i++)
	{
		struct ess_state *s=&card->channels[i];

		s->index = i;

		s->card = card;
		init_waitqueue_head(&s->dma_adc.wait);
		init_waitqueue_head(&s->dma_dac.wait);
		init_waitqueue_head(&s->open_wait);
		spin_lock_init(&s->lock);
		SILLY_INIT_SEM(s->open_sem);
		s->magic = ESS_STATE_MAGIC;
		
		s->apu[0] = 6*i;
		s->apu[1] = (6*i)+1;
		s->apu[2] = (6*i)+2;
		s->apu[3] = (6*i)+3;
		s->apu[4] = (6*i)+4;
		s->apu[5] = (6*i)+5;
		
		if(s->dma_adc.ready || s->dma_dac.ready || s->dma_adc.rawbuf)
			printk("maestro: BOTCH!\n");
		/* register devices */
		if ((s->dev_audio = register_sound_dsp(&ess_audio_fops, -1)) < 0)
			break;
	}
	
	num = i;
	
	/* clear the rest if we ran out of slots to register */
	for(;i<NR_DSPS;i++)
	{
		struct ess_state *s=&card->channels[i];
		s->dev_audio = -1;
	}
	
	ess = &card->channels[0];

	/*
	 *	Ok card ready. Begin setup proper
	 */

	printk(KERN_INFO "maestro: Configuring %s found at IO 0x%04X IRQ %d\n", 
		card_names[card_type],iobase,card->irq);
	pci_read_config_dword(pcidev, PCI_SUBSYSTEM_VENDOR_ID, &n);
	printk(KERN_INFO "maestro:  subvendor id: 0x%08x\n",n); 

	maestro_config(card);

	if(maestro_ac97_get(iobase, 0x00)==0x0080) {
		maestro_pt101_init(card,iobase);
	} else {
		maestro_ac97_init(card,iobase);
	}

	if ((card->dev_mixer = register_sound_mixer(&ess_mixer_fops, -1)) < 0) {
		printk("maestro: couldn't register mixer!\n");
	} else {
		memcpy(card->mix.mixer_state,mixer_defaults,sizeof(card->mix.mixer_state));
		mixer_push_state(card);
	}
	
	if(request_irq(card->irq, ess_interrupt, SA_SHIRQ, card_names[card_type], card))
	{
		printk(KERN_ERR "maestro: unable to allocate irq %d,\n", card->irq);
		unregister_sound_mixer(card->dev_mixer);
		for(i=0;i<NR_DSPS;i++)
		{
			struct ess_state *s = &card->channels[i];
			if(s->dev_audio != -1)
				unregister_sound_dsp(s->dev_audio);
		}
		release_region(card->iobase, 256);		
		kfree(card);
		return 0;
	}

	printk(KERN_INFO "maestro: %d channels configured.\n", num);
	return 1; 
}

#ifdef MODULE
int init_module(void)
#else
int SILLY_MAKE_INIT(init_maestro(void))
#endif
{
	struct pci_dev *pcidev = NULL;
	int foundone = 0;

	if (!pci_present())   /* No PCI bus in this machine! */
		return -ENODEV;
	printk(KERN_INFO "maestro: version " DRIVER_VERSION " time " __TIME__ " " __DATE__ "\n");

	pcidev = NULL;

	if (dsps_order < 0)   {
		dsps_order = 1;
		printk(KERN_WARNING "maestro: clipping dsps_order to %d\n",dsps_order);
	}
	else if (dsps_order > MAX_DSP_ORDER)  {
		dsps_order = MAX_DSP_ORDER;
		printk(KERN_WARNING "maestro: clipping dsps_order to %d\n",dsps_order);
	}

#ifdef CONFIG_APM
	init_waitqueue_head(&suspend_queue);
#endif

	/*
	 *	Find the ESS Maestro 2.
	 */

	while( (pcidev = pci_find_device(PCI_VENDOR_ESS, PCI_DEVICE_ID_ESS_ESS1968, pcidev))!=NULL ) {
		if (maestro_install(pcidev, TYPE_MAESTRO2))
			foundone=1;
	}

	/*
	 *	Find the ESS Maestro 2E
	 */

	while( (pcidev = pci_find_device(PCI_VENDOR_ESS, PCI_DEVICE_ID_ESS_ESS1978, pcidev))!=NULL) {
		if (maestro_install(pcidev, TYPE_MAESTRO2E))
			foundone=1;
	}

	/*
	 *	ESS Maestro 1
	 */

	while((pcidev = pci_find_device(PCI_VENDOR_ESS_OLD, PCI_DEVICE_ID_ESS_ESS0100, pcidev))!=NULL) {
		if (maestro_install(pcidev, TYPE_MAESTRO))
			foundone=1;
	}
	if( ! foundone ) {
		printk("maestro: no devices found.\n");
		return -ENODEV;
	}
	return 0;
}

/* --------------------------------------------------------------------- */

#ifdef MODULE
MODULE_AUTHOR("Zach Brown <zab@redhat.com>, Alan Cox <alan@redhat.com>");
MODULE_DESCRIPTION("ESS Maestro Driver");
#ifdef M_DEBUG
MODULE_PARM(debug,"i");
#endif
MODULE_PARM(dsps_order,"i");

void cleanup_module(void)
{
	struct ess_card *s;

#ifdef CONFIG_APM
	apm_unregister_callback(maestro_apm_callback);
#endif
	while ((s = devs)) {
		int i;
		devs = devs->next;
	
		/* XXX maybe should force stop bob, but should be all 
			stopped by _release by now */
		free_irq(s->irq, s);
		unregister_sound_mixer(s->dev_mixer);
		for(i=0;i<NR_DSPS;i++)
		{
			struct ess_state *ess = &s->channels[i];
			if(ess->dev_audio != -1)
				unregister_sound_dsp(ess->dev_audio);
		}
		release_region(s->iobase, 256);
		kfree(s);
	}
	M_printk("maestro: unloading\n");
}

#endif /* MODULE */
#ifdef CONFIG_APM

void
check_suspend(void)
{
	DECLARE_WAITQUEUE(wait, current);

	if(!in_suspend) return;

	in_suspend++;
	add_wait_queue(&suspend_queue, &wait);
	current->state = TASK_UNINTERRUPTIBLE;
	schedule();
	remove_wait_queue(&suspend_queue, &wait);
	current->state = TASK_RUNNING;
}

static int 
maestro_apm_suspend(void)
{
	struct ess_card *card;
	unsigned long flags;

	save_flags(flags);
	cli();

	for (card = devs; card ; card = card->next) {
		int i,j;

		M_printk("maestro: apm in dev %p\n",card);

		for(i=0;i<NR_DSPS;i++) {
			struct ess_state *s = &card->channels[i];

			if(s->dev_audio == -1)
				continue;

			M_printk("maestro: stopping apus for device %d\n",i);
			stop_dac(s);
			stop_adc(s);
			for(j=0;j<6;j++) 
				card->apu_map[s->apu[i]][5]=apu_get_register(s,i,5);

		}

		/* get rid of interrupts? */
		if( card->dsps_open > 0)
			stop_bob(&card->channels[0]);
	}
	in_suspend=1;

	restore_flags(flags);

	/* we'll let the bios do the rest of the power down.. */

	return 0;
}
static int 
maestro_apm_resume(void)
{
	struct ess_card *card;
	unsigned long flags;

	save_flags(flags);
	cli();
	in_suspend=0;
	M_printk("maestro: resuming\n");

	/* first lets just bring everything back. .*/
	for (card = devs; card ; card = card->next) {
		int i;

		M_printk("maestro: apm in dev %p\n",card);

		maestro_config(card);
		/* need to restore the base pointers.. */ 
		if(card->dmapages) 
			set_base_registers(&card->channels[0],card->dmapages);

		mixer_push_state(card);

		for(i=0;i<NR_DSPS;i++) {
			struct ess_state *s = &card->channels[i];
			int chan,reg;

			if(s->dev_audio == -1)
				continue;

			for(chan = 0 ; chan < 6 ; chan++) {
				wave_set_register(s,s->apu[chan]<<3,s->apu_base[chan]);
				for(reg = 1 ; reg < NR_APU_REGS ; reg++)  
					apu_set_register(s,chan,reg,s->card->apu_map[s->apu[chan]][reg]);
			}
			for(chan = 0 ; chan < 6 ; chan++)  
				apu_set_register(s,chan,0,s->card->apu_map[s->apu[chan]][0] & 0xFF0F);
		}
	}

	/* now we flip on the music */
	for (card = devs; card ; card = card->next) {
		int i;

		M_printk("maestro: apm in dev %p\n",card);

		for(i=0;i<NR_DSPS;i++) {
			struct ess_state *s = &card->channels[i];

			/* these use the apu_mode, and can handle
				spurious calls */
			start_dac(s);	
			start_adc(s);	
		}
		if( card->dsps_open > 0)
			start_bob(&card->channels[0]);
	}

	restore_flags(flags);

	wake_up(&suspend_queue);

	return 0;
}

int 
maestro_apm_callback(apm_event_t ae) {

	M_printk("maestro: apm event received: 0x%x\n",ae);

	switch(ae) {
	case APM_SYS_SUSPEND: 
	case APM_CRITICAL_SUSPEND: 
	case APM_USER_SUSPEND: 
		maestro_apm_suspend();break;
	case APM_NORMAL_RESUME: 
	case APM_CRITICAL_RESUME: 
	case APM_STANDBY_RESUME: 
		maestro_apm_resume();break;
	default: break;
	}

	return 0;
}
#endif
