/*
 * sound/sequencer.c
 *
 * The sequencer personality manager.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 * Alan Cox	   : reformatted and fixed a pair of null pointer bugs
 */
#include <linux/config.h>

#include <linux/kmod.h>


#define SEQUENCER_C
#include "sound_config.h"

#ifdef CONFIG_SEQUENCER
#include "softoss.h"
int             (*softsynthp) (int cmd, int parm1, int parm2, unsigned long parm3) = NULL;

#include "midi_ctrl.h"

static int      sequencer_ok = 0;
static struct sound_timer_operations *tmr;
static int      tmr_no = -1;	/* Currently selected timer */
static int      pending_timer = -1;	/* For timer change operation */
extern unsigned long seq_time;

static int      obsolete_api_used = 0;

/*
 * Local counts for number of synth and MIDI devices. These are initialized
 * by the sequencer_open.
 */
static int      max_mididev = 0;
static int      max_synthdev = 0;

/*
 * The seq_mode gives the operating mode of the sequencer:
 *      1 = level1 (the default)
 *      2 = level2 (extended capabilities)
 */

#define SEQ_1	1
#define SEQ_2	2
static int      seq_mode = SEQ_1;

static DECLARE_WAIT_QUEUE_HEAD(seq_sleeper);
static DECLARE_WAIT_QUEUE_HEAD(midi_sleeper);

static int      midi_opened[MAX_MIDI_DEV] = {
	0
};

static int      midi_written[MAX_MIDI_DEV] = {
	0
};

static unsigned long prev_input_time = 0;
static int      prev_event_time;

#include "tuning.h"

#define EV_SZ	8
#define IEV_SZ	8

static unsigned char *queue = NULL;
static unsigned char *iqueue = NULL;

static volatile int qhead = 0, qtail = 0, qlen = 0;
static volatile int iqhead = 0, iqtail = 0, iqlen = 0;
static volatile int seq_playing = 0;
static volatile int sequencer_busy = 0;
static int      output_threshold;
static long     pre_event_timeout;
static unsigned synth_open_mask;

static int      seq_queue(unsigned char *note, char nonblock);
static void     seq_startplay(void);
static int      seq_sync(void);
static void     seq_reset(void);

#if MAX_SYNTH_DEV > 15
#error Too many synthesizer devices enabled.
#endif

int sequencer_read(int dev, struct file *file, char *buf, int count)
{
	int c = count, p = 0;
	int ev_len;
	unsigned long flags;

	dev = dev >> 4;

	ev_len = seq_mode == SEQ_1 ? 4 : 8;

	save_flags(flags);
	cli();

	if (!iqlen)
	{
 		if (file->f_flags & O_NONBLOCK) {
  			restore_flags(flags);
  			return -EAGAIN;
  		}

 		interruptible_sleep_on_timeout(&midi_sleeper,
					       pre_event_timeout);
		if (!iqlen)
		{
			restore_flags(flags);
			return 0;
		}
	}
	while (iqlen && c >= ev_len)
	{
		char *fixit = (char *) &iqueue[iqhead * IEV_SZ];
		copy_to_user(&(buf)[p], fixit, ev_len);
		p += ev_len;
		c -= ev_len;

		iqhead = (iqhead + 1) % SEQ_MAX_QUEUE;
		iqlen--;
	}
	restore_flags(flags);
	return count - c;
}

static void sequencer_midi_output(int dev)
{
	/*
	 * Currently NOP
	 */
}

void seq_copy_to_input(unsigned char *event_rec, int len)
{
	unsigned long flags;

	/*
	 * Verify that the len is valid for the current mode.
	 */

	if (len != 4 && len != 8)
		return;
	if ((seq_mode == SEQ_1) != (len == 4))
		return;

	if (iqlen >= (SEQ_MAX_QUEUE - 1))
		return;		/* Overflow */

	save_flags(flags);
	cli();
	memcpy(&iqueue[iqtail * IEV_SZ], event_rec, len);
	iqlen++;
	iqtail = (iqtail + 1) % SEQ_MAX_QUEUE;
	wake_up(&midi_sleeper);
	restore_flags(flags);
}

static void sequencer_midi_input(int dev, unsigned char data)
{
	unsigned int tstamp;
	unsigned char event_rec[4];

	if (data == 0xfe)	/* Ignore active sensing */
		return;

	if (softsynthp != NULL)
		tstamp = softsynthp(SSYN_GETTIME, 0, 0, 0);
	else
		tstamp = jiffies - seq_time;

	if (tstamp != prev_input_time)
	{
		tstamp = (tstamp << 8) | SEQ_WAIT;
		seq_copy_to_input((unsigned char *) &tstamp, 4);
		prev_input_time = tstamp;
	}
	event_rec[0] = SEQ_MIDIPUTC;
	event_rec[1] = data;
	event_rec[2] = dev;
	event_rec[3] = 0;

	seq_copy_to_input(event_rec, 4);
}

void seq_input_event(unsigned char *event_rec, int len)
{
	unsigned long this_time;

	if (seq_mode == SEQ_2)
		this_time = tmr->get_time(tmr_no);
	else if (softsynthp != NULL)
		this_time = softsynthp(SSYN_GETTIME, 0, 0, 0);
	else
		this_time = jiffies - seq_time;

	if (this_time != prev_input_time)
	{
		unsigned char   tmp_event[8];

		tmp_event[0] = EV_TIMING;
		tmp_event[1] = TMR_WAIT_ABS;
		tmp_event[2] = 0;
		tmp_event[3] = 0;
		*(unsigned int *) &tmp_event[4] = this_time;

		seq_copy_to_input(tmp_event, 8);
		prev_input_time = this_time;
	}
	seq_copy_to_input(event_rec, len);
}

