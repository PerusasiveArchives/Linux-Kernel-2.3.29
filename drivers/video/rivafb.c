/*
 * linux/drivers/video/rivafb.c - nVidia RIVA 128/TNT/TNT2 fb driver
 *
 * Copyright 1999 Jeff Garzik <jgarzik@pobox.com>
 *
 * Contributors:
 *
 *	ani joshi:  Lots of debugging and cleanup work, really helped
 *	get the driver going
 *
 * Initial template from skeletonfb.c, created 28 Dec 1997 by Geert Uytterhoeven
 * Includes riva_hw.c from nVidia, see copyright below.
 * KGI code provided the basis for state storage, init, and mode switching.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file README.legal in the main directory of this archive
 * for more details.
 */

/* version number of this driver */
#define RIVAFB_VERSION "0.6.5"

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/selection.h>
#include <linux/tty.h>
#include <linux/malloc.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/pci.h>

#include <video/fbcon.h>

#include "riva_hw.h"
#include "nv4ref.h"
#include "nvreg.h"
#include "vga.h"
#include <video/fbcon-cfb4.h>
#include <video/fbcon-cfb8.h>
#include <video/fbcon-cfb16.h>
#include <video/fbcon-cfb32.h>

#ifndef CONFIG_PCI /* sanity check */
#error This driver requires PCI support.
#endif

/*****************************************************************
 *
 * various helpful macros and constants
 *
 */

/* #define RIVAFBDEBUG */
#ifdef RIVAFBDEBUG
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#ifndef RIVA_NDEBUG
#define assert(expr) \
	if(!(expr)) { \
        printk( "Assertion failed! %s,%s,%s,line=%d\n",\
        #expr,__FILE__,__FUNCTION__,__LINE__); \
	*(int*)0 = 0; \
        }
#else
#define assert(expr)
#endif

/* GGI compatibility macros */
#define io_out8 outb
#define io_in8 inb
#define FatalError panic
#define NUM_SEQ_REGS 0x05
#define NUM_CRT_REGS 0x41
#define NUM_GRC_REGS 0x09
#define NUM_ATC_REGS 0x15

/* max number of VGA controllers we scan for */
#define RIVA_MAX_VGA_CONTROLLERS 8

#define PFX "rivafb: "

#define CNVT_TOHW(val,width) ((((val)<<(width))+0x7FFF-(val))>>16)

/* macro that allows you to set overflow bits */
#define SetBitField(value,from,to) SetBF(to,GetBF(value,from))
#define SetBit(n) (1<<(n))
#define Set8Bits(value) ((value)&0xff)

struct riva_chip_info {
	const char *name;
	unsigned arch_rev;
	unsigned short vendor;
	unsigned short device;
};


static const struct riva_chip_info riva_pci_probe_list[] =
{
	{"RIVA-128", 3, PCI_VENDOR_ID_NVIDIA_SGS, PCI_DEVICE_ID_NVIDIA_SGS_RIVA128},
	{"RIVA-TNT", 4, PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT},
	{"RIVA-TNT2", 5, PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_TNT2},
	{"RIVA-TNT2", 5, PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_UTNT2},
	{"RIVA-TNT2", 5, PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_VTNT2},
	{"RIVA-TNT2", 5, PCI_VENDOR_ID_NVIDIA, PCI_DEVICE_ID_NVIDIA_ITNT2},
	{NULL, 0, 0, 0}
};


/* holds the state of the VGA core and extended Riva hw state from riva_hw.c.
 * From KGI originally. */
struct riva_regs {
	u8 attr[0x15];
	u8 crtc[0x41];
	u8 gra[0x09];
	u8 seq[0x05];
	u8 misc_output;
	RIVA_HW_STATE ext;
};


/*
 * describes the state of a Riva board
 */
struct rivafb_par {
	struct riva_regs state; /* state of hw board */
	__u32 visual;	/* FB_VISUAL_xxx */
	unsigned depth; /* bpp of current mode */
};

typedef struct {
	unsigned char red, green, blue, transp;
} riva_cfb8_cmap_t;



struct rivafb_info;
struct rivafb_info {
	struct fb_info info;	/* kernel framebuffer info */

	RIVA_HW_INST riva;	/* interface to riva_hw.c */

	const char *drvr_name;	/* Riva hardware board type */

	unsigned long ctrl_base_phys; /* physical control register base addr */
	unsigned long fb_base_phys; /* physical framebuffer base addr */

	caddr_t ctrl_base;	/* virtual control register base addr */
	caddr_t fb_base;	/* virtual framebuffer base addr */

	unsigned ram_amount;	/* amount of RAM on card, in megabytes */
	unsigned dclk_max;	/* max DCLK */

	struct riva_regs initial_state;		/* initial startup video mode */

	struct display disp;
	int currcon;
	struct display *currcon_display;

	struct rivafb_info *next;

	struct pci_dev *pd;		/* pointer to board's pci info */
	unsigned base0_region_size;	/* size of control register region */
	unsigned base1_region_size;	/* size of framebuffer region */

#ifdef FBCON_HAS_CFB8
	riva_cfb8_cmap_t palette[256];	/* VGA DAC palette cache */
#endif

#if defined(FBCON_HAS_CFB16) || defined(FBCON_HAS_CFB32)
        union {
#ifdef FBCON_HAS_CFB16
                u_int16_t       cfb16[16];
#endif
#ifdef FBCON_HAS_CFB32
                u_int32_t       cfb32[16];
#endif
        } con_cmap;
#endif /* FBCON_HAS_CFB16 | FBCON_HAS_CFB32 */
};

/* ------------------- global variables ------------------------ */


static struct rivafb_info *riva_boards = NULL;

/* command line data, set in rivafb_setup() */
static char fontname[40] __initdata = { 0 };
static char noaccel __initdata = 0;		/* unused */
#ifndef MODULE
static const char *mode_option __initdata = NULL;
#endif


/* ------------------- prototypes ------------------------------ */

int rivafb_open (struct fb_info *info, int user);
int rivafb_release (struct fb_info *info, int user);
int rivafb_get_fix (struct fb_fix_screeninfo *fix, int con,
		  struct fb_info *info);
int rivafb_get_var (struct fb_var_screeninfo *var, int con,
		  struct fb_info *info);
int rivafb_set_var (struct fb_var_screeninfo *var, int con,
		  struct fb_info *info);
int rivafb_get_cmap (struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info);
int rivafb_set_cmap (struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info);
int rivafb_pan_display (struct fb_var_screeninfo *var, int con,
		      struct fb_info *info);
int rivafb_ioctl (struct inode *inode, struct file *file, unsigned int cmd,
		unsigned long arg, int con, struct fb_info *info);
int rivafb_switch(int con, struct fb_info *info);
void rivafb_blank (int blank, struct fb_info *info);

static void riva_load_video_mode (struct rivafb_info *rivainfo,
				struct fb_var_screeninfo *video_mode);
static int riva_getcolreg (unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 struct fb_info *info);
static int riva_setcolreg (unsigned regno, unsigned red, unsigned green,
		    unsigned blue, unsigned transp,
		    struct fb_info *info);
static int riva_get_cmap_len(const struct fb_var_screeninfo *var);

static u32 riva_pci_region_size (struct pci_dev *pd, unsigned baseaddr);

static void riva_pci_iounmap (struct rivafb_info *rinfo);
static int riva_pci_register (struct pci_dev *pd, const struct riva_chip_info *rci);
static int riva_pci_register_devices (void);
static int riva_set_fbinfo (struct rivafb_info *rinfo);

static
void riva_save_state(struct rivafb_info *rinfo, struct riva_regs *regs);
static
void riva_load_state(struct rivafb_info *rinfo, struct riva_regs *regs);
static
struct rivafb_info *riva_board_list_add(struct rivafb_info *board_list,
				 struct rivafb_info *new_node);

#if 0
static void riva_pci_region_init (struct pci_dev *pd, struct rivafb_info *rinfo);
#endif

static void riva_wclut (unsigned char regnum, unsigned char red,
	    		unsigned char green, unsigned char blue);




/* kernel interface */
static struct fb_ops riva_fb_ops =
{
	rivafb_open,
	rivafb_release,
	rivafb_get_fix,
	rivafb_get_var,
	rivafb_set_var,
	rivafb_get_cmap,
	rivafb_set_cmap,
	rivafb_pan_display,
	rivafb_ioctl
};




/* from GGI */
static const struct riva_regs reg_template =
{
	{ 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, /* ATTR */
	  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	  0x41, 0x01, 0x0F, 0x13, 0x00 },
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* CRT  */
	  0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE3, /* 0x10 */
	  0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x20 */
	  0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x30 */
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
          0x00,                                           /* 0x40 */
	},
	{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x05, 0x0F, /* GRA  */
	  0xFF },
	{ 0x03, 0x01, 0x0F, 0x00, 0x0E},                  /* SEQ  */
	0xEB						  /* MISC */
};



