/*
 *  linux/drivers/video/fbmem.c
 *
 *  Copyright (C) 1994 Martin Schaller
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/module.h>

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mman.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/console_struct.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#ifdef CONFIG_KMOD
#include <linux/kmod.h>
#endif

#if defined(__mc68000__) || defined(CONFIG_APUS)
#include <asm/setup.h>
#endif
#ifdef __powerpc__
#include <asm/io.h>
#endif
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/fb.h>


    /*
     *  Frame buffer device initialization and setup routines
     */

extern int acornfb_init(void);
extern int acornfb_setup(char*);
extern int amifb_init(void);
extern int amifb_setup(char*);
extern int atafb_init(void);
extern int atafb_setup(char*);
extern int macfb_init(void);
extern int macfb_setup(char*);
extern int cyberfb_init(void);
extern int cyberfb_setup(char*);
extern int pm2fb_init(void);
extern int pm2fb_setup(char*);
extern int cyber2000fb_init(void);
extern int cyber2000fb_setup(char*);
extern int retz3fb_init(void);
extern int retz3fb_setup(char*);
extern int clgenfb_init(void);
extern int clgenfb_setup(char*);
extern int vfb_init(void);
extern int vfb_setup(char*);
extern int offb_init(void);
extern int offb_setup(char*);
extern int atyfb_init(void);
extern int atyfb_setup(char*);
extern int aty128fb_init(void);
extern int aty128fb_setup(char*);
extern int igafb_init(void);
extern int igafb_setup(char*);
extern int imsttfb_init(void);
extern int imsttfb_setup(char*);
extern int dnfb_init(void);
extern int tgafb_init(void);
extern int tgafb_setup(char*);
extern int virgefb_init(void);
extern int virgefb_setup(char*);
extern int resolver_video_setup(char*);
extern int s3triofb_init(void);
extern int s3triofb_setup(char*);
extern int vesafb_init(void);
extern int vesafb_setup(char*);
extern int vga16fb_init(void);
extern int vga16fb_setup(char*);
extern int matroxfb_init(void);
extern int matroxfb_setup(char*);
extern int hpfb_init(void);
extern int hpfb_setup(char*);
extern int sbusfb_init(void);
extern int sbusfb_setup(char*);
extern int valkyriefb_init(void);
extern int valkyriefb_setup(char*);
extern int control_init(void);
extern int control_setup(char*);
extern int g364fb_init(void);
extern int fm2fb_init(void);
extern int fm2fb_setup(char*);
extern int q40fb_init(void);
extern int sgivwfb_init(void);
extern int sgivwfb_setup(char*);
extern int rivafb_init(void);
extern int rivafb_setup(char*);
extern int tdfxfb_init(void);
extern int tdfxfb_setup(char*);

