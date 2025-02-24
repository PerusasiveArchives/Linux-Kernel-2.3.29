/*======================================================================

    PC Card Driver Services
    
    ds.c 1.100 1999/11/08 20:47:02
    
    The contents of this file are subject to the Mozilla Public
    License Version 1.1 (the "License"); you may not use this file
    except in compliance with the License. You may obtain a copy of
    the License at http://www.mozilla.org/MPL/

    Software distributed under the License is distributed on an "AS
    IS" basis, WITHOUT WARRANTY OF ANY KIND, either express or
    implied. See the License for the specific language governing
    rights and limitations under the License.

    The initial developer of the original code is David A. Hinds
    <dhinds@pcmcia.sourceforge.org>.  Portions created by David A. Hinds
    are Copyright (C) 1999 David A. Hinds.  All Rights Reserved.

    Alternatively, the contents of this file may be used under the
    terms of the GNU Public License version 2 (the "GPL"), in which
    case the provisions of the GPL are applicable instead of the
    above.  If you wish to allow the use of your version of this file
    only under the terms of the GPL and not to allow others to use
    your version of this file under the MPL, indicate your decision
    by deleting the provisions above and replace them with the notice
    and other provisions required by the GPL.  If you do not delete
    the provisions above, a recipient may use your version of this
    file under either the MPL or the GPL.
    
======================================================================*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/ioctl.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>

#include <pcmcia/version.h>
#include <pcmcia/cs_types.h>
#include <pcmcia/cs.h>
#include <pcmcia/bulkmem.h>
#include <pcmcia/cistpl.h>
#include <pcmcia/ds.h>

#ifdef PCMCIA_DEBUG
int pc_debug = PCMCIA_DEBUG;
MODULE_PARM(pc_debug, "i");
#define DEBUG(n, args...) if (pc_debug>(n)) printk(KERN_DEBUG args)
static const char *version =
"ds.c 1.100 1999/11/08 20:47:02 (David Hinds)";
#else
#define DEBUG(n, args...)
#endif

/*====================================================================*/

typedef struct driver_info_t {
    dev_info_t		dev_info;
    int			use_count, status;
    dev_link_t		*(*attach)(void);
    void		(*detach)(dev_link_t *);
    struct driver_info_t *next;
} driver_info_t;

typedef struct socket_bind_t {
    driver_info_t	*driver;
    dev_link_t		*instance;
    struct socket_bind_t *next;
} socket_bind_t;

/* Device user information */
#define MAX_EVENTS	32
#define USER_MAGIC	0x7ea4
#define CHECK_USER(u) \
    (((u) == NULL) || ((u)->user_magic != USER_MAGIC))
typedef struct user_info_t {
    u_int		user_magic;
    int			event_head, event_tail;
    event_t		event[MAX_EVENTS];
    struct user_info_t	*next;
} user_info_t;

/* Socket state information */
typedef struct socket_info_t {
    client_handle_t	handle;
    int			state;
    user_info_t		*user;
    int			req_pending, req_result;
    wait_queue_head_t	queue, request;
    struct timer_list	removal;
    socket_bind_t	*bind;
} socket_info_t;

#define SOCKET_PRESENT		0x01
#define SOCKET_BUSY		0x02
#define SOCKET_REMOVAL_PENDING	0x10

/*====================================================================*/

/* Device driver ID passed to Card Services */
static dev_info_t dev_info = "Driver Services";

/* Linked list of all registered device drivers */
static driver_info_t *root_driver = NULL;

static int sockets = 0, major_dev = -1;
static socket_info_t *socket_table = NULL;

extern struct proc_dir_entry *proc_pccard;

/* We use this to distinguish in-kernel from modular drivers */
static int init_status = 1;

/*====================================================================*/

static void cs_error(client_handle_t handle, int func, int ret)
{
    error_info_t err = { func, ret };
    CardServices(ReportError, handle, &err);
}

/*======================================================================

    Register_pccard_driver() and unregister_pccard_driver() are used
    tell Driver Services that a PC Card client driver is available to
    be bound to sockets.
    
======================================================================*/

