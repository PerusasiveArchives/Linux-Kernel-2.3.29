/*
 * arch/arm/kernel/hw-footbridge.c
 *
 * Footbridge-dependent machine fixup
 *
 * Copyright (C) 1998, 1999 Russell King, Phil Blundell
 */
#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ptrace.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/smp.h>
#include <linux/mm.h>
#include <linux/init.h>

#include <asm/dec21285.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/leds.h>
#include <asm/system.h>

#define IRDA_IO_BASE		0x180
#define GP1_IO_BASE		0x338
#define GP2_IO_BASE		0x33a

/*
 * Netwinder stuff
 */
#ifdef CONFIG_ARCH_NETWINDER

/*
 * Winbond WB83977F accessibility stuff
 */
static inline void wb977_open(void)
{
	outb(0x87, 0x370);
	outb(0x87, 0x370);
}

static inline void wb977_close(void)
{
	outb(0xaa, 0x370);
}

static inline void wb977_wb(int reg, int val)
{
	outb(reg, 0x370);
	outb(val, 0x371);
}

static inline void wb977_ww(int reg, int val)
{
	outb(reg, 0x370);
	outb(val >> 8, 0x371);
	outb(reg + 1, 0x370);
	outb(val, 0x371);
}

#define wb977_device_select(dev)	wb977_wb(0x07, dev)
#define wb977_device_disable()		wb977_wb(0x30, 0x00)
#define wb977_device_enable()		wb977_wb(0x30, 0x01)

/*
 * This is a lock for accessing ports GP1_IO_BASE and GP2_IO_BASE
 */
spinlock_t __netwinder_data gpio_lock = SPIN_LOCK_UNLOCKED;

static unsigned int __netwinder_data current_gpio_op = 0;
static unsigned int __netwinder_data current_gpio_io = 0;
static unsigned int __netwinder_data current_cpld = 0;

void __netwinder_text gpio_modify_op(int mask, int set)
{
	unsigned int new_gpio, changed;

	new_gpio = (current_gpio_op & ~mask) | set;
	changed = new_gpio ^ current_gpio_op;
	current_gpio_op = new_gpio;

	if (changed & 0xff)
		outb(new_gpio, GP1_IO_BASE);
	if (changed & 0xff00)
		outb(new_gpio >> 8, GP2_IO_BASE);
}

static inline void __gpio_modify_io(int mask, int in)
{
	unsigned int new_gpio, changed;
	int port;

	new_gpio = (current_gpio_io & ~mask) | in;
	changed = new_gpio ^ current_gpio_io;
	current_gpio_io = new_gpio;

	changed >>= 1;
	new_gpio >>= 1;

	wb977_device_select(7);

	for (port = 0xe1; changed && port < 0xe8; changed >>= 1) {
		wb977_wb(port, new_gpio & 1);

		port += 1;
		new_gpio >>= 1;
	}

	wb977_device_select(8);

	for (port = 0xe8; changed && port < 0xec; changed >>= 1) {
		wb977_wb(port, new_gpio & 1);

		port += 1;
		new_gpio >>= 1;
	}
}

void __netwinder_text gpio_modify_io(int mask, int in)
{
	/* Open up the SuperIO chip */
	wb977_open();

	__gpio_modify_io(mask, in);

	/* Close up the EFER gate */
	wb977_close();
}

int __netwinder_text gpio_read(void)
{
	return inb(GP1_IO_BASE) | inb(GP2_IO_BASE) << 8;
}

/*
 * Initialise the Winbond W83977F global registers
 */
static inline void wb977_init_global(void)
{
	/*
	 * Enable R/W config registers
	 */
	wb977_wb(0x26, 0x40);

	/*
	 * Power down FDC (not used)
	 */
	wb977_wb(0x22, 0xfe);

	/*
	 * GP12, GP11, CIRRX, IRRXH, GP10
	 */
	wb977_wb(0x2a, 0xc1);

	/*
	 * GP23, GP22, GP21, GP20, GP13
	 */
	wb977_wb(0x2b, 0x6b);

	/*
	 * GP17, GP16, GP15, GP14
	 */
	wb977_wb(0x2c, 0x55);
}

/*
 * Initialise the Winbond W83977F printer port
 */
static inline void wb977_init_printer(void)
{
	wb977_device_select(1);

	/*
	 * mode 1 == EPP
	 */
	wb977_wb(0xf0, 0x01);
}

/*
 * Initialise the Winbond W83977F keyboard controller
 */
