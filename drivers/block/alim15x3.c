/*
 * linux/drivers/block/alim15x3.c	Version 0.06	Sept. 3, 1999
 *
 *  Copyright (C) 1998-99 Michel Aubry, Maintainer
 *  Copyright (C) 1998-99 Andrzej Krzysztofowicz, Maintainer
 *
 *  Copyright (C) 1998-99 Andre Hedrick (andre@suse.com)
 *  May be copied or modified under the terms of the GNU General Public License
 *
 *  (U)DMA capable version of ali 1533/1543(C), 1535(D)
 *
 *  version: 1.0 beta2 (Sep. 2, 1999)
 *	e-mail your problems to cjtsai@ali.com.tw
 *
 **********************************************************************
 *  9/7/99 --Parts from the above author are included and need to be
 *  converted into standard interface, once I finish the thought.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/hdreg.h>
#include <linux/ide.h>

#include <asm/io.h>

#include "ide_modes.h"

#define DISPLAY_ALI_TIMINGS

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
#include <linux/stat.h>
#include <linux/proc_fs.h>

static int ali_get_info(char *buffer, char **addr, off_t offset, int count);
extern int (*ali_display_info)(char *, char **, off_t, int);  /* ide-proc.c */
struct pci_dev *bmide_dev;

char *fifo[4] = {
	"FIFO Off",
	"FIFO On ",
	"DMA mode",
	"PIO mode" };

char *udmaT[8] = {
	"1.5T",
	"  2T",
	"2.5T",
	"  3T",
	"3.5T",
	"  4T",
	"  6T",
	"  8T"
};

char *channel_status[8] = {
	"OK            ",
	"busy          ",
	"DRQ           ",
	"DRQ busy      ",
	"error         ",
	"error busy    ",
	"error DRQ     ",
	"error DRQ busy"
};