int register_pccard_driver(dev_info_t *dev_info,
			   dev_link_t *(*attach)(void),
			   void (*detach)(dev_link_t *))
{
    driver_info_t *driver;
    socket_bind_t *b;
    int i;

    DEBUG(0, "ds: register_pccard_driver('%s')\n", (char *)dev_info);
    for (driver = root_driver; driver; driver = driver->next)
	if (strncmp((char *)dev_info, (char *)driver->dev_info,
		    DEV_NAME_LEN) == 0)
	    break;
    if (!driver) {
	driver = kmalloc(sizeof(driver_info_t), GFP_KERNEL);
	strncpy(driver->dev_info, (char *)dev_info, DEV_NAME_LEN);
	driver->use_count = 0;
	driver->status = init_status;
	driver->next = root_driver;
	root_driver = driver;
    }

    driver->attach = attach;
    driver->detach = detach;
    if (driver->use_count == 0) return 0;
    
    /* Instantiate any already-bound devices */
    for (i = 0; i < sockets; i++)
	for (b = socket_table[i].bind; b; b = b->next) {
	    if (b->driver != driver) continue;
	    b->instance = driver->attach();
	    if (b->instance == NULL)
		printk(KERN_NOTICE "ds: unable to create instance "
		       "of '%s'!\n", driver->dev_info);
	}
    
    return 0;
} /* register_pccard_driver */

/*====================================================================*/

int unregister_pccard_driver(dev_info_t *dev_info)
{
    driver_info_t *target, **d = &root_driver;
    socket_bind_t *b;
    int i;
    
    DEBUG(0, "ds: unregister_pccard_driver('%s')\n",
	  (char *)dev_info);
    while ((*d) && (strncmp((*d)->dev_info, (char *)dev_info,
			    DEV_NAME_LEN) != 0))
	d = &(*d)->next;
    if (*d == NULL)
	return -1;
    
    target = *d;
    if (target->use_count == 0) {
	*d = target->next;
	kfree(target);
    } else {
	/* Blank out any left-over device instances */
	target->attach = NULL; target->detach = NULL;
	for (i = 0; i < sockets; i++)
	    for (b = socket_table[i].bind; b; b = b->next)
		if (b->driver == target) b->instance = NULL;
    }
    return 0;
} /* unregister_pccard_driver */

/*====================================================================*/

#ifdef CONFIG_PROC_FS
static int proc_read_drivers(char *buf, char **start, off_t pos,
			     int count, int *eof, void *data)
{
    driver_info_t *d;
    char *p = buf;
    for (d = root_driver; d; d = d->next)
	p += sprintf(p, "%-24.24s %d %d\n", d->dev_info,
		     d->status, d->use_count);
    return (p - buf);
}
#endif

/*======================================================================

    These manage a ring buffer of events pending for one user process
    
======================================================================*/

static int queue_empty(user_info_t *user)
{
    return (user->event_head == user->event_tail);
}

static event_t get_queued_event(user_info_t *user)
{
    user->event_tail = (user->event_tail+1) % MAX_EVENTS;
    return user->event[user->event_tail];
}

static void queue_event(user_info_t *user, event_t event)
{
    user->event_head = (user->event_head+1) % MAX_EVENTS;
    if (user->event_head == user->event_tail)
	user->event_tail = (user->event_tail+1) % MAX_EVENTS;
    user->event[user->event_head] = event;
}

static void handle_event(socket_info_t *s, event_t event)
{
    user_info_t *user;
    for (user = s->user; user; user = user->next)
	queue_event(user, event);
    wake_up_interruptible(&s->queue);
}

static int handle_request(socket_info_t *s, event_t event)
{
    if (s->req_pending != 0)
	return CS_IN_USE;
    if (s->state & SOCKET_BUSY)
	s->req_pending = 1;
    handle_event(s, event);
    if (s->req_pending > 0) {
	interruptible_sleep_on(&s->request);
	if (signal_pending(current))
	    return CS_IN_USE;
	else
	    return s->req_result;
    }
    return CS_SUCCESS;
}

static void handle_removal(u_long sn)
{
    socket_info_t *s = &socket_table[sn];
    handle_event(s, CS_EVENT_CARD_REMOVAL);
    s->state &= ~SOCKET_REMOVAL_PENDING;
}

/*======================================================================

    The card status event handler.
    
======================================================================*/