/* ------------------- general utility functions -------------------------- */

/**
 * riva_set_dispsw
 * @rivainfo: pointer to internal driver struct for a given Riva card
 *
 * DESCRIPTION:
 * Sets up console Low level operations depending on the current? color depth
 * of the display
 */

void riva_set_dispsw (struct rivafb_info *rinfo)
{
	struct display *disp = &rinfo->disp;

	DPRINTK ("ENTER\n");

	assert (rinfo != NULL);

	disp->dispsw_data = NULL;

	switch (disp->var.bits_per_pixel) {
#ifdef FBCON_HAS_MFB
	case 1:
		disp->dispsw = &fbcon_mfb;
		break;
#endif
#ifdef FBCON_HAS_CFB4
	case 4:
		disp->dispsw = &fbcon_cfb4;
		break;
#endif
#ifdef FBCON_HAS_CFB8
	case 8:
		disp->dispsw = &fbcon_cfb8;
		break;
#endif
#ifdef FBCON_HAS_CFB16
	case 15:
	case 16:
		disp->dispsw = &fbcon_cfb16;
		disp->dispsw_data = &rinfo->con_cmap.cfb16;
		break;
#endif
#ifdef FBCON_HAS_CFB24
	case 24:
		disp->dispsw = &fbcon_cfb24;
		disp->dispsw_data = rinfo->con_cmap.cfb24;
		break;
#endif
#ifdef FBCON_HAS_CFB32
	case 32:
		disp->dispsw = &fbcon_cfb32;
		disp->dispsw_data = rinfo->con_cmap.cfb32;
		break;
#endif
	default:
		DPRINTK ("Setting fbcon_dummy renderer\n");
		disp->dispsw = &fbcon_dummy;
	}

	DPRINTK ("EXIT\n");
}




static int riva_init_disp_var (struct rivafb_info *rinfo)
{
	if (mode_option)
		fb_find_mode(&rinfo->disp.var, &rinfo->info, mode_option,
	    		  NULL, 0, NULL, 8);
	else 
		fb_find_mode(&rinfo->disp.var, &rinfo->info,
			     "640x480-8@60", NULL, 0, NULL, 8);
	return 0;
}




static int __init riva_init_disp (struct rivafb_info *rinfo)
{
	struct fb_info *info;
	struct display *disp;

	DPRINTK("ENTER\n");

	assert (rinfo != NULL);

	info = &rinfo->info;
	disp = &rinfo->disp;

	info->disp = disp;

#warning FIXME: assure that disp->cmap is completely filled out

	disp->screen_base = rinfo->fb_base;
	disp->visual = FB_VISUAL_PSEUDOCOLOR;
	disp->type = FB_TYPE_PACKED_PIXELS;
	disp->type_aux = 0;
	disp->ypanstep = 1;
	disp->ywrapstep = 0;
	disp->next_line = disp->line_length =
		(disp->var.xres_virtual * disp->var.bits_per_pixel) >> 3;
	disp->can_soft_blank = 1;
	disp->inverse = 0;

	riva_set_dispsw(rinfo);

	rinfo->currcon_display = disp;

	if ((riva_init_disp_var (rinfo)) < 0) {	/* must be done last */
		DPRINTK("EXIT, returning -1\n");
		return -1;
	}

	DPRINTK("EXIT, returning 0\n");
	return 0;

}



static int __init riva_set_fbinfo (struct rivafb_info *rinfo)
{
	struct fb_info *info;

	assert (rinfo != NULL);

	info = &rinfo->info;

	strcpy (info->modename, rinfo->drvr_name);
	info->node = -1;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->fbops = &riva_fb_ops;

#warning FIXME: set monspecs to what???

	info->display_fg = NULL; /* FIXME: correct? */
	strncpy (info->fontname, fontname, sizeof (info->fontname));
	info->fontname[sizeof (info->fontname) - 1] = 0;

	info->changevar = NULL; /* FIXME: needed? */
	info->switch_con = rivafb_switch;
	info->updatevar = NULL; /* FIXME? */
	info->blank = rivafb_blank;

	if (riva_init_disp (rinfo) < 0)	/* must be done last */
		return -1;

	return 0;
}




/* ----------------------------- PCI bus ----------------------------- */




/**
 * riva_pci_region_size
 * @pd: pointer to PCI device to be configured
 *
 * DESCRIPTION:
 * Obtains from the PCI bus the size of the address space allocated to
 * the given card's base address zero region.
 */

static
u32 riva_pci_region_size (struct pci_dev *pd, unsigned baseaddr)
{
	return pd->resource[baseaddr].end - pd->resource[baseaddr].start + 1;
}




#if 0

/**
 * riva_pci_iomap
 * @nv:
 *
 * DESCRIPTION:
 */

