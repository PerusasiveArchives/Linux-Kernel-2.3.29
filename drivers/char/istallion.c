/*****************************************************************************/

/*
 *	istallion.c  -- stallion intelligent multiport serial driver.
 *
 *	Copyright (C) 1996-1999  Stallion Technologies (support@stallion.oz.au).
 *	Copyright (C) 1994-1996  Greg Ungerer.
 *
 *	This code is loosely based on the Linux serial driver, written by
 *	Linus Torvalds, Theodore T'so and others.
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*****************************************************************************/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/tty_flip.h>
#include <linux/serial.h>
#include <linux/cdk.h>
#include <linux/comstats.h>
#include <linux/version.h>
#include <linux/istallion.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/init.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

/*****************************************************************************/

/*
 *	Define different board types. Not all of the following board types
 *	are supported by this driver. But I will use the standard "assigned"
 *	board numbers. Currently supported boards are abbreviated as:
 *	ECP = EasyConnection 8/64, ONB = ONboard, BBY = Brumby and
 *	STAL = Stallion.
 */
#define	BRD_UNKNOWN	0
#define	BRD_STALLION	1
#define	BRD_BRUMBY4	2
#define	BRD_ONBOARD2	3
#define	BRD_ONBOARD	4
#define	BRD_BRUMBY8	5
#define	BRD_BRUMBY16	6
#define	BRD_ONBOARDE	7
#define	BRD_ONBOARD32	9
#define	BRD_ONBOARD2_32	10
#define	BRD_ONBOARDRS	11
#define	BRD_EASYIO	20
#define	BRD_ECH		21
#define	BRD_ECHMC	22
#define	BRD_ECP		23
#define BRD_ECPE	24
#define	BRD_ECPMC	25
#define	BRD_ECHPCI	26
#define	BRD_ECH64PCI	27
#define	BRD_EASYIOPCI	28
#define	BRD_ECPPCI	29

#define	BRD_BRUMBY	BRD_BRUMBY4

/*
 *	Define a configuration structure to hold the board configuration.
 *	Need to set this up in the code (for now) with the boards that are
 *	to be configured into the system. This is what needs to be modified
 *	when adding/removing/modifying boards. Each line entry in the
 *	stli_brdconf[] array is a board. Each line contains io/irq/memory
 *	ranges for that board (as well as what type of board it is).
 *	Some examples:
 *		{ BRD_ECP, 0x2a0, 0, 0xcc000, 0, 0 },
 *	This line will configure an EasyConnection 8/64 at io address 2a0,
 *	and shared memory address of cc000. Multiple EasyConnection 8/64
 *	boards can share the same shared memory address space. No interrupt
 *	is required for this board type.
 *	Another example:
 *		{ BRD_ECPE, 0x5000, 0, 0x80000000, 0, 0 },
 *	This line will configure an EasyConnection 8/64 EISA in slot 5 and
 *	shared memory address of 0x80000000 (2 GByte). Multiple
 *	EasyConnection 8/64 EISA boards can share the same shared memory
 *	address space. No interrupt is required for this board type.
 *	Another example:
 *		{ BRD_ONBOARD, 0x240, 0, 0xd0000, 0, 0 },
 *	This line will configure an ONboard (ISA type) at io address 240,
 *	and shared memory address of d0000. Multiple ONboards can share
 *	the same shared memory address space. No interrupt required.
 *	Another example:
 *		{ BRD_BRUMBY4, 0x360, 0, 0xc8000, 0, 0 },
 *	This line will configure a Brumby board (any number of ports!) at
 *	io address 360 and shared memory address of c8000. All Brumby boards
 *	configured into a system must have their own separate io and memory
 *	addresses. No interrupt is required.
 *	Another example:
 *		{ BRD_STALLION, 0x330, 0, 0xd0000, 0, 0 },
 *	This line will configure an original Stallion board at io address 330
 *	and shared memory address d0000 (this would only be valid for a "V4.0"
 *	or Rev.O Stallion board). All Stallion boards configured into the
 *	system must have their own separate io and memory addresses. No
 *	interrupt is required.
 */

typedef struct {
	int		brdtype;
	int		ioaddr1;
	int		ioaddr2;
	unsigned long	memaddr;
	int		irq;
	int		irqtype;
} stlconf_t;

static stlconf_t	stli_brdconf[] = {
	/*{ BRD_ECP, 0x2a0, 0, 0xcc000, 0, 0 },*/
};

static int	stli_nrbrds = sizeof(stli_brdconf) / sizeof(stlconf_t);

/*
 *	There is some experimental EISA board detection code in this driver.
 *	By default it is disabled, but for those that want to try it out,
 *	then set the define below to be 1.
 */
#define	STLI_EISAPROBE	0

/*****************************************************************************/

/*
 *	Define some important driver characteristics. Device major numbers
 *	allocated as per Linux Device Registry.
 */
#ifndef	STL_SIOMEMMAJOR
#define	STL_SIOMEMMAJOR		28
#endif
#ifndef	STL_SERIALMAJOR
#define	STL_SERIALMAJOR		24
#endif
#ifndef	STL_CALLOUTMAJOR
#define	STL_CALLOUTMAJOR	25
#endif

#define	STL_DRVTYPSERIAL	1
#define	STL_DRVTYPCALLOUT	2

/*****************************************************************************/

/*
 *	Define our local driver identity first. Set up stuff to deal with
 *	all the local structures required by a serial tty driver.
 */
static char	*stli_drvtitle = "Stallion Intelligent Multiport Serial Driver";
static char	*stli_drvname = "istallion";
static char	*stli_drvversion = "5.6.0";
static char	*stli_serialname = "ttyE";
static char	*stli_calloutname = "cue";

static struct tty_driver	stli_serial;
static struct tty_driver	stli_callout;
static struct tty_struct	*stli_ttys[STL_MAXDEVS];
static struct termios		*stli_termios[STL_MAXDEVS];
static struct termios		*stli_termioslocked[STL_MAXDEVS];
static int			stli_refcount;

/*
 *	We will need to allocate a temporary write buffer for chars that
 *	come direct from user space. The problem is that a copy from user
 *	space might cause a page fault (typically on a system that is
 *	swapping!). All ports will share one buffer - since if the system
 *	is already swapping a shared buffer won't make things any worse.
 */
static char			*stli_tmpwritebuf = (char *) NULL;
static DECLARE_MUTEX(stli_tmpwritesem);

#define	STLI_TXBUFSIZE		4096

/*
 *	Use a fast local buffer for cooked characters. Typically a whole
 *	bunch of cooked characters come in for a port, 1 at a time. So we
 *	save those up into a local buffer, then write out the whole lot
 *	with a large memcpy. Just use 1 buffer for all ports, since its
 *	use it is only need for short periods of time by each port.
 */
static char			*stli_txcookbuf = (char *) NULL;
static int			stli_txcooksize = 0;
static int			stli_txcookrealsize = 0;
static struct tty_struct	*stli_txcooktty = (struct tty_struct *) NULL;

/*
 *	Define a local default termios struct. All ports will be created
 *	with this termios initially. Basically all it defines is a raw port
 *	at 9600 baud, 8 data bits, no parity, 1 stop bit.
 */
static struct termios		stli_deftermios = {
	0,
	0,
	(B9600 | CS8 | CREAD | HUPCL | CLOCAL),
	0,
	0,
	INIT_C_CC
};

/*
 *	Define global stats structures. Not used often, and can be
 *	re-used for each stats call.
 */
static comstats_t	stli_comstats;
static combrd_t		stli_brdstats;
static asystats_t	stli_cdkstats;
static stlibrd_t	stli_dummybrd;
static stliport_t	stli_dummyport;

/*****************************************************************************/

static stlibrd_t	*stli_brds[STL_MAXBRDS];

static int		stli_shared = 0;

/*
 *	Per board state flags. Used with the state field of the board struct.
 *	Not really much here... All we need to do is keep track of whether
 *	the board has been detected, and whether it is actually running a slave
 *	or not.
 */
#define	BST_FOUND	0x1
#define	BST_STARTED	0x2

/*
 *	Define the set of port state flags. These are marked for internal
 *	state purposes only, usually to do with the state of communications
 *	with the slave. Most of them need to be updated atomically, so always
 *	use the bit setting operations (unless protected by cli/sti).
 */
#define	ST_INITIALIZING	1
#define	ST_OPENING	2
#define	ST_CLOSING	3
#define	ST_CMDING	4
#define	ST_TXBUSY	5
#define	ST_RXING	6
#define	ST_DOFLUSHRX	7
#define	ST_DOFLUSHTX	8
#define	ST_DOSIGS	9
#define	ST_RXSTOP	10
#define	ST_GETSIGS	11

/*
 *	Define an array of board names as printable strings. Handy for
 *	referencing boards when printing trace and stuff.
 */
static char	*stli_brdnames[] = {
	"Unknown",
	"Stallion",
	"Brumby",
	"ONboard-MC",
	"ONboard",
	"Brumby",
	"Brumby",
	"ONboard-EI",
	(char *) NULL,
	"ONboard",
	"ONboard-MC",
	"ONboard-MC",
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	(char *) NULL,
	"EasyIO",
	"EC8/32-AT",
	"EC8/32-MC",
	"EC8/64-AT",
	"EC8/64-EI",
	"EC8/64-MC",
	"EC8/32-PCI",
	"EC8/64-PCI",
	"EasyIO-PCI",
	"EC/RA-PCI",
};

/*****************************************************************************/

#ifdef MODULE
/*
 *	Define some string labels for arguments passed from the module
 *	load line. These allow for easy board definitions, and easy
 *	modification of the io, memory and irq resoucres.
 */

static char	*board0[8];
static char	*board1[8];
static char	*board2[8];
static char	*board3[8];

static char	**stli_brdsp[] = {
	(char **) &board0,
	(char **) &board1,
	(char **) &board2,
	(char **) &board3
};

/*
 *	Define a set of common board names, and types. This is used to
 *	parse any module arguments.
 */

typedef struct stlibrdtype {
	char	*name;
	int	type;
} stlibrdtype_t;

static stlibrdtype_t	stli_brdstr[] = {
	{ "stallion", BRD_STALLION },
	{ "1", BRD_STALLION },
	{ "brumby", BRD_BRUMBY },
	{ "brumby4", BRD_BRUMBY },
	{ "brumby/4", BRD_BRUMBY },
	{ "brumby-4", BRD_BRUMBY },
	{ "brumby8", BRD_BRUMBY },
	{ "brumby/8", BRD_BRUMBY },
	{ "brumby-8", BRD_BRUMBY },
	{ "brumby16", BRD_BRUMBY },
	{ "brumby/16", BRD_BRUMBY },
	{ "brumby-16", BRD_BRUMBY },
	{ "2", BRD_BRUMBY },
	{ "onboard2", BRD_ONBOARD2 },
	{ "onboard-2", BRD_ONBOARD2 },
	{ "onboard/2", BRD_ONBOARD2 },
	{ "onboard-mc", BRD_ONBOARD2 },
	{ "onboard/mc", BRD_ONBOARD2 },
	{ "onboard-mca", BRD_ONBOARD2 },
	{ "onboard/mca", BRD_ONBOARD2 },
	{ "3", BRD_ONBOARD2 },
	{ "onboard", BRD_ONBOARD },
	{ "onboardat", BRD_ONBOARD },
	{ "4", BRD_ONBOARD },
	{ "onboarde", BRD_ONBOARDE },
	{ "onboard-e", BRD_ONBOARDE },
	{ "onboard/e", BRD_ONBOARDE },
	{ "onboard-ei", BRD_ONBOARDE },
	{ "onboard/ei", BRD_ONBOARDE },
	{ "7", BRD_ONBOARDE },
	{ "ecp", BRD_ECP },
	{ "ecpat", BRD_ECP },
	{ "ec8/64", BRD_ECP },
	{ "ec8/64-at", BRD_ECP },
	{ "ec8/64-isa", BRD_ECP },
	{ "23", BRD_ECP },
	{ "ecpe", BRD_ECPE },
	{ "ecpei", BRD_ECPE },
	{ "ec8/64-e", BRD_ECPE },
	{ "ec8/64-ei", BRD_ECPE },
	{ "24", BRD_ECPE },
	{ "ecpmc", BRD_ECPMC },
	{ "ec8/64-mc", BRD_ECPMC },
	{ "ec8/64-mca", BRD_ECPMC },
	{ "25", BRD_ECPMC },
	{ "ecppci", BRD_ECPPCI },
	{ "ec/ra", BRD_ECPPCI },
	{ "ec/ra-pc", BRD_ECPPCI },
	{ "ec/ra-pci", BRD_ECPPCI },
	{ "29", BRD_ECPPCI },
};

/*
 *	Define the module agruments.
 */
MODULE_AUTHOR("Greg Ungerer");
MODULE_DESCRIPTION("Stallion Intelligent Multiport Serial Driver");

MODULE_PARM(board0, "1-3s");
MODULE_PARM_DESC(board0, "Board 0 config -> name[,ioaddr[,memaddr]");
MODULE_PARM(board1, "1-3s");
MODULE_PARM_DESC(board1, "Board 1 config -> name[,ioaddr[,memaddr]");
MODULE_PARM(board2, "1-3s");
MODULE_PARM_DESC(board2, "Board 2 config -> name[,ioaddr[,memaddr]");
MODULE_PARM(board3, "1-3s");
MODULE_PARM_DESC(board3, "Board 3 config -> name[,ioaddr[,memaddr]");

#endif

/*
 *	Set up a default memory address table for EISA board probing.
 *	The default addresses are all bellow 1Mbyte, which has to be the
 *	case anyway. They should be safe, since we only read values from
 *	them, and interrupts are disabled while we do it. If the higher
 *	memory support is compiled in then we also try probing around
 *	the 1Gb, 2Gb and 3Gb areas as well...
 */
static unsigned long	stli_eisamemprobeaddrs[] = {
	0xc0000,    0xd0000,    0xe0000,    0xf0000,
	0x80000000, 0x80010000, 0x80020000, 0x80030000,
	0x40000000, 0x40010000, 0x40020000, 0x40030000,
	0xc0000000, 0xc0010000, 0xc0020000, 0xc0030000,
	0xff000000, 0xff010000, 0xff020000, 0xff030000,
};

static int	stli_eisamempsize = sizeof(stli_eisamemprobeaddrs) / sizeof(unsigned long);
int		stli_eisaprobe = STLI_EISAPROBE;

/*
 *	Define the Stallion PCI vendor and device IDs.
 */
#ifdef CONFIG_PCI
#ifndef	PCI_VENDOR_ID_STALLION
#define	PCI_VENDOR_ID_STALLION		0x124d
#endif
#ifndef PCI_DEVICE_ID_ECRA
#define	PCI_DEVICE_ID_ECRA		0x0004
#endif
#endif

/*****************************************************************************/

/*
 *	Hardware configuration info for ECP boards. These defines apply
 *	to the directly accessible io ports of the ECP. There is a set of
 *	defines for each ECP board type, ISA, EISA, MCA and PCI.
 */
#define	ECP_IOSIZE	4

#define	ECP_MEMSIZE	(128 * 1024)
#define	ECP_PCIMEMSIZE	(256 * 1024)

#define	ECP_ATPAGESIZE	(4 * 1024)
#define	ECP_MCPAGESIZE	(4 * 1024)
#define	ECP_EIPAGESIZE	(64 * 1024)
#define	ECP_PCIPAGESIZE	(64 * 1024)

#define	STL_EISAID	0x8c4e

/*
 *	Important defines for the ISA class of ECP board.
 */
#define	ECP_ATIREG	0
#define	ECP_ATCONFR	1
#define	ECP_ATMEMAR	2
#define	ECP_ATMEMPR	3
#define	ECP_ATSTOP	0x1
#define	ECP_ATINTENAB	0x10
#define	ECP_ATENABLE	0x20
#define	ECP_ATDISABLE	0x00
#define	ECP_ATADDRMASK	0x3f000
#define	ECP_ATADDRSHFT	12

/*
 *	Important defines for the EISA class of ECP board.
 */
#define	ECP_EIIREG	0
#define	ECP_EIMEMARL	1
#define	ECP_EICONFR	2
#define	ECP_EIMEMARH	3
#define	ECP_EIENABLE	0x1
#define	ECP_EIDISABLE	0x0
#define	ECP_EISTOP	0x4
#define	ECP_EIEDGE	0x00
#define	ECP_EILEVEL	0x80
#define	ECP_EIADDRMASKL	0x00ff0000
#define	ECP_EIADDRSHFTL	16
#define	ECP_EIADDRMASKH	0xff000000
#define	ECP_EIADDRSHFTH	24
#define	ECP_EIBRDENAB	0xc84

#define	ECP_EISAID	0x4

/*
 *	Important defines for the Micro-channel class of ECP board.
 *	(It has a lot in common with the ISA boards.)
 */
#define	ECP_MCIREG	0
#define	ECP_MCCONFR	1
#define	ECP_MCSTOP	0x20
#define	ECP_MCENABLE	0x80
#define	ECP_MCDISABLE	0x00

/*
 *	Important defines for the PCI class of ECP board.
 *	(It has a lot in common with the other ECP boards.)
 */
#define	ECP_PCIIREG	0
#define	ECP_PCICONFR	1
#define	ECP_PCISTOP	0x01

/*
 *	Hardware configuration info for ONboard and Brumby boards. These
 *	defines apply to the directly accessible io ports of these boards.
 */
#define	ONB_IOSIZE	16
#define	ONB_MEMSIZE	(64 * 1024)
#define	ONB_ATPAGESIZE	(64 * 1024)
#define	ONB_MCPAGESIZE	(64 * 1024)
#define	ONB_EIMEMSIZE	(128 * 1024)
#define	ONB_EIPAGESIZE	(64 * 1024)

/*
 *	Important defines for the ISA class of ONboard board.
 */
#define	ONB_ATIREG	0
#define	ONB_ATMEMAR	1
#define	ONB_ATCONFR	2
#define	ONB_ATSTOP	0x4
#define	ONB_ATENABLE	0x01
#define	ONB_ATDISABLE	0x00
#define	ONB_ATADDRMASK	0xff0000
#define	ONB_ATADDRSHFT	16

#define	ONB_MEMENABLO	0
#define	ONB_MEMENABHI	0x02

/*
 *	Important defines for the EISA class of ONboard board.
 */
#define	ONB_EIIREG	0
#define	ONB_EIMEMARL	1
#define	ONB_EICONFR	2
#define	ONB_EIMEMARH	3
#define	ONB_EIENABLE	0x1
#define	ONB_EIDISABLE	0x0
#define	ONB_EISTOP	0x4
#define	ONB_EIEDGE	0x00
#define	ONB_EILEVEL	0x80
#define	ONB_EIADDRMASKL	0x00ff0000
#define	ONB_EIADDRSHFTL	16
#define	ONB_EIADDRMASKH	0xff000000
#define	ONB_EIADDRSHFTH	24
#define	ONB_EIBRDENAB	0xc84

#define	ONB_EISAID	0x1

/*
 *	Important defines for the Brumby boards. They are pretty simple,
 *	there is not much that is programmably configurable.
 */
#define	BBY_IOSIZE	16
#define	BBY_MEMSIZE	(64 * 1024)
#define	BBY_PAGESIZE	(16 * 1024)

#define	BBY_ATIREG	0
#define	BBY_ATCONFR	1
#define	BBY_ATSTOP	0x4

/*
 *	Important defines for the Stallion boards. They are pretty simple,
 *	there is not much that is programmably configurable.
 */
#define	STAL_IOSIZE	16
#define	STAL_MEMSIZE	(64 * 1024)
#define	STAL_PAGESIZE	(64 * 1024)

/*
 *	Define the set of status register values for EasyConnection panels.
 *	The signature will return with the status value for each panel. From
 *	this we can determine what is attached to the board - before we have
 *	actually down loaded any code to it.
 */
#define	ECH_PNLSTATUS	2
#define	ECH_PNL16PORT	0x20
#define	ECH_PNLIDMASK	0x07
#define	ECH_PNLXPID	0x40
#define	ECH_PNLINTRPEND	0x80

