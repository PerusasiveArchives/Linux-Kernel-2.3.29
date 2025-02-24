/*
 * File Name:
 *   defxx.c
 *
 * Copyright Information:
 *   Copyright Digital Equipment Corporation 1996.
 *
 *   This software may be used and distributed according to the terms of
 *   the GNU Public License, incorporated herein by reference.
 *
 * Abstract:
 *   A Linux device driver supporting the Digital Equipment Corporation
 *   FDDI EISA and PCI controller families.  Supported adapters include:
 *
 *		DEC FDDIcontroller/EISA (DEFEA)
 *		DEC FDDIcontroller/PCI  (DEFPA)
 *
 * Maintainers:
 *   LVS	Lawrence V. Stefani
 *
 * Contact:
 *	 The author may be reached at:
 *
 *		Inet: stefani@lkg.dec.com
 *		Mail: Digital Equipment Corporation
 *			  550 King Street
 *			  M/S: LKG1-3/M07
 *			  Littleton, MA  01460
 *
 * Credits:
 *   I'd like to thank Patricia Cross for helping me get started with
 *   Linux, David Davies for a lot of help upgrading and configuring
 *   my development system and for answering many OS and driver
 *   development questions, and Alan Cox for recommendations and
 *   integration help on getting FDDI support into Linux.  LVS
 *
 * Driver Architecture:
 *   The driver architecture is largely based on previous driver work
 *   for other operating systems.  The upper edge interface and
 *   functions were largely taken from existing Linux device drivers
 *   such as David Davies' DE4X5.C driver and Donald Becker's TULIP.C
 *   driver.
 *
 *   Adapter Probe -
 *		The driver scans for supported EISA adapters by reading the
 *		SLOT ID register for each EISA slot and making a match
 *		against the expected value.  The supported PCI adapters are
 *		discovered using successive calls to pcibios_find_device.
 *		The first time the probe routine is called, all supported
 *		devices are discovered and initialized.  The adapters aren't
 *		brought up to an operational state until the open routine is
 *		called.
 *
 *   Bus-Specific Initialization -
 *		This driver currently supports both EISA and PCI controller
 *		families.  While the custom DMA chip and FDDI logic is similar
 *		or identical, the bus logic is very different.  After
 *		initialization, the	only bus-specific differences is in how the
 *		driver enables and disables interrupts.  Other than that, the
 *		run-time critical code behaves the same on both families.
 *		It's important to note that both adapter families are configured
 *		to I/O map, rather than memory map, the adapter registers.
 *
 *   Driver Open/Close -
 *		In the driver open routine, the driver ISR (interrupt service
 *		routine) is registered and the adapter is brought to an
 *		operational state.  In the driver close routine, the opposite
 *		occurs; the driver ISR is deregistered and the adapter is
 *		brought to a safe, but closed state.  Users may use consecutive
 *		commands to bring the adapter up and down as in the following
 *		example:
 *					ifconfig fddi0 up
 *					ifconfig fddi0 down
 *					ifconfig fddi0 up
 *
 *   Driver Shutdown -
 *		Apparently, there is no shutdown or halt routine support under
 *		Linux.  This routine would be called during "reboot" or
 *		"shutdown" to allow the driver to place the adapter in a safe
 *		state before a warm reboot occurs.  To be really safe, the user
 *		should close the adapter before shutdown (eg. ifconfig fddi0 down)
 *		to ensure that the adapter DMA engine is taken off-line.  However,
 *		the current driver code anticipates this problem and always issues
 *		a soft reset of the adapter	at the beginning of driver initialization.
 *		A future driver enhancement in this area may occur in 2.1.X where
 *		Alan indicated that a shutdown handler may be implemented.
 *
 *   Interrupt Service Routine -
 *		The driver supports shared interrupts, so the ISR is registered for
 *		each board with the appropriate flag and the pointer to that board's
 *		device structure.  This provides the context during interrupt
 *		processing to support shared interrupts and multiple boards.
 *
 *		Interrupt enabling/disabling can occur at many levels.  At the host
 *		end, you can disable system interrupts, or disable interrupts at the
 *		PIC (on Intel systems).  Across the bus, both EISA and PCI adapters
 *		have a bus-logic chip interrupt enable/disable as well as a DMA
 *		controller interrupt enable/disable.
 *
 *		The driver currently enables and disables adapter interrupts at the
 *		bus-logic chip and assumes that Linux will take care of clearing or
 *		acknowledging any host-based interrupt chips.
 *
 *   Control Functions -
 *		Control functions are those used to support functions such as adding
 *		or deleting multicast addresses, enabling or disabling packet
 *		reception filters, or other custom/proprietary commands.  Presently,
 *		the driver supports the "get statistics", "set multicast list", and
 *		"set mac address" functions defined by Linux.  A list of possible
 *		enhancements include:
 *
 *				- Custom ioctl interface for executing port interface commands
 *				- Custom ioctl interface for adding unicast addresses to
 *				  adapter CAM (to support bridge functions).
 *				- Custom ioctl interface for supporting firmware upgrades.
 *
 *   Hardware (port interface) Support Routines -
 *		The driver function names that start with "dfx_hw_" represent
 *		low-level port interface routines that are called frequently.  They
 *		include issuing a DMA or port control command to the adapter,
 *		resetting the adapter, or reading the adapter state.  Since the
 *		driver initialization and run-time code must make calls into the
 *		port interface, these routines were written to be as generic and
 *		usable as possible.
 *
 *   Receive Path -
 *		The adapter DMA engine supports a 256 entry receive descriptor block
 *		of which up to 255 entries can be used at any given time.  The
 *		architecture is a standard producer, consumer, completion model in
 *		which the driver "produces" receive buffers to the adapter, the
 *		adapter "consumes" the receive buffers by DMAing incoming packet data,
 *		and the driver "completes" the receive buffers by servicing the
 *		incoming packet, then "produces" a new buffer and starts the cycle
 *		again.  Receive buffers can be fragmented in up to 16 fragments
 *		(descriptor	entries).  For simplicity, this driver posts
 *		single-fragment receive buffers of 4608 bytes, then allocates a
 *		sk_buff, copies the data, then reposts the buffer.  To reduce CPU
 *		utilization, a better approach would be to pass up the receive
 *		buffer (no extra copy) then allocate and post a replacement buffer.
 *		This is a performance enhancement that should be looked into at
 *		some point.
 *
 *   Transmit Path -
 *		Like the receive path, the adapter DMA engine supports a 256 entry
 *		transmit descriptor block of which up to 255 entries can be used at
 *		any	given time.  Transmit buffers can be fragmented	in up to 255
 *		fragments (descriptor entries).  This driver always posts one
 *		fragment per transmit packet request.
 *
 *		The fragment contains the entire packet from FC to end of data.
 *		Before posting the buffer to the adapter, the driver sets a three-byte
 *		packet request header (PRH) which is required by the Motorola MAC chip
 *		used on the adapters.  The PRH tells the MAC the type of token to
 *		receive/send, whether or not to generate and append the CRC, whether
 *		synchronous or asynchronous framing is used, etc.  Since the PRH
 *		definition is not necessarily consistent across all FDDI chipsets,
 *		the driver, rather than the common FDDI packet handler routines,
 *		sets these bytes.
 *
 *		To reduce the amount of descriptor fetches needed per transmit request,
 *		the driver takes advantage of the fact that there are at least three
 *		bytes available before the skb->data field on the outgoing transmit
 *		request.  This is guaranteed by having fddi_setup() in net_init.c set
 *		dev->hard_header_len to 24 bytes.  21 bytes accounts for the largest
 *		header in an 802.2 SNAP frame.  The other 3 bytes are the extra "pad"
 *		bytes which we'll use to store the PRH.
 *
 *		There's a subtle advantage to adding these pad bytes to the
 *		hard_header_len, it ensures that the data portion of the packet for
 *		an 802.2 SNAP frame is longword aligned.  Other FDDI driver
 *		implementations may not need the extra padding and can start copying
 *		or DMAing directly from the FC byte which starts at skb->data.  Should
 *		another driver implementation need ADDITIONAL padding, the net_init.c
 *		module should be updated and dev->hard_header_len should be increased.
 *		NOTE: To maintain the alignment on the data portion of the packet,
 *		dev->hard_header_len should always be evenly divisible by 4 and at
 *		least 24 bytes in size.
 *
 * Modification History:
 *		Date		Name	Description
 *		16-Aug-96	LVS		Created.
 *		20-Aug-96	LVS		Updated dfx_probe so that version information
 *							string is only displayed if 1 or more cards are
 *							found.  Changed dfx_rcv_queue_process to copy
 *							3 NULL bytes before FC to ensure that data is
 *							longword aligned in receive buffer.
 *		09-Sep-96	LVS		Updated dfx_ctl_set_multicast_list to enable
 *							LLC group promiscuous mode if multicast list
 *							is too large.  LLC individual/group promiscuous
 *							mode is now disabled if IFF_PROMISC flag not set.
 *							dfx_xmt_queue_pkt no longer checks for NULL skb
 *							on Alan Cox recommendation.  Added node address
 *							override support.
 *		12-Sep-96	LVS		Reset current address to factory address during
 *							device open.  Updated transmit path to post a
 *							single fragment which includes PRH->end of data.
 */

/* Version information string - should be updated prior to each new release!!! */

static const char *version = "defxx.c:v1.04 09/16/96  Lawrence V. Stefani (stefani@lkg.dec.com)\n";

/* Include files */

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <asm/byteorder.h>
#include <asm/bitops.h>
#include <asm/io.h>

#include <linux/netdevice.h>
#include <linux/fddidevice.h>
#include <linux/skbuff.h>

#include "defxx.h"

#define DYNAMIC_BUFFERS 1

#define SKBUFF_RX_COPYBREAK 200
/*
 * NEW_SKB_SIZE = PI_RCV_DATA_K_SIZE_MAX+128 to allow 128 byte
 * alignment for compatibility with old EISA boards.
 */
#define NEW_SKB_SIZE (PI_RCV_DATA_K_SIZE_MAX+128)

/* Define global routines */

int	dfx_probe(struct net_device *dev);

/* Define module-wide (static) routines */

static struct net_device *dfx_alloc_device(struct net_device *dev, u16 iobase);

static void		dfx_bus_init(struct net_device *dev);
static void		dfx_bus_config_check(DFX_board_t *bp);

static int		dfx_driver_init(struct net_device *dev);
static int		dfx_adap_init(DFX_board_t *bp);

static int		dfx_open(struct net_device *dev);
static int		dfx_close(struct net_device *dev);

static void		dfx_int_pr_halt_id(DFX_board_t *bp);
static void		dfx_int_type_0_process(DFX_board_t *bp);
static void		dfx_int_common(DFX_board_t *bp);
static void		dfx_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static struct		net_device_stats *dfx_ctl_get_stats(struct net_device *dev);
static void		dfx_ctl_set_multicast_list(struct net_device *dev);
static int		dfx_ctl_set_mac_address(struct net_device *dev, void *addr);
static int		dfx_ctl_update_cam(DFX_board_t *bp);
static int		dfx_ctl_update_filters(DFX_board_t *bp);

static int		dfx_hw_dma_cmd_req(DFX_board_t *bp);
static int		dfx_hw_port_ctrl_req(DFX_board_t *bp, PI_UINT32	command, PI_UINT32 data_a, PI_UINT32 data_b, PI_UINT32 *host_data);
static void		dfx_hw_adap_reset(DFX_board_t *bp, PI_UINT32 type);
static int		dfx_hw_adap_state_rd(DFX_board_t *bp);
static int		dfx_hw_dma_uninit(DFX_board_t *bp, PI_UINT32 type);

static void		dfx_rcv_init(DFX_board_t *bp);
static void		dfx_rcv_queue_process(DFX_board_t *bp);

static int		dfx_xmt_queue_pkt(struct sk_buff *skb, struct net_device *dev);
static void		dfx_xmt_done(DFX_board_t *bp);
static void		dfx_xmt_flush(DFX_board_t *bp);

/* Define module-wide (static) variables */

static int		num_boards = 0;			/* total number of adapters configured */
static int		already_probed = 0;		/* have we already entered dfx_probe? */


/*
 * =======================
 * = dfx_port_write_byte =
 * = dfx_port_read_byte	 =
 * = dfx_port_write_long =
 * = dfx_port_read_long  =
 * =======================
 *   
 * Overview:
 *   Routines for reading and writing values from/to adapter
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp     - pointer to board information
 *   offset - register offset from base I/O address
 *   data   - for dfx_port_write_byte and dfx_port_write_long, this
 *			  is a value to write.
 *			  for dfx_port_read_byte and dfx_port_read_byte, this
 *			  is a pointer to store the read value.
 *
 * Functional Description:
 *   These routines perform the correct operation to read or write
 *   the adapter register.
 *   
 *   EISA port block base addresses are based on the slot number in which the
 *   controller is installed.  For example, if the EISA controller is installed
 *   in slot 4, the port block base address is 0x4000.  If the controller is
 *   installed in slot 2, the port block base address is 0x2000, and so on.
 *   This port block can be used to access PDQ, ESIC, and DEFEA on-board
 *   registers using the register offsets defined in DEFXX.H.
 *
 *   PCI port block base addresses are assigned by the PCI BIOS or system
 *	 firmware.  There is one 128 byte port block which can be accessed.  It
 *   allows for I/O mapping of both PDQ and PFI registers using the register
 *   offsets defined in DEFXX.H.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   bp->base_addr is a valid base I/O address for this adapter.
 *   offset is a valid register offset for this adapter.
 *
 * Side Effects:
 *   Rather than produce macros for these functions, these routines
 *   are defined using "inline" to ensure that the compiler will
 *   generate inline code and not waste a procedure call and return.
 *   This provides all the benefits of macros, but with the
 *   advantage of strict data type checking.
 */

static inline void dfx_port_write_byte(
	DFX_board_t	*bp,
	int			offset,
	u8			data
	)

	{
	u16 port = bp->base_addr + offset;

	outb(data, port);
	return;
	}

static inline void dfx_port_read_byte(
	DFX_board_t	*bp,
	int			offset,
	u8			*data
	)

	{
	u16 port = bp->base_addr + offset;

	*data = inb(port);
	return;
	}

static inline void dfx_port_write_long(
	DFX_board_t	*bp,
	int			offset,
	u32			data
	)

	{
	u16 port = bp->base_addr + offset;

	outl(data, port);
	return;
	}

static inline void dfx_port_read_long(
	DFX_board_t	*bp,
	int			offset,
	u32			*data
	)

	{
	u16 port = bp->base_addr + offset;

	*data = inl(port);
	return;
	}