static inline void wb977_init_keyboard(void)
{
	wb977_device_select(5);

	/*
	 * Keyboard controller address
	 */
	wb977_ww(0x60, 0x0060);
	wb977_ww(0x62, 0x0064);

	/*
	 * Keyboard IRQ 1, active high, edge trigger
	 */
	wb977_wb(0x70, 1);
	wb977_wb(0x71, 0x02);

	/*
	 * Mouse IRQ 5, active high, edge trigger
	 */
	wb977_wb(0x72, 5);
	wb977_wb(0x73, 0x02);

	/*
	 * KBC 8MHz
	 */
	wb977_wb(0xf0, 0x40);

	/*
	 * Enable device
	 */
	wb977_device_enable();
}

/*
 * Initialise the Winbond W83977F Infra-Red device
 */
static inline void wb977_init_irda(void)
{
	wb977_device_select(6);

	/*
	 * IR base address
	 */
	wb977_ww(0x60, IRDA_IO_BASE);

	/*
	 * IRDA IRQ 6, active high, edge trigger
	 */
	wb977_wb(0x70, 6);
	wb977_wb(0x71, 0x02);

	/*
	 * RX DMA - ISA DMA 0
	 */
	wb977_wb(0x74, 0x00);

	/*
	 * TX DMA - Disable Tx DMA
	 */
	wb977_wb(0x75, 0x04);

	/*
	 * Append CRC, Enable bank selection
	 */
	wb977_wb(0xf0, 0x03);

	/*
	 * Enable device
	 */
	wb977_device_enable();
}

/*
 * Initialise Winbond W83977F general purpose IO
 */
static inline void wb977_init_gpio(void)
{
	unsigned long flags;

	/*
	 * Set up initial I/O definitions
	 */
	current_gpio_io = -1;
	__gpio_modify_io(-1, GPIO_DONE | GPIO_WDTIMER);

	wb977_device_select(7);

	/*
	 * Group1 base address
	 */
	wb977_ww(0x60, GP1_IO_BASE);
	wb977_ww(0x62, 0);
	wb977_ww(0x64, 0);

	/*
	 * GP10 (Orage button) IRQ 10, active high, edge trigger
	 */
	wb977_wb(0x70, 10);
	wb977_wb(0x71, 0x02);

	/*
	 * GP10: Debounce filter enabled, IRQ, input
	 */
	wb977_wb(0xe0, 0x19);

	/*
	 * Enable Group1
	 */
	wb977_device_enable();

	wb977_device_select(8);

	/*
	 * Group2 base address
	 */
	wb977_ww(0x60, GP2_IO_BASE);

	/*
	 * Clear watchdog timer regs
	 *  - timer disable
	 */
	wb977_wb(0xf2, 0x00);

	/*
	 *  - disable LED, no mouse nor keyboard IRQ
	 */
	wb977_wb(0xf3, 0x00);

	/*
	 *  - timer counting, disable power LED, disable timeouot
	 */
	wb977_wb(0xf4, 0x00);

	/*
	 * Enable group2
	 */
	wb977_device_enable();

	/*
	 * Set Group1/Group2 outputs
	 */
	spin_lock_irqsave(&gpio_lock, flags);
	gpio_modify_op(-1, GPIO_RED_LED | GPIO_FAN);
	spin_unlock_irqrestore(&gpio_loc, flags);
}

/*
 * Initialise the Winbond W83977F chip.
 */
static void __init wb977_init(void)
{
	request_region(0x370, 2, "W83977AF configuration");

	/*
	 * Open up the SuperIO chip
	 */
	wb977_open();

	/*
	 * Initialise the global registers
	 */
	wb977_init_global();

	/*
	 * Initialise the various devices in
	 * the multi-IO chip.
	 */
	wb977_init_printer();
	wb977_init_keyboard();
	wb977_init_irda();
	wb977_init_gpio();

	/*
	 * Close up the EFER gate
	 */
	wb977_close();
}

void __netwinder_text cpld_modify(int mask, int set)
{
	int msk;

	current_cpld = (current_cpld & ~mask) | set;

	gpio_modify_io(GPIO_DATA | GPIO_IOCLK | GPIO_IOLOAD, 0);
	gpio_modify_op(GPIO_IOLOAD, 0);

	for (msk = 8; msk; msk >>= 1) {
		int bit = current_cpld & msk;

		gpio_modify_op(GPIO_DATA | GPIO_IOCLK, bit ? GPIO_DATA : 0);
		gpio_modify_op(GPIO_IOCLK, GPIO_IOCLK);
	}

	gpio_modify_op(GPIO_IOCLK|GPIO_DATA, 0);
	gpio_modify_op(GPIO_IOLOAD|GPIO_DSCLK, GPIO_IOLOAD|GPIO_DSCLK);
	gpio_modify_op(GPIO_IOLOAD, 0);
}

