/*
 *  linux/drivers/video/vgacon.c -- Low level VGA based console driver
 *
 *	Created 28 Sep 1997 by Geert Uytterhoeven
 *
 *	Rewritten by Martin Mares <mj@ucw.cz>, July 1998
 *
 *  This file is based on the old console.c, vga.c and vesa_blank.c drivers.
 *
 *	Copyright (C) 1991, 1992  Linus Torvalds
 *			    1995  Jay Estabrook
 *
 *	User definable mapping table and font loading by Eugene G. Crosser,
 *	<crosser@pccross.msk.su>
 *
 *	Improved loadable font/UTF-8 support by H. Peter Anvin
 *	Feb-Sep 1995 <peter.anvin@linux.org>
 *
 *	Colour palette handling, by Simon Tatham
 *	17-Jun-95 <sgt20@cam.ac.uk>
 *
 *	if 512 char mode is already enabled don't re-enable it,
 *	because it causes screen to flicker, by Mitja Horvat
 *	5-May-96 <mitja.horvat@guest.arnes.si>
 *
 *	Use 2 outw instead of 4 outb_p to reduce erroneous text
 *	flashing on RHS of screen during heavy console scrolling .
 *	Oct 1996, Paul Gortmaker.
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/string.h>
#include <linux/kd.h>
#include <linux/malloc.h>
#include <linux/vt_kern.h>
#include <linux/selection.h>
#include <linux/ioport.h>
#include <linux/init.h>

#include <asm/io.h>


#define BLANK 0x0020

#define CAN_LOAD_EGA_FONTS	/* undefine if the user must not do this */
#define CAN_LOAD_PALETTE	/* undefine if the user must not do this */

/* You really do _NOT_ want to define this, unless you have buggy
 * Trident VGA which will resize cursor when moving it between column
 * 15 & 16. If you define this and your VGA is OK, inverse bug will
 * appear.
 */
#undef TRIDENT_GLITCH

#define dac_reg		0x3c8
#define dac_val		0x3c9
#define attrib_port	0x3c0
#define seq_port_reg	0x3c4
#define seq_port_val	0x3c5
#define gr_port_reg	0x3ce
#define gr_port_val	0x3cf
#define video_misc_rd	0x3cc
#define video_misc_wr	0x3c2

/*
 *  Interface used by the world
 */

static const char *vgacon_startup(void);
static void vgacon_init(struct vc_data *c, int init);
static void vgacon_deinit(struct vc_data *c);
static void vgacon_cursor(struct vc_data *c, int mode);
static int vgacon_switch(struct vc_data *c);
static int vgacon_blank(struct vc_data *c, int blank);
static int vgacon_font_op(struct vc_data *c, struct console_font_op *op);
static int vgacon_set_palette(struct vc_data *c, unsigned char *table);
static int vgacon_scrolldelta(struct vc_data *c, int lines);
static int vgacon_set_origin(struct vc_data *c);
static void vgacon_save_screen(struct vc_data *c);
static int vgacon_scroll(struct vc_data *c, int t, int b, int dir, int lines);
static u8 vgacon_build_attr(struct vc_data *c, u8 color, u8 intensity, u8 blink, u8 underline, u8 reverse);
static void vgacon_invert_region(struct vc_data *c, u16 *p, int count);
static unsigned long vgacon_uni_pagedir[2];


/* Description of the hardware situation */
static unsigned long   vga_vram_base;		/* Base of video memory */
static unsigned long   vga_vram_end;		/* End of video memory */
static u16             vga_video_port_reg;	/* Video register select port */
static u16             vga_video_port_val;	/* Video register value port */
static unsigned int    vga_video_num_columns;	/* Number of text columns */
static unsigned int    vga_video_num_lines;	/* Number of text lines */
static int	       vga_can_do_color = 0;	/* Do we support colors? */
static unsigned int    vga_default_font_height;	/* Height of default screen font */
static unsigned char   vga_video_type;		/* Card type */
static unsigned char   vga_hardscroll_enabled;
static unsigned char   vga_hardscroll_user_enable = 1;
static unsigned char   vga_font_is_default = 1;
static int	       vga_vesa_blanked;
static int	       vga_palette_blanked;
static int	       vga_is_gfx;
static int	       vga_512_chars;
static int	       vga_video_font_height;
static unsigned int    vga_rolled_over = 0;


static int __init no_scroll(char *str)
{
	/*
	 * Disabling scrollback is required for the Braillex ib80-piezo
	 * Braille reader made by F.H. Papenmeier (Germany).
	 * Use the "no-scroll" bootflag.
	 */
	vga_hardscroll_user_enable = vga_hardscroll_enabled = 0;
	return 1;
}

__setup("no-scroll", no_scroll);

/*
 * By replacing the four outb_p with two back to back outw, we can reduce
 * the window of opportunity to see text mislocated to the RHS of the
 * console during heavy scrolling activity. However there is the remote
 * possibility that some pre-dinosaur hardware won't like the back to back
 * I/O. Since the Xservers get away with it, we should be able to as well.
 */