int sequencer_write(int dev, struct file *file, const char *buf, int count)
{
	unsigned char event_rec[EV_SZ], ev_code;
	int p = 0, c, ev_size;
	int err;
	int mode = translate_mode(file);

	dev = dev >> 4;

	DEB(printk("sequencer_write(dev=%d, count=%d)\n", dev, count));

	if (mode == OPEN_READ)
		return -EIO;

	c = count;

	while (c >= 4)
	{
		copy_from_user((char *) event_rec, &(buf)[p], 4);
		ev_code = event_rec[0];

		if (ev_code == SEQ_FULLSIZE)
		{
			int err, fmt;

			dev = *(unsigned short *) &event_rec[2];
			if (dev < 0 || dev >= max_synthdev || synth_devs[dev] == NULL)
				return -ENXIO;

			if (!(synth_open_mask & (1 << dev)))
				return -ENXIO;

			fmt = (*(short *) &event_rec[0]) & 0xffff;
			err = synth_devs[dev]->load_patch(dev, fmt, buf, p + 4, c, 0);
			if (err < 0)
				return err;

			return err;
		}
		if (ev_code >= 128)
		{
			if (seq_mode == SEQ_2 && ev_code == SEQ_EXTENDED)
			{
				printk(KERN_WARNING "Sequencer: Invalid level 2 event %x\n", ev_code);
				return -EINVAL;
			}
			ev_size = 8;

			if (c < ev_size)
			{
				if (!seq_playing)
					seq_startplay();
				return count - c;
			}
			copy_from_user((char *) &event_rec[4], &(buf)[p + 4], 4);

		}
		else
		{
			if (seq_mode == SEQ_2)
			{
				printk(KERN_WARNING "Sequencer: 4 byte event in level 2 mode\n");
				return -EINVAL;
			}
			ev_size = 4;

			if (event_rec[0] != SEQ_MIDIPUTC)
				obsolete_api_used = 1;
		}

		if (event_rec[0] == SEQ_MIDIPUTC)
		{
			if (!midi_opened[event_rec[2]])
			{
				int mode;
				int dev = event_rec[2];

				if (dev >= max_mididev || midi_devs[dev]==NULL)
				{
					/*printk("Sequencer Error: Nonexistent MIDI device %d\n", dev);*/
					return -ENXIO;
				}
				mode = translate_mode(file);

				if ((err = midi_devs[dev]->open(dev, mode,
								sequencer_midi_input, sequencer_midi_output)) < 0)
				{
					seq_reset();
					printk(KERN_WARNING "Sequencer Error: Unable to open Midi #%d\n", dev);
					return err;
				}
				midi_opened[dev] = 1;
			}
		}
		if (!seq_queue(event_rec, (file->f_flags & (O_NONBLOCK) ? 1 : 0)))
		{
			int processed = count - c;

			if (!seq_playing)
				seq_startplay();

			if (!processed && (file->f_flags & O_NONBLOCK))
				return -EAGAIN;
			else
				return processed;
		}
		p += ev_size;
		c -= ev_size;
	}

	if (!seq_playing)
		seq_startplay();

	return count;
}

static int seq_queue(unsigned char *note, char nonblock)
{

	/*
	 * Test if there is space in the queue
	 */

	if (qlen >= SEQ_MAX_QUEUE)
		if (!seq_playing)
			seq_startplay();	/*
						 * Give chance to drain the queue
						 */

	if (!nonblock && qlen >= SEQ_MAX_QUEUE && !waitqueue_active(&seq_sleeper)) {
		/*
		 * Sleep until there is enough space on the queue
		 */
		interruptible_sleep_on(&seq_sleeper);
	}
	if (qlen >= SEQ_MAX_QUEUE)
	{
		return 0;	/*
				 * To be sure
				 */
	}
	memcpy(&queue[qtail * EV_SZ], note, EV_SZ);

	qtail = (qtail + 1) % SEQ_MAX_QUEUE;
	qlen++;

	return 1;
}

static int extended_event(unsigned char *q)
{
	int dev = q[2];

	if (dev < 0 || dev >= max_synthdev)
		return -ENXIO;

	if (!(synth_open_mask & (1 << dev)))
		return -ENXIO;

	switch (q[1])
	{
		case SEQ_NOTEOFF:
			synth_devs[dev]->kill_note(dev, q[3], q[4], q[5]);
			break;

		case SEQ_NOTEON:
			if (q[4] > 127 && q[4] != 255)
				return 0;

			if (q[5] == 0)
			{
				synth_devs[dev]->kill_note(dev, q[3], q[4], q[5]);
				break;
			}
			synth_devs[dev]->start_note(dev, q[3], q[4], q[5]);
			break;

		case SEQ_PGMCHANGE:
			synth_devs[dev]->set_instr(dev, q[3], q[4]);
			break;

		case SEQ_AFTERTOUCH:
			synth_devs[dev]->aftertouch(dev, q[3], q[4]);
			break;

		case SEQ_BALANCE:
			synth_devs[dev]->panning(dev, q[3], (char) q[4]);
			break;

		case SEQ_CONTROLLER:
			synth_devs[dev]->controller(dev, q[3], q[4], (short) (q[5] | (q[6] << 8)));
			break;

		case SEQ_VOLMODE:
			if (synth_devs[dev]->volume_method != NULL)
				synth_devs[dev]->volume_method(dev, q[3]);
			break;

		default:
			return -EINVAL;
	}
	return 0;
}

static int find_voice(int dev, int chn, int note)
{
	unsigned short key;
	int i;

	key = (chn << 8) | (note + 1);
	for (i = 0; i < synth_devs[dev]->alloc.max_voice; i++)
		if (synth_devs[dev]->alloc.map[i] == key)
			return i;
	return -1;
}