static void __init cpld_init(void)
{
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	cpld_modify(-1, CPLD_UNMUTE | CPLD_7111_DISABLE);
	spin_unlock_irqrestore(&gpio_lock, flags);
}

static unsigned char rwa_unlock[] __initdata =
{ 0x00, 0x00, 0x6a, 0xb5, 0xda, 0xed, 0xf6, 0xfb, 0x7d, 0xbe, 0xdf, 0x6f, 0x37, 0x1b,
  0x0d, 0x86, 0xc3, 0x61, 0xb0, 0x58, 0x2c, 0x16, 0x8b, 0x45, 0xa2, 0xd1, 0xe8, 0x74,
  0x3a, 0x9d, 0xce, 0xe7, 0x73, 0x39 };

#ifndef DEBUG
#define dprintk if (0) printk
#else
#define dprintk printk
#endif

#define WRITE_RWA(r,v) do { outb((r), 0x279); udelay(10); outb((v), 0xa79); } while (0)

static inline void rwa010_unlock(void)
{
	int i;

	WRITE_RWA(2, 2);
	mdelay(10);

	for (i = 0; i < sizeof(rwa_unlock); i++) {
		outb(rwa_unlock[i], 0x279);
		udelay(10);
	}
}

static inline void rwa010_read_ident(void)
{
	unsigned char si[9];
	int i, j;

	WRITE_RWA(3, 0);
	WRITE_RWA(0, 128);

	outb(1, 0x279);

	mdelay(1);

	dprintk("Identifier: ");
	for (i = 0; i < 9; i++) {
		si[i] = 0;
		for (j = 0; j < 8; j++) {
			int bit;
			udelay(250);
			inb(0x203);
			udelay(250);
			bit = inb(0x203);
			dprintk("%02X ", bit);
			bit = (bit == 0xaa) ? 1 : 0;
			si[i] |= bit << j;
		}
		dprintk("(%02X) ", si[i]);
	}
	dprintk("\n");
}

static inline void rwa010_global_init(void)
{
	WRITE_RWA(6, 2);	// Assign a card no = 2

	dprintk("Card no = %d\n", inb(0x203));

	/* disable the modem section of the chip */
	WRITE_RWA(7, 3);
	WRITE_RWA(0x30, 0);

	/* disable the cdrom section of the chip */
	WRITE_RWA(7, 4);
	WRITE_RWA(0x30, 0);

	/* disable the MPU-401 section of the chip */
	WRITE_RWA(7, 2);
	WRITE_RWA(0x30, 0);
}

static inline void rwa010_game_port_init(void)
{
	int i;

	WRITE_RWA(7, 5);

	dprintk("Slider base: ");
	WRITE_RWA(0x61, 1);
	i = inb(0x203);

	WRITE_RWA(0x60, 2);
	dprintk("%02X%02X (201)\n", inb(0x203), i);

	WRITE_RWA(0x30, 1);
}

static inline void rwa010_waveartist_init(int base, int irq, int dma)
{
	int i;

	WRITE_RWA(7, 0);

	dprintk("WaveArtist base: ");
	WRITE_RWA(0x61, base);
	i = inb(0x203);

	WRITE_RWA(0x60, base >> 8);
	dprintk("%02X%02X (%X),", inb(0x203), i, base);

	WRITE_RWA(0x70, irq);
	dprintk(" irq: %d (%d),", inb(0x203), irq);

	WRITE_RWA(0x74, dma);
	dprintk(" dma: %d (%d)\n", inb(0x203), dma);

	WRITE_RWA(0x30, 1);
}

static inline void rwa010_soundblaster_init(int sb_base, int al_base, int irq, int dma)
{
	int i;

	WRITE_RWA(7, 1);

	dprintk("SoundBlaster base: ");
	WRITE_RWA(0x61, sb_base);
	i = inb(0x203);

	WRITE_RWA(0x60, sb_base >> 8);
	dprintk("%02X%02X (%X),", inb(0x203), i, sb_base);

	dprintk(" irq: ");
	WRITE_RWA(0x70, irq);
	dprintk("%d (%d),", inb(0x203), irq);

	dprintk(" 8-bit DMA: ");
	WRITE_RWA(0x74, dma);
	dprintk("%d (%d)\n", inb(0x203), dma);

	dprintk("AdLib base: ");
	WRITE_RWA(0x63, al_base);
	i = inb(0x203);

	WRITE_RWA(0x62, al_base >> 8);
	dprintk("%02X%02X (%X)\n", inb(0x203), i, al_base);

	WRITE_RWA(0x30, 1);
}