static int ali_get_info(char *buffer, char **addr, off_t offset, int count)
{
	byte reg53h, reg5xh, reg5yh, reg5xh1, reg5yh1;
	unsigned int bibma;
	byte c0, c1;
	byte rev, tmp;
	char *p = buffer;
	char *q;

	/* fetch rev. */
	pci_read_config_byte(bmide_dev, 0x08, &rev);
	if (rev >= 0xc1)	/* M1543C or newer */
		udmaT[7] = " ???";
	else
		fifo[3]  = "   ???  ";

	/* first fetch bibma: */
	pci_read_config_dword(bmide_dev, 0x20, &bibma);
	bibma = (bibma & 0xfff0) ;
	/*
	 * at that point bibma+0x2 et bibma+0xa are byte
	 * registers to investigate:
	 */
	c0 = inb((unsigned short)bibma + 0x02);
	c1 = inb((unsigned short)bibma + 0x0a);

	p += sprintf(p,
		"\n                                Ali M15x3 Chipset.\n");
	p += sprintf(p,
		"                                ------------------\n");
	pci_read_config_byte(bmide_dev, 0x78, &reg53h);
	p += sprintf(p, "PCI Clock: %d.\n", reg53h);

	pci_read_config_byte(bmide_dev, 0x53, &reg53h);
	p += sprintf(p,
		"CD_ROM FIFO:%s, CD_ROM DMA:%s\n",
		(reg53h & 0x02) ? "Yes" : "No ",
		(reg53h & 0x01) ? "Yes" : "No " );
	pci_read_config_byte(bmide_dev, 0x74, &reg53h);
	p += sprintf(p,
		"FIFO Status: contains %d Words, runs%s%s\n\n",
		(reg53h & 0x3f),
		(reg53h & 0x40) ? " OVERWR" : "",
		(reg53h & 0x80) ? " OVERRD." : "." );

	p += sprintf(p,
		"-------------------primary channel-------------------secondary channel---------\n\n");

	pci_read_config_byte(bmide_dev, 0x09, &reg53h);
	p += sprintf(p,
		"channel status:       %s                               %s\n",
		(reg53h & 0x20) ? "On " : "Off",
		(reg53h & 0x10) ? "On " : "Off" );

	p += sprintf(p,
		"both channels togth:  %s                               %s\n",
		(c0&0x80) ? "No " : "Yes",
		(c1&0x80) ? "No " : "Yes" );

	pci_read_config_byte(bmide_dev, 0x76, &reg53h);
	p += sprintf(p,
		"Channel state:        %s                    %s\n",
		channel_status[reg53h & 0x07],
		channel_status[(reg53h & 0x70) >> 4] );

	pci_read_config_byte(bmide_dev, 0x58, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5c, &reg5yh);
	p += sprintf(p,
		"Add. Setup Timing:    %dT                                %dT\n",
		(reg5xh & 0x07) ? (reg5xh & 0x07) : 8,
		(reg5yh & 0x07) ? (reg5yh & 0x07) : 8 );

	pci_read_config_byte(bmide_dev, 0x59, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5d, &reg5yh);
	p += sprintf(p,
		"Command Act. Count:   %dT                                %dT\n"
		"Command Rec. Count:   %dT                               %dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8, 
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16 );

	p += sprintf(p,
		"----------------drive0-----------drive1------------drive0-----------drive1------\n\n");
	p += sprintf(p,
		"DMA enabled:      %s              %s               %s              %s\n",
		(c0&0x20) ? "Yes" : "No ",
		(c0&0x40) ? "Yes" : "No ",
		(c1&0x20) ? "Yes" : "No ",
		(c1&0x40) ? "Yes" : "No " );

	pci_read_config_byte(bmide_dev, 0x54, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x55, &reg5yh);
	q = "FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n";
	if (rev < 0xc1) {
		if ((rev == 0x20) && (pci_read_config_byte(bmide_dev, 0x4f, &tmp), (tmp &= 0x20))) {
			p += sprintf(p, q, 8, 8, 8, 8);
		} else {
			p += sprintf(p, q,
				(reg5xh & 0x03) + 12,
				((reg5xh & 0x30)>>4) + 12,
				(reg5yh & 0x03) + 12,
				((reg5yh & 0x30)>>4) + 12 );
		}
	} else {
		p += sprintf(p, q,
			(tmp = (reg5xh & 0x03)) ? (tmp << 3) : 4,
			(tmp = ((reg5xh & 0x30)>>4)) ? (tmp << 3) : 4,
			(tmp = (reg5yh & 0x03)) ? (tmp << 3) : 4,
			(tmp = ((reg5yh & 0x30)>>4)) ? (tmp << 3) : 4 );
	}

#if 0
	p += sprintf(p, 
		"FIFO threshold:   %2d Words         %2d Words          %2d Words         %2d Words\n",
		(reg5xh & 0x03) + 12,
		((reg5xh & 0x30)>>4) + 12,
		(reg5yh & 0x03) + 12,
		((reg5yh & 0x30)>>4) + 12 );
#endif

	p += sprintf(p,
		"FIFO mode:        %s         %s          %s         %s\n",
		fifo[((reg5xh & 0x0c) >> 2)],
		fifo[((reg5xh & 0xc0) >> 6)],
		fifo[((reg5yh & 0x0c) >> 2)],
		fifo[((reg5yh & 0xc0) >> 6)] );

	pci_read_config_byte(bmide_dev, 0x5a, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x5b, &reg5xh1);
	pci_read_config_byte(bmide_dev, 0x5e, &reg5yh);
	pci_read_config_byte(bmide_dev, 0x5f, &reg5yh1);

	p += sprintf(p,/*
		"------------------drive0-----------drive1------------drive0-----------drive1------\n")*/
		"Dt RW act. Cnt    %2dT              %2dT               %2dT              %2dT\n"
		"Dt RW rec. Cnt    %2dT              %2dT               %2dT              %2dT\n\n",
		(reg5xh & 0x70) ? ((reg5xh & 0x70) >> 4) : 8,
		(reg5xh1 & 0x70) ? ((reg5xh1 & 0x70) >> 4) : 8,
		(reg5yh & 0x70) ? ((reg5yh & 0x70) >> 4) : 8,
		(reg5yh1 & 0x70) ? ((reg5yh1 & 0x70) >> 4) : 8,
		(reg5xh & 0x0f) ? (reg5xh & 0x0f) : 16,
		(reg5xh1 & 0x0f) ? (reg5xh1 & 0x0f) : 16,
		(reg5yh & 0x0f) ? (reg5yh & 0x0f) : 16,
		(reg5yh1 & 0x0f) ? (reg5yh1 & 0x0f) : 16 );

	p += sprintf(p,
		"-----------------------------------UDMA Timings--------------------------------\n\n");

	pci_read_config_byte(bmide_dev, 0x56, &reg5xh);
	pci_read_config_byte(bmide_dev, 0x57, &reg5yh);
	p += sprintf(p,
		"UDMA:             %s               %s                %s               %s\n"
		"UDMA timings:     %s             %s              %s             %s\n\n",
		(reg5xh & 0x08) ? "OK" : "No",
		(reg5xh & 0x80) ? "OK" : "No",
		(reg5yh & 0x08) ? "OK" : "No",
		(reg5yh & 0x80) ? "OK" : "No",
		udmaT[(reg5xh & 0x07)],
		udmaT[(reg5xh & 0x70) >> 4],
		udmaT[reg5yh & 0x07],
		udmaT[(reg5yh & 0x70) >> 4] );

	return p-buffer; /* => must be less than 4k! */
}
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

