/* -*- linux-c -*- --------------------------------------------------------- *
 *
 * linux/fs/autofs/root.c
 *
 *  Copyright 1997-1998 Transmeta Corporation -- All Rights Reserved
 *
 * This file is part of the Linux kernel and is made available under
 * the terms of the GNU General Public License, version 2, or at your
 * option, any later version, incorporated herein by reference.
 *
 * ------------------------------------------------------------------------- */

#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/param.h>
#include "autofs_i.h"

static int autofs_root_readdir(struct file *,void *,filldir_t);
static struct dentry *autofs_root_lookup(struct inode *,struct dentry *);
static int autofs_root_symlink(struct inode *,struct dentry *,const char *);
static int autofs_root_unlink(struct inode *,struct dentry *);
static int autofs_root_rmdir(struct inode *,struct dentry *);
static int autofs_root_mkdir(struct inode *,struct dentry *,int);
static int autofs_root_ioctl(struct inode *, struct file *,unsigned int,unsigned long);

static struct file_operations autofs_root_operations = {
        NULL,                   /* llseek */
        NULL,                   /* read */
        NULL,                   /* write */
        autofs_root_readdir,    /* readdir */
        NULL,                   /* poll */
        autofs_root_ioctl,	/* ioctl */
        NULL,                   /* mmap */
        NULL,                   /* open */
	NULL,			/* flush */
        NULL,                   /* release */
        NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL,			/* revalidate */
	NULL			/* lock */
};

