/*
 *  linux/fs/ext2/namei.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 *  Directory entry file type support and forward compatibility hooks
 *  	for B-tree directories by Theodore Ts'o (tytso@mit.edu), 1998
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/locks.h>
#include <linux/quotaops.h>



/*
 * define how far ahead to read directories while searching them.
 */
#define NAMEI_RA_CHUNKS  2
#define NAMEI_RA_BLOCKS  4
#define NAMEI_RA_SIZE        (NAMEI_RA_CHUNKS * NAMEI_RA_BLOCKS)
#define NAMEI_RA_INDEX(c,b)  (((c) * NAMEI_RA_BLOCKS) + (b))

/*
 * NOTE! unlike strncmp, ext2_match returns 1 for success, 0 for failure.
 *
 * `len <= EXT2_NAME_LEN' is guaranteed by caller.
 * `de != NULL' is guaranteed by caller.
 */
static inline int ext2_match (int len, const char * const name,
		       struct ext2_dir_entry_2 * de)
{
	if (len != de->name_len)
		return 0;
	if (!de->inode)
		return 0;
	return !memcmp(name, de->name, len);
}

/*
 *	ext2_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as a parameter - res_dir). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head * ext2_find_entry (struct inode * dir,
					     const char * const name, int namelen,
					     struct ext2_dir_entry_2 ** res_dir)
{
	struct super_block * sb;
	struct buffer_head * bh_use[NAMEI_RA_SIZE];
	struct buffer_head * bh_read[NAMEI_RA_SIZE];
	unsigned long offset;
	int block, toread, i, err;

	*res_dir = NULL;
	sb = dir->i_sb;

	if (namelen > EXT2_NAME_LEN)
		return NULL;

	memset (bh_use, 0, sizeof (bh_use));
	toread = 0;
	for (block = 0; block < NAMEI_RA_SIZE; ++block) {
		struct buffer_head * bh;

		if ((block << EXT2_BLOCK_SIZE_BITS (sb)) >= dir->i_size)
			break;
		bh = ext2_getblk (dir, block, 0, &err);
		bh_use[block] = bh;
		if (bh && !buffer_uptodate(bh))
			bh_read[toread++] = bh;
	}

	for (block = 0, offset = 0; offset < dir->i_size; block++) {
		struct buffer_head * bh;
		struct ext2_dir_entry_2 * de;
		char * dlimit;

		if ((block % NAMEI_RA_BLOCKS) == 0 && toread) {
			ll_rw_block (READ, toread, bh_read);
			toread = 0;
		}
		bh = bh_use[block % NAMEI_RA_SIZE];
		if (!bh) {
#if 0
			ext2_error (sb, "ext2_find_entry",
				    "directory #%lu contains a hole at offset %lu",
				    dir->i_ino, offset);
#endif
			offset += sb->s_blocksize;
			continue;
		}
		wait_on_buffer (bh);
		if (!buffer_uptodate(bh)) {
			/*
			 * read error: all bets are off
			 */
			break;
		}

		de = (struct ext2_dir_entry_2 *) bh->b_data;
		dlimit = bh->b_data + sb->s_blocksize;
		while ((char *) de < dlimit) {
			/* this code is executed quadratically often */
			/* do minimal checking `by hand' */
			int de_len;

			if ((char *) de + namelen <= dlimit &&
			    ext2_match (namelen, name, de)) {
				/* found a match -
				   just to be sure, do a full check */
				if (!ext2_check_dir_entry("ext2_find_entry",
							  dir, de, bh, offset))
					goto failure;
				for (i = 0; i < NAMEI_RA_SIZE; ++i) {
					if (bh_use[i] != bh)
						brelse (bh_use[i]);
				}
				*res_dir = de;
				return bh;
			}
			/* prevent looping on a bad block */
			de_len = le16_to_cpu(de->rec_len);
			if (de_len <= 0)
				goto failure;
			offset += de_len;
			de = (struct ext2_dir_entry_2 *)
				((char *) de + de_len);
		}

		brelse (bh);
		if (((block + NAMEI_RA_SIZE) << EXT2_BLOCK_SIZE_BITS (sb)) >=
		    dir->i_size)
			bh = NULL;
		else
			bh = ext2_getblk (dir, block + NAMEI_RA_SIZE, 0, &err);
		bh_use[block % NAMEI_RA_SIZE] = bh;
		if (bh && !buffer_uptodate(bh))
			bh_read[toread++] = bh;
	}