static byte m5229_revision	= 0;
static byte chip_is_1543c_e	= 0;
static byte cable_80_pin[2]	= { 0, 0 };

byte ali_proc = 0;
static struct pci_dev *isa_dev;

static void ali15x3_tune_drive (ide_drive_t *drive, byte pio)
{
	ide_pio_data_t d;
	ide_hwif_t *hwif = HWIF(drive);
	struct pci_dev *dev = hwif->pci_dev;
	int s_time, a_time, c_time;
	byte s_clc, a_clc, r_clc;
	unsigned long flags;
	int bus_speed = ide_system_bus_speed();
	int port = hwif->index ? 0x5c : 0x58;

	pio = ide_get_best_pio_mode(drive, pio, 5, &d);
	s_time = ide_pio_timings[pio].setup_time;
	a_time = ide_pio_timings[pio].active_time;
	if ((s_clc = (s_time * bus_speed + 999) / 1000) >= 8)
		s_clc = 0;
	if ((a_clc = (a_time * bus_speed + 999) / 1000) >= 8)
		a_clc = 0;
	c_time = ide_pio_timings[pio].cycle_time;

#if 0
	if ((r_clc = ((c_time - s_time - a_time) * bus_speed + 999) / 1000) >= 16)
		r_clc = 0;
#endif

	if (!(r_clc = (c_time * bus_speed + 999) / 1000 - a_clc - s_clc)) {
		r_clc = 1;
	} else {
		if (r_clc >= 16)
		r_clc = 0;
	}
	save_flags(flags);
	cli();
	pci_write_config_byte(dev, port, s_clc);
	pci_write_config_byte(dev, port+drive->select.b.unit+2, (a_clc << 4) | r_clc);
	restore_flags(flags);

	/*
	 * setup   active  rec
	 * { 70,   165,    365 },   PIO Mode 0
	 * { 50,   125,    208 },   PIO Mode 1
	 * { 30,   100,    110 },   PIO Mode 2
	 * { 30,   80,     70  },   PIO Mode 3 with IORDY
	 * { 25,   70,     25  },   PIO Mode 4 with IORDY  ns
	 * { 20,   50,     30  }    PIO Mode 5 with IORDY (nonstandard)
	 */

}

static __inline__ unsigned char dma2_bits_to_command(unsigned char bits)
{
	if (bits & 0x04)
		return XFER_MW_DMA_2;
	if (bits & 0x02)
		return XFER_MW_DMA_1;
	return XFER_MW_DMA_0;
}