/*
 *	Define some macros to do things to the board. Even those these boards
 *	are somewhat related there is often significantly different ways of
 *	doing some operation on it (like enable, paging, reset, etc). So each
 *	board class has a set of functions which do the commonly required
 *	operations. The macros below basically just call these functions,
 *	generally checking for a NULL function - which means that the board
 *	needs nothing done to it to achieve this operation!
 */
#define	EBRDINIT(brdp)						\
	if (brdp->init != NULL)					\
		(* brdp->init)(brdp)

#define	EBRDENABLE(brdp)					\
	if (brdp->enable != NULL)				\
		(* brdp->enable)(brdp);

#define	EBRDDISABLE(brdp)					\
	if (brdp->disable != NULL)				\
		(* brdp->disable)(brdp);

#define	EBRDINTR(brdp)						\
	if (brdp->intr != NULL)					\
		(* brdp->intr)(brdp);

#define	EBRDRESET(brdp)						\
	if (brdp->reset != NULL)				\
		(* brdp->reset)(brdp);

#define	EBRDGETMEMPTR(brdp,offset)				\
	(* brdp->getmemptr)(brdp, offset, __LINE__)

/*
 *	Define the maximal baud rate, and the default baud base for ports.
 */
#define	STL_MAXBAUD	460800
#define	STL_BAUDBASE	115200
#define	STL_CLOSEDELAY	(5 * HZ / 10)

/*****************************************************************************/

/*
 *	Define macros to extract a brd or port number from a minor number.
 */
#define	MINOR2BRD(min)		(((min) & 0xc0) >> 6)
#define	MINOR2PORT(min)		((min) & 0x3f)

/*
 *	Define a baud rate table that converts termios baud rate selector
 *	into the actual baud rate value. All baud rate calculations are based
 *	on the actual baud rate required.
 */
static unsigned int	stli_baudrates[] = {
	0, 50, 75, 110, 134, 150, 200, 300, 600, 1200, 1800, 2400, 4800,
	9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600
};

/*****************************************************************************/

/*
 *	Define some handy local macros...
 */
#undef MIN
#define	MIN(a,b)	(((a) <= (b)) ? (a) : (b))

#undef	TOLOWER
#define	TOLOWER(x)	((((x) >= 'A') && ((x) <= 'Z')) ? ((x) + 0x20) : (x))

/*****************************************************************************/

/*
 *	Prototype all functions in this driver!
 */

#ifdef MODULE
int		init_module(void);
void		cleanup_module(void);
static void	stli_argbrds(void);
static int	stli_parsebrd(stlconf_t *confp, char **argp);

static unsigned long	stli_atol(char *str);
#endif

int		stli_init(void);
static int	stli_open(struct tty_struct *tty, struct file *filp);
static void	stli_close(struct tty_struct *tty, struct file *filp);
static int	stli_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count);
static void	stli_putchar(struct tty_struct *tty, unsigned char ch);
static void	stli_flushchars(struct tty_struct *tty);
static int	stli_writeroom(struct tty_struct *tty);
static int	stli_charsinbuffer(struct tty_struct *tty);
static int	stli_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg);
static void	stli_settermios(struct tty_struct *tty, struct termios *old);
static void	stli_throttle(struct tty_struct *tty);
static void	stli_unthrottle(struct tty_struct *tty);
static void	stli_stop(struct tty_struct *tty);
static void	stli_start(struct tty_struct *tty);
static void	stli_flushbuffer(struct tty_struct *tty);
static void	stli_breakctl(struct tty_struct *tty, int state);
static void	stli_waituntilsent(struct tty_struct *tty, int timeout);
static void	stli_sendxchar(struct tty_struct *tty, char ch);
static void	stli_hangup(struct tty_struct *tty);
static int	stli_portinfo(stlibrd_t *brdp, stliport_t *portp, int portnr, char *pos);

static int	stli_brdinit(stlibrd_t *brdp);
static int	stli_startbrd(stlibrd_t *brdp);
static int	stli_memopen(struct inode *ip, struct file *fp);
static int	stli_memclose(struct inode *ip, struct file *fp);
static ssize_t	stli_memread(struct file *fp, char *buf, size_t count, loff_t *offp);
static ssize_t	stli_memwrite(struct file *fp, const char *buf, size_t count, loff_t *offp);
static int	stli_memioctl(struct inode *ip, struct file *fp, unsigned int cmd, unsigned long arg);
static void	stli_brdpoll(stlibrd_t *brdp, volatile cdkhdr_t *hdrp);
static void	stli_poll(unsigned long arg);
static int	stli_hostcmd(stlibrd_t *brdp, stliport_t *portp);
static int	stli_initopen(stlibrd_t *brdp, stliport_t *portp);
static int	stli_rawopen(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait);
static int	stli_rawclose(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait);
static int	stli_waitcarrier(stlibrd_t *brdp, stliport_t *portp, struct file *filp);
static void	stli_dohangup(void *arg);
static void	stli_delay(int len);
static int	stli_setport(stliport_t *portp);
static int	stli_cmdwait(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback);
static void	stli_sendcmd(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback);
static void	stli_dodelaycmd(stliport_t *portp, volatile cdkctrl_t *cp);
static void	stli_mkasyport(stliport_t *portp, asyport_t *pp, struct termios *tiosp);
static void	stli_mkasysigs(asysigs_t *sp, int dtr, int rts);
static long	stli_mktiocm(unsigned long sigvalue);
static void	stli_read(stlibrd_t *brdp, stliport_t *portp);
static void	stli_getserial(stliport_t *portp, struct serial_struct *sp);
static int	stli_setserial(stliport_t *portp, struct serial_struct *sp);
static int	stli_getbrdstats(combrd_t *bp);
static int	stli_getportstats(stliport_t *portp, comstats_t *cp);
static int	stli_portcmdstats(stliport_t *portp);
static int	stli_clrportstats(stliport_t *portp, comstats_t *cp);
static int	stli_getportstruct(unsigned long arg);
static int	stli_getbrdstruct(unsigned long arg);
static void	*stli_memalloc(int len);
static stlibrd_t *stli_allocbrd(void);

static void	stli_ecpinit(stlibrd_t *brdp);
static void	stli_ecpenable(stlibrd_t *brdp);
static void	stli_ecpdisable(stlibrd_t *brdp);
static char	*stli_ecpgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecpreset(stlibrd_t *brdp);
static void	stli_ecpintr(stlibrd_t *brdp);
static void	stli_ecpeiinit(stlibrd_t *brdp);
static void	stli_ecpeienable(stlibrd_t *brdp);
static void	stli_ecpeidisable(stlibrd_t *brdp);
static char	*stli_ecpeigetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecpeireset(stlibrd_t *brdp);
static void	stli_ecpmcenable(stlibrd_t *brdp);
static void	stli_ecpmcdisable(stlibrd_t *brdp);
static char	*stli_ecpmcgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecpmcreset(stlibrd_t *brdp);
static void	stli_ecppciinit(stlibrd_t *brdp);
static char	*stli_ecppcigetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_ecppcireset(stlibrd_t *brdp);

static void	stli_onbinit(stlibrd_t *brdp);
static void	stli_onbenable(stlibrd_t *brdp);
static void	stli_onbdisable(stlibrd_t *brdp);
static char	*stli_onbgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_onbreset(stlibrd_t *brdp);
static void	stli_onbeinit(stlibrd_t *brdp);
static void	stli_onbeenable(stlibrd_t *brdp);
static void	stli_onbedisable(stlibrd_t *brdp);
static char	*stli_onbegetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_onbereset(stlibrd_t *brdp);
static void	stli_bbyinit(stlibrd_t *brdp);
static char	*stli_bbygetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_bbyreset(stlibrd_t *brdp);
static void	stli_stalinit(stlibrd_t *brdp);
static char	*stli_stalgetmemptr(stlibrd_t *brdp, unsigned long offset, int line);
static void	stli_stalreset(stlibrd_t *brdp);

static stliport_t *stli_getport(int brdnr, int panelnr, int portnr);

static inline int	stli_initbrds(void);
static inline int	stli_initecp(stlibrd_t *brdp);
static inline int	stli_initonb(stlibrd_t *brdp);
static inline int	stli_findeisabrds(void);
static inline int	stli_eisamemprobe(stlibrd_t *brdp);
static inline int	stli_initports(stlibrd_t *brdp);
static inline int	stli_getbrdnr(void);

#ifdef	CONFIG_PCI
static inline int	stli_findpcibrds(void);
static inline int	stli_initpcibrd(int brdtype, struct pci_dev *devp);
#endif

/*****************************************************************************/

/*
 *	Define the driver info for a user level shared memory device. This
 *	device will work sort of like the /dev/kmem device - except that it
 *	will give access to the shared memory on the Stallion intelligent
 *	board. This is also a very useful debugging tool.
 */
static struct file_operations	stli_fsiomem = {
	NULL,		/* llseek */
	stli_memread,	/* read */
	stli_memwrite,	/* write */
	NULL,		/* readdir */
	NULL,		/* poll */
	stli_memioctl,	/* ioctl */
	NULL,		/* mmap */
	stli_memopen,	/* open */
	NULL,		/* flush */
	stli_memclose,	/* release */
	NULL,		/* fsync */
	NULL,		/* fasync */
	NULL,		/* check_media_change */
	NULL,		/* revalidate */
	NULL		/* lock */
};

/*****************************************************************************/

/*
 *	Define a timer_list entry for our poll routine. The slave board
 *	is polled every so often to see if anything needs doing. This is
 *	much cheaper on host cpu than using interrupts. It turns out to
 *	not increase character latency by much either...
 */
static struct timer_list	stli_timerlist = {
	NULL, NULL, 0, 0, stli_poll
};

static int	stli_timeron = 0;

/*
 *	Define the calculation for the timeout routine.
 */
#define	STLI_TIMEOUT	(jiffies + 1)

/*****************************************************************************/

#ifdef MODULE

/*
 *	Loadable module initialization stuff.
 */

int init_module()
{
	unsigned long	flags;

#if DEBUG
	printk("init_module()\n");
#endif

	save_flags(flags);
	cli();
	stli_init();
	restore_flags(flags);

	return(0);
}

/*****************************************************************************/

void cleanup_module()
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	unsigned long	flags;
	int		i, j;

#if DEBUG
	printk("cleanup_module()\n");
#endif

	printk(KERN_INFO "Unloading %s: version %s\n", stli_drvtitle,
		stli_drvversion);

	save_flags(flags);
	cli();

/*
 *	Free up all allocated resources used by the ports. This includes
 *	memory and interrupts.
 */
	if (stli_timeron) {
		stli_timeron = 0;
		del_timer(&stli_timerlist);
	}

	i = tty_unregister_driver(&stli_serial);
	j = tty_unregister_driver(&stli_callout);
	if (i || j) {
		printk("STALLION: failed to un-register tty driver, "
			"errno=%d,%d\n", -i, -j);
		restore_flags(flags);
		return;
	}
	if ((i = unregister_chrdev(STL_SIOMEMMAJOR, "staliomem")))
		printk("STALLION: failed to un-register serial memory device, "
			"errno=%d\n", -i);

	if (stli_tmpwritebuf != (char *) NULL)
		kfree_s(stli_tmpwritebuf, STLI_TXBUFSIZE);
	if (stli_txcookbuf != (char *) NULL)
		kfree_s(stli_txcookbuf, STLI_TXBUFSIZE);

	for (i = 0; (i < stli_nrbrds); i++) {
		if ((brdp = stli_brds[i]) == (stlibrd_t *) NULL)
			continue;
		for (j = 0; (j < STL_MAXPORTS); j++) {
			portp = brdp->ports[j];
			if (portp != (stliport_t *) NULL) {
				if (portp->tty != (struct tty_struct *) NULL)
					tty_hangup(portp->tty);
				kfree_s(portp, sizeof(stliport_t));
			}
		}

		iounmap(brdp->membase);
		if (brdp->iosize > 0)
			release_region(brdp->iobase, brdp->iosize);
		kfree_s(brdp, sizeof(stlibrd_t));
		stli_brds[i] = (stlibrd_t *) NULL;
	}

	restore_flags(flags);
}

/*****************************************************************************/

/*
 *	Check for any arguments passed in on the module load command line.
 */

static void stli_argbrds()
{
	stlconf_t	conf;
	stlibrd_t	*brdp;
	int		nrargs, i;

#if DEBUG
	printk("stli_argbrds()\n");
#endif

	nrargs = sizeof(stli_brdsp) / sizeof(char **);

	for (i = stli_nrbrds; (i < nrargs); i++) {
		memset(&conf, 0, sizeof(conf));
		if (stli_parsebrd(&conf, stli_brdsp[i]) == 0)
			continue;
		if ((brdp = stli_allocbrd()) == (stlibrd_t *) NULL)
			continue;
		stli_nrbrds = i + 1;
		brdp->brdnr = i;
		brdp->brdtype = conf.brdtype;
		brdp->iobase = conf.ioaddr1;
		brdp->memaddr = conf.memaddr;
		stli_brdinit(brdp);
	}
}

/*****************************************************************************/

/*
 *	Convert an ascii string number into an unsigned long.
 */

static unsigned long stli_atol(char *str)
{
	unsigned long	val;
	int		base, c;
	char		*sp;

	val = 0;
	sp = str;
	if ((*sp == '0') && (*(sp+1) == 'x')) {
		base = 16;
		sp += 2;
	} else if (*sp == '0') {
		base = 8;
		sp++;
	} else {
		base = 10;
	}

	for (; (*sp != 0); sp++) {
		c = (*sp > '9') ? (TOLOWER(*sp) - 'a' + 10) : (*sp - '0');
		if ((c < 0) || (c >= base)) {
			printk("STALLION: invalid argument %s\n", str);
			val = 0;
			break;
		}
		val = (val * base) + c;
	}
	return(val);
}

/*****************************************************************************/

/*
 *	Parse the supplied argument string, into the board conf struct.
 */

static int stli_parsebrd(stlconf_t *confp, char **argp)
{
	char	*sp;
	int	nrbrdnames, i;

#if DEBUG
	printk("stli_parsebrd(confp=%x,argp=%x)\n", (int) confp, (int) argp);
#endif

	if ((argp[0] == (char *) NULL) || (*argp[0] == 0))
		return(0);

	for (sp = argp[0], i = 0; ((*sp != 0) && (i < 25)); sp++, i++)
		*sp = TOLOWER(*sp);

	nrbrdnames = sizeof(stli_brdstr) / sizeof(stlibrdtype_t);
	for (i = 0; (i < nrbrdnames); i++) {
		if (strcmp(stli_brdstr[i].name, argp[0]) == 0)
			break;
	}
	if (i >= nrbrdnames) {
		printk("STALLION: unknown board name, %s?\n", argp[0]);
		return(0);
	}

	confp->brdtype = stli_brdstr[i].type;
	if ((argp[1] != (char *) NULL) && (*argp[1] != 0))
		confp->ioaddr1 = stli_atol(argp[1]);
	if ((argp[2] != (char *) NULL) && (*argp[2] != 0))
		confp->memaddr = stli_atol(argp[2]);
	return(1);
}

#endif

/*****************************************************************************/

/*
 *	Local driver kernel malloc routine.
 */

static void *stli_memalloc(int len)
{
	return((void *) kmalloc(len, GFP_KERNEL));
}

/*****************************************************************************/

static int stli_open(struct tty_struct *tty, struct file *filp)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	unsigned int	minordev;
	int		brdnr, portnr, rc;

#if DEBUG
	printk("stli_open(tty=%x,filp=%x): device=%x\n", (int) tty,
		(int) filp, tty->device);
#endif

	minordev = MINOR(tty->device);
	brdnr = MINOR2BRD(minordev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	brdp = stli_brds[brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);
	if ((brdp->state & BST_STARTED) == 0)
		return(-ENODEV);
	portnr = MINOR2PORT(minordev);
	if ((portnr < 0) || (portnr > brdp->nrports))
		return(-ENODEV);

	portp = brdp->ports[portnr];
	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	if (portp->devnr < 1)
		return(-ENODEV);

	MOD_INC_USE_COUNT;

/*
 *	Check if this port is in the middle of closing. If so then wait
 *	until it is closed then return error status based on flag settings.
 *	The sleep here does not need interrupt protection since the wakeup
 *	for it is done with the same context.
 */
	if (portp->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&portp->close_wait);
		if (portp->flags & ASYNC_HUP_NOTIFY)
			return(-EAGAIN);
		return(-ERESTARTSYS);
	}

/*
 *	On the first open of the device setup the port hardware, and
 *	initialize the per port data structure. Since initializing the port
 *	requires several commands to the board we will need to wait for any
 *	other open that is already initializing the port.
 */
	portp->tty = tty;
	tty->driver_data = portp;
	portp->refcount++;

	while (test_bit(ST_INITIALIZING, &portp->state)) {
		if (signal_pending(current))
			return(-ERESTARTSYS);
		interruptible_sleep_on(&portp->raw_wait);
	}

	if ((portp->flags & ASYNC_INITIALIZED) == 0) {
		set_bit(ST_INITIALIZING, &portp->state);
		if ((rc = stli_initopen(brdp, portp)) >= 0) {
			portp->flags |= ASYNC_INITIALIZED;
			clear_bit(TTY_IO_ERROR, &tty->flags);
		}
		clear_bit(ST_INITIALIZING, &portp->state);
		wake_up_interruptible(&portp->raw_wait);
		if (rc < 0)
			return(rc);
	}

/*
 *	Check if this port is in the middle of closing. If so then wait
 *	until it is closed then return error status, based on flag settings.
 *	The sleep here does not need interrupt protection since the wakeup
 *	for it is done with the same context.
 */
	if (portp->flags & ASYNC_CLOSING) {
		interruptible_sleep_on(&portp->close_wait);
		if (portp->flags & ASYNC_HUP_NOTIFY)
			return(-EAGAIN);
		return(-ERESTARTSYS);
	}

/*
 *	Based on type of open being done check if it can overlap with any
 *	previous opens still in effect. If we are a normal serial device
 *	then also we might have to wait for carrier.
 */
	if (tty->driver.subtype == STL_DRVTYPCALLOUT) {
		if (portp->flags & ASYNC_NORMAL_ACTIVE)
			return(-EBUSY);
		if (portp->flags & ASYNC_CALLOUT_ACTIVE) {
			if ((portp->flags & ASYNC_SESSION_LOCKOUT) &&
			    (portp->session != current->session))
				return(-EBUSY);
			if ((portp->flags & ASYNC_PGRP_LOCKOUT) &&
			    (portp->pgrp != current->pgrp))
				return(-EBUSY);
		}
		portp->flags |= ASYNC_CALLOUT_ACTIVE;
	} else {
		if (filp->f_flags & O_NONBLOCK) {
			if (portp->flags & ASYNC_CALLOUT_ACTIVE)
				return(-EBUSY);
		} else {
			if ((rc = stli_waitcarrier(brdp, portp, filp)) != 0)
				return(rc);
		}
		portp->flags |= ASYNC_NORMAL_ACTIVE;
	}

	if ((portp->refcount == 1) && (portp->flags & ASYNC_SPLIT_TERMIOS)) {
		if (tty->driver.subtype == STL_DRVTYPSERIAL)
			*tty->termios = portp->normaltermios;
		else
			*tty->termios = portp->callouttermios;
		stli_setport(portp);
	}

	portp->session = current->session;
	portp->pgrp = current->pgrp;
	return(0);
}

/*****************************************************************************/

static void stli_close(struct tty_struct *tty, struct file *filp)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	unsigned long	flags;

#if DEBUG
	printk("stli_close(tty=%x,filp=%x)\n", (int) tty, (int) filp);
#endif

	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	save_flags(flags);
	cli();
	if (tty_hung_up_p(filp)) {
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}
	if ((tty->count == 1) && (portp->refcount != 1))
		portp->refcount = 1;
	if (portp->refcount-- > 1) {
		MOD_DEC_USE_COUNT;
		restore_flags(flags);
		return;
	}

	portp->flags |= ASYNC_CLOSING;

	if (portp->flags & ASYNC_NORMAL_ACTIVE)
		portp->normaltermios = *tty->termios;
	if (portp->flags & ASYNC_CALLOUT_ACTIVE)
		portp->callouttermios = *tty->termios;