static struct {
	const char *name;
	int (*init)(void);
	int (*setup)(char*);
} fb_drivers[] __initdata = {
#ifdef CONFIG_FB_3DFX
	{ "tdfx", tdfxfb_init, tdfxfb_setup },
#endif
#ifdef CONFIG_FB_SGIVW
	{ "sgivw", sgivwfb_init, sgivwfb_setup },
#endif
#ifdef CONFIG_FB_RETINAZ3
	{ "retz3", retz3fb_init, retz3fb_setup },
#endif
#ifdef CONFIG_FB_ACORN
	{ "acorn", acornfb_init, acornfb_setup },
#endif
#ifdef CONFIG_FB_AMIGA
	{ "amifb", amifb_init, amifb_setup },
#endif
#ifdef CONFIG_FB_ATARI
	{ "atafb", atafb_init, atafb_setup },
#endif
#ifdef CONFIG_FB_MAC
	{ "macfb", macfb_init, macfb_setup },
#endif
#ifdef CONFIG_FB_CYBER
	{ "cyber", cyberfb_init, cyberfb_setup },
#endif
#ifdef CONFIG_FB_CYBER2000
	{ "cyber2000", cyber2000fb_init, cyber2000fb_setup },
#endif
#ifdef CONFIG_FB_PM2
	{ "pm2fb", pm2fb_init, pm2fb_setup },
#endif
#ifdef CONFIG_FB_CLGEN
	{ "clgen", clgenfb_init, clgenfb_setup },
#endif
#ifdef CONFIG_FB_OF
	{ "offb", offb_init, offb_setup },
#endif
#ifdef CONFIG_FB_SBUS
	{ "sbus", sbusfb_init, sbusfb_setup },
#endif
#ifdef CONFIG_FB_ATY
	{ "atyfb", atyfb_init, atyfb_setup },
#endif
#ifdef CONFIG_FB_ATY128
	{ "aty128fb", aty128fb_init, aty128fb_setup },
#endif
#ifdef CONFIG_FB_IGA
        { "igafb", igafb_init, igafb_setup },
#endif
#ifdef CONFIG_FB_IMSTT
	{ "imsttfb", imsttfb_init, imsttfb_setup },
#endif
#ifdef CONFIG_APOLLO
	{ "apollo", dnfb_init, NULL },
#endif
#ifdef CONFIG_FB_Q40
	{ "q40fb", q40fb_init, NULL },
#endif
#ifdef CONFIG_FB_S3TRIO
	{ "s3trio", s3triofb_init, s3triofb_setup },
#endif 
#ifdef CONFIG_FB_TGA
	{ "tga", tgafb_init, tgafb_setup },
#endif
#ifdef CONFIG_FB_VIRGE
	{ "virge", virgefb_init, virgefb_setup },
#endif
#ifdef CONFIG_FB_RIVA
	{ "riva", rivafb_init, rivafb_setup },
#endif
#ifdef CONFIG_FB_VESA
	{ "vesa", vesafb_init, vesafb_setup },
#endif 
#ifdef CONFIG_FB_VGA16
	{ "vga16", vga16fb_init, vga16fb_setup },
#endif 
#ifdef CONFIG_FB_MATROX
	{ "matrox", matroxfb_init, matroxfb_setup },
#endif
#ifdef CONFIG_FB_HP300
	{ "hpfb", hpfb_init, hpfb_setup },
#endif 
#ifdef CONFIG_FB_CONTROL
	{ "controlfb", control_init, control_setup },
#endif
#ifdef CONFIG_FB_VALKYRIE
	{ "valkyriefb", valkyriefb_init, valkyriefb_setup },
#endif
#ifdef CONFIG_FB_G364
	{ "g364", g364fb_init, NULL },
#endif
#ifdef CONFIG_FB_FM2
	{ "fm2fb", fm2fb_init, fm2fb_setup },
#endif 
#ifdef CONFIG_GSP_RESOLVER
	/* Not a real frame buffer device... */
	{ "resolver", NULL, resolver_video_setup },
#endif
#ifdef CONFIG_FB_VIRTUAL
	/* Must be last to avoid that vfb becomes your primary display */
	{ "vfb", vfb_init, vfb_setup },
#endif
};

#define NUM_FB_DRIVERS	(sizeof(fb_drivers)/sizeof(*fb_drivers))

extern const char *global_mode_option;

static initcall_t pref_init_funcs[FB_MAX];
static int num_pref_init_funcs __initdata = 0;


#define GET_INODE(i) MKDEV(FB_MAJOR, (i) << FB_MODES_SHIFT)
#define GET_FB_VAR_IDX(node) (MINOR(node) & ((1 << FB_MODES_SHIFT)-1)) 

struct fb_info *registered_fb[FB_MAX];
int num_registered_fb = 0;
int fbcon_softback_size = 32768;

char con2fb_map[MAX_NR_CONSOLES];

static int first_fb_vc = 0;
static int last_fb_vc = MAX_NR_CONSOLES-1;
static int fbcon_is_default = 1;

static int PROC_CONSOLE(const struct fb_info *info)
{
	int fgc;
	
	if (info->display_fg != NULL)
		fgc = info->display_fg->vc_num;
	else
		return -1;
		
	if (!current->tty)
		return fgc;

	if (current->tty->driver.type != TTY_DRIVER_TYPE_CONSOLE)
		/* XXX Should report error here? */
		return fgc;

	if (MINOR(current->tty->device) < 1)
		return fgc;

	return MINOR(current->tty->device) - 1;
}

