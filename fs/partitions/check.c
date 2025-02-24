/*
 *  Code extracted from drivers/block/genhd.c
 *  Copyright (C) 1991-1998  Linus Torvalds
 *  Re-organised Feb 1998 Russell King
 *
 *  We now have independent partition support from the
 *  block drivers, which allows all the partition code to
 *  be grouped in one location, and it to be mostly self
 *  contained.
 *
 *  Added needed MAJORS for new pairs, {hdi,hdj}, {hdk,hdl}
 */

#include <linux/config.h>
#include <linux/fs.h>
#include <linux/genhd.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/blk.h>
#include <linux/init.h>

#include "check.h"

#include "acorn.h"
#include "amiga.h"
#include "atari.h"
#include "mac.h"
#include "msdos.h"
#include "osf.h"
#include "sgi.h"
#include "sun.h"

extern void device_init(void);
extern void md_setup_drive(void);
extern int *blk_size[];
extern void rd_load(void);
extern void initrd_load(void);

struct gendisk *gendisk_head;

static int (*check_part[])(struct gendisk *hd, kdev_t dev, unsigned long first_sect, int first_minor) = {
#ifdef CONFIG_ACORN_PARTITION
	acorn_partition,
#endif
#ifdef CONFIG_MSDOS_PARTITION
	msdos_partition,
#endif
#ifdef CONFIG_OSF_PARTITION
	osf_partition,
#endif
#ifdef CONFIG_SUN_PARTITION
	sun_partition,
#endif
#ifdef CONFIG_AMIGA_PARTITION
	amiga_partition,
#endif
#ifdef CONFIG_ATARI_PARTITION
	atari_partition,
#endif
#ifdef CONFIG_MAC_PARTITION
	mac_partition,
#endif
#ifdef CONFIG_SGI_PARTITION
	sgi_partition,
#endif
#ifdef CONFIG_ULTRIX_PARTITION
	ultrix_partition,
#endif
	NULL
};

/*
 * disk_name() is used by genhd.c and blkpg.c.
 * It formats the devicename of the indicated disk into
 * the supplied buffer (of size at least 32), and returns
 * a pointer to that same buffer (for convenience).
 */
char *disk_name (struct gendisk *hd, int minor, char *buf)
{
	unsigned int part;
	const char *maj = hd->major_name;
	int unit = (minor >> hd->minor_shift) + 'a';

	/*
	 * IDE devices use multiple major numbers, but the drives
	 * are named as:  {hda,hdb}, {hdc,hdd}, {hde,hdf}, {hdg,hdh}..
	 * This requires special handling here.
	 */
	switch (hd->major) {
		case IDE9_MAJOR:
			unit += 2;
		case IDE8_MAJOR:
			unit += 2;
		case IDE7_MAJOR:
			unit += 2;
		case IDE6_MAJOR:
			unit += 2;
		case IDE5_MAJOR:
			unit += 2;
		case IDE4_MAJOR:
			unit += 2;
		case IDE3_MAJOR:
			unit += 2;
		case IDE2_MAJOR:
			unit += 2;
		case IDE1_MAJOR:
			unit += 2;
		case IDE0_MAJOR:
			maj = "hd";
			break;
	}
	part = minor & ((1 << hd->minor_shift) - 1);
	if (hd->major >= SCSI_DISK1_MAJOR && hd->major <= SCSI_DISK7_MAJOR) {
		unit = unit + (hd->major - SCSI_DISK1_MAJOR + 1) * 16;
		if (unit > 'z') {
			unit -= 'z' + 1;
			sprintf(buf, "sd%c%c", 'a' + unit / 26, 'a' + unit % 26);
			if (part)
				sprintf(buf + 4, "%d", part);
			return buf;
		}
	}
	if (hd->major >= COMPAQ_SMART2_MAJOR && hd->major <= COMPAQ_SMART2_MAJOR+7) {
		int ctlr = hd->major - COMPAQ_SMART2_MAJOR;
 		int disk = minor >> hd->minor_shift;
 		int part = minor & (( 1 << hd->minor_shift) - 1);
 		if (part == 0)
 			sprintf(buf, "%s/c%dd%d", maj, ctlr, disk);
 		else
 			sprintf(buf, "%s/c%dd%dp%d", maj, ctlr, disk, part);
 		return buf;
 	}
	if (hd->major >= DAC960_MAJOR && hd->major <= DAC960_MAJOR+7) {
		int ctlr = hd->major - DAC960_MAJOR;
 		int disk = minor >> hd->minor_shift;
 		int part = minor & (( 1 << hd->minor_shift) - 1);
 		if (part == 0)
 			sprintf(buf, "%s/c%dd%d", maj, ctlr, disk);
 		else
 			sprintf(buf, "%s/c%dd%dp%d", maj, ctlr, disk, part);
 		return buf;
 	}
	if (part)
		sprintf(buf, "%s%c%d", maj, unit, part);
	else
		sprintf(buf, "%s%c", maj, unit);
	return buf;
}

/*
 * Add a partitions details to the devices partition description.
 */