/*
 *	May want to wait for data to drain before closing. The BUSY flag
 *	keeps track of whether we are still transmitting or not. It is
 *	updated by messages from the slave - indicating when all chars
 *	really have drained.
 */
	if (tty == stli_txcooktty)
		stli_flushchars(tty);
	tty->closing = 1;
	if (portp->closing_wait != ASYNC_CLOSING_WAIT_NONE)
		tty_wait_until_sent(tty, portp->closing_wait);

	portp->flags &= ~ASYNC_INITIALIZED;
	brdp = stli_brds[portp->brdnr];
	stli_rawclose(brdp, portp, 0, 0);
	if (tty->termios->c_cflag & HUPCL) {
		stli_mkasysigs(&portp->asig, 0, 0);
		if (test_bit(ST_CMDING, &portp->state))
			set_bit(ST_DOSIGS, &portp->state);
		else
			stli_sendcmd(brdp, portp, A_SETSIGNALS, &portp->asig,
				sizeof(asysigs_t), 0);
	}
	clear_bit(ST_TXBUSY, &portp->state);
	clear_bit(ST_RXSTOP, &portp->state);
	set_bit(TTY_IO_ERROR, &tty->flags);
	if (tty->ldisc.flush_buffer)
		(tty->ldisc.flush_buffer)(tty);
	set_bit(ST_DOFLUSHRX, &portp->state);
	stli_flushbuffer(tty);

	tty->closing = 0;
	portp->tty = (struct tty_struct *) NULL;

	if (portp->openwaitcnt) {
		if (portp->close_delay)
			stli_delay(portp->close_delay);
		wake_up_interruptible(&portp->open_wait);
	}

	portp->flags &= ~(ASYNC_CALLOUT_ACTIVE | ASYNC_NORMAL_ACTIVE |
		ASYNC_CLOSING);
	wake_up_interruptible(&portp->close_wait);
	MOD_DEC_USE_COUNT;
	restore_flags(flags);
}

/*****************************************************************************/

/*
 *	Carry out first open operations on a port. This involves a number of
 *	commands to be sent to the slave. We need to open the port, set the
 *	notification events, set the initial port settings, get and set the
 *	initial signal values. We sleep and wait in between each one. But
 *	this still all happens pretty quickly.
 */

static int stli_initopen(stlibrd_t *brdp, stliport_t *portp)
{
	struct tty_struct	*tty;
	asynotify_t		nt;
	asyport_t		aport;
	int			rc;

#if DEBUG
	printk("stli_initopen(brdp=%x,portp=%x)\n", (int) brdp, (int) portp);
#endif

	if ((rc = stli_rawopen(brdp, portp, 0, 1)) < 0)
		return(rc);

	memset(&nt, 0, sizeof(asynotify_t));
	nt.data = (DT_TXLOW | DT_TXEMPTY | DT_RXBUSY | DT_RXBREAK);
	nt.signal = SG_DCD;
	if ((rc = stli_cmdwait(brdp, portp, A_SETNOTIFY, &nt,
	    sizeof(asynotify_t), 0)) < 0)
		return(rc);

	tty = portp->tty;
	if (tty == (struct tty_struct *) NULL)
		return(-ENODEV);
	stli_mkasyport(portp, &aport, tty->termios);
	if ((rc = stli_cmdwait(brdp, portp, A_SETPORT, &aport,
	    sizeof(asyport_t), 0)) < 0)
		return(rc);

	set_bit(ST_GETSIGS, &portp->state);
	if ((rc = stli_cmdwait(brdp, portp, A_GETSIGNALS, &portp->asig,
	    sizeof(asysigs_t), 1)) < 0)
		return(rc);
	if (test_and_clear_bit(ST_GETSIGS, &portp->state))
		portp->sigs = stli_mktiocm(portp->asig.sigvalue);
	stli_mkasysigs(&portp->asig, 1, 1);
	if ((rc = stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig,
	    sizeof(asysigs_t), 0)) < 0)
		return(rc);

	return(0);
}

/*****************************************************************************/

/*
 *	Send an open message to the slave. This will sleep waiting for the
 *	acknowledgement, so must have user context. We need to co-ordinate
 *	with close events here, since we don't want open and close events
 *	to overlap.
 */

static int stli_rawopen(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkctrl_t	*cp;
	volatile unsigned char	*bits;
	unsigned long		flags;
	int			rc;

#if DEBUG
	printk("stli_rawopen(brdp=%x,portp=%x,arg=%x,wait=%d)\n",
		(int) brdp, (int) portp, (int) arg, wait);
#endif

/*
 *	Send a message to the slave to open this port.
 */
	save_flags(flags);
	cli();

/*
 *	Slave is already closing this port. This can happen if a hangup
 *	occurs on this port. So we must wait until it is complete. The
 *	order of opens and closes may not be preserved across shared
 *	memory, so we must wait until it is complete.
 */
	while (test_bit(ST_CLOSING, &portp->state)) {
		if (signal_pending(current)) {
			restore_flags(flags);
			return(-ERESTARTSYS);
		}
		interruptible_sleep_on(&portp->raw_wait);
	}

/*
 *	Everything is ready now, so write the open message into shared
 *	memory. Once the message is in set the service bits to say that
 *	this port wants service.
 */
	EBRDENABLE(brdp);
	cp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->ctrl;
	cp->openarg = arg;
	cp->open = 1;
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset +
		portp->portidx;
	*bits |= portp->portbit;
	EBRDDISABLE(brdp);

	if (wait == 0) {
		restore_flags(flags);
		return(0);
	}

/*
 *	Slave is in action, so now we must wait for the open acknowledgment
 *	to come back.
 */
	rc = 0;
	set_bit(ST_OPENING, &portp->state);
	while (test_bit(ST_OPENING, &portp->state)) {
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}
		interruptible_sleep_on(&portp->raw_wait);
	}
	restore_flags(flags);

	if ((rc == 0) && (portp->rc != 0))
		rc = -EIO;
	return(rc);
}

/*****************************************************************************/

/*
 *	Send a close message to the slave. Normally this will sleep waiting
 *	for the acknowledgement, but if wait parameter is 0 it will not. If
 *	wait is true then must have user context (to sleep).
 */

static int stli_rawclose(stlibrd_t *brdp, stliport_t *portp, unsigned long arg, int wait)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkctrl_t	*cp;
	volatile unsigned char	*bits;
	unsigned long		flags;
	int			rc;

#if DEBUG
	printk("stli_rawclose(brdp=%x,portp=%x,arg=%x,wait=%d)\n",
		(int) brdp, (int) portp, (int) arg, wait);
#endif

	save_flags(flags);
	cli();

/*
 *	Slave is already closing this port. This can happen if a hangup
 *	occurs on this port.
 */
	if (wait) {
		while (test_bit(ST_CLOSING, &portp->state)) {
			if (signal_pending(current)) {
				restore_flags(flags);
				return(-ERESTARTSYS);
			}
			interruptible_sleep_on(&portp->raw_wait);
		}
	}

/*
 *	Write the close command into shared memory.
 */
	EBRDENABLE(brdp);
	cp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->ctrl;
	cp->closearg = arg;
	cp->close = 1;
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset +
		portp->portidx;
	*bits |= portp->portbit;
	EBRDDISABLE(brdp);

	set_bit(ST_CLOSING, &portp->state);
	if (wait == 0) {
		restore_flags(flags);
		return(0);
	}

/*
 *	Slave is in action, so now we must wait for the open acknowledgment
 *	to come back.
 */
	rc = 0;
	while (test_bit(ST_CLOSING, &portp->state)) {
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}
		interruptible_sleep_on(&portp->raw_wait);
	}
	restore_flags(flags);

	if ((rc == 0) && (portp->rc != 0))
		rc = -EIO;
	return(rc);
}

/*****************************************************************************/

/*
 *	Send a command to the slave and wait for the response. This must
 *	have user context (it sleeps). This routine is generic in that it
 *	can send any type of command. Its purpose is to wait for that command
 *	to complete (as opposed to initiating the command then returning).
 */

static int stli_cmdwait(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback)
{
	unsigned long	flags;

#if DEBUG
	printk("stli_cmdwait(brdp=%x,portp=%x,cmd=%x,arg=%x,size=%d,"
		"copyback=%d)\n", (int) brdp, (int) portp, (int) cmd,
		(int) arg, size, copyback);
#endif

	save_flags(flags);
	cli();
	while (test_bit(ST_CMDING, &portp->state)) {
		if (signal_pending(current)) {
			restore_flags(flags);
			return(-ERESTARTSYS);
		}
		interruptible_sleep_on(&portp->raw_wait);
	}

	stli_sendcmd(brdp, portp, cmd, arg, size, copyback);

	while (test_bit(ST_CMDING, &portp->state)) {
		if (signal_pending(current)) {
			restore_flags(flags);
			return(-ERESTARTSYS);
		}
		interruptible_sleep_on(&portp->raw_wait);
	}
	restore_flags(flags);

	if (portp->rc != 0)
		return(-EIO);
	return(0);
}

/*****************************************************************************/

/*
 *	Send the termios settings for this port to the slave. This sleeps
 *	waiting for the command to complete - so must have user context.
 */

static int stli_setport(stliport_t *portp)
{
	stlibrd_t	*brdp;
	asyport_t	aport;

#if DEBUG
	printk("stli_setport(portp=%x)\n", (int) portp);
#endif

	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	if (portp->tty == (struct tty_struct *) NULL)
		return(-ENODEV);
	if ((portp->brdnr < 0) && (portp->brdnr >= stli_nrbrds))
		return(-ENODEV);
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);

	stli_mkasyport(portp, &aport, portp->tty->termios);
	return(stli_cmdwait(brdp, portp, A_SETPORT, &aport, sizeof(asyport_t), 0));
}

/*****************************************************************************/

/*
 *	Wait for a specified delay period, this is not a busy-loop. It will
 *	give up the processor while waiting. Unfortunately this has some
 *	rather intimate knowledge of the process management stuff.
 */

static void stli_delay(int len)
{
#if DEBUG
	printk("stli_delay(len=%d)\n", len);
#endif
	if (len > 0) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(len);
		current->state = TASK_RUNNING;
	}
}

/*****************************************************************************/

/*
 *	Possibly need to wait for carrier (DCD signal) to come high. Say
 *	maybe because if we are clocal then we don't need to wait...
 */

static int stli_waitcarrier(stlibrd_t *brdp, stliport_t *portp, struct file *filp)
{
	unsigned long	flags;
	int		rc, doclocal;

#if DEBUG
	printk("stli_waitcarrier(brdp=%x,portp=%x,filp=%x)\n",
		(int) brdp, (int) portp, (int) filp);
#endif

	rc = 0;
	doclocal = 0;

	if (portp->flags & ASYNC_CALLOUT_ACTIVE) {
		if (portp->normaltermios.c_cflag & CLOCAL)
			doclocal++;
	} else {
		if (portp->tty->termios->c_cflag & CLOCAL)
			doclocal++;
	}

	save_flags(flags);
	cli();
	portp->openwaitcnt++;
	if (! tty_hung_up_p(filp))
		portp->refcount--;

	for (;;) {
		if ((portp->flags & ASYNC_CALLOUT_ACTIVE) == 0) {
			stli_mkasysigs(&portp->asig, 1, 1);
			if ((rc = stli_cmdwait(brdp, portp, A_SETSIGNALS,
			    &portp->asig, sizeof(asysigs_t), 0)) < 0)
				break;
		}
		if (tty_hung_up_p(filp) ||
		    ((portp->flags & ASYNC_INITIALIZED) == 0)) {
			if (portp->flags & ASYNC_HUP_NOTIFY)
				rc = -EBUSY;
			else
				rc = -ERESTARTSYS;
			break;
		}
		if (((portp->flags & ASYNC_CALLOUT_ACTIVE) == 0) &&
		    ((portp->flags & ASYNC_CLOSING) == 0) &&
		    (doclocal || (portp->sigs & TIOCM_CD))) {
			break;
		}
		if (signal_pending(current)) {
			rc = -ERESTARTSYS;
			break;
		}
		interruptible_sleep_on(&portp->open_wait);
	}

	if (! tty_hung_up_p(filp))
		portp->refcount++;
	portp->openwaitcnt--;
	restore_flags(flags);

	return(rc);
}

/*****************************************************************************/

/*
 *	Write routine. Take the data and put it in the shared memory ring
 *	queue. If port is not already sending chars then need to mark the
 *	service bits for this port.
 */

static int stli_write(struct tty_struct *tty, int from_user, const unsigned char *buf, int count)
{
	volatile cdkasy_t	*ap;
	volatile cdkhdr_t	*hdrp;
	volatile unsigned char	*bits;
	unsigned char		*shbuf, *chbuf;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		len, stlen, head, tail, size;
	unsigned long		flags;

#if DEBUG
	printk("stli_write(tty=%x,from_user=%d,buf=%x,count=%d)\n",
		(int) tty, from_user, (int) buf, count);
#endif

	if ((tty == (struct tty_struct *) NULL) ||
	    (stli_tmpwritebuf == (char *) NULL))
		return(0);
	if (tty == stli_txcooktty)
		stli_flushchars(tty);
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(0);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(0);
	chbuf = (unsigned char *) buf;

/*
 *	If copying direct from user space we need to be able to handle page
 *	faults while we are copying. To do this copy as much as we can now
 *	into a kernel buffer. From there we copy it into shared memory. The
 *	big problem is that we do not want shared memory enabled when we are
 *	sleeping (other boards may be serviced while asleep). Something else
 *	to note here is the reading of the tail twice. Since the boards
 *	shared memory can be on an 8-bit bus then we need to be very careful
 *	reading 16 bit quantities - since both the board (slave) and host
 *	could be writing and reading at the same time.
 */
	if (from_user) {
		save_flags(flags);
		cli();
		EBRDENABLE(brdp);
		ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
		head = (unsigned int) ap->txq.head;
		tail = (unsigned int) ap->txq.tail;
		if (tail != ((unsigned int) ap->txq.tail))
			tail = (unsigned int) ap->txq.tail;
		len = (head >= tail) ? (portp->txsize - (head - tail) - 1) :
			(tail - head - 1);
		count = MIN(len, count);
		EBRDDISABLE(brdp);
		restore_flags(flags);

		down(&stli_tmpwritesem);
		copy_from_user(stli_tmpwritebuf, chbuf, count);
		chbuf = &stli_tmpwritebuf[0];
	}

/*
 *	All data is now local, shove as much as possible into shared memory.
 */
	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	head = (unsigned int) ap->txq.head;
	tail = (unsigned int) ap->txq.tail;
	if (tail != ((unsigned int) ap->txq.tail))
		tail = (unsigned int) ap->txq.tail;
	size = portp->txsize;
	if (head >= tail) {
		len = size - (head - tail) - 1;
		stlen = size - head;
	} else {
		len = tail - head - 1;
		stlen = len;
	}

	len = MIN(len, count);
	count = 0;
	shbuf = (char *) EBRDGETMEMPTR(brdp, portp->txoffset);

	while (len > 0) {
		stlen = MIN(len, stlen);
		memcpy((shbuf + head), chbuf, stlen);
		chbuf += stlen;
		len -= stlen;
		count += stlen;
		head += stlen;
		if (head >= size) {
			head = 0;
			stlen = tail;
		}
	}

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	ap->txq.head = head;
	if (test_bit(ST_TXBUSY, &portp->state)) {
		if (ap->changed.data & DT_TXEMPTY)
			ap->changed.data &= ~DT_TXEMPTY;
	}
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset +
		portp->portidx;
	*bits |= portp->portbit;
	set_bit(ST_TXBUSY, &portp->state);
	EBRDDISABLE(brdp);

	if (from_user)
		up(&stli_tmpwritesem);
	restore_flags(flags);

	return(count);
}

/*****************************************************************************/

/*
 *	Output a single character. We put it into a temporary local buffer
 *	(for speed) then write out that buffer when the flushchars routine
 *	is called. There is a safety catch here so that if some other port
 *	writes chars before the current buffer has been, then we write them
 *	first them do the new ports.
 */

static void stli_putchar(struct tty_struct *tty, unsigned char ch)
{
#if DEBUG
	printk("stli_putchar(tty=%x,ch=%x)\n", (int) tty, (int) ch);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	if (tty != stli_txcooktty) {
		if (stli_txcooktty != (struct tty_struct *) NULL)
			stli_flushchars(stli_txcooktty);
		stli_txcooktty = tty;
	}

	stli_txcookbuf[stli_txcooksize++] = ch;
}

/*****************************************************************************/

/*
 *	Transfer characters from the local TX cooking buffer to the board.
 *	We sort of ignore the tty that gets passed in here. We rely on the
 *	info stored with the TX cook buffer to tell us which port to flush
 *	the data on. In any case we clean out the TX cook buffer, for re-use
 *	by someone else.
 */

static void stli_flushchars(struct tty_struct *tty)
{
	volatile cdkhdr_t	*hdrp;
	volatile unsigned char	*bits;
	volatile cdkasy_t	*ap;
	struct tty_struct	*cooktty;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		len, stlen, head, tail, size, count, cooksize;
	unsigned char		*buf, *shbuf;
	unsigned long		flags;

#if DEBUG
	printk("stli_flushchars(tty=%x)\n", (int) tty);
#endif

	cooksize = stli_txcooksize;
	cooktty = stli_txcooktty;
	stli_txcooksize = 0;
	stli_txcookrealsize = 0;
	stli_txcooktty = (struct tty_struct *) NULL;

	if (tty == (struct tty_struct *) NULL)
		return;
	if (cooktty == (struct tty_struct *) NULL)
		return;
	if (tty != cooktty)
		tty = cooktty;
	if (cooksize == 0)
		return;

	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	head = (unsigned int) ap->txq.head;
	tail = (unsigned int) ap->txq.tail;
	if (tail != ((unsigned int) ap->txq.tail))
		tail = (unsigned int) ap->txq.tail;
	size = portp->txsize;
	if (head >= tail) {
		len = size - (head - tail) - 1;
		stlen = size - head;
	} else {
		len = tail - head - 1;
		stlen = len;
	}

	len = MIN(len, cooksize);
	count = 0;
	shbuf = (char *) EBRDGETMEMPTR(brdp, portp->txoffset);
	buf = stli_txcookbuf;

	while (len > 0) {
		stlen = MIN(len, stlen);
		memcpy((shbuf + head), buf, stlen);
		buf += stlen;
		len -= stlen;
		count += stlen;
		head += stlen;
		if (head >= size) {
			head = 0;
			stlen = tail;
		}
	}

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	ap->txq.head = head;

	if (test_bit(ST_TXBUSY, &portp->state)) {
		if (ap->changed.data & DT_TXEMPTY)
			ap->changed.data &= ~DT_TXEMPTY;
	}
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset +
		portp->portidx;
	*bits |= portp->portbit;
	set_bit(ST_TXBUSY, &portp->state);

	EBRDDISABLE(brdp);
	restore_flags(flags);
}

/*****************************************************************************/