static inline void write_vga(unsigned char reg, unsigned int val)
{
	unsigned int v1, v2;
	unsigned long flags;

	/*
	 * ddprintk might set the console position from interrupt
	 * handlers, thus the write has to be IRQ-atomic.
	 */
	save_flags(flags);
	cli();

#ifndef SLOW_VGA
	v1 = reg + (val & 0xff00);
	v2 = reg + 1 + ((val << 8) & 0xff00);
	outw(v1, vga_video_port_reg);
	outw(v2, vga_video_port_reg);
#else
	outb_p(reg, vga_video_port_reg);
	outb_p(val >> 8, vga_video_port_val);
	outb_p(reg+1, vga_video_port_reg);
	outb_p(val & 0xff, vga_video_port_val);
#endif
	restore_flags(flags);
}

static const char __init *vgacon_startup(void)
{
	const char *display_desc = NULL;
	u16 saved1, saved2;
	volatile u16 *p;

	if (ORIG_VIDEO_ISVGA == VIDEO_TYPE_VLFB) {
	no_vga:
#ifdef CONFIG_DUMMY_CONSOLE
		conswitchp = &dummy_con;
		return conswitchp->con_startup();
#else
		return NULL;
#endif
	}


	vga_video_num_lines = ORIG_VIDEO_LINES;
	vga_video_num_columns = ORIG_VIDEO_COLS;

	if (ORIG_VIDEO_MODE == 7)	/* Is this a monochrome display? */
	{
		vga_vram_base = 0xb0000;
		vga_video_port_reg = 0x3b4;
		vga_video_port_val = 0x3b5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			static struct resource ega_console_resource = { "ega", 0x3B0, 0x3BF };
			vga_video_type = VIDEO_TYPE_EGAM;
			vga_vram_end = 0xb8000;
			display_desc = "EGA+";
			request_resource(&ioport_resource, &ega_console_resource);
		}
		else
		{
			static struct resource mda1_console_resource = { "mda", 0x3B0, 0x3BB };
			static struct resource mda2_console_resource = { "mda", 0x3BF, 0x3BF };
			vga_video_type = VIDEO_TYPE_MDA;
			vga_vram_end = 0xb2000;
			display_desc = "*MDA";
			request_resource(&ioport_resource, &mda1_console_resource);
			request_resource(&ioport_resource, &mda2_console_resource);
			vga_video_font_height = 14;
		}
	}
	else				/* If not, it is color. */
	{
		vga_can_do_color = 1;
		vga_vram_base = 0xb8000;
		vga_video_port_reg = 0x3d4;
		vga_video_port_val = 0x3d5;
		if ((ORIG_VIDEO_EGA_BX & 0xff) != 0x10)
		{
			int i;

			vga_vram_end = 0xc0000;

			if (!ORIG_VIDEO_ISVGA) {
				static struct resource ega_console_resource = { "ega", 0x3C0, 0x3DF };
				vga_video_type = VIDEO_TYPE_EGAC;
				display_desc = "EGA";
				request_resource(&ioport_resource, &ega_console_resource);
			} else {
				static struct resource vga_console_resource = { "vga+", 0x3C0, 0x3DF };
				vga_video_type = VIDEO_TYPE_VGAC;
				display_desc = "VGA+";
				request_resource(&ioport_resource, &vga_console_resource);

#ifdef VGA_CAN_DO_64KB
				/*
				 * get 64K rather than 32K of video RAM.
				 * This doesn't actually work on all "VGA"
				 * controllers (it seems like setting MM=01
				 * and COE=1 isn't necessarily a good idea)
				 */
				vga_vram_base = 0xa0000;
				vga_vram_end = 0xb0000;
				outb_p (6, 0x3ce) ;
				outb_p (6, 0x3cf) ;
#endif

				/*
				 * Normalise the palette registers, to point
				 * the 16 screen colours to the first 16
				 * DAC entries.
				 */

				for (i=0; i<16; i++) {
					inb_p (0x3da) ;
					outb_p (i, 0x3c0) ;
					outb_p (i, 0x3c0) ;
				}
				outb_p (0x20, 0x3c0) ;

				/* now set the DAC registers back to their
				 * default values */

				for (i=0; i<16; i++) {
					outb_p (color_table[i], 0x3c8) ;
					outb_p (default_red[i], 0x3c9) ;
					outb_p (default_grn[i], 0x3c9) ;
					outb_p (default_blu[i], 0x3c9) ;
				}
			}
		}
		else
		{
			static struct resource cga_console_resource = { "cga", 0x3D4, 0x3D5 };
			vga_video_type = VIDEO_TYPE_CGA;
			vga_vram_end = 0xba000;
			display_desc = "*CGA";
			request_resource(&ioport_resource, &cga_console_resource);
			vga_video_font_height = 8;
		}
	}

	vga_vram_base = VGA_MAP_MEM(vga_vram_base);
	vga_vram_end = VGA_MAP_MEM(vga_vram_end);

	/*
	 *	Find out if there is a graphics card present.
	 *	Are there smarter methods around?
	 */
	p = (volatile u16 *)vga_vram_base;
	saved1 = scr_readw(p);
	saved2 = scr_readw(p + 1);
	scr_writew(0xAA55, p);
	scr_writew(0x55AA, p + 1);
	if (scr_readw(p) != 0xAA55 || scr_readw(p + 1) != 0x55AA) {
		scr_writew(saved1, p);
		scr_writew(saved2, p + 1);
		goto no_vga;
	}
	scr_writew(0x55AA, p);
	scr_writew(0xAA55, p + 1);
	if (scr_readw(p) != 0x55AA || scr_readw(p + 1) != 0xAA55) {
		scr_writew(saved1, p);
		scr_writew(saved2, p + 1);
		goto no_vga;
	}
	scr_writew(saved1, p);
	scr_writew(saved2, p + 1);

	if (vga_video_type == VIDEO_TYPE_EGAC
	    || vga_video_type == VIDEO_TYPE_VGAC
	    || vga_video_type == VIDEO_TYPE_EGAM) {
		vga_hardscroll_enabled = vga_hardscroll_user_enable;
		vga_default_font_height = ORIG_VIDEO_POINTS;
		vga_video_font_height = ORIG_VIDEO_POINTS;
		/* This may be suboptimal but is a safe bet - go with it */
		video_scan_lines =
			vga_video_font_height * vga_video_num_lines;
	}
	video_font_height = vga_video_font_height;

	return display_desc;
}