static __inline__ unsigned char udma2_bits_to_command(unsigned char bits)
{
	if (bits & 0x10)
		return XFER_UDMA_4;
	if (bits & 0x08)
		return XFER_UDMA_3;
	if (bits & 0x04)
		return XFER_UDMA_2;
	if (bits & 0x02)
		return XFER_UDMA_1;
	return XFER_UDMA_0;
}

static __inline__ int wait_for_ready(ide_drive_t *drive)
{
	int timeout = 20000;	/* (old value: 100) */
	byte stat;

	while (--timeout) {
		stat = GET_STAT();
		/*
		 * printk("STAT(%2x) ", stat);
		 */
		if (!(stat & BUSY_STAT)) {
			if ((stat & READY_STAT) || (stat & ERR_STAT)) {
				break;
			}
		}
		/*
		 * (old value: 100)
		 */
		udelay(150);
	}
	if ((stat & ERR_STAT) || timeout <= 0)
		return 1;
	return 0;
}

static void ali15x3_do_setfeature(ide_drive_t *drive, byte command)
{
	unsigned long flags;
	byte old_select;

	save_flags(flags);
	cli();
		
	/* save old selected device */
	old_select = IN_BYTE(IDE_SELECT_REG);
	/* "SELECT " */
	OUT_BYTE(drive->select.all, IDE_SELECT_REG);
	/* "SETXFER " */
	OUT_BYTE(SETFEATURES_XFER, IDE_FEATURE_REG);
	/* "CMND " */
	OUT_BYTE(command, IDE_NSECTOR_REG);

	if(wait_for_ready(drive))	/* "wait " */
		goto out;

	/* "SETFEATURE " */
	OUT_BYTE(WIN_SETFEATURES, IDE_COMMAND_REG);
	/* "wait " */
	(void) wait_for_ready(drive);

out:
	/*
	 * restore to old "selected device"
	 */
	OUT_BYTE(old_select, IDE_SELECT_REG);
	restore_flags(flags);
}

static void ali15x3_dma2_enable(ide_drive_t *drive, unsigned long dma_base)
{
	byte unit = (drive->select.b.unit & 0x01);
	byte bits = (drive->id->dma_mword | drive->id->dma_1word) & 0x07;
	byte tmpbyte;
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	int m5229_udma_setting_index = hwif->channel? 0x57 : 0x56;		
	
	ali15x3_do_setfeature(drive, dma2_bits_to_command(bits));

	/*
	 * clear "ultra enable" bit
	 */
	pci_read_config_byte(hwif->pci_dev, m5229_udma_setting_index, &tmpbyte);
	if (unit) {
		tmpbyte &= 0x7f;
	} else {
		tmpbyte &= 0xf7;
	}
	save_flags(flags);
	cli();
	pci_write_config_byte(hwif->pci_dev, m5229_udma_setting_index, tmpbyte);
	restore_flags(flags);
	drive->id->dma_ultra = 0x00;

	/*
	 * Enable DMA
	 */
	outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);
	printk("ALI15X3: MultiWord DMA enabled\n");
}