void add_gd_partition(struct gendisk *hd, int minor, int start, int size)
{
	char buf[40];
	hd->part[minor].start_sect = start;
	hd->part[minor].nr_sects   = size;
	if (hd->major >= COMPAQ_SMART2_MAJOR+0 && hd->major <= COMPAQ_SMART2_MAJOR+7)
		printk(" p%d", (minor & ((1 << hd->minor_shift) - 1)));
	else
		printk(" %s", disk_name(hd, minor, buf));
}

int get_hardsect_size(kdev_t dev)
{
	if (hardsect_size[MAJOR(dev)] != NULL)
		return hardsect_size[MAJOR(dev)][MINOR(dev)];
	else
		return 512;
}

unsigned int get_ptable_blocksize(kdev_t dev)
{
	int ret = 1024;

	/*
	 * See whether the low-level driver has given us a minumum blocksize.
	 * If so, check to see whether it is larger than the default of 1024.
	 */
	if (!blksize_size[MAJOR(dev)])
		return ret;

	/*
	 * Check for certain special power of two sizes that we allow.
	 * With anything larger than 1024, we must force the blocksize up to
	 * the natural blocksize for the device so that we don't have to try
	 * and read partial sectors.  Anything smaller should be just fine.
	 */

	switch (blksize_size[MAJOR(dev)][MINOR(dev)]) {
		case 2048:
			ret = 2048;
			break;
		case 4096:
			ret = 4096;
			break;
		case 8192:
			ret = 8192;
			break;
		case 1024:
		case 512:
		case 256:
		case 0:
			/*
			 * These are all OK.
			 */
			break;
		default:
			panic("Strange blocksize for partition table\n");
	}

	return ret;
}

#ifdef CONFIG_PROC_FS
int get_partition_list(char * page)
{
	struct gendisk *p;
	char buf[40];
	int n, len;

	len = sprintf(page, "major minor  #blocks  name\n\n");
	for (p = gendisk_head; p; p = p->next) {
		for (n=0; n < (p->nr_real << p->minor_shift); n++) {
			if (p->part[n].nr_sects && len < PAGE_SIZE - 80) {
				len += sprintf(page+len,
					       "%4d  %4d %10d %s\n",
					       p->major, n, p->sizes[n],
					       disk_name(p, n, buf));
			}
		}
	}
	return len;
}
#endif

void check_partition(struct gendisk *hd, kdev_t dev, int first_part_minor)
{
	static int first_time = 1;
	unsigned long first_sector;
	char buf[40];
	int i;

	if (first_time)
		printk(KERN_INFO "Partition check:\n");
	first_time = 0;
	first_sector = hd->part[MINOR(dev)].start_sect;

	/*
	 * This is a kludge to allow the partition check to be
	 * skipped for specific drives (e.g. IDE CD-ROM drives)
	 */
	if ((int)first_sector == -1) {
		hd->part[MINOR(dev)].start_sect = 0;
		return;
	}

	printk(KERN_INFO " %s:", disk_name(hd, MINOR(dev), buf));
	for (i = 0; check_part[i]; i++)
		if (check_part[i](hd, dev, first_sector, first_part_minor))
			return;

	printk(" unknown partition table\n");
}

/*
 * This function will re-read the partition tables for a given device,
 * and set things back up again.  There are some important caveats,
 * however.  You must ensure that no one is using the device, and no one
 * can start using the device while this function is being executed.
 *
 * Much of the cleanup from the old partition tables should have already been
 * done
 */
void resetup_one_dev(struct gendisk *dev, int drive)
{
	int i;
	int first_minor	= drive << dev->minor_shift;
	int end_minor	= first_minor + dev->max_p;

	blk_size[dev->major] = NULL;
	check_partition(dev, MKDEV(dev->major, first_minor), 1 + first_minor);

 	/*
 	 * We need to set the sizes array before we will be able to access
 	 * any of the partitions on this device.
 	 */
	if (dev->sizes != NULL) {	/* optional safeguard in ll_rw_blk.c */
		for (i = first_minor; i < end_minor; i++)
			dev->sizes[i] = dev->part[i].nr_sects >> (BLOCK_SIZE_BITS - 9);
		blk_size[dev->major] = dev->sizes;
	}
}

static inline void setup_dev(struct gendisk *dev)
{
	int i, drive;
	int end_minor	= dev->max_nr * dev->max_p;

	blk_size[dev->major] = NULL;
	for (i = 0; i < end_minor; i++) {
		dev->part[i].start_sect = 0;
		dev->part[i].nr_sects = 0;
		dev->sizes[i] = 0;
	}
	dev->init(dev);	
	for (drive = 0 ; drive < dev->nr_real ; drive++)
		resetup_one_dev(dev, drive);
}

int __init partition_setup(void)
{
	struct gendisk *p;

	device_init();

	for (p = gendisk_head ; p ; p=p->next)
		setup_dev(p);

#ifdef CONFIG_BLK_DEV_RAM
#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start && mount_initrd) initrd_load();
	else
#endif
	rd_load();
#endif
#ifdef CONFIG_MD_BOOT
	md_setup_drive();
#endif

	return 0;
}

__initcall(partition_setup);