static
void riva_pci_region_init (struct pci_dev *pd, struct rivafb_info *rinfo)
{
	unsigned long addr;

	assert (rinfo != NULL);
	assert (pd != NULL);

	rinfo->pd = pd;
	rinfo->base0_region_size = riva_pci_region_size (pd, PCI_BASE_ADDRESS_0);
	assert(rinfo->base0_region_size >= 0x00800000); /* from GGI */
	rinfo->base1_region_size = riva_pci_region_size (pd, PCI_BASE_ADDRESS_1);
	assert(rinfo->base0_region_size >= 0x01000000); /* from GGI */
	
	/* Get the base addresses */
	pcibios_read_config_dword(0, rinfo->pd->devfn, PCI_BASE_ADDRESS_0, 
                                  (unsigned int*)&addr);
#ifdef SHOULDUSETHAT
	/* This function should return an int. */
	if (!addr)  
	  return -ENXIO;
#endif
	
	rinfo->ctrl_base_phys = addr & PCI_BASE_ADDRESS_MEM_MASK;

	pcibios_read_config_dword(0, rinfo->pd->devfn, PCI_BASE_ADDRESS_1, 
                                  (unsigned int*)&addr);

	rinfo->fb_base_phys = addr & PCI_BASE_ADDRESS_MEM_MASK;

	rinfo->ctrl_base = ioremap (rinfo->ctrl_base_phys, rinfo->base0_region_size);
	assert (rinfo->ctrl_base != NULL);
	rinfo->fb_base = ioremap (rinfo->fb_base_phys, rinfo->base1_region_size);
	assert (rinfo->fb_base != NULL);

        rinfo->riva.EnableIRQ = 0;
        rinfo->riva.IO      = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
        rinfo->riva.PRAMDAC = (unsigned *)(rinfo->ctrl_base+0x00680000);
        rinfo->riva.PFB     = (unsigned *)(rinfo->ctrl_base+0x00100000);
        rinfo->riva.PFIFO   = (unsigned *)(rinfo->ctrl_base+0x00002000);
        rinfo->riva.PGRAPH  = (unsigned *)(rinfo->ctrl_base+0x00400000);
        rinfo->riva.PEXTDEV = (unsigned *)(rinfo->ctrl_base+0x00101000);
        rinfo->riva.PTIMER  = (unsigned *)(rinfo->ctrl_base+0x00009000);
        rinfo->riva.PMC     = (unsigned *)(rinfo->ctrl_base+0x00000000);
        rinfo->riva.FIFO    = (unsigned *)(rinfo->ctrl_base+0x00800000);

        switch (rinfo->riva.Architecture)
        {
        case 3:
                rinfo->riva.PRAMIN  = (unsigned *)(rinfo->ctrl_base+0x00C00000);
                break;
        case 4:
	case 5:
                rinfo->riva.PCRTC   = (unsigned *)(rinfo->ctrl_base+0x00600000);
                rinfo->riva.PRAMIN  = (unsigned *)(rinfo->ctrl_base+0x00710000);
                break;
        }
}

#endif /* 0 */



/**
 * riva_pci_iounmap
 * @rinfo:
 *
 * DESCRIPTION:
 */

static
void riva_pci_iounmap (struct rivafb_info *rinfo)
{
	assert (rinfo != NULL);
	assert (rinfo->ctrl_base_phys != 0x00000000);
	assert (rinfo->ctrl_base != NULL);
	assert (rinfo->fb_base_phys != 0x00000000);
	assert (rinfo->fb_base != NULL);

	iounmap (rinfo->ctrl_base);
	iounmap (rinfo->fb_base);
}





static void __init riva_init_clut (struct rivafb_info *fb_info)
{
	int j, k;
	
	for (j = 0; j < 256; j++) {
		if (j < 16) {
			k = color_table[j];
			fb_info->palette[j].red = default_red[k];
			fb_info->palette[j].green = default_grn[k];
			fb_info->palette[j].blue = default_blu[k];
		} else {
			fb_info->palette[j].red =
			fb_info->palette[j].green =
			fb_info->palette[j].blue = j;
		}
		
		riva_wclut (j,
			    fb_info->palette[j].red,
			    fb_info->palette[j].green,
			    fb_info->palette[j].blue);
	}
}



static int __init riva_pci_register (struct pci_dev *pd,
				     const struct riva_chip_info *rci)
{
	struct rivafb_info *rinfo;

	assert (pd != NULL);
	assert (rci != NULL);

	rinfo = kmalloc (sizeof (struct rivafb_info), GFP_KERNEL);
	assert (rinfo != NULL);
	memset (rinfo, 0, sizeof (struct rivafb_info));

	rinfo->drvr_name = rci->name;
	rinfo->riva.Architecture = rci->arch_rev;

	rinfo->pd = pd;
	rinfo->base0_region_size = riva_pci_region_size (pd, 0);
	rinfo->base1_region_size = riva_pci_region_size (pd, 1);

	assert(rinfo->base0_region_size >= 0x00800000); /* from GGI */
	assert(rinfo->base0_region_size >= 0x01000000); /* from GGI */

	rinfo->ctrl_base_phys = rinfo->pd->resource[0].start;
	rinfo->fb_base_phys = rinfo->pd->resource[1].start;

	__request_region(&ioport_resource, 0x3C0, 32, "rivafb");

	if (!__request_region (&iomem_resource, rinfo->ctrl_base_phys,
			       rinfo->base0_region_size, "rivafb") ||
	    !__request_region (&iomem_resource, rinfo->fb_base_phys,
			       rinfo->base1_region_size, "rivafb")) {
		printk (KERN_ERR PFX "cannot reserve MMIO region\n");
		return -ENXIO;
	}

	rinfo->ctrl_base = ioremap (rinfo->ctrl_base_phys,
				    rinfo->base0_region_size);
	assert (rinfo->ctrl_base != NULL);

	rinfo->fb_base = ioremap (rinfo->fb_base_phys,
				  rinfo->base1_region_size);
	assert (rinfo->fb_base != NULL);

        rinfo->riva.EnableIRQ = 0;
        rinfo->riva.IO      = (inb(0x3CC) & 0x01) ? 0x3D0 : 0x3B0;
        rinfo->riva.PRAMDAC = (unsigned *)(rinfo->ctrl_base+0x00680000);
        rinfo->riva.PFB     = (unsigned *)(rinfo->ctrl_base+0x00100000);
        rinfo->riva.PFIFO   = (unsigned *)(rinfo->ctrl_base+0x00002000);
        rinfo->riva.PGRAPH  = (unsigned *)(rinfo->ctrl_base+0x00400000);
        rinfo->riva.PEXTDEV = (unsigned *)(rinfo->ctrl_base+0x00101000);
        rinfo->riva.PTIMER  = (unsigned *)(rinfo->ctrl_base+0x00009000);
        rinfo->riva.PMC     = (unsigned *)(rinfo->ctrl_base+0x00000000);
        rinfo->riva.FIFO    = (unsigned *)(rinfo->ctrl_base+0x00800000);

        switch (rinfo->riva.Architecture)
        {
        case 3:
                rinfo->riva.PRAMIN  = (unsigned *)(rinfo->ctrl_base+0x00C00000);
                break;
        case 4:
	case 5:
                rinfo->riva.PCRTC   = (unsigned *)(rinfo->ctrl_base+0x00600000);
                rinfo->riva.PRAMIN  = (unsigned *)(rinfo->ctrl_base+0x00710000);
                break;
        }

        RivaGetConfig(&rinfo->riva);

	/* back to normal */

	assert (rinfo->pd != NULL);

	/* unlock io */
	vga_io_wcrt (0x11, 0xFF); /* vgaHWunlock() + riva unlock (0x7F) */
	outb(rinfo->riva.LockUnlockIO, rinfo->riva.LockUnlockIndex);
	outb(rinfo->riva.LockUnlockIO + 1, 0x57);

	memcpy (&rinfo->initial_state, &reg_template, sizeof (reg_template));
	riva_save_state (rinfo, &rinfo->initial_state);

        rinfo->ram_amount = rinfo->riva.RamAmountKBytes * 1024;
	rinfo->dclk_max = rinfo->riva.MaxVClockFreqKHz * 1000;

	riva_set_fbinfo (rinfo);
	
	riva_init_clut (rinfo);

	riva_load_video_mode (rinfo, &rinfo->disp.var);

	if (register_framebuffer ((struct fb_info *) rinfo) < 0) {
		printk(KERN_ERR PFX "error registering riva framebuffer\n");
		kfree (rinfo);
		return -1;
	}

	riva_boards = riva_board_list_add (riva_boards, rinfo);

	printk("PCI Riva NV%d framebuffer ver %s (%s, %dMB @ 0x%lX)\n",
	       rinfo->riva.Architecture,
	       RIVAFB_VERSION,
	       rinfo->drvr_name,
	       rinfo->ram_amount / (1024 * 1024),
	       rinfo->fb_base_phys);

	return 0;
}




