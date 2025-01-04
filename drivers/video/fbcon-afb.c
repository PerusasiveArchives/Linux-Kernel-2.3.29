/*
 *  linux/drivers/video/afb.c -- Low level frame buffer operations for
 *				 bitplanes � la Amiga
 *
 *	Created 5 Apr 1997 by Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive for
 *  more details.
 */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/string.h>
#include <linux/fb.h>

#include <video/fbcon.h>
#include <video/fbcon-afb.h>


    /*
     *  Bitplanes � la Amiga
     */

static u8 expand_table[1024] = {
    /*  bg = fg = 0 */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    /* bg = 0, fg = 1 */
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
    0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
    0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
    0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
    0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
    0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
    0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
    0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
    0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
    0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
    0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
    0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
    0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
    0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
    0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
    0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff,
    /* bg = 1, fg = 0 */
    0xff, 0xfe, 0xfd, 0xfc, 0xfb, 0xfa, 0xf9, 0xf8,
    0xf7, 0xf6, 0xf5, 0xf4, 0xf3, 0xf2, 0xf1, 0xf0,
    0xef, 0xee, 0xed, 0xec, 0xeb, 0xea, 0xe9, 0xe8,
    0xe7, 0xe6, 0xe5, 0xe4, 0xe3, 0xe2, 0xe1, 0xe0,
    0xdf, 0xde, 0xdd, 0xdc, 0xdb, 0xda, 0xd9, 0xd8,
    0xd7, 0xd6, 0xd5, 0xd4, 0xd3, 0xd2, 0xd1, 0xd0,
    0xcf, 0xce, 0xcd, 0xcc, 0xcb, 0xca, 0xc9, 0xc8,
    0xc7, 0xc6, 0xc5, 0xc4, 0xc3, 0xc2, 0xc1, 0xc0,
    0xbf, 0xbe, 0xbd, 0xbc, 0xbb, 0xba, 0xb9, 0xb8,
    0xb7, 0xb6, 0xb5, 0xb4, 0xb3, 0xb2, 0xb1, 0xb0,
    0xaf, 0xae, 0xad, 0xac, 0xab, 0xaa, 0xa9, 0xa8,
    0xa7, 0xa6, 0xa5, 0xa4, 0xa3, 0xa2, 0xa1, 0xa0,
    0x9f, 0x9e, 0x9d, 0x9c, 0x9b, 0x9a, 0x99, 0x98,
    0x97, 0x96, 0x95, 0x94, 0x93, 0x92, 0x91, 0x90,
    0x8f, 0x8e, 0x8d, 0x8c, 0x8b, 0x8a, 0x89, 0x88,
    0x87, 0x86, 0x85, 0x84, 0x83, 0x82, 0x81, 0x80,
    0x7f, 0x7e, 0x7d, 0x7c, 0x7b, 0x7a, 0x79, 0x78,
    0x77, 0x76, 0x75, 0x74, 0x73, 0x72, 0x71, 0x70,
    0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x69, 0x68,
    0x67, 0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x60,
    0x5f, 0x5e, 0x5d, 0x5c, 0x5b, 0x5a, 0x59, 0x58,
    0x57, 0x56, 0x55, 0x54, 0x53, 0x52, 0x51, 0x50,
    0x4f, 0x4e, 0x4d, 0x4c, 0x4b, 0x4a, 0x49, 0x48,
    0x47, 0x46, 0x45, 0x44, 0x43, 0x42, 0x41, 0x40,
    0x3f, 0x3e, 0x3d, 0x3c, 0x3b, 0x3a, 0x39, 0x38,
    0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30,
    0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28,
    0x27, 0x26, 0x25, 0x24, 0x23, 0x22, 0x21, 0x20,
    0x1f, 0x1e, 0x1d, 0x1c, 0x1b, 0x1a, 0x19, 0x18,
    0x17, 0x16, 0x15, 0x14, 0x13, 0x12, 0x11, 0x10,
    0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
    0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00,
    /* bg = fg = 1 */
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

void fbcon_afb_setup(struct display *p)
{
    if (p->line_length)
	p->next_line = p->line_length;
    else
	p->next_line = p->var.xres_virtual>>3;
    p->next_plane = p->var.yres_virtual*p->next_line;
}

void fbcon_afb_bmove(struct display *p, int sy, int sx, int dy, int dx,
		     int height, int width)
{
    u8 *src, *dest, *src0, *dest0;
    u_short i, j;

    if (sx == 0 && dx == 0 && width == p->next_line) {
	src = p->screen_base+sy*fontheight(p)*width;
	dest = p->screen_base+dy*fontheight(p)*width;
	i = p->var.bits_per_pixel;
	do {
	    fb_memmove(dest, src, height*fontheight(p)*width);
	    src += p->next_plane;
	    dest += p->next_plane;
	} while (--i);
    } else if (dy <= sy) {
	src0 = p->screen_base+sy*fontheight(p)*p->next_line+sx;
	dest0 = p->screen_base+dy*fontheight(p)*p->next_line+dx;
	i = p->var.bits_per_pixel;
	do {
	    src = src0;
	    dest = dest0;
	    j = height*fontheight(p);
	    do {
	        fb_memmove(dest, src, width);
	        src += p->next_line;
	        dest += p->next_line;
	    } while (--j);
	    src0 += p->next_plane;
	    dest0 += p->next_plane;
	} while (--i);
    } else {
	src0 = p->screen_base+(sy+height)*fontheight(p)*p->next_line+sx;
	dest0 = p->screen_base+(dy+height)*fontheight(p)*p->next_line+dx;
	i = p->var.bits_per_pixel;
	do {
	    src = src0;
	    dest = dest0;
	    j = height*fontheight(p);
	    do {
	        src -= p->next_line;
	        dest -= p->next_line;
	        fb_memmove(dest, src, width);
	    } while (--j);
	    src0 += p->next_plane;
	    dest0 += p->next_plane;
	} while (--i);
    }
}

void fbcon_afb_clear(struct vc_data *conp, struct display *p, int sy, int sx,
		     int height, int width)
{
    u8 *dest, *dest0;
    u_short i, j;
    int bg;

    dest0 = p->screen_base+sy*fontheight(p)*p->next_line+sx;

    bg = attr_bgcol_ec(p,conp);
    i = p->var.bits_per_pixel;
    do {
	dest = dest0;
	j = height*fontheight(p);
	do {
	    if (bg & 1)
	        fb_memset255(dest, width);
	    else
	        fb_memclear(dest, width);
	    dest += p->next_line;
	} while (--j);
	bg >>= 1;
	dest0 += p->next_plane;
    } while (--i);
}

void fbcon_afb_putc(struct vc_data *conp, struct display *p, int c, int yy,
		    int xx)
{
    u8 *dest, *dest0, *cdat, *cdat0, *expand;
    u_short i, j;
    int fg, bg;

    dest0 = p->screen_base+yy*fontheight(p)*p->next_line+xx;
    cdat0 = p->fontdata+(c&p->charmask)*fontheight(p);
    fg = attr_fgcol(p,c);
    bg = attr_bgcol(p,c);

    i = p->var.bits_per_pixel;
    do {
	dest = dest0;
	cdat = cdat0;
	expand = expand_table;
	if (bg & 1)
	    expand += 512;
	if (fg & 1)
	    expand += 256;
	j = fontheight(p);
	do {
	    *dest = expand[*cdat++];
	    dest += p->next_line;
	} while (--j);
	bg >>= 1;
	fg >>= 1;
	dest0 += p->next_plane;
    } while (--i);
}

    /*
     *  I've split the console character loop in two parts
     *  (cfr. fbcon_putcs_ilbm())
     */

void fbcon_afb_putcs(struct vc_data *conp, struct display *p, 
		     const unsigned short *s, int count, int yy, int xx)
{
    u8 *dest, *dest0, *dest1, *expand;
    u8 *cdat1, *cdat2, *cdat3, *cdat4, *cdat10, *cdat20, *cdat30, *cdat40;
    u_short i, j;
    u16 c1, c2, c3, c4;
    int fg0, bg0, fg, bg;

    dest0 = p->screen_base+yy*fontheight(p)*p->next_line+xx;
    fg0 = attr_fgcol(p, scr_readw(s));
    bg0 = attr_bgcol(p, scr_readw(s));

    while (count--)
	if (xx&3 || count < 3) {	/* Slow version */
	    c1 = scr_readw(s++) & p->charmask;
	    dest1 = dest0++;
	    xx++;

	    cdat10 = p->fontdata+c1*fontheight(p);
	    fg = fg0;
	    bg = bg0;

	    i = p->var.bits_per_pixel;
	    do {
	        dest = dest1;
	        cdat1 = cdat10;
		expand = expand_table;
		if (bg & 1)
		    expand += 512;
		if (fg & 1)
		    expand += 256;
		j = fontheight(p);
		do {
		    *dest = expand[*cdat1++];
		    dest += p->next_line;
	        } while (--j);
	        bg >>= 1;
	        fg >>= 1;
		dest1 += p->next_plane;
	    } while (--i);
	} else {			/* Fast version */
	    c1 = scr_readw(&s[0]) & p->charmask;
	    c2 = scr_readw(&s[1]) & p->charmask;
	    c3 = scr_readw(&s[2]) & p->charmask;
	    c4 = scr_readw(&s[3]) & p->charmask;

	    dest1 = dest0;
	    cdat10 = p->fontdata+c1*fontheight(p);
	    cdat20 = p->fontdata+c2*fontheight(p);
	    cdat30 = p->fontdata+c3*fontheight(p);
	    cdat40 = p->fontdata+c4*fontheight(p);
	    fg = fg0;
	    bg = bg0;

	    i = p->var.bits_per_pixel;
	    do {
	        dest = dest1;
	        cdat1 = cdat10;
	        cdat2 = cdat20;
	        cdat3 = cdat30;
	        cdat4 = cdat40;
		expand = expand_table;
		if (bg & 1)
		    expand += 512;
		if (fg & 1)
		    expand += 256;
		j = fontheight(p);
	        do {
#if defined(__BIG_ENDIAN)
		    *(u32 *)dest = expand[*cdat1++]<<24 |
				   expand[*cdat2++]<<16 |
				   expand[*cdat3++]<<8 |
				   expand[*cdat4++];
#elif defined(__LITTLE_ENDIAN)
		    *(u32 *)dest = expand[*cdat1++] |
				   expand[*cdat2++]<<8 |
				   expand[*cdat3++]<<16 |
				   expand[*cdat4++]<<24;
#else
#error FIXME: No endianness??
#endif
		    dest += p->next_line;
	        } while (--j);
	        bg >>= 1;
	        fg >>= 1;
		dest1 += p->next_plane;
	    } while (--i);
	    s += 4;
	    dest0 += 4;
	    xx += 4;
	    count -= 3;
	}
}

void fbcon_afb_revc(struct display *p, int xx, int yy)
{
    u8 *dest, *dest0;
    u_short i, j;
    int mask;

    dest0 = p->screen_base+yy*fontheight(p)*p->next_line+xx;
    mask = p->fgcol ^ p->bgcol;

    /*
     *  This should really obey the individual character's
     *  background and foreground colors instead of simply
     *  inverting.
     */

    i = p->var.bits_per_pixel;
    do {
	if (mask & 1) {
	    dest = dest0;
	    j = fontheight(p);
	    do {
	        *dest = ~*dest;
		dest += p->next_line;
	    } while (--j);
	}
	mask >>= 1;
	dest0 += p->next_plane;
    } while (--i);
}


    /*
     *  `switch' for the low level operations
     */

struct display_switch fbcon_afb = {
    fbcon_afb_setup, fbcon_afb_bmove, fbcon_afb_clear, fbcon_afb_putc,
    fbcon_afb_putcs, fbcon_afb_revc, NULL, NULL, NULL, FONTWIDTH(8)
};


#ifdef MODULE
int init_module(void)
{
    return 0;
}

void cleanup_module(void)
{}
#endif /* MODULE */


    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(fbcon_afb);
EXPORT_SYMBOL(fbcon_afb_setup);
EXPORT_SYMBOL(fbcon_afb_bmove);
EXPORT_SYMBOL(fbcon_afb_clear);
EXPORT_SYMBOL(fbcon_afb_putc);
EXPORT_SYMBOL(fbcon_afb_putcs);
EXPORT_SYMBOL(fbcon_afb_revc);