static int stli_writeroom(struct tty_struct *tty)
{
	volatile cdkasyrq_t	*rp;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		head, tail, len;
	unsigned long		flags;

#if DEBUG
	printk("stli_writeroom(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return(0);
	if (tty == stli_txcooktty) {
		if (stli_txcookrealsize != 0) {
			len = stli_txcookrealsize - stli_txcooksize;
			return(len);
		}
	}

	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(0);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(0);

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->txq;
	head = (unsigned int) rp->head;
	tail = (unsigned int) rp->tail;
	if (tail != ((unsigned int) rp->tail))
		tail = (unsigned int) rp->tail;
	len = (head >= tail) ? (portp->txsize - (head - tail)) : (tail - head);
	len--;
	EBRDDISABLE(brdp);
	restore_flags(flags);

	if (tty == stli_txcooktty) {
		stli_txcookrealsize = len;
		len -= stli_txcooksize;
	}
	return(len);
}

/*****************************************************************************/

/*
 *	Return the number of characters in the transmit buffer. Normally we
 *	will return the number of chars in the shared memory ring queue.
 *	We need to kludge around the case where the shared memory buffer is
 *	empty but not all characters have drained yet, for this case just
 *	return that there is 1 character in the buffer!
 */

static int stli_charsinbuffer(struct tty_struct *tty)
{
	volatile cdkasyrq_t	*rp;
	stliport_t		*portp;
	stlibrd_t		*brdp;
	unsigned int		head, tail, len;
	unsigned long		flags;

#if DEBUG
	printk("stli_charsinbuffer(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return(0);
	if (tty == stli_txcooktty)
		stli_flushchars(tty);
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(0);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(0);

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->txq;
	head = (unsigned int) rp->head;
	tail = (unsigned int) rp->tail;
	if (tail != ((unsigned int) rp->tail))
		tail = (unsigned int) rp->tail;
	len = (head >= tail) ? (head - tail) : (portp->txsize - (tail - head));
	if ((len == 0) && test_bit(ST_TXBUSY, &portp->state))
		len = 1;
	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(len);
}

/*****************************************************************************/

/*
 *	Generate the serial struct info.
 */

static void stli_getserial(stliport_t *portp, struct serial_struct *sp)
{
	struct serial_struct	sio;
	stlibrd_t		*brdp;

#if DEBUG
	printk("stli_getserial(portp=%x,sp=%x)\n", (int) portp, (int) sp);
#endif

	memset(&sio, 0, sizeof(struct serial_struct));
	sio.type = PORT_UNKNOWN;
	sio.line = portp->portnr;
	sio.irq = 0;
	sio.flags = portp->flags;
	sio.baud_base = portp->baud_base;
	sio.close_delay = portp->close_delay;
	sio.closing_wait = portp->closing_wait;
	sio.custom_divisor = portp->custom_divisor;
	sio.xmit_fifo_size = 0;
	sio.hub6 = 0;

	brdp = stli_brds[portp->brdnr];
	if (brdp != (stlibrd_t *) NULL)
		sio.port = brdp->iobase;
		
	copy_to_user(sp, &sio, sizeof(struct serial_struct));
}

/*****************************************************************************/

/*
 *	Set port according to the serial struct info.
 *	At this point we do not do any auto-configure stuff, so we will
 *	just quietly ignore any requests to change irq, etc.
 */

static int stli_setserial(stliport_t *portp, struct serial_struct *sp)
{
	struct serial_struct	sio;
	int			rc;

#if DEBUG
	printk("stli_setserial(portp=%x,sp=%x)\n", (int) portp, (int) sp);
#endif

	copy_from_user(&sio, sp, sizeof(struct serial_struct));
	if (!capable(CAP_SYS_ADMIN)) {
		if ((sio.baud_base != portp->baud_base) ||
		    (sio.close_delay != portp->close_delay) ||
		    ((sio.flags & ~ASYNC_USR_MASK) !=
		    (portp->flags & ~ASYNC_USR_MASK)))
			return(-EPERM);
	} 

	portp->flags = (portp->flags & ~ASYNC_USR_MASK) |
		(sio.flags & ASYNC_USR_MASK);
	portp->baud_base = sio.baud_base;
	portp->close_delay = sio.close_delay;
	portp->closing_wait = sio.closing_wait;
	portp->custom_divisor = sio.custom_divisor;

	if ((rc = stli_setport(portp)) < 0)
		return(rc);
	return(0);
}

/*****************************************************************************/

static int stli_ioctl(struct tty_struct *tty, struct file *file, unsigned int cmd, unsigned long arg)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	unsigned long	lval;
	unsigned int	ival;
	int		rc;

#if DEBUG
	printk("stli_ioctl(tty=%x,file=%x,cmd=%x,arg=%x)\n",
		(int) tty, (int) file, cmd, (int) arg);
#endif

	if (tty == (struct tty_struct *) NULL)
		return(-ENODEV);
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return(0);
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(0);

	if ((cmd != TIOCGSERIAL) && (cmd != TIOCSSERIAL) &&
 	    (cmd != COM_GETPORTSTATS) && (cmd != COM_CLRPORTSTATS)) {
		if (tty->flags & (1 << TTY_IO_ERROR))
			return(-EIO);
	}

	rc = 0;

	switch (cmd) {
	case TIOCGSOFTCAR:
		rc = put_user(((tty->termios->c_cflag & CLOCAL) ? 1 : 0),
			(unsigned int *) arg);
		break;
	case TIOCSSOFTCAR:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		    sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			tty->termios->c_cflag =
				(tty->termios->c_cflag & ~CLOCAL) |
				(ival ? CLOCAL : 0);
		}
		break;
	case TIOCMGET:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(unsigned int))) == 0) {
			if ((rc = stli_cmdwait(brdp, portp, A_GETSIGNALS,
			    &portp->asig, sizeof(asysigs_t), 1)) < 0)
				return(rc);
			lval = stli_mktiocm(portp->asig.sigvalue);
			put_user(lval, (unsigned int *) arg);
		}
		break;
	case TIOCMBIS:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		    sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			stli_mkasysigs(&portp->asig,
				((ival & TIOCM_DTR) ? 1 : -1),
				((ival & TIOCM_RTS) ? 1 : -1));
			rc = stli_cmdwait(brdp, portp, A_SETSIGNALS,
				&portp->asig, sizeof(asysigs_t), 0);
		}
		break;
	case TIOCMBIC:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		    sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			stli_mkasysigs(&portp->asig,
				((ival & TIOCM_DTR) ? 0 : -1),
				((ival & TIOCM_RTS) ? 0 : -1));
			rc = stli_cmdwait(brdp, portp, A_SETSIGNALS,
				&portp->asig, sizeof(asysigs_t), 0);
		}
		break;
	case TIOCMSET:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		    sizeof(unsigned int))) == 0) {
			get_user(ival, (unsigned int *) arg);
			stli_mkasysigs(&portp->asig,
				((ival & TIOCM_DTR) ? 1 : 0),
				((ival & TIOCM_RTS) ? 1 : 0));
			rc = stli_cmdwait(brdp, portp, A_SETSIGNALS,
				&portp->asig, sizeof(asysigs_t), 0);
		}
		break;
	case TIOCGSERIAL:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(struct serial_struct))) == 0)
			stli_getserial(portp, (struct serial_struct *) arg);
		break;
	case TIOCSSERIAL:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		    sizeof(struct serial_struct))) == 0)
			rc = stli_setserial(portp, (struct serial_struct *)arg);
		break;
	case STL_GETPFLAG:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(unsigned long))) == 0)
			put_user(portp->pflag, (unsigned int *) arg);
		break;
	case STL_SETPFLAG:
		if ((rc = verify_area(VERIFY_READ, (void *) arg,
		    sizeof(unsigned long))) == 0) {
			get_user(portp->pflag, (unsigned int *) arg);
			stli_setport(portp);
		}
		break;
	case COM_GETPORTSTATS:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(comstats_t))) == 0)
			rc = stli_getportstats(portp, (comstats_t *) arg);
		break;
	case COM_CLRPORTSTATS:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(comstats_t))) == 0)
			rc = stli_clrportstats(portp, (comstats_t *) arg);
		break;
	case TIOCSERCONFIG:
	case TIOCSERGWILD:
	case TIOCSERSWILD:
	case TIOCSERGETLSR:
	case TIOCSERGSTRUCT:
	case TIOCSERGETMULTI:
	case TIOCSERSETMULTI:
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	return(rc);
}

/*****************************************************************************/

/*
 *	This routine assumes that we have user context and can sleep.
 *	Looks like it is true for the current ttys implementation..!!
 */

static void stli_settermios(struct tty_struct *tty, struct termios *old)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	struct termios	*tiosp;
	asyport_t	aport;

#if DEBUG
	printk("stli_settermios(tty=%x,old=%x)\n", (int) tty, (int) old);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	tiosp = tty->termios;
	if ((tiosp->c_cflag == old->c_cflag) &&
	    (tiosp->c_iflag == old->c_iflag))
		return;

	stli_mkasyport(portp, &aport, tiosp);
	stli_cmdwait(brdp, portp, A_SETPORT, &aport, sizeof(asyport_t), 0);
	stli_mkasysigs(&portp->asig, ((tiosp->c_cflag & CBAUD) ? 1 : 0), -1);
	stli_cmdwait(brdp, portp, A_SETSIGNALS, &portp->asig,
		sizeof(asysigs_t), 0);
	if ((old->c_cflag & CRTSCTS) && ((tiosp->c_cflag & CRTSCTS) == 0))
		tty->hw_stopped = 0;
	if (((old->c_cflag & CLOCAL) == 0) && (tiosp->c_cflag & CLOCAL))
		wake_up_interruptible(&portp->open_wait);
}

/*****************************************************************************/

/*
 *	Attempt to flow control who ever is sending us data. We won't really
 *	do any flow control action here. We can't directly, and even if we
 *	wanted to we would have to send a command to the slave. The slave
 *	knows how to flow control, and will do so when its buffers reach its
 *	internal high water marks. So what we will do is set a local state
 *	bit that will stop us sending any RX data up from the poll routine
 *	(which is the place where RX data from the slave is handled).
 */

static void stli_throttle(struct tty_struct *tty)
{
	stliport_t	*portp;

#if DEBUG
	printk("stli_throttle(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	set_bit(ST_RXSTOP, &portp->state);
}

/*****************************************************************************/

/*
 *	Unflow control the device sending us data... That means that all
 *	we have to do is clear the RXSTOP state bit. The next poll call
 *	will then be able to pass the RX data back up.
 */

static void stli_unthrottle(struct tty_struct *tty)
{
	stliport_t	*portp;

#if DEBUG
	printk("stli_unthrottle(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	clear_bit(ST_RXSTOP, &portp->state);
}

/*****************************************************************************/

/*
 *	Stop the transmitter. Basically to do this we will just turn TX
 *	interrupts off.
 */

static void stli_stop(struct tty_struct *tty)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	asyctrl_t	actrl;

#if DEBUG
	printk("stli_stop(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	memset(&actrl, 0, sizeof(asyctrl_t));
	actrl.txctrl = CT_STOPFLOW;
#if 0
	stli_cmdwait(brdp, portp, A_PORTCTRL, &actrl, sizeof(asyctrl_t), 0);
#endif
}

/*****************************************************************************/

/*
 *	Start the transmitter again. Just turn TX interrupts back on.
 */

static void stli_start(struct tty_struct *tty)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	asyctrl_t	actrl;

#if DEBUG
	printk("stli_start(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	memset(&actrl, 0, sizeof(asyctrl_t));
	actrl.txctrl = CT_STARTFLOW;
#if 0
	stli_cmdwait(brdp, portp, A_PORTCTRL, &actrl, sizeof(asyctrl_t), 0);
#endif
}

/*****************************************************************************/

/*
 *	Scheduler called hang up routine. This is called from the scheduler,
 *	not direct from the driver "poll" routine. We can't call it there
 *	since the real local hangup code will enable/disable the board and
 *	other things that we can't do while handling the poll. Much easier
 *	to deal with it some time later (don't really care when, hangups
 *	aren't that time critical).
 */

static void stli_dohangup(void *arg)
{
	stliport_t	*portp;

#if DEBUG
	printk("stli_dohangup(portp=%x)\n", (int) arg);
#endif

	portp = (stliport_t *) arg;
	if (portp == (stliport_t *) NULL)
		return;
	if (portp->tty == (struct tty_struct *) NULL)
		return;
	tty_hangup(portp->tty);
}

/*****************************************************************************/

/*
 *	Hangup this port. This is pretty much like closing the port, only
 *	a little more brutal. No waiting for data to drain. Shutdown the
 *	port and maybe drop signals. This is rather tricky really. We want
 *	to close the port as well.
 */

static void stli_hangup(struct tty_struct *tty)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	unsigned long	flags;

#if DEBUG
	printk("stli_hangup(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	portp->flags &= ~ASYNC_INITIALIZED;

	save_flags(flags);
	cli();
	if (! test_bit(ST_CLOSING, &portp->state))
		stli_rawclose(brdp, portp, 0, 0);
	if (tty->termios->c_cflag & HUPCL) {
		stli_mkasysigs(&portp->asig, 0, 0);
		if (test_bit(ST_CMDING, &portp->state)) {
			set_bit(ST_DOSIGS, &portp->state);
			set_bit(ST_DOFLUSHTX, &portp->state);
			set_bit(ST_DOFLUSHRX, &portp->state);
		} else {
			stli_sendcmd(brdp, portp, A_SETSIGNALSF,
				&portp->asig, sizeof(asysigs_t), 0);
		}
	}
	restore_flags(flags);

	clear_bit(ST_TXBUSY, &portp->state);
	clear_bit(ST_RXSTOP, &portp->state);
	set_bit(TTY_IO_ERROR, &tty->flags);
	portp->tty = (struct tty_struct *) NULL;
	portp->flags &= ~(ASYNC_NORMAL_ACTIVE | ASYNC_CALLOUT_ACTIVE);
	portp->refcount = 0;
	wake_up_interruptible(&portp->open_wait);
}

/*****************************************************************************/

/*
 *	Flush characters from the lower buffer. We may not have user context
 *	so we cannot sleep waiting for it to complete. Also we need to check
 *	if there is chars for this port in the TX cook buffer, and flush them
 *	as well.
 */

static void stli_flushbuffer(struct tty_struct *tty)
{
	stliport_t	*portp;
	stlibrd_t	*brdp;
	unsigned long	ftype, flags;

#if DEBUG
	printk("stli_flushbuffer(tty=%x)\n", (int) tty);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	save_flags(flags);
	cli();
	if (tty == stli_txcooktty) {
		stli_txcooktty = (struct tty_struct *) NULL;
		stli_txcooksize = 0;
		stli_txcookrealsize = 0;
	}
	if (test_bit(ST_CMDING, &portp->state)) {
		set_bit(ST_DOFLUSHTX, &portp->state);
	} else {
		ftype = FLUSHTX;
		if (test_bit(ST_DOFLUSHRX, &portp->state)) {
			ftype |= FLUSHRX;
			clear_bit(ST_DOFLUSHRX, &portp->state);
		}
		stli_sendcmd(brdp, portp, A_FLUSH, &ftype,
			sizeof(unsigned long), 0);
	}
	restore_flags(flags);

	wake_up_interruptible(&tty->write_wait);
	if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
	    tty->ldisc.write_wakeup)
		(tty->ldisc.write_wakeup)(tty);
}

/*****************************************************************************/

static void stli_breakctl(struct tty_struct *tty, int state)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	long		arg;
	/* long savestate, savetime; */

#if DEBUG
	printk("stli_breakctl(tty=%x,state=%d)\n", (int) tty, state);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

/*
 *	Due to a bug in the tty send_break() code we need to preserve
 *	the current process state and timeout...
	savetime = current->timeout;
	savestate = current->state;
 */

	arg = (state == -1) ? BREAKON : BREAKOFF;
	stli_cmdwait(brdp, portp, A_BREAK, &arg, sizeof(long), 0);

/*
 *
	current->timeout = savetime;
	current->state = savestate;
 */
}

/*****************************************************************************/

static void stli_waituntilsent(struct tty_struct *tty, int timeout)
{
	stliport_t	*portp;
	unsigned long	tend;

#if DEBUG
	printk("stli_waituntilsent(tty=%x,timeout=%x)\n", (int) tty, timeout);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;

	if (timeout == 0)
		timeout = HZ;
	tend = jiffies + timeout;

	while (test_bit(ST_TXBUSY, &portp->state)) {
		if (signal_pending(current))
			break;
		stli_delay(2);
		if (time_after_eq(jiffies, tend))
			break;
	}
}

/*****************************************************************************/

static void stli_sendxchar(struct tty_struct *tty, char ch)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	asyctrl_t	actrl;

#if DEBUG
	printk("stli_sendxchar(tty=%x,ch=%x)\n", (int) tty, ch);
#endif

	if (tty == (struct tty_struct *) NULL)
		return;
	portp = tty->driver_data;
	if (portp == (stliport_t *) NULL)
		return;
	if ((portp->brdnr < 0) || (portp->brdnr >= stli_nrbrds))
		return;
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return;

	memset(&actrl, 0, sizeof(asyctrl_t));
	if (ch == STOP_CHAR(tty)) {
		actrl.rxctrl = CT_STOPFLOW;
	} else if (ch == START_CHAR(tty)) {
		actrl.rxctrl = CT_STARTFLOW;
	} else {
		actrl.txctrl = CT_SENDCHR;
		actrl.tximdch = ch;
	}

	stli_cmdwait(brdp, portp, A_PORTCTRL, &actrl, sizeof(asyctrl_t), 0);
}

/*****************************************************************************/

#define	MAXLINE		80

/*
 *	Format info for a specified port. The line is deliberately limited
 *	to 80 characters. (If it is too long it will be truncated, if too
 *	short then padded with spaces).
 */

static int stli_portinfo(stlibrd_t *brdp, stliport_t *portp, int portnr, char *pos)
{
	char	*sp, *uart;
	int	rc, cnt;

	rc = stli_portcmdstats(portp);

	uart = "UNKNOWN";
	if (brdp->state & BST_STARTED) {
		switch (stli_comstats.hwid) {
		case 0:		uart = "2681"; break;
		case 1:		uart = "SC26198"; break;
		default:	uart = "CD1400"; break;
		}
	}

	sp = pos;
	sp += sprintf(sp, "%d: uart:%s ", portnr, uart);

	if ((brdp->state & BST_STARTED) && (rc >= 0)) {
		sp += sprintf(sp, "tx:%d rx:%d", (int) stli_comstats.txtotal,
			(int) stli_comstats.rxtotal);

		if (stli_comstats.rxframing)
			sp += sprintf(sp, " fe:%d",
				(int) stli_comstats.rxframing);
		if (stli_comstats.rxparity)
			sp += sprintf(sp, " pe:%d",
				(int) stli_comstats.rxparity);
		if (stli_comstats.rxbreaks)
			sp += sprintf(sp, " brk:%d",
				(int) stli_comstats.rxbreaks);
		if (stli_comstats.rxoverrun)
			sp += sprintf(sp, " oe:%d",
				(int) stli_comstats.rxoverrun);

		cnt = sprintf(sp, "%s%s%s%s%s ",
			(stli_comstats.signals & TIOCM_RTS) ? "|RTS" : "",
			(stli_comstats.signals & TIOCM_CTS) ? "|CTS" : "",
			(stli_comstats.signals & TIOCM_DTR) ? "|DTR" : "",
			(stli_comstats.signals & TIOCM_CD) ? "|DCD" : "",
			(stli_comstats.signals & TIOCM_DSR) ? "|DSR" : "");
		*sp = ' ';
		sp += cnt;
	}

	for (cnt = (sp - pos); (cnt < (MAXLINE - 1)); cnt++)
		*sp++ = ' ';
	if (cnt >= MAXLINE)
		pos[(MAXLINE - 2)] = '+';
	pos[(MAXLINE - 1)] = '\n';

	return(MAXLINE);
}

/*****************************************************************************/

/*
 *	Port info, read from the /proc file system.
 */

static int stli_readproc(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	stlibrd_t	*brdp;
	stliport_t	*portp;
	int		brdnr, portnr, totalport;
	int		curoff, maxoff;
	char		*pos;

#if DEBUG
	printk("stli_readproc(page=%x,start=%x,off=%x,count=%d,eof=%x,"
		"data=%x\n", (int) page, (int) start, (int) off, count,
		(int) eof, (int) data);
#endif

	pos = page;
	totalport = 0;
	curoff = 0;

	if (off == 0) {
		pos += sprintf(pos, "%s: version %s", stli_drvtitle,
			stli_drvversion);
		while (pos < (page + MAXLINE - 1))
			*pos++ = ' ';
		*pos++ = '\n';
	}
	curoff =  MAXLINE;

/*
 *	We scan through for each board, panel and port. The offset is
 *	calculated on the fly, and irrelevant ports are skipped.
 */
	for (brdnr = 0; (brdnr < stli_nrbrds); brdnr++) {
		brdp = stli_brds[brdnr];
		if (brdp == (stlibrd_t *) NULL)
			continue;
		if (brdp->state == 0)
			continue;

		maxoff = curoff + (brdp->nrports * MAXLINE);
		if (off >= maxoff) {
			curoff = maxoff;
			continue;
		}

		totalport = brdnr * STL_MAXPORTS;
		for (portnr = 0; (portnr < brdp->nrports); portnr++,
		    totalport++) {
			portp = brdp->ports[portnr];
			if (portp == (stliport_t *) NULL)
				continue;
			if (off >= (curoff += MAXLINE))
				continue;
			if ((pos - page + MAXLINE) > count)
				goto stli_readdone;
			pos += stli_portinfo(brdp, portp, totalport, pos);
		}
	}

	*eof = 1;

stli_readdone:
	*start = page;
	return(pos - page);
}