static int fbmem_read_proc(char *buf, char **start, off_t offset,
			   int len, int *eof, void *private)
{
	struct fb_info **fi;

	len = 0;
	for (fi = registered_fb; fi < &registered_fb[FB_MAX] && len < 4000; fi++)
		if (*fi)
			len += sprintf(buf + len, "%d %s\n",
				       GET_FB_IDX((*fi)->node),
				       (*fi)->modename);
	*start = buf + offset;
	return len > offset ? len - offset : 0;
}

static ssize_t
fb_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_fix_screeninfo fix;
	char *base_addr;
	ssize_t copy_size;

	if (! fb || ! info->disp)
		return -ENODEV;

	fb->fb_get_fix(&fix,PROC_CONSOLE(info), info);
	base_addr=info->disp->screen_base;
	copy_size=(count + p <= fix.smem_len ? count : fix.smem_len - p);
	if (copy_to_user(buf, base_addr+p, copy_size))
	    return -EFAULT;
	*ppos += copy_size;
	return copy_size;
}

static ssize_t
fb_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	unsigned long p = *ppos;
	struct inode *inode = file->f_dentry->d_inode;
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_fix_screeninfo fix;
	char *base_addr;
	ssize_t copy_size;

	if (! fb || ! info->disp)
		return -ENODEV;

	fb->fb_get_fix(&fix, PROC_CONSOLE(info), info);
	base_addr=info->disp->screen_base;
	copy_size=(count + p <= fix.smem_len ? count : fix.smem_len - p);
	if (copy_from_user(base_addr+p, buf, copy_size))
	    return -EFAULT;
	file->f_pos += copy_size;
	return copy_size;
}


static int set_all_vcs(int fbidx, struct fb_ops *fb,
		       struct fb_var_screeninfo *var, struct fb_info *info)
{
    int unit, err;

    var->activate |= FB_ACTIVATE_TEST;
    err = fb->fb_set_var(var, PROC_CONSOLE(info), info);
    var->activate &= ~FB_ACTIVATE_TEST;
    if (err)
	    return err;
    for (unit = 0; unit < MAX_NR_CONSOLES; unit++)
	    if (fb_display[unit].conp && con2fb_map[unit] == fbidx)
		    fb->fb_set_var(var, unit, info);
    return 0;
}

static void set_con2fb_map(int unit, int newidx)
{
    int oldidx = con2fb_map[unit];
    struct fb_info *oldfb, *newfb;
    struct vc_data *conp;
    char *fontdata;
    unsigned short fontwidth, fontheight, fontwidthlog, fontheightlog;
    int userfont;

    if (newidx != con2fb_map[unit]) {
       oldfb = registered_fb[oldidx];
       newfb = registered_fb[newidx];
       if (newfb->fbops->fb_open(newfb,0))
	   return;
       oldfb->fbops->fb_release(oldfb,0);
       conp = fb_display[unit].conp;
       fontdata = fb_display[unit].fontdata;
       fontwidth = fb_display[unit]._fontwidth;
       fontheight = fb_display[unit]._fontheight;
       fontwidthlog = fb_display[unit]._fontwidthlog;
       fontheightlog = fb_display[unit]._fontheightlog;
       userfont = fb_display[unit].userfont;
       con2fb_map[unit] = newidx;
       fb_display[unit] = *(newfb->disp);
       fb_display[unit].conp = conp;
       fb_display[unit].fontdata = fontdata;
       fb_display[unit]._fontwidth = fontwidth;
       fb_display[unit]._fontheight = fontheight;
       fb_display[unit]._fontwidthlog = fontwidthlog;
       fb_display[unit]._fontheightlog = fontheightlog;
       fb_display[unit].userfont = userfont;
       fb_display[unit].fb_info = newfb;
       if (!newfb->changevar)
	   newfb->changevar = oldfb->changevar;
       /* tell console var has changed */
       if (newfb->changevar)
	   newfb->changevar(unit);
    }
}

#ifdef CONFIG_KMOD
static void try_to_load(int fb)
{
	char modname[16];

	sprintf(modname, "fb%d", fb);
	request_module(modname);
}
#endif /* CONFIG_KMOD */