/*
 * =============
 * = dfx_probe =
 * =============
 *   
 * Overview:
 *   Probes for supported FDDI EISA and PCI controllers
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This routine is called by the OS for each FDDI device name (fddi0,
 *   fddi1,...,fddi6, fddi7) specified in drivers/net/Space.c.  Since
 *   the DEFXX.C driver currently does not support being loaded as a
 *   module, dfx_probe() will initialize all devices the first time
 *   it is called.
 *
 *   Let's say that dfx_probe() is getting called to initialize fddi0.
 *   Furthermore, let's say there are three supported controllers in the
 *   system.  Before dfx_probe() leaves, devices fddi0, fddi1, and fddi2
 *   will be initialized and a global flag will be set to indicate that
 *   dfx_probe() has already been called.
 *
 *   However...the OS doesn't know that we've already initialized
 *   devices fddi1 and fddi2 so dfx_probe() gets called again and again
 *   until it reaches the end of the device list for FDDI (presently,
 *   fddi7).  It's important that the driver "pretend" to probe for
 *   devices fddi1 and fddi2 and return success.  Devices fddi3
 *   through fddi7 will return failure since they weren't initialized.
 *
 *   This algorithm seems to work for the time being.  As other FDDI
 *   drivers are written for Linux, a more generic approach (perhaps
 *   similar to the Ethernet card approach) may need to be implemented.
 *   
 * Return Codes:
 *   0		 - This device (fddi0, fddi1, etc) configured successfully
 *   -ENODEV - No devices present, or no Digital FDDI EISA or PCI device
 *			   present for this device name
 *
 * Assumptions:
 *   For the time being, DEFXX.C is the only FDDI driver under Linux.
 *   As this assumption changes, this routine will likely be impacted.
 *   Also, it is assumed that no more than eight (8) FDDI controllers
 *   will be configured in the system (fddi0 through fddi7).  This
 *   routine will not allocate new device structures.  If more than
 *   eight FDDI controllers need to be configured, drivers/net/Space.c
 *   should be updated as well as the DFX_MAX_NUM_BOARDS constant in
 *   DEFXX.H.
 *
 * Side Effects:
 *   Device structures for FDDI adapters (fddi0, fddi1, etc) are
 *   initialized and the board resources are read and stored in
 *   the device structure.
 */

int __init dfx_probe(struct net_device *dev)
{
	int				i;				/* used in for loops */
	int				version_disp;	/* was version info string already displayed? */
	int				port_len;		/* length of port address range (in bytes) */
	u16				port;			/* temporary I/O (port) address */
	struct pci_dev *		pdev = NULL;		/* PCI device record */
	u16				command;		/* PCI Configuration space Command register val */
	u32				slot_id;		/* EISA hardware (slot) ID read from adapter */
	DFX_board_t		*bp;			/* board pointer */

	DBG_printk("In dfx_probe...\n");

	/*
	 * Verify whether we're going through dfx_probe() again
	 *
	 * If so, see if we're going through for a subsequent fddi device that
	 * we've already initialized.  If we are, return success (0).  If not,
	 * return failure (-ENODEV).
	 */

	version_disp = 0;				/* default to version string not displayed */
	if (already_probed)
		{
		DBG_printk("Already entered dfx_probe\n");
		if (dev != NULL)
			if ((strncmp(dev->name, "fddi", 4) == 0) && (dev->base_addr != 0))
				{
				DBG_printk("In dfx_probe for fddi adapter (%s) we've already initialized it, so return success\n", dev->name);
				return(0);
				}
		return(-ENODEV);
		}
	already_probed = 1;			/* set global flag */

	/* Scan for FDDI EISA controllers */

	for (i=0; i < DFX_MAX_EISA_SLOTS; i++)		/* only scan for up to 16 EISA slots */
		{
		port = (i << 12) + PI_ESIC_K_SLOT_ID;	/* port = I/O address for reading slot ID */
		slot_id = inl(port);					/* read EISA HW (slot) ID */
		if ((slot_id & 0xF0FFFFFF) == DEFEA_PRODUCT_ID)
			{
			if (!version_disp)					/* display version info if adapter is found */
				{
				version_disp = 1;				/* set display flag to TRUE so that */
				printk(version);				/* we only display this string ONCE */
				}
				
			port = (i << 12);					/* recalc base addr */

			/* Verify port address range is not already being used */

			port_len = PI_ESIC_K_CSR_IO_LEN;
			if (check_region(port, port_len) == 0)
				{
				/* Allocate a new device structure for this adapter */

				dev = dfx_alloc_device(dev, port);
				if (dev != NULL)
					{
					/* Initialize board structure with bus-specific info */

					bp = (DFX_board_t *) dev->priv;
					bp->dev = dev;
					bp->bus_type = DFX_BUS_TYPE_EISA;
					if (dfx_driver_init(dev) == DFX_K_SUCCESS)
						num_boards++;		/* only increment global board count on success */
					else
						dev->base_addr = 0;	/* clear port address field in device structure on failure */
					}
				}
			else
				printk("I/O range allocated to adapter (0x%X-0x%X) is already being used!\n", port, (port + port_len-1));
			}
		}

	/* Scan for FDDI PCI controllers */

	if (pci_present())						/* is PCI even present? */
		while ((pdev = pci_find_device(PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_FDDI, pdev)))
			{
			if (!version_disp)					/* display version info if adapter is found */
				{
				version_disp = 1;				/* set display flag to TRUE so that */
				printk(version);				/* we only display this string ONCE */
				}

			/* Verify that I/O enable bit is set (PCI slot is enabled) */

			pci_read_config_word(pdev, PCI_COMMAND, &command);
			if ((command & PCI_COMMAND_IO) == 0)
				printk("I/O enable bit not set!  Verify that slot is enabled\n");
			else
				{
				/* Turn off memory mapped space and enable mastering */

				command |= PCI_COMMAND_MASTER;
				command &= ~PCI_COMMAND_MEMORY;
				pci_write_config_word(pdev, PCI_COMMAND, command);

				/* Get I/O base address from PCI Configuration Space */

				port = pdev->resource[1].start;

				/* Verify port address range is not already being used */

				port_len = PFI_K_CSR_IO_LEN;

				if (check_region(port, port_len) == 0)
					{
					/* Allocate a new device structure for this adapter */

					dev = dfx_alloc_device(dev, port);
					if (dev != NULL)
						{
						/* Initialize board structure with bus-specific info */

						bp = (DFX_board_t *) dev->priv;
						bp->dev = dev;
						bp->bus_type = DFX_BUS_TYPE_PCI;
						bp->pci_dev = pdev;
						if (dfx_driver_init(dev) == DFX_K_SUCCESS)
							num_boards++;		/* only increment global board count on success */
						else
							dev->base_addr = 0;	/* clear port address field in device structure on failure */
						}
					}
				else
					printk("I/O range allocated to adapter (0x%X-0x%X) is already being used!\n", port, (port + port_len-1));
				}
			}

	/*
	 * If we're at this point we're going through dfx_probe() for the first
	 * time.  Return success (0) if we've initialized 1 or more boards.
	 * Otherwise, return failure (-ENODEV).
	 */

	if (num_boards > 0)
		return(0);
	else
		return(-ENODEV);
	}


/*
 * ====================
 * = dfx_alloc_device =
 * ====================
 *   
 * Overview:
 *   Allocate new device structure for adapter
 *  
 * Returns:
 *   Pointer to device structure for this adapter or NULL if
 *   none are available or could not allocate memory for
 *   private board structure.
 *       
 * Arguments:
 *   dev    - pointer to device information for last device
 *   iobase - base I/O address of new adapter
 *
 * Functional Description:
 *   The algorithm for allocating a new device structure is
 *   fairly simple.  Since we're presently the only FDDI driver
 *   under Linux, we'll find the first device structure with an
 *   "fddi*" device name that's free.  If we run out of devices,
 *   we'll fail on error.  This is simpler than trying to
 *   allocate the memory for a new device structure, determine
 *   the next free number (beyond 7) and link it into the chain
 *   of devices.  A user can always modify drivers/net/Space.c
 *   to add new FDDI device structures if necessary.
 *
 *   Beyond finding a free FDDI device structure, this routine
 *   initializes most of the fields, resource tags, and dispatch
 *   pointers in the device structure and calls the common
 *   fddi_setup() routine to perform the rest of the device
 *   structure initialization.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   If additional FDDI drivers are integrated into Linux,
 *   we'll likely need to use a different approach to
 *   allocate a device structure.  Perhaps one that is
 *   similar to what the Ethernet drivers use.
 *
 * Side Effects:
 *   None
 */

struct net_device __init *dfx_alloc_device( struct net_device *dev, u16 iobase)
{
	struct net_device *tmp_dev;		/* pointer to a device structure */

	DBG_printk("In dfx_alloc_device...\n");

	/* Find next free fddi entry */

	for (tmp_dev = dev; tmp_dev != NULL; tmp_dev = tmp_dev->next)
		if ((strncmp(tmp_dev->name, "fddi", 4) == 0) && (tmp_dev->base_addr == 0))
			break;
	if (tmp_dev == NULL)
		{
		printk("Could not find free FDDI device structure for this adapter!\n");
		return(NULL);
		}
	DBG_printk("Device entry free, device name = %s\n", tmp_dev->name);

	/* Allocate space for private board structure */

	tmp_dev->priv = (void *) kmalloc(sizeof(DFX_board_t), GFP_KERNEL);
	if (tmp_dev->priv == NULL)
		{
		printk("Could not allocate memory for private board structure!\n");
		return(NULL);
		}
	memset(tmp_dev->priv, 0, sizeof(DFX_board_t));	/* clear structure */

	/* Initialize new device structure */

	tmp_dev->rmem_end	= 0;		/* shared memory isn't used */
	tmp_dev->rmem_start	= 0;		/* shared memory isn't used */
	tmp_dev->mem_end	= 0;		/* shared memory isn't used */
	tmp_dev->mem_start	= 0;		/* shared memory isn't used */
	tmp_dev->base_addr	= iobase;	/* save port (I/O) base address */
	tmp_dev->irq		= 0;		/* set in dfx_bus_init() */
	tmp_dev->if_port	= 0;		/* not applicable to FDDI adapters */
	tmp_dev->dma		= 0;		/* Bus Master DMA doesn't require channel */

	tmp_dev->get_stats				= &dfx_ctl_get_stats;
	tmp_dev->open					= &dfx_open;
	tmp_dev->stop					= &dfx_close;
	tmp_dev->hard_start_xmit		= &dfx_xmt_queue_pkt;
	tmp_dev->hard_header			= NULL;		/* set in fddi_setup() */
	tmp_dev->rebuild_header			= NULL;		/* set in fddi_setup() */
	tmp_dev->set_multicast_list		= &dfx_ctl_set_multicast_list;
	tmp_dev->set_mac_address		= &dfx_ctl_set_mac_address;
	tmp_dev->do_ioctl			= NULL;		/* not supported for now &&& */
	tmp_dev->set_config			= NULL;		/* not supported for now &&& */
	tmp_dev->hard_header_cache		= NULL;		/* not supported */
	tmp_dev->header_cache_update		= NULL;		/* not supported */
	tmp_dev->change_mtu			= NULL;		/* set in fddi_setup() */

	/* Initialize remaining device structure information */

	fddi_setup(tmp_dev);
	return(tmp_dev);
	}


/*
 * ================
 * = dfx_bus_init =
 * ================
 *   
 * Overview:
 *   Initializes EISA and PCI controller bus-specific logic.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Determine and save adapter IRQ in device table,
 *   then perform bus-specific logic initialization.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   dev->base_addr has already been set with the proper
 *	 base I/O address for this device.
 *
 * Side Effects:
 *   Interrupts are enabled at the adapter bus-specific logic.
 *   Note:  Interrupts at the DMA engine (PDQ chip) are not
 *   enabled yet.
 */

void __init dfx_bus_init(struct net_device *dev)
{
	DFX_board_t *bp = (DFX_board_t *)dev->priv;
	u8			val;	/* used for I/O read/writes */

	DBG_printk("In dfx_bus_init...\n");

	/*
	 * Initialize base I/O address field in bp structure
	 *
	 * Note: bp->base_addr is the same as dev->base_addr.
	 *		 It's useful because often we'll need to read
	 *		 or write registers where we already have the
	 *		 bp pointer instead of the dev pointer.  Having
	 *		 the base address in the bp structure will
	 *		 save a pointer dereference.
	 *
	 *		 IMPORTANT!! This field must be defined before
	 *		 any of the dfx_port_* inline functions are
	 *		 called.
	 */

	bp->base_addr = dev->base_addr;

	/* Initialize adapter based on bus type */

	if (bp->bus_type == DFX_BUS_TYPE_EISA)
		{
		/* Get the interrupt level from the ESIC chip */

		dfx_port_read_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, &val);
		switch ((val & PI_CONFIG_STAT_0_M_IRQ) >> PI_CONFIG_STAT_0_V_IRQ)
			{
			case PI_CONFIG_STAT_0_IRQ_K_9:
				dev->irq = 9;
				break;

			case PI_CONFIG_STAT_0_IRQ_K_10:
				dev->irq = 10;
				break;

			case PI_CONFIG_STAT_0_IRQ_K_11:
				dev->irq = 11;
				break;

			case PI_CONFIG_STAT_0_IRQ_K_15:
				dev->irq = 15;
				break;
			}

		/* Enable access to I/O on the board by writing 0x03 to Function Control Register */

		dfx_port_write_byte(bp, PI_ESIC_K_FUNCTION_CNTRL, PI_ESIC_K_FUNCTION_CNTRL_IO_ENB);

		/* Set the I/O decode range of the board */

		val = ((dev->base_addr >> 12) << PI_IO_CMP_V_SLOT);
		dfx_port_write_byte(bp, PI_ESIC_K_IO_CMP_0_1, val);
		dfx_port_write_byte(bp, PI_ESIC_K_IO_CMP_1_1, val);

		/* Enable access to rest of module (including PDQ and packet memory) */

		dfx_port_write_byte(bp, PI_ESIC_K_SLOT_CNTRL, PI_SLOT_CNTRL_M_ENB);

		/*
		 * Map PDQ registers into I/O space.  This is done by clearing a bit
		 * in Burst Holdoff register.
		 */

		dfx_port_read_byte(bp, PI_ESIC_K_BURST_HOLDOFF, &val);
		dfx_port_write_byte(bp, PI_ESIC_K_BURST_HOLDOFF, (val & ~PI_BURST_HOLDOFF_M_MEM_MAP));

		/* Enable interrupts at EISA bus interface chip (ESIC) */

		dfx_port_read_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, &val);
		dfx_port_write_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, (val | PI_CONFIG_STAT_0_M_INT_ENB));
		}
	else
		{
		struct pci_dev *pdev = bp->pci_dev;

		/* Get the interrupt level from the PCI Configuration Table */

		dev->irq = pdev->irq;

		/* Check Latency Timer and set if less than minimal */

		pci_read_config_byte(pdev, PCI_LATENCY_TIMER, &val);
		if (val < PFI_K_LAT_TIMER_MIN)	/* if less than min, override with default */
			{
			val = PFI_K_LAT_TIMER_DEF;
			pci_write_config_byte(pdev, PCI_LATENCY_TIMER, val);
			}

		/* Enable interrupts at PCI bus interface chip (PFI) */

		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL, (PFI_MODE_M_PDQ_INT_ENB | PFI_MODE_M_DMA_ENB));
		}
	return;
	}


/*
 * ========================
 * = dfx_bus_config_check =
 * ========================
 *   
 * Overview:
 *   Checks the configuration (burst size, full-duplex, etc.)  If any parameters
 *   are illegal, then this routine will set new defaults.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   For Revision 1 FDDI EISA, Revision 2 or later FDDI EISA with rev E or later
 *   PDQ, and all FDDI PCI controllers, all values are legal.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   dfx_adap_init has NOT been called yet so burst size and other items have
 *   not been set.
 *
 * Side Effects:
 *   None
 */