/*****************************************************************************/

/*
 *	Generic send command routine. This will send a message to the slave,
 *	of the specified type with the specified argument. Must be very
 *	careful of data that will be copied out from shared memory -
 *	containing command results. The command completion is all done from
 *	a poll routine that does not have user context. Therefore you cannot
 *	copy back directly into user space, or to the kernel stack of a
 *	process. This routine does not sleep, so can be called from anywhere.
 */

static void stli_sendcmd(stlibrd_t *brdp, stliport_t *portp, unsigned long cmd, void *arg, int size, int copyback)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkctrl_t	*cp;
	volatile unsigned char	*bits;
	unsigned long		flags;

#if DEBUG
	printk("stli_sendcmd(brdp=%x,portp=%x,cmd=%x,arg=%x,size=%d,"
		"copyback=%d)\n", (int) brdp, (int) portp, (int) cmd,
		(int) arg, size, copyback);
#endif

	save_flags(flags);
	cli();

	if (test_bit(ST_CMDING, &portp->state)) {
		printk("STALLION: command already busy, cmd=%x!\n", (int) cmd);
		restore_flags(flags);
		return;
	}

	EBRDENABLE(brdp);
	cp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->ctrl;
	if (size > 0) {
		memcpy((void *) &(cp->args[0]), arg, size);
		if (copyback) {
			portp->argp = arg;
			portp->argsize = size;
		}
	}
	cp->status = 0;
	cp->cmd = cmd;
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	bits = ((volatile unsigned char *) hdrp) + brdp->slaveoffset +
		portp->portidx;
	*bits |= portp->portbit;
	set_bit(ST_CMDING, &portp->state);
	EBRDDISABLE(brdp);
	restore_flags(flags);
}

/*****************************************************************************/

/*
 *	Read data from shared memory. This assumes that the shared memory
 *	is enabled and that interrupts are off. Basically we just empty out
 *	the shared memory buffer into the tty buffer. Must be careful to
 *	handle the case where we fill up the tty buffer, but still have
 *	more chars to unload.
 */

static inline void stli_read(stlibrd_t *brdp, stliport_t *portp)
{
	volatile cdkasyrq_t	*rp;
	volatile char		*shbuf;
	struct tty_struct	*tty;
	unsigned int		head, tail, size;
	unsigned int		len, stlen;

#if DEBUG
	printk("stli_read(brdp=%x,portp=%d)\n", (int) brdp, (int) portp);
#endif

	if (test_bit(ST_RXSTOP, &portp->state))
		return;
	tty = portp->tty;
	if (tty == (struct tty_struct *) NULL)
		return;

	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->rxq;
	head = (unsigned int) rp->head;
	if (head != ((unsigned int) rp->head))
		head = (unsigned int) rp->head;
	tail = (unsigned int) rp->tail;
	size = portp->rxsize;
	if (head >= tail) {
		len = head - tail;
		stlen = len;
	} else {
		len = size - (tail - head);
		stlen = size - tail;
	}

	len = MIN(len, (TTY_FLIPBUF_SIZE - tty->flip.count));
	shbuf = (volatile char *) EBRDGETMEMPTR(brdp, portp->rxoffset);

	while (len > 0) {
		stlen = MIN(len, stlen);
		memcpy(tty->flip.char_buf_ptr, (char *) (shbuf + tail), stlen);
		memset(tty->flip.flag_buf_ptr, 0, stlen);
		tty->flip.char_buf_ptr += stlen;
		tty->flip.flag_buf_ptr += stlen;
		tty->flip.count += stlen;

		len -= stlen;
		tail += stlen;
		if (tail >= size) {
			tail = 0;
			stlen = head;
		}
	}
	rp = &((volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr))->rxq;
	rp->tail = tail;

	if (head != tail)
		set_bit(ST_RXING, &portp->state);

	tty_schedule_flip(tty);
}

/*****************************************************************************/

/*
 *	Set up and carry out any delayed commands. There is only a small set
 *	of slave commands that can be done "off-level". So it is not too
 *	difficult to deal with them here.
 */

static inline void stli_dodelaycmd(stliport_t *portp, volatile cdkctrl_t *cp)
{
	int	cmd;

	if (test_bit(ST_DOSIGS, &portp->state)) {
		if (test_bit(ST_DOFLUSHTX, &portp->state) &&
		    test_bit(ST_DOFLUSHRX, &portp->state))
			cmd = A_SETSIGNALSF;
		else if (test_bit(ST_DOFLUSHTX, &portp->state))
			cmd = A_SETSIGNALSFTX;
		else if (test_bit(ST_DOFLUSHRX, &portp->state))
			cmd = A_SETSIGNALSFRX;
		else
			cmd = A_SETSIGNALS;
		clear_bit(ST_DOFLUSHTX, &portp->state);
		clear_bit(ST_DOFLUSHRX, &portp->state);
		clear_bit(ST_DOSIGS, &portp->state);
		memcpy((void *) &(cp->args[0]), (void *) &portp->asig,
			sizeof(asysigs_t));
		cp->status = 0;
		cp->cmd = cmd;
		set_bit(ST_CMDING, &portp->state);
	} else if (test_bit(ST_DOFLUSHTX, &portp->state) ||
	    test_bit(ST_DOFLUSHRX, &portp->state)) {
		cmd = ((test_bit(ST_DOFLUSHTX, &portp->state)) ? FLUSHTX : 0);
		cmd |= ((test_bit(ST_DOFLUSHRX, &portp->state)) ? FLUSHRX : 0);
		clear_bit(ST_DOFLUSHTX, &portp->state);
		clear_bit(ST_DOFLUSHRX, &portp->state);
		memcpy((void *) &(cp->args[0]), (void *) &cmd, sizeof(int));
		cp->status = 0;
		cp->cmd = A_FLUSH;
		set_bit(ST_CMDING, &portp->state);
	}
}

/*****************************************************************************/

/*
 *	Host command service checking. This handles commands or messages
 *	coming from the slave to the host. Must have board shared memory
 *	enabled and interrupts off when called. Notice that by servicing the
 *	read data last we don't need to change the shared memory pointer
 *	during processing (which is a slow IO operation).
 *	Return value indicates if this port is still awaiting actions from
 *	the slave (like open, command, or even TX data being sent). If 0
 *	then port is still busy, otherwise no longer busy.
 */

static inline int stli_hostcmd(stlibrd_t *brdp, stliport_t *portp)
{
	volatile cdkasy_t	*ap;
	volatile cdkctrl_t	*cp;
	struct tty_struct	*tty;
	asynotify_t		nt;
	unsigned long		oldsigs;
	int			rc, donerx;

#if DEBUG
	printk("stli_hostcmd(brdp=%x,channr=%d)\n", (int) brdp, channr);
#endif

	ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
	cp = &ap->ctrl;

/*
 *	Check if we are waiting for an open completion message.
 */
	if (test_bit(ST_OPENING, &portp->state)) {
		rc = (int) cp->openarg;
		if ((cp->open == 0) && (rc != 0)) {
			if (rc > 0)
				rc--;
			cp->openarg = 0;
			portp->rc = rc;
			clear_bit(ST_OPENING, &portp->state);
			wake_up_interruptible(&portp->raw_wait);
		}
	}

/*
 *	Check if we are waiting for a close completion message.
 */
	if (test_bit(ST_CLOSING, &portp->state)) {
		rc = (int) cp->closearg;
		if ((cp->close == 0) && (rc != 0)) {
			if (rc > 0)
				rc--;
			cp->closearg = 0;
			portp->rc = rc;
			clear_bit(ST_CLOSING, &portp->state);
			wake_up_interruptible(&portp->raw_wait);
		}
	}

/*
 *	Check if we are waiting for a command completion message. We may
 *	need to copy out the command results associated with this command.
 */
	if (test_bit(ST_CMDING, &portp->state)) {
		rc = cp->status;
		if ((cp->cmd == 0) && (rc != 0)) {
			if (rc > 0)
				rc--;
			if (portp->argp != (void *) NULL) {
				memcpy(portp->argp, (void *) &(cp->args[0]),
					portp->argsize);
				portp->argp = (void *) NULL;
			}
			cp->status = 0;
			portp->rc = rc;
			clear_bit(ST_CMDING, &portp->state);
			stli_dodelaycmd(portp, cp);
			wake_up_interruptible(&portp->raw_wait);
		}
	}

/*
 *	Check for any notification messages ready. This includes lots of
 *	different types of events - RX chars ready, RX break received,
 *	TX data low or empty in the slave, modem signals changed state.
 */
	donerx = 0;

	if (ap->notify) {
		nt = ap->changed;
		ap->notify = 0;
		tty = portp->tty;

		if (nt.signal & SG_DCD) {
			oldsigs = portp->sigs;
			portp->sigs = stli_mktiocm(nt.sigvalue);
			clear_bit(ST_GETSIGS, &portp->state);
			if ((portp->sigs & TIOCM_CD) &&
			    ((oldsigs & TIOCM_CD) == 0))
				wake_up_interruptible(&portp->open_wait);
			if ((oldsigs & TIOCM_CD) &&
			    ((portp->sigs & TIOCM_CD) == 0)) {
				if (portp->flags & ASYNC_CHECK_CD) {
					if (! ((portp->flags & ASYNC_CALLOUT_ACTIVE) &&
					    (portp->flags & ASYNC_CALLOUT_NOHUP))) {
						if (tty != (struct tty_struct *) NULL)
							queue_task(&portp->tqhangup, &tq_scheduler);
					}
				}
			}
		}

		if (nt.data & DT_TXEMPTY)
			clear_bit(ST_TXBUSY, &portp->state);
		if (nt.data & (DT_TXEMPTY | DT_TXLOW)) {
			if (tty != (struct tty_struct *) NULL) {
				if ((tty->flags & (1 << TTY_DO_WRITE_WAKEUP)) &&
				    tty->ldisc.write_wakeup) {
					(tty->ldisc.write_wakeup)(tty);
					EBRDENABLE(brdp);
				}
				wake_up_interruptible(&tty->write_wait);
			}
		}

		if ((nt.data & DT_RXBREAK) && (portp->rxmarkmsk & BRKINT)) {
			if (tty != (struct tty_struct *) NULL) {
				if (tty->flip.count < TTY_FLIPBUF_SIZE) {
					tty->flip.count++;
					*tty->flip.flag_buf_ptr++ = TTY_BREAK;
					*tty->flip.char_buf_ptr++ = 0;
					if (portp->flags & ASYNC_SAK) {
						do_SAK(tty);
						EBRDENABLE(brdp);
					}
					tty_schedule_flip(tty);
				}
			}
		}

		if (nt.data & DT_RXBUSY) {
			donerx++;
			stli_read(brdp, portp);
		}
	}

/*
 *	It might seem odd that we are checking for more RX chars here.
 *	But, we need to handle the case where the tty buffer was previously
 *	filled, but we had more characters to pass up. The slave will not
 *	send any more RX notify messages until the RX buffer has been emptied.
 *	But it will leave the service bits on (since the buffer is not empty).
 *	So from here we can try to process more RX chars.
 */
	if ((!donerx) && test_bit(ST_RXING, &portp->state)) {
		clear_bit(ST_RXING, &portp->state);
		stli_read(brdp, portp);
	}

	return((test_bit(ST_OPENING, &portp->state) ||
		test_bit(ST_CLOSING, &portp->state) ||
		test_bit(ST_CMDING, &portp->state) ||
		test_bit(ST_TXBUSY, &portp->state) ||
		test_bit(ST_RXING, &portp->state)) ? 0 : 1);
}

/*****************************************************************************/

/*
 *	Service all ports on a particular board. Assumes that the boards
 *	shared memory is enabled, and that the page pointer is pointed
 *	at the cdk header structure.
 */

static inline void stli_brdpoll(stlibrd_t *brdp, volatile cdkhdr_t *hdrp)
{
	stliport_t	*portp;
	unsigned char	hostbits[(STL_MAXCHANS / 8) + 1];
	unsigned char	slavebits[(STL_MAXCHANS / 8) + 1];
	unsigned char	*slavep;
	int		bitpos, bitat, bitsize;
	int 		channr, nrdevs, slavebitchange;

	bitsize = brdp->bitsize;
	nrdevs = brdp->nrdevs;

/*
 *	Check if slave wants any service. Basically we try to do as
 *	little work as possible here. There are 2 levels of service
 *	bits. So if there is nothing to do we bail early. We check
 *	8 service bits at a time in the inner loop, so we can bypass
 *	the lot if none of them want service.
 */
	memcpy(&hostbits[0], (((unsigned char *) hdrp) + brdp->hostoffset),
		bitsize);

	memset(&slavebits[0], 0, bitsize);
	slavebitchange = 0;

	for (bitpos = 0; (bitpos < bitsize); bitpos++) {
		if (hostbits[bitpos] == 0)
			continue;
		channr = bitpos * 8;
		for (bitat = 0x1; (channr < nrdevs); channr++, bitat <<= 1) {
			if (hostbits[bitpos] & bitat) {
				portp = brdp->ports[(channr - 1)];
				if (stli_hostcmd(brdp, portp)) {
					slavebitchange++;
					slavebits[bitpos] |= bitat;
				}
			}
		}
	}

/*
 *	If any of the ports are no longer busy then update them in the
 *	slave request bits. We need to do this after, since a host port
 *	service may initiate more slave requests.
 */
	if (slavebitchange) {
		hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
		slavep = ((unsigned char *) hdrp) + brdp->slaveoffset;
		for (bitpos = 0; (bitpos < bitsize); bitpos++) {
			if (slavebits[bitpos])
				slavep[bitpos] &= ~slavebits[bitpos];
		}
	}
}

/*****************************************************************************/

/*
 *	Driver poll routine. This routine polls the boards in use and passes
 *	messages back up to host when necessary. This is actually very
 *	CPU efficient, since we will always have the kernel poll clock, it
 *	adds only a few cycles when idle (since board service can be
 *	determined very easily), but when loaded generates no interrupts
 *	(with their expensive associated context change).
 */

static void stli_poll(unsigned long arg)
{
	volatile cdkhdr_t	*hdrp;
	stlibrd_t		*brdp;
	int 			brdnr;

	stli_timerlist.expires = STLI_TIMEOUT;
	add_timer(&stli_timerlist);

/*
 *	Check each board and do any servicing required.
 */
	for (brdnr = 0; (brdnr < stli_nrbrds); brdnr++) {
		brdp = stli_brds[brdnr];
		if (brdp == (stlibrd_t *) NULL)
			continue;
		if ((brdp->state & BST_STARTED) == 0)
			continue;

		EBRDENABLE(brdp);
		hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
		if (hdrp->hostreq)
			stli_brdpoll(brdp, hdrp);
		EBRDDISABLE(brdp);
	}
}

/*****************************************************************************/

/*
 *	Translate the termios settings into the port setting structure of
 *	the slave.
 */

static void stli_mkasyport(stliport_t *portp, asyport_t *pp, struct termios *tiosp)
{
#if DEBUG
	printk("stli_mkasyport(portp=%x,pp=%x,tiosp=%d)\n",
		(int) portp, (int) pp, (int) tiosp);
#endif

	memset(pp, 0, sizeof(asyport_t));

/*
 *	Start of by setting the baud, char size, parity and stop bit info.
 */
	pp->baudout = tiosp->c_cflag & CBAUD;
	if (pp->baudout & CBAUDEX) {
		pp->baudout &= ~CBAUDEX;
		if ((pp->baudout < 1) || (pp->baudout > 4))
			tiosp->c_cflag &= ~CBAUDEX;
		else
			pp->baudout += 15;
	}
	pp->baudout = stli_baudrates[pp->baudout];
	if ((tiosp->c_cflag & CBAUD) == B38400) {
		if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_HI)
			pp->baudout = 57600;
		else if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_VHI)
			pp->baudout = 115200;
		else if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_SHI)
			pp->baudout = 230400;
		else if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_WARP)
			pp->baudout = 460800;
		else if ((portp->flags & ASYNC_SPD_MASK) == ASYNC_SPD_CUST)
			pp->baudout = (portp->baud_base / portp->custom_divisor);
	}
	if (pp->baudout > STL_MAXBAUD)
		pp->baudout = STL_MAXBAUD;
	pp->baudin = pp->baudout;

	switch (tiosp->c_cflag & CSIZE) {
	case CS5:
		pp->csize = 5;
		break;
	case CS6:
		pp->csize = 6;
		break;
	case CS7:
		pp->csize = 7;
		break;
	default:
		pp->csize = 8;
		break;
	}

	if (tiosp->c_cflag & CSTOPB)
		pp->stopbs = PT_STOP2;
	else
		pp->stopbs = PT_STOP1;

	if (tiosp->c_cflag & PARENB) {
		if (tiosp->c_cflag & PARODD)
			pp->parity = PT_ODDPARITY;
		else
			pp->parity = PT_EVENPARITY;
	} else {
		pp->parity = PT_NOPARITY;
	}

/*
 *	Set up any flow control options enabled.
 */
	if (tiosp->c_iflag & IXON) {
		pp->flow |= F_IXON;
		if (tiosp->c_iflag & IXANY)
			pp->flow |= F_IXANY;
	}
	if (tiosp->c_cflag & CRTSCTS)
		pp->flow |= (F_RTSFLOW | F_CTSFLOW);

	pp->startin = tiosp->c_cc[VSTART];
	pp->stopin = tiosp->c_cc[VSTOP];
	pp->startout = tiosp->c_cc[VSTART];
	pp->stopout = tiosp->c_cc[VSTOP];

/*
 *	Set up the RX char marking mask with those RX error types we must
 *	catch. We can get the slave to help us out a little here, it will
 *	ignore parity errors and breaks for us, and mark parity errors in
 *	the data stream.
 */
	if (tiosp->c_iflag & IGNPAR)
		pp->iflag |= FI_IGNRXERRS;
	if (tiosp->c_iflag & IGNBRK)
		pp->iflag |= FI_IGNBREAK;

	portp->rxmarkmsk = 0;
	if (tiosp->c_iflag & (INPCK | PARMRK))
		pp->iflag |= FI_1MARKRXERRS;
	if (tiosp->c_iflag & BRKINT)
		portp->rxmarkmsk |= BRKINT;

/*
 *	Set up clocal processing as required.
 */
	if (tiosp->c_cflag & CLOCAL)
		portp->flags &= ~ASYNC_CHECK_CD;
	else
		portp->flags |= ASYNC_CHECK_CD;

/*
 *	Transfer any persistent flags into the asyport structure.
 */
	pp->pflag = (portp->pflag & 0xffff);
	pp->vmin = (portp->pflag & P_RXIMIN) ? 1 : 0;
	pp->vtime = (portp->pflag & P_RXITIME) ? 1 : 0;
	pp->cc[1] = (portp->pflag & P_RXTHOLD) ? 1 : 0;
}

/*****************************************************************************/

/*
 *	Construct a slave signals structure for setting the DTR and RTS
 *	signals as specified.
 */

static void stli_mkasysigs(asysigs_t *sp, int dtr, int rts)
{
#if DEBUG
	printk("stli_mkasysigs(sp=%x,dtr=%d,rts=%d)\n", (int) sp, dtr, rts);
#endif

	memset(sp, 0, sizeof(asysigs_t));
	if (dtr >= 0) {
		sp->signal |= SG_DTR;
		sp->sigvalue |= ((dtr > 0) ? SG_DTR : 0);
	}
	if (rts >= 0) {
		sp->signal |= SG_RTS;
		sp->sigvalue |= ((rts > 0) ? SG_RTS : 0);
	}
}