static int ds_event(event_t event, int priority,
		    event_callback_args_t *args)
{
    socket_info_t *s;
    int i;

    DEBUG(1, "ds: ds_event(0x%06x, %d, 0x%p)\n",
	  event, priority, args->client_handle);
    s = args->client_data;
    i = s - socket_table;
    
    switch (event) {
	
    case CS_EVENT_CARD_REMOVAL:
	s->state &= ~SOCKET_PRESENT;
	if (!(s->state & SOCKET_REMOVAL_PENDING)) {
	    s->state |= SOCKET_REMOVAL_PENDING;
	    s->removal.expires = jiffies + HZ/10;
	    add_timer(&s->removal);
	}
	break;
	
    case CS_EVENT_CARD_INSERTION:
	s->state |= SOCKET_PRESENT;
	handle_event(s, event);
	break;

    case CS_EVENT_EJECTION_REQUEST:
	return handle_request(s, event);
	break;
	
    default:
	handle_event(s, event);
	break;
    }

    return 0;
} /* ds_event */

/*======================================================================

    bind_mtd() connects a memory region with an MTD client.
    
======================================================================*/

static int bind_mtd(int i, mtd_info_t *mtd_info)
{
    mtd_bind_t bind_req;
    int ret;

    bind_req.dev_info = &mtd_info->dev_info;
    bind_req.Attributes = mtd_info->Attributes;
    bind_req.Socket = i;
    bind_req.CardOffset = mtd_info->CardOffset;
    ret = CardServices(BindMTD, &bind_req);
    if (ret != CS_SUCCESS) {
	cs_error(NULL, BindMTD, ret);
	printk(KERN_NOTICE "ds: unable to bind MTD '%s' to socket %d"
	       " offset 0x%x\n",
	       (char *)bind_req.dev_info, i, bind_req.CardOffset);
	return -ENODEV;
    }
    return 0;
} /* bind_mtd */

/*======================================================================

    bind_request() connects a socket to a particular client driver.
    It looks up the specified device ID in the list of registered
    drivers, binds it to the socket, and tries to create an instance
    of the device.  unbind_request() deletes a driver instance.
    
======================================================================*/

static int bind_request(int i, bind_info_t *bind_info)
{
    struct driver_info_t *driver;
    socket_bind_t *b;
    bind_req_t bind_req;
    socket_info_t *s = &socket_table[i];
    int ret;

    DEBUG(2, "bind_request(%d, '%s')\n", i,
	  (char *)bind_info->dev_info);
    for (driver = root_driver; driver; driver = driver->next)
	if (strcmp((char *)driver->dev_info,
		   (char *)bind_info->dev_info) == 0)
	    break;
    if (driver == NULL) {
	driver = kmalloc(sizeof(driver_info_t), GFP_KERNEL);
	strncpy(driver->dev_info, bind_info->dev_info, DEV_NAME_LEN);
	driver->use_count = 0;
	driver->next = root_driver;
	driver->attach = NULL; driver->detach = NULL;
	root_driver = driver;
    }

    for (b = s->bind; b; b = b->next)
	if (driver == b->driver)
	    break;
    if (b != NULL) {
	bind_info->instance = b->instance;
	return -EBUSY;
    }

    bind_req.Socket = i;
    bind_req.Function = bind_info->function;
    bind_req.dev_info = &driver->dev_info;
    ret = CardServices(BindDevice, &bind_req);
    if (ret != CS_SUCCESS) {
	cs_error(NULL, BindDevice, ret);
	printk(KERN_NOTICE "ds: unable to bind '%s' to socket %d\n",
	       (char *)dev_info, i);
	return -ENODEV;
    }

    /* Add binding to list for this socket */
    driver->use_count++;
    b = kmalloc(sizeof(socket_bind_t), GFP_KERNEL);
    b->driver = driver;
    b->instance = NULL;
    b->next = s->bind;
    s->bind = b;
    
    if (driver->attach) {
	b->instance = driver->attach();
	if (b->instance == NULL) {
	    printk(KERN_NOTICE "ds: unable to create instance "
		   "of '%s'!\n", (char *)bind_info->dev_info);
	    return -ENODEV;
	}
    }
    
    return 0;
} /* bind_request */

/*====================================================================*/