static void rwa010_soundblaster_reset(void)
{
	int i;

	outb(1, 0x226);
	udelay(3);
	outb(0, 0x226);

	for (i = 0; i < 5; i++) {
		if (inb(0x22e) & 0x80)
			break;
		mdelay(1);
	}
	if (i == 5)
		printk("SoundBlaster: DSP reset failed\n");

	dprintk("SoundBlaster DSP reset: %02X (AA)\n", inb(0x22a));

	for (i = 0; i < 5; i++) {
		if ((inb(0x22c) & 0x80) == 0)
			break;
		mdelay(1);
	}

	if (i == 5)
		printk("SoundBlaster: DSP not ready\n");
	else {
		outb(0xe1, 0x22c);

		dprintk("SoundBlaster DSP id: ");
		i = inb(0x22a);
		udelay(1);
		i |= inb(0x22a) << 8;
		dprintk("%04X\n", i);

		for (i = 0; i < 5; i++) {
			if ((inb(0x22c) & 0x80) == 0)
				break;
			mdelay(1);
		}

		if (i == 5)
			printk("SoundBlaster: could not turn speaker off\n");

		outb(0xd3, 0x22c);
	}

	/* turn on OPL3 */
	outb(5, 0x38a);
	outb(1, 0x38b);
}

static void __init rwa010_init(void)
{
	rwa010_unlock();
	rwa010_read_ident();
	rwa010_global_init();
	rwa010_game_port_init();
	rwa010_waveartist_init(0x250, 3, 7);
	rwa010_soundblaster_init(0x220, 0x388, 3, 1);
	rwa010_soundblaster_reset();
}

EXPORT_SYMBOL(gpio_lock);
EXPORT_SYMBOL(gpio_modify_op);
EXPORT_SYMBOL(gpio_modify_io);
EXPORT_SYMBOL(cpld_modify);

#endif

#ifdef CONFIG_LEDS
#define DEFAULT_LEDS	0
#else
#define DEFAULT_LEDS	GPIO_GREEN_LED
#endif

/*
 * CATS stuff
 */
#ifdef CONFIG_CATS

#define CONFIG_PORT	0x370
#define INDEX_PORT	(CONFIG_PORT)
#define DATA_PORT	(CONFIG_PORT + 1)

static void __init cats_hw_init(void)
{
	/* Set Aladdin to CONFIGURE mode */
	outb(0x51, CONFIG_PORT);
	outb(0x23, CONFIG_PORT);

	/* Select logical device 3 */
	outb(0x07, INDEX_PORT);
	outb(0x03, DATA_PORT);

	/* Set parallel port to DMA channel 3, ECP+EPP1.9, 
	   enable EPP timeout */
	outb(0x74, INDEX_PORT);
	outb(0x03, DATA_PORT);
	
	outb(0xf0, INDEX_PORT);
	outb(0x0f, DATA_PORT);

	outb(0xf1, INDEX_PORT);
	outb(0x07, DATA_PORT);

	/* Select logical device 4 */
	outb(0x07, INDEX_PORT);
	outb(0x04, DATA_PORT);

	/* UART1 high speed mode */
	outb(0xf0, INDEX_PORT);
	outb(0x02, DATA_PORT);

	/* Select logical device 5 */
	outb(0x07, INDEX_PORT);
	outb(0x05, DATA_PORT);

	/* UART2 high speed mode */
	outb(0xf0, INDEX_PORT);
	outb(0x02, DATA_PORT);

	/* Set Aladdin to RUN mode */
	outb(0xbb, CONFIG_PORT);
}

#endif

void __init hw_init(void)
{
	extern void register_isa_ports(unsigned int, unsigned int, 
				       unsigned int);
	register_isa_ports(DC21285_PCI_MEM, DC21285_PCI_IO, 0);

#ifdef CONFIG_ARCH_NETWINDER
	/*
	 * this ought to have a better home...
	 * Since this calls the above routines, which are
	 * compiled only if CONFIG_ARCH_NETWINDER is set,
	 * these should only be parsed by the compiler
	 * in the same circumstance.
	 */
	if (machine_is_netwinder()) {
		unsigned long flags;

		wb977_init();
		cpld_init();
		rwa010_init();

		spin_lock_irqsave(&gpio_lock, flags);
		gpio_modify_op(GPIO_RED_LED|GPIO_GREEN_LED, DEFAULT_LEDS);
		spin_unlock_irqrestore(&gpio_lock, flags);
	}
#endif
#ifdef CONFIG_CATS
	if (machine_is_cats())
		cats_hw_init();
#endif

	leds_event(led_start);
}