/*****************************************************************************/

/*
 *	Convert the signals returned from the slave into a local TIOCM type
 *	signals value. We keep them locally in TIOCM format.
 */

static long stli_mktiocm(unsigned long sigvalue)
{
	long	tiocm;

#if DEBUG
	printk("stli_mktiocm(sigvalue=%x)\n", (int) sigvalue);
#endif

	tiocm = 0;
	tiocm |= ((sigvalue & SG_DCD) ? TIOCM_CD : 0);
	tiocm |= ((sigvalue & SG_CTS) ? TIOCM_CTS : 0);
	tiocm |= ((sigvalue & SG_RI) ? TIOCM_RI : 0);
	tiocm |= ((sigvalue & SG_DSR) ? TIOCM_DSR : 0);
	tiocm |= ((sigvalue & SG_DTR) ? TIOCM_DTR : 0);
	tiocm |= ((sigvalue & SG_RTS) ? TIOCM_RTS : 0);
	return(tiocm);
}

/*****************************************************************************/

/*
 *	All panels and ports actually attached have been worked out. All
 *	we need to do here is set up the appropriate per port data structures.
 */

static inline int stli_initports(stlibrd_t *brdp)
{
	stliport_t	*portp;
	int		i, panelnr, panelport;

#if DEBUG
	printk("stli_initports(brdp=%x)\n", (int) brdp);
#endif

	for (i = 0, panelnr = 0, panelport = 0; (i < brdp->nrports); i++) {
		portp = (stliport_t *) stli_memalloc(sizeof(stliport_t));
		if (portp == (stliport_t *) NULL) {
			printk("STALLION: failed to allocate port structure\n");
			continue;
		}

		memset(portp, 0, sizeof(stliport_t));
		portp->magic = STLI_PORTMAGIC;
		portp->portnr = i;
		portp->brdnr = brdp->brdnr;
		portp->panelnr = panelnr;
		portp->baud_base = STL_BAUDBASE;
		portp->close_delay = STL_CLOSEDELAY;
		portp->closing_wait = 30 * HZ;
		portp->tqhangup.routine = stli_dohangup;
		portp->tqhangup.data = portp;
		init_waitqueue_head(&portp->open_wait);
		init_waitqueue_head(&portp->close_wait);
		init_waitqueue_head(&portp->raw_wait);
		portp->normaltermios = stli_deftermios;
		portp->callouttermios = stli_deftermios;
		panelport++;
		if (panelport >= brdp->panels[panelnr]) {
			panelport = 0;
			panelnr++;
		}
		brdp->ports[i] = portp;
	}

	return(0);
}

/*****************************************************************************/

/*
 *	All the following routines are board specific hardware operations.
 */

static void stli_ecpinit(stlibrd_t *brdp)
{
	unsigned long	memconf;

#if DEBUG
	printk("stli_ecpinit(brdp=%d)\n", (int) brdp);
#endif

	outb(ECP_ATSTOP, (brdp->iobase + ECP_ATCONFR));
	udelay(10);
	outb(ECP_ATDISABLE, (brdp->iobase + ECP_ATCONFR));
	udelay(100);

	memconf = (brdp->memaddr & ECP_ATADDRMASK) >> ECP_ATADDRSHFT;
	outb(memconf, (brdp->iobase + ECP_ATMEMAR));
}

/*****************************************************************************/

static void stli_ecpenable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpenable(brdp=%x)\n", (int) brdp);
#endif
	outb(ECP_ATENABLE, (brdp->iobase + ECP_ATCONFR));
}

/*****************************************************************************/

static void stli_ecpdisable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpdisable(brdp=%x)\n", (int) brdp);
#endif
	outb(ECP_ATDISABLE, (brdp->iobase + ECP_ATCONFR));
}

/*****************************************************************************/

static char *stli_ecpgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_ecpgetmemptr(brdp=%x,offset=%x)\n", (int) brdp,
		(int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_ATPAGESIZE);
		val = (unsigned char) (offset / ECP_ATPAGESIZE);
	}
	outb(val, (brdp->iobase + ECP_ATMEMPR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecpreset(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpreset(brdp=%x)\n", (int) brdp);
#endif

	outb(ECP_ATSTOP, (brdp->iobase + ECP_ATCONFR));
	udelay(10);
	outb(ECP_ATDISABLE, (brdp->iobase + ECP_ATCONFR));
	udelay(500);
}

/*****************************************************************************/

static void stli_ecpintr(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_ecpintr(brdp=%x)\n", (int) brdp);
#endif
	outb(0x1, brdp->iobase);
}

/*****************************************************************************/

/*
 *	The following set of functions act on ECP EISA boards.
 */

static void stli_ecpeiinit(stlibrd_t *brdp)
{
	unsigned long	memconf;

#if DEBUG
	printk("stli_ecpeiinit(brdp=%x)\n", (int) brdp);
#endif

	outb(0x1, (brdp->iobase + ECP_EIBRDENAB));
	outb(ECP_EISTOP, (brdp->iobase + ECP_EICONFR));
	udelay(10);
	outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
	udelay(500);

	memconf = (brdp->memaddr & ECP_EIADDRMASKL) >> ECP_EIADDRSHFTL;
	outb(memconf, (brdp->iobase + ECP_EIMEMARL));
	memconf = (brdp->memaddr & ECP_EIADDRMASKH) >> ECP_EIADDRSHFTH;
	outb(memconf, (brdp->iobase + ECP_EIMEMARH));
}

/*****************************************************************************/

static void stli_ecpeienable(stlibrd_t *brdp)
{	
	outb(ECP_EIENABLE, (brdp->iobase + ECP_EICONFR));
}

/*****************************************************************************/

static void stli_ecpeidisable(stlibrd_t *brdp)
{	
	outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
}

/*****************************************************************************/

static char *stli_ecpeigetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_ecpeigetmemptr(brdp=%x,offset=%x,line=%d)\n",
		(int) brdp, (int) offset, line);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_EIPAGESIZE);
		if (offset < ECP_EIPAGESIZE)
			val = ECP_EIENABLE;
		else
			val = ECP_EIENABLE | 0x40;
	}
	outb(val, (brdp->iobase + ECP_EICONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecpeireset(stlibrd_t *brdp)
{	
	outb(ECP_EISTOP, (brdp->iobase + ECP_EICONFR));
	udelay(10);
	outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
	udelay(500);
}

/*****************************************************************************/

/*
 *	The following set of functions act on ECP MCA boards.
 */

static void stli_ecpmcenable(stlibrd_t *brdp)
{	
	outb(ECP_MCENABLE, (brdp->iobase + ECP_MCCONFR));
}

/*****************************************************************************/

static void stli_ecpmcdisable(stlibrd_t *brdp)
{	
	outb(ECP_MCDISABLE, (brdp->iobase + ECP_MCCONFR));
}

/*****************************************************************************/

static char *stli_ecpmcgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_MCPAGESIZE);
		val = ((unsigned char) (offset / ECP_MCPAGESIZE)) | ECP_MCENABLE;
	}
	outb(val, (brdp->iobase + ECP_MCCONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecpmcreset(stlibrd_t *brdp)
{	
	outb(ECP_MCSTOP, (brdp->iobase + ECP_MCCONFR));
	udelay(10);
	outb(ECP_MCDISABLE, (brdp->iobase + ECP_MCCONFR));
	udelay(500);
}

/*****************************************************************************/

/*
 *	The following set of functions act on ECP PCI boards.
 */

static void stli_ecppciinit(stlibrd_t *brdp)
{
#if DEBUG
	printk("stli_ecppciinit(brdp=%x)\n", (int) brdp);
#endif

	outb(ECP_PCISTOP, (brdp->iobase + ECP_PCICONFR));
	udelay(10);
	outb(0, (brdp->iobase + ECP_PCICONFR));
	udelay(500);
}

/*****************************************************************************/

static char *stli_ecppcigetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_ecppcigetmemptr(brdp=%x,offset=%x,line=%d)\n",
		(int) brdp, (int) offset, line);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), board=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ECP_PCIPAGESIZE);
		val = (offset / ECP_PCIPAGESIZE) << 1;
	}
	outb(val, (brdp->iobase + ECP_PCICONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_ecppcireset(stlibrd_t *brdp)
{	
	outb(ECP_PCISTOP, (brdp->iobase + ECP_PCICONFR));
	udelay(10);
	outb(0, (brdp->iobase + ECP_PCICONFR));
	udelay(500);
}

/*****************************************************************************/

/*
 *	The following routines act on ONboards.
 */

static void stli_onbinit(stlibrd_t *brdp)
{
	unsigned long	memconf;

#if DEBUG
	printk("stli_onbinit(brdp=%d)\n", (int) brdp);
#endif

	outb(ONB_ATSTOP, (brdp->iobase + ONB_ATCONFR));
	udelay(10);
	outb(ONB_ATDISABLE, (brdp->iobase + ONB_ATCONFR));
	mdelay(1000);

	memconf = (brdp->memaddr & ONB_ATADDRMASK) >> ONB_ATADDRSHFT;
	outb(memconf, (brdp->iobase + ONB_ATMEMAR));
	outb(0x1, brdp->iobase);
	mdelay(1);
}

/*****************************************************************************/

static void stli_onbenable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbenable(brdp=%x)\n", (int) brdp);
#endif
	outb((brdp->enabval | ONB_ATENABLE), (brdp->iobase + ONB_ATCONFR));
}

/*****************************************************************************/

static void stli_onbdisable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbdisable(brdp=%x)\n", (int) brdp);
#endif
	outb((brdp->enabval | ONB_ATDISABLE), (brdp->iobase + ONB_ATCONFR));
}

/*****************************************************************************/

static char *stli_onbgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void	*ptr;

#if DEBUG
	printk("stli_onbgetmemptr(brdp=%x,offset=%x)\n", (int) brdp,
		(int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
	} else {
		ptr = brdp->membase + (offset % ONB_ATPAGESIZE);
	}
	return(ptr);
}

/*****************************************************************************/

static void stli_onbreset(stlibrd_t *brdp)
{	

#if DEBUG
	printk("stli_onbreset(brdp=%x)\n", (int) brdp);
#endif

	outb(ONB_ATSTOP, (brdp->iobase + ONB_ATCONFR));
	udelay(10);
	outb(ONB_ATDISABLE, (brdp->iobase + ONB_ATCONFR));
	mdelay(1000);
}

/*****************************************************************************/

/*
 *	The following routines act on ONboard EISA.
 */

static void stli_onbeinit(stlibrd_t *brdp)
{
	unsigned long	memconf;

#if DEBUG
	printk("stli_onbeinit(brdp=%d)\n", (int) brdp);
#endif

	outb(0x1, (brdp->iobase + ONB_EIBRDENAB));
	outb(ONB_EISTOP, (brdp->iobase + ONB_EICONFR));
	udelay(10);
	outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
	mdelay(1000);

	memconf = (brdp->memaddr & ONB_EIADDRMASKL) >> ONB_EIADDRSHFTL;
	outb(memconf, (brdp->iobase + ONB_EIMEMARL));
	memconf = (brdp->memaddr & ONB_EIADDRMASKH) >> ONB_EIADDRSHFTH;
	outb(memconf, (brdp->iobase + ONB_EIMEMARH));
	outb(0x1, brdp->iobase);
	mdelay(1);
}

/*****************************************************************************/

static void stli_onbeenable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbeenable(brdp=%x)\n", (int) brdp);
#endif
	outb(ONB_EIENABLE, (brdp->iobase + ONB_EICONFR));
}

/*****************************************************************************/

static void stli_onbedisable(stlibrd_t *brdp)
{	
#if DEBUG
	printk("stli_onbedisable(brdp=%x)\n", (int) brdp);
#endif
	outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
}

/*****************************************************************************/