void __init dfx_bus_config_check(DFX_board_t *bp)
{
	int	status;				/* return code from adapter port control call */
	u32	slot_id;			/* EISA-bus hardware id (DEC3001, DEC3002,...) */
	u32	host_data;			/* LW data returned from port control call */

	DBG_printk("In dfx_bus_config_check...\n");

	/* Configuration check only valid for EISA adapter */

	if (bp->bus_type == DFX_BUS_TYPE_EISA)
		{
		dfx_port_read_long(bp, PI_ESIC_K_SLOT_ID, &slot_id);

		/*
		 * First check if revision 2 EISA controller.  Rev. 1 cards used
		 * PDQ revision B, so no workaround needed in this case.  Rev. 3
		 * cards used PDQ revision E, so no workaround needed in this
		 * case, either.  Only Rev. 2 cards used either Rev. D or E
		 * chips, so we must verify the chip revision on Rev. 2 cards.
		 */

		if (slot_id == DEFEA_PROD_ID_2)
			{
			/*
			 * Revision 2 FDDI EISA controller found, so let's check PDQ
			 * revision of adapter.
			 */

			status = dfx_hw_port_ctrl_req(bp,
											PI_PCTRL_M_SUB_CMD,
											PI_SUB_CMD_K_PDQ_REV_GET,
											0,
											&host_data);
			if ((status != DFX_K_SUCCESS) || (host_data == 2))
				{
				/*
				 * Either we couldn't determine the PDQ revision, or
				 * we determined that it is at revision D.  In either case,
				 * we need to implement the workaround.
				 */

				/* Ensure that the burst size is set to 8 longwords or less */

				switch (bp->burst_size)
					{
					case PI_PDATA_B_DMA_BURST_SIZE_32:
					case PI_PDATA_B_DMA_BURST_SIZE_16:
						bp->burst_size = PI_PDATA_B_DMA_BURST_SIZE_8;
						break;

					default:
						break;
					}

				/* Ensure that full-duplex mode is not enabled */

				bp->full_duplex_enb = PI_SNMP_K_FALSE;
				}
			}
		}
	return;
	}


/*
 * ===================
 * = dfx_driver_init =
 * ===================
 *   
 * Overview:
 *   Initializes remaining adapter board structure information
 *   and makes sure adapter is in a safe state prior to dfx_open().
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This function allocates additional resources such as the host memory
 *   blocks needed by the adapter (eg. descriptor and consumer blocks).
 *	 Remaining bus initialization steps are also completed.  The adapter
 *   is also reset so that it is in the DMA_UNAVAILABLE state.  The OS
 *   must call dfx_open() to open the adapter and bring it on-line.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	- initialization succeeded
 *   DFX_K_FAILURE	- initialization failed - could not allocate memory
 *						or read adapter MAC address
 *
 * Assumptions:
 *   Memory allocated from kmalloc() call is physically contiguous, locked
 *   memory whose physical address equals its virtual address.
 *
 * Side Effects:
 *   Adapter is reset and should be in DMA_UNAVAILABLE state before
 *   returning from this routine.
 */

int __init dfx_driver_init(struct net_device *dev)
{
	DFX_board_t *bp = (DFX_board_t *)dev->priv;
	int			alloc_size;			/* total buffer size needed */
	char		*top_v, *curr_v;	/* virtual addrs into memory block */
	u32			top_p, curr_p;		/* physical addrs into memory block */
	u32			data;				/* host data register value */

	DBG_printk("In dfx_driver_init...\n");

	/* Initialize bus-specific hardware registers */

	dfx_bus_init(dev);

	/*
	 * Initialize default values for configurable parameters
	 *
	 * Note: All of these parameters are ones that a user may
	 *       want to customize.  It'd be nice to break these
	 *		 out into Space.c or someplace else that's more
	 *		 accessible/understandable than this file.
	 */

	bp->full_duplex_enb		= PI_SNMP_K_FALSE;
	bp->req_ttrt			= 8 * 12500;		/* 8ms in 80 nanosec units */
	bp->burst_size			= PI_PDATA_B_DMA_BURST_SIZE_DEF;
	bp->rcv_bufs_to_post	= RCV_BUFS_DEF;

	/*
	 * Ensure that HW configuration is OK
	 *
	 * Note: Depending on the hardware revision, we may need to modify
	 *       some of the configurable parameters to workaround hardware
	 *       limitations.  We'll perform this configuration check AFTER
	 *       setting the parameters to their default values.
	 */

	dfx_bus_config_check(bp);

	/* Disable PDQ interrupts first */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);

	/* Place adapter in DMA_UNAVAILABLE state by resetting adapter */

	(void) dfx_hw_dma_uninit(bp, PI_PDATA_A_RESET_M_SKIP_ST);

	/*  Read the factory MAC address from the adapter then save it */

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_MLA,
							PI_PDATA_A_MLA_K_LO,
							0,
							&data) != DFX_K_SUCCESS)
		{
		printk("%s: Could not read adapter factory MAC address!\n", dev->name);
		return(DFX_K_FAILURE);
		}
	memcpy(&bp->factory_mac_addr[0], &data, sizeof(u32));

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_MLA,
							PI_PDATA_A_MLA_K_HI,
							0,
							&data) != DFX_K_SUCCESS)
		{
		printk("%s: Could not read adapter factory MAC address!\n", dev->name);
		return(DFX_K_FAILURE);
		}
	memcpy(&bp->factory_mac_addr[4], &data, sizeof(u16));

	/*
	 * Set current address to factory address
	 *
	 * Note: Node address override support is handled through
	 *       dfx_ctl_set_mac_address.
	 */

	memcpy(dev->dev_addr, bp->factory_mac_addr, FDDI_K_ALEN);
	if (bp->bus_type == DFX_BUS_TYPE_EISA)
		printk("%s: DEFEA at I/O addr = 0x%lX, IRQ = %d, Hardware addr = %02X-%02X-%02X-%02X-%02X-%02X\n",
				dev->name,
				dev->base_addr,
				dev->irq,
				dev->dev_addr[0],
				dev->dev_addr[1],
				dev->dev_addr[2],
				dev->dev_addr[3],
				dev->dev_addr[4],
				dev->dev_addr[5]);
	else
		printk("%s: DEFPA at I/O addr = 0x%lX, IRQ = %d, Hardware addr = %02X-%02X-%02X-%02X-%02X-%02X\n",
				dev->name,
				dev->base_addr,
				dev->irq,
				dev->dev_addr[0],
				dev->dev_addr[1],
				dev->dev_addr[2],
				dev->dev_addr[3],
				dev->dev_addr[4],
				dev->dev_addr[5]);

	/*
	 * Get memory for descriptor block, consumer block, and other buffers
	 * that need to be DMA read or written to by the adapter.
	 */

	alloc_size = sizeof(PI_DESCR_BLOCK) +
					PI_CMD_REQ_K_SIZE_MAX +
					PI_CMD_RSP_K_SIZE_MAX +
#ifndef DYNAMIC_BUFFERS
					(bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX) +
#endif
					sizeof(PI_CONSUMER_BLOCK) +
					(PI_ALIGN_K_DESC_BLK - 1);
	top_v = (char *) kmalloc(alloc_size, GFP_KERNEL);
	if (top_v == NULL)
		{
		printk("%s: Could not allocate memory for host buffers and structures!\n", dev->name);
		return(DFX_K_FAILURE);
		}
	memset(top_v, 0, alloc_size);	/* zero out memory before continuing */
	top_p = virt_to_bus(top_v);		/* get physical address of buffer */

	/*
	 *  To guarantee the 8K alignment required for the descriptor block, 8K - 1
	 *  plus the amount of memory needed was allocated.  The physical address
	 *	is now 8K aligned.  By carving up the memory in a specific order,
	 *  we'll guarantee the alignment requirements for all other structures.
	 *
	 *  Note: If the assumptions change regarding the non-paged, non-cached,
	 *		  physically contiguous nature of the memory block or the address
	 *		  alignments, then we'll need to implement a different algorithm
	 *		  for allocating the needed memory.
	 */

	curr_p = (u32) (ALIGN(top_p, PI_ALIGN_K_DESC_BLK));
	curr_v = top_v + (curr_p - top_p);

	/* Reserve space for descriptor block */

	bp->descr_block_virt = (PI_DESCR_BLOCK *) curr_v;
	bp->descr_block_phys = curr_p;
	curr_v += sizeof(PI_DESCR_BLOCK);
	curr_p += sizeof(PI_DESCR_BLOCK);

	/* Reserve space for command request buffer */

	bp->cmd_req_virt = (PI_DMA_CMD_REQ *) curr_v;
	bp->cmd_req_phys = curr_p;
	curr_v += PI_CMD_REQ_K_SIZE_MAX;
	curr_p += PI_CMD_REQ_K_SIZE_MAX;

	/* Reserve space for command response buffer */

	bp->cmd_rsp_virt = (PI_DMA_CMD_RSP *) curr_v;
	bp->cmd_rsp_phys = curr_p;
	curr_v += PI_CMD_RSP_K_SIZE_MAX;
	curr_p += PI_CMD_RSP_K_SIZE_MAX;

	/* Reserve space for the LLC host receive queue buffers */

	bp->rcv_block_virt = curr_v;
	bp->rcv_block_phys = curr_p;

#ifndef DYNAMIC_BUFFERS
	curr_v += (bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX);
	curr_p += (bp->rcv_bufs_to_post * PI_RCV_DATA_K_SIZE_MAX);
#endif

	/* Reserve space for the consumer block */

	bp->cons_block_virt = (PI_CONSUMER_BLOCK *) curr_v;
	bp->cons_block_phys = curr_p;

	/* Display virtual and physical addresses if debug driver */

	DBG_printk("%s: Descriptor block virt = %0lX, phys = %0X\n",				dev->name, (long)bp->descr_block_virt,	bp->descr_block_phys);
	DBG_printk("%s: Command Request buffer virt = %0lX, phys = %0X\n",			dev->name, (long)bp->cmd_req_virt,		bp->cmd_req_phys);
	DBG_printk("%s: Command Response buffer virt = %0lX, phys = %0X\n",			dev->name, (long)bp->cmd_rsp_virt,		bp->cmd_rsp_phys);
	DBG_printk("%s: Receive buffer block virt = %0lX, phys = %0X\n",			dev->name, (long)bp->rcv_block_virt,	bp->rcv_block_phys);
	DBG_printk("%s: Consumer block virt = %0lX, phys = %0X\n",					dev->name, (long)bp->cons_block_virt,	bp->cons_block_phys);

	return(DFX_K_SUCCESS);
	}


/*
 * =================
 * = dfx_adap_init =
 * =================
 *   
 * Overview:
 *   Brings the adapter to the link avail/link unavailable state.
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Issues the low-level firmware/hardware calls necessary to bring
 *   the adapter up, or to properly reset and restore adapter during
 *   run-time.
 *
 * Return Codes:
 *   DFX_K_SUCCESS - Adapter brought up successfully
 *   DFX_K_FAILURE - Adapter initialization failed
 *
 * Assumptions:
 *   bp->reset_type should be set to a valid reset type value before
 *   calling this routine.
 *
 * Side Effects:
 *   Adapter should be in LINK_AVAILABLE or LINK_UNAVAILABLE state
 *   upon a successful return of this routine.
 */