failure:
	for (i = 0; i < NAMEI_RA_SIZE; ++i)
		brelse (bh_use[i]);
	return NULL;
}

struct dentry *ext2_lookup(struct inode * dir, struct dentry *dentry)
{
	struct inode * inode;
	struct ext2_dir_entry_2 * de;
	struct buffer_head * bh;

	if (dentry->d_name.len > EXT2_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	bh = ext2_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de);
	inode = NULL;
	if (bh) {
		unsigned long ino = le32_to_cpu(de->inode);
		brelse (bh);
		inode = iget(dir->i_sb, ino);

		if (!inode)
			return ERR_PTR(-EACCES);
	}
	d_add(dentry, inode);
	return NULL;
}

/*
 *	ext2_add_entry()
 *
 * adds a file entry to the specified directory, using the same
 * semantics as ext2_find_entry(). It returns NULL if it failed.
 *
 * NOTE!! The inode part of 'de' is left at 0 - which means you
 * may not sleep between calling this and putting something into
 * the entry, as someone else might have used it while you slept.
 */
static struct buffer_head * ext2_add_entry (struct inode * dir,
					    const char * name, int namelen,
					    struct ext2_dir_entry_2 ** res_dir,
					    int *err)
{
	unsigned long offset;
	unsigned short rec_len;
	struct buffer_head * bh;
	struct ext2_dir_entry_2 * de, * de1;
	struct super_block * sb;

	*err = -EINVAL;
	*res_dir = NULL;
	if (!dir || !dir->i_nlink)
		return NULL;
	sb = dir->i_sb;

	if (!namelen)
		return NULL;
	/*
	 * Is this a busy deleted directory?  Can't create new files if so
	 */
	if (dir->i_size == 0)
	{
		*err = -ENOENT;
		return NULL;
	}
	bh = ext2_bread (dir, 0, 0, err);
	if (!bh)
		return NULL;
	rec_len = EXT2_DIR_REC_LEN(namelen);
	offset = 0;
	de = (struct ext2_dir_entry_2 *) bh->b_data;
	*err = -ENOSPC;
	while (1) {
		if ((char *)de >= sb->s_blocksize + bh->b_data) {
			brelse (bh);
			bh = NULL;
			bh = ext2_bread (dir, offset >> EXT2_BLOCK_SIZE_BITS(sb), 1, err);
			if (!bh)
				return NULL;
			if (dir->i_size <= offset) {
				if (dir->i_size == 0) {
					*err = -ENOENT;
					return NULL;
				}

				ext2_debug ("creating next block\n");

				de = (struct ext2_dir_entry_2 *) bh->b_data;
				de->inode = 0;
				de->rec_len = le16_to_cpu(sb->s_blocksize);
				dir->i_size = offset + sb->s_blocksize;
				dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
				mark_inode_dirty(dir);
			} else {

				ext2_debug ("skipping to next block\n");

				de = (struct ext2_dir_entry_2 *) bh->b_data;
			}
		}
		if (!ext2_check_dir_entry ("ext2_add_entry", dir, de, bh,
					   offset)) {
			*err = -ENOENT;
			brelse (bh);
			return NULL;
		}
		if (ext2_match (namelen, name, de)) {
				*err = -EEXIST;
				brelse (bh);
				return NULL;
		}
		if ((le32_to_cpu(de->inode) == 0 && le16_to_cpu(de->rec_len) >= rec_len) ||
		    (le16_to_cpu(de->rec_len) >= EXT2_DIR_REC_LEN(de->name_len) + rec_len)) {
			offset += le16_to_cpu(de->rec_len);
			if (le32_to_cpu(de->inode)) {
				de1 = (struct ext2_dir_entry_2 *) ((char *) de +
					EXT2_DIR_REC_LEN(de->name_len));
				de1->rec_len = cpu_to_le16(le16_to_cpu(de->rec_len) -
					EXT2_DIR_REC_LEN(de->name_len));
				de->rec_len = cpu_to_le16(EXT2_DIR_REC_LEN(de->name_len));
				de = de1;
			}
			de->inode = 0;
			de->name_len = namelen;
			de->file_type = 0;
			memcpy (de->name, name, namelen);
			/*
			 * XXX shouldn't update any times until successful
			 * completion of syscall, but too many callers depend
			 * on this.
			 *
			 * XXX similarly, too many callers depend on
			 * ext2_new_inode() setting the times, but error
			 * recovery deletes the inode, so the worst that can
			 * happen is that the times are slightly out of date
			 * and/or different from the directory change time.
			 */
			dir->i_mtime = dir->i_ctime = CURRENT_TIME;
			dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
			mark_inode_dirty(dir);
			dir->i_version = ++event;
			mark_buffer_dirty(bh, 1);
			*res_dir = de;
			*err = 0;
			return bh;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ext2_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	}
	brelse (bh);
	return NULL;
}

/*
 * ext2_delete_entry deletes a directory entry by merging it with the
 * previous entry
 */
static int ext2_delete_entry (struct ext2_dir_entry_2 * dir,
			      struct buffer_head * bh)
{
	struct ext2_dir_entry_2 * de, * pde;
	int i;

	i = 0;
	pde = NULL;
	de = (struct ext2_dir_entry_2 *) bh->b_data;
	while (i < bh->b_size) {
		if (!ext2_check_dir_entry ("ext2_delete_entry", NULL, 
					   de, bh, i))
			return -EIO;
		if (de == dir)  {
			if (pde)
				pde->rec_len =
					cpu_to_le16(le16_to_cpu(pde->rec_len) +
						    le16_to_cpu(dir->rec_len));
			else
				dir->inode = 0;
			return 0;
		}
		i += le16_to_cpu(de->rec_len);
		pde = de;
		de = (struct ext2_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	}
	return -ENOENT;
}

static inline void ext2_set_de_type(struct super_block *sb,
				struct ext2_dir_entry_2 *de,
				umode_t mode) {
	if (!EXT2_HAS_INCOMPAT_FEATURE(sb, EXT2_FEATURE_INCOMPAT_FILETYPE))
		return;
	if (S_ISCHR(mode))
		de->file_type = EXT2_FT_CHRDEV;
	else if (S_ISBLK(mode))
		de->file_type = EXT2_FT_BLKDEV;
	else if (S_ISFIFO(mode))  
		de->file_type = EXT2_FT_FIFO;
	else if (S_ISLNK(mode))
		de->file_type = EXT2_FT_SYMLINK;
	else if (S_ISREG(mode))
		de->file_type = EXT2_FT_REG_FILE;
	else if (S_ISDIR(mode))  
		de->file_type = EXT2_FT_DIR;
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate(). 
 */
int ext2_create (struct inode * dir, struct dentry * dentry, int mode)
{
	struct inode * inode;
	struct buffer_head * bh;
	struct ext2_dir_entry_2 * de;
	int err = -EIO;

	/*
	 * N.B. Several error exits in ext2_new_inode don't set err.
	 */
	inode = ext2_new_inode (dir, mode, &err);
	if (!inode)
		return err;

	inode->i_op = &ext2_file_inode_operations;
	inode->i_mode = mode;
	mark_inode_dirty(inode);
	bh = ext2_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh) {
		inode->i_nlink--;
		mark_inode_dirty(inode);
		iput (inode);
		return err;
	}
	de->inode = cpu_to_le32(inode->i_ino);
	ext2_set_de_type(dir->i_sb, de, S_IFREG);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);
	d_instantiate(dentry, inode);
	return 0;
}

int ext2_mknod (struct inode * dir, struct dentry *dentry, int mode, int rdev)
{
	struct inode * inode;
	struct buffer_head * bh;
	struct ext2_dir_entry_2 * de;
	int err = -EIO;

	inode = ext2_new_inode (dir, mode, &err);
	if (!inode)
		goto out;

	inode->i_uid = current->fsuid;
	init_special_inode(inode, mode, rdev);
	bh = ext2_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		goto out_no_entry;
	de->inode = cpu_to_le32(inode->i_ino);
	dir->i_version = ++event;
	ext2_set_de_type(dir->i_sb, de, inode->i_mode);
	mark_inode_dirty(inode);
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	d_instantiate(dentry, inode);
	brelse(bh);
	err = 0;
out:
	return err;

out_no_entry:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput(inode);
	goto out;
}

int ext2_mkdir(struct inode * dir, struct dentry * dentry, int mode)
{
	struct inode * inode;
	struct buffer_head * bh, * dir_block;
	struct ext2_dir_entry_2 * de;
	int err;

	err = -EMLINK;
	if (dir->i_nlink >= EXT2_LINK_MAX)
		goto out;

	err = -EIO;
	inode = ext2_new_inode (dir, S_IFDIR, &err);
	if (!inode)
		goto out;

	inode->i_op = &ext2_dir_inode_operations;
	inode->i_size = inode->i_sb->s_blocksize;
	inode->i_blocks = 0;	
	dir_block = ext2_bread (inode, 0, 1, &err);
	if (!dir_block) {
		inode->i_nlink--; /* is this nlink == 0? */
		mark_inode_dirty(inode);
		iput (inode);
		return err;
	}
	de = (struct ext2_dir_entry_2 *) dir_block->b_data;
	de->inode = cpu_to_le32(inode->i_ino);
	de->name_len = 1;
	de->rec_len = cpu_to_le16(EXT2_DIR_REC_LEN(de->name_len));
	strcpy (de->name, ".");
	ext2_set_de_type(dir->i_sb, de, S_IFDIR);
	de = (struct ext2_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	de->inode = cpu_to_le32(dir->i_ino);
	de->rec_len = cpu_to_le16(inode->i_sb->s_blocksize - EXT2_DIR_REC_LEN(1));
	de->name_len = 2;
	strcpy (de->name, "..");
	ext2_set_de_type(dir->i_sb, de, S_IFDIR);
	inode->i_nlink = 2;
	mark_buffer_dirty(dir_block, 1);
	brelse (dir_block);
	inode->i_mode = S_IFDIR | mode;
	if (dir->i_mode & S_ISGID)
		inode->i_mode |= S_ISGID;
	mark_inode_dirty(inode);
	bh = ext2_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		goto out_no_entry;
	de->inode = cpu_to_le32(inode->i_ino);
	ext2_set_de_type(dir->i_sb, de, S_IFDIR);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	dir->i_nlink++;
	dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
	mark_inode_dirty(dir);
	d_instantiate(dentry, inode);
	brelse (bh);
	err = 0;
out:
	return err;

out_no_entry:
	inode->i_nlink = 0;
	mark_inode_dirty(inode);
	iput (inode);
	goto out;
}

/*
 * routine to check that the specified directory is empty (for rmdir)
 */
static int empty_dir (struct inode * inode)
{
	unsigned long offset;
	struct buffer_head * bh;
	struct ext2_dir_entry_2 * de, * de1;
	struct super_block * sb;
	int err;

	sb = inode->i_sb;
	if (inode->i_size < EXT2_DIR_REC_LEN(1) + EXT2_DIR_REC_LEN(2) ||
	    !(bh = ext2_bread (inode, 0, 0, &err))) {
	    	ext2_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no data block",
			      inode->i_ino);
		return 1;
	}
	de = (struct ext2_dir_entry_2 *) bh->b_data;
	de1 = (struct ext2_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	if (le32_to_cpu(de->inode) != inode->i_ino || !le32_to_cpu(de1->inode) || 
	    strcmp (".", de->name) || strcmp ("..", de1->name)) {
	    	ext2_warning (inode->i_sb, "empty_dir",
			      "bad directory (dir #%lu) - no `.' or `..'",
			      inode->i_ino);
		brelse (bh);
		return 1;
	}
	offset = le16_to_cpu(de->rec_len) + le16_to_cpu(de1->rec_len);
	de = (struct ext2_dir_entry_2 *) ((char *) de1 + le16_to_cpu(de1->rec_len));
	while (offset < inode->i_size ) {
		if (!bh || (void *) de >= (void *) (bh->b_data + sb->s_blocksize)) {
			brelse (bh);
			bh = ext2_bread (inode, offset >> EXT2_BLOCK_SIZE_BITS(sb), 0, &err);
			if (!bh) {
#if 0
				ext2_error (sb, "empty_dir",
					    "directory #%lu contains a hole at offset %lu",
					    inode->i_ino, offset);
#endif
				offset += sb->s_blocksize;
				continue;
			}
			de = (struct ext2_dir_entry_2 *) bh->b_data;
		}
		if (!ext2_check_dir_entry ("empty_dir", inode, de, bh,
					   offset)) {
			brelse (bh);
			return 1;
		}
		if (le32_to_cpu(de->inode)) {
			brelse (bh);
			return 0;
		}
		offset += le16_to_cpu(de->rec_len);
		de = (struct ext2_dir_entry_2 *) ((char *) de + le16_to_cpu(de->rec_len));
	}
	brelse (bh);
	return 1;
}

int ext2_rmdir (struct inode * dir, struct dentry *dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct ext2_dir_entry_2 * de;

	retval = -ENOENT;
	bh = ext2_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de);
	if (!bh)
		goto end_rmdir;

	inode = dentry->d_inode;
	DQUOT_INIT(inode);

	retval = -EIO;
	if (le32_to_cpu(de->inode) != inode->i_ino)
		goto end_rmdir;

	retval = -ENOTEMPTY;
	if (!empty_dir (inode))
		goto end_rmdir;

	retval = ext2_delete_entry (de, bh);
	dir->i_version = ++event;
	if (retval)
		goto end_rmdir;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	if (inode->i_nlink != 2)
		ext2_warning (inode->i_sb, "ext2_rmdir",
			      "empty directory has nlink!=2 (%d)",
			      inode->i_nlink);
	inode->i_version = ++event;
	inode->i_nlink = 0;
	inode->i_size = 0;
	mark_inode_dirty(inode);
	dir->i_nlink--;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
	mark_inode_dirty(dir);
	d_delete(dentry);

end_rmdir:
	brelse (bh);
	return retval;
}

int ext2_unlink(struct inode * dir, struct dentry *dentry)
{
	int retval;
	struct inode * inode;
	struct buffer_head * bh;
	struct ext2_dir_entry_2 * de;

	retval = -ENOENT;
	bh = ext2_find_entry (dir, dentry->d_name.name, dentry->d_name.len, &de);
	if (!bh)
		goto end_unlink;

	inode = dentry->d_inode;
	DQUOT_INIT(inode);

	retval = -EIO;
	if (le32_to_cpu(de->inode) != inode->i_ino)
		goto end_unlink;
	
	if (!inode->i_nlink) {
		ext2_warning (inode->i_sb, "ext2_unlink",
			      "Deleting nonexistent file (%lu), %d",
			      inode->i_ino, inode->i_nlink);
		inode->i_nlink = 1;
	}
	retval = ext2_delete_entry (de, bh);
	if (retval)
		goto end_unlink;
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
	mark_inode_dirty(dir);
	inode->i_nlink--;
	mark_inode_dirty(inode);
	inode->i_ctime = dir->i_ctime;
	retval = 0;
	d_delete(dentry);	/* This also frees the inode */

end_unlink:
	brelse (bh);
	return retval;
}

int ext2_symlink (struct inode * dir, struct dentry *dentry, const char * symname)
{
	struct ext2_dir_entry_2 * de;
	struct inode * inode;
	struct buffer_head * bh = NULL, * name_block = NULL;
	char * link;
	int i, l, err = -EIO;
	char c;

	if (!(inode = ext2_new_inode (dir, S_IFLNK, &err))) {
		return err;
	}
	inode->i_mode = S_IFLNK | S_IRWXUGO;
	inode->i_op = &ext2_symlink_inode_operations;
	for (l = 0; l < inode->i_sb->s_blocksize - 1 &&
	     symname [l]; l++)
		;
	if (l >= sizeof (inode->u.ext2_i.i_data)) {

		ext2_debug ("l=%d, normal symlink\n", l);

		name_block = ext2_bread (inode, 0, 1, &err);
		if (!name_block) {
			inode->i_nlink--;
			mark_inode_dirty(inode);
			iput (inode);
			return err;
		}
		link = name_block->b_data;
	} else {
		link = (char *) inode->u.ext2_i.i_data;

		ext2_debug ("l=%d, fast symlink\n", l);

	}
	i = 0;
	while (i < inode->i_sb->s_blocksize - 1 && (c = *(symname++)))
		link[i++] = c;
	link[i] = 0;
	if (name_block) {
		mark_buffer_dirty(name_block, 1);
		brelse (name_block);
	}
	inode->i_size = i;
	mark_inode_dirty(inode);

	bh = ext2_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		goto out_no_entry;
	de->inode = cpu_to_le32(inode->i_ino);
	ext2_set_de_type(dir->i_sb, de, S_IFLNK);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);
	d_instantiate(dentry, inode);
	err = 0;
out:
	return err;

out_no_entry:
	inode->i_nlink--;
	mark_inode_dirty(inode);
	iput (inode);
	goto out;
}