struct inode_operations autofs_root_inode_operations = {
        &autofs_root_operations, /* file operations */
        NULL,                   /* create */
        autofs_root_lookup,     /* lookup */
        NULL,                   /* link */
        autofs_root_unlink,     /* unlink */
        autofs_root_symlink,    /* symlink */
        autofs_root_mkdir,      /* mkdir */
        autofs_root_rmdir,      /* rmdir */
        NULL,                   /* mknod */
        NULL,                   /* rename */
        NULL,                   /* readlink */
        NULL,                   /* follow_link */
        NULL,                   /* get_block */
        NULL,                   /* readpage */
        NULL,                   /* writepage */
        NULL,                   /* flushpage */
        NULL,                   /* truncate */
        NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

static int autofs_root_readdir(struct file *filp, void *dirent, filldir_t filldir)
{
	struct autofs_dir_ent *ent = NULL;
	struct autofs_dirhash *dirhash;
	struct autofs_sb_info *sbi;
	struct inode * inode = filp->f_dentry->d_inode;
	off_t onr, nr;

	sbi = autofs_sbi(inode->i_sb);
	dirhash = &sbi->dirhash;
	nr = filp->f_pos;

	switch(nr)
	{
	case 0:
		if (filldir(dirent, ".", 1, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	case 1:
		if (filldir(dirent, "..", 2, nr, inode->i_ino) < 0)
			return 0;
		filp->f_pos = ++nr;
		/* fall through */
	default:
		while ( onr = nr, ent = autofs_hash_enum(dirhash,&nr,ent) ) {
			if ( !ent->dentry || ent->dentry->d_mounts != ent->dentry ) {
				if (filldir(dirent,ent->name,ent->len,onr,ent->ino) < 0)
					return 0;
				filp->f_pos = nr;
			}
		}
		break;
	}

	return 0;
}

static int try_to_fill_dentry(struct dentry *dentry, struct super_block *sb, struct autofs_sb_info *sbi)
{
	struct inode * inode;
	struct autofs_dir_ent *ent;
	int status = 0;

	if ( !(ent = autofs_hash_lookup(&sbi->dirhash, &dentry->d_name)) ) {
		do {
			if ( status && dentry->d_inode ) {
				if ( status != -ENOENT )
					printk("autofs warning: lookup failure on positive dentry, status = %d, name = %s\n", status, dentry->d_name.name);
				return 0; /* Try to get the kernel to invalidate this dentry */
			}

			/* Turn this into a real negative dentry? */
			if (status == -ENOENT) {
				dentry->d_time = jiffies + AUTOFS_NEGATIVE_TIMEOUT;
				dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
				return 1;
			} else if (status) {
				/* Return a negative dentry, but leave it "pending" */
				return 1;
			}
			status = autofs_wait(sbi, &dentry->d_name);
		} while (!(ent = autofs_hash_lookup(&sbi->dirhash, &dentry->d_name)) );
	}

	/* Abuse this field as a pointer to the directory entry, used to
	   find the expire list pointers */
	dentry->d_time = (unsigned long) ent;
	
	if (!dentry->d_inode) {
		inode = iget(sb, ent->ino);
		if (!inode) {
			/* Failed, but leave pending for next time */
			return 1;
		}
		dentry->d_inode = inode;
	}

	/* If this is a directory that isn't a mount point, bitch at the
	   daemon and fix it in user space */
	if ( S_ISDIR(dentry->d_inode->i_mode) && dentry->d_mounts == dentry ) {
		return !autofs_wait(sbi, &dentry->d_name);
	}

	/* We don't update the usages for the autofs daemon itself, this
	   is necessary for recursive autofs mounts */
	if ( !autofs_oz_mode(sbi) ) {
		autofs_update_usage(&sbi->dirhash,ent);
	}

	dentry->d_flags &= ~DCACHE_AUTOFS_PENDING;
	return 1;
}


/*
 * Revalidate is called on every cache lookup.  Some of those
 * cache lookups may actually happen while the dentry is not
 * yet completely filled in, and revalidate has to delay such
 * lookups..
 */
static int autofs_revalidate(struct dentry * dentry, int flags)
{
	struct inode * dir = dentry->d_parent->d_inode;
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_dir_ent *ent;

	/* Pending dentry */
	if ( dentry->d_flags & DCACHE_AUTOFS_PENDING ) {
		if (autofs_oz_mode(sbi))
			return 1;
		else
			return try_to_fill_dentry(dentry, dir->i_sb, sbi);
	}

	/* Negative dentry.. invalidate if "old" */
	if (!dentry->d_inode)
		return (dentry->d_time - jiffies <= AUTOFS_NEGATIVE_TIMEOUT);
		
	/* Check for a non-mountpoint directory */
	if ( S_ISDIR(dentry->d_inode->i_mode) && dentry->d_mounts == dentry ) {
		if (autofs_oz_mode(sbi))
			return 1;
		else
			return try_to_fill_dentry(dentry, dir->i_sb, sbi);
	}

	/* Update the usage list */
	if ( !autofs_oz_mode(sbi) ) {
		ent = (struct autofs_dir_ent *) dentry->d_time;
		if ( ent )
			autofs_update_usage(&sbi->dirhash,ent);
	}
	return 1;
}

static struct dentry_operations autofs_dentry_operations = {
	autofs_revalidate,	/* d_revalidate */
	NULL,			/* d_hash */
	NULL,			/* d_compare */
};

static struct dentry *autofs_root_lookup(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi;
	int oz_mode;

	DPRINTK(("autofs_root_lookup: name = "));
	autofs_say(dentry->d_name.name,dentry->d_name.len);

	if (dentry->d_name.len > NAME_MAX)
		return ERR_PTR(-ENOENT);/* File name too long to exist */

	sbi = autofs_sbi(dir->i_sb);

	oz_mode = autofs_oz_mode(sbi);
	DPRINTK(("autofs_lookup: pid = %u, pgrp = %u, catatonic = %d, oz_mode = %d\n",
		 current->pid, current->pgrp, sbi->catatonic, oz_mode));

	/*
	 * Mark the dentry incomplete, but add it. This is needed so
	 * that the VFS layer knows about the dentry, and we can count
	 * on catching any lookups through the revalidate.
	 *
	 * Let all the hard work be done by the revalidate function that
	 * needs to be able to do this anyway..
	 *
	 * We need to do this before we release the directory semaphore.
	 */
	dentry->d_op = &autofs_dentry_operations;
	dentry->d_flags |= DCACHE_AUTOFS_PENDING;
	d_add(dentry, NULL);

	up(&dir->i_sem);
	autofs_revalidate(dentry, 0);
	down(&dir->i_sem);

	/*
	 * If we are still pending, check if we had to handle
	 * a signal. If so we can force a restart..
	 */
	if (dentry->d_flags & DCACHE_AUTOFS_PENDING) {
		if (signal_pending(current))
			return ERR_PTR(-ERESTARTNOINTR);
	}

	/*
	 * If this dentry is unhashed, then we shouldn't honour this
	 * lookup even if the dentry is positive.  Returning ENOENT here
	 * doesn't do the right thing for all system calls, but it should
	 * be OK for the operations we permit from an autofs.
	 */
	if ( dentry->d_inode && list_empty(&dentry->d_hash) )
		return ERR_PTR(-ENOENT);

	return NULL;
}

static int autofs_root_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;
	unsigned int n;
	int slsize;
	struct autofs_symlink *sl;

	DPRINTK(("autofs_root_symlink: %s <- ", symname));
	autofs_say(dentry->d_name.name,dentry->d_name.len);

	if ( !autofs_oz_mode(sbi) )
		return -EACCES;

	if ( dentry->d_name.len > NAME_MAX )
		return -ENAMETOOLONG;

	if ( autofs_hash_lookup(dh, &dentry->d_name) )
		return -EEXIST;

	n = find_first_zero_bit(sbi->symlink_bitmap,AUTOFS_MAX_SYMLINKS);
	if ( n >= AUTOFS_MAX_SYMLINKS )
		return -ENOSPC;

	set_bit(n,sbi->symlink_bitmap);
	sl = &sbi->symlink[n];
	sl->len = strlen(symname);
	sl->data = kmalloc(slsize = sl->len+1, GFP_KERNEL);
	if ( !sl->data ) {
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}

	ent = kmalloc(sizeof(struct autofs_dir_ent), GFP_KERNEL);
	if ( !ent ) {
		kfree(sl->data);
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}

	ent->name = kmalloc(dentry->d_name.len+1, GFP_KERNEL);
	if ( !ent->name ) {
		kfree(sl->data);
		kfree(ent);
		clear_bit(n,sbi->symlink_bitmap);
		return -ENOSPC;
	}

	memcpy(sl->data,symname,slsize);
	sl->mtime = CURRENT_TIME;

	ent->ino = AUTOFS_FIRST_SYMLINK + n;
	ent->hash = dentry->d_name.hash;
	memcpy(ent->name, dentry->d_name.name, 1+(ent->len = dentry->d_name.len));
	ent->dentry = NULL;	/* We don't keep the dentry for symlinks */

	autofs_hash_insert(dh,ent);
	d_instantiate(dentry, iget(dir->i_sb,ent->ino));

	return 0;
}

/*
 * NOTE!
 *
 * Normal filesystems would do a "d_delete()" to tell the VFS dcache
 * that the file no longer exists. However, doing that means that the
 * VFS layer can turn the dentry into a negative dentry, which we
 * obviously do not want (we're dropping the entry not because it
 * doesn't exist, but because it has timed out).
 *
 * Also see autofs_root_rmdir()..
 */
static int autofs_root_unlink(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;
	unsigned int n;

	/* This allows root to remove symlinks */
	if ( !autofs_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EACCES;

	ent = autofs_hash_lookup(dh, &dentry->d_name);
	if ( !ent )
		return -ENOENT;

	n = ent->ino - AUTOFS_FIRST_SYMLINK;
	if ( n >= AUTOFS_MAX_SYMLINKS )
		return -EISDIR;	/* It's a directory, dummy */
	if ( !test_bit(n,sbi->symlink_bitmap) )
		return -EINVAL;	/* Nonexistent symlink?  Shouldn't happen */
	
	dentry->d_time = (unsigned long)(struct autofs_dirhash *)NULL;
	autofs_hash_delete(ent);
	clear_bit(n,sbi->symlink_bitmap);
	kfree(sbi->symlink[n].data);
	d_drop(dentry);
	
	return 0;
}

static int autofs_root_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;

	if ( !autofs_oz_mode(sbi) )
		return -EACCES;

	ent = autofs_hash_lookup(dh, &dentry->d_name);
	if ( !ent )
		return -ENOENT;

	if ( (unsigned int)ent->ino < AUTOFS_FIRST_DIR_INO )
		return -ENOTDIR; /* Not a directory */

	if ( ent->dentry != dentry ) {
		printk("autofs_rmdir: odentry != dentry for entry %s\n", dentry->d_name.name);
	}

	dentry->d_time = (unsigned long)(struct autofs_dir_ent *)NULL;
	autofs_hash_delete(ent);
	dir->i_nlink--;
	d_drop(dentry);

	return 0;
}

static int autofs_root_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct autofs_sb_info *sbi = autofs_sbi(dir->i_sb);
	struct autofs_dirhash *dh = &sbi->dirhash;
	struct autofs_dir_ent *ent;
	ino_t ino;

	if ( !autofs_oz_mode(sbi) )
		return -EACCES;

	if ( dentry->d_name.len > NAME_MAX )
		return -ENAMETOOLONG;

	ent = autofs_hash_lookup(dh, &dentry->d_name);
	if ( ent )
		return -EEXIST;

	if ( sbi->next_dir_ino < AUTOFS_FIRST_DIR_INO ) {
		printk("autofs: Out of inode numbers -- what the heck did you do??\n");
		return -ENOSPC;
	}
	ino = sbi->next_dir_ino++;

	ent = kmalloc(sizeof(struct autofs_dir_ent), GFP_KERNEL);
	if ( !ent )
		return -ENOSPC;

	ent->name = kmalloc(dentry->d_name.len+1, GFP_KERNEL);
	if ( !ent->name ) {
		kfree(ent);
		return -ENOSPC;
	}

	ent->hash = dentry->d_name.hash;
	memcpy(ent->name, dentry->d_name.name, 1+(ent->len = dentry->d_name.len));
	ent->ino = ino;
	ent->dentry = dentry;
	autofs_hash_insert(dh,ent);

	dir->i_nlink++;
	d_instantiate(dentry, iget(dir->i_sb,ino));

	return 0;
}

/* Get/set timeout ioctl() operation */
static inline int autofs_get_set_timeout(struct autofs_sb_info *sbi,
					 unsigned long *p)
{
	int rv;
	unsigned long ntimeout;

	if ( (rv = get_user(ntimeout, p)) ||
	     (rv = put_user(sbi->exp_timeout/HZ, p)) )
		return rv;

	if ( ntimeout > ULONG_MAX/HZ )
		sbi->exp_timeout = 0;
	else
		sbi->exp_timeout = ntimeout * HZ;

	return 0;
}

/* Return protocol version */
static inline int autofs_get_protover(int *p)
{
	return put_user(AUTOFS_PROTO_VERSION, p);
}

/* Perform an expiry operation */
static inline int autofs_expire_run(struct super_block *sb,
				    struct autofs_sb_info *sbi,
				    struct autofs_packet_expire *pkt_p)
{
	struct autofs_dir_ent *ent;
	struct autofs_packet_expire pkt;