static void vgacon_init(struct vc_data *c, int init)
{
	unsigned long p;
	
	/* We cannot be loaded as a module, therefore init is always 1 */
	c->vc_can_do_color = vga_can_do_color;
	c->vc_cols = vga_video_num_columns;
	c->vc_rows = vga_video_num_lines;
	c->vc_complement_mask = 0x7700;
	p = *c->vc_uni_pagedir_loc;
	if (c->vc_uni_pagedir_loc == &c->vc_uni_pagedir ||
	    !--c->vc_uni_pagedir_loc[1])
		con_free_unimap(c->vc_num);
	c->vc_uni_pagedir_loc = vgacon_uni_pagedir;
	vgacon_uni_pagedir[1]++;
	if (!vgacon_uni_pagedir[0] && p)
		con_set_default_unimap(c->vc_num);
}

static inline void vga_set_mem_top(struct vc_data *c)
{
	write_vga(12, (c->vc_visible_origin-vga_vram_base)/2);
}

static void vgacon_deinit(struct vc_data *c)
{
	/* When closing the last console, reset video origin */
	if (!--vgacon_uni_pagedir[1]) {
		c->vc_visible_origin = vga_vram_base;
		vga_set_mem_top(c);
		con_free_unimap(c->vc_num);
	}
	c->vc_uni_pagedir_loc = &c->vc_uni_pagedir;
	con_set_default_unimap(c->vc_num);
}

static u8 vgacon_build_attr(struct vc_data *c, u8 color, u8 intensity, u8 blink, u8 underline, u8 reverse)
{
	u8 attr = color;

	if (vga_can_do_color) {
		if (underline)
			attr = (attr & 0xf0) | c->vc_ulcolor;
		else if (intensity == 0)
			attr = (attr & 0xf0) | c->vc_halfcolor;
	}
	if (reverse)
		attr = ((attr) & 0x88) | ((((attr) >> 4) | ((attr) << 4)) & 0x77);
	if (blink)
		attr ^= 0x80;
	if (intensity == 2)
		attr ^= 0x08;
	if (!vga_can_do_color) {
		if (underline)
			attr = (attr & 0xf8) | 0x01;
		else if (intensity == 0)
			attr = (attr & 0xf0) | 0x08;
	}
	return attr;
}

static void vgacon_invert_region(struct vc_data *c, u16 *p, int count)
{
	int col = vga_can_do_color;

	while (count--) {
		u16 a = scr_readw(p);
		if (col)
			a = ((a) & 0x88ff) | (((a) & 0x7000) >> 4) | (((a) & 0x0700) << 4);
		else
			a ^= ((a & 0x0700) == 0x0100) ? 0x7000 : 0x7700;
		scr_writew(a, p++);
	}
}