static int __init riva_pci_register_devices (void)
{
	struct pci_dev *pd;
	const struct riva_chip_info *nci = &riva_pci_probe_list[0];

	assert (nci != NULL);
	while (nci->name != NULL) {
		pd = pci_find_device (nci->vendor, nci->device, NULL);
		while (pd != NULL) {
			if (riva_pci_register (pd, nci) < 0)
				return -1;

			pd = pci_find_device (nci->vendor, nci->device, pd);
		}

		nci++;
	}

	return 0;
}



/*** riva_wclut - set CLUT entry ***/
static void riva_wclut (unsigned char regnum, unsigned char red,
	    		unsigned char green, unsigned char blue)
{
	unsigned int data = VGA_PEL_D;
	
	/* address write mode register is not translated.. */
	vga_io_w (VGA_PEL_IW, regnum);

	vga_io_w (data, red);
	vga_io_w (data, green);
	vga_io_w (data, blue);
}



/* ------------ Hardware Independent Functions ------------ */

int __init rivafb_setup(char *options)
{
    char *this_opt;

    if (!options || !*options)
	return 0;

    for (this_opt = strtok(options, ","); this_opt;
	 this_opt = strtok(NULL, ",")) {
	if (!strncmp(this_opt, "font:", 5)) {
		char *p;
		int i;

		p = this_opt + 5;
		for (i = 0; i < sizeof(fontname) - 1; i++)
			if (!*p || *p == ' ' || *p == ',')
				break;
		memcpy(fontname, this_opt + 5, i);
		fontname[i] = 0;
	}
	
	else if (!strncmp(this_opt, "noaccel", 7)) {
		noaccel = 1;
	}
	
	else
	    mode_option = this_opt;
    }
    return 0;
}

    /*
     *  Initialization
     */

int __init rivafb_init (void)
{
	if (riva_pci_register_devices () < 0)
	    	return -ENXIO;

	if (riva_boards == NULL)
		return -ENODEV;

	return 0;
}


/* ------------------------------------------------------------------------- */
    /*
     *  Cleanup
     */

void __exit rivafb_cleanup (struct fb_info *info)
{
#warning FIXME: is this all we need to do?

	struct rivafb_info *tmp, *board = riva_boards;

	while (board != NULL) {
		riva_load_state (board, &board->initial_state);
		riva_pci_iounmap (board);

		unregister_framebuffer ((struct fb_info *) board);

		tmp = board;
		board = board->next;

		kfree_s (tmp, sizeof (struct rivafb_info));
	}

	(void) info; /* unused function arg */
}




    /*
     *  Frame buffer operations
     */

int rivafb_open(struct fb_info *info, int user)
{
    /* Nothing, only a usage count for the moment */
    MOD_INC_USE_COUNT;
    return 0;
}

int rivafb_release(struct fb_info *info, int user)
{
    MOD_DEC_USE_COUNT;
    return 0;
}


/**
 * rivafb_get_fix
 * @fix:
 * @con:
 * @info:
 *
 * DESCRIPTION:
 */

int rivafb_get_fix (struct fb_fix_screeninfo *fix, int con,
		  struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;
	struct display *p;

	DPRINTK ("ENTER\n");

	assert (fix != NULL);
	assert (info != NULL);
	assert (rivainfo->drvr_name && rivainfo->drvr_name[0]);
	assert (rivainfo->fb_base_phys > 0);
	assert (rivainfo->ram_amount > 0);

	p = (con < 0) ? rivainfo->info.disp : &fb_display[con];

	memset (fix, 0, sizeof (struct fb_fix_screeninfo));
	sprintf (fix->id, "Riva %s", rivainfo->drvr_name);

	fix->smem_start = rivainfo->fb_base_phys;
	fix->smem_len = rivainfo->ram_amount;

	fix->type = p->type;
	fix->type_aux = p->type_aux;
	fix->visual = p->visual;

	fix->xpanstep = 1;
	fix->ypanstep = 1;
	fix->ywrapstep = 0; /* FIXME: no ywrap for now */

	fix->line_length = p->line_length;

#warning FIXME: set up MMIO region
	fix->mmio_start = 0;
	fix->mmio_len = 0;

#warning FIXME: reference riva acceleration fb.h constant here
	fix->accel = FB_ACCEL_NONE;

	DPRINTK ("EXIT, returning 0\n");

	return 0;
}



/**
 * rivafb_get_var
 * @var:
 * @con:
 * @info:
 *
 * DESCRIPTION:
 */

int rivafb_get_var (struct fb_var_screeninfo *var, int con,
		  struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;

	DPRINTK ("ENTER\n");

	assert (info != NULL);
	assert (var != NULL);

	*var = (con < 0) ? rivainfo->disp.var : fb_display[con].var;

	DPRINTK ("EXIT, returning 0\n");

	return 0;
}



/**
 * rivafb_set_var
 * @var:
 * @con:
 * @info:
 *
 * DESCRIPTION:
 */