static int 
fb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	 unsigned long arg)
{
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_cmap cmap;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	struct fb_con2fbmap con2fb;
	int i;
	
	if (! fb)
		return -ENODEV;
	switch (cmd) {
	case FBIOGET_VSCREENINFO:
		if ((i = fb->fb_get_var(&var, PROC_CONSOLE(info), info)))
			return i;
		return copy_to_user((void *) arg, &var,
				    sizeof(var)) ? -EFAULT : 0;
	case FBIOPUT_VSCREENINFO:
		if (copy_from_user(&var, (void *) arg, sizeof(var)))
			return -EFAULT;
		i = var.activate & FB_ACTIVATE_ALL
			    ? set_all_vcs(fbidx, fb, &var, info)
			    : fb->fb_set_var(&var, PROC_CONSOLE(info), info);
		if (i)
			return i;
		if (copy_to_user((void *) arg, &var, sizeof(var)))
			return -EFAULT;
		return 0;
	case FBIOGET_FSCREENINFO:
		if ((i = fb->fb_get_fix(&fix, PROC_CONSOLE(info), info)))
			return i;
		return copy_to_user((void *) arg, &fix, sizeof(fix)) ?
			-EFAULT : 0;
	case FBIOPUTCMAP:
		if (copy_from_user(&cmap, (void *) arg, sizeof(cmap)))
			return -EFAULT;
		return (fb->fb_set_cmap(&cmap, 0, PROC_CONSOLE(info), info));
	case FBIOGETCMAP:
		if (copy_from_user(&cmap, (void *) arg, sizeof(cmap)))
			return -EFAULT;
		return (fb->fb_get_cmap(&cmap, 0, PROC_CONSOLE(info), info));
	case FBIOPAN_DISPLAY:
		if (copy_from_user(&var, (void *) arg, sizeof(var)))
			return -EFAULT;
		if ((i=fb->fb_pan_display(&var, PROC_CONSOLE(info), info)))
			return i;
		if (copy_to_user((void *) arg, &var, sizeof(var)))
			return -EFAULT;
		return i;
	case FBIOGET_CON2FBMAP:
		if (copy_from_user(&con2fb, (void *)arg, sizeof(con2fb)))
			return -EFAULT;
		if (con2fb.console < 1 || con2fb.console > MAX_NR_CONSOLES)
		    return -EINVAL;
		con2fb.framebuffer = con2fb_map[con2fb.console-1];
		return copy_to_user((void *)arg, &con2fb,
				    sizeof(con2fb)) ? -EFAULT : 0;
	case FBIOPUT_CON2FBMAP:
		if (copy_from_user(&con2fb, (void *)arg, sizeof(con2fb)))
			return - EFAULT;
		if (con2fb.console < 0 || con2fb.console > MAX_NR_CONSOLES)
		    return -EINVAL;
		if (con2fb.framebuffer < 0 || con2fb.framebuffer >= FB_MAX)
		    return -EINVAL;
#ifdef CONFIG_KMOD
		if (!registered_fb[con2fb.framebuffer])
		    try_to_load(con2fb.framebuffer);
#endif /* CONFIG_KMOD */
		if (!registered_fb[con2fb.framebuffer])
		    return -EINVAL;
		if (con2fb.console != 0)
		    set_con2fb_map(con2fb.console-1, con2fb.framebuffer);
		else
		    /* set them all */
		    for (i = 0; i < MAX_NR_CONSOLES; i++)
			set_con2fb_map(i, con2fb.framebuffer);
		return 0;
	case FBIOBLANK:
		if (info->blank == 0)
			return -EINVAL;
		(*info->blank)(arg, info);
		return 0;
	default:
		return fb->fb_ioctl(inode, file, cmd, arg, PROC_CONSOLE(info),
				    info);
	}
}