int dfx_adap_init(
	DFX_board_t *bp
	)

	{
	DBG_printk("In dfx_adap_init...\n");

	/* Disable PDQ interrupts first */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);

	/* Place adapter in DMA_UNAVAILABLE state by resetting adapter */

	if (dfx_hw_dma_uninit(bp, bp->reset_type) != DFX_K_SUCCESS)
		{
		printk("%s: Could not uninitialize/reset adapter!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/*
	 * When the PDQ is reset, some false Type 0 interrupts may be pending,
	 * so we'll acknowledge all Type 0 interrupts now before continuing.
	 */

	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_0_STATUS, PI_HOST_INT_K_ACK_ALL_TYPE_0);

	/*
	 * Clear Type 1 and Type 2 registers before going to DMA_AVAILABLE state
	 *
	 * Note: We only need to clear host copies of these registers.  The PDQ reset
	 *       takes care of the on-board register values.
	 */

	bp->cmd_req_reg.lword	= 0;
	bp->cmd_rsp_reg.lword	= 0;
	bp->rcv_xmt_reg.lword	= 0;

	/* Clear consumer block before going to DMA_AVAILABLE state */

	memset(bp->cons_block_virt, 0, sizeof(PI_CONSUMER_BLOCK));

	/* Initialize the DMA Burst Size */

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_SUB_CMD,
							PI_SUB_CMD_K_BURST_SIZE_SET,
							bp->burst_size,
							NULL) != DFX_K_SUCCESS)
		{
		printk("%s: Could not set adapter burst size!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/*
	 * Set base address of Consumer Block
	 *
	 * Assumption: 32-bit physical address of consumer block is 64 byte
	 *			   aligned.  That is, bits 0-5 of the address must be zero.
	 */

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_CONS_BLOCK,
							bp->cons_block_phys,
							0,
							NULL) != DFX_K_SUCCESS)
		{
		printk("%s: Could not set consumer block address!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/*
	 * Set base address of Descriptor Block and bring adapter to DMA_AVAILABLE state
	 *
	 * Note: We also set the literal and data swapping requirements in this
	 *	     command.  Since this driver presently runs on Intel platforms
	 *		 which are Little Endian, we'll tell the adapter to byte swap
	 *		 data only.  This code will need to change when we support
	 *		 Big Endian systems (eg. PowerPC).
	 *
	 * Assumption: 32-bit physical address of descriptor block is 8Kbyte
	 *             aligned.  That is, bits 0-12 of the address must be zero.
	 */

	if (dfx_hw_port_ctrl_req(bp,
							PI_PCTRL_M_INIT,
							(u32) (bp->descr_block_phys | PI_PDATA_A_INIT_M_BSWAP_DATA),
							0,
							NULL) != DFX_K_SUCCESS)
		{
		printk("%s: Could not set descriptor block address!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/* Set transmit flush timeout value */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_CHARS_SET;
	bp->cmd_req_virt->char_set.item[0].item_code	= PI_ITEM_K_FLUSH_TIME;
	bp->cmd_req_virt->char_set.item[0].value		= 3;	/* 3 seconds */
	bp->cmd_req_virt->char_set.item[0].item_index	= 0;
	bp->cmd_req_virt->char_set.item[1].item_code	= PI_ITEM_K_EOL;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		{
		printk("%s: DMA command request failed!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/* Set the initial values for eFDXEnable and MACTReq MIB objects */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_SNMP_SET;
	bp->cmd_req_virt->snmp_set.item[0].item_code	= PI_ITEM_K_FDX_ENB_DIS;
	bp->cmd_req_virt->snmp_set.item[0].value		= bp->full_duplex_enb;
	bp->cmd_req_virt->snmp_set.item[0].item_index	= 0;
	bp->cmd_req_virt->snmp_set.item[1].item_code	= PI_ITEM_K_MAC_T_REQ;
	bp->cmd_req_virt->snmp_set.item[1].value		= bp->req_ttrt;
	bp->cmd_req_virt->snmp_set.item[1].item_index	= 0;
	bp->cmd_req_virt->snmp_set.item[2].item_code	= PI_ITEM_K_EOL;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		{
		printk("%s: DMA command request failed!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/* Initialize adapter CAM */

	if (dfx_ctl_update_cam(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Adapter CAM update failed!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/* Initialize adapter filters */

	if (dfx_ctl_update_filters(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Adapter filters update failed!\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/* Initialize receive descriptor block and produce buffers */

	dfx_rcv_init(bp);

	/* Issue START command and bring adapter to LINK_(UN)AVAILABLE state */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_START;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Start command failed\n", bp->dev->name);
		return(DFX_K_FAILURE);
		}

	/* Initialization succeeded, reenable PDQ interrupts */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_ENABLE_DEF_INTS);
	return(DFX_K_SUCCESS);
	}


/*
 * ============
 * = dfx_open =
 * ============
 *   
 * Overview:
 *   Opens the adapter
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This function brings the adapter to an operational state.
 *
 * Return Codes:
 *   0		 - Adapter was successfully opened
 *   -EAGAIN - Could not register IRQ or adapter initialization failed
 *
 * Assumptions:
 *   This routine should only be called for a device that was
 *   initialized successfully during the dfx_probe process.
 *
 * Side Effects:
 *   Adapter should be in LINK_AVAILABLE or LINK_UNAVAILABLE state
 *   if the open is successful.
 */

int dfx_open(
	struct net_device *dev
	)

	{
	DFX_board_t	*bp = (DFX_board_t *)dev->priv;

	DBG_printk("In dfx_open...\n");

	/* Register IRQ - support shared interrupts by passing device ptr */

	if (request_irq(dev->irq, (void *)dfx_interrupt, SA_SHIRQ, dev->name, dev))
		{
		printk("%s: Requested IRQ %d is busy\n", dev->name, dev->irq);
		return(-EAGAIN);
		}

	/*
	 * Set current address to factory MAC address
	 *
	 * Note: We've already done this step in dfx_driver_init.
	 *       However, it's possible that a user has set a node
	 *		 address override, then closed and reopened the
	 *		 adapter.  Unless we reset the device address field
	 *		 now, we'll continue to use the existing modified
	 *		 address.
	 */

	memcpy(dev->dev_addr, bp->factory_mac_addr, FDDI_K_ALEN);

	/* Clear local unicast/multicast address tables and counts */

	memset(bp->uc_table, 0, sizeof(bp->uc_table));
	memset(bp->mc_table, 0, sizeof(bp->mc_table));
	bp->uc_count = 0;
	bp->mc_count = 0;

	/* Disable promiscuous filter settings */

	bp->ind_group_prom	= PI_FSTATE_K_BLOCK;
	bp->group_prom		= PI_FSTATE_K_BLOCK;

	/* Reset and initialize adapter */

	bp->reset_type = PI_PDATA_A_RESET_M_SKIP_ST;	/* skip self-test */
	if (dfx_adap_init(bp) != DFX_K_SUCCESS)
		{
		printk("%s: Adapter open failed!\n", dev->name);
		return(-EAGAIN);
		}

	/* Set device structure info */

	dev->tbusy			= 0;
	dev->interrupt		= DFX_UNMASK_INTERRUPTS;
	dev->start			= 1;
	return(0);
	}


/*
 * =============
 * = dfx_close =
 * =============
 *   
 * Overview:
 *   Closes the device/module.
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This routine closes the adapter and brings it to a safe state.
 *   The interrupt service routine is deregistered with the OS.
 *   The adapter can be opened again with another call to dfx_open().
 *
 * Return Codes:
 *   Always return 0.
 *
 * Assumptions:
 *   No further requests for this adapter are made after this routine is
 *   called.  dfx_open() can be called to reset and reinitialize the
 *   adapter.
 *
 * Side Effects:
 *   Adapter should be in DMA_UNAVAILABLE state upon completion of this
 *   routine.
 */

int dfx_close(
	struct net_device *dev
	)

	{
	DFX_board_t	*bp = (DFX_board_t *)dev->priv;

	DBG_printk("In dfx_close...\n");

	/* Disable PDQ interrupts first */

	dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);

	/* Place adapter in DMA_UNAVAILABLE state by resetting adapter */

	(void) dfx_hw_dma_uninit(bp, PI_PDATA_A_RESET_M_SKIP_ST);

	/*
	 * Flush any pending transmit buffers
	 *
	 * Note: It's important that we flush the transmit buffers
	 *		 BEFORE we clear our copy of the Type 2 register.
	 *		 Otherwise, we'll have no idea how many buffers
	 *		 we need to free.
	 */

	dfx_xmt_flush(bp);

	/*
	 * Clear Type 1 and Type 2 registers after adapter reset
	 *
	 * Note: Even though we're closing the adapter, it's
	 *       possible that an interrupt will occur after
	 *		 dfx_close is called.  Without some assurance to
	 *		 the contrary we want to make sure that we don't
	 *		 process receive and transmit LLC frames and update
	 *		 the Type 2 register with bad information.
	 */

	bp->cmd_req_reg.lword	= 0;
	bp->cmd_rsp_reg.lword	= 0;
	bp->rcv_xmt_reg.lword	= 0;

	/* Clear consumer block for the same reason given above */

	memset(bp->cons_block_virt, 0, sizeof(PI_CONSUMER_BLOCK));

	/* Clear device structure flags */

	dev->start = 0;
	dev->tbusy = 1;

	/* Deregister (free) IRQ */

	free_irq(dev->irq, dev);
	return(0);
	}


/*
 * ======================
 * = dfx_int_pr_halt_id =
 * ======================
 *   
 * Overview:
 *   Displays halt id's in string form.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Determine current halt id and display appropriate string.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

void dfx_int_pr_halt_id(
	DFX_board_t	*bp
	)

	{
	PI_UINT32	port_status;			/* PDQ port status register value */
	PI_UINT32	halt_id;				/* PDQ port status halt ID */

	/* Read the latest port status */

	dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &port_status);

	/* Display halt state transition information */

	halt_id = (port_status & PI_PSTATUS_M_HALT_ID) >> PI_PSTATUS_V_HALT_ID;
	switch (halt_id)
		{
		case PI_HALT_ID_K_SELFTEST_TIMEOUT:
			printk("%s: Halt ID: Selftest Timeout\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_PARITY_ERROR:
			printk("%s: Halt ID: Host Bus Parity Error\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_HOST_DIR_HALT:
			printk("%s: Halt ID: Host-Directed Halt\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_SW_FAULT:
			printk("%s: Halt ID: Adapter Software Fault\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_HW_FAULT:
			printk("%s: Halt ID: Adapter Hardware Fault\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_PC_TRACE:
			printk("%s: Halt ID: FDDI Network PC Trace Path Test\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_DMA_ERROR:
			printk("%s: Halt ID: Adapter DMA Error\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_IMAGE_CRC_ERROR:
			printk("%s: Halt ID: Firmware Image CRC Error\n", bp->dev->name);
			break;

		case PI_HALT_ID_K_BUS_EXCEPTION:
			printk("%s: Halt ID: 68000 Bus Exception\n", bp->dev->name);
			break;

		default:
			printk("%s: Halt ID: Unknown (code = %X)\n", bp->dev->name, halt_id);
			break;
		}
	return;
	}


/*
 * ==========================
 * = dfx_int_type_0_process =
 * ==========================
 *   
 * Overview:
 *   Processes Type 0 interrupts.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Processes all enabled Type 0 interrupts.  If the reason for the interrupt
 *   is a serious fault on the adapter, then an error message is displayed
 *   and the adapter is reset.
 *
 *   One tricky potential timing window is the rapid succession of "link avail"
 *   "link unavail" state change interrupts.  The acknowledgement of the Type 0
 *   interrupt must be done before reading the state from the Port Status
 *   register.  This is true because a state change could occur after reading
 *   the data, but before acknowledging the interrupt.  If this state change
 *   does happen, it would be lost because the driver is using the old state,
 *   and it will never know about the new state because it subsequently
 *   acknowledges the state change interrupt.
 *
 *          INCORRECT                                      CORRECT
 *      read type 0 int reasons                   read type 0 int reasons
 *      read adapter state                        ack type 0 interrupts
 *      ack type 0 interrupts                     read adapter state
 *      ... process interrupt ...                 ... process interrupt ...
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   An adapter reset may occur if the adapter has any Type 0 error interrupts
 *   or if the port status indicates that the adapter is halted.  The driver
 *   is responsible for reinitializing the adapter with the current CAM
 *   contents and adapter filter settings.
 */

void dfx_int_type_0_process(
	DFX_board_t	*bp
	)

	{
	PI_UINT32	type_0_status;		/* Host Interrupt Type 0 register */
	PI_UINT32	state;				/* current adap state (from port status) */

	/*
	 * Read host interrupt Type 0 register to determine which Type 0
	 * interrupts are pending.  Immediately write it back out to clear
	 * those interrupts.
	 */

	dfx_port_read_long(bp, PI_PDQ_K_REG_TYPE_0_STATUS, &type_0_status);
	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_0_STATUS, type_0_status);

	/* Check for Type 0 error interrupts */

	if (type_0_status & (PI_TYPE_0_STAT_M_NXM |
							PI_TYPE_0_STAT_M_PM_PAR_ERR |
							PI_TYPE_0_STAT_M_BUS_PAR_ERR))
		{
		/* Check for Non-Existent Memory error */

		if (type_0_status & PI_TYPE_0_STAT_M_NXM)
			printk("%s: Non-Existent Memory Access Error\n", bp->dev->name);

		/* Check for Packet Memory Parity error */

		if (type_0_status & PI_TYPE_0_STAT_M_PM_PAR_ERR)
			printk("%s: Packet Memory Parity Error\n", bp->dev->name);

		/* Check for Host Bus Parity error */

		if (type_0_status & PI_TYPE_0_STAT_M_BUS_PAR_ERR)
			printk("%s: Host Bus Parity Error\n", bp->dev->name);

		/* Reset adapter and bring it back on-line */

		bp->link_available = PI_K_FALSE;	/* link is no longer available */
		bp->reset_type = 0;					/* rerun on-board diagnostics */
		printk("%s: Resetting adapter...\n", bp->dev->name);
		if (dfx_adap_init(bp) != DFX_K_SUCCESS)
			{
			printk("%s: Adapter reset failed!  Disabling adapter interrupts.\n", bp->dev->name);
			dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);
			return;
			}
		printk("%s: Adapter reset successful!\n", bp->dev->name);
		return;
		}

	/* Check for transmit flush interrupt */

	if (type_0_status & PI_TYPE_0_STAT_M_XMT_FLUSH)
		{
		/* Flush any pending xmt's and acknowledge the flush interrupt */

		bp->link_available = PI_K_FALSE;		/* link is no longer available */
		dfx_xmt_flush(bp);						/* flush any outstanding packets */
		(void) dfx_hw_port_ctrl_req(bp,
									PI_PCTRL_M_XMT_DATA_FLUSH_DONE,
									0,
									0,
									NULL);
		}

	/* Check for adapter state change */

	if (type_0_status & PI_TYPE_0_STAT_M_STATE_CHANGE)
		{                     
		/* Get latest adapter state */

		state = dfx_hw_adap_state_rd(bp);	/* get adapter state */
		if (state == PI_STATE_K_HALTED)
			{
			/*
			 * Adapter has transitioned to HALTED state, try to reset
			 * adapter to bring it back on-line.  If reset fails,
			 * leave the adapter in the broken state.
			 */

			printk("%s: Controller has transitioned to HALTED state!\n", bp->dev->name);
			dfx_int_pr_halt_id(bp);			/* display halt id as string */

			/* Reset adapter and bring it back on-line */

			bp->link_available = PI_K_FALSE;	/* link is no longer available */
			bp->reset_type = 0;					/* rerun on-board diagnostics */
			printk("%s: Resetting adapter...\n", bp->dev->name);
			if (dfx_adap_init(bp) != DFX_K_SUCCESS)
				{
				printk("%s: Adapter reset failed!  Disabling adapter interrupts.\n", bp->dev->name);
				dfx_port_write_long(bp, PI_PDQ_K_REG_HOST_INT_ENB, PI_HOST_INT_K_DISABLE_ALL_INTS);
				return;
				}
			printk("%s: Adapter reset successful!\n", bp->dev->name);
			}
		else if (state == PI_STATE_K_LINK_AVAIL)
			{
			bp->link_available = PI_K_TRUE;		/* set link available flag */
			}
		}
	return;
	}


/*
 * ==================
 * = dfx_int_common =
 * ==================
 *   
 * Overview:
 *   Interrupt service routine (ISR)
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   This is the ISR which processes incoming adapter interrupts.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   This routine assumes PDQ interrupts have not been disabled.
 *   When interrupts are disabled at the PDQ, the Port Status register
 *   is automatically cleared.  This routine uses the Port Status
 *   register value to determine whether a Type 0 interrupt occurred,
 *   so it's important that adapter interrupts are not normally
 *   enabled/disabled at the PDQ.
 *
 *   It's vital that this routine is NOT reentered for the
 *   same board and that the OS is not in another section of
 *   code (eg. dfx_xmt_queue_pkt) for the same board on a
 *   different thread.
 *
 * Side Effects:
 *   Pending interrupts are serviced.  Depending on the type of
 *   interrupt, acknowledging and clearing the interrupt at the
 *   PDQ involves writing a register to clear the interrupt bit
 *   or updating completion indices.
 */

void dfx_int_common(
	DFX_board_t	*bp
	)

	{
	PI_UINT32	port_status;		/* Port Status register */

	/* Process xmt interrupts - frequent case, so always call this routine */

	dfx_xmt_done(bp);				/* free consumed xmt packets */

	/* Process rcv interrupts - frequent case, so always call this routine */

	dfx_rcv_queue_process(bp);		/* service received LLC frames */

	/*
	 * Transmit and receive producer and completion indices are updated on the
	 * adapter by writing to the Type 2 Producer register.  Since the frequent
	 * case is that we'll be processing either LLC transmit or receive buffers,
	 * we'll optimize I/O writes by doing a single register write here.
	 */

	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_2_PROD, bp->rcv_xmt_reg.lword);

	/* Read PDQ Port Status register to find out which interrupts need processing */

	dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &port_status);

	/* Process Type 0 interrupts (if any) - infrequent, so only call when needed */

	if (port_status & PI_PSTATUS_M_TYPE_0_PENDING)
		dfx_int_type_0_process(bp);	/* process Type 0 interrupts */
	return;
	}


/*
 * =================
 * = dfx_interrupt =
 * =================
 *   
 * Overview:
 *   Interrupt processing routine
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   irq	- interrupt vector
 *   dev_id	- pointer to device information
 *	 regs	- pointer to registers structure
 *
 * Functional Description:
 *   This routine calls the interrupt processing routine for this adapter.  It
 *   disables and reenables adapter interrupts, as appropriate.  We can support
 *   shared interrupts since the incoming dev_id pointer provides our device
 *   structure context.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   The interrupt acknowledgement at the hardware level (eg. ACKing the PIC
 *   on Intel-based systems) is done by the operating system outside this
 *   routine.
 *
 *	 System interrupts are enabled through this call.
 *
 * Side Effects:
 *   Interrupts are disabled, then reenabled at the adapter.
 */

void dfx_interrupt(
	int				irq,
	void			*dev_id,
	struct pt_regs	*regs
	)

	{
	struct net_device	*dev = (struct net_device *) dev_id;
	DFX_board_t		*bp;	/* private board structure pointer */
	u8				tmp;	/* used for disabling/enabling ints */

	/* Get board pointer only if device structure is valid */

	if (dev == NULL)
		{
		printk("dfx_interrupt(): irq %d for unknown device!\n", irq);
		return;
		}
	bp = (DFX_board_t *) dev->priv;

	spin_lock(&bp->lock);
	
	/* See if we're already servicing an interrupt */

	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler!\n", dev->name);
	dev->interrupt = DFX_MASK_INTERRUPTS;	/* ensure non reentrancy */

	/* Service adapter interrupts */

	if (bp->bus_type == DFX_BUS_TYPE_PCI)
		{
		/* Disable PDQ-PFI interrupts at PFI */

		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL, PFI_MODE_M_DMA_ENB);

		/* Call interrupt service routine for this adapter */

		dfx_int_common(bp);

		/* Clear PDQ interrupt status bit and reenable interrupts */

		dfx_port_write_long(bp, PFI_K_REG_STATUS, PFI_STATUS_M_PDQ_INT);
		dfx_port_write_long(bp, PFI_K_REG_MODE_CTRL,
					(PFI_MODE_M_PDQ_INT_ENB + PFI_MODE_M_DMA_ENB));
		}
	else
		{
		/* Disable interrupts at the ESIC */

		dfx_port_read_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, &tmp);
		tmp &= ~PI_CONFIG_STAT_0_M_INT_ENB;
		dfx_port_write_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, tmp);

		/* Call interrupt service routine for this adapter */

		dfx_int_common(bp);

		/* Reenable interrupts at the ESIC */

		dfx_port_read_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, &tmp);
		tmp |= PI_CONFIG_STAT_0_M_INT_ENB;
		dfx_port_write_byte(bp, PI_ESIC_K_IO_CONFIG_STAT_0, tmp);
		}

	dev->interrupt = DFX_UNMASK_INTERRUPTS;
	spin_unlock(&bp->lock);
	return;
	}


/*
 * =====================
 * = dfx_ctl_get_stats =
 * =====================
 *   
 * Overview:
 *   Get statistics for FDDI adapter
 *  
 * Returns:
 *   Pointer to FDDI statistics structure
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Gets current MIB objects from adapter, then
 *   returns FDDI statistics structure as defined
 *   in if_fddi.h.
 *
 *   Note: Since the FDDI statistics structure is
 *   still new and the device structure doesn't
 *   have an FDDI-specific get statistics handler,
 *   we'll return the FDDI statistics structure as
 *   a pointer to an Ethernet statistics structure.
 *   That way, at least the first part of the statistics
 *   structure can be decoded properly, and it allows
 *   "smart" applications to perform a second cast to
 *   decode the FDDI-specific statistics.
 *
 *   We'll have to pay attention to this routine as the
 *   device structure becomes more mature and LAN media
 *   independent.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

struct net_device_stats *dfx_ctl_get_stats(
	struct net_device *dev
	)

	{
	DFX_board_t	*bp = (DFX_board_t *)dev->priv;

	/* Fill the bp->stats structure with driver-maintained counters */

	bp->stats.rx_packets			= bp->rcv_total_frames;
	bp->stats.tx_packets			= bp->xmt_total_frames;
	bp->stats.rx_bytes			= bp->rcv_total_bytes;
	bp->stats.tx_bytes			= bp->xmt_total_bytes;
	bp->stats.rx_errors				= (u32)(bp->rcv_crc_errors + bp->rcv_frame_status_errors + bp->rcv_length_errors);
	bp->stats.tx_errors				= bp->xmt_length_errors;
	bp->stats.rx_dropped			= bp->rcv_discards;
	bp->stats.tx_dropped			= bp->xmt_discards;
	bp->stats.multicast				= bp->rcv_multicast_frames;
	bp->stats.transmit_collision	= 0;	/* always zero (0) for FDDI */

	/* Get FDDI SMT MIB objects */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_SMT_MIB_GET;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return((struct net_device_stats *) &bp->stats);

	/* Fill the bp->stats structure with the SMT MIB object values */

	memcpy(bp->stats.smt_station_id, &bp->cmd_rsp_virt->smt_mib_get.smt_station_id, sizeof(bp->cmd_rsp_virt->smt_mib_get.smt_station_id));
	bp->stats.smt_op_version_id					= bp->cmd_rsp_virt->smt_mib_get.smt_op_version_id;
	bp->stats.smt_hi_version_id					= bp->cmd_rsp_virt->smt_mib_get.smt_hi_version_id;
	bp->stats.smt_lo_version_id					= bp->cmd_rsp_virt->smt_mib_get.smt_lo_version_id;
	memcpy(bp->stats.smt_user_data, &bp->cmd_rsp_virt->smt_mib_get.smt_user_data, sizeof(bp->cmd_rsp_virt->smt_mib_get.smt_user_data));
	bp->stats.smt_mib_version_id				= bp->cmd_rsp_virt->smt_mib_get.smt_mib_version_id;
	bp->stats.smt_mac_cts						= bp->cmd_rsp_virt->smt_mib_get.smt_mac_ct;
	bp->stats.smt_non_master_cts				= bp->cmd_rsp_virt->smt_mib_get.smt_non_master_ct;
	bp->stats.smt_master_cts					= bp->cmd_rsp_virt->smt_mib_get.smt_master_ct;
	bp->stats.smt_available_paths				= bp->cmd_rsp_virt->smt_mib_get.smt_available_paths;
	bp->stats.smt_config_capabilities			= bp->cmd_rsp_virt->smt_mib_get.smt_config_capabilities;
	bp->stats.smt_config_policy					= bp->cmd_rsp_virt->smt_mib_get.smt_config_policy;
	bp->stats.smt_connection_policy				= bp->cmd_rsp_virt->smt_mib_get.smt_connection_policy;
	bp->stats.smt_t_notify						= bp->cmd_rsp_virt->smt_mib_get.smt_t_notify;
	bp->stats.smt_stat_rpt_policy				= bp->cmd_rsp_virt->smt_mib_get.smt_stat_rpt_policy;
	bp->stats.smt_trace_max_expiration			= bp->cmd_rsp_virt->smt_mib_get.smt_trace_max_expiration;
	bp->stats.smt_bypass_present				= bp->cmd_rsp_virt->smt_mib_get.smt_bypass_present;
	bp->stats.smt_ecm_state						= bp->cmd_rsp_virt->smt_mib_get.smt_ecm_state;
	bp->stats.smt_cf_state						= bp->cmd_rsp_virt->smt_mib_get.smt_cf_state;
	bp->stats.smt_remote_disconnect_flag		= bp->cmd_rsp_virt->smt_mib_get.smt_remote_disconnect_flag;
	bp->stats.smt_station_status				= bp->cmd_rsp_virt->smt_mib_get.smt_station_status;
	bp->stats.smt_peer_wrap_flag				= bp->cmd_rsp_virt->smt_mib_get.smt_peer_wrap_flag;
	bp->stats.smt_time_stamp					= bp->cmd_rsp_virt->smt_mib_get.smt_msg_time_stamp.ls;
	bp->stats.smt_transition_time_stamp			= bp->cmd_rsp_virt->smt_mib_get.smt_transition_time_stamp.ls;
	bp->stats.mac_frame_status_functions		= bp->cmd_rsp_virt->smt_mib_get.mac_frame_status_functions;
	bp->stats.mac_t_max_capability				= bp->cmd_rsp_virt->smt_mib_get.mac_t_max_capability;
	bp->stats.mac_tvx_capability				= bp->cmd_rsp_virt->smt_mib_get.mac_tvx_capability;
	bp->stats.mac_available_paths				= bp->cmd_rsp_virt->smt_mib_get.mac_available_paths;
	bp->stats.mac_current_path					= bp->cmd_rsp_virt->smt_mib_get.mac_current_path;
	memcpy(bp->stats.mac_upstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_upstream_nbr, FDDI_K_ALEN);
	memcpy(bp->stats.mac_downstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_downstream_nbr, FDDI_K_ALEN);
	memcpy(bp->stats.mac_old_upstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_old_upstream_nbr, FDDI_K_ALEN);
	memcpy(bp->stats.mac_old_downstream_nbr, &bp->cmd_rsp_virt->smt_mib_get.mac_old_downstream_nbr, FDDI_K_ALEN);
	bp->stats.mac_dup_address_test				= bp->cmd_rsp_virt->smt_mib_get.mac_dup_address_test;
	bp->stats.mac_requested_paths				= bp->cmd_rsp_virt->smt_mib_get.mac_requested_paths;
	bp->stats.mac_downstream_port_type			= bp->cmd_rsp_virt->smt_mib_get.mac_downstream_port_type;
	memcpy(bp->stats.mac_smt_address, &bp->cmd_rsp_virt->smt_mib_get.mac_smt_address, FDDI_K_ALEN);
	bp->stats.mac_t_req							= bp->cmd_rsp_virt->smt_mib_get.mac_t_req;
	bp->stats.mac_t_neg							= bp->cmd_rsp_virt->smt_mib_get.mac_t_neg;
	bp->stats.mac_t_max							= bp->cmd_rsp_virt->smt_mib_get.mac_t_max;
	bp->stats.mac_tvx_value						= bp->cmd_rsp_virt->smt_mib_get.mac_tvx_value;
	bp->stats.mac_frame_error_threshold			= bp->cmd_rsp_virt->smt_mib_get.mac_frame_error_threshold;
	bp->stats.mac_frame_error_ratio				= bp->cmd_rsp_virt->smt_mib_get.mac_frame_error_ratio;
	bp->stats.mac_rmt_state						= bp->cmd_rsp_virt->smt_mib_get.mac_rmt_state;
	bp->stats.mac_da_flag						= bp->cmd_rsp_virt->smt_mib_get.mac_da_flag;
	bp->stats.mac_una_da_flag					= bp->cmd_rsp_virt->smt_mib_get.mac_unda_flag;
	bp->stats.mac_frame_error_flag				= bp->cmd_rsp_virt->smt_mib_get.mac_frame_error_flag;
	bp->stats.mac_ma_unitdata_available			= bp->cmd_rsp_virt->smt_mib_get.mac_ma_unitdata_available;
	bp->stats.mac_hardware_present				= bp->cmd_rsp_virt->smt_mib_get.mac_hardware_present;
	bp->stats.mac_ma_unitdata_enable			= bp->cmd_rsp_virt->smt_mib_get.mac_ma_unitdata_enable;
	bp->stats.path_tvx_lower_bound				= bp->cmd_rsp_virt->smt_mib_get.path_tvx_lower_bound;
	bp->stats.path_t_max_lower_bound			= bp->cmd_rsp_virt->smt_mib_get.path_t_max_lower_bound;
	bp->stats.path_max_t_req					= bp->cmd_rsp_virt->smt_mib_get.path_max_t_req;
	memcpy(bp->stats.path_configuration, &bp->cmd_rsp_virt->smt_mib_get.path_configuration, sizeof(bp->cmd_rsp_virt->smt_mib_get.path_configuration));
	bp->stats.port_my_type[0]					= bp->cmd_rsp_virt->smt_mib_get.port_my_type[0];
	bp->stats.port_my_type[1]					= bp->cmd_rsp_virt->smt_mib_get.port_my_type[1];
	bp->stats.port_neighbor_type[0]				= bp->cmd_rsp_virt->smt_mib_get.port_neighbor_type[0];
	bp->stats.port_neighbor_type[1]				= bp->cmd_rsp_virt->smt_mib_get.port_neighbor_type[1];
	bp->stats.port_connection_policies[0]		= bp->cmd_rsp_virt->smt_mib_get.port_connection_policies[0];
	bp->stats.port_connection_policies[1]		= bp->cmd_rsp_virt->smt_mib_get.port_connection_policies[1];
	bp->stats.port_mac_indicated[0]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_indicated[0];
	bp->stats.port_mac_indicated[1]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_indicated[1];
	bp->stats.port_current_path[0]				= bp->cmd_rsp_virt->smt_mib_get.port_current_path[0];
	bp->stats.port_current_path[1]				= bp->cmd_rsp_virt->smt_mib_get.port_current_path[1];
	memcpy(&bp->stats.port_requested_paths[0*3], &bp->cmd_rsp_virt->smt_mib_get.port_requested_paths[0], 3);
	memcpy(&bp->stats.port_requested_paths[1*3], &bp->cmd_rsp_virt->smt_mib_get.port_requested_paths[1], 3);
	bp->stats.port_mac_placement[0]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_placement[0];
	bp->stats.port_mac_placement[1]				= bp->cmd_rsp_virt->smt_mib_get.port_mac_placement[1];
	bp->stats.port_available_paths[0]			= bp->cmd_rsp_virt->smt_mib_get.port_available_paths[0];
	bp->stats.port_available_paths[1]			= bp->cmd_rsp_virt->smt_mib_get.port_available_paths[1];
	bp->stats.port_pmd_class[0]					= bp->cmd_rsp_virt->smt_mib_get.port_pmd_class[0];
	bp->stats.port_pmd_class[1]					= bp->cmd_rsp_virt->smt_mib_get.port_pmd_class[1];
	bp->stats.port_connection_capabilities[0]	= bp->cmd_rsp_virt->smt_mib_get.port_connection_capabilities[0];
	bp->stats.port_connection_capabilities[1]	= bp->cmd_rsp_virt->smt_mib_get.port_connection_capabilities[1];
	bp->stats.port_bs_flag[0]					= bp->cmd_rsp_virt->smt_mib_get.port_bs_flag[0];
	bp->stats.port_bs_flag[1]					= bp->cmd_rsp_virt->smt_mib_get.port_bs_flag[1];
	bp->stats.port_ler_estimate[0]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_estimate[0];
	bp->stats.port_ler_estimate[1]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_estimate[1];
	bp->stats.port_ler_cutoff[0]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_cutoff[0];
	bp->stats.port_ler_cutoff[1]				= bp->cmd_rsp_virt->smt_mib_get.port_ler_cutoff[1];
	bp->stats.port_ler_alarm[0]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_alarm[0];
	bp->stats.port_ler_alarm[1]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_alarm[1];
	bp->stats.port_connect_state[0]				= bp->cmd_rsp_virt->smt_mib_get.port_connect_state[0];
	bp->stats.port_connect_state[1]				= bp->cmd_rsp_virt->smt_mib_get.port_connect_state[1];
	bp->stats.port_pcm_state[0]					= bp->cmd_rsp_virt->smt_mib_get.port_pcm_state[0];
	bp->stats.port_pcm_state[1]					= bp->cmd_rsp_virt->smt_mib_get.port_pcm_state[1];
	bp->stats.port_pc_withhold[0]				= bp->cmd_rsp_virt->smt_mib_get.port_pc_withhold[0];
	bp->stats.port_pc_withhold[1]				= bp->cmd_rsp_virt->smt_mib_get.port_pc_withhold[1];
	bp->stats.port_ler_flag[0]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_flag[0];
	bp->stats.port_ler_flag[1]					= bp->cmd_rsp_virt->smt_mib_get.port_ler_flag[1];
	bp->stats.port_hardware_present[0]			= bp->cmd_rsp_virt->smt_mib_get.port_hardware_present[0];
	bp->stats.port_hardware_present[1]			= bp->cmd_rsp_virt->smt_mib_get.port_hardware_present[1];

	/* Get FDDI counters */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_CNTRS_GET;
	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return((struct net_device_stats *) &bp->stats);

	/* Fill the bp->stats structure with the FDDI counter values */

	bp->stats.mac_frame_cts				= bp->cmd_rsp_virt->cntrs_get.cntrs.frame_cnt.ls;
	bp->stats.mac_copied_cts			= bp->cmd_rsp_virt->cntrs_get.cntrs.copied_cnt.ls;
	bp->stats.mac_transmit_cts			= bp->cmd_rsp_virt->cntrs_get.cntrs.transmit_cnt.ls;
	bp->stats.mac_error_cts				= bp->cmd_rsp_virt->cntrs_get.cntrs.error_cnt.ls;
	bp->stats.mac_lost_cts				= bp->cmd_rsp_virt->cntrs_get.cntrs.lost_cnt.ls;
	bp->stats.port_lct_fail_cts[0]		= bp->cmd_rsp_virt->cntrs_get.cntrs.lct_rejects[0].ls;
	bp->stats.port_lct_fail_cts[1]		= bp->cmd_rsp_virt->cntrs_get.cntrs.lct_rejects[1].ls;
	bp->stats.port_lem_reject_cts[0]	= bp->cmd_rsp_virt->cntrs_get.cntrs.lem_rejects[0].ls;
	bp->stats.port_lem_reject_cts[1]	= bp->cmd_rsp_virt->cntrs_get.cntrs.lem_rejects[1].ls;
	bp->stats.port_lem_cts[0]			= bp->cmd_rsp_virt->cntrs_get.cntrs.link_errors[0].ls;
	bp->stats.port_lem_cts[1]			= bp->cmd_rsp_virt->cntrs_get.cntrs.link_errors[1].ls;

	return((struct net_device_stats *) &bp->stats);
	}


/*
 * ==============================
 * = dfx_ctl_set_multicast_list =
 * ==============================
 *   
 * Overview:
 *   Enable/Disable LLC frame promiscuous mode reception
 *   on the adapter and/or update multicast address table.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   dev - pointer to device information
 *
 * Functional Description:
 *   This routine follows a fairly simple algorithm for setting the
 *   adapter filters and CAM:
 *
 *		if IFF_PROMISC flag is set
 *			enable LLC individual/group promiscuous mode
 *		else
 *			disable LLC individual/group promiscuous mode
 *			if number of incoming multicast addresses >
 *					(CAM max size - number of unicast addresses in CAM)
 *				enable LLC group promiscuous mode
 *				set driver-maintained multicast address count to zero
 *			else
 *				disable LLC group promiscuous mode
 *				set driver-maintained multicast address count to incoming count
 *			update adapter CAM
 *		update adapter filters
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   Multicast addresses are presented in canonical (LSB) format.
 *
 * Side Effects:
 *   On-board adapter CAM and filters are updated.
 */

void dfx_ctl_set_multicast_list(
	struct net_device *dev
	)

	{
	DFX_board_t			*bp = (DFX_board_t *)dev->priv;
	int					i;			/* used as index in for loop */
	struct dev_mc_list	*dmi;		/* ptr to multicast addr entry */

	/* Enable LLC frame promiscuous mode, if necessary */

	if (dev->flags & IFF_PROMISC)
		bp->ind_group_prom = PI_FSTATE_K_PASS;		/* Enable LLC ind/group prom mode */

	/* Else, update multicast address table */

	else
		{
		bp->ind_group_prom = PI_FSTATE_K_BLOCK;		/* Disable LLC ind/group prom mode */
		/*
		 * Check whether incoming multicast address count exceeds table size
		 *
		 * Note: The adapters utilize an on-board 64 entry CAM for
		 *       supporting perfect filtering of multicast packets
		 *		 and bridge functions when adding unicast addresses.
		 *		 There is no hash function available.  To support
		 *		 additional multicast addresses, the all multicast
		 *		 filter (LLC group promiscuous mode) must be enabled.
		 *
		 *		 The firmware reserves two CAM entries for SMT-related
		 *		 multicast addresses, which leaves 62 entries available.
		 *		 The following code ensures that we're not being asked
		 *		 to add more than 62 addresses to the CAM.  If we are,
		 *		 the driver will enable the all multicast filter.
		 *		 Should the number of multicast addresses drop below
		 *		 the high water mark, the filter will be disabled and
		 *		 perfect filtering will be used.
		 */

		if (dev->mc_count > (PI_CMD_ADDR_FILTER_K_SIZE - bp->uc_count))
			{
			bp->group_prom	= PI_FSTATE_K_PASS;		/* Enable LLC group prom mode */
			bp->mc_count	= 0;					/* Don't add mc addrs to CAM */
			}
		else
			{
			bp->group_prom	= PI_FSTATE_K_BLOCK;	/* Disable LLC group prom mode */
			bp->mc_count	= dev->mc_count;		/* Add mc addrs to CAM */
			}

		/* Copy addresses to multicast address table, then update adapter CAM */

		dmi = dev->mc_list;				/* point to first multicast addr */
		for (i=0; i < bp->mc_count; i++)
			{
			memcpy(&bp->mc_table[i*FDDI_K_ALEN], dmi->dmi_addr, FDDI_K_ALEN);
			dmi = dmi->next;			/* point to next multicast addr */
			}
		if (dfx_ctl_update_cam(bp) != DFX_K_SUCCESS)
			{
			DBG_printk("%s: Could not update multicast address table!\n", dev->name);
			}
		else
			{
			DBG_printk("%s: Multicast address table updated!  Added %d addresses.\n", dev->name, bp->mc_count);
			}
		}

	/* Update adapter filters */

	if (dfx_ctl_update_filters(bp) != DFX_K_SUCCESS)
		{
		DBG_printk("%s: Could not update adapter filters!\n", dev->name);
		}
	else
		{
		DBG_printk("%s: Adapter filters updated!\n", dev->name);
		}
	return;
	}


/*
 * ===========================
 * = dfx_ctl_set_mac_address =
 * ===========================
 *   
 * Overview:
 *   Add node address override (unicast address) to adapter
 *   CAM and update dev_addr field in device table.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   dev  - pointer to device information
 *   addr - pointer to sockaddr structure containing unicast address to add
 *
 * Functional Description:
 *   The adapter supports node address overrides by adding one or more
 *   unicast addresses to the adapter CAM.  This is similar to adding
 *   multicast addresses.  In this routine we'll update the driver and
 *   device structures with the new address, then update the adapter CAM
 *   to ensure that the adapter will copy and strip frames destined and
 *   sourced by that address.
 *
 * Return Codes:
 *   Always returns zero.
 *
 * Assumptions:
 *   The address pointed to by addr->sa_data is a valid unicast
 *   address and is presented in canonical (LSB) format.
 *
 * Side Effects:
 *   On-board adapter CAM is updated.  On-board adapter filters
 *   may be updated.
 */

int dfx_ctl_set_mac_address(
	struct net_device	*dev,
	void			*addr
	)

	{
	DFX_board_t		*bp = (DFX_board_t *)dev->priv;
	struct sockaddr	*p_sockaddr = (struct sockaddr *)addr;

	/* Copy unicast address to driver-maintained structs and update count */

	memcpy(dev->dev_addr, p_sockaddr->sa_data, FDDI_K_ALEN);	/* update device struct */
	memcpy(&bp->uc_table[0], p_sockaddr->sa_data, FDDI_K_ALEN);	/* update driver struct */
	bp->uc_count = 1;

	/*
	 * Verify we're not exceeding the CAM size by adding unicast address
	 *
	 * Note: It's possible that before entering this routine we've
	 *       already filled the CAM with 62 multicast addresses.
	 *		 Since we need to place the node address override into
	 *		 the CAM, we have to check to see that we're not
	 *		 exceeding the CAM size.  If we are, we have to enable
	 *		 the LLC group (multicast) promiscuous mode filter as
	 *		 in dfx_ctl_set_multicast_list.
	 */

	if ((bp->uc_count + bp->mc_count) > PI_CMD_ADDR_FILTER_K_SIZE)
		{
		bp->group_prom	= PI_FSTATE_K_PASS;		/* Enable LLC group prom mode */
		bp->mc_count	= 0;					/* Don't add mc addrs to CAM */

		/* Update adapter filters */

		if (dfx_ctl_update_filters(bp) != DFX_K_SUCCESS)
			{
			DBG_printk("%s: Could not update adapter filters!\n", dev->name);
			}
		else
			{
			DBG_printk("%s: Adapter filters updated!\n", dev->name);
			}
		}

	/* Update adapter CAM with new unicast address */

	if (dfx_ctl_update_cam(bp) != DFX_K_SUCCESS)
		{
		DBG_printk("%s: Could not set new MAC address!\n", dev->name);
		}
	else
		{
		DBG_printk("%s: Adapter CAM updated with new MAC address\n", dev->name);
		}
	return(0);			/* always return zero */
	}


/*
 * ======================
 * = dfx_ctl_update_cam =
 * ======================
 *
 * Overview:
 *   Procedure to update adapter CAM (Content Addressable Memory)
 *   with desired unicast and multicast address entries.
 *
 * Returns:
 *   Condition code
 *
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Updates adapter CAM with current contents of board structure
 *   unicast and multicast address tables.  Since there are only 62
 *   free entries in CAM, this routine ensures that the command
 *   request buffer is not overrun.
 *
 * Return Codes:
 *   DFX_K_SUCCESS - Request succeeded
 *   DFX_K_FAILURE - Request failed
 *
 * Assumptions:
 *   All addresses being added (unicast and multicast) are in canonical
 *   order.
 *
 * Side Effects:
 *   On-board adapter CAM is updated.
 */

int dfx_ctl_update_cam(
	DFX_board_t *bp
	)

	{
	int			i;				/* used as index */
	PI_LAN_ADDR	*p_addr;		/* pointer to CAM entry */

	/*
	 * Fill in command request information
	 *
	 * Note: Even though both the unicast and multicast address
	 *       table entries are stored as contiguous 6 byte entries,
	 *		 the firmware address filter set command expects each
	 *		 entry to be two longwords (8 bytes total).  We must be
	 *		 careful to only copy the six bytes of each unicast and
	 *		 multicast table entry into each command entry.  This
	 *		 is also why we must first clear the entire command
	 *		 request buffer.
	 */

	memset(bp->cmd_req_virt, 0, PI_CMD_REQ_K_SIZE_MAX);	/* first clear buffer */
	bp->cmd_req_virt->cmd_type = PI_CMD_K_ADDR_FILTER_SET;
	p_addr = &bp->cmd_req_virt->addr_filter_set.entry[0];

	/* Now add unicast addresses to command request buffer, if any */

	for (i=0; i < (int)bp->uc_count; i++)
		{
		if (i < PI_CMD_ADDR_FILTER_K_SIZE)
			{
			memcpy(p_addr, &bp->uc_table[i*FDDI_K_ALEN], FDDI_K_ALEN);
			p_addr++;			/* point to next command entry */
			}
		}

	/* Now add multicast addresses to command request buffer, if any */

	for (i=0; i < (int)bp->mc_count; i++)
		{
		if ((i + bp->uc_count) < PI_CMD_ADDR_FILTER_K_SIZE)
			{
			memcpy(p_addr, &bp->mc_table[i*FDDI_K_ALEN], FDDI_K_ALEN);
			p_addr++;			/* point to next command entry */
			}
		}

	/* Issue command to update adapter CAM, then return */

	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return(DFX_K_FAILURE);
	return(DFX_K_SUCCESS);
	}


/*
 * ==========================
 * = dfx_ctl_update_filters =
 * ==========================
 *
 * Overview:
 *   Procedure to update adapter filters with desired
 *   filter settings.
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Enables or disables filter using current filter settings.
 *
 * Return Codes:
 *   DFX_K_SUCCESS - Request succeeded.
 *   DFX_K_FAILURE - Request failed.
 *
 * Assumptions:
 *   We must always pass up packets destined to the broadcast
 *   address (FF-FF-FF-FF-FF-FF), so we'll always keep the
 *   broadcast filter enabled.
 *
 * Side Effects:
 *   On-board adapter filters are updated.
 */

int dfx_ctl_update_filters(
	DFX_board_t *bp
	)

	{
	int	i = 0;					/* used as index */

	/* Fill in command request information */

	bp->cmd_req_virt->cmd_type = PI_CMD_K_FILTERS_SET;

	/* Initialize Broadcast filter - * ALWAYS ENABLED * */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_BROADCAST;
	bp->cmd_req_virt->filter_set.item[i++].value	= PI_FSTATE_K_PASS;

	/* Initialize LLC Individual/Group Promiscuous filter */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_IND_GROUP_PROM;
	bp->cmd_req_virt->filter_set.item[i++].value	= bp->ind_group_prom;

	/* Initialize LLC Group Promiscuous filter */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_GROUP_PROM;
	bp->cmd_req_virt->filter_set.item[i++].value	= bp->group_prom;

	/* Terminate the item code list */

	bp->cmd_req_virt->filter_set.item[i].item_code	= PI_ITEM_K_EOL;

	/* Issue command to update adapter filters, then return */

	if (dfx_hw_dma_cmd_req(bp) != DFX_K_SUCCESS)
		return(DFX_K_FAILURE);
	return(DFX_K_SUCCESS);
	}


/*
 * ======================
 * = dfx_hw_dma_cmd_req =
 * ======================
 *   
 * Overview:
 *   Sends PDQ DMA command to adapter firmware
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   The command request and response buffers are posted to the adapter in the manner
 *   described in the PDQ Port Specification:
 *
 *		1. Command Response Buffer is posted to adapter.
 *		2. Command Request Buffer is posted to adapter.
 *		3. Command Request consumer index is polled until it indicates that request
 *         buffer has been DMA'd to adapter.
 *		4. Command Response consumer index is polled until it indicates that response
 *         buffer has been DMA'd from adapter.
 *
 *   This ordering ensures that a response buffer is already available for the firmware
 *   to use once it's done processing the request buffer.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	  - DMA command succeeded
 * 	 DFX_K_OUTSTATE   - Adapter is NOT in proper state
 *   DFX_K_HW_TIMEOUT - DMA command timed out
 *
 * Assumptions:
 *   Command request buffer has already been filled with desired DMA command.
 *
 * Side Effects:
 *   None
 */

int dfx_hw_dma_cmd_req(
	DFX_board_t *bp
	)

	{
	int status;			/* adapter status */
	int timeout_cnt;	/* used in for loops */
	
	/* Make sure the adapter is in a state that we can issue the DMA command in */
	
	status = dfx_hw_adap_state_rd(bp);
	if ((status == PI_STATE_K_RESET)		||
		(status == PI_STATE_K_HALTED)		||
		(status == PI_STATE_K_DMA_UNAVAIL)	||
		(status == PI_STATE_K_UPGRADE))
		return(DFX_K_OUTSTATE);

	/* Put response buffer on the command response queue */

	bp->descr_block_virt->cmd_rsp[bp->cmd_rsp_reg.index.prod].long_0 = (u32) (PI_RCV_DESCR_M_SOP |
			((PI_CMD_RSP_K_SIZE_MAX / PI_ALIGN_K_CMD_RSP_BUFF) << PI_RCV_DESCR_V_SEG_LEN));
	bp->descr_block_virt->cmd_rsp[bp->cmd_rsp_reg.index.prod].long_1 = bp->cmd_rsp_phys;

	/* Bump (and wrap) the producer index and write out to register */

	bp->cmd_rsp_reg.index.prod += 1;
	bp->cmd_rsp_reg.index.prod &= PI_CMD_RSP_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_RSP_PROD, bp->cmd_rsp_reg.lword);

	/* Put request buffer on the command request queue */
	
	bp->descr_block_virt->cmd_req[bp->cmd_req_reg.index.prod].long_0 = (u32) (PI_XMT_DESCR_M_SOP |
			PI_XMT_DESCR_M_EOP | (PI_CMD_REQ_K_SIZE_MAX << PI_XMT_DESCR_V_SEG_LEN));
	bp->descr_block_virt->cmd_req[bp->cmd_req_reg.index.prod].long_1 = bp->cmd_req_phys;

	/* Bump (and wrap) the producer index and write out to register */

	bp->cmd_req_reg.index.prod += 1;
	bp->cmd_req_reg.index.prod &= PI_CMD_REQ_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_REQ_PROD, bp->cmd_req_reg.lword);

	/*
	 * Here we wait for the command request consumer index to be equal
	 * to the producer, indicating that the adapter has DMAed the request.
	 */

	for (timeout_cnt = 20000; timeout_cnt > 0; timeout_cnt--)
		{
		if (bp->cmd_req_reg.index.prod == (u8)(bp->cons_block_virt->cmd_req))
			break;
		udelay(100);			/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0) 
		return(DFX_K_HW_TIMEOUT);

	/* Bump (and wrap) the completion index and write out to register */

	bp->cmd_req_reg.index.comp += 1;
	bp->cmd_req_reg.index.comp &= PI_CMD_REQ_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_REQ_PROD, bp->cmd_req_reg.lword);

	/*
	 * Here we wait for the command response consumer index to be equal
	 * to the producer, indicating that the adapter has DMAed the response.
	 */

	for (timeout_cnt = 20000; timeout_cnt > 0; timeout_cnt--)
		{
		if (bp->cmd_rsp_reg.index.prod == (u8)(bp->cons_block_virt->cmd_rsp))
			break;
		udelay(100);			/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0) 
		return(DFX_K_HW_TIMEOUT);

	/* Bump (and wrap) the completion index and write out to register */

	bp->cmd_rsp_reg.index.comp += 1;
	bp->cmd_rsp_reg.index.comp &= PI_CMD_RSP_K_NUM_ENTRIES-1;
	dfx_port_write_long(bp, PI_PDQ_K_REG_CMD_RSP_PROD, bp->cmd_rsp_reg.lword);
	return(DFX_K_SUCCESS);
	}


/*
 * ========================
 * = dfx_hw_port_ctrl_req =
 * ========================
 *   
 * Overview:
 *   Sends PDQ port control command to adapter firmware
 *  
 * Returns:
 *   Host data register value in host_data if ptr is not NULL
 *       
 * Arguments:
 *   bp			- pointer to board information
 *	 command	- port control command
 *	 data_a		- port data A register value
 *	 data_b		- port data B register value
 *	 host_data	- ptr to host data register value
 *
 * Functional Description:
 *   Send generic port control command to adapter by writing
 *   to various PDQ port registers, then polling for completion.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	  - port control command succeeded
 *   DFX_K_HW_TIMEOUT - port control command timed out
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

int dfx_hw_port_ctrl_req(
	DFX_board_t	*bp,
	PI_UINT32	command,
	PI_UINT32	data_a,
	PI_UINT32	data_b,
	PI_UINT32	*host_data
	)

	{
	PI_UINT32	port_cmd;		/* Port Control command register value */
	int			timeout_cnt;	/* used in for loops */

	/* Set Command Error bit in command longword */
	
	port_cmd = (PI_UINT32) (command | PI_PCTRL_M_CMD_ERROR);

	/* Issue port command to the adapter */

	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_DATA_A, data_a);
	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_DATA_B, data_b);
	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_CTRL, port_cmd);

	/* Now wait for command to complete */

	if (command == PI_PCTRL_M_BLAST_FLASH)
		timeout_cnt = 600000;	/* set command timeout count to 60 seconds */
	else
		timeout_cnt = 20000;	/* set command timeout count to 2 seconds */

	for (; timeout_cnt > 0; timeout_cnt--)
		{
		dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_CTRL, &port_cmd);
		if (!(port_cmd & PI_PCTRL_M_CMD_ERROR))
			break;
		udelay(100);			/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0) 
		return(DFX_K_HW_TIMEOUT);

	/*
	 * If the address of host_data is non-zero, assume caller has supplied a  
	 * non NULL pointer, and return the contents of the HOST_DATA register in 
	 * it.
	 */

	if (host_data != NULL)
		dfx_port_read_long(bp, PI_PDQ_K_REG_HOST_DATA, host_data);
	return(DFX_K_SUCCESS);
	}