static void ali15x3_udma_enable(ide_drive_t *drive, unsigned long dma_base)
{	
	byte unit = (drive->select.b.unit & 0x01);
	byte bits = drive->id->dma_ultra & 0x1f;	
	byte tmpbyte;
	ide_hwif_t *hwif = HWIF(drive);
	unsigned long flags;
	unsigned char udma_mode = 0;
	int m5229_udma_setting_index = hwif->channel? 0x57 : 0x56;

	if (bits & 0x18) {
		/*
		 * 00011000, disk: ultra66
		 */
		if (m5229_revision < 0xc2) {
			/*
			 * controller: ultra33
			 */
			bits = 0x04;
			/*
			 * 00000100, use ultra33, mode 2
			 */
			drive->id->dma_ultra &= ~0xFF00;
			drive->id->dma_ultra |= 0x0004;			
		} else {
			/*
			 * controller: ultra66
			 *
			 * Try to detect word93 bit13 and
			 * 80-pin cable (from host view)
			 */
			if (!((drive->id->word93 & 0x2000) &&
			    cable_80_pin[hwif->channel])) {	
				bits = 0x04;
				/*
				 * 00000100, use ultra33, mode 2
				 */
				drive->id->dma_ultra &= ~0xFF00;
				drive->id->dma_ultra |= 0x0004;
			}
		}
	}

	/*
	 * set feature regardless
	 */
	ali15x3_do_setfeature(drive, udma_mode = udma2_bits_to_command(bits));
	udma_mode &= 0x0f;	/* get UDMA mode */

	/*
	 * Enable DMA and UltraDMA
	 */
	outb(inb(dma_base+2)|(1<<(5+unit)), dma_base+2);
	/*
	 * m5229 ultra
	 */
	pci_read_config_byte(hwif->pci_dev, m5229_udma_setting_index, &tmpbyte);
	/*
	 * clear bit0~3 or bit 4~7
	 */
	tmpbyte &= (0x0f << ((1-unit) << 2));
	/*
	 * enable ultra dma and set timing
	 */
	tmpbyte |= ((0x08 | (4-udma_mode)) << (unit << 2));
	/*
	 * set to m5229
	 */
	save_flags(flags);
	cli();		
	pci_write_config_byte(hwif->pci_dev, m5229_udma_setting_index, tmpbyte);
	restore_flags(flags);

	if (udma_mode >= 3) {
		/*
		 * ultra 66
		 */
		pci_read_config_byte(hwif->pci_dev, 0x4b, &tmpbyte);
		tmpbyte |= 1;			
		save_flags(flags);
		cli();
		pci_write_config_byte(hwif->pci_dev, 0x4b, tmpbyte);		
		restore_flags(flags);
	}

	printk("ALI15X3: Ultra DMA enabled\n");
}

static int ali15x3_dma_onoff(ide_drive_t *drive, int enable)
{
	if (enable) {
		ide_hwif_t *hwif = HWIF(drive);
		unsigned long dma_base = hwif->dma_base;
		struct hd_driveid *id = drive->id;
				
		if ((id->field_valid & 0x0004) && 
		    (id->dma_ultra & 0x001f)) {
			/*
			 * 1543C_E, in ultra mode, WDC "harddisk"
			 * will cause "CRC" errors (even if no CRC problem),
			 * so we try to use "DMA" here
			 */
			if (m5229_revision <= 0x20) {
				/*
				 * Normal MultiWord DMA modes.
				 */
				ali15x3_dma2_enable(drive, dma_base);
			} else if ((m5229_revision < 0xC2) && 
				   ((drive->media!=ide_disk) ||
				    (chip_is_1543c_e &&
				     strstr(id->model, "WDC ")))) {
				/*
				 * Normal MultiWord DMA modes.
				 */
				ali15x3_dma2_enable(drive, dma_base);
			} else {
				/*
				 * m5229_revision >= 0xC2 for UltraDMA modes.
				 */
				ali15x3_udma_enable(drive, dma_base);
			}
		} else {
			/*
			 * Normal MultiWord DMA modes.
			 */
			ali15x3_dma2_enable(drive, dma_base);
		}
	}

	drive->using_dma = enable;	/* on, off */
	return 0;
}

static int ali15x3_config_drive_for_dma(ide_drive_t *drive)
{
	struct hd_driveid *id = drive->id;
	ide_hwif_t *hwif = HWIF(drive);

	if ((m5229_revision<=0x20) && (drive->media!=ide_disk))
		return hwif->dmaproc(ide_dma_off_quietly, drive);
	/*
	 * Even if the drive is not _currently_ in a DMA
	 * mode, we succeed, and we'll enable it manually
	 * below in alim15x3_dma_onoff
	 */
	if ((id != NULL) && (id->capability & 1) && hwif->autodma) {
		if (id->field_valid & 0x0004) {
			if (id->dma_ultra & 0x001F)
				return hwif->dmaproc(ide_dma_on, drive);
		}
		if (id->field_valid & 0x0002) {
			if ((id->dma_mword & 0x0007) || (id->dma_1word & 0x0007))
				return hwif->dmaproc(ide_dma_on, drive);
		}
	}
	return hwif->dmaproc(ide_dma_off_quietly, drive);
}