static int 
fb_mmap(struct file *file, struct vm_area_struct * vma)
{
	int fbidx = GET_FB_IDX(file->f_dentry->d_inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];
	struct fb_ops *fb = info->fbops;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	unsigned long start, off;
	u32 len;

	if (vma->vm_pgoff > (~0UL >> PAGE_SHIFT))
		return -EINVAL;
	off = vma->vm_pgoff << PAGE_SHIFT;
	if (!fb)
		return -ENODEV;
	if (fb->fb_mmap)
		return fb->fb_mmap(info, file, vma);

#if defined(__sparc__)
	/* Should never get here, all fb drivers should have their own
	   mmap routines */
	return -EINVAL;
#else
	/* non-SPARC... */

	fb->fb_get_fix(&fix, PROC_CONSOLE(info), info);

	/* frame buffer memory */
	start = fix.smem_start;
	len = (start & ~PAGE_MASK)+fix.smem_len;
	start &= PAGE_MASK;
	len = (len+~PAGE_MASK) & PAGE_MASK;		/* someone's on crack. */
	if (off >= len) {
		/* memory mapped io */
		off -= len;
		fb->fb_get_var(&var, PROC_CONSOLE(info), info);
		if (var.accel_flags)
			return -EINVAL;
		start = fix.mmio_start;
		len = (start & ~PAGE_MASK)+fix.mmio_len;
		start &= PAGE_MASK;
		len = (len+~PAGE_MASK) & PAGE_MASK;
	}
	if ((vma->vm_end - vma->vm_start + off) > len)
		return -EINVAL;
	off += start;
	vma->vm_pgoff = off >> PAGE_SHIFT;
#if defined(__mc68000__)
	if (CPU_IS_020_OR_030)
		pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE030;
	if (CPU_IS_040_OR_060) {
		pgprot_val(vma->vm_page_prot) &= _CACHEMASK040;
		/* Use no-cache mode, serialized */
		pgprot_val(vma->vm_page_prot) |= _PAGE_NOCACHE_S;
	}
#elif defined(__powerpc__)
	pgprot_val(vma->vm_page_prot) |= _PAGE_NO_CACHE|_PAGE_GUARDED;
#elif defined(__alpha__)
	/* Caching is off in the I/O space quadrant by design.  */
#elif defined(__i386__)
	if (boot_cpu_data.x86 > 3)
		pgprot_val(vma->vm_page_prot) |= _PAGE_PCD;
#elif defined(__mips__)
	pgprot_val(vma->vm_page_prot) &= ~_CACHE_MASK;
	pgprot_val(vma->vm_page_prot) |= _CACHE_UNCACHED;
#elif defined(__arm__)
#if defined(CONFIG_CPU_32) && !defined(CONFIG_ARCH_ACORN)
	/* On Acorn architectures, we want to keep the framebuffer
	 * cached.
	 */
	pgprot_val(vma->vm_page_prot) &= ~(PTE_CACHEABLE | PTE_BUFFERABLE);
#endif
#else
#warning What do we have to do here??
#endif
	if (io_remap_page_range(vma->vm_start, off,
			     vma->vm_end - vma->vm_start, vma->vm_page_prot))
		return -EAGAIN;
	return 0;

#endif /* defined(__sparc__) */
}

static int
fb_open(struct inode *inode, struct file *file)
{
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info;

#ifdef CONFIG_KMOD
	if (!(info = registered_fb[fbidx]))
		try_to_load(fbidx);
#endif /* CONFIG_KMOD */
	if (!(info = registered_fb[fbidx]))
		return -ENODEV;
	return info->fbops->fb_open(info,1);
}

static int 
fb_release(struct inode *inode, struct file *file)
{
	int fbidx = GET_FB_IDX(inode->i_rdev);
	struct fb_info *info = registered_fb[fbidx];

	info->fbops->fb_release(info,1);
	return 0;
}

static struct file_operations fb_fops = {
	NULL,		/* lseek	*/
	fb_read,	/* read		*/
	fb_write,	/* write	*/
	NULL,		/* readdir 	*/
	NULL,		/* poll 	*/
	fb_ioctl,	/* ioctl 	*/
	fb_mmap,	/* mmap		*/
	fb_open,	/* open 	*/
	NULL,		/* flush	*/
	fb_release,	/* release 	*/
	NULL		/* fsync 	*/
};