/*
 * =====================
 * = dfx_hw_adap_reset =
 * =====================
 *   
 * Overview:
 *   Resets adapter
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp   - pointer to board information
 *   type - type of reset to perform
 *
 * Functional Description:
 *   Issue soft reset to adapter by writing to PDQ Port Reset
 *   register.  Use incoming reset type to tell adapter what
 *   kind of reset operation to perform.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   This routine merely issues a soft reset to the adapter.
 *   It is expected that after this routine returns, the caller
 *   will appropriately poll the Port Status register for the
 *   adapter to enter the proper state.
 *
 * Side Effects:
 *   Internal adapter registers are cleared.
 */

void dfx_hw_adap_reset(
	DFX_board_t	*bp,
	PI_UINT32	type
	)

	{
	/* Set Reset type and assert reset */

	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_DATA_A, type);	/* tell adapter type of reset */
	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_RESET, PI_RESET_M_ASSERT_RESET);

	/* Wait for at least 1 Microsecond according to the spec. We wait 20 just to be safe */

	udelay(20);

	/* Deassert reset */

	dfx_port_write_long(bp, PI_PDQ_K_REG_PORT_RESET, 0);
	return;
	}


/*
 * ========================
 * = dfx_hw_adap_state_rd =
 * ========================
 *   
 * Overview:
 *   Returns current adapter state
 *  
 * Returns:
 *   Adapter state per PDQ Port Specification
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Reads PDQ Port Status register and returns adapter state.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

int dfx_hw_adap_state_rd(
	DFX_board_t *bp
	)

	{
	PI_UINT32 port_status;		/* Port Status register value */

	dfx_port_read_long(bp, PI_PDQ_K_REG_PORT_STATUS, &port_status);
	return((port_status & PI_PSTATUS_M_STATE) >> PI_PSTATUS_V_STATE);
	}