static char *stli_onbegetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_onbegetmemptr(brdp=%x,offset=%x,line=%d)\n",
		(int) brdp, (int) offset, line);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % ONB_EIPAGESIZE);
		if (offset < ONB_EIPAGESIZE)
			val = ONB_EIENABLE;
		else
			val = ONB_EIENABLE | 0x40;
	}
	outb(val, (brdp->iobase + ONB_EICONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_onbereset(stlibrd_t *brdp)
{	

#if DEBUG
	printk("stli_onbereset(brdp=%x)\n", (int) brdp);
#endif

	outb(ONB_EISTOP, (brdp->iobase + ONB_EICONFR));
	udelay(10);
	outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
	mdelay(1000);
}

/*****************************************************************************/

/*
 *	The following routines act on Brumby boards.
 */

static void stli_bbyinit(stlibrd_t *brdp)
{

#if DEBUG
	printk("stli_bbyinit(brdp=%d)\n", (int) brdp);
#endif

	outb(BBY_ATSTOP, (brdp->iobase + BBY_ATCONFR));
	udelay(10);
	outb(0, (brdp->iobase + BBY_ATCONFR));
	mdelay(1000);
	outb(0x1, brdp->iobase);
	mdelay(1);
}

/*****************************************************************************/

static char *stli_bbygetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void		*ptr;
	unsigned char	val;

#if DEBUG
	printk("stli_bbygetmemptr(brdp=%x,offset=%x)\n", (int) brdp,
		(int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
		val = 0;
	} else {
		ptr = brdp->membase + (offset % BBY_PAGESIZE);
		val = (unsigned char) (offset / BBY_PAGESIZE);
	}
	outb(val, (brdp->iobase + BBY_ATCONFR));
	return(ptr);
}

/*****************************************************************************/

static void stli_bbyreset(stlibrd_t *brdp)
{	

#if DEBUG
	printk("stli_bbyreset(brdp=%x)\n", (int) brdp);
#endif

	outb(BBY_ATSTOP, (brdp->iobase + BBY_ATCONFR));
	udelay(10);
	outb(0, (brdp->iobase + BBY_ATCONFR));
	mdelay(1000);
}

/*****************************************************************************/

/*
 *	The following routines act on original old Stallion boards.
 */

static void stli_stalinit(stlibrd_t *brdp)
{

#if DEBUG
	printk("stli_stalinit(brdp=%d)\n", (int) brdp);
#endif

	outb(0x1, brdp->iobase);
	mdelay(1000);
}

/*****************************************************************************/

static char *stli_stalgetmemptr(stlibrd_t *brdp, unsigned long offset, int line)
{	
	void	*ptr;

#if DEBUG
	printk("stli_stalgetmemptr(brdp=%x,offset=%x)\n", (int) brdp,
		(int) offset);
#endif

	if (offset > brdp->memsize) {
		printk("STALLION: shared memory pointer=%x out of range at "
			"line=%d(%d), brd=%d\n", (int) offset, line,
			__LINE__, brdp->brdnr);
		ptr = 0;
	} else {
		ptr = brdp->membase + (offset % STAL_PAGESIZE);
	}
	return(ptr);
}

/*****************************************************************************/

static void stli_stalreset(stlibrd_t *brdp)
{	
	volatile unsigned long	*vecp;

#if DEBUG
	printk("stli_stalreset(brdp=%x)\n", (int) brdp);
#endif

	vecp = (volatile unsigned long *) (brdp->membase + 0x30);
	*vecp = 0xffff0000;
	outb(0, brdp->iobase);
	mdelay(1000);
}

/*****************************************************************************/

/*
 *	Try to find an ECP board and initialize it. This handles only ECP
 *	board types.
 */

static inline int stli_initecp(stlibrd_t *brdp)
{
	cdkecpsig_t	sig;
	cdkecpsig_t	*sigsp;
	unsigned int	status, nxtid;
	char		*name;
	int		panelnr, nrports;

#if DEBUG
	printk("stli_initecp(brdp=%x)\n", (int) brdp);
#endif

/*
 *	Do a basic sanity check on the IO and memory addresses.
 */
	if ((brdp->iobase == 0) || (brdp->memaddr == 0))
		return(-ENODEV);

	brdp->iosize = ECP_IOSIZE;
	if (check_region(brdp->iobase, brdp->iosize))
		printk("STALLION: Warning, board %d I/O address %x conflicts "
			"with another device\n", brdp->brdnr, brdp->iobase);

/*
 *	Based on the specific board type setup the common vars to access
 *	and enable shared memory. Set all board specific information now
 *	as well.
 */
	switch (brdp->brdtype) {
	case BRD_ECP:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = ECP_MEMSIZE;
		brdp->pagesize = ECP_ATPAGESIZE;
		brdp->init = stli_ecpinit;
		brdp->enable = stli_ecpenable;
		brdp->reenable = stli_ecpenable;
		brdp->disable = stli_ecpdisable;
		brdp->getmemptr = stli_ecpgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecpreset;
		name = "serial(EC8/64)";
		break;

	case BRD_ECPE:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = ECP_MEMSIZE;
		brdp->pagesize = ECP_EIPAGESIZE;
		brdp->init = stli_ecpeiinit;
		brdp->enable = stli_ecpeienable;
		brdp->reenable = stli_ecpeienable;
		brdp->disable = stli_ecpeidisable;
		brdp->getmemptr = stli_ecpeigetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecpeireset;
		name = "serial(EC8/64-EI)";
		break;

	case BRD_ECPMC:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = ECP_MEMSIZE;
		brdp->pagesize = ECP_MCPAGESIZE;
		brdp->init = NULL;
		brdp->enable = stli_ecpmcenable;
		brdp->reenable = stli_ecpmcenable;
		brdp->disable = stli_ecpmcdisable;
		brdp->getmemptr = stli_ecpmcgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecpmcreset;
		name = "serial(EC8/64-MCA)";
		break;

	case BRD_ECPPCI:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = ECP_PCIMEMSIZE;
		brdp->pagesize = ECP_PCIPAGESIZE;
		brdp->init = stli_ecppciinit;
		brdp->enable = NULL;
		brdp->reenable = NULL;
		brdp->disable = NULL;
		brdp->getmemptr = stli_ecppcigetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_ecppcireset;
		name = "serial(EC/RA-PCI)";
		break;

	default:
		return(-EINVAL);
	}

/*
 *	The per-board operations structure is all set up, so now let's go
 *	and get the board operational. Firstly initialize board configuration
 *	registers. Set the memory mapping info so we can get at the boards
 *	shared memory.
 */
	EBRDINIT(brdp);

	brdp->membase = ioremap(brdp->memaddr, brdp->memsize);
	if (brdp->membase == (void *) NULL)
		return(-ENOMEM);

/*
 *	Now that all specific code is set up, enable the shared memory and
 *	look for the a signature area that will tell us exactly what board
 *	this is, and what it is connected to it.
 */
	EBRDENABLE(brdp);
	sigsp = (cdkecpsig_t *) EBRDGETMEMPTR(brdp, CDK_SIGADDR);
	memcpy(&sig, sigsp, sizeof(cdkecpsig_t));
	EBRDDISABLE(brdp);

#if 0
	printk("%s(%d): sig-> magic=%x rom=%x panel=%x,%x,%x,%x,%x,%x,%x,%x\n",
		__FILE__, __LINE__, (int) sig.magic, sig.romver, sig.panelid[0],
		(int) sig.panelid[1], (int) sig.panelid[2],
		(int) sig.panelid[3], (int) sig.panelid[4],
		(int) sig.panelid[5], (int) sig.panelid[6],
		(int) sig.panelid[7]);
#endif

	if (sig.magic != ECP_MAGIC)
		return(-ENODEV);

/*
 *	Scan through the signature looking at the panels connected to the
 *	board. Calculate the total number of ports as we go.
 */
	for (panelnr = 0, nxtid = 0; (panelnr < STL_MAXPANELS); panelnr++) {
		status = sig.panelid[nxtid];
		if ((status & ECH_PNLIDMASK) != nxtid)
			break;

		brdp->panelids[panelnr] = status;
		nrports = (status & ECH_PNL16PORT) ? 16 : 8;
		if ((nrports == 16) && ((status & ECH_PNLXPID) == 0))
			nxtid++;
		brdp->panels[panelnr] = nrports;
		brdp->nrports += nrports;
		nxtid++;
		brdp->nrpanels++;
	}

	request_region(brdp->iobase, brdp->iosize, name);
	brdp->state |= BST_FOUND;
	return(0);
}

/*****************************************************************************/

/*
 *	Try to find an ONboard, Brumby or Stallion board and initialize it.
 *	This handles only these board types.
 */

static inline int stli_initonb(stlibrd_t *brdp)
{
	cdkonbsig_t	sig;
	cdkonbsig_t	*sigsp;
	char		*name;
	int		i;

#if DEBUG
	printk("stli_initonb(brdp=%x)\n", (int) brdp);
#endif

/*
 *	Do a basic sanity check on the IO and memory addresses.
 */
	if ((brdp->iobase == 0) || (brdp->memaddr == 0))
		return(-ENODEV);

	brdp->iosize = ONB_IOSIZE;
	if (check_region(brdp->iobase, brdp->iosize))
		printk("STALLION: Warning, board %d I/O address %x conflicts "
			"with another device\n", brdp->brdnr, brdp->iobase);

/*
 *	Based on the specific board type setup the common vars to access
 *	and enable shared memory. Set all board specific information now
 *	as well.
 */
	switch (brdp->brdtype) {
	case BRD_ONBOARD:
	case BRD_ONBOARD32:
	case BRD_ONBOARD2:
	case BRD_ONBOARD2_32:
	case BRD_ONBOARDRS:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = ONB_MEMSIZE;
		brdp->pagesize = ONB_ATPAGESIZE;
		brdp->init = stli_onbinit;
		brdp->enable = stli_onbenable;
		brdp->reenable = stli_onbenable;
		brdp->disable = stli_onbdisable;
		brdp->getmemptr = stli_onbgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_onbreset;
		if (brdp->memaddr > 0x100000)
			brdp->enabval = ONB_MEMENABHI;
		else
			brdp->enabval = ONB_MEMENABLO;
		name = "serial(ONBoard)";
		break;

	case BRD_ONBOARDE:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = ONB_EIMEMSIZE;
		brdp->pagesize = ONB_EIPAGESIZE;
		brdp->init = stli_onbeinit;
		brdp->enable = stli_onbeenable;
		brdp->reenable = stli_onbeenable;
		brdp->disable = stli_onbedisable;
		brdp->getmemptr = stli_onbegetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_onbereset;
		name = "serial(ONBoard/E)";
		break;

	case BRD_BRUMBY4:
	case BRD_BRUMBY8:
	case BRD_BRUMBY16:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = BBY_MEMSIZE;
		brdp->pagesize = BBY_PAGESIZE;
		brdp->init = stli_bbyinit;
		brdp->enable = NULL;
		brdp->reenable = NULL;
		brdp->disable = NULL;
		brdp->getmemptr = stli_bbygetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_bbyreset;
		name = "serial(Brumby)";
		break;

	case BRD_STALLION:
		brdp->membase = (void *) brdp->memaddr;
		brdp->memsize = STAL_MEMSIZE;
		brdp->pagesize = STAL_PAGESIZE;
		brdp->init = stli_stalinit;
		brdp->enable = NULL;
		brdp->reenable = NULL;
		brdp->disable = NULL;
		brdp->getmemptr = stli_stalgetmemptr;
		brdp->intr = stli_ecpintr;
		brdp->reset = stli_stalreset;
		name = "serial(Stallion)";
		break;

	default:
		return(-EINVAL);
	}

/*
 *	The per-board operations structure is all set up, so now let's go
 *	and get the board operational. Firstly initialize board configuration
 *	registers. Set the memory mapping info so we can get at the boards
 *	shared memory.
 */
	EBRDINIT(brdp);

	brdp->membase = ioremap(brdp->memaddr, brdp->memsize);
	if (brdp->membase == (void *) NULL)
		return(-ENOMEM);

/*
 *	Now that all specific code is set up, enable the shared memory and
 *	look for the a signature area that will tell us exactly what board
 *	this is, and how many ports.
 */
	EBRDENABLE(brdp);
	sigsp = (cdkonbsig_t *) EBRDGETMEMPTR(brdp, CDK_SIGADDR);
	memcpy(&sig, sigsp, sizeof(cdkonbsig_t));
	EBRDDISABLE(brdp);

#if 0
	printk("%s(%d): sig-> magic=%x:%x:%x:%x romver=%x amask=%x:%x:%x\n",
		__FILE__, __LINE__, sig.magic0, sig.magic1, sig.magic2,
		sig.magic3, sig.romver, sig.amask0, sig.amask1, sig.amask2);
#endif

	if ((sig.magic0 != ONB_MAGIC0) || (sig.magic1 != ONB_MAGIC1) ||
	    (sig.magic2 != ONB_MAGIC2) || (sig.magic3 != ONB_MAGIC3))
		return(-ENODEV);

/*
 *	Scan through the signature alive mask and calculate how many ports
 *	there are on this board.
 */
	brdp->nrpanels = 1;
	if (sig.amask1) {
		brdp->nrports = 32;
	} else {
		for (i = 0; (i < 16); i++) {
			if (((sig.amask0 << i) & 0x8000) == 0)
				break;
		}
		brdp->nrports = i;
	}
	brdp->panels[0] = brdp->nrports;

	request_region(brdp->iobase, brdp->iosize, name);
	brdp->state |= BST_FOUND;
	return(0);
}

/*****************************************************************************/

/*
 *	Start up a running board. This routine is only called after the
 *	code has been down loaded to the board and is operational. It will
 *	read in the memory map, and get the show on the road...
 */

static int stli_startbrd(stlibrd_t *brdp)
{
	volatile cdkhdr_t	*hdrp;
	volatile cdkmem_t	*memp;
	volatile cdkasy_t	*ap;
	unsigned long		flags;
	stliport_t		*portp;
	int			portnr, nrdevs, i, rc;

#if DEBUG
	printk("stli_startbrd(brdp=%x)\n", (int) brdp);
#endif

	rc = 0;

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	hdrp = (volatile cdkhdr_t *) EBRDGETMEMPTR(brdp, CDK_CDKADDR);
	nrdevs = hdrp->nrdevs;

#if 0
	printk("%s(%d): CDK version %d.%d.%d --> "
		"nrdevs=%d memp=%x hostp=%x slavep=%x\n",
		 __FILE__, __LINE__, hdrp->ver_release, hdrp->ver_modification,
		 hdrp->ver_fix, nrdevs, (int) hdrp->memp, (int) hdrp->hostp,
		 (int) hdrp->slavep);
#endif

	if (nrdevs < (brdp->nrports + 1)) {
		printk("STALLION: slave failed to allocate memory for all "
			"devices, devices=%d\n", nrdevs);
		brdp->nrports = nrdevs - 1;
	}
	brdp->nrdevs = nrdevs;
	brdp->hostoffset = hdrp->hostp - CDK_CDKADDR;
	brdp->slaveoffset = hdrp->slavep - CDK_CDKADDR;
	brdp->bitsize = (nrdevs + 7) / 8;
	memp = (volatile cdkmem_t *) hdrp->memp;
	if (((unsigned long) memp) > brdp->memsize) {
		printk("STALLION: corrupted shared memory region?\n");
		rc = -EIO;
		goto stli_donestartup;
	}
	memp = (volatile cdkmem_t *) EBRDGETMEMPTR(brdp, (unsigned long) memp);
	if (memp->dtype != TYP_ASYNCTRL) {
		printk("STALLION: no slave control device found\n");
		goto stli_donestartup;
	}
	memp++;

/*
 *	Cycle through memory allocation of each port. We are guaranteed to
 *	have all ports inside the first page of slave window, so no need to
 *	change pages while reading memory map.
 */
	for (i = 1, portnr = 0; (i < nrdevs); i++, portnr++, memp++) {
		if (memp->dtype != TYP_ASYNC)
			break;
		portp = brdp->ports[portnr];
		if (portp == (stliport_t *) NULL)
			break;
		portp->devnr = i;
		portp->addr = memp->offset;
		portp->reqbit = (unsigned char) (0x1 << (i * 8 / nrdevs));
		portp->portidx = (unsigned char) (i / 8);
		portp->portbit = (unsigned char) (0x1 << (i % 8));
	}

	hdrp->slavereq = 0xff;

/*
 *	For each port setup a local copy of the RX and TX buffer offsets
 *	and sizes. We do this separate from the above, because we need to
 *	move the shared memory page...
 */
	for (i = 1, portnr = 0; (i < nrdevs); i++, portnr++) {
		portp = brdp->ports[portnr];
		if (portp == (stliport_t *) NULL)
			break;
		if (portp->addr == 0)
			break;
		ap = (volatile cdkasy_t *) EBRDGETMEMPTR(brdp, portp->addr);
		if (ap != (volatile cdkasy_t *) NULL) {
			portp->rxsize = ap->rxq.size;
			portp->txsize = ap->txq.size;
			portp->rxoffset = ap->rxq.offset;
			portp->txoffset = ap->txq.offset;
		}
	}

stli_donestartup:
	EBRDDISABLE(brdp);
	restore_flags(flags);

	if (rc == 0)
		brdp->state |= BST_STARTED;

	if (! stli_timeron) {
		stli_timeron++;
		stli_timerlist.expires = STLI_TIMEOUT;
		add_timer(&stli_timerlist);
	}

	return(rc);
}

/*****************************************************************************/

/*
 *	Probe and initialize the specified board.
 */

static int __init stli_brdinit(stlibrd_t *brdp)
{
#if DEBUG
	printk("stli_brdinit(brdp=%x)\n", (int) brdp);
#endif

	stli_brds[brdp->brdnr] = brdp;

	switch (brdp->brdtype) {
	case BRD_ECP:
	case BRD_ECPE:
	case BRD_ECPMC:
	case BRD_ECPPCI:
		stli_initecp(brdp);
		break;
	case BRD_ONBOARD:
	case BRD_ONBOARDE:
	case BRD_ONBOARD2:
	case BRD_ONBOARD32:
	case BRD_ONBOARD2_32:
	case BRD_ONBOARDRS:
	case BRD_BRUMBY4:
	case BRD_BRUMBY8:
	case BRD_BRUMBY16:
	case BRD_STALLION:
		stli_initonb(brdp);
		break;
	case BRD_EASYIO:
	case BRD_ECH:
	case BRD_ECHMC:
	case BRD_ECHPCI:
		printk("STALLION: %s board type not supported in this driver\n",
			stli_brdnames[brdp->brdtype]);
		return(ENODEV);
	default:
		printk("STALLION: board=%d is unknown board type=%d\n",
			brdp->brdnr, brdp->brdtype);
		return(ENODEV);
	}

	if ((brdp->state & BST_FOUND) == 0) {
		printk("STALLION: %s board not found, board=%d io=%x mem=%x\n",
			stli_brdnames[brdp->brdtype], brdp->brdnr,
			brdp->iobase, (int) brdp->memaddr);
		return(ENODEV);
	}

	stli_initports(brdp);
	printk("STALLION: %s found, board=%d io=%x mem=%x "
		"nrpanels=%d nrports=%d\n", stli_brdnames[brdp->brdtype],
		brdp->brdnr, brdp->iobase, (int) brdp->memaddr,
		brdp->nrpanels, brdp->nrports);
	return(0);
}

/*****************************************************************************/

/*
 *	Probe around trying to find where the EISA boards shared memory
 *	might be. This is a bit if hack, but it is the best we can do.
 */

static inline int stli_eisamemprobe(stlibrd_t *brdp)
{
	cdkecpsig_t	ecpsig, *ecpsigp;
	cdkonbsig_t	onbsig, *onbsigp;
	int		i, foundit;

#if DEBUG
	printk("stli_eisamemprobe(brdp=%x)\n", (int) brdp);
#endif

/*
 *	First up we reset the board, to get it into a known state. There
 *	is only 2 board types here we need to worry about. Don;t use the
 *	standard board init routine here, it programs up the shared
 *	memory address, and we don't know it yet...
 */
	if (brdp->brdtype == BRD_ECPE) {
		outb(0x1, (brdp->iobase + ECP_EIBRDENAB));
		outb(ECP_EISTOP, (brdp->iobase + ECP_EICONFR));
		udelay(10);
		outb(ECP_EIDISABLE, (brdp->iobase + ECP_EICONFR));
		udelay(500);
		stli_ecpeienable(brdp);
	} else if (brdp->brdtype == BRD_ONBOARDE) {
		outb(0x1, (brdp->iobase + ONB_EIBRDENAB));
		outb(ONB_EISTOP, (brdp->iobase + ONB_EICONFR));
		udelay(10);
		outb(ONB_EIDISABLE, (brdp->iobase + ONB_EICONFR));
		mdelay(100);
		outb(0x1, brdp->iobase);
		mdelay(1);
		stli_onbeenable(brdp);
	} else {
		return(-ENODEV);
	}

	foundit = 0;
	brdp->memsize = ECP_MEMSIZE;

/*
 *	Board shared memory is enabled, so now we have a poke around and
 *	see if we can find it.
 */
	for (i = 0; (i < stli_eisamempsize); i++) {
		brdp->memaddr = stli_eisamemprobeaddrs[i];
		brdp->membase = (void *) brdp->memaddr;
		brdp->membase = ioremap(brdp->memaddr, brdp->memsize);
		if (brdp->membase == (void *) NULL)
			continue;

		if (brdp->brdtype == BRD_ECPE) {
			ecpsigp = (cdkecpsig_t *) stli_ecpeigetmemptr(brdp,
				CDK_SIGADDR, __LINE__);
			memcpy(&ecpsig, ecpsigp, sizeof(cdkecpsig_t));
			if (ecpsig.magic == ECP_MAGIC)
				foundit = 1;
		} else {
			onbsigp = (cdkonbsig_t *) stli_onbegetmemptr(brdp,
				CDK_SIGADDR, __LINE__);
			memcpy(&onbsig, onbsigp, sizeof(cdkonbsig_t));
			if ((onbsig.magic0 == ONB_MAGIC0) &&
			    (onbsig.magic1 == ONB_MAGIC1) &&
			    (onbsig.magic2 == ONB_MAGIC2) &&
			    (onbsig.magic3 == ONB_MAGIC3))
				foundit = 1;
		}

		iounmap(brdp->membase);
		if (foundit)
			break;
	}

/*
 *	Regardless of whether we found the shared memory or not we must
 *	disable the region. After that return success or failure.
 */
	if (brdp->brdtype == BRD_ECPE)
		stli_ecpeidisable(brdp);
	else
		stli_onbedisable(brdp);

	if (! foundit) {
		brdp->memaddr = 0;
		brdp->membase = 0;
		printk("STALLION: failed to probe shared memory region for "
			"%s in EISA slot=%d\n", stli_brdnames[brdp->brdtype],
			(brdp->iobase >> 12));
		return(-ENODEV);
	}
	return(0);
}

/*****************************************************************************/

/*
 *	Probe around and try to find any EISA boards in system. The biggest
 *	problem here is finding out what memory address is associated with
 *	an EISA board after it is found. The registers of the ECPE and
 *	ONboardE are not readable - so we can't read them from there. We
 *	don't have access to the EISA CMOS (or EISA BIOS) so we don't
 *	actually have any way to find out the real value. The best we can
 *	do is go probing around in the usual places hoping we can find it.
 */

static inline int stli_findeisabrds()
{
	stlibrd_t	*brdp;
	unsigned int	iobase, eid;
	int		i;

#if DEBUG
	printk("stli_findeisabrds()\n");
#endif

/*
 *	Firstly check if this is an EISA system. Do this by probing for
 *	the system board EISA ID. If this is not an EISA system then
 *	don't bother going any further!
 */
	outb(0xff, 0xc80);
	if (inb(0xc80) == 0xff)
		return(0);

/*
 *	Looks like an EISA system, so go searching for EISA boards.
 */
	for (iobase = 0x1000; (iobase <= 0xc000); iobase += 0x1000) {
		outb(0xff, (iobase + 0xc80));
		eid = inb(iobase + 0xc80);
		eid |= inb(iobase + 0xc81) << 8;
		if (eid != STL_EISAID)
			continue;

/*
 *		We have found a board. Need to check if this board was
 *		statically configured already (just in case!).
 */
		for (i = 0; (i < STL_MAXBRDS); i++) {
			brdp = stli_brds[i];
			if (brdp == (stlibrd_t *) NULL)
				continue;
			if (brdp->iobase == iobase)
				break;
		}
		if (i < STL_MAXBRDS)
			continue;

/*
 *		We have found a Stallion board and it is not configured already.
 *		Allocate a board structure and initialize it.
 */
		if ((brdp = stli_allocbrd()) == (stlibrd_t *) NULL)
			return(-ENOMEM);
		if ((brdp->brdnr = stli_getbrdnr()) < 0)
			return(-ENOMEM);
		eid = inb(iobase + 0xc82);
		if (eid == ECP_EISAID)
			brdp->brdtype = BRD_ECPE;
		else if (eid == ONB_EISAID)
			brdp->brdtype = BRD_ONBOARDE;
		else
			brdp->brdtype = BRD_UNKNOWN;
		brdp->iobase = iobase;
		outb(0x1, (iobase + 0xc84));
		if (stli_eisamemprobe(brdp))
			outb(0, (iobase + 0xc84));
		stli_brdinit(brdp);
	}

	return(0);
}

/*****************************************************************************/

/*
 *	Find the next available board number that is free.
 */

static inline int stli_getbrdnr()
{
	int	i;

	for (i = 0; (i < STL_MAXBRDS); i++) {
		if (stli_brds[i] == (stlibrd_t *) NULL) {
			if (i >= stli_nrbrds)
				stli_nrbrds = i + 1;
			return(i);
		}
	}
	return(-1);
}

/*****************************************************************************/

#ifdef	CONFIG_PCI

/*
 *	We have a Stallion board. Allocate a board structure and
 *	initialize it. Read its IO and MEMORY resources from PCI
 *	configuration space.
 */

static inline int stli_initpcibrd(int brdtype, struct pci_dev *devp)
{
	stlibrd_t	*brdp;

#if DEBUG
	printk("stli_initpcibrd(brdtype=%d,busnr=%x,devnr=%x)\n", brdtype,
		dev->bus->number, dev->devfn);
#endif

	if ((brdp = stli_allocbrd()) == (stlibrd_t *) NULL)
		return(-ENOMEM);
	if ((brdp->brdnr = stli_getbrdnr()) < 0) {
		printk("STALLION: too many boards found, "
			"maximum supported %d\n", STL_MAXBRDS);
		return(0);
	}
	brdp->brdtype = brdtype;

#if DEBUG
	printk("%s(%d): BAR[]=%x,%x,%x,%x\n", __FILE__, __LINE__,
		devp->resource[0].start, devp->resource[1].start,
		devp->resource[2].start, devp->resource[3].start);
#endif

/*
 *	We have all resources from the board, so lets setup the actual
 *	board structure now.
 */
	brdp->iobase = (devp->resource[3].start & PCI_BASE_ADDRESS_IO_MASK);
	brdp->memaddr = (devp->resource[2].start & PCI_BASE_ADDRESS_MEM_MASK);
	stli_brdinit(brdp);

	return(0);
}

/*****************************************************************************/

/*
 *	Find all Stallion PCI boards that might be installed. Initialize each
 *	one as it is found.
 */

static inline int stli_findpcibrds()
{
	struct pci_dev	*dev = NULL;
	int		rc;

#if DEBUG
	printk("stli_findpcibrds()\n");
#endif

	if (! pci_present())
		return(0);

	while ((dev = pci_find_device(PCI_VENDOR_ID_STALLION,
	    PCI_DEVICE_ID_ECRA, dev))) {
		if ((rc = stli_initpcibrd(BRD_ECPPCI, dev)))
			return(rc);
	}

	return(0);
}

#endif

/*****************************************************************************/

/*
 *	Allocate a new board structure. Fill out the basic info in it.
 */

static stlibrd_t *stli_allocbrd()
{
	stlibrd_t	*brdp;

	brdp = (stlibrd_t *) stli_memalloc(sizeof(stlibrd_t));
	if (brdp == (stlibrd_t *) NULL) {
		printk("STALLION: failed to allocate memory (size=%d)\n",
			sizeof(stlibrd_t));
		return((stlibrd_t *) NULL);
	}

	memset(brdp, 0, sizeof(stlibrd_t));
	brdp->magic = STLI_BOARDMAGIC;
	return(brdp);
}

/*****************************************************************************/

/*
 *	Scan through all the boards in the configuration and see what we
 *	can find.
 */

static inline int stli_initbrds()
{
	stlibrd_t	*brdp, *nxtbrdp;
	stlconf_t	*confp;
	int		i, j;

#if DEBUG
	printk("stli_initbrds()\n");
#endif

	if (stli_nrbrds > STL_MAXBRDS) {
		printk("STALLION: too many boards in configuration table, "
			"truncating to %d\n", STL_MAXBRDS);
		stli_nrbrds = STL_MAXBRDS;
	}

/*
 *	Firstly scan the list of static boards configured. Allocate
 *	resources and initialize the boards as found. If this is a
 *	module then let the module args override static configuration.
 */
	for (i = 0; (i < stli_nrbrds); i++) {
		confp = &stli_brdconf[i];
#ifdef MODULE
		stli_parsebrd(confp, stli_brdsp[i]);
#endif
		if ((brdp = stli_allocbrd()) == (stlibrd_t *) NULL)
			return(-ENOMEM);
		brdp->brdnr = i;
		brdp->brdtype = confp->brdtype;
		brdp->iobase = confp->ioaddr1;
		brdp->memaddr = confp->memaddr;
		stli_brdinit(brdp);
	}

/*
 *	Static configuration table done, so now use dynamic methods to
 *	see if any more boards should be configured.
 */
#ifdef MODULE
	stli_argbrds();
#endif
	if (stli_eisaprobe)
		stli_findeisabrds();
#ifdef CONFIG_PCI
	stli_findpcibrds();
#endif

/*
 *	All found boards are initialized. Now for a little optimization, if
 *	no boards are sharing the "shared memory" regions then we can just
 *	leave them all enabled. This is in fact the usual case.
 */
	stli_shared = 0;
	if (stli_nrbrds > 1) {
		for (i = 0; (i < stli_nrbrds); i++) {
			brdp = stli_brds[i];
			if (brdp == (stlibrd_t *) NULL)
				continue;
			for (j = i + 1; (j < stli_nrbrds); j++) {
				nxtbrdp = stli_brds[j];
				if (nxtbrdp == (stlibrd_t *) NULL)
					continue;
				if ((brdp->membase >= nxtbrdp->membase) &&
				    (brdp->membase <= (nxtbrdp->membase +
				    nxtbrdp->memsize - 1))) {
					stli_shared++;
					break;
				}
			}
		}
	}

	if (stli_shared == 0) {
		for (i = 0; (i < stli_nrbrds); i++) {
			brdp = stli_brds[i];
			if (brdp == (stlibrd_t *) NULL)
				continue;
			if (brdp->state & BST_FOUND) {
				EBRDENABLE(brdp);
				brdp->enable = NULL;
				brdp->disable = NULL;
			}
		}
	}

	return(0);
}

/*****************************************************************************/

/*
 *	Code to handle an "staliomem" read operation. This device is the 
 *	contents of the board shared memory. It is used for down loading
 *	the slave image (and debugging :-)
 */

static ssize_t stli_memread(struct file *fp, char *buf, size_t count, loff_t *offp)
{
	unsigned long	flags;
	void		*memptr;
	stlibrd_t	*brdp;
	int		brdnr, size, n;

#if DEBUG
	printk("stli_memread(fp=%x,buf=%x,count=%x,offp=%x)\n", (int) fp,
		(int) buf, count, (int) offp);
#endif

	brdnr = MINOR(fp->f_dentry->d_inode->i_rdev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	brdp = stli_brds[brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);
	if (brdp->state == 0)
		return(-ENODEV);
	if (fp->f_pos >= brdp->memsize)
		return(0);

	size = MIN(count, (brdp->memsize - fp->f_pos));

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	while (size > 0) {
		memptr = (void *) EBRDGETMEMPTR(brdp, fp->f_pos);
		n = MIN(size, (brdp->pagesize - (((unsigned long) fp->f_pos) % brdp->pagesize)));
		copy_to_user(buf, memptr, n);
		fp->f_pos += n;
		buf += n;
		size -= n;
	}
	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(count);
}

/*****************************************************************************/

/*
 *	Code to handle an "staliomem" write operation. This device is the 
 *	contents of the board shared memory. It is used for down loading
 *	the slave image (and debugging :-)
 */

static ssize_t stli_memwrite(struct file *fp, const char *buf, size_t count, loff_t *offp)
{
	unsigned long	flags;
	void		*memptr;
	stlibrd_t	*brdp;
	char		*chbuf;
	int		brdnr, size, n;

#if DEBUG
	printk("stli_memwrite(fp=%x,buf=%x,count=%x,offp=%x)\n", (int) fp,
		(int) buf, count, (int) offp);
#endif

	brdnr = MINOR(fp->f_dentry->d_inode->i_rdev);
	if (brdnr >= stli_nrbrds)
		return(-ENODEV);
	brdp = stli_brds[brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);
	if (brdp->state == 0)
		return(-ENODEV);
	if (fp->f_pos >= brdp->memsize)
		return(0);

	chbuf = (char *) buf;
	size = MIN(count, (brdp->memsize - fp->f_pos));

	save_flags(flags);
	cli();
	EBRDENABLE(brdp);
	while (size > 0) {
		memptr = (void *) EBRDGETMEMPTR(brdp, fp->f_pos);
		n = MIN(size, (brdp->pagesize - (((unsigned long) fp->f_pos) % brdp->pagesize)));
		copy_from_user(memptr, chbuf, n);
		fp->f_pos += n;
		chbuf += n;
		size -= n;
	}
	EBRDDISABLE(brdp);
	restore_flags(flags);

	return(count);
}

/*****************************************************************************/

/*
 *	Return the board stats structure to user app.
 */

static int stli_getbrdstats(combrd_t *bp)
{
	stlibrd_t	*brdp;
	int		i;

	copy_from_user(&stli_brdstats, bp, sizeof(combrd_t));
	if (stli_brdstats.brd >= STL_MAXBRDS)
		return(-ENODEV);
	brdp = stli_brds[stli_brdstats.brd];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);

	memset(&stli_brdstats, 0, sizeof(combrd_t));
	stli_brdstats.brd = brdp->brdnr;
	stli_brdstats.type = brdp->brdtype;
	stli_brdstats.hwid = 0;
	stli_brdstats.state = brdp->state;
	stli_brdstats.ioaddr = brdp->iobase;
	stli_brdstats.memaddr = brdp->memaddr;
	stli_brdstats.nrpanels = brdp->nrpanels;
	stli_brdstats.nrports = brdp->nrports;
	for (i = 0; (i < brdp->nrpanels); i++) {
		stli_brdstats.panels[i].panel = i;
		stli_brdstats.panels[i].hwid = brdp->panelids[i];
		stli_brdstats.panels[i].nrports = brdp->panels[i];
	}

	copy_to_user(bp, &stli_brdstats, sizeof(combrd_t));
	return(0);
}

/*****************************************************************************/

/*
 *	Resolve the referenced port number into a port struct pointer.
 */

static stliport_t *stli_getport(int brdnr, int panelnr, int portnr)
{
	stlibrd_t	*brdp;
	int		i;

	if ((brdnr < 0) || (brdnr >= STL_MAXBRDS))
		return((stliport_t *) NULL);
	brdp = stli_brds[brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return((stliport_t *) NULL);
	for (i = 0; (i < panelnr); i++)
		portnr += brdp->panels[i];
	if ((portnr < 0) || (portnr >= brdp->nrports))
		return((stliport_t *) NULL);
	return(brdp->ports[portnr]);
}

/*****************************************************************************/

/*
 *	Return the port stats structure to user app. A NULL port struct
 *	pointer passed in means that we need to find out from the app
 *	what port to get stats for (used through board control device).
 */

static int stli_portcmdstats(stliport_t *portp)
{
	unsigned long	flags;
	stlibrd_t	*brdp;
	int		rc;

	memset(&stli_comstats, 0, sizeof(comstats_t));

	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);

	if (brdp->state & BST_STARTED) {
		if ((rc = stli_cmdwait(brdp, portp, A_GETSTATS,
		    &stli_cdkstats, sizeof(asystats_t), 1)) < 0)
			return(rc);
	} else {
		memset(&stli_cdkstats, 0, sizeof(asystats_t));
	}

	stli_comstats.brd = portp->brdnr;
	stli_comstats.panel = portp->panelnr;
	stli_comstats.port = portp->portnr;
	stli_comstats.state = portp->state;
	stli_comstats.flags = portp->flags;

	save_flags(flags);
	cli();
	if (portp->tty != (struct tty_struct *) NULL) {
		if (portp->tty->driver_data == portp) {
			stli_comstats.ttystate = portp->tty->flags;
			stli_comstats.rxbuffered = portp->tty->flip.count;
			if (portp->tty->termios != (struct termios *) NULL) {
				stli_comstats.cflags = portp->tty->termios->c_cflag;
				stli_comstats.iflags = portp->tty->termios->c_iflag;
				stli_comstats.oflags = portp->tty->termios->c_oflag;
				stli_comstats.lflags = portp->tty->termios->c_lflag;
			}
		}
	}
	restore_flags(flags);

	stli_comstats.txtotal = stli_cdkstats.txchars;
	stli_comstats.rxtotal = stli_cdkstats.rxchars + stli_cdkstats.ringover;
	stli_comstats.txbuffered = stli_cdkstats.txringq;
	stli_comstats.rxbuffered += stli_cdkstats.rxringq;
	stli_comstats.rxoverrun = stli_cdkstats.overruns;
	stli_comstats.rxparity = stli_cdkstats.parity;
	stli_comstats.rxframing = stli_cdkstats.framing;
	stli_comstats.rxlost = stli_cdkstats.ringover;
	stli_comstats.rxbreaks = stli_cdkstats.rxbreaks;
	stli_comstats.txbreaks = stli_cdkstats.txbreaks;
	stli_comstats.txxon = stli_cdkstats.txstart;
	stli_comstats.txxoff = stli_cdkstats.txstop;
	stli_comstats.rxxon = stli_cdkstats.rxstart;
	stli_comstats.rxxoff = stli_cdkstats.rxstop;
	stli_comstats.rxrtsoff = stli_cdkstats.rtscnt / 2;
	stli_comstats.rxrtson = stli_cdkstats.rtscnt - stli_comstats.rxrtsoff;
	stli_comstats.modem = stli_cdkstats.dcdcnt;
	stli_comstats.hwid = stli_cdkstats.hwid;
	stli_comstats.signals = stli_mktiocm(stli_cdkstats.signals);

	return(0);
}

/*****************************************************************************/

/*
 *	Return the port stats structure to user app. A NULL port struct
 *	pointer passed in means that we need to find out from the app
 *	what port to get stats for (used through board control device).
 */

static int stli_getportstats(stliport_t *portp, comstats_t *cp)
{
	stlibrd_t	*brdp;
	int		rc;

	if (portp == (stliport_t *) NULL) {
		copy_from_user(&stli_comstats, cp, sizeof(comstats_t));
		portp = stli_getport(stli_comstats.brd, stli_comstats.panel,
			stli_comstats.port);
		if (portp == (stliport_t *) NULL)
			return(-ENODEV);
	}

	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);

	if ((rc = stli_portcmdstats(portp)) < 0)
		return(rc);

	copy_to_user(cp, &stli_comstats, sizeof(comstats_t));
	return(0);
}

/*****************************************************************************/

/*
 *	Clear the port stats structure. We also return it zeroed out...
 */

static int stli_clrportstats(stliport_t *portp, comstats_t *cp)
{
	stlibrd_t	*brdp;
	int		rc;

	if (portp == (stliport_t *) NULL) {
		copy_from_user(&stli_comstats, cp, sizeof(comstats_t));
		portp = stli_getport(stli_comstats.brd, stli_comstats.panel,
			stli_comstats.port);
		if (portp == (stliport_t *) NULL)
			return(-ENODEV);
	}

	brdp = stli_brds[portp->brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);

	if (brdp->state & BST_STARTED) {
		if ((rc = stli_cmdwait(brdp, portp, A_CLEARSTATS, 0, 0, 0)) < 0)
			return(rc);
	}

	memset(&stli_comstats, 0, sizeof(comstats_t));
	stli_comstats.brd = portp->brdnr;
	stli_comstats.panel = portp->panelnr;
	stli_comstats.port = portp->portnr;

	copy_to_user(cp, &stli_comstats, sizeof(comstats_t));
	return(0);
}

/*****************************************************************************/

/*
 *	Return the entire driver ports structure to a user app.
 */

static int stli_getportstruct(unsigned long arg)
{
	stliport_t	*portp;

	copy_from_user(&stli_dummyport, (void *) arg, sizeof(stliport_t));
	portp = stli_getport(stli_dummyport.brdnr, stli_dummyport.panelnr,
		 stli_dummyport.portnr);
	if (portp == (stliport_t *) NULL)
		return(-ENODEV);
	copy_to_user((void *) arg, portp, sizeof(stliport_t));
	return(0);
}

/*****************************************************************************/

/*
 *	Return the entire driver board structure to a user app.
 */

static int stli_getbrdstruct(unsigned long arg)
{
	stlibrd_t	*brdp;

	copy_from_user(&stli_dummybrd, (void *) arg, sizeof(stlibrd_t));
	if ((stli_dummybrd.brdnr < 0) || (stli_dummybrd.brdnr >= STL_MAXBRDS))
		return(-ENODEV);
	brdp = stli_brds[stli_dummybrd.brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);
	copy_to_user((void *) arg, brdp, sizeof(stlibrd_t));
	return(0);
}

/*****************************************************************************/

/*
 *	Memory device open code. Need to keep track of opens and close
 *	for module handling.
 */

static int stli_memopen(struct inode *ip, struct file *fp)
{
	MOD_INC_USE_COUNT;
	return(0);
}

/*****************************************************************************/

static int stli_memclose(struct inode *ip, struct file *fp)
{
	MOD_DEC_USE_COUNT;
	return(0);
}

/*****************************************************************************/

/*
 *	The "staliomem" device is also required to do some special operations on
 *	the board. We need to be able to send an interrupt to the board,
 *	reset it, and start/stop it.
 */

static int stli_memioctl(struct inode *ip, struct file *fp, unsigned int cmd, unsigned long arg)
{
	stlibrd_t	*brdp;
	int		brdnr, rc, done;

#if DEBUG
	printk("stli_memioctl(ip=%x,fp=%x,cmd=%x,arg=%x)\n", (int) ip,
		(int) fp, cmd, (int) arg);
#endif

/*
 *	First up handle the board independent ioctls.
 */
	done = 0;
	rc = 0;

	switch (cmd) {
	case COM_GETPORTSTATS:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(comstats_t))) == 0)
			rc = stli_getportstats((stliport_t *) NULL,
				(comstats_t *) arg);
		done++;
		break;
	case COM_CLRPORTSTATS:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(comstats_t))) == 0)
			rc = stli_clrportstats((stliport_t *) NULL,
				(comstats_t *) arg);
		done++;
		break;
	case COM_GETBRDSTATS:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(combrd_t))) == 0)
			rc = stli_getbrdstats((combrd_t *) arg);
		done++;
		break;
	case COM_READPORT:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(stliport_t))) == 0)
			rc = stli_getportstruct(arg);
		done++;
		break;
	case COM_READBOARD:
		if ((rc = verify_area(VERIFY_WRITE, (void *) arg,
		    sizeof(stlibrd_t))) == 0)
			rc = stli_getbrdstruct(arg);
		done++;
		break;
	default:
		break;
	}

	if (done)
		return(rc);