int rivafb_set_var (struct fb_var_screeninfo *var, int con,
		  struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;
	struct display *dsp;
	struct fb_var_screeninfo v;
	int nom, den;		/* translating from pixels->bytes */
	int i;
	unsigned chgvar = 0;
	static struct {
		int xres, yres;
	} modes[] = {
		{ 1600, 1280 },
		{ 1280, 1024 },
		{ 1024, 768 },
		{ 800, 600 },
		{ 640, 480 },
		{ -1, -1 }
	};

	DPRINTK ("ENTER\n");

	assert (info != NULL);
	assert (var != NULL);

	DPRINTK ("Requested: %dx%dx%d\n", var->xres, var->yres, var->bits_per_pixel);
	DPRINTK ("  virtual: %dx%d\n", var->xres_virtual, var->yres_virtual);
	DPRINTK ("   offset: (%d,%d)\n", var->xoffset, var->yoffset);
	DPRINTK ("grayscale: %d\n", var->grayscale);

	dsp = (con < 0) ? rivainfo->info.disp : &fb_display[con];
	assert (dsp != NULL);

	/* if var has changed, we should call changevar() later */
	if (con >= 0) {
		chgvar = ((dsp->var.xres != var->xres) ||
		    (dsp->var.yres != var->yres) ||
                    (dsp->var.xres_virtual != var->xres_virtual) ||
		    (dsp->var.yres_virtual != var->yres_virtual) ||
		    (dsp->var.bits_per_pixel != var->bits_per_pixel) ||
		    memcmp(&dsp->var.red, &var->red, sizeof(var->red)) ||
		    memcmp(&dsp->var.green, &var->green, sizeof(var->green)) ||
		    memcmp(&dsp->var.blue, &var->blue, sizeof(var->blue)));
	}

	memcpy (&v, var, sizeof (v));

	switch (v.bits_per_pixel) {
#ifdef FBCON_HAS_MFB
	case 1:
		dsp->dispsw = &fbcon_mfb;
		dsp->line_length = v.xres_virtual / 8;
		dsp->visual = FB_VISUAL_MONO10;
		nom = 4;
		den = 8;
		break;
#endif

#ifdef FBCON_HAS_CFB8
	case 2 ... 8:
		v.bits_per_pixel = 8;
		dsp->dispsw = &fbcon_cfb8;
		nom = 1;
		den = 1;
		dsp->line_length = v.xres_virtual;
		dsp->visual = FB_VISUAL_PSEUDOCOLOR;
		v.red.offset = 0;
		v.red.length = 6;
		v.green.offset = 0;
		v.green.length = 6;
		v.blue.offset = 0;
		v.blue.length = 6;
		break;
#endif

#ifdef FBCON_HAS_CFB16
	case 9 ... 16:
		v.bits_per_pixel = 16;
		dsp->dispsw = &fbcon_cfb16;
		dsp->dispsw_data = &rivainfo->con_cmap.cfb16;
		nom = 2;
		den = 1;
		dsp->line_length = v.xres_virtual * 2;
		dsp->visual = FB_VISUAL_DIRECTCOLOR;
#ifdef CONFIG_PREP
		v.red.offset = 2;
		v.green.offset = -3;
		v.blue.offset = 8;
#else
		v.red.offset = 10;
		v.green.offset = 5;
		v.blue.offset = 0;
#endif
		v.red.length = 5;
		v.green.length = 5;
		v.blue.length = 5;
		break;
#endif

#ifdef FBCON_HAS_CFB32
	case 17 ... 32:
		v.bits_per_pixel = 32;
		dsp->dispsw = &fbcon_cfb32;
		dsp->dispsw_data = rivainfo->con_cmap.cfb32;
		nom = 4;
		den = 1;
		dsp->line_length = v.xres_virtual * 4;
		dsp->visual = FB_VISUAL_DIRECTCOLOR;
#ifdef CONFIG_PREP
		v.red.offset = 8;
		v.green.offset = 16;
		v.blue.offset = 24;
#else
		v.red.offset = 16;
		v.green.offset = 8;
		v.blue.offset = 0;
#endif
		v.red.length = 8;
		v.green.length = 8;
		v.blue.length = 8;
		break;
#endif

	default:
		printk (KERN_ERR PFX "mode %dx%dx%d rejected...color depth not supported.\n",
			var->xres, var->yres, var->bits_per_pixel);
		DPRINTK ("EXIT, returning -EINVAL\n");
		return -EINVAL;
	}

	if (v.xres * nom / den * v.yres > rivainfo->ram_amount) {
		printk (KERN_ERR PFX "mode %dx%dx%d rejected...resolution too high to fit into video memory!\n",
			var->xres, var->yres, var->bits_per_pixel);
		DPRINTK ("EXIT - EINVAL error\n");
		return -EINVAL;
	}

	/* use highest possible virtual resolution */
	if (v.xres_virtual == -1 &&
	    v.yres_virtual == -1) {
		printk (KERN_WARNING PFX "using maximum available virtual resolution\n");
		for (i = 0; modes[i].xres != -1; i++) {
			if (modes[i].xres * nom / den * modes[i].yres < rivainfo->ram_amount / 2)
				break;
		}
		if (modes[i].xres == -1) {
			printk (KERN_ERR PFX "could not find a virtual resolution that fits into video memory!!\n");
			DPRINTK ("EXIT - EINVAL error\n");
			return -EINVAL;
		}
		v.xres_virtual = modes[i].xres;
		v.yres_virtual = modes[i].yres;

		printk (KERN_INFO PFX "virtual resolution set to maximum of %dx%d\n",
			v.xres_virtual, v.yres_virtual);
	} else if (v.xres_virtual == -1) {
		/* FIXME: maximize X virtual resolution only */
	} else if (v.yres_virtual == -1) {
		/* FIXME: maximize Y virtual resolution only */
	}

	if (v.xoffset < 0)
		v.xoffset = 0;
	if (v.yoffset < 0)
		v.yoffset = 0;

	/* truncate xoffset and yoffset to maximum if too high */
	if (v.xoffset > v.xres_virtual - v.xres)
		v.xoffset = v.xres_virtual - v.xres - 1;

	if (v.yoffset > v.yres_virtual - v.yres)
		v.yoffset = v.yres_virtual - v.yres - 1;

	v.red.msb_right =
	    v.green.msb_right =
	    v.blue.msb_right =
	    v.transp.offset =
	    v.transp.length =
	    v.transp.msb_right = 0;

	switch (v.activate & FB_ACTIVATE_MASK) {
	case FB_ACTIVATE_TEST:
		DPRINTK ("EXIT - FB_ACTIVATE_TEST\n");
		return 0;
	case FB_ACTIVATE_NXTOPEN:	/* ?? */
	case FB_ACTIVATE_NOW:
		break;		/* continue */
	default:
		DPRINTK ("EXIT - unknown activation type\n");
		return -EINVAL;	/* unknown */
	}

	dsp->type = FB_TYPE_PACKED_PIXELS;

#warning FIXME: verify that the above code sets dsp->* fields correctly

	memcpy (&dsp->var, &v, sizeof (v));

	riva_load_video_mode (rivainfo, &v);

	if (chgvar && info && info->changevar)
		info->changevar (con);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}



/**
 * rivafb_get_cmap
 * @cmap:
 * @kspc:
 * @con:
 * @info:
 *
 * DESCRIPTION:
 *
 * NOTES:
 * Copied from matroxfb::matroxfb_get_cmap()
 */

