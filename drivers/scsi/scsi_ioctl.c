#define __NO_VERSION__
#include <linux/module.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/page.h>

#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/string.h>

#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include <scsi/scsi_ioctl.h>

#define NORMAL_RETRIES			5
#define NORMAL_TIMEOUT			(10 * HZ)
#define FORMAT_UNIT_TIMEOUT		(2 * 60 * 60 * HZ)
#define START_STOP_TIMEOUT		(60 * HZ)
#define MOVE_MEDIUM_TIMEOUT		(5 * 60 * HZ)
#define READ_ELEMENT_STATUS_TIMEOUT	(5 * 60 * HZ)

#define MAX_BUF PAGE_SIZE

#define max(a,b) (((a) > (b)) ? (a) : (b))

/*
 * If we are told to probe a host, we will return 0 if  the host is not
 * present, 1 if the host is present, and will return an identifying
 * string at *arg, if arg is non null, filling to the length stored at
 * (int *) arg
 */

static int ioctl_probe(struct Scsi_Host *host, void *buffer)
{
	int temp, result;
	unsigned int len, slen;
	const char *string;

	if ((temp = host->hostt->present) && buffer) {
		result = verify_area(VERIFY_READ, buffer, sizeof(long));
		if (result)
			return result;

		get_user(len, (unsigned int *) buffer);
		if (host->hostt->info)
			string = host->hostt->info(host);
		else
			string = host->hostt->name;
		if (string) {
			slen = strlen(string);
			if (len > slen)
				len = slen + 1;
			result = verify_area(VERIFY_WRITE, buffer, len);
			if (result)
				return result;

			copy_to_user(buffer, string, len);
		}
	}
	return temp;
}

/*

 * The SCSI_IOCTL_SEND_COMMAND ioctl sends a command out to the SCSI host.
 * The NORMAL_TIMEOUT and NORMAL_RETRIES  variables are used.  
 * 
 * dev is the SCSI device struct ptr, *(int *) arg is the length of the
 * input data, if any, not including the command string & counts, 
 * *((int *)arg + 1) is the output buffer size in bytes.
 * 
 * *(char *) ((int *) arg)[2] the actual command byte.   
 * 
 * Note that if more than MAX_BUF bytes are requested to be transfered,
 * the ioctl will fail with error EINVAL.  MAX_BUF can be increased in
 * the future by increasing the size that scsi_malloc will accept.
 * 
 * This size *does not* include the initial lengths that were passed.
 * 
 * The SCSI command is read from the memory location immediately after the
 * length words, and the input data is right after the command.  The SCSI
 * routines know the command size based on the opcode decode.  
 * 
 * The output area is then filled in starting from the command byte. 
 */

static void scsi_ioctl_done(Scsi_Cmnd * SCpnt)
{
	struct request *req;

	req = &SCpnt->request;
	req->rq_status = RQ_SCSI_DONE;	/* Busy, but indicate request done */

	if (req->sem != NULL) {
		up(req->sem);
	}
}