/*
 *	Now handle the board specific ioctls. These all depend on the
 *	minor number of the device they were called from.
 */
	brdnr = MINOR(ip->i_rdev);
	if (brdnr >= STL_MAXBRDS)
		return(-ENODEV);
	brdp = stli_brds[brdnr];
	if (brdp == (stlibrd_t *) NULL)
		return(-ENODEV);
	if (brdp->state == 0)
		return(-ENODEV);

	switch (cmd) {
	case STL_BINTR:
		EBRDINTR(brdp);
		break;
	case STL_BSTART:
		rc = stli_startbrd(brdp);
		break;
	case STL_BSTOP:
		brdp->state &= ~BST_STARTED;
		break;
	case STL_BRESET:
		brdp->state &= ~BST_STARTED;
		EBRDRESET(brdp);
		if (stli_shared == 0) {
			if (brdp->reenable != NULL)
				(* brdp->reenable)(brdp);
		}
		break;
	default:
		rc = -ENOIOCTLCMD;
		break;
	}

	return(rc);
}

/*****************************************************************************/

int __init stli_init(void)
{
	printk(KERN_INFO "%s: version %s\n", stli_drvtitle, stli_drvversion);

	stli_initbrds();

/*
 *	Allocate a temporary write buffer.
 */
	stli_tmpwritebuf = (char *) stli_memalloc(STLI_TXBUFSIZE);
	if (stli_tmpwritebuf == (char *) NULL)
		printk("STALLION: failed to allocate memory (size=%d)\n",
			STLI_TXBUFSIZE);
	stli_txcookbuf = (char *) stli_memalloc(STLI_TXBUFSIZE);
	if (stli_txcookbuf == (char *) NULL)
		printk("STALLION: failed to allocate memory (size=%d)\n",
			STLI_TXBUFSIZE);

/*
 *	Set up a character driver for the shared memory region. We need this
 *	to down load the slave code image. Also it is a useful debugging tool.
 */
	if (register_chrdev(STL_SIOMEMMAJOR, "staliomem", &stli_fsiomem))
		printk("STALLION: failed to register serial memory device\n");

/*
 *	Set up the tty driver structure and register us as a driver.
 *	Also setup the callout tty device.
 */
	memset(&stli_serial, 0, sizeof(struct tty_driver));
	stli_serial.magic = TTY_DRIVER_MAGIC;
	stli_serial.driver_name = stli_drvname;
	stli_serial.name = stli_serialname;
	stli_serial.major = STL_SERIALMAJOR;
	stli_serial.minor_start = 0;
	stli_serial.num = STL_MAXBRDS * STL_MAXPORTS;
	stli_serial.type = TTY_DRIVER_TYPE_SERIAL;
	stli_serial.subtype = STL_DRVTYPSERIAL;
	stli_serial.init_termios = stli_deftermios;
	stli_serial.flags = TTY_DRIVER_REAL_RAW;
	stli_serial.refcount = &stli_refcount;
	stli_serial.table = stli_ttys;
	stli_serial.termios = stli_termios;
	stli_serial.termios_locked = stli_termioslocked;
	
	stli_serial.open = stli_open;
	stli_serial.close = stli_close;
	stli_serial.write = stli_write;
	stli_serial.put_char = stli_putchar;
	stli_serial.flush_chars = stli_flushchars;
	stli_serial.write_room = stli_writeroom;
	stli_serial.chars_in_buffer = stli_charsinbuffer;
	stli_serial.ioctl = stli_ioctl;
	stli_serial.set_termios = stli_settermios;
	stli_serial.throttle = stli_throttle;
	stli_serial.unthrottle = stli_unthrottle;
	stli_serial.stop = stli_stop;
	stli_serial.start = stli_start;
	stli_serial.hangup = stli_hangup;
	stli_serial.flush_buffer = stli_flushbuffer;
	stli_serial.break_ctl = stli_breakctl;
	stli_serial.wait_until_sent = stli_waituntilsent;
	stli_serial.send_xchar = stli_sendxchar;
	stli_serial.read_proc = stli_readproc;

	stli_callout = stli_serial;
	stli_callout.name = stli_calloutname;
	stli_callout.major = STL_CALLOUTMAJOR;
	stli_callout.subtype = STL_DRVTYPCALLOUT;
	stli_callout.read_proc = 0;

	if (tty_register_driver(&stli_serial))
		printk("STALLION: failed to register serial driver\n");
	if (tty_register_driver(&stli_callout))
		printk("STALLION: failed to register callout driver\n");

	return(0);
}

/*****************************************************************************/