static int ali15x3_dmaproc (ide_dma_action_t func, ide_drive_t *drive)
{
	switch(func) {
		case ide_dma_check:
			return ali15x3_config_drive_for_dma(drive);
		case ide_dma_on:
		case ide_dma_off:
		case ide_dma_off_quietly:
			return ali15x3_dma_onoff(drive, (func == ide_dma_on));
		case ide_dma_write:
			if ((m5229_revision < 0xC2) && (drive->media != ide_disk))
				return 1;	/* try PIO instead of DMA */
			break;
		default:
			break;
	}

	return ide_dmaproc(func, drive);	/* use standard DMA stuff */
}

unsigned int __init pci_init_ali15x3 (struct pci_dev *dev, const char *name)
{
	struct pci_dev *isa;
	unsigned long fixdma_base = dev->resource[4].start;
	byte tmpbyte;

	pci_read_config_byte(dev, PCI_REVISION_ID, &m5229_revision);

	for (isa = pci_devices; isa; isa=isa->next) {
		/*
		 * look for ISA bridge
		 */
		if (isa->vendor == PCI_VENDOR_ID_AL &&
		    isa->device == PCI_DEVICE_ID_AL_M1533) {
			isa_dev = isa;
			break;
		}
	}

	if (!fixdma_base || fixdma_base == PCI_BASE_ADDRESS_IO_MASK) {
		/*
		 *
		 */
	} else {
		/*
		 * enable DMA capable bit, and "not" simplex only
		 */
		outb(inb(fixdma_base+2) & 0x60, fixdma_base+2);

		if (inb(fixdma_base+2) & 0x80)
			printk("%s: simplex device: DMA will fail!!\n", name);
	}

	/*
	 * FIXME !!! This detection needs to be in "ata66_ali15x3()"
	 * below as a standard detection return.
	 */

	if (m5229_revision >= 0xC2) {
		unsigned long flags;
		/*
		 * 1543C-B?, 1535, 1535D, 1553
		 * Note 1: not all "motherboard" support this detection
		 * Note 2: if no udma 66 device, the detection may "error".
		 *         but in this case, we will not set the device to
		 *         ultra 66, the detection result is not important
		 */
		save_flags(flags);
		cli();

		/*
		 * enable "Cable Detection", m5229, 0x4b, bit3
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		pci_write_config_byte(dev, 0x4b, tmpbyte | 0x08);

		/*
		 * set south-bridge's enable bit, m1533, 0x79
		 */
		pci_read_config_byte(isa_dev, 0x79, &tmpbyte);
		if (m5229_revision == 0xC2) {
			/*
			 * 1543C-B0 (m1533, 0x79, bit 2)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x04);
		} else if (m5229_revision == 0xC3) {
			/*
			 * 1553/1535 (m1533, 0x79, bit 1)
			 */
			pci_write_config_byte(isa_dev, 0x79, tmpbyte | 0x02);
		}
		restore_flags(flags);
		/*
		 * Ultra66 cable detection (from Host View)
		 * m5229, 0x4a, bit0: primary, bit1: secondary 80 pin
		 */
		pci_read_config_byte(dev, 0x4a, &tmpbyte);
		/*
		 * 0x4a, bit0 is 0 => primary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x01))
			cable_80_pin[0] = 1;
		/*
		 * 0x4a, bit1 is 0 => secondary channel
		 * has 80-pin (from host view)
		 */
		if (!(tmpbyte & 0x02))
			cable_80_pin[1] = 1;
	} else {
		unsigned long flags;
		/*
		 * revision 0x20 (1543-E, 1543-F)
		 * revision 0xC0, 0xC1 (1543C-C, 1543C-D, 1543C-E)
		 * clear CD-ROM DMA write bit, m5229, 0x4b, bit 7
		 */
		pci_read_config_byte(dev, 0x4b, &tmpbyte);
		save_flags(flags);
		cli();
		/*
		 * clear bit 7
		 */
		pci_write_config_byte(dev, 0x4b, tmpbyte & 0x7F);
		restore_flags(flags);

		/*
		 * check m1533, 0x5e, bit 1~4 == 1001 => & 00011110 = 00010010
		 */
		pci_read_config_byte(isa_dev, 0x5e, &tmpbyte);
		chip_is_1543c_e = ((tmpbyte & 0x1e) == 0x12) ? 1: 0;
	}

	if (m5229_revision == 0x20) {
		/*
		 * check M1533 revision (offset 0x08)
		 */
		pci_read_config_byte(isa_dev, 0x08, &tmpbyte);
		if (tmpbyte == 0x0A) {
			unsigned long flags;
			pci_read_config_byte(dev, 0x4e, &tmpbyte);
			save_flags(flags);
			cli();
			/*
			 * set bit 6
			 */
			pci_write_config_byte(dev, 0x4e, tmpbyte | 0x40);
			restore_flags(flags);

			/*
			 * this special version is similar to revision 0xC2
			 * but does not support UDMA66
			 * (cable_80_pin[0] = 0; cable_80_pin[1] = 0;)
			 */
			m5229_revision = 0xC2;
		}
	}

	return 0;
}

