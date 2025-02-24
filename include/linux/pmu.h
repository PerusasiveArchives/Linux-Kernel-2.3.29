/*
 * Definitions for talking to the PMU.  The PMU is a microcontroller
 * which controls battery charging and system power on PowerBook 3400
 * and 2400 models as well as the RTC and various other things.
 *
 * Copyright (C) 1998 Paul Mackerras.
 */

#include <linux/config.h>
/*
 * PMU commands
 */
#define PMU_POWER_CTRL		0x11	/* control power of some devices */
#define PMU_ADB_CMD		0x20	/* send ADB packet */
#define PMU_ADB_POLL_OFF	0x21	/* disable ADB auto-poll */
#define PMU_WRITE_NVRAM		0x33	/* write non-volatile RAM */
#define PMU_READ_NVRAM		0x3b	/* read non-volatile RAM */
#define PMU_SET_RTC		0x30	/* set real-time clock */
#define PMU_READ_RTC		0x38	/* read real-time clock */
#define PMU_SET_VOLBUTTON	0x40	/* set volume up/down position */
#define PMU_BACKLIGHT_BRIGHT	0x41	/* set backlight brightness */
#define PMU_GET_VOLBUTTON	0x48	/* get volume up/down position */
#define PMU_PCEJECT		0x4c	/* eject PC-card from slot */
#define PMU_BATTERY_STATE	0x6b	/* report battery state etc. */
#define PMU_SET_INTR_MASK	0x70	/* set PMU interrupt mask */
#define PMU_INT_ACK		0x78	/* read interrupt bits */
#define PMU_SHUTDOWN		0x7e	/* turn power off */
#define PMU_SLEEP		0x7f	/* put CPU to sleep */
#define PMU_RESET		0xd0	/* reset CPU */
#define PMU_GET_BRIGHTBUTTON	0xd9	/* report brightness up/down pos */
#define PMU_GET_COVER		0xdc	/* report cover open/closed */

/* Bits to use with the PMU_POWER_CTRL command */
#define PMU_POW_ON		0x80	/* OR this to power ON the device */
#define PMU_POW_OFF		0x00	/* leave bit 7 to 0 to power it OFF */
#define PMU_POW_BACKLIGHT	0x01	/* backlight power */
#define PMU_POW_IRLED		0x04	/* IR led power (on wallstreet) */

/* Bits in PMU interrupt and interrupt mask bytes */
#define PMU_INT_ADB_AUTO	0x04	/* ADB autopoll, when PMU_INT_ADB */
#define PMU_INT_PCEJECT		0x04	/* PC-card eject buttons */
#define PMU_INT_SNDBRT		0x08	/* sound/brightness up/down buttons */
#define PMU_INT_ADB		0x10	/* ADB autopoll or reply data */
#define PMU_INT_BATTERY		0x20
#define PMU_INT_WAKEUP		0x40
#define PMU_INT_TICK		0x80	/* 1-second tick interrupt */

/* Kind of PMU (model) */
enum {
	PMU_UNKNOWN,
	PMU_OHARE_BASED,	/* 2400, 3400, 3500 (old G3 powerbook) */
	PMU_HEATHROW_BASED,	/* PowerBook G3 series */
	PMU_PADDINGTON_BASED,	/* 1999 PowerBook G3 */
};

/*
 * Ioctl commands for the /dev/pmu device
 */
#include <linux/ioctl.h>

/* no param */
#define PMU_IOC_SLEEP		_IO('B', 0)
/* out param: u32*	backlight value: 0 to 31 */
#define PMU_IOC_GET_BACKLIGHT	_IOR('B', 1, sizeof(__u32*))
/* in param: u32	backlight value: 0 to 31 */
#define PMU_IOC_SET_BACKLIGHT	_IOW('B', 2, sizeof(__u32))
/* out param: u32*	backlight value: 0 to 31 */
#define PMU_IOC_GET_MODEL	_IOR('B', 3, sizeof(__u32*))

#ifdef __KERNEL__

int find_via_pmu(void);
int via_pmu_init(void);

int pmu_request(struct adb_request *req,
		void (*done)(struct adb_request *), int nbytes, ...);
void pmu_poll(void);

void pmu_enable_backlight(int on);
void pmu_set_brightness(int level);

void pmu_enable_irled(int on);

void pmu_restart(void);
void pmu_shutdown(void);

int pmu_present(void);
int pmu_get_model(void);

#ifdef CONFIG_PMAC_PBOOK
/*
 * Stuff for putting the powerbook to sleep and waking it again.
 *
 */
#include <linux/list.h>

struct pmu_sleep_notifier
{
	int (*notifier_call)(struct pmu_sleep_notifier *self, int when);
	int priority;
	struct list_head list;
};

/* Code values for calling sleep/wakeup handlers
 *
 * Note: If a sleep request got cancelled, all drivers will get
 * the PBOOK_SLEEP_REJECT, even those who didn't get the PBOOK_SLEEP_REQUEST.
 */
#define PBOOK_SLEEP_REQUEST	1
#define PBOOK_SLEEP_NOW		2
#define PBOOK_SLEEP_REJECT	3
#define PBOOK_WAKE		4

/* Result codes returned by the notifiers */
#define PBOOK_SLEEP_OK		0
#define PBOOK_SLEEP_REFUSE	-1

/* priority levels in notifiers */
#define SLEEP_LEVEL_VIDEO	100	/* Video driver (first wake) */
#define SLEEP_LEVEL_SOUND	90	/* Sound driver */
#define SLEEP_LEVEL_MEDIABAY	80	/* Media bay driver */
#define SLEEP_LEVEL_BLOCK	70	/* IDE, SCSI */
#define SLEEP_LEVEL_NET		60	/* bmac */
#define SLEEP_LEVEL_ADB		50	/* ADB */
#define SLEEP_LEVEL_MISC	30	/* Anything */
#define SLEEP_LEVEL_LAST	0	/* Anything */

/* special register notifier functions */
int pmu_register_sleep_notifier(struct pmu_sleep_notifier* notifier);
int pmu_unregister_sleep_notifier(struct pmu_sleep_notifier* notifier);

#endif /* CONFIG_PMAC_PBOOK */


#endif	/* __KERNEL */