static int alloc_voice(int dev, int chn, int note)
{
	unsigned short  key;
	int voice;

	key = (chn << 8) | (note + 1);

	voice = synth_devs[dev]->alloc_voice(dev, chn, note,
					     &synth_devs[dev]->alloc);
	synth_devs[dev]->alloc.map[voice] = key;
	synth_devs[dev]->alloc.alloc_times[voice] =
			synth_devs[dev]->alloc.timestamp++;
	return voice;
}

static void seq_chn_voice_event(unsigned char *event_rec)
{
#define dev event_rec[1]
#define cmd event_rec[2]
#define chn event_rec[3]
#define note event_rec[4]
#define parm event_rec[5]

	int voice = -1;

	if ((int) dev > max_synthdev || synth_devs[dev] == NULL)
		return;
	if (!(synth_open_mask & (1 << dev)))
		return;
	if (!synth_devs[dev])
		return;

	if (seq_mode == SEQ_2)
	{
		if (synth_devs[dev]->alloc_voice)
			voice = find_voice(dev, chn, note);

		if (cmd == MIDI_NOTEON && parm == 0)
		{
			cmd = MIDI_NOTEOFF;
			parm = 64;
		}
	}

	switch (cmd)
	{
		case MIDI_NOTEON:
			if (note > 127 && note != 255)	/* Not a seq2 feature */
				return;

			if (voice == -1 && seq_mode == SEQ_2 && synth_devs[dev]->alloc_voice)
			{
				/* Internal synthesizer (FM, GUS, etc) */
				voice = alloc_voice(dev, chn, note);
			}
			if (voice == -1)
				voice = chn;

			if (seq_mode == SEQ_2 && (int) dev < num_synths)
			{
				/*
				 * The MIDI channel 10 is a percussive channel. Use the note
				 * number to select the proper patch (128 to 255) to play.
				 */

				if (chn == 9)
				{
					synth_devs[dev]->set_instr(dev, voice, 128 + note);
					synth_devs[dev]->chn_info[chn].pgm_num = 128 + note;
				}
				synth_devs[dev]->setup_voice(dev, voice, chn);
			}
			synth_devs[dev]->start_note(dev, voice, note, parm);
			break;

		case MIDI_NOTEOFF:
			if (voice == -1)
				voice = chn;
			synth_devs[dev]->kill_note(dev, voice, note, parm);
			break;

		case MIDI_KEY_PRESSURE:
			if (voice == -1)
				voice = chn;
			synth_devs[dev]->aftertouch(dev, voice, parm);
			break;

		default:
	}
#undef dev
#undef cmd
#undef chn
#undef note
#undef parm
}


static void seq_chn_common_event(unsigned char *event_rec)
{
	unsigned char dev = event_rec[1];
	unsigned char cmd = event_rec[2];
	unsigned char chn = event_rec[3];
	unsigned char p1 = event_rec[4];

	/* unsigned char p2 = event_rec[5]; */
	unsigned short w14 = *(short *) &event_rec[6];

	if ((int) dev > max_synthdev || synth_devs[dev] == NULL)
		return;
	if (!(synth_open_mask & (1 << dev)))
		return;
	if (!synth_devs[dev])
		return;

	switch (cmd)
	{
		case MIDI_PGM_CHANGE:
			if (seq_mode == SEQ_2)
			{
				synth_devs[dev]->chn_info[chn].pgm_num = p1;
				if ((int) dev >= num_synths)
					synth_devs[dev]->set_instr(dev, chn, p1);
			}
			else
				synth_devs[dev]->set_instr(dev, chn, p1);

			break;

		case MIDI_CTL_CHANGE:
			if (seq_mode == SEQ_2)
			{
				if (chn > 15 || p1 > 127)
					break;

				synth_devs[dev]->chn_info[chn].controllers[p1] = w14 & 0x7f;

				if (p1 < 32)	/* Setting MSB should clear LSB to 0 */
					synth_devs[dev]->chn_info[chn].controllers[p1 + 32] = 0;

				if ((int) dev < num_synths)
				{
					int val = w14 & 0x7f;
					int i, key;

					if (p1 < 64)	/* Combine MSB and LSB */
					{
						val = ((synth_devs[dev]->
							chn_info[chn].controllers[p1 & ~32] & 0x7f) << 7)
							| (synth_devs[dev]->
							chn_info[chn].controllers[p1 | 32] & 0x7f);
						p1 &= ~32;
					}
					/* Handle all playing notes on this channel */

					key = ((int) chn << 8);

					for (i = 0; i < synth_devs[dev]->alloc.max_voice; i++)
						if ((synth_devs[dev]->alloc.map[i] & 0xff00) == key)
							synth_devs[dev]->controller(dev, i, p1, val);
				}
				else
					synth_devs[dev]->controller(dev, chn, p1, w14);
			}
			else	/* Mode 1 */
				synth_devs[dev]->controller(dev, chn, p1, w14);
			break;

		case MIDI_PITCH_BEND:
			if (seq_mode == SEQ_2)
			{
				synth_devs[dev]->chn_info[chn].bender_value = w14;

				if ((int) dev < num_synths)
				{
					/* Handle all playing notes on this channel */
					int i, key;

					key = (chn << 8);

					for (i = 0; i < synth_devs[dev]->alloc.max_voice; i++)
						if ((synth_devs[dev]->alloc.map[i] & 0xff00) == key)
							synth_devs[dev]->bender(dev, i, w14);
				}
				else
					synth_devs[dev]->bender(dev, chn, w14);
			}
			else	/* MODE 1 */
				synth_devs[dev]->bender(dev, chn, w14);
			break;

		default:
	}
}