static int ioctl_internal_command(Scsi_Device * dev, char *cmd,
				  int timeout, int retries)
{
	unsigned long flags;
	int result;
	Scsi_Cmnd *SCpnt;
	Scsi_Device *SDpnt;

	spin_lock_irqsave(&io_request_lock, flags);

	SCSI_LOG_IOCTL(1, printk("Trying ioctl with scsi command %d\n", cmd[0]));
	SCpnt = scsi_allocate_device(NULL, dev, 1);
	{
		DECLARE_MUTEX_LOCKED(sem);
		SCpnt->request.sem = &sem;
		scsi_do_cmd(SCpnt, cmd, NULL, 0, scsi_ioctl_done, timeout, retries);
		spin_unlock_irqrestore(&io_request_lock, flags);
		down(&sem);
		spin_lock_irqsave(&io_request_lock, flags);
		SCpnt->request.sem = NULL;
	}

	SCSI_LOG_IOCTL(2, printk("Ioctl returned  0x%x\n", SCpnt->result));

	if (driver_byte(SCpnt->result) != 0)
		switch (SCpnt->sense_buffer[2] & 0xf) {
		case ILLEGAL_REQUEST:
			if (cmd[0] == ALLOW_MEDIUM_REMOVAL)
				dev->lockable = 0;
			else
				printk("SCSI device (ioctl) reports ILLEGAL REQUEST.\n");
			break;
		case NOT_READY:	/* This happens if there is no disc in drive */
			if (dev->removable && (cmd[0] != TEST_UNIT_READY)) {
				printk(KERN_INFO "Device not ready.  Make sure there is a disc in the drive.\n");
				break;
			}
		case UNIT_ATTENTION:
			if (dev->removable) {
				dev->changed = 1;
				SCpnt->result = 0;	/* This is no longer considered an error */
				/* gag this error, VFS will log it anyway /axboe */
				/* printk(KERN_INFO "Disc change detected.\n"); */
				break;
			};
		default:	/* Fall through for non-removable media */
			printk("SCSI error: host %d id %d lun %d return code = %x\n",
			       dev->host->host_no,
			       dev->id,
			       dev->lun,
			       SCpnt->result);
			printk("\tSense class %x, sense error %x, extended sense %x\n",
			       sense_class(SCpnt->sense_buffer[0]),
			       sense_error(SCpnt->sense_buffer[0]),
			       SCpnt->sense_buffer[2] & 0xf);

		};

	result = SCpnt->result;

	SCSI_LOG_IOCTL(2, printk("IOCTL Releasing command\n"));
	SDpnt = SCpnt->device;
	scsi_release_command(SCpnt);
	SCpnt = NULL;

	if (!SDpnt->was_reset && SDpnt->scsi_request_fn)
		(*SDpnt->scsi_request_fn) ();

	wake_up(&SDpnt->device_wait);
	spin_unlock_irqrestore(&io_request_lock, flags);
	return result;
}

/*
 * This interface is depreciated - users should use the scsi generic (sg)
 * interface instead, as this is a more flexible approach to performing
 * generic SCSI commands on a device.
 *
 * The structure that we are passed should look like:
 *
 * struct sdata {
 *  unsigned int inlen;	     [i] Length of data to be written to device 
 *  unsigned int outlen;     [i] Length of data to be read from device 
 *  unsigned char cmd[x];    [i] SCSI command (6 <= x <= 12).
 *			     [o] Data read from device starts here.
 *			     [o] On error, sense buffer starts here.
 *  unsigned char wdata[y];  [i] Data written to device starts here.
 * };
 * Notes:
 *   -	The SCSI command length is determined by examining the 1st byte
 *	of the given command. There is no way to override this.
 *   -	Data transfers are limited to PAGE_SIZE (4K on i386, 8K on alpha).
 *   -	The length (x + y) must be at least OMAX_SB_LEN bytes long to
 *	accomodate the sense buffer when an error occurs.
 *	The sense buffer is truncated to OMAX_SB_LEN (16) bytes so that
 *	old code will not be surprised.
 *   -	If a Unix error occurs (e.g. ENOMEM) then the user will receive
 *	a negative return and the Unix error code in 'errno'. 
 *	If the SCSI command succeeds then 0 is returned.
 *	Positive numbers returned are the compacted SCSI error codes (4 
 *	bytes in one int) where the lowest byte is the SCSI status.
 *	See the drivers/scsi/scsi.h file for more information on this.
 *
 */
#define OMAX_SB_LEN 16   /* Old sense buffer length */