static void vgacon_set_cursor_size(int xpos, int from, int to)
{
	unsigned long flags;
	int curs, cure;
	static int lastfrom, lastto;

#ifdef TRIDENT_GLITCH
	if (xpos<16) from--, to--;
#endif

	if ((from == lastfrom) && (to == lastto)) return;
	lastfrom = from; lastto = to;

	save_flags(flags); cli();
	outb_p(0x0a, vga_video_port_reg);		/* Cursor start */
	curs = inb_p(vga_video_port_val);
	outb_p(0x0b, vga_video_port_reg);		/* Cursor end */
	cure = inb_p(vga_video_port_val);

	curs = (curs & 0xc0) | from;
	cure = (cure & 0xe0) | to;

	outb_p(0x0a, vga_video_port_reg);		/* Cursor start */
	outb_p(curs, vga_video_port_val);
	outb_p(0x0b, vga_video_port_reg);		/* Cursor end */
	outb_p(cure, vga_video_port_val);
	restore_flags(flags);
}

static void vgacon_cursor(struct vc_data *c, int mode)
{
    if (c->vc_origin != c->vc_visible_origin)
	vgacon_scrolldelta(c, 0);
    switch (mode) {
	case CM_ERASE:
	    write_vga(14, (vga_vram_end - vga_vram_base - 1)/2);
	    break;

	case CM_MOVE:
	case CM_DRAW:
	    write_vga(14, (c->vc_pos-vga_vram_base)/2);
	    switch (c->vc_cursor_type & 0x0f) {
		case CUR_UNDERLINE:
			vgacon_set_cursor_size(c->vc_x, 
					video_font_height - (video_font_height < 10 ? 2 : 3),
					video_font_height - (video_font_height < 10 ? 1 : 2));
			break;
		case CUR_TWO_THIRDS:
			vgacon_set_cursor_size(c->vc_x, 
					 video_font_height / 3,
					 video_font_height - (video_font_height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_THIRD:
			vgacon_set_cursor_size(c->vc_x, 
					 (video_font_height*2) / 3,
					 video_font_height - (video_font_height < 10 ? 1 : 2));
			break;
		case CUR_LOWER_HALF:
			vgacon_set_cursor_size(c->vc_x, 
					 video_font_height / 2,
					 video_font_height - (video_font_height < 10 ? 1 : 2));
			break;
		case CUR_NONE:
			vgacon_set_cursor_size(c->vc_x, 31, 30);
			break;
          	default:
			vgacon_set_cursor_size(c->vc_x, 1, video_font_height);
			break;
		}
	    break;
    }
}

static int vgacon_switch(struct vc_data *c)
{
	/*
	 * We need to save screen size here as it's the only way
	 * we can spot the screen has been resized and we need to
	 * set size of freshly allocated screens ourselves.
	 */
	vga_video_num_columns = c->vc_cols;
	vga_video_num_lines = c->vc_rows;
	if (!vga_is_gfx)
		scr_memcpyw_to((u16 *) c->vc_origin, (u16 *) c->vc_screenbuf, c->vc_screenbuf_size);
	return 0;	/* Redrawing not needed */
}

static void vga_set_palette(struct vc_data *c, unsigned char *table)
{
	int i, j ;

	for (i=j=0; i<16; i++) {
		outb_p (table[i], dac_reg) ;
		outb_p (c->vc_palette[j++]>>2, dac_val) ;
		outb_p (c->vc_palette[j++]>>2, dac_val) ;
		outb_p (c->vc_palette[j++]>>2, dac_val) ;
	}
}

static int vgacon_set_palette(struct vc_data *c, unsigned char *table)
{
#ifdef CAN_LOAD_PALETTE
	if (vga_video_type != VIDEO_TYPE_VGAC || vga_palette_blanked || !CON_IS_VISIBLE(c))
		return -EINVAL;
	vga_set_palette(c, table);
	return 0;
#else
	return -EINVAL;
#endif
}

/* structure holding original VGA register settings */
static struct {
	unsigned char	SeqCtrlIndex;		/* Sequencer Index reg.   */
	unsigned char	CrtCtrlIndex;		/* CRT-Contr. Index reg.  */
	unsigned char	CrtMiscIO;		/* Miscellaneous register */
	unsigned char	HorizontalTotal;	/* CRT-Controller:00h */
	unsigned char	HorizDisplayEnd;	/* CRT-Controller:01h */
	unsigned char	StartHorizRetrace;	/* CRT-Controller:04h */
	unsigned char	EndHorizRetrace;	/* CRT-Controller:05h */
	unsigned char	Overflow;		/* CRT-Controller:07h */
	unsigned char	StartVertRetrace;	/* CRT-Controller:10h */
	unsigned char	EndVertRetrace;		/* CRT-Controller:11h */
	unsigned char	ModeControl;		/* CRT-Controller:17h */
	unsigned char	ClockingMode;		/* Seq-Controller:01h */
} vga_state;

static void vga_vesa_blank(int mode)
{
	/* save original values of VGA controller registers */
	if(!vga_vesa_blanked) {
		cli();
		vga_state.SeqCtrlIndex = inb_p(seq_port_reg);
		vga_state.CrtCtrlIndex = inb_p(vga_video_port_reg);
		vga_state.CrtMiscIO = inb_p(video_misc_rd);
		sti();

		outb_p(0x00,vga_video_port_reg);	/* HorizontalTotal */
		vga_state.HorizontalTotal = inb_p(vga_video_port_val);
		outb_p(0x01,vga_video_port_reg);	/* HorizDisplayEnd */
		vga_state.HorizDisplayEnd = inb_p(vga_video_port_val);
		outb_p(0x04,vga_video_port_reg);	/* StartHorizRetrace */
		vga_state.StartHorizRetrace = inb_p(vga_video_port_val);
		outb_p(0x05,vga_video_port_reg);	/* EndHorizRetrace */
		vga_state.EndHorizRetrace = inb_p(vga_video_port_val);
		outb_p(0x07,vga_video_port_reg);	/* Overflow */
		vga_state.Overflow = inb_p(vga_video_port_val);
		outb_p(0x10,vga_video_port_reg);	/* StartVertRetrace */
		vga_state.StartVertRetrace = inb_p(vga_video_port_val);
		outb_p(0x11,vga_video_port_reg);	/* EndVertRetrace */
		vga_state.EndVertRetrace = inb_p(vga_video_port_val);
		outb_p(0x17,vga_video_port_reg);	/* ModeControl */
		vga_state.ModeControl = inb_p(vga_video_port_val);
		outb_p(0x01,seq_port_reg);		/* ClockingMode */
		vga_state.ClockingMode = inb_p(seq_port_val);
	}

	/* assure that video is enabled */
	/* "0x20" is VIDEO_ENABLE_bit in register 01 of sequencer */
	cli();
	outb_p(0x01,seq_port_reg);
	outb_p(vga_state.ClockingMode | 0x20,seq_port_val);

	/* test for vertical retrace in process.... */
	if ((vga_state.CrtMiscIO & 0x80) == 0x80)
		outb_p(vga_state.CrtMiscIO & 0xef,video_misc_wr);

	/*
	 * Set <End of vertical retrace> to minimum (0) and
	 * <Start of vertical Retrace> to maximum (incl. overflow)
	 * Result: turn off vertical sync (VSync) pulse.
	 */
	if (mode & VESA_VSYNC_SUSPEND) {
		outb_p(0x10,vga_video_port_reg);	/* StartVertRetrace */
		outb_p(0xff,vga_video_port_val); 	/* maximum value */
		outb_p(0x11,vga_video_port_reg);	/* EndVertRetrace */
		outb_p(0x40,vga_video_port_val);	/* minimum (bits 0..3)  */
		outb_p(0x07,vga_video_port_reg);	/* Overflow */
		outb_p(vga_state.Overflow | 0x84,vga_video_port_val); /* bits 9,10 of vert. retrace */
	}

	if (mode & VESA_HSYNC_SUSPEND) {
		/*
		 * Set <End of horizontal retrace> to minimum (0) and
		 *  <Start of horizontal Retrace> to maximum
		 * Result: turn off horizontal sync (HSync) pulse.
		 */
		outb_p(0x04,vga_video_port_reg);	/* StartHorizRetrace */
		outb_p(0xff,vga_video_port_val);	/* maximum */
		outb_p(0x05,vga_video_port_reg);	/* EndHorizRetrace */
		outb_p(0x00,vga_video_port_val);	/* minimum (0) */
	}

	/* restore both index registers */
	outb_p(vga_state.SeqCtrlIndex,seq_port_reg);
	outb_p(vga_state.CrtCtrlIndex,vga_video_port_reg);
	sti();
}

static void vga_vesa_unblank(void)
{
	/* restore original values of VGA controller registers */
	cli();
	outb_p(vga_state.CrtMiscIO,video_misc_wr);

	outb_p(0x00,vga_video_port_reg);		/* HorizontalTotal */
	outb_p(vga_state.HorizontalTotal,vga_video_port_val);
	outb_p(0x01,vga_video_port_reg);		/* HorizDisplayEnd */
	outb_p(vga_state.HorizDisplayEnd,vga_video_port_val);
	outb_p(0x04,vga_video_port_reg);		/* StartHorizRetrace */
	outb_p(vga_state.StartHorizRetrace,vga_video_port_val);
	outb_p(0x05,vga_video_port_reg);		/* EndHorizRetrace */
	outb_p(vga_state.EndHorizRetrace,vga_video_port_val);
	outb_p(0x07,vga_video_port_reg);		/* Overflow */
	outb_p(vga_state.Overflow,vga_video_port_val);
	outb_p(0x10,vga_video_port_reg);		/* StartVertRetrace */
	outb_p(vga_state.StartVertRetrace,vga_video_port_val);
	outb_p(0x11,vga_video_port_reg);		/* EndVertRetrace */
	outb_p(vga_state.EndVertRetrace,vga_video_port_val);
	outb_p(0x17,vga_video_port_reg);		/* ModeControl */
	outb_p(vga_state.ModeControl,vga_video_port_val);
	outb_p(0x01,seq_port_reg);		/* ClockingMode */
	outb_p(vga_state.ClockingMode,seq_port_val);

	/* restore index/control registers */
	outb_p(vga_state.SeqCtrlIndex,seq_port_reg);
	outb_p(vga_state.CrtCtrlIndex,vga_video_port_reg);
	sti();
}

static void vga_pal_blank(void)
{
	int i;

	for (i=0; i<16; i++) {
		outb_p (i, dac_reg) ;
		outb_p (0, dac_val) ;
		outb_p (0, dac_val) ;
		outb_p (0, dac_val) ;
	}
}

static int vgacon_blank(struct vc_data *c, int blank)
{
	switch (blank) {
	case 0:				/* Unblank */
		if (vga_vesa_blanked) {
			vga_vesa_unblank();
			vga_vesa_blanked = 0;
		}
		if (vga_palette_blanked) {
			vga_set_palette(c, color_table);
			vga_palette_blanked = 0;
			return 0;
		}
		vga_is_gfx = 0;
		/* Tell console.c that it has to restore the screen itself */
		return 1;
	case 1:				/* Normal blanking */
		if (vga_video_type == VIDEO_TYPE_VGAC) {
			vga_pal_blank();
			vga_palette_blanked = 1;
			return 0;
		}
		vgacon_set_origin(c);
		scr_memsetw((void *)vga_vram_base, BLANK, c->vc_screenbuf_size);
		return 1;
	case -1:			/* Entering graphic mode */
		scr_memsetw((void *)vga_vram_base, BLANK, c->vc_screenbuf_size);
		vga_is_gfx = 1;
		return 1;
	default:			/* VESA blanking */
		if (vga_video_type == VIDEO_TYPE_VGAC) {
			vga_vesa_blank(blank-1);
			vga_vesa_blanked = blank;
		}
		return 0;
	}
}

/*
 * PIO_FONT support.
 *
 * The font loading code goes back to the codepage package by
 * Joel Hoffman (joel@wam.umd.edu). (He reports that the original
 * reference is: "From: p. 307 of _Programmer's Guide to PC & PS/2
 * Video Systems_ by Richard Wilton. 1987.  Microsoft Press".)
 *
 * Change for certain monochrome monitors by Yury Shevchuck
 * (sizif@botik.yaroslavl.su).
 */

#ifdef CAN_LOAD_EGA_FONTS

#define colourmap 0xa0000
/* Pauline Middelink <middelin@polyware.iaf.nl> reports that we
   should use 0xA0000 for the bwmap as well.. */
#define blackwmap 0xa0000
#define cmapsz 8192

static int
vgacon_do_font_op(char *arg, int set, int ch512)
{
	int i;
	char *charmap;
	int beg;
	unsigned short video_port_status = vga_video_port_reg + 6;
	int font_select = 0x00;

	if (vga_video_type != VIDEO_TYPE_EGAM) {
		charmap = (char *)VGA_MAP_MEM(colourmap);
		beg = 0x0e;
#ifdef VGA_CAN_DO_64KB
		if (vga_video_type == VIDEO_TYPE_VGAC)
			beg = 0x06;
#endif
	} else {
		charmap = (char *)VGA_MAP_MEM(blackwmap);
		beg = 0x0a;
	}
	
#ifdef BROKEN_GRAPHICS_PROGRAMS
	/*
	 * All fonts are loaded in slot 0 (0:1 for 512 ch)
	 */

	if (!arg)
		return -EINVAL;		/* Return to default font not supported */

	vga_font_is_default = 0;
	font_select = ch512 ? 0x04 : 0x00;
#else	
	/*
	 * The default font is kept in slot 0 and is never touched.
	 * A custom font is loaded in slot 2 (256 ch) or 2:3 (512 ch)
	 */

	if (set) {
		vga_font_is_default = !arg;
		if (!arg)
			ch512 = 0;		/* Default font is always 256 */
		font_select = arg ? (ch512 ? 0x0e : 0x0a) : 0x00;
	}

	if ( !vga_font_is_default )
		charmap += 4*cmapsz;
#endif

	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x04, seq_port_val );   /* CPU writes only to map 2 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x07, seq_port_val );   /* Sequential addressing */
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* Clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x02, gr_port_val );    /* select map 2 */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* disable odd-even addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( 0x00, gr_port_val );    /* map start at A000:0000 */
	sti();
	
	if (arg) {
		if (set)
			for (i=0; i<cmapsz ; i++)
				vga_writeb(arg[i], charmap + i);
		else
			for (i=0; i<cmapsz ; i++)
				arg[i] = vga_readb(charmap + i);

		/*
		 * In 512-character mode, the character map is not contiguous if
		 * we want to remain EGA compatible -- which we do
		 */

		if (ch512) {
			charmap += 2*cmapsz;
			arg += cmapsz;
			if (set)
				for (i=0; i<cmapsz ; i++)
					vga_writeb(arg[i], charmap+i);
			else
				for (i=0; i<cmapsz ; i++)
					arg[i] = vga_readb(charmap+i);
		}
	}
	
	cli();
	outb_p( 0x00, seq_port_reg );   /* First, the sequencer */
	outb_p( 0x01, seq_port_val );   /* Synchronous reset */
	outb_p( 0x02, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* CPU writes to maps 0 and 1 */
	outb_p( 0x04, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* odd-even addressing */
	if (set) {
		outb_p( 0x03, seq_port_reg ); /* Character Map Select */
		outb_p( font_select, seq_port_val );
	}
	outb_p( 0x00, seq_port_reg );
	outb_p( 0x03, seq_port_val );   /* clear synchronous reset */

	outb_p( 0x04, gr_port_reg );    /* Now, the graphics controller */
	outb_p( 0x00, gr_port_val );    /* select map 0 for CPU */
	outb_p( 0x05, gr_port_reg );
	outb_p( 0x10, gr_port_val );    /* enable even-odd addressing */
	outb_p( 0x06, gr_port_reg );
	outb_p( beg, gr_port_val );     /* map starts at b800:0 or b000:0 */

	/* if 512 char mode is already enabled don't re-enable it. */
	if ((set)&&(ch512!=vga_512_chars)) {	/* attribute controller */
		int i;
		for(i=0; i<MAX_NR_CONSOLES; i++) {
			struct vc_data *c = vc_cons[i].d;
			if (c && c->vc_sw == &vga_con)
				c->vc_hi_font_mask = ch512 ? 0x0800 : 0;
		}
		vga_512_chars=ch512;
		/* 256-char: enable intensity bit
		   512-char: disable intensity bit */
		inb_p( video_port_status );	/* clear address flip-flop */
		outb_p ( 0x12, attrib_port ); /* color plane enable register */
		outb_p ( ch512 ? 0x07 : 0x0f, attrib_port );
		/* Wilton (1987) mentions the following; I don't know what
		   it means, but it works, and it appears necessary */
		inb_p( video_port_status );
		outb_p ( 0x20, attrib_port );
	}
	sti();

	return 0;
}

/*
 * Adjust the screen to fit a font of a certain height
 */
static int
vgacon_adjust_height(unsigned fontheight)
{
	int rows, maxscan;
	unsigned char ovr, vde, fsr;

	if (fontheight == vga_video_font_height)
		return 0;

	vga_video_font_height = video_font_height = fontheight;

	rows = video_scan_lines/fontheight;	/* Number of video rows we end up with */
	maxscan = rows*fontheight - 1;		/* Scan lines to actually display-1 */

	/* Reprogram the CRTC for the new font size
	   Note: the attempt to read the overflow register will fail
	   on an EGA, but using 0xff for the previous value appears to
	   be OK for EGA text modes in the range 257-512 scan lines, so I
	   guess we don't need to worry about it.

	   The same applies for the spill bits in the font size and cursor
	   registers; they are write-only on EGA, but it appears that they
	   are all don't care bits on EGA, so I guess it doesn't matter. */

	cli();
	outb_p( 0x07, vga_video_port_reg );		/* CRTC overflow register */
	ovr = inb_p(vga_video_port_val);
	outb_p( 0x09, vga_video_port_reg );		/* Font size register */
	fsr = inb_p(vga_video_port_val);
	sti();

	vde = maxscan & 0xff;			/* Vertical display end reg */
	ovr = (ovr & 0xbd) +			/* Overflow register */
	      ((maxscan & 0x100) >> 7) +
	      ((maxscan & 0x200) >> 3);
	fsr = (fsr & 0xe0) + (fontheight-1);    /*  Font size register */

	cli();
	outb_p( 0x07, vga_video_port_reg );		/* CRTC overflow register */
	outb_p( ovr, vga_video_port_val );
	outb_p( 0x09, vga_video_port_reg );		/* Font size */
	outb_p( fsr, vga_video_port_val );
	outb_p( 0x12, vga_video_port_reg );		/* Vertical display limit */
	outb_p( vde, vga_video_port_val );
	sti();

	vc_resize_all(rows, 0);			/* Adjust console size */
	return 0;
}

static int vgacon_font_op(struct vc_data *c, struct console_font_op *op)
{
	int rc;

	if (vga_video_type < VIDEO_TYPE_EGAM)
		return -EINVAL;

	if (op->op == KD_FONT_OP_SET) {
		if (op->width != 8 || (op->charcount != 256 && op->charcount != 512))
			return -EINVAL;
		rc = vgacon_do_font_op(op->data, 1, op->charcount == 512);
		if (!rc && !(op->flags & KD_FONT_FLAG_DONT_RECALC))
			rc = vgacon_adjust_height(op->height);
	} else if (op->op == KD_FONT_OP_GET) {
		op->width = 8;
		op->height = vga_video_font_height;
		op->charcount = vga_512_chars ? 512 : 256;
		if (!op->data) return 0;
		rc = vgacon_do_font_op(op->data, 0, 0);
	} else
		rc = -ENOSYS;
	return rc;
}

#else

static int vgacon_font_op(struct vc_data *c, struct console_font_op *op)
{
	return -ENOSYS;
}

#endif

static int vgacon_scrolldelta(struct vc_data *c, int lines)
{
	if (!lines)			/* Turn scrollback off */
		c->vc_visible_origin = c->vc_origin;
	else {
		int vram_size = vga_vram_end - vga_vram_base;
		int margin = c->vc_size_row * 4;
		int ul, we, p, st;

		if (vga_rolled_over > (c->vc_scr_end - vga_vram_base) + margin) {
			ul = c->vc_scr_end - vga_vram_base;
			we = vga_rolled_over + c->vc_size_row;
		} else {
			ul = 0;
			we = vram_size;
		}
		p = (c->vc_visible_origin - vga_vram_base - ul + we) % we + lines * c->vc_size_row;
		st = (c->vc_origin - vga_vram_base - ul + we) % we;
		if (p < margin)
			p = 0;
		if (p > st - margin)
			p = st;
		c->vc_visible_origin = vga_vram_base + (p + ul) % we;
	}
	vga_set_mem_top(c);
	return 1;
}

static int vgacon_set_origin(struct vc_data *c)
{
	if (vga_is_gfx ||	/* We don't play origin tricks in graphic modes */
	    (console_blanked && !vga_palette_blanked))	/* Nor we write to blanked screens */
		return 0;
	c->vc_origin = c->vc_visible_origin = vga_vram_base;
	vga_set_mem_top(c);
	vga_rolled_over = 0;
	return 1;
}

static void vgacon_save_screen(struct vc_data *c)
{
	static int vga_bootup_console = 0;

	if (!vga_bootup_console) {
		/* This is a gross hack, but here is the only place we can
		 * set bootup console parameters without messing up generic
		 * console initialization routines.
		 */
		vga_bootup_console = 1;
		c->vc_x = ORIG_X;
		c->vc_y = ORIG_Y;
	}
	if (!vga_is_gfx)
		scr_memcpyw_from((u16 *) c->vc_screenbuf, (u16 *) c->vc_origin, c->vc_screenbuf_size);
}

static int vgacon_scroll(struct vc_data *c, int t, int b, int dir, int lines)
{
	unsigned long oldo;
	unsigned int delta;
	
	if (t || b != c->vc_rows || vga_is_gfx)
		return 0;

	if (c->vc_origin != c->vc_visible_origin)
		vgacon_scrolldelta(c, 0);

	if (!vga_hardscroll_enabled || lines >= c->vc_rows/2)
		return 0;

	oldo = c->vc_origin;
	delta = lines * c->vc_size_row;
	if (dir == SM_UP) {
		if (c->vc_scr_end + delta >= vga_vram_end) {
			scr_memcpyw((u16 *)vga_vram_base,
				    (u16 *)(oldo + delta),
				    c->vc_screenbuf_size - delta);
			c->vc_origin = vga_vram_base;
			vga_rolled_over = oldo - vga_vram_base;
		} else
			c->vc_origin += delta;
		scr_memsetw((u16 *)(c->vc_origin + c->vc_screenbuf_size - delta), c->vc_video_erase_char, delta);
	} else {
		if (oldo - delta < vga_vram_base) {
			scr_memmovew((u16 *)(vga_vram_end - c->vc_screenbuf_size + delta),
				     (u16 *)oldo,
				     c->vc_screenbuf_size - delta);
			c->vc_origin = vga_vram_end - c->vc_screenbuf_size;
			vga_rolled_over = 0;
		} else
			c->vc_origin -= delta;
		c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
		scr_memsetw((u16 *)(c->vc_origin), c->vc_video_erase_char, delta);
	}
	c->vc_scr_end = c->vc_origin + c->vc_screenbuf_size;
	c->vc_visible_origin = c->vc_origin;
	vga_set_mem_top(c);
	c->vc_pos = (c->vc_pos - oldo) + c->vc_origin;
	return 1;
}


/*
 *  The console `switch' structure for the VGA based console
 */

static int vgacon_dummy(struct vc_data *c)
{
	return 0;
}

#define DUMMY (void *) vgacon_dummy

struct consw vga_con = {
	vgacon_startup,
	vgacon_init,
	vgacon_deinit,
	DUMMY,				/* con_clear */
	DUMMY,				/* con_putc */
	DUMMY,				/* con_putcs */
	vgacon_cursor,
	vgacon_scroll,			/* con_scroll */
	DUMMY,				/* con_bmove */
	vgacon_switch,
	vgacon_blank,
	vgacon_font_op,
	vgacon_set_palette,
	vgacon_scrolldelta,
	vgacon_set_origin,
	vgacon_save_screen,
	vgacon_build_attr,
	vgacon_invert_region
};