static int get_device_info(int i, bind_info_t *bind_info, int first)
{
    socket_info_t *s = &socket_table[i];
    socket_bind_t *b;
    dev_node_t *node;
    
    for (b = s->bind; b; b = b->next)
	if (strcmp((char *)b->driver->dev_info,
		   (char *)bind_info->dev_info) == 0)
	    break;
    if (b == NULL) return -ENODEV;
    if ((b->instance == NULL) ||
	(b->instance->state & DEV_CONFIG_PENDING))
	return -EAGAIN;
    if (first)
	node = b->instance->dev;
    else
	for (node = b->instance->dev; node; node = node->next)
	    if (node == bind_info->next) break;
    if (node == NULL) return -ENODEV;

    strncpy(bind_info->name, node->dev_name, DEV_NAME_LEN);
    bind_info->name[DEV_NAME_LEN-1] = '\0';
    bind_info->major = node->major;
    bind_info->minor = node->minor;
    bind_info->next = node->next;
    
    return 0;
} /* get_device_info */

/*====================================================================*/

static int unbind_request(int i, bind_info_t *bind_info)
{
    socket_info_t *s = &socket_table[i];
    socket_bind_t **b, *c;

    DEBUG(2, "unbind_request(%d, '%s')\n", i,
	  (char *)bind_info->dev_info);
    for (b = &s->bind; *b; b = &(*b)->next)
	if (strcmp((char *)(*b)->driver->dev_info,
		   (char *)bind_info->dev_info) == 0)
	    break;
    if (*b == NULL)
	return -ENODEV;
    
    c = *b;
    c->driver->use_count--;
    if (c->driver->detach) {
	if (c->instance)
	    c->driver->detach(c->instance);
    } else {
	if (c->driver->use_count == 0) {
	    driver_info_t **d;
	    for (d = &root_driver; *d; d = &((*d)->next))
		if (c->driver == *d) break;
	    *d = (*d)->next;
	    kfree_s(c->driver, sizeof(driver_info_t));
	}
    }
    *b = c->next;
    kfree_s(c, sizeof(socket_bind_t));
    
    return 0;
} /* unbind_request */

/*======================================================================

    The user-mode PC Card device interface

======================================================================*/

static int ds_open(struct inode *inode, struct file *file)
{
    socket_t i = MINOR(inode->i_rdev);
    socket_info_t *s;
    user_info_t *user;

    DEBUG(0, "ds_open(socket %d)\n", i);
    if ((i >= sockets) || (sockets == 0))
	return -ENODEV;
    s = &socket_table[i];
    if ((file->f_flags & O_ACCMODE) != O_RDONLY) {
	if (s->state & SOCKET_BUSY)
	    return -EBUSY;
	else
	    s->state |= SOCKET_BUSY;
    }
    
    MOD_INC_USE_COUNT;
    user = kmalloc(sizeof(user_info_t), GFP_KERNEL);
    user->event_tail = user->event_head = 0;
    user->next = s->user;
    user->user_magic = USER_MAGIC;
    s->user = user;
    file->private_data = user;
    
    if (s->state & SOCKET_PRESENT)
	queue_event(user, CS_EVENT_CARD_INSERTION);
    return 0;
} /* ds_open */

/*====================================================================*/

static int ds_release(struct inode *inode, struct file *file)
{
    socket_t i = MINOR(inode->i_rdev);
    socket_info_t *s;
    user_info_t *user, **link;

    DEBUG(0, "ds_release(socket %d)\n", i);
    if ((i >= sockets) || (sockets == 0))
	return 0;
    s = &socket_table[i];
    user = file->private_data;
    if (CHECK_USER(user))
	return 0;

    /* Unlink user data structure */
    if ((file->f_flags & O_ACCMODE) != O_RDONLY)
	s->state &= ~SOCKET_BUSY;
    file->private_data = NULL;
    for (link = &s->user; *link; link = &(*link)->next)
	if (*link == user) break;
    if (link == NULL)
	return 0;
    *link = user->next;
    user->user_magic = 0;
    kfree_s(user, sizeof(user_info_t));
    
    MOD_DEC_USE_COUNT;
    return 0;
} /* ds_release */

/*====================================================================*/