	memset(&pkt,0,sizeof pkt);

	pkt.hdr.proto_version = AUTOFS_PROTO_VERSION;
	pkt.hdr.type = autofs_ptype_expire;

	if ( !sbi->exp_timeout ||
	     !(ent = autofs_expire(sb,sbi)) )
		return -EAGAIN;

	pkt.len = ent->len;
	memcpy(pkt.name, ent->name, pkt.len);
	pkt.name[pkt.len] = '\0';

	if ( copy_to_user(pkt_p, &pkt, sizeof(struct autofs_packet_expire)) )
		return -EFAULT;

	return 0;
}

/*
 * ioctl()'s on the root directory is the chief method for the daemon to
 * generate kernel reactions
 */
static int autofs_root_ioctl(struct inode *inode, struct file *filp,
			     unsigned int cmd, unsigned long arg)
{
	struct autofs_sb_info *sbi = autofs_sbi(inode->i_sb);

	DPRINTK(("autofs_ioctl: cmd = 0x%08x, arg = 0x%08lx, sbi = %p, pgrp = %u\n",cmd,arg,sbi,current->pgrp));

	if ( _IOC_TYPE(cmd) != _IOC_TYPE(AUTOFS_IOC_FIRST) ||
	     _IOC_NR(cmd) - _IOC_NR(AUTOFS_IOC_FIRST) >= AUTOFS_IOC_COUNT )
		return -ENOTTY;
	
	if ( !autofs_oz_mode(sbi) && !capable(CAP_SYS_ADMIN) )
		return -EPERM;
	
	switch(cmd) {
	case AUTOFS_IOC_READY:	/* Wait queue: go ahead and retry */
		return autofs_wait_release(sbi,(autofs_wqt_t)arg,0);
	case AUTOFS_IOC_FAIL:	/* Wait queue: fail with ENOENT */
		return autofs_wait_release(sbi,(autofs_wqt_t)arg,-ENOENT);
	case AUTOFS_IOC_CATATONIC: /* Enter catatonic mode (daemon shutdown) */
		autofs_catatonic_mode(sbi);
		return 0;
	case AUTOFS_IOC_PROTOVER: /* Get protocol version */
		return autofs_get_protover((int *)arg);
	case AUTOFS_IOC_SETTIMEOUT:
		return autofs_get_set_timeout(sbi,(unsigned long *)arg);
	case AUTOFS_IOC_EXPIRE:
		return autofs_expire_run(inode->i_sb,sbi,
					 (struct autofs_packet_expire *)arg);
	default:
		return -ENOSYS;
	}
}