int scsi_ioctl_send_command(Scsi_Device * dev, Scsi_Ioctl_Command * sic)
{
	unsigned long flags;
	char *buf;
	unsigned char cmd[12];
	char *cmd_in;
	Scsi_Cmnd *SCpnt;
	Scsi_Device *SDpnt;
	unsigned char opcode;
	int inlen, outlen, cmdlen;
	int needed, buf_needed;
	int timeout, retries, result;

	if (!sic)
		return -EINVAL;
	/*
	 * Verify that we can read at least this much.
	 */
	result = verify_area(VERIFY_READ, sic, sizeof(Scsi_Ioctl_Command));
	if (result)
		return result;

	get_user(inlen, &sic->inlen);
	get_user(outlen, &sic->outlen);

	/*
	 * We do not transfer more than MAX_BUF with this interface.
	 * If the user needs to transfer more data than this, they
	 * should use scsi_generics (sg) instead.
	 */
	if (inlen > MAX_BUF)
		return -EINVAL;
	if (outlen > MAX_BUF)
		return -EINVAL;

	cmd_in = sic->data;
	get_user(opcode, cmd_in);

	needed = buf_needed = (inlen > outlen ? inlen : outlen);
	if (buf_needed) {
		buf_needed = (buf_needed + 511) & ~511;
		if (buf_needed > MAX_BUF)
			buf_needed = MAX_BUF;
		spin_lock_irqsave(&io_request_lock, flags);
		buf = (char *) scsi_malloc(buf_needed);
		spin_unlock_irqrestore(&io_request_lock, flags);
		if (!buf)
			return -ENOMEM;
		memset(buf, 0, buf_needed);
	} else
		buf = NULL;

	/*
	 * Obtain the command from the user's address space.
	 */
	cmdlen = COMMAND_SIZE(opcode);

	result = verify_area(VERIFY_READ, cmd_in, cmdlen + inlen);
	if (result)
		return result;

	copy_from_user(cmd, cmd_in, cmdlen);

	/*
	 * Obtain the data to be sent to the device (if any).
	 */
	copy_from_user(buf, cmd_in + cmdlen, inlen);

	/*
	 * Set the lun field to the correct value.
	 */
	cmd[1] = (cmd[1] & 0x1f) | (dev->lun << 5);

	switch (opcode) {
	case FORMAT_UNIT:
		timeout = FORMAT_UNIT_TIMEOUT;
		retries = 1;
		break;
	case START_STOP:
		timeout = START_STOP_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	case MOVE_MEDIUM:
		timeout = MOVE_MEDIUM_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	case READ_ELEMENT_STATUS:
		timeout = READ_ELEMENT_STATUS_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	default:
		timeout = NORMAL_TIMEOUT;
		retries = NORMAL_RETRIES;
		break;
	}

#ifndef DEBUG_NO_CMD

	spin_lock_irqsave(&io_request_lock, flags);

	SCpnt = scsi_allocate_device(NULL, dev, 1);

	{
		DECLARE_MUTEX_LOCKED(sem);
		SCpnt->request.sem = &sem;
		scsi_do_cmd(SCpnt, cmd, buf, needed, scsi_ioctl_done,
			    timeout, retries);
		spin_unlock_irqrestore(&io_request_lock, flags);
		down(&sem);
		SCpnt->request.sem = NULL;
	}

	/* 
	 * If there was an error condition, pass the info back to the user. 
	 */
	if (SCpnt->result) {
		int sb_len = sizeof(SCpnt->sense_buffer);

		sb_len = (sb_len > OMAX_SB_LEN) ? OMAX_SB_LEN : sb_len;
		result = verify_area(VERIFY_WRITE, cmd_in, sb_len);
		if (result)
			return result;
		copy_to_user(cmd_in, SCpnt->sense_buffer, sb_len);
	} else {
		result = verify_area(VERIFY_WRITE, cmd_in, outlen);
		if (result)
			return result;
		copy_to_user(cmd_in, buf, outlen);
	}
	result = SCpnt->result;

	spin_lock_irqsave(&io_request_lock, flags);

	wake_up(&SCpnt->device->device_wait);
	SDpnt = SCpnt->device;
	scsi_release_command(SCpnt);
	SCpnt = NULL;

	if (buf)
		scsi_free(buf, buf_needed);

	if (SDpnt->scsi_request_fn)
		(*SDpnt->scsi_request_fn) ();

	spin_unlock_irqrestore(&io_request_lock, flags);
	return result;
#else
	{
		int i;
		printk("scsi_ioctl : device %d.  command = ", dev->id);
		for (i = 0; i < 12; ++i)
			printk("%02x ", cmd[i]);
		printk("\nbuffer =");
		for (i = 0; i < 20; ++i)
			printk("%02x ", buf[i]);
		printk("\n");
		printk("inlen = %d, outlen = %d, cmdlen = %d\n",
		       inlen, outlen, cmdlen);
		printk("buffer = %d, cmd_in = %d\n", buffer, cmd_in);
	}
	return 0;
#endif
}

/*
 * the scsi_ioctl() function differs from most ioctls in that it does
 * not take a major/minor number as the dev field.  Rather, it takes
 * a pointer to a scsi_devices[] element, a structure. 
 */