/*
 * =====================
 * = dfx_hw_dma_uninit =
 * =====================
 *   
 * Overview:
 *   Brings adapter to DMA_UNAVAILABLE state
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   bp   - pointer to board information
 *   type - type of reset to perform
 *
 * Functional Description:
 *   Bring adapter to DMA_UNAVAILABLE state by performing the following:
 *		1. Set reset type bit in Port Data A Register then reset adapter.
 *		2. Check that adapter is in DMA_UNAVAILABLE state.
 *
 * Return Codes:
 *   DFX_K_SUCCESS	  - adapter is in DMA_UNAVAILABLE state
 *   DFX_K_HW_TIMEOUT - adapter did not reset properly
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   Internal adapter registers are cleared.
 */

int dfx_hw_dma_uninit(
	DFX_board_t	*bp,
	PI_UINT32	type
	)

	{
	int timeout_cnt;	/* used in for loops */

	/* Set reset type bit and reset adapter */

	dfx_hw_adap_reset(bp, type);

	/* Now wait for adapter to enter DMA_UNAVAILABLE state */

	for (timeout_cnt = 100000; timeout_cnt > 0; timeout_cnt--)
		{
		if (dfx_hw_adap_state_rd(bp) == PI_STATE_K_DMA_UNAVAIL)
			break;
		udelay(100);					/* wait for 100 microseconds */
		}
	if (timeout_cnt == 0) 
		return(DFX_K_HW_TIMEOUT);
	return(DFX_K_SUCCESS);
	}