int
register_framebuffer(struct fb_info *fb_info)
{
	int i, j;
	static int fb_ever_opened[FB_MAX];
	static int first = 1;

	if (num_registered_fb == FB_MAX)
		return -ENXIO;
	num_registered_fb++;
	for (i = 0 ; i < FB_MAX; i++)
		if (!registered_fb[i])
			break;
	fb_info->node=GET_INODE(i);
	registered_fb[i] = fb_info;
	if (!fb_ever_opened[i]) {
		/*
		 *  We assume initial frame buffer devices can be opened this
		 *  many times
		 */
		for (j = 0; j < MAX_NR_CONSOLES; j++)
			if (con2fb_map[j] == i)
				fb_info->fbops->fb_open(fb_info,0);
		fb_ever_opened[i] = 1;
	}

	if (first) {
		first = 0;
		take_over_console(&fb_con, first_fb_vc, last_fb_vc, fbcon_is_default);
	}

	return 0;
}

int
unregister_framebuffer(const struct fb_info *fb_info)
{
	int i, j;

	i = GET_FB_IDX(fb_info->node);
	for (j = 0; j < MAX_NR_CONSOLES; j++)
		if (con2fb_map[j] == i)
			return -EBUSY;
	if (!registered_fb[i])
		return -EINVAL; 
	registered_fb[i]=NULL;
	num_registered_fb--;
	return 0;
}

void __init 
fbmem_init(void)
{
	int i;

	create_proc_read_entry("fb", 0, 0, fbmem_read_proc, NULL);

	if (register_chrdev(FB_MAJOR,"fb",&fb_fops))
		printk("unable to get major %d for fb devs\n", FB_MAJOR);

	/*
	 *  Probe for all builtin frame buffer devices
	 */
	for (i = 0; i < num_pref_init_funcs; i++)
		pref_init_funcs[i]();

	for (i = 0; i < NUM_FB_DRIVERS; i++)
		if (fb_drivers[i].init)
			fb_drivers[i].init();
}

    /*
     *  Command line options
     */

int __init video_setup(char *options)
{
    int i, j;

    if (!options || !*options)
	    return 0;
	    
    if (!strncmp(options, "scrollback:", 11)) {
	    options += 11;
	    if (*options) {
		fbcon_softback_size = simple_strtoul(options, &options, 0);
		if (*options == 'k' || *options == 'K') {
			fbcon_softback_size *= 1024;
			options++;
		}
		if (*options != ',')
			return 0;
		options++;
	    } else
	        return 0;
    }

    if (!strncmp(options, "map:", 4)) {
	    options += 4;
	    if (*options)
		    for (i = 0, j = 0; i < MAX_NR_CONSOLES; i++) {
			    if (!options[j])
				    j = 0;
			    con2fb_map[i] = (options[j++]-'0') % FB_MAX;
		    }
	    return 0;
    }
    
    if (!strncmp(options, "vc:", 3)) {
	    options += 3;
	    if (*options)
		first_fb_vc = simple_strtoul(options, &options, 10) - 1;
	    if (first_fb_vc < 0)
		first_fb_vc = 0;
	    if (*options++ == '-')
		last_fb_vc = simple_strtoul(options, &options, 10) - 1;
	    fbcon_is_default = 0;
    }

    if (num_pref_init_funcs == FB_MAX)
	    return 0;

    for (i = 0; i < NUM_FB_DRIVERS; i++) {
	    j = strlen(fb_drivers[i].name);
	    if (!strncmp(options, fb_drivers[i].name, j) &&
		options[j] == ':') {
		    if (!strcmp(options+j+1, "off"))
			    fb_drivers[i].init = NULL;
		    else {
			    if (fb_drivers[i].init) {
				    pref_init_funcs[num_pref_init_funcs++] =
					    fb_drivers[i].init;
				    fb_drivers[i].init = NULL;
			    }
			    if (fb_drivers[i].setup)
				    fb_drivers[i].setup(options+j+1);
		    }
		    return 0;
	    }
    }

    /*
     * If we get here no fb was specified.
     * We consider the argument to be a global video mode option.
     */
    global_mode_option = options;
    return 0;
}

__setup("video=", video_setup);

    /*
     *  Visible symbols for modules
     */

EXPORT_SYMBOL(register_framebuffer);
EXPORT_SYMBOL(unregister_framebuffer);