static int seq_timing_event(unsigned char *event_rec)
{
	unsigned char cmd = event_rec[1];
	unsigned int parm = *(int *) &event_rec[4];

	if (seq_mode == SEQ_2)
	{
		int ret;

		if ((ret = tmr->event(tmr_no, event_rec)) == TIMER_ARMED)
			if ((SEQ_MAX_QUEUE - qlen) >= output_threshold)
				wake_up(&seq_sleeper);
		return ret;
	}
	switch (cmd)
	{
		case TMR_WAIT_REL:
			parm += prev_event_time;

			/*
			 * NOTE!  No break here. Execution of TMR_WAIT_REL continues in the
			 * next case (TMR_WAIT_ABS)
			 */

		case TMR_WAIT_ABS:
			if (parm > 0)
			{
				long time;

				time = parm;
				prev_event_time = time;

				seq_playing = 1;
				if (softsynthp != NULL)
					softsynthp(SSYN_REQUEST, time, 0, 0);
				else
					request_sound_timer(time);

				if ((SEQ_MAX_QUEUE - qlen) >= output_threshold)
					wake_up(&seq_sleeper);
				return TIMER_ARMED;
			}
			break;

		case TMR_START:
			if (softsynthp != NULL)
			{
				softsynthp(SSYN_START, 0, 0, 0);
				seq_time = 0;
			}
			else
				seq_time = jiffies;
			prev_input_time = 0;
			prev_event_time = 0;
			break;

		case TMR_STOP:
			break;

		case TMR_CONTINUE:
			break;

		case TMR_TEMPO:
			break;

		case TMR_ECHO:
			if (seq_mode == SEQ_2)
				seq_copy_to_input(event_rec, 8);
			else
			{
				parm = (parm << 8 | SEQ_ECHO);
				seq_copy_to_input((unsigned char *) &parm, 4);
			}
			break;

		default:
	}

	return TIMER_NOT_ARMED;
}

static void seq_local_event(unsigned char *event_rec)
{
	unsigned char   cmd = event_rec[1];
	unsigned int    parm = *((unsigned int *) &event_rec[4]);

	switch (cmd)
	{
		case LOCL_STARTAUDIO:
#ifdef CONFIG_AUDIO
			DMAbuf_start_devices(parm);
#endif
			break;

		default:
	}
}

static void seq_sysex_message(unsigned char *event_rec)
{
	int dev = event_rec[1];
	int i, l = 0;
	unsigned char  *buf = &event_rec[2];

	if ((int) dev > max_synthdev)
		return;
	if (!(synth_open_mask & (1 << dev)))
		return;
	if (!synth_devs[dev])
		return;

	l = 0;
	for (i = 0; i < 6 && buf[i] != 0xff; i++)
		l = i + 1;

	if (!synth_devs[dev]->send_sysex)
		return;
	if (l > 0)
		synth_devs[dev]->send_sysex(dev, buf, l);
}

static int play_event(unsigned char *q)
{
	/*
	 * NOTE! This routine returns
	 *   0 = normal event played.
	 *   1 = Timer armed. Suspend playback until timer callback.
	 *   2 = MIDI output buffer full. Restore queue and suspend until timer
	 */
	unsigned int *delay;

	switch (q[0])
	{
		case SEQ_NOTEOFF:
			if (synth_open_mask & (1 << 0))
				if (synth_devs[0])
					synth_devs[0]->kill_note(0, q[1], 255, q[3]);
			break;

		case SEQ_NOTEON:
			if (q[4] < 128 || q[4] == 255)
				if (synth_open_mask & (1 << 0))
					if (synth_devs[0])
						synth_devs[0]->start_note(0, q[1], q[2], q[3]);
			break;

		case SEQ_WAIT:
			delay = (unsigned int *) q;	/*
							 * Bytes 1 to 3 are containing the *
							 * delay in 'ticks'
							 */
			*delay = (*delay >> 8) & 0xffffff;

			if (*delay > 0)
			{
				long time;

				seq_playing = 1;
				time = *delay;
				prev_event_time = time;

				if (softsynthp != NULL)
					softsynthp(SSYN_REQUEST, time, 0, 0);
				else
					request_sound_timer(time);

				if ((SEQ_MAX_QUEUE - qlen) >= output_threshold)
					wake_up(&seq_sleeper);
				/*
				 * The timer is now active and will reinvoke this function
				 * after the timer expires. Return to the caller now.
				 */
				return 1;
			}
			break;

		case SEQ_PGMCHANGE:
			if (synth_open_mask & (1 << 0))
				if (synth_devs[0])
					synth_devs[0]->set_instr(0, q[1], q[2]);
			break;

		case SEQ_SYNCTIMER: 	/*
					 * Reset timer
					 */
			if (softsynthp != NULL)
				seq_time = 0;
			else
				seq_time = jiffies;
			prev_input_time = 0;
			prev_event_time = 0;
			if (softsynthp != NULL)
				softsynthp(SSYN_START, 0, 0, 0);
			break;

		case SEQ_MIDIPUTC:	/*
					 * Put a midi character
					 */
			if (midi_opened[q[2]])
			{
				int dev;

				dev = q[2];

				if (dev < 0 || dev >= num_midis || midi_devs[dev] == NULL)
					break;

				if (!midi_devs[dev]->outputc(dev, q[1]))
				{
					/*
					 * Output FIFO is full. Wait one timer cycle and try again.
					 */

					seq_playing = 1;
					if (softsynthp != NULL)
						softsynthp(SSYN_REQUEST, -1, 0, 0);
					else
						request_sound_timer(-1);
					return 2;
				}
				else
					midi_written[dev] = 1;
			}
			break;

		case SEQ_ECHO:
			seq_copy_to_input(q, 4);	/*
							 * Echo back to the process
							 */
			break;

		case SEQ_PRIVATE:
			if ((int) q[1] < max_synthdev)
				synth_devs[q[1]]->hw_control(q[1], q);
			break;

		case SEQ_EXTENDED:
			extended_event(q);
			break;

		case EV_CHN_VOICE:
			seq_chn_voice_event(q);
			break;

		case EV_CHN_COMMON:
			seq_chn_common_event(q);
			break;

		case EV_TIMING:
			if (seq_timing_event(q) == TIMER_ARMED)
			{
				return 1;
			}
			break;

		case EV_SEQ_LOCAL:
			seq_local_event(q);
			break;

		case EV_SYSEX:
			seq_sysex_message(q);
			break;

		default:
	}
	return 0;
}