/*
 *	Align an sk_buff to a boundary power of 2
 *
 */
 
void my_skb_align(struct sk_buff *skb, int n)
{
	u32 x=(u32)skb->data;	/* We only want the low bits .. */
	u32 v;
	
	v=(x+n-1)&~(n-1);	/* Where we want to be */
	
	skb_reserve(skb, v-x);
}


/*
 * ================
 * = dfx_rcv_init =
 * ================
 *   
 * Overview:
 *   Produces buffers to adapter LLC Host receive descriptor block
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   This routine can be called during dfx_adap_init() or during an adapter
 *	 reset.  It initializes the descriptor block and produces all allocated
 *   LLC Host queue receive buffers.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   The PDQ has been reset and the adapter and driver maintained Type 2
 *   register indices are cleared.
 *
 * Side Effects:
 *   Receive buffers are posted to the adapter LLC queue and the adapter
 *   is notified.
 */

void dfx_rcv_init(
	DFX_board_t *bp
	)

	{
	int	i, j;					/* used in for loop */

	/*
	 *  Since each receive buffer is a single fragment of same length, initialize
	 *  first longword in each receive descriptor for entire LLC Host descriptor
	 *  block.  Also initialize second longword in each receive descriptor with
	 *  physical address of receive buffer.  We'll always allocate receive
	 *  buffers in powers of 2 so that we can easily fill the 256 entry descriptor
	 *  block and produce new receive buffers by simply updating the receive
	 *  producer index.
	 *
	 * 	Assumptions:
	 *		To support all shipping versions of PDQ, the receive buffer size
	 *		must be mod 128 in length and the physical address must be 128 byte
	 *		aligned.  In other words, bits 0-6 of the length and address must
	 *		be zero for the following descriptor field entries to be correct on
	 *		all PDQ-based boards.  We guaranteed both requirements during
	 *		driver initialization when we allocated memory for the receive buffers.
	 */

#ifdef DYNAMIC_BUFFERS
	for (i = 0; i < (int)(bp->rcv_bufs_to_post); i++)
		for (j = 0; (i + j) < (int)PI_RCV_DATA_K_NUM_ENTRIES; j += bp->rcv_bufs_to_post)
		{
			struct sk_buff *newskb;
			bp->descr_block_virt->rcv_data[i+j].long_0 = (u32) (PI_RCV_DESCR_M_SOP |
				((PI_RCV_DATA_K_SIZE_MAX / PI_ALIGN_K_RCV_DATA_BUFF) << PI_RCV_DESCR_V_SEG_LEN));
			newskb = dev_alloc_skb(NEW_SKB_SIZE);
			/*
			 * align to 128 bytes for compatibility with
			 * the old EISA boards.
			 */
			 
			my_skb_align(newskb,128);
			bp->descr_block_virt->rcv_data[i+j].long_1 = virt_to_bus(newskb->data);
			/*
			 * p_rcv_buff_va is only used inside the
			 * kernel so we put the skb pointer here.
			 */
			bp->p_rcv_buff_va[i+j] = (char *) newskb;
		}
#else
	for (i=0; i < (int)(bp->rcv_bufs_to_post); i++)
		for (j=0; (i + j) < (int)PI_RCV_DATA_K_NUM_ENTRIES; j += bp->rcv_bufs_to_post)
			{
			bp->descr_block_virt->rcv_data[i+j].long_0 = (u32) (PI_RCV_DESCR_M_SOP |
				((PI_RCV_DATA_K_SIZE_MAX / PI_ALIGN_K_RCV_DATA_BUFF) << PI_RCV_DESCR_V_SEG_LEN));
			bp->descr_block_virt->rcv_data[i+j].long_1 = (u32) (bp->rcv_block_phys + (i * PI_RCV_DATA_K_SIZE_MAX));
			bp->p_rcv_buff_va[i+j] = (char *) (bp->rcv_block_virt + (i * PI_RCV_DATA_K_SIZE_MAX));
			}
#endif

	/* Update receive producer and Type 2 register */

	bp->rcv_xmt_reg.index.rcv_prod = bp->rcv_bufs_to_post;
	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_2_PROD, bp->rcv_xmt_reg.lword);
	return;
	}


/*
 * =========================
 * = dfx_rcv_queue_process =
 * =========================
 *   
 * Overview:
 *   Process received LLC frames.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   Received LLC frames are processed until there are no more consumed frames.
 *   Once all frames are processed, the receive buffers are returned to the
 *   adapter.  Note that this algorithm fixes the length of time that can be spent
 *   in this routine, because there are a fixed number of receive buffers to
 *   process and buffers are not produced until this routine exits and returns
 *   to the ISR.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   None
 *
 * Side Effects:
 *   None
 */