static ssize_t ds_read(struct file *file, char *buf,
		       size_t count, loff_t *ppos)
{
    socket_t i = MINOR(file->f_dentry->d_inode->i_rdev);
    socket_info_t *s;
    user_info_t *user;

    DEBUG(2, "ds_read(socket %d)\n", i);
    
    if ((i >= sockets) || (sockets == 0))
	return -ENODEV;
    if (count < 4)
	return -EINVAL;
    s = &socket_table[i];
    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;
    
    if (queue_empty(user)) {
	interruptible_sleep_on(&s->queue);
	if (signal_pending(current))
	    return -EINTR;
    }
    put_user(get_queued_event(user), (int *)buf);
    return 4;
} /* ds_read */

/*====================================================================*/

static ssize_t ds_write(struct file *file, const char *buf,
			size_t count, loff_t *ppos)
{
    socket_t i = MINOR(file->f_dentry->d_inode->i_rdev);
    socket_info_t *s;
    user_info_t *user;

    DEBUG(2, "ds_write(socket %d)\n", i);
    
    if ((i >= sockets) || (sockets == 0))
	return -ENODEV;
    if (count != 4)
	return -EINVAL;
    if ((file->f_flags & O_ACCMODE) == O_RDONLY)
	return -EBADF;
    s = &socket_table[i];
    user = file->private_data;
    if (CHECK_USER(user))
	return -EIO;

    if (s->req_pending) {
	s->req_pending--;
	get_user(s->req_result, (int *)buf);
	if ((s->req_result != 0) || (s->req_pending == 0))
	    wake_up_interruptible(&s->request);
    } else
	return -EIO;

    return 4;
} /* ds_write */

/*====================================================================*/

static u_int ds_poll(struct file *file, poll_table *wait)
{
    socket_t i = MINOR(file->f_dentry->d_inode->i_rdev);
    socket_info_t *s;
    user_info_t *user;

    DEBUG(2, "ds_poll(socket %d)\n", i);
    
    if ((i >= sockets) || (sockets == 0))
	return POLLERR;
    s = &socket_table[i];
    user = file->private_data;
    if (CHECK_USER(user))
	return POLLERR;
    poll_wait(file, &s->queue, wait);
    if (!queue_empty(user))
	return POLLIN | POLLRDNORM;
    return 0;
} /* ds_poll */

/*====================================================================*/