static void seq_startplay(void)
{
	unsigned long flags;
	int this_one, action;

	while (qlen > 0)
	{

		save_flags(flags);
		cli();
		qhead = ((this_one = qhead) + 1) % SEQ_MAX_QUEUE;
		qlen--;
		restore_flags(flags);

		seq_playing = 1;

		if ((action = play_event(&queue[this_one * EV_SZ])))
		{		/* Suspend playback. Next timer routine invokes this routine again */
			if (action == 2)
			{
				qlen++;
				qhead = this_one;
			}
			return;
		}
	}

	seq_playing = 0;

	if ((SEQ_MAX_QUEUE - qlen) >= output_threshold)
		wake_up(&seq_sleeper);
}

static void reset_controllers(int dev, unsigned char *controller, int update_dev)
{
	int i;
	for (i = 0; i < 128; i++)
		controller[i] = ctrl_def_values[i];
}

static void setup_mode2(void)
{
	int dev;

	max_synthdev = num_synths;

	for (dev = 0; dev < num_midis; dev++)
	{
		if (midi_devs[dev] && midi_devs[dev]->converter != NULL)
		{
			synth_devs[max_synthdev++] = midi_devs[dev]->converter;
		}
	}

	for (dev = 0; dev < max_synthdev; dev++)
	{
		int chn;

		synth_devs[dev]->sysex_ptr = 0;
		synth_devs[dev]->emulation = 0;

		for (chn = 0; chn < 16; chn++)
		{
			synth_devs[dev]->chn_info[chn].pgm_num = 0;
			reset_controllers(dev,
				synth_devs[dev]->chn_info[chn].controllers,0);
			synth_devs[dev]->chn_info[chn].bender_value = (1 << 7);	/* Neutral */
			synth_devs[dev]->chn_info[chn].bender_range = 200;
		}
	}
	max_mididev = 0;
	seq_mode = SEQ_2;
}

int sequencer_open(int dev, struct file *file)
{
	int retval, mode, i;
	int level, tmp;
	unsigned long flags;

	if (!sequencer_ok)
		sequencer_init();

	level = ((dev & 0x0f) == SND_DEV_SEQ2) ? 2 : 1;

	dev = dev >> 4;
	mode = translate_mode(file);

	DEB(printk("sequencer_open(dev=%d)\n", dev));

	if (!sequencer_ok)
	{
/*		printk("Sound card: sequencer not initialized\n");*/
		return -ENXIO;
	}
	if (dev)		/* Patch manager device (obsolete) */
		return -ENXIO;

	if(synth_devs[dev] == NULL)
		request_module("synth0");

	if (mode == OPEN_READ)
	{
		if (!num_midis)
		{
			/*printk("Sequencer: No MIDI devices. Input not possible\n");*/
			sequencer_busy = 0;
			return -ENXIO;
		}
	}
	save_flags(flags);
	cli();
	if (sequencer_busy)
	{
		restore_flags(flags);
		return -EBUSY;
	}
	sequencer_busy = 1;
	obsolete_api_used = 0;
	restore_flags(flags);

	max_mididev = num_midis;
	max_synthdev = num_synths;
	pre_event_timeout = MAX_SCHEDULE_TIMEOUT;
	seq_mode = SEQ_1;

	if (pending_timer != -1)
	{
		tmr_no = pending_timer;
		pending_timer = -1;
	}
	if (tmr_no == -1)	/* Not selected yet */
	{
		int i, best;

		best = -1;
		for (i = 0; i < num_sound_timers; i++)
			if (sound_timer_devs[i] && sound_timer_devs[i]->priority > best)
			{
				tmr_no = i;
				best = sound_timer_devs[i]->priority;
			}
		if (tmr_no == -1)	/* Should not be */
			tmr_no = 0;
	}
	tmr = sound_timer_devs[tmr_no];

	if (level == 2)
	{
		if (tmr == NULL)
		{
			/*printk("sequencer: No timer for level 2\n");*/
			sequencer_busy = 0;
			return -ENXIO;
		}
		setup_mode2();
	}
	if (!max_synthdev && !max_mididev)
	{
		sequencer_busy=0;
		return -ENXIO;
	}

	synth_open_mask = 0;

	for (i = 0; i < max_mididev; i++)
	{
		midi_opened[i] = 0;
		midi_written[i] = 0;
	}

	for (i = 0; i < max_synthdev; i++)
	{
		if (synth_devs[i]==NULL)
			continue;

		if ((tmp = synth_devs[i]->open(i, mode)) < 0)
		{
			printk(KERN_WARNING "Sequencer: Warning! Cannot open synth device #%d (%d)\n", i, tmp);
			if (synth_devs[i]->midi_dev)
				printk(KERN_WARNING "(Maps to MIDI dev #%d)\n", synth_devs[i]->midi_dev);
		}
		else
		{
			synth_open_mask |= (1 << i);
			if (synth_devs[i]->midi_dev)
				midi_opened[synth_devs[i]->midi_dev] = 1;
		}
	}

	if (softsynthp != NULL)
		seq_time = 0;
	else
		seq_time = jiffies;

	prev_input_time = 0;
	prev_event_time = 0;
	if (softsynthp != NULL)
		softsynthp(SSYN_START, 0, 0, 0);

	if (seq_mode == SEQ_1 && (mode == OPEN_READ || mode == OPEN_READWRITE))
	{
		/*
		 * Initialize midi input devices
		 */

		for (i = 0; i < max_mididev; i++)
			if (!midi_opened[i] && midi_devs[i])
			{
				if ((retval = midi_devs[i]->open(i, mode,
					sequencer_midi_input, sequencer_midi_output)) >= 0)
				{
					midi_opened[i] = 1;
				}
			}
	}
	if (seq_mode == SEQ_2)
		tmr->open(tmr_no, seq_mode);

 	init_waitqueue_head(&seq_sleeper);
 	init_waitqueue_head(&midi_sleeper);
	output_threshold = SEQ_MAX_QUEUE / 2;

	return 0;
}