int rivafb_get_cmap (struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;
	struct display *dsp;

	DPRINTK ("ENTER\n");

	assert (rivainfo != NULL);
	assert (cmap != NULL);

	dsp = (con < 0) ? rivainfo->info.disp : &fb_display[con];

	if (con == rivainfo->currcon) {	/* current console? */
		int rc = fb_get_cmap (cmap, kspc, riva_getcolreg, info);
		DPRINTK ("EXIT - returning %d\n", rc);
		return rc;
	} else if (dsp->cmap.len)	/* non default colormap? */
		fb_copy_cmap (&dsp->cmap, cmap, kspc ? 0 : 2);
	else
		fb_copy_cmap (fb_default_cmap (riva_get_cmap_len (&dsp->var)),
			      cmap, kspc ? 0 : 2);

	DPRINTK ("EXIT, returning 0\n");

	return 0;
}


/**
 * rivafb_set_cmap
 * @cmap:
 * @kspc:
 * @con:
 * @info:
 *
 * DESCRIPTION:
 *
 * NOTES:
 * Copied from matroxfb::matroxfb_set_cmap()
 */

int rivafb_set_cmap (struct fb_cmap *cmap, int kspc, int con,
		   struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;
	struct display *dsp;
	unsigned int cmap_len;

	DPRINTK ("ENTER\n");

	assert (rivainfo != NULL);
	assert (cmap != NULL);

	dsp = (con < 0) ? rivainfo->info.disp : &fb_display[con];

	cmap_len = riva_get_cmap_len (&dsp->var);
	if (dsp->cmap.len != cmap_len) {
		int err = fb_alloc_cmap (&dsp->cmap, cmap_len, 0);
		if (err) {
			DPRINTK ("EXIT - returning %d\n", err);
			return err;
		}
	}
	if (con == rivainfo->currcon) {	/* current console? */
		int rc = fb_set_cmap (cmap, kspc, riva_setcolreg, info);
		DPRINTK ("EXIT - returning %d\n", rc);
		return rc;
	} else
		fb_copy_cmap (cmap, &dsp->cmap, kspc ? 0 : 1);

	DPRINTK ("EXIT, returning 0\n");

	return 0;
}



/**
 * rivafb_pan_display
 * @var: standard kernel fb changeable data
 * @par: riva-specific hardware info about current video mode
 * @info: pointer to rivafb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Pan (or wrap, depending on the `vmode' field) the display using the
 * `xoffset' and `yoffset' fields of the `var' structure.
 * If the values don't fit, return -EINVAL.
 *
 * This call looks only at xoffset, yoffset and the FB_VMODE_YWRAP flag
 */

int rivafb_pan_display (struct fb_var_screeninfo *var, int con,
		      struct fb_info *info)
{
	unsigned int base;
	struct display *dsp;
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;

	DPRINTK ("ENTER\n");

	assert (rivainfo != NULL);

	if (var->xoffset > (var->xres_virtual - var->xres))
		return -EINVAL;
	if (var->yoffset > (var->yres_virtual - var->yres))
		return -EINVAL;

	dsp = (con < 0) ? rivainfo->info.disp : &fb_display[con];

	if (var->vmode & FB_VMODE_YWRAP) {
		if (var->yoffset < 0 || var->yoffset >= dsp->var.yres_virtual || var->xoffset)
			return -EINVAL;
	} else {
		if (var->xoffset+dsp->var.xres > dsp->var.xres_virtual ||
		    var->yoffset+dsp->var.yres > dsp->var.yres_virtual)
			return -EINVAL;
	}

	base = var->yoffset * dsp->line_length + var->xoffset;

	if (con == rivainfo->currcon) {
		/* FIXME: do the dirty deed */
	}

	dsp->var.xoffset = var->xoffset;
	dsp->var.yoffset = var->yoffset;

	if (var->vmode & FB_VMODE_YWRAP)
		dsp->var.vmode |= FB_VMODE_YWRAP;
	else
		dsp->var.vmode &= ~FB_VMODE_YWRAP;

	DPRINTK ("EXIT, returning 0\n");

	return 0;
}



/**
 * rivafb_ioctl
 * @inode:
 * @file:
 * @cmd:
 * @arg:
 * @con:
 * @info:
 *
 * DESCRIPTION:
 */

int rivafb_ioctl (struct inode *inode, struct file *file, unsigned int cmd,
		unsigned long arg, int con, struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;

	DPRINTK ("ENTER\n");

	assert (rivainfo != NULL);

	/* no rivafb-specific ioctls */

	DPRINTK ("EXIT, returning -EINVAL\n");

	return -EINVAL;
}




/**
 * rivafb_switch
 * @con:
 * @info:
 *
 * DESCRIPTION:
 */

int rivafb_switch(int con, struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;
	struct fb_cmap* cmap;
	struct display *dsp;

	DPRINTK("ENTER\n");

	assert (rivainfo != NULL);

	dsp = (con < 0) ? rivainfo->info.disp : &fb_display[con];

	if (rivainfo->currcon >= 0) {
		/* Do we have to save the colormap? */
		cmap = &(rivainfo->currcon_display->cmap);
		DPRINTK("switch1: con = %d, cmap.len = %d\n", rivainfo->currcon, cmap->len);

		if (cmap->len) {
			DPRINTK("switch1a: %p %p %p %p\n", cmap->red, cmap->green, cmap->blue, cmap->transp);
			fb_get_cmap(cmap, 1, riva_getcolreg, info);
#ifdef DEBUG
			if (cmap->red) {
				DPRINTK("switch1r: %X\n", cmap->red[0]);
			}
#endif
		}
	}
	rivainfo->currcon = con;
	rivainfo->currcon_display = dsp;
	dsp->var.activate = FB_ACTIVATE_NOW;

#ifdef riva_DEBUG
	cmap = &dsp->cmap;
	DPRINTK("switch2: con = %d, cmap.len = %d\n", con, cmap->len);
	DPRINTK("switch2a: %p %p %p %p\n", cmap->red, cmap->green, cmap->blue, cmap->transp);
	if (dsp->cmap.red) {
		DPRINTK("switch2r: %X\n", cmap->red[0]);
	}
#endif

	rivafb_set_var(&dsp->var, con, info);

#ifdef riva_DEBUG
	DPRINTK("switch3: con = %d, cmap.len = %d\n", con, cmap->len);
	DPRINTK("switch3a: %p %p %p %p\n", cmap->red, cmap->green, cmap->blue, cmap->transp);
	if (dsp->cmap.red) {
		DPRINTK("switch3r: %X\n", cmap->red[0]);
	}
#endif

	DPRINTK("EXIT, returning 0\n");

	return 0;
}