int ext2_link (struct dentry * old_dentry,
		struct inode * dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;
	struct ext2_dir_entry_2 * de;
	struct buffer_head * bh;
	int err;

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	if (inode->i_nlink >= EXT2_LINK_MAX)
		return -EMLINK;

	bh = ext2_add_entry (dir, dentry->d_name.name, dentry->d_name.len, &de, &err);
	if (!bh)
		return err;

	de->inode = cpu_to_le32(inode->i_ino);
	ext2_set_de_type(dir->i_sb, de, inode->i_mode);
	dir->i_version = ++event;
	mark_buffer_dirty(bh, 1);
	if (IS_SYNC(dir)) {
		ll_rw_block (WRITE, 1, &bh);
		wait_on_buffer (bh);
	}
	brelse (bh);
	inode->i_nlink++;
	inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	inode->i_count++;
	d_instantiate(dentry, inode);
	return 0;
}

#define PARENT_INO(buffer) \
	((struct ext2_dir_entry_2 *) ((char *) buffer + \
	le16_to_cpu(((struct ext2_dir_entry_2 *) buffer)->rec_len)))->inode

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
int ext2_rename (struct inode * old_dir, struct dentry *old_dentry,
			   struct inode * new_dir,struct dentry *new_dentry)
{
	struct inode * old_inode, * new_inode;
	struct buffer_head * old_bh, * new_bh, * dir_bh;
	struct ext2_dir_entry_2 * old_de, * new_de;
	int retval;

