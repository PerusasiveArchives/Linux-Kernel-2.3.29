/*
 * dir.c
 *
 * Copyright (c) 1999 Al Smith
 */

#include <linux/efs_fs.h>

static int efs_readdir(struct file *, void *, filldir_t);

static struct file_operations efs_dir_operations = {
	NULL,			/* lseek */
	NULL,			/* read */
	NULL,			/* write */
	efs_readdir,		/* readdir */
	NULL,			/* poll */
	NULL,			/* ioctl */
	NULL,			/* mmap */
	NULL,			/* open */
	NULL,			/* flush */
	NULL,			/* release */
	NULL,			/* fsync */
	NULL,			/* fasync */
	NULL,			/* check_media_change */
	NULL			/* revalidate */
};

extern int efs_get_block(struct inode *, long, struct buffer_head *, int);

struct inode_operations efs_dir_inode_operations = {
	&efs_dir_operations,	/* default directory file-ops */
	NULL,			/* create */
	efs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	efs_get_block,		/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* flushpage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,			/* smap */
	NULL			/* revalidate */
};

static int efs_readdir(struct file *filp, void *dirent, filldir_t filldir) {
	struct inode *inode = filp->f_dentry->d_inode;
	struct buffer_head *bh;

	struct efs_dir		*dirblock;
	struct efs_dentry	*dirslot;
	efs_ino_t		inodenum;
	efs_block_t		block;
	int			slot, namelen;
	char			*nameptr;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	if (inode->i_size & (EFS_DIRBSIZE-1))
		printk(KERN_WARNING "EFS: WARNING: readdir(): directory size not a multiple of EFS_DIRBSIZE\n");

	/* work out where this entry can be found */
	block = filp->f_pos >> EFS_DIRBSIZE_BITS;

	/* each block contains at most 256 slots */
	slot  = filp->f_pos & 0xff;

	/* look at all blocks */
	while (block < inode->i_blocks) {
		/* read the dir block */
		bh = bread(inode->i_dev, efs_bmap(inode, block), EFS_DIRBSIZE);

		if (!bh) {
			printk(KERN_ERR "EFS: readdir(): failed to read dir block %d\n", block);
			break;
		}

		dirblock = (struct efs_dir *) bh->b_data; 

		if (be16_to_cpu(dirblock->magic) != EFS_DIRBLK_MAGIC) {
			printk(KERN_ERR "EFS: readdir(): invalid directory block\n");
			brelse(bh);
			break;
		}

		while (slot < dirblock->slots) {
			if (dirblock->space[slot] == 0) {
				slot++;
				continue;
			}

			dirslot  = (struct efs_dentry *) (((char *) bh->b_data) + EFS_SLOTAT(dirblock, slot));

			inodenum = be32_to_cpu(dirslot->inode);
			namelen  = dirslot->namelen;
			nameptr  = dirslot->name;

#ifdef DEBUG
			printk(KERN_DEBUG "EFS: readdir(): block %d slot %d/%d: inode %u, name \"%s\", namelen %u\n", block, slot, dirblock->slots-1, inodenum, nameptr, namelen);
#endif
			if (namelen > 0) {
				/* found the next entry */
				filp->f_pos = (block << EFS_DIRBSIZE_BITS) | slot;

				/* copy filename and data in dirslot */
				filldir(dirent, nameptr, namelen, filp->f_pos, inodenum);

				/* sanity check */
				if (nameptr - (char *) dirblock + namelen > EFS_DIRBSIZE) {
					printk(KERN_WARNING "EFS: directory entry %d exceeds directory block\n", slot);
					slot++;
					continue;
				}

				/* store position of next slot */
				if (++slot == dirblock->slots) {
					slot = 0;
					block++;
				}
				brelse(bh);
				filp->f_pos = (block << EFS_DIRBSIZE_BITS) | slot;
				return 0;
			}
			slot++;
		}
		brelse(bh);

		slot = 0;
		block++;
	}

	filp->f_pos = (block << EFS_DIRBSIZE_BITS) | slot;
	return 0;
}