void seq_drain_midi_queues(void)
{
	int i, n;

	/*
	 * Give the Midi drivers time to drain their output queues
	 */

	n = 1;

	while (!signal_pending(current) && n)
	{
		n = 0;

		for (i = 0; i < max_mididev; i++)
			if (midi_opened[i] && midi_written[i])
				if (midi_devs[i]->buffer_status != NULL)
					if (midi_devs[i]->buffer_status(i))
						n++;

		/*
		 * Let's have a delay
		 */

 		if (n)
 			interruptible_sleep_on_timeout(&seq_sleeper,
						       HZ/10);
	}
}

void sequencer_release(int dev, struct file *file)
{
	int i;
	int mode = translate_mode(file);

	dev = dev >> 4;

	DEB(printk("sequencer_release(dev=%d)\n", dev));

	/*
	 * Wait until the queue is empty (if we don't have nonblock)
	 */

	if (mode != OPEN_READ && !(file->f_flags & O_NONBLOCK))
	{
		while (!signal_pending(current) && qlen > 0)
		{
  			seq_sync();
 			interruptible_sleep_on_timeout(&seq_sleeper,
						       3*HZ);
 			/* Extra delay */
		}
	}

	if (mode != OPEN_READ)
		seq_drain_midi_queues();	/*
						 * Ensure the output queues are empty
						 */
	seq_reset();
	if (mode != OPEN_READ)
		seq_drain_midi_queues();	/*
						 * Flush the all notes off messages
						 */

	for (i = 0; i < max_synthdev; i++)
	{
		if (synth_open_mask & (1 << i))	/*
						 * Actually opened
						 */
			if (synth_devs[i])
			{
				synth_devs[i]->close(i);

				if (synth_devs[i]->midi_dev)
					midi_opened[synth_devs[i]->midi_dev] = 0;
			}
	}

	for (i = 0; i < max_mididev; i++)
	{
		if (midi_opened[i])
			midi_devs[i]->close(i);
	}

	if (seq_mode == SEQ_2)
		tmr->close(tmr_no);

	if (obsolete_api_used)
		printk(KERN_WARNING "/dev/music: Obsolete (4 byte) API was used by %s\n", current->comm);
	sequencer_busy = 0;
}

static int seq_sync(void)
{
	unsigned long flags;

	if (qlen && !seq_playing && !signal_pending(current))
		seq_startplay();

	save_flags(flags);
	cli();
 	if (qlen > 0)
 		interruptible_sleep_on_timeout(&seq_sleeper, HZ);
	restore_flags(flags);
	return qlen;
}

static void midi_outc(int dev, unsigned char data)
{
	/*
	 * NOTE! Calls sleep(). Don't call this from interrupt.
	 */

	int n;
	unsigned long flags;

	/*
	 * This routine sends one byte to the Midi channel.
	 * If the output FIFO is full, it waits until there
	 * is space in the queue
	 */

	n = 3 * HZ;		/* Timeout */

	save_flags(flags);
	cli();
 	while (n && !midi_devs[dev]->outputc(dev, data)) {
 		interruptible_sleep_on_timeout(&seq_sleeper, HZ/25);
  		n--;
  	}
	restore_flags(flags);
}

static void seq_reset(void)
{
	/*
	 * NOTE! Calls sleep(). Don't call this from interrupt.
	 */

	int i;
	int chn;
	unsigned long flags;

	if (softsynthp != NULL)
		softsynthp(SSYN_STOP, 0, 0, 0);
	else
		sound_stop_timer();

	seq_time = jiffies;
	prev_input_time = 0;
	prev_event_time = 0;

	qlen = qhead = qtail = 0;
	iqlen = iqhead = iqtail = 0;

	for (i = 0; i < max_synthdev; i++)
		if (synth_open_mask & (1 << i))
			if (synth_devs[i])
				synth_devs[i]->reset(i);

	if (seq_mode == SEQ_2)
	{
		for (chn = 0; chn < 16; chn++)
			for (i = 0; i < max_synthdev; i++)
				if (synth_open_mask & (1 << i))
					if (synth_devs[i])
					{
						synth_devs[i]->controller(i, chn, 123, 0);	/* All notes off */
						synth_devs[i]->controller(i, chn, 121, 0);	/* Reset all ctl */
						synth_devs[i]->bender(i, chn, 1 << 13);	/* Bender off */
					}
	}
	else	/* seq_mode == SEQ_1 */
	{
		for (i = 0; i < max_mididev; i++)
			if (midi_written[i])	/*
						 * Midi used. Some notes may still be playing
						 */
			{
				/*
				 *      Sending just a ACTIVE SENSING message should be enough to stop all
				 *      playing notes. Since there are devices not recognizing the
				 *      active sensing, we have to send some all notes off messages also.
				 */
				midi_outc(i, 0xfe);

				for (chn = 0; chn < 16; chn++)
				{
					midi_outc(i, (unsigned char) (0xb0 + (chn & 0x0f)));		/* control change */
					midi_outc(i, 0x7b);	/* All notes off */
					midi_outc(i, 0);	/* Dummy parameter */
				}

				midi_devs[i]->close(i);

				midi_written[i] = 0;
				midi_opened[i] = 0;
			}
	}

	seq_playing = 0;

	save_flags(flags);
	cli();

	if (waitqueue_active(&seq_sleeper)) {
		/*      printk( "Sequencer Warning: Unexpected sleeping process - Waking up\n"); */
		wake_up(&seq_sleeper);
	}
	restore_flags(flags);
}