	old_bh = new_bh = dir_bh = NULL;

	old_bh = ext2_find_entry (old_dir, old_dentry->d_name.name, old_dentry->d_name.len, &old_de);
	/*
	 *  Check for inode number is _not_ due to possible IO errors.
	 *  We might rmdir the source, keep it as pwd of some process
	 *  and merrily kill the link to whatever was created under the
	 *  same name. Goodbye sticky bit ;-<
	 */
	old_inode = old_dentry->d_inode;
	retval = -ENOENT;
	if (!old_bh || le32_to_cpu(old_de->inode) != old_inode->i_ino)
		goto end_rename;

	new_inode = new_dentry->d_inode;
	new_bh = ext2_find_entry (new_dir, new_dentry->d_name.name,
				new_dentry->d_name.len, &new_de);
	if (new_bh) {
		if (!new_inode) {
			brelse (new_bh);
			new_bh = NULL;
		} else {
			DQUOT_INIT(new_inode);
		}
	}
	if (S_ISDIR(old_inode->i_mode)) {
		if (new_inode) {
			retval = -ENOTEMPTY;
			if (!empty_dir (new_inode))
				goto end_rename;
		}
		retval = -EIO;
		dir_bh = ext2_bread (old_inode, 0, 0, &retval);
		if (!dir_bh)
			goto end_rename;
		if (le32_to_cpu(PARENT_INO(dir_bh->b_data)) != old_dir->i_ino)
			goto end_rename;
		retval = -EMLINK;
		if (!new_inode && new_dir!=old_dir &&
				new_dir->i_nlink >= EXT2_LINK_MAX)
			goto end_rename;
	}
	if (!new_bh) {
		new_bh = ext2_add_entry (new_dir, new_dentry->d_name.name,
					new_dentry->d_name.len, &new_de,
					&retval);
		if (!new_bh)
			goto end_rename;
	}
	new_dir->i_version = ++event;