void rivafb_blank (int blank, struct fb_info *info)
{
	unsigned char tmp;
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;

	DPRINTK ("ENTER\n");

	assert (rivainfo != NULL);

	tmp = vga_io_rseq (VGA_SEQ_CLOCK_MODE) & ~VGA_SR01_SCREEN_OFF;

	if (blank)
		tmp |= VGA_SR01_SCREEN_OFF;

	vga_io_wseq (VGA_SEQ_CLOCK_MODE, tmp);

	DPRINTK("EXIT\n");
}


/* -------------------------------------------------------------------------
 *
 * internal fb_ops helper functions
 *
 * -------------------------------------------------------------------------
 */


/**
 * riva_get_cmap_len
 * @var:
 *
 * DESCRIPTION:
 */

static int riva_get_cmap_len(const struct fb_var_screeninfo *var)
{
	int rc = 16; /* reasonable default */

	assert (var != NULL);

	switch (var->bits_per_pixel) {
#ifdef FBCON_HAS_CFB4
		case 4:
			rc = 16;	/* pseudocolor... 16 entries HW palette */
			break;
#endif
#ifdef FBCON_HAS_CFB8
		case 8:
			rc = 256;	/* pseudocolor... 256 entries HW palette */
			break;
#endif
#ifdef FBCON_HAS_CFB16
		case 16:
			rc = 16;	/* directcolor... 16 entries SW palette */
			break;		/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
#endif
#ifdef FBCON_HAS_CFB32
		case 32:
			rc = 16;	/* directcolor... 16 entries SW palette */
			break;		/* Mystique: truecolor, 16 entries SW palette, HW palette hardwired into 1:1 mapping */
#endif
		default:
			assert (0);
			/* should not occur */
			break;
	}

	return rc;
}


/**
 * riva_getcolreg
 * @regno:
 * @red:
 * @green:
 * @blue:
 * @transp:
 * @info: pointer to rivafb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Read a single color register and split it into colors/transparent.
 * The return values must have a 16 bit magnitude.
 * Return != 0 for invalid regno.
 *
 * CALLED FROM:
 * fbcmap.c:fb_get_cmap()
 *	fbgen.c:fbgen_get_cmap()
 *	fbgen.c:fbgen_switch()
 */

static int riva_getcolreg (unsigned regno, unsigned *red, unsigned *green,
			 unsigned *blue, unsigned *transp,
			 struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;

    if (regno > 255)
	return 1;
    *red = rivainfo->palette[regno].red;
    *green = rivainfo->palette[regno].green;
    *blue = rivainfo->palette[regno].blue;
    *transp = 0;
    return 0;
}


/**
 * riva_setcolreg
 * @regno:
 * @red:
 * @green:
 * @blue:
 * @transp:
 * @info: pointer to rivafb_info object containing info for current riva board
 *
 * DESCRIPTION:
 * Set a single color register. The values supplied have a 16 bit
 * magnitude.
 * Return != 0 for invalid regno.
 *
 * CALLED FROM:
 * fbcmap.c:fb_set_cmap()
 *	fbgen.c:fbgen_get_cmap()
 *	fbgen.c:fbgen_install_cmap()
 *		fbgen.c:fbgen_set_var()
 *		fbgen.c:fbgen_switch()
 *		fbgen.c:fbgen_blank()
 *	fbgen.c:fbgen_blank()
 */

static int riva_setcolreg (unsigned regno, unsigned red, unsigned green,
		    unsigned blue, unsigned transp,
		    struct fb_info *info)
{
	struct rivafb_info *rivainfo = (struct rivafb_info *) info;
	struct display *p;

	DPRINTK("ENTER\n");

	assert (rivainfo != NULL);
	assert (rivainfo->currcon_display != NULL);

	if (regno > 255)
		return -EINVAL;

	p = rivainfo->currcon_display;
	if (p->var.grayscale) {
		/* gray = 0.30*R + 0.59*G + 0.11*B */
		red = green = blue = (red * 77 + green * 151 + blue * 28) >> 8;
	}

#ifdef FBCON_HAS_CFB8
	switch (p->var.bits_per_pixel) {
	case 8:
		/* "transparent" stuff is completely ignored. */
		riva_wclut (regno, red >> 10, green >> 10, blue >> 10);
		break;
	default:
		/* do nothing */
		break;
	}
#endif	/* FBCON_HAS_CFB8 */

	rivainfo->palette[regno].red = red;
	rivainfo->palette[regno].green = green;
	rivainfo->palette[regno].blue = blue;

	if (regno >= 16)
		return 0;
		
	switch (p->var.bits_per_pixel) {

#ifdef FBCON_HAS_CFB16
	case 16:
		assert (regno < 16);
#ifdef CONFIG_PREP
		rivainfo->con_cmap.cfb16[regno] =
		    ((red & 0xf800) >> 9) |
		    ((green & 0xf800) >> 14) |
		    ((green & 0xf800) << 2) |
		    ((blue & 0xf800) >> 3);
#else
		rivainfo->con_cmap.cfb16[regno] =
		    ((red & 0xf800) >> 1) |
		    ((green & 0xf800) >> 6) |
		    ((blue & 0xf800) >> 11);
#endif
		break;
#endif /* FBCON_HAS_CFB16 */

#ifdef FBCON_HAS_CFB32
	case 32:
		assert (regno < 16);
#ifdef CONFIG_PREP
		rivainfo->con_cmap.cfb32[regno] =
		    ((red & 0xff00)) |
		    ((green & 0xff00) << 8) |
		    ((blue & 0xff00) << 16);
#else
		rivainfo->con_cmap.cfb32[regno] =
		    ((red & 0xff00) << 8) |
		    ((green & 0xff00)) |
		    ((blue & 0xff00) >> 8);
#endif
		break;
#endif /* FBCON_HAS_CFB32 */

	default:
		/* do nothing */
		break;
	}

	return 0;
}



/*
 * riva_load_video_mode()
 *
 * calculate some timings and then send em off to riva_load_state()
 */

static void riva_load_video_mode (struct rivafb_info *rinfo,
				struct fb_var_screeninfo *video_mode)
{
	struct riva_regs newmode;
	int bpp, width, hDisplaySize, hDisplay, hStart,
		hEnd, hTotal, height, vDisplay, vStart,
		vEnd, vTotal, dotClock;

	/* time to calculate */

	bpp = video_mode->bits_per_pixel;
	width = hDisplaySize = video_mode->xres;
	hDisplay = (hDisplaySize/8) - 1;
	hStart = (hDisplaySize + video_mode->right_margin)/8 + 2;
	hEnd = (hDisplaySize + video_mode->right_margin +
		video_mode->hsync_len)/8 - 1;
	hTotal = (hDisplaySize + video_mode->right_margin +
		  video_mode->hsync_len + video_mode->left_margin)/8 - 1;
	height = video_mode->yres;
	vDisplay = video_mode->yres - 1;
	vStart = video_mode->yres + video_mode->lower_margin - 1;
	vEnd = video_mode->yres + video_mode->lower_margin +
	       video_mode->vsync_len - 1;
	vTotal = video_mode->yres + video_mode->lower_margin +
		 video_mode->vsync_len + video_mode->upper_margin + 2;
	dotClock = 1000000000 / video_mode->pixclock;