int scsi_ioctl(Scsi_Device * dev, int cmd, void *arg)
{
	int result;
	char scsi_cmd[12];

	/* No idea how this happens.... */
	if (!dev)
		return -ENXIO;

	/*
	 * If we are in the middle of error recovery, don't let anyone
	 * else try and use this device.  Also, if error recovery fails, it
	 * may try and take the device offline, in which case all further
	 * access to the device is prohibited.
	 */
	if (!scsi_block_when_processing_errors(dev)) {
		return -ENODEV;
	}
	switch (cmd) {
	case SCSI_IOCTL_GET_IDLUN:
		result = verify_area(VERIFY_WRITE, arg, sizeof(Scsi_Idlun));
		if (result)
			return result;

		put_user(dev->id
			 + (dev->lun << 8)
			 + (dev->channel << 16)
			 + ((dev->host->host_no & 0xff) << 24),
			 &((Scsi_Idlun *) arg)->dev_id);
		put_user(dev->host->unique_id, &((Scsi_Idlun *) arg)->host_unique_id);
		return 0;
	case SCSI_IOCTL_GET_BUS_NUMBER:
		result = verify_area(VERIFY_WRITE, (void *) arg, sizeof(int));
		if (result)
			return result;
		put_user(dev->host->host_no, (int *) arg);
		return 0;
	case SCSI_IOCTL_TAGGED_ENABLE:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		if (!dev->tagged_supported)
			return -EINVAL;
		dev->tagged_queue = 1;
		dev->current_tag = 1;
		return 0;
	case SCSI_IOCTL_TAGGED_DISABLE:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		if (!dev->tagged_supported)
			return -EINVAL;
		dev->tagged_queue = 0;
		dev->current_tag = 0;
		return 0;
	case SCSI_IOCTL_PROBE_HOST:
		return ioctl_probe(dev->host, arg);
	case SCSI_IOCTL_SEND_COMMAND:
		if (!capable(CAP_SYS_ADMIN))
			return -EACCES;
		return scsi_ioctl_send_command((Scsi_Device *) dev,
					     (Scsi_Ioctl_Command *) arg);
	case SCSI_IOCTL_DOORLOCK:
		if (!dev->removable || !dev->lockable)
			return 0;
		scsi_cmd[0] = ALLOW_MEDIUM_REMOVAL;
		scsi_cmd[1] = dev->lun << 5;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = SCSI_REMOVAL_PREVENT;
		return ioctl_internal_command((Scsi_Device *) dev, scsi_cmd,
					 NORMAL_TIMEOUT, NORMAL_RETRIES);
		break;
	case SCSI_IOCTL_DOORUNLOCK:
		if (!dev->removable || !dev->lockable)
			return 0;
		scsi_cmd[0] = ALLOW_MEDIUM_REMOVAL;
		scsi_cmd[1] = dev->lun << 5;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = SCSI_REMOVAL_ALLOW;
		return ioctl_internal_command((Scsi_Device *) dev, scsi_cmd,
					 NORMAL_TIMEOUT, NORMAL_RETRIES);
	case SCSI_IOCTL_TEST_UNIT_READY:
		scsi_cmd[0] = TEST_UNIT_READY;
		scsi_cmd[1] = dev->lun << 5;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = 0;
		return ioctl_internal_command((Scsi_Device *) dev, scsi_cmd,
					 NORMAL_TIMEOUT, NORMAL_RETRIES);
		break;
	case SCSI_IOCTL_START_UNIT:
		scsi_cmd[0] = START_STOP;
		scsi_cmd[1] = dev->lun << 5;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = 1;
		return ioctl_internal_command((Scsi_Device *) dev, scsi_cmd,
				     START_STOP_TIMEOUT, NORMAL_RETRIES);
		break;
	case SCSI_IOCTL_STOP_UNIT:
		scsi_cmd[0] = START_STOP;
		scsi_cmd[1] = dev->lun << 5;
		scsi_cmd[2] = scsi_cmd[3] = scsi_cmd[5] = 0;
		scsi_cmd[4] = 0;
		return ioctl_internal_command((Scsi_Device *) dev, scsi_cmd,
				     START_STOP_TIMEOUT, NORMAL_RETRIES);
		break;
	default:
		if (dev->host->hostt->ioctl)
			return dev->host->hostt->ioctl(dev, cmd, arg);
		return -EINVAL;
	}
	return -EINVAL;
}

/*
 * Just like scsi_ioctl, only callable from kernel space with no 
 * fs segment fiddling.
 */

int kernel_scsi_ioctl(Scsi_Device * dev, int cmd, void *arg)
{
	mm_segment_t oldfs;
	int tmp;
	oldfs = get_fs();
	set_fs(get_ds());
	tmp = scsi_ioctl(dev, cmd, arg);
	set_fs(oldfs);
	return tmp;
}

/*
 * Overrides for Emacs so that we almost follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