	/*
	 * ok, that's it
	 */
	new_de->inode = le32_to_cpu(old_inode->i_ino);
	if (EXT2_HAS_INCOMPAT_FEATURE(new_dir->i_sb,
				      EXT2_FEATURE_INCOMPAT_FILETYPE))
		new_de->file_type = old_de->file_type;
	
	ext2_delete_entry (old_de, old_bh);

	old_dir->i_version = ++event;
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime = CURRENT_TIME;
		mark_inode_dirty(new_inode);
	}
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	old_dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
	mark_inode_dirty(old_dir);
	if (dir_bh) {
		PARENT_INO(dir_bh->b_data) = le32_to_cpu(new_dir->i_ino);
		mark_buffer_dirty(dir_bh, 1);
		old_dir->i_nlink--;
		mark_inode_dirty(old_dir);
		if (new_inode) {
			new_inode->i_nlink--;
			mark_inode_dirty(new_inode);
		} else {
			new_dir->i_nlink++;
			new_dir->u.ext2_i.i_flags &= ~EXT2_BTREE_FL;
			mark_inode_dirty(new_dir);
		}
	}
	mark_buffer_dirty(old_bh,  1);
	if (IS_SYNC(old_dir)) {
		ll_rw_block (WRITE, 1, &old_bh);
		wait_on_buffer (old_bh);
	}
	mark_buffer_dirty(new_bh, 1);
	if (IS_SYNC(new_dir)) {
		ll_rw_block (WRITE, 1, &new_bh);
		wait_on_buffer (new_bh);
	}

	retval = 0;

end_rename:
	brelse (dir_bh);
	brelse (old_bh);
	brelse (new_bh);
	return retval;
}