	memcpy(&newmode, &reg_template, sizeof(struct riva_regs));

	newmode.crtc[0x0] = Set8Bits(hTotal - 4);
	newmode.crtc[0x1] = Set8Bits(hDisplay);
	newmode.crtc[0x2] = Set8Bits(hDisplay);
	newmode.crtc[0x3] = SetBitField(hTotal,4:0,4:0) | SetBit(7);
	newmode.crtc[0x4] = Set8Bits(hStart);
	newmode.crtc[0x5] = SetBitField(hTotal,5:5,7:7)
				| SetBitField(hEnd,4:0,4:0);
	newmode.crtc[0x6] = SetBitField(vTotal,7:0,7:0);
	newmode.crtc[0x7] = SetBitField(vTotal,8:8,0:0)
				| SetBitField(vDisplay,8:8,1:1)
				| SetBitField(vStart,8:8,2:2)
				| SetBitField(vDisplay,8:8,3:3)
				| SetBit(4)
				| SetBitField(vTotal,9:9,5:5)
				| SetBitField(vDisplay,9:9,6:6)
				| SetBitField(vStart,9:9,7:7);
	newmode.crtc[0x9] = SetBitField(vDisplay,9:9,5:5)
				| SetBit(6);
	newmode.crtc[0x10] = Set8Bits(vStart);
	newmode.crtc[0x11] = SetBitField(vEnd,3:0,3:0)
				| SetBit(5);
	newmode.crtc[0x12] = Set8Bits(vDisplay);
	newmode.crtc[0x13] = ((width/8)*(bpp/8)) & 0xFF;
	newmode.crtc[0x15] = Set8Bits(vDisplay);
	newmode.crtc[0x16] = Set8Bits(vTotal + 1);

	newmode.ext.bpp = bpp;
	newmode.ext.width = width;
	newmode.ext.height = height;

	rinfo->riva.CalcStateExt(&rinfo->riva,&newmode.ext,bpp,width,
				 hDisplaySize,hDisplay,hStart,hEnd,hTotal,
				 height,vDisplay,vStart,vEnd,vTotal,dotClock);

	rinfo->initial_state = newmode;
	riva_load_state(rinfo,&newmode);
}





/* ------------------------------------------------------------------------- */


    /*
     *  Modularization
     */

#ifdef MODULE
int __init init_module(void)
{
    return rivafb_init();
}

void __exit cleanup_module(void)
{
	struct rivafb_info *tmp, *board = riva_boards;

	while (board != NULL) {
		riva_load_state (board, &board->initial_state);

		unregister_framebuffer ((struct fb_info *) board);

		tmp = board;
		iounmap (tmp->ctrl_base);
		iounmap (tmp->fb_base);
		board = board->next;

		kfree (tmp);
	}
}
#endif /* MODULE */





/* from GGI */
static
void riva_save_state(struct rivafb_info *rinfo, struct riva_regs *regs)
{
        int i;

        io_out8(NV_CIO_SR_LOCK_INDEX, 0x3D4);
        io_out8(NV_CIO_SR_UNLOCK_RW_VALUE, 0x3D5);

        rinfo->riva.UnloadStateExt(&rinfo->riva, &regs->ext);

        regs->misc_output = io_in8(0x3CC);

        for (i = 0; i < NUM_CRT_REGS; i++)
        {
                io_out8(i, 0x3D4);
                regs->crtc[i] = io_in8(0x3D5);
        }

        for (i = 0; i < NUM_ATC_REGS; i++)
        {
                io_out8(i, 0x3C0);
                regs->attr[i] = io_in8(0x3C1);
        }

        for (i = 0; i < NUM_GRC_REGS; i++)
        {
                io_out8(i, 0x3CE);
                regs->gra[i] = io_in8(0x3CF);
        }


        for (i = 0; i < NUM_SEQ_REGS; i++)
        {
                io_out8(i, 0x3C4);
                regs->seq[i] = io_in8(0x3C5);
        }
}


/* from GGI */
static
void riva_load_state(struct rivafb_info *rinfo, struct riva_regs *regs)
{
        int i;
	RIVA_HW_STATE *state = &regs->ext;

        io_out8(0x11, 0x3D4);
        io_out8(0x00, 0x3D5);

        io_out8(NV_CIO_SR_LOCK_INDEX, 0x3D4);
        io_out8(NV_CIO_SR_UNLOCK_RW_VALUE, 0x3D5);

 	rinfo->riva.LoadStateExt(&rinfo->riva,state);

        io_out8(regs->misc_output, 0x3C2);

        for (i = 0; i < NUM_CRT_REGS; i++)
        {
                if (i < 0x19)
                {
                        io_out8(i, 0x3D4);
                        io_out8(regs->crtc[i], 0x3D5);
                }
                else
                {
                        switch (i)
                        {
                                case 0x19:
                                case 0x20:
                                case 0x21:
                                case 0x22:
                                case 0x23:
                                case 0x24:
                                case 0x25:
                                case 0x26:
                                case 0x27:
#if 0
                                case 0x28:
#endif
                                case 0x29:
                                case 0x2a:
                                case 0x2b:
                                case 0x2c:
                                case 0x2d:
                                case 0x2e:
                                case 0x2f:
                                case 0x30:
                                case 0x31:
                                case 0x32:
                                case 0x33:
                                case 0x34:
                                case 0x35:
                                case 0x36:
                                case 0x37:
                                case 0x38:
                                case 0x39:
                                case 0x3a:
                                case 0x3b:
                                case 0x3c:
                                case 0x3d:
                                case 0x3e:
                                case 0x3f:
#if 0
                                case 0x40:
#endif
                                break;
                                default:
                                io_out8(i, 0x3D4);
                                io_out8(regs->crtc[i], 0x3D5);
                        }
                }
        }

        for (i = 0; i < NUM_ATC_REGS; i++)
        {
                io_out8(i, 0x3C0);
                io_out8(regs->attr[i], 0x3C1);
        }

        for (i = 0; i < NUM_GRC_REGS; i++)
        {
                io_out8(i, 0x3CE);
                io_out8(regs->gra[i], 0x3CF);
        }

        for (i = 0; i < NUM_SEQ_REGS; i++)
        {
                io_out8(i, 0x3C4);
                io_out8(regs->seq[i], 0x3C5);
        }
}




/**
 * riva_board_list_add
 * @board_list: Root node of list of boards
 * @new_node: New node to be added
 *
 * DESCRIPTION:
 * Adds @new_node to the list referenced by @board_list
 *
 * RETURNS:
 * New root node
 */
static
struct rivafb_info *riva_board_list_add(struct rivafb_info *board_list,
				 struct rivafb_info *new_node)
{
	struct rivafb_info *i_p = board_list;

	new_node->next = NULL;

	if (board_list == NULL)
		return new_node;

	while (i_p->next != NULL)
		i_p = i_p->next;
	i_p->next = new_node;

	return board_list;
}