unsigned int __init ata66_ali15x3 (ide_hwif_t *hwif)
{
	/*
	 * FIXME !!!!
	 * {0x4a,0x01,0x01}, {0x4a,0x02,0x02}
	 */
	return 0;
}

void __init ide_init_ali15x3 (ide_hwif_t *hwif)
{
	byte ideic, inmir;
	byte irq_routing_table[] = { -1,  9, 3, 10, 4,  5, 7,  6,
				      1, 11, 0, 12, 0, 14, 0, 15 };

	hwif->irq = hwif->channel ? 15 : 14;

	if (isa_dev) {
		/*
		 * read IDE interface control
		 */
		pci_read_config_byte(isa_dev, 0x58, &ideic);

		/* bit0, bit1 */
		ideic = ideic & 0x03;

		/* get IRQ for IDE Controller */
		if ((hwif->channel && ideic == 0x03) ||
		    (!hwif->channel && !ideic)) {
			/*
			 * get SIRQ1 routing table
			 */
			pci_read_config_byte(isa_dev, 0x44, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		} else if (hwif->channel && !(ideic & 0x01)) {
			/*
			 * get SIRQ2 routing table
			 */
			pci_read_config_byte(isa_dev, 0x75, &inmir);
			inmir = inmir & 0x0f;
			hwif->irq = irq_routing_table[inmir];
		}
	}

	hwif->tuneproc = &ali15x3_tune_drive;
	if ((hwif->dma_base) && (m5229_revision >= 0x20)) {
		/*
		 * M1543C or newer for DMAing
		 */
		hwif->dmaproc = &ali15x3_dmaproc;
		hwif->autodma = 1;
	} else {
		hwif->autodma = 0;
		hwif->drives[0].autotune = 1;
		hwif->drives[1].autotune = 1;
	}

#if defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS)
	ali_proc = 1;
	bmide_dev = hwif->pci_dev;
	ali_display_info = &ali_get_info;
#endif  /* defined(DISPLAY_ALI_TIMINGS) && defined(CONFIG_PROC_FS) */

	return;
}

void ide_dmacapable_ali15x3 (ide_hwif_t *hwif, unsigned long dmabase)
{
	if ((dmabase) && (m5229_revision < 0x20)) {
		return;
	}
	ide_setup_dma(hwif, dmabase, 8);
}