static void seq_panic(void)
{
	/*
	 * This routine is called by the application in case the user
	 * wants to reset the system to the default state.
	 */

	seq_reset();

	/*
	 * Since some of the devices don't recognize the active sensing and
	 * all notes off messages, we have to shut all notes manually.
	 *
	 *      TO BE IMPLEMENTED LATER
	 */

	/*
	 * Also return the controllers to their default states
	 */
}

int sequencer_ioctl(int dev, struct file *file, unsigned int cmd, caddr_t arg)
{
	int midi_dev, orig_dev, val, err;
	int mode = translate_mode(file);
	struct synth_info inf;
	struct seq_event_rec event_rec;
	unsigned long flags;

	orig_dev = dev = dev >> 4;

	switch (cmd)
	{
		case SNDCTL_TMR_TIMEBASE:
		case SNDCTL_TMR_TEMPO:
		case SNDCTL_TMR_START:
		case SNDCTL_TMR_STOP:
		case SNDCTL_TMR_CONTINUE:
		case SNDCTL_TMR_METRONOME:
		case SNDCTL_TMR_SOURCE:
			if (seq_mode != SEQ_2)
				return -EINVAL;
			return tmr->ioctl(tmr_no, cmd, arg);

		case SNDCTL_TMR_SELECT:
			if (seq_mode != SEQ_2)
				return -EINVAL;
			if (get_user(pending_timer, (int *)arg))
				return -EFAULT;
			if (pending_timer < 0 || pending_timer >= num_sound_timers || sound_timer_devs[pending_timer] == NULL)
			{
				pending_timer = -1;
				return -EINVAL;
			}
			val = pending_timer;
			break;

		case SNDCTL_SEQ_PANIC:
			seq_panic();
			return -EINVAL;

		case SNDCTL_SEQ_SYNC:
			if (mode == OPEN_READ)
				return 0;
			while (qlen > 0 && !signal_pending(current))
				seq_sync();
			return qlen ? -EINTR : 0;

		case SNDCTL_SEQ_RESET:
			seq_reset();
			return 0;

		case SNDCTL_SEQ_TESTMIDI:
			if (__get_user(midi_dev, (int *)arg))
				return -EFAULT;
			if (midi_dev < 0 || midi_dev >= max_mididev || !midi_devs[midi_dev])
				return -ENXIO;

			if (!midi_opened[midi_dev] &&
				(err = midi_devs[midi_dev]->open(midi_dev, mode, sequencer_midi_input,
						     sequencer_midi_output)) < 0)
				return err;
			midi_opened[midi_dev] = 1;
			return 0;

		case SNDCTL_SEQ_GETINCOUNT:
			if (mode == OPEN_WRITE)
				return 0;
			val = iqlen;
			break;

		case SNDCTL_SEQ_GETOUTCOUNT:
			if (mode == OPEN_READ)
				return 0;
			val = SEQ_MAX_QUEUE - qlen;
			break;

		case SNDCTL_SEQ_GETTIME:
			if (seq_mode == SEQ_2)
				return tmr->ioctl(tmr_no, cmd, arg);
			if (softsynthp != NULL)
				val = softsynthp(SSYN_GETTIME, 0, 0, 0);
			else
				val = jiffies - seq_time;
			break;

		case SNDCTL_SEQ_CTRLRATE:
			/*
			 * If *arg == 0, just return the current rate
			 */
			if (seq_mode == SEQ_2)
				return tmr->ioctl(tmr_no, cmd, arg);

			if (get_user(val, (int *)arg))
				return -EFAULT;
			if (val != 0)
				return -EINVAL;
			val = HZ;
			break;

		case SNDCTL_SEQ_RESETSAMPLES:
		case SNDCTL_SYNTH_REMOVESAMPLE:
		case SNDCTL_SYNTH_CONTROL:
			if (get_user(dev, (int *)arg))
				return -EFAULT;
			if (dev < 0 || dev >= num_synths || synth_devs[dev] == NULL)
				return -ENXIO;
			if (!(synth_open_mask & (1 << dev)) && !orig_dev)
				return -EBUSY;
			return synth_devs[dev]->ioctl(dev, cmd, arg);

		case SNDCTL_SEQ_NRSYNTHS:
			val = max_synthdev;
			break;

		case SNDCTL_SEQ_NRMIDIS:
			val = max_mididev;
			break;

		case SNDCTL_SYNTH_MEMAVL:
			if (get_user(dev, (int *)arg))
				return -EFAULT;
			if (dev < 0 || dev >= num_synths || synth_devs[dev] == NULL)
				return -ENXIO;
			if (!(synth_open_mask & (1 << dev)) && !orig_dev)
				return -EBUSY;
			val = synth_devs[dev]->ioctl(dev, cmd, arg);
			break;

		case SNDCTL_FM_4OP_ENABLE:
			if (get_user(dev, (int *)arg))
				return -EFAULT;
			if (dev < 0 || dev >= num_synths || synth_devs[dev] == NULL)
				return -ENXIO;
			if (!(synth_open_mask & (1 << dev)))
				return -ENXIO;
			synth_devs[dev]->ioctl(dev, cmd, arg);
			return 0;

		case SNDCTL_SYNTH_INFO:
			if (get_user(dev, (int *)(&(((struct synth_info *)arg)->device))))
				return -EFAULT;
			if (dev < 0 || dev >= max_synthdev)
				return -ENXIO;
			if (!(synth_open_mask & (1 << dev)) && !orig_dev)
				return -EBUSY;
			return synth_devs[dev]->ioctl(dev, cmd, arg);

		/* Like SYNTH_INFO but returns ID in the name field */
		case SNDCTL_SYNTH_ID:
			if (get_user(dev, (int *)(&(((struct synth_info *)arg)->device))))
				return -EFAULT;
			if (dev < 0 || dev >= max_synthdev)
				return -ENXIO;
			if (!(synth_open_mask & (1 << dev)) && !orig_dev)
				return -EBUSY;
			memcpy(&inf, synth_devs[dev]->info, sizeof(inf));
			strncpy(inf.name, synth_devs[dev]->id, sizeof(inf.name));
			inf.device = dev;
			return copy_to_user(arg, &inf, sizeof(inf))?-EFAULT:0;

		case SNDCTL_SEQ_OUTOFBAND:
			if (copy_from_user(&event_rec, arg, sizeof(event_rec)))
				return -EFAULT;
			save_flags(flags);
			cli();
			play_event(event_rec.arr);
			restore_flags(flags);
			return 0;

		case SNDCTL_MIDI_INFO:
			if (get_user(dev, (int *)(&(((struct midi_info *)arg)->device))))
				return -EFAULT;
			if (dev < 0 || dev >= max_mididev || !midi_devs[dev])
				return -ENXIO;
			midi_devs[dev]->info.device = dev;
			return copy_to_user(arg, &midi_devs[dev]->info, sizeof(struct midi_info))?-EFAULT:0;

		case SNDCTL_SEQ_THRESHOLD:
			if (get_user(val, (int *)arg))
				return -EFAULT;
			if (val < 1)
				val = 1;
			if (val >= SEQ_MAX_QUEUE)
				val = SEQ_MAX_QUEUE - 1;
			output_threshold = val;
			return 0;

		case SNDCTL_MIDI_PRETIME:
			if (get_user(val, (int *)arg))
				return -EFAULT;
			if (val < 0)
				val = 0;
			val = (HZ * val) / 10;
			pre_event_timeout = val;
			break;

		default:
			if (mode == OPEN_READ)
				return -EIO;
			if (!synth_devs[0])
				return -ENXIO;
			if (!(synth_open_mask & (1 << 0)))
				return -ENXIO;
			if (!synth_devs[0]->ioctl)
				return -EINVAL;
			return synth_devs[0]->ioctl(0, cmd, arg);
	}
	return put_user(val, (int *)arg);
}