static int ds_ioctl(struct inode * inode, struct file * file,
		    u_int cmd, u_long arg)
{
    socket_t i = MINOR(inode->i_rdev);
    socket_info_t *s;
    u_int size;
    int ret, err;
    ds_ioctl_arg_t buf;

    DEBUG(2, "ds_ioctl(socket %d, %#x, %#lx)\n", i, cmd, arg);
    
    if ((i >= sockets) || (sockets == 0))
	return -ENODEV;
    s = &socket_table[i];
    
    size = (cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT;
    if (size > sizeof(ds_ioctl_arg_t)) return -EINVAL;

    /* Permission check */
    if (!(cmd & IOC_OUT) && !suser())
	return -EPERM;
	
    if (cmd & IOC_IN) {
	err = verify_area(VERIFY_READ, (char *)arg, size);
	if (err) {
	    DEBUG(3, "ds_ioctl(): verify_read = %d\n", err);
	    return err;
	}
    }
    if (cmd & IOC_OUT) {
	err = verify_area(VERIFY_WRITE, (char *)arg, size);
	if (err) {
	    DEBUG(3, "ds_ioctl(): verify_write = %d\n", err);
	    return err;
	}
    }
    
    err = ret = 0;
    
    if (cmd & IOC_IN) copy_from_user((char *)&buf, (char *)arg, size);
    
    switch (cmd) {
    case DS_ADJUST_RESOURCE_INFO:
	ret = CardServices(AdjustResourceInfo, s->handle, &buf.adjust);
	break;
    case DS_GET_CARD_SERVICES_INFO:
	ret = CardServices(GetCardServicesInfo, &buf.servinfo);
	break;
    case DS_GET_CONFIGURATION_INFO:
	ret = CardServices(GetConfigurationInfo, s->handle, &buf.config);
	break;
    case DS_GET_FIRST_TUPLE:
	ret = CardServices(GetFirstTuple, s->handle, &buf.tuple);
	break;
    case DS_GET_NEXT_TUPLE:
	ret = CardServices(GetNextTuple, s->handle, &buf.tuple);
	break;
    case DS_GET_TUPLE_DATA:
	buf.tuple.TupleData = buf.tuple_parse.data;
	buf.tuple.TupleDataMax = sizeof(buf.tuple_parse.data);
	ret = CardServices(GetTupleData, s->handle, &buf.tuple);
	break;
    case DS_PARSE_TUPLE:
	buf.tuple.TupleData = buf.tuple_parse.data;
	ret = CardServices(ParseTuple, s->handle, &buf.tuple,
			   &buf.tuple_parse.parse);
	break;
    case DS_RESET_CARD:
	ret = CardServices(ResetCard, s->handle, NULL);
	break;
    case DS_GET_STATUS:
	ret = CardServices(GetStatus, s->handle, &buf.status);
	break;
    case DS_VALIDATE_CIS:
	ret = CardServices(ValidateCIS, s->handle, &buf.cisinfo);
	break;
    case DS_SUSPEND_CARD:
	ret = CardServices(SuspendCard, s->handle, NULL);
	break;
    case DS_RESUME_CARD:
	ret = CardServices(ResumeCard, s->handle, NULL);
	break;
    case DS_EJECT_CARD:
	ret = CardServices(EjectCard, s->handle, NULL);
	break;
    case DS_INSERT_CARD:
	ret = CardServices(InsertCard, s->handle, NULL);
	break;
    case DS_ACCESS_CONFIGURATION_REGISTER:
	if ((buf.conf_reg.Action == CS_WRITE) && !suser())
	    return -EPERM;
	ret = CardServices(AccessConfigurationRegister, s->handle,
			   &buf.conf_reg);
	break;
    case DS_GET_FIRST_REGION:
        ret = CardServices(GetFirstRegion, s->handle, &buf.region);
	break;
    case DS_GET_NEXT_REGION:
	ret = CardServices(GetNextRegion, s->handle, &buf.region);
	break;
    case DS_GET_FIRST_WINDOW:
	buf.win_info.handle = (window_handle_t)s->handle;
	ret = CardServices(GetFirstWindow, &buf.win_info.handle,
			   &buf.win_info.window);
	break;
    case DS_GET_NEXT_WINDOW:
	ret = CardServices(GetNextWindow, &buf.win_info.handle,
			   &buf.win_info.window);
	break;
    case DS_GET_MEM_PAGE:
	ret = CardServices(GetMemPage, buf.win_info.handle,
			   &buf.win_info.map);
	break;
    case DS_REPLACE_CIS:
	ret = CardServices(ReplaceCIS, s->handle, &buf.cisdump);
	break;
    case DS_BIND_REQUEST:
	if (!suser()) return -EPERM;
	err = bind_request(i, &buf.bind_info);
	break;
    case DS_GET_DEVICE_INFO:
	err = get_device_info(i, &buf.bind_info, 1);
	break;
    case DS_GET_NEXT_DEVICE:
	err = get_device_info(i, &buf.bind_info, 0);
	break;
    case DS_UNBIND_REQUEST:
	err = unbind_request(i, &buf.bind_info);
	break;
    case DS_BIND_MTD:
	if (!suser()) return -EPERM;
	err = bind_mtd(i, &buf.mtd_info);
	break;
    default:
	err = -EINVAL;
    }
    
    if ((err == 0) && (ret != CS_SUCCESS)) {
	DEBUG(2, "ds_ioctl: ret = %d\n", ret);
	switch (ret) {
	case CS_BAD_SOCKET: case CS_NO_CARD:
	    err = -ENODEV; break;
	case CS_BAD_ARGS: case CS_BAD_ATTRIBUTE: case CS_BAD_IRQ:
	case CS_BAD_TUPLE:
	    err = -EINVAL; break;
	case CS_IN_USE:
	    err = -EBUSY; break;
	case CS_OUT_OF_RESOURCE:
	    err = -ENOSPC; break;
	case CS_NO_MORE_ITEMS:
	    err = -ENODATA; break;
	case CS_UNSUPPORTED_FUNCTION:
	    err = -ENOSYS; break;
	default:
	    err = -EIO; break;
	}
    }
    
    if (cmd & IOC_OUT) copy_to_user((char *)arg, (char *)&buf, size);
     
    return err;
} /* ds_ioctl */

/*====================================================================*/

static struct file_operations ds_fops = {
    NULL,		/* lseek */
    ds_read,		/* read */
    ds_write,		/* write */
    NULL,		/* readdir */
    ds_poll,		/* poll */
    ds_ioctl,		/* ioctl */
    NULL,		/* mmap */
    ds_open,		/* open */
    NULL,		/* flush */
    ds_release,		/* release */
    NULL		/* fsync */
};

EXPORT_SYMBOL(register_pccard_driver);
EXPORT_SYMBOL(unregister_pccard_driver);

/*====================================================================*/

int __init init_pcmcia_ds(void)
{
    client_reg_t client_reg;
    servinfo_t serv;
    bind_req_t bind;
    socket_info_t *s;
    int i, ret;
    
    DEBUG(0, "%s\n", version);
    
    CardServices(GetCardServicesInfo, &serv);
    if (serv.Revision != CS_RELEASE_CODE) {
	printk(KERN_NOTICE "ds: Card Services release does not match!\n");
	return -1;
    }
    if (serv.Count == 0) {
	printk(KERN_NOTICE "ds: no socket drivers loaded!\n");
	return -1;
    }
    
    sockets = serv.Count;
    socket_table = kmalloc(sockets*sizeof(socket_info_t), GFP_KERNEL);
    for (i = 0, s = socket_table; i < sockets; i++, s++) {
	s->state = 0;
	s->user = NULL;
	s->req_pending = 0;
	init_waitqueue_head(&s->queue);
	init_waitqueue_head(&s->request);
	s->handle = NULL;
	s->removal.prev = s->removal.next = NULL;
	s->removal.data = i;
	s->removal.function = &handle_removal;
	s->bind = NULL;
    }
    
    /* Set up hotline to Card Services */
    client_reg.dev_info = bind.dev_info = &dev_info;
    client_reg.Attributes = INFO_MASTER_CLIENT;
    client_reg.EventMask =
	CS_EVENT_CARD_INSERTION | CS_EVENT_CARD_REMOVAL |
	CS_EVENT_RESET_PHYSICAL | CS_EVENT_CARD_RESET |
	CS_EVENT_EJECTION_REQUEST | CS_EVENT_INSERTION_REQUEST |
        CS_EVENT_PM_SUSPEND | CS_EVENT_PM_RESUME;
    client_reg.event_handler = &ds_event;
    client_reg.Version = 0x0210;
    for (i = 0; i < sockets; i++) {
	bind.Socket = i;
	bind.Function = BIND_FN_ALL;
	ret = CardServices(BindDevice, &bind);
	if (ret != CS_SUCCESS) {
	    cs_error(NULL, BindDevice, ret);
	    break;
	}
	client_reg.event_callback_args.client_data = &socket_table[i];
	ret = CardServices(RegisterClient, &socket_table[i].handle,
			   &client_reg);
	if (ret != CS_SUCCESS) {
	    cs_error(NULL, RegisterClient, ret);
	    break;
	}
    }
    
    /* Set up character device for user mode clients */
    i = register_chrdev(0, "pcmcia", &ds_fops);
    if (i == -EBUSY)
	printk(KERN_NOTICE "unable to find a free device # for "
	       "Driver Services\n");
    else
	major_dev = i;

#ifdef CONFIG_PROC_FS
    if (proc_pccard) {
	struct proc_dir_entry *ent;
	ent = create_proc_entry("drivers", 0, proc_pccard);
	ent->read_proc = proc_read_drivers;
    }
    init_status = 0;
#endif
    return 0;
}

#ifdef MODULE

int __init init_module(void)
{
    return init_pcmcia_ds();
}

void __exit cleanup_module(void)
{
    int i;
#ifdef CONFIG_PROC_FS
    if (proc_pccard)
	remove_proc_entry("drivers", proc_pccard);
#endif
    if (major_dev != -1)
	unregister_chrdev(major_dev, "pcmcia");
    for (i = 0; i < sockets; i++)
	CardServices(DeregisterClient, socket_table[i].handle);
    sockets = 0;
    kfree(socket_table);
}

#endif