void dfx_rcv_queue_process(
	DFX_board_t *bp
	)

	{
	PI_TYPE_2_CONSUMER	*p_type_2_cons;		/* ptr to rcv/xmt consumer block register */
	char				*p_buff;			/* ptr to start of packet receive buffer (FMC descriptor) */
	u32					descr, pkt_len;		/* FMC descriptor field and packet length */
	struct sk_buff		*skb;				/* pointer to a sk_buff to hold incoming packet data */

	/* Service all consumed LLC receive frames */

	p_type_2_cons = (PI_TYPE_2_CONSUMER *)(&bp->cons_block_virt->xmt_rcv_data);
	while (bp->rcv_xmt_reg.index.rcv_comp != p_type_2_cons->index.rcv_cons)
		{
		/* Process any errors */

		int entry;

		entry = bp->rcv_xmt_reg.index.rcv_comp;
#ifdef DYNAMIC_BUFFERS
		p_buff = (char *) (((struct sk_buff *)bp->p_rcv_buff_va[entry])->data);
#else
		p_buff = (char *) bp->p_rcv_buff_va[entry];
#endif
		memcpy(&descr, p_buff + RCV_BUFF_K_DESCR, sizeof(u32));

		if (descr & PI_FMC_DESCR_M_RCC_FLUSH)
			{
			if (descr & PI_FMC_DESCR_M_RCC_CRC)
				bp->rcv_crc_errors++;
			else
				bp->rcv_frame_status_errors++;
			}
		else
		{
			int rx_in_place = 0;

			/* The frame was received without errors - verify packet length */

			pkt_len = (u32)((descr & PI_FMC_DESCR_M_LEN) >> PI_FMC_DESCR_V_LEN);
			pkt_len -= 4;				/* subtract 4 byte CRC */
			if (!IN_RANGE(pkt_len, FDDI_K_LLC_ZLEN, FDDI_K_LLC_LEN))
				bp->rcv_length_errors++;
			else{
#ifdef DYNAMIC_BUFFERS
				if (pkt_len > SKBUFF_RX_COPYBREAK) {
					struct sk_buff *newskb;

					newskb = dev_alloc_skb(NEW_SKB_SIZE);
					if (newskb){
						rx_in_place = 1;
						
						my_skb_align(newskb, 128);
						skb = (struct sk_buff *)bp->p_rcv_buff_va[entry];
						skb_reserve(skb, RCV_BUFF_K_PADDING);
						bp->p_rcv_buff_va[entry] = (char *)newskb;
						bp->descr_block_virt->rcv_data[entry].long_1 = virt_to_bus(newskb->data);
					} else
						skb = 0;
				} else
#endif
					skb = dev_alloc_skb(pkt_len+3);	/* alloc new buffer to pass up, add room for PRH */
				if (skb == NULL)
					{
					printk("%s: Could not allocate receive buffer.  Dropping packet.\n", bp->dev->name);
					bp->rcv_discards++;
					break;
					}
				else {
#ifndef DYNAMIC_BUFFERS
					if (! rx_in_place)
#endif
					{
						/* Receive buffer allocated, pass receive packet up */

						memcpy(skb->data, p_buff + RCV_BUFF_K_PADDING, pkt_len+3);
					}
					
					skb_reserve(skb,3);		/* adjust data field so that it points to FC byte */
					skb_put(skb, pkt_len);		/* pass up packet length, NOT including CRC */
					skb->dev = bp->dev;		/* pass up device pointer */

					skb->protocol = fddi_type_trans(skb, bp->dev);
					netif_rx(skb);

					/* Update the rcv counters */

					bp->rcv_total_frames++;
					if (*(p_buff + RCV_BUFF_K_DA) & 0x01)
						bp->rcv_multicast_frames++;
				 
					bp->rcv_total_bytes += skb->len;
				}
			}
			}

		/*
		 * Advance the producer (for recycling) and advance the completion
		 * (for servicing received frames).  Note that it is okay to
		 * advance the producer without checking that it passes the
		 * completion index because they are both advanced at the same
		 * rate.
		 */

		bp->rcv_xmt_reg.index.rcv_prod += 1;
		bp->rcv_xmt_reg.index.rcv_comp += 1;
		}
	return;
	}


/*
 * =====================
 * = dfx_xmt_queue_pkt =
 * =====================
 *   
 * Overview:
 *   Queues packets for transmission
 *  
 * Returns:
 *   Condition code
 *       
 * Arguments:
 *   skb - pointer to sk_buff to queue for transmission
 *   dev - pointer to device information
 *
 * Functional Description:
 *   Here we assume that an incoming skb transmit request
 *   is contained in a single physically contiguous buffer
 *   in which the virtual address of the start of packet
 *   (skb->data) can be converted to a physical address
 *   by using virt_to_bus().
 *
 *   Since the adapter architecture requires a three byte
 *   packet request header to prepend the start of packet,
 *   we'll write the three byte field immediately prior to
 *   the FC byte.  This assumption is valid because we've
 *   ensured that dev->hard_header_len includes three pad
 *   bytes.  By posting a single fragment to the adapter,
 *   we'll reduce the number of descriptor fetches and
 *   bus traffic needed to send the request.
 *
 *   Also, we can't free the skb until after it's been DMA'd
 *   out by the adapter, so we'll queue it in the driver and
 *   return it in dfx_xmt_done.
 *
 * Return Codes:
 *   0 - driver queued packet, link is unavailable, or skbuff was bad
 *	 1 - caller should requeue the sk_buff for later transmission
 *
 * Assumptions:
 *	 First and foremost, we assume the incoming skb pointer
 *   is NOT NULL and is pointing to a valid sk_buff structure.
 *
 *   The outgoing packet is complete, starting with the
 *   frame control byte including the last byte of data,
 *   but NOT including the 4 byte CRC.  We'll let the
 *   adapter hardware generate and append the CRC.
 *
 *   The entire packet is stored in one physically
 *   contiguous buffer which is not cached and whose
 *   32-bit physical address can be determined.
 *
 *   It's vital that this routine is NOT reentered for the
 *   same board and that the OS is not in another section of
 *   code (eg. dfx_int_common) for the same board on a
 *   different thread.
 *
 * Side Effects:
 *   None
 */

int dfx_xmt_queue_pkt(
	struct sk_buff	*skb,
	struct net_device	*dev
	)

	{
	DFX_board_t		*bp = (DFX_board_t *) dev->priv;
	u8			prod;				/* local transmit producer index */
	PI_XMT_DESCR		*p_xmt_descr;		/* ptr to transmit descriptor block entry */
	XMT_DRIVER_DESCR	*p_xmt_drv_descr;	/* ptr to transmit driver descriptor */
	unsigned long		flags;

	/*
	 * Verify that incoming transmit request is OK
	 *
	 * Note: The packet size check is consistent with other
	 *		 Linux device drivers, although the correct packet
	 *		 size should be verified before calling the
	 *		 transmit routine.
	 */

	if (!IN_RANGE(skb->len, FDDI_K_LLC_ZLEN, FDDI_K_LLC_LEN))
	{
		printk("%s: Invalid packet length - %u bytes\n", 
			dev->name, skb->len);
		bp->xmt_length_errors++;		/* bump error counter */
		mark_bh(NET_BH);
		dev_kfree_skb(skb);
		return(0);				/* return "success" */
	}
	/*
	 * See if adapter link is available, if not, free buffer
	 *
	 * Note: If the link isn't available, free buffer and return 0
	 *		 rather than tell the upper layer to requeue the packet.
	 *		 The methodology here is that by the time the link
	 *		 becomes available, the packet to be sent will be
	 *		 fairly stale.  By simply dropping the packet, the
	 *		 higher layer protocols will eventually time out
	 *		 waiting for response packets which it won't receive.
	 */

	if (bp->link_available == PI_K_FALSE)
		{
		if (dfx_hw_adap_state_rd(bp) == PI_STATE_K_LINK_AVAIL)	/* is link really available? */
			bp->link_available = PI_K_TRUE;		/* if so, set flag and continue */
		else
			{
			bp->xmt_discards++;					/* bump error counter */
			dev_kfree_skb(skb);		/* free sk_buff now */
			return(0);							/* return "success" */
			}
		}

	spin_lock_irqsave(&bp->lock, flags);
	
	/* Get the current producer and the next free xmt data descriptor */

	prod		= bp->rcv_xmt_reg.index.xmt_prod;
	p_xmt_descr = &(bp->descr_block_virt->xmt_data[prod]);

	/*
	 * Get pointer to auxiliary queue entry to contain information
	 * for this packet.
	 *
	 * Note: The current xmt producer index will become the
	 *	 current xmt completion index when we complete this
	 *	 packet later on.  So, we'll get the pointer to the
	 *	 next auxiliary queue entry now before we bump the
	 *	 producer index.
	 */

	p_xmt_drv_descr = &(bp->xmt_drv_descr_blk[prod++]);	/* also bump producer index */

	/* Write the three PRH bytes immediately before the FC byte */

	skb_push(skb,3);
	skb->data[0] = DFX_PRH0_BYTE;	/* these byte values are defined */
	skb->data[1] = DFX_PRH1_BYTE;	/* in the Motorola FDDI MAC chip */
	skb->data[2] = DFX_PRH2_BYTE;	/* specification */

	/*
	 * Write the descriptor with buffer info and bump producer
	 *
	 * Note: Since we need to start DMA from the packet request
	 *		 header, we'll add 3 bytes to the DMA buffer length,
	 *		 and we'll determine the physical address of the
	 *		 buffer from the PRH, not skb->data.
	 *
	 * Assumptions:
	 *		 1. Packet starts with the frame control (FC) byte
	 *		    at skb->data.
	 *		 2. The 4-byte CRC is not appended to the buffer or
	 *			included in the length.
	 *		 3. Packet length (skb->len) is from FC to end of
	 *			data, inclusive.
	 *		 4. The packet length does not exceed the maximum
	 *			FDDI LLC frame length of 4491 bytes.
	 *		 5. The entire packet is contained in a physically
	 *			contiguous, non-cached, locked memory space
	 *			comprised of a single buffer pointed to by
	 *			skb->data.
	 *		 6. The physical address of the start of packet
	 *			can be determined from the virtual address
	 *			by using virt_to_bus() and is only 32-bits
	 *			wide.
	 */

	p_xmt_descr->long_0	= (u32) (PI_XMT_DESCR_M_SOP | PI_XMT_DESCR_M_EOP | ((skb->len) << PI_XMT_DESCR_V_SEG_LEN));
	p_xmt_descr->long_1 = (u32) virt_to_bus(skb->data);

	/*
	 * Verify that descriptor is actually available
	 *
	 * Note: If descriptor isn't available, return 1 which tells
	 *	 the upper layer to requeue the packet for later
	 *	 transmission.
	 *
	 *       We need to ensure that the producer never reaches the
	 *	 completion, except to indicate that the queue is empty.
	 */

	if (prod == bp->rcv_xmt_reg.index.xmt_comp)
	{
		skb_pull(skb,3);
		spin_unlock_irqrestore(&bp->lock, flags);
		return(1);			/* requeue packet for later */
	}

	/*
	 * Save info for this packet for xmt done indication routine
	 *
	 * Normally, we'd save the producer index in the p_xmt_drv_descr
	 * structure so that we'd have it handy when we complete this
	 * packet later (in dfx_xmt_done).  However, since the current
	 * transmit architecture guarantees a single fragment for the
	 * entire packet, we can simply bump the completion index by
	 * one (1) for each completed packet.
	 *
	 * Note: If this assumption changes and we're presented with
	 *	 an inconsistent number of transmit fragments for packet
	 *	 data, we'll need to modify this code to save the current
	 *	 transmit producer index.
	 */

	p_xmt_drv_descr->p_skb = skb;

	/* Update Type 2 register */

	bp->rcv_xmt_reg.index.xmt_prod = prod;
	dfx_port_write_long(bp, PI_PDQ_K_REG_TYPE_2_PROD, bp->rcv_xmt_reg.lword);
	spin_unlock_irqrestore(&bp->lock, flags);
	return(0);							/* packet queued to adapter */
	}


/*
 * ================
 * = dfx_xmt_done =
 * ================
 *   
 * Overview:
 *   Processes all frames that have been transmitted.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   For all consumed transmit descriptors that have not
 *   yet been completed, we'll free the skb we were holding
 *   onto using dev_kfree_skb and bump the appropriate
 *   counters.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   The Type 2 register is not updated in this routine.  It is
 *   assumed that it will be updated in the ISR when dfx_xmt_done
 *   returns.
 *
 * Side Effects:
 *   None
 */

void dfx_xmt_done(
	DFX_board_t *bp
	)

	{
	XMT_DRIVER_DESCR	*p_xmt_drv_descr;	/* ptr to transmit driver descriptor */
	PI_TYPE_2_CONSUMER	*p_type_2_cons;		/* ptr to rcv/xmt consumer block register */

	/* Service all consumed transmit frames */

	p_type_2_cons = (PI_TYPE_2_CONSUMER *)(&bp->cons_block_virt->xmt_rcv_data);
	while (bp->rcv_xmt_reg.index.xmt_comp != p_type_2_cons->index.xmt_cons)
		{
		/* Get pointer to the transmit driver descriptor block information */

		p_xmt_drv_descr = &(bp->xmt_drv_descr_blk[bp->rcv_xmt_reg.index.xmt_comp]);

		/* Increment transmit counters */

		bp->xmt_total_frames++;
		bp->xmt_total_bytes += p_xmt_drv_descr->p_skb->len;

		/* Return skb to operating system */

		dev_kfree_skb(p_xmt_drv_descr->p_skb);

		/*
		 * Move to start of next packet by updating completion index
		 *
		 * Here we assume that a transmit packet request is always
		 * serviced by posting one fragment.  We can therefore
		 * simplify the completion code by incrementing the
		 * completion index by one.  This code will need to be
		 * modified if this assumption changes.  See comments
		 * in dfx_xmt_queue_pkt for more details.
		 */

		bp->rcv_xmt_reg.index.xmt_comp += 1;
		}
	return;
	}


/*
 * =================
 * = dfx_xmt_flush =
 * =================
 *   
 * Overview:
 *   Processes all frames whether they've been transmitted
 *   or not.
 *  
 * Returns:
 *   None
 *       
 * Arguments:
 *   bp - pointer to board information
 *
 * Functional Description:
 *   For all produced transmit descriptors that have not
 *   yet been completed, we'll free the skb we were holding
 *   onto using dev_kfree_skb and bump the appropriate
 *   counters.  Of course, it's possible that some of
 *   these transmit requests actually did go out, but we
 *   won't make that distinction here.  Finally, we'll
 *   update the consumer index to match the producer.
 *
 * Return Codes:
 *   None
 *
 * Assumptions:
 *   This routine does NOT update the Type 2 register.  It
 *   is assumed that this routine is being called during a
 *   transmit flush interrupt, or a shutdown or close routine.
 *
 * Side Effects:
 *   None
 */

void dfx_xmt_flush(
	DFX_board_t *bp
	)

	{
	u32					prod_cons;			/* rcv/xmt consumer block longword */
	XMT_DRIVER_DESCR	*p_xmt_drv_descr;	/* ptr to transmit driver descriptor */

	/* Flush all outstanding transmit frames */

	while (bp->rcv_xmt_reg.index.xmt_comp != bp->rcv_xmt_reg.index.xmt_prod)
		{
		/* Get pointer to the transmit driver descriptor block information */

		p_xmt_drv_descr = &(bp->xmt_drv_descr_blk[bp->rcv_xmt_reg.index.xmt_comp]);

		/* Return skb to operating system */

		dev_kfree_skb(p_xmt_drv_descr->p_skb);

		/* Increment transmit error counter */

		bp->xmt_discards++;

		/*
		 * Move to start of next packet by updating completion index
		 *
		 * Here we assume that a transmit packet request is always
		 * serviced by posting one fragment.  We can therefore
		 * simplify the completion code by incrementing the
		 * completion index by one.  This code will need to be
		 * modified if this assumption changes.  See comments
		 * in dfx_xmt_queue_pkt for more details.
		 */

		bp->rcv_xmt_reg.index.xmt_comp += 1;
		}

	/* Update the transmit consumer index in the consumer block */

	prod_cons = (u32)(bp->cons_block_virt->xmt_rcv_data & ~PI_CONS_M_XMT_INDEX);
	prod_cons |= (u32)(bp->rcv_xmt_reg.index.xmt_prod << PI_CONS_V_XMT_INDEX);
	bp->cons_block_virt->xmt_rcv_data = prod_cons;
	return;
	}


/*
 * Local variables:
 * kernel-compile-command: "gcc -D__KERNEL__ -I/root/linux/include -Wall -Wstrict-prototypes -O2 -pipe -fomit-frame-pointer -fno-strength-reduce -m486 -malign-loops=2 -malign-jumps=2 -malign-functions=2 -DCPU=586 -c defxx.c"
 * End:
 */
