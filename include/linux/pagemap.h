#ifndef _LINUX_PAGEMAP_H
#define _LINUX_PAGEMAP_H

/*
 * Page-mapping primitive inline functions
 *
 * Copyright 1995 Linus Torvalds
 */

#include <linux/mm.h>
#include <linux/fs.h>
#include <linux/list.h>

#include <asm/system.h>
#include <asm/pgtable.h>
#include <linux/highmem.h>

/*
 * The page cache can done in larger chunks than
 * one page, because it allows for more efficient
 * throughput (it can then be mapped into user
 * space in smaller chunks for same flexibility).
 *
 * Or rather, it _will_ be done in larger chunks.
 */
#define PAGE_CACHE_SHIFT	PAGE_SHIFT
#define PAGE_CACHE_SIZE		PAGE_SIZE
#define PAGE_CACHE_MASK		PAGE_MASK
#define PAGE_CACHE_ALIGN(addr)	(((addr)+PAGE_CACHE_SIZE-1)&PAGE_CACHE_MASK)

#define page_cache_alloc()	alloc_pages(GFP_HIGHUSER, 0)
#define page_cache_free(x)	__free_page(x)
#define page_cache_release(x)	__free_page(x)

/*
 * From a kernel address, get the "struct page *"
 */
#define page_cache_entry(x)	(mem_map + MAP_NR(x))

extern unsigned int page_hash_bits;
#define PAGE_HASH_BITS (page_hash_bits)
#define PAGE_HASH_SIZE (1 << PAGE_HASH_BITS)

extern atomic_t page_cache_size; /* # of pages currently in the hash table */
extern struct page **page_hash_table;

extern void page_cache_init(unsigned long);

/*
 * We use a power-of-two hash table to avoid a modulus,
 * and get a reasonable hash by knowing roughly how the
 * inode pointer and indexes are distributed (ie, we
 * roughly know which bits are "significant")
 *
 * For the time being it will work for struct address_space too (most of
 * them sitting inside the inodes). We might want to change it later.
 */
extern inline unsigned long _page_hashfn(struct address_space * mapping, unsigned long index)
{
#define i (((unsigned long) mapping)/(sizeof(struct inode) & ~ (sizeof(struct inode) - 1)))
#define s(x) ((x)+((x)>>PAGE_HASH_BITS))
	return s(i+index) & (PAGE_HASH_SIZE-1);
#undef i
#undef o
#undef s
}

#define page_hash(mapping,index) (page_hash_table+_page_hashfn(mapping,index))

extern struct page * __find_get_page (struct address_space *mapping,
				unsigned long index, struct page **hash);
#define find_get_page(mapping, index) \
		__find_get_page(mapping, index, page_hash(mapping, index))
extern struct page * __find_lock_page (struct address_space * mapping,
				unsigned long index, struct page **hash);
extern void lock_page(struct page *page);
#define find_lock_page(mapping, index) \
		__find_lock_page(mapping, index, page_hash(mapping, index))

extern void __add_page_to_hash_queue(struct page * page, struct page **p);

extern void add_to_page_cache(struct page * page, struct address_space *mapping, unsigned long index);
extern int add_to_page_cache_unique(struct page * page, struct address_space *mapping, unsigned long index, struct page **hash);

extern inline void add_page_to_hash_queue(struct page * page, struct inode * inode, unsigned long index)
{
	__add_page_to_hash_queue(page, page_hash(&inode->i_data,index));
}

extern inline void add_page_to_inode_queue(struct address_space *mapping, struct page * page)
{
	struct list_head *head = &mapping->pages;

	if (!mapping->nrpages++) {
		if (!list_empty(head))
			BUG();
	} else {
		if (list_empty(head))
			BUG();
	}
	list_add(&page->list, head);
	page->mapping = mapping;
}

extern inline void remove_page_from_inode_queue(struct page * page)
{
	struct address_space * mapping = page->mapping;

	mapping->nrpages--;
	list_del(&page->list);
}

extern void ___wait_on_page(struct page *);

extern inline void wait_on_page(struct page * page)
{
	if (PageLocked(page))
		___wait_on_page(page);
}

#endif