unsigned int sequencer_poll(int dev, struct file *file, poll_table * wait)
{
	unsigned long flags;
	unsigned int mask = 0;

	dev = dev >> 4;

	save_flags(flags);
	cli();
	/* input */
	poll_wait(file, &midi_sleeper, wait);
	if (iqlen)
		mask |= POLLIN | POLLRDNORM;

	/* output */
	poll_wait(file, &seq_sleeper, wait);
	if ((SEQ_MAX_QUEUE - qlen) >= output_threshold)
		mask |= POLLOUT | POLLWRNORM;
	restore_flags(flags);
	return mask;
}


void sequencer_timer(unsigned long dummy)
{
	seq_startplay();
}

int note_to_freq(int note_num)
{

	/*
	 * This routine converts a midi note to a frequency (multiplied by 1000)
	 */

	int note, octave, note_freq;
	static int notes[] =
	{
		261632, 277189, 293671, 311132, 329632, 349232,
		369998, 391998, 415306, 440000, 466162, 493880
	};

#define BASE_OCTAVE	5

	octave = note_num / 12;
	note = note_num % 12;

	note_freq = notes[note];

	if (octave < BASE_OCTAVE)
		note_freq >>= (BASE_OCTAVE - octave);
	else if (octave > BASE_OCTAVE)
		note_freq <<= (octave - BASE_OCTAVE);

	/*
	 * note_freq >>= 1;
	 */

	return note_freq;
}

unsigned long compute_finetune(unsigned long base_freq, int bend, int range,
		 int vibrato_cents)
{
	unsigned long amount;
	int negative, semitones, cents, multiplier = 1;

	if (!bend)
		return base_freq;
	if (!range)
		return base_freq;

	if (!base_freq)
		return base_freq;

	if (range >= 8192)
		range = 8192;

	bend = bend * range / 8192;	/* Convert to cents */
	bend += vibrato_cents;

	if (!bend)
		return base_freq;

	negative = bend < 0 ? 1 : 0;

	if (bend < 0)
		bend *= -1;
	if (bend > range)
		bend = range;

	/*
	   if (bend > 2399)
	   bend = 2399;
	 */
	while (bend > 2399)
	{
		multiplier *= 4;
		bend -= 2400;
	}

	semitones = bend / 100;
	if (semitones > 99)
		semitones = 99;
	cents = bend % 100;

	amount = (int) (semitone_tuning[semitones] * multiplier * cent_tuning[cents]) / 10000;

	if (negative)
		return (base_freq * 10000) / amount;	/* Bend down */
	else
		return (base_freq * amount) / 10000;	/* Bend up */
}


void sequencer_init(void)
{
	/* drag in sequencer_syms.o */
	{
		extern char sequencer_syms_symbol;
		sequencer_syms_symbol = 0;
	}

	if (sequencer_ok)
		return;
#ifdef CONFIG_MIDI
	MIDIbuf_init();
#endif
	queue = (unsigned char *)vmalloc(SEQ_MAX_QUEUE * EV_SZ);
	if (queue == NULL)
	{
		printk(KERN_ERR "sequencer: Can't allocate memory for sequencer output queue\n");
		return;
	}
	iqueue = (unsigned char *)vmalloc(SEQ_MAX_QUEUE * IEV_SZ);
	if (iqueue == NULL)
	{
		printk(KERN_ERR "sequencer: Can't allocate memory for sequencer input queue\n");
		vfree(queue);
		return;
	}
	sequencer_ok = 1;
}

void sequencer_unload(void)
{
	if(queue)
	{
		vfree(queue);
		queue=NULL;
	}
	if(iqueue)
	{
		vfree(iqueue);
		iqueue=NULL;
	}
}

#endif
