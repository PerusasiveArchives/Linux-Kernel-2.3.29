/*
 * arch/m68k/atari/stram.c: Functions for ST-RAM allocations
 *
 * Copyright 1994-97 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/kdev_t.h>
#include <linux/major.h>
#include <linux/init.h>
#include <linux/swap.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/shm.h>

#include <asm/setup.h>
#include <asm/machdep.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/atarihw.h>
#include <asm/atari_stram.h>
#include <asm/io.h>
#include <asm/semaphore.h>


#ifdef CONFIG_STRAM_SWAP
#define MAJOR_NR    Z2RAM_MAJOR
#include <linux/blk.h>
#undef DEVICE_NAME
#define DEVICE_NAME	"stram"
#endif

#undef DEBUG

#ifdef DEBUG
#define	DPRINTK(fmt,args...) printk( fmt, ##args )
#else
#define DPRINTK(fmt,args...)
#endif

#if defined(CONFIG_PROC_FS) && defined(CONFIG_STRAM_PROC)
/* abbrev for the && above... */
#define DO_PROC
#include <linux/proc_fs.h>
#endif

/* Pre-swapping comments:
 *
 * ++roman:
 * 
 * New version of ST-Ram buffer allocation. Instead of using the
 * 1 MB - 4 KB that remain when the ST-Ram chunk starts at $1000
 * (1 MB granularity!), such buffers are reserved like this:
 *
 *  - If the kernel resides in ST-Ram anyway, we can take the buffer
 *    from behind the current kernel data space the normal way
 *    (incrementing start_mem).
 *    
 *  - If the kernel is in TT-Ram, stram_init() initializes start and
 *    end of the available region. Buffers are allocated from there
 *    and mem_init() later marks the such used pages as reserved.
 *    Since each TT-Ram chunk is at least 4 MB in size, I hope there
 *    won't be an overrun of the ST-Ram region by normal kernel data
 *    space.
 *    
 * For that, ST-Ram may only be allocated while kernel initialization
 * is going on, or exactly: before mem_init() is called. There is also
 * no provision now for freeing ST-Ram buffers. It seems that isn't
 * really needed.
 *
 */

/*
 * New Nov 1997: Use ST-RAM as swap space!
 *
 * In the past, there were often problems with modules that require ST-RAM
 * buffers. Such drivers have to use __get_dma_pages(), which unfortunately
 * often isn't very successful in allocating more than 1 page :-( [1] The net
 * result was that most of the time you couldn't insmod such modules (ataflop,
 * ACSI, SCSI on Falcon, Atari internal framebuffer, not to speak of acsi_slm,
 * which needs a 1 MB buffer... :-).
 *
 * To overcome this limitation, ST-RAM can now be turned into a very
 * high-speed swap space. If a request for an ST-RAM buffer comes, the kernel
 * now tries to unswap some pages on that swap device to make some free (and
 * contiguous) space. This works much better in comparison to
 * __get_dma_pages(), since used swap pages can be selectively freed by either
 * moving them to somewhere else in swap space, or by reading them back into
 * system memory. Ok, there operation of unswapping isn't really cheap (for
 * each page, one has to go through the page tables of all processes), but it
 * doesn't happen that often (only when allocation ST-RAM, i.e. when loading a
 * module that needs ST-RAM). But it at least makes it possible to load such
 * modules!
 *
 * It could also be that overall system performance increases a bit due to
 * ST-RAM swapping, since slow ST-RAM isn't used anymore for holding data or
 * executing code in. It's then just a (very fast, compared to disk) back
 * storage for not-so-often needed data. (But this effect must be compared
 * with the loss of total memory...) Don't know if the effect is already
 * visible on a TT, where the speed difference between ST- and TT-RAM isn't
 * that dramatic, but it should on machines where TT-RAM is really much faster
 * (e.g. Afterburner).
 * 
 *   [1]: __get_free_pages() does a fine job if you only want one page, but if
 * you want more (contiguous) pages, it can give you such a block only if
 * there's already a free one. The algorithm can't try to free buffers or swap
 * out something in order to make more free space, since all that page-freeing
 * mechanisms work "target-less", i.e. they just free something, but not in a
 * specific place. I.e., __get_free_pages() can't do anything to free
 * *adjacent* pages :-( This situation becomes even worse for DMA memory,
 * since the freeing algorithms are also blind to DMA capability of pages.
 */

/* 1998-10-20: ++andreas
   unswap_by_move disabled because it does not handle swapped shm pages.
*/

#ifdef CONFIG_STRAM_SWAP
#define ALIGN_IF_SWAP(x)	PAGE_ALIGN(x)
#else
#define ALIGN_IF_SWAP(x)	(x)
#endif

/* get index of swap page at address 'addr' */
#define SWAP_NR(addr)		(((unsigned long)(addr)-swap_start) >> PAGE_SHIFT)

/* get address of swap page #'nr' */
#define SWAP_ADDR(nr)		((void *)(swap_start + ((nr)<<PAGE_SHIFT)))

/* get number of pages for 'n' bytes (already page-aligned) */
#define N_PAGES(n)			((n) >> PAGE_SHIFT)

/* The following two numbers define the maximum fraction of ST-RAM in total
 * memory, below that the kernel would automatically use ST-RAM as swap
 * space. This decision can be overriden with stram_swap= */
#define MAX_STRAM_FRACTION_NOM		1
#define MAX_STRAM_FRACTION_DENOM	3

/* Start and end of the (pre-mem_init) reserved ST-RAM region */
static unsigned long rsvd_stram_beg, rsvd_stram_end;

/* Start and end (virtual) of ST-RAM */
static unsigned long stram_start, stram_end;

/* set after memory_init() executed and allocations via start_mem aren't
 * possible anymore */
static int mem_init_done = 0;

/* set if kernel is in ST-RAM */
static int kernel_in_stram;

typedef struct stram_block {
	struct stram_block *next;
	unsigned long start;
	unsigned long size;
	unsigned flags;
	const char *owner;
} BLOCK;

/* values for flags field */
#define BLOCK_FREE		0x01	/* free structure in the BLOCKs pool */
#define BLOCK_KMALLOCED	0x02	/* structure allocated by kmalloc() */
#define BLOCK_STATIC	0x04	/* pre-mem_init() allocated block */
#define BLOCK_GFP		0x08	/* block allocated with __get_dma_pages() */
#define BLOCK_INSWAP	0x10	/* block allocated in swap space */

/* list of allocated blocks */
static BLOCK *alloc_list = NULL;

/* We can't always use kmalloc() to allocate BLOCK structures, since
 * stram_alloc() can be called rather early. So we need some pool of
 * statically allocated structures. 20 of them is more than enough, so in most
 * cases we never should need to call kmalloc(). */
#define N_STATIC_BLOCKS	20
static BLOCK static_blocks[N_STATIC_BLOCKS];

#ifdef CONFIG_STRAM_SWAP
/* max. number of bytes to use for swapping
 *  0 = no ST-RAM swapping
 * -1 = do swapping (to whole ST-RAM) if it's less than MAX_STRAM_FRACTION of
 *      total memory
 */
static int max_swap_size = -1;

/* start and end of swapping area */
static unsigned long swap_start, swap_end;

/* The ST-RAM's swap info structure */
static struct swap_info_struct *stram_swap_info;

/* The ST-RAM's swap type */
static int stram_swap_type;

/* Semaphore for get_stram_region.  */
static DECLARE_MUTEX(stram_swap_sem);

/* major and minor device number of the ST-RAM device; for the major, we use
 * the same as Amiga z2ram, which is really similar and impossible on Atari,
 * and for the minor a relatively odd number to avoid the user creating and
 * using that device. */
#define	STRAM_MAJOR		Z2RAM_MAJOR
#define	STRAM_MINOR		13

/* Some impossible pointer value */
#define MAGIC_FILE_P	(struct file *)0xffffdead

#ifdef DO_PROC
static unsigned stat_swap_read = 0;
static unsigned stat_swap_write = 0;
static unsigned stat_swap_move = 0;
static unsigned stat_swap_force = 0;
#endif /* DO_PROC */

#endif /* CONFIG_STRAM_SWAP */

/***************************** Prototypes *****************************/

#ifdef CONFIG_STRAM_SWAP
static int swap_init( unsigned long start_mem, unsigned long swap_data );
static void *get_stram_region( unsigned long n_pages );
static void free_stram_region( unsigned long offset, unsigned long n_pages
			       );
static int in_some_region( unsigned long addr );
static unsigned long find_free_region( unsigned long n_pages, unsigned long
				       *total_free, unsigned long
				       *region_free );
static void do_stram_request( void );
static int stram_open( struct inode *inode, struct file *filp );
static int stram_release( struct inode *inode, struct file *filp );
static void do_z2_request( void );
#endif
static int get_gfp_order( unsigned long size );
static void reserve_region( unsigned long addr, unsigned long end );
static BLOCK *add_region( void *addr, unsigned long size );
static BLOCK *find_region( void *addr );
static int remove_region( BLOCK *block );

/************************* End of Prototypes **************************/


/* ------------------------------------------------------------------------ */
/*							   Public Interface								*/
/* ------------------------------------------------------------------------ */

/*
 * This init function is called very early by atari/config.c
 * It initializes some internal variables needed for stram_alloc()
 */
void __init atari_stram_init(void)
{
	int i;

	/* initialize static blocks */
	for( i = 0; i < N_STATIC_BLOCKS; ++i )
		static_blocks[i].flags = BLOCK_FREE;

	/* determine whether kernel code resides in ST-RAM (then ST-RAM is the
	 * first memory block at virtual 0x0) */
	stram_start = (unsigned long)phys_to_virt(0);
	kernel_in_stram = (stram_start == 0);

	for( i = 0; i < m68k_num_memory; ++i ) {
		if (m68k_memory[i].addr == 0) {
			/* skip first 2kB or page (supervisor-only!) */
			rsvd_stram_beg = stram_start + ALIGN_IF_SWAP(0x800);
			rsvd_stram_end = rsvd_stram_beg;
			stram_end = stram_start + m68k_memory[i].size;
			return;
		}
	}
	/* Should never come here! (There is always ST-Ram!) */
	panic( "atari_stram_init: no ST-RAM found!" );
}


/*
 * This function is called from mem_init() to reserve the pages needed for
 * ST-RAM management.
 */
void __init atari_stram_reserve_pages(unsigned long start_mem)
{
#ifdef CONFIG_STRAM_SWAP
	/* if max_swap_size is negative (i.e. no stram_swap= option given),
	 * determine at run time whether to use ST-RAM swapping */
	if (max_swap_size < 0)
		/* Use swapping if ST-RAM doesn't make up more than MAX_STRAM_FRACTION
		 * of total memory. In that case, the max. size is set to 16 MB,
		 * because ST-RAM can never be bigger than that.
		 * Also, never use swapping on a Hades, there's no separate ST-RAM in
		 * that machine. */
		max_swap_size =
			(!MACH_IS_HADES &&
			 (N_PAGES(stram_end-stram_start)*MAX_STRAM_FRACTION_DENOM <=
			  max_mapnr*MAX_STRAM_FRACTION_NOM)) ? 16*1024*1024 : 0;
	DPRINTK( "atari_stram_reserve_pages: max_swap_size = %d\n", max_swap_size );
#endif

	/* always reserve first page of ST-RAM, the first 2 kB are
	 * supervisor-only! */
	set_bit( PG_reserved, &mem_map[MAP_NR(stram_start)].flags );

#ifdef CONFIG_STRAM_SWAP
	if (!max_swap_size) {
	  fallback:
#endif
		DPRINTK( "atari_stram_reserve_pages: swapping disabled\n" );
		if (!kernel_in_stram) {
			/* Reserve all pages that have been marked by pre-mem_init
			 * stram_alloc() (e.g. for the screen memory). */
			reserve_region( rsvd_stram_beg, rsvd_stram_end );
			DPRINTK( "atari_stram_reserve_pages: reseverved %08lx-%08lx\n",
					 rsvd_stram_beg, rsvd_stram_end );
		}
		/* else (kernel in ST-RAM): nothing to do, ST-RAM buffers are
		 * kernel data */
#ifdef CONFIG_STRAM_SWAP
	}
	else {
		unsigned long swap_data;
		BLOCK *p;

		/* determine first page to use as swap:
		 * if the kernel is in TT-RAM, this is the first page of (usable)
		 * ST-RAM; else if there were already some allocations (probable...),
		 * use the lowest address of these (the list is sorted by address!);
		 * otherwise just use the end of kernel data (= start_mem) */
		swap_start = !kernel_in_stram ? stram_start + PAGE_SIZE :
					 alloc_list ? alloc_list->start :
					 start_mem;
		/* decrement by one page, rest of kernel assumes that first swap page
		 * is always reserved and maybe doesn't handle SWP_ENTRY == 0
		 * correctly */
		swap_start -= PAGE_SIZE;
		swap_end = stram_end;
		if (swap_end-swap_start > max_swap_size)
			swap_end =  swap_start + max_swap_size;
		DPRINTK( "atari_stram_reserve_pages: swapping enabled; "
				 "swap=%08lx-%08lx\n", swap_start, swap_end );
		
		/* reserve some amount of memory for maintainance of
		 * swapping itself: one page for each 2048 (PAGE_SIZE/2)
		 * swap pages. (2 bytes for each page) */
		swap_data = start_mem;
		start_mem += ((SWAP_NR(swap_end) + PAGE_SIZE/2 - 1)
			      >> (PAGE_SHIFT-1)) << PAGE_SHIFT;
		/* correct swap_start if necessary */
		if (swap_start == swap_data)
			swap_start = start_mem;
		
		if (!swap_init( start_mem, swap_data )) {
			printk( KERN_ERR "ST-RAM swap space initialization failed\n" );
			max_swap_size = 0;
			goto fallback;
		}
		/* reserve region for swapping meta-data */
		reserve_region( swap_data, start_mem );
		/* reserve swapping area itself */
		reserve_region( swap_start+PAGE_SIZE, swap_end );

		/* Formerly static areas have been included in the swap area. */
		for( p = alloc_list; p; p = p->next ) {
			if (p->flags & BLOCK_STATIC)
				p->flags = (p->flags & ~BLOCK_STATIC) | BLOCK_INSWAP;
		}

		/*
		 * If the whole ST-RAM is used for swapping, there are no allocatable
		 * dma pages left. But unfortunately, some shared parts of the kernel
		 * (particularily the SCSI mid-level) call __get_dma_pages()
		 * unconditionally :-( These calls then fail, and scsi.c even doesn't
		 * check for NULL return values and just crashes. The quick fix for
		 * this (instead of doing much clean up work in the SCSI code) is to
		 * pretend all pages are DMA-able by setting mach_max_dma_address to
		 * ULONG_MAX. This doesn't change any functionality so far, since
		 * get_dma_pages() shouldn't be used on Atari anyway anymore (better
		 * use atari_stram_alloc()), and the Atari SCSI drivers don't need DMA
		 * memory. But unfortunately there's now no kind of warning (even not
		 * a NULL return value) if you use get_dma_pages() nevertheless :-(
		 * You just will get non-DMA-able memory...
		 */
		mach_max_dma_address = 0xffffffff;

		/*
		 * Ok, num_physpages needs not be really exact, but it's better to
		 * subtract the pages set aside for swapping.
		 */
		num_physpages -= SWAP_NR(swap_end)-1;
	}
#endif
	
	mem_init_done = 1;
}


/*
 * This is main public interface: somehow allocate a ST-RAM block
 * There are three strategies:
 * 
 *  - If we're before mem_init(), we have to make a static allocation. The
 *    region is taken in the kernel data area (if the kernel is in ST-RAM) or
 *    from the start of ST-RAM (if the kernel is in TT-RAM) and added to the
 *    rsvd_stram_* region. The ST-RAM is somewhere in the middle of kernel
 *    address space in the latter case.
 * 
 *  - If mem_init() already has been called and ST-RAM swapping is enabled,
 *    try to get the memory from the (pseudo) swap-space, either free already
 *    or by moving some other pages out of the swap.
 *
 *  - If mem_init() already has been called, and ST-RAM swapping is not
 *    enabled, the only possibility is to try with __get_dma_pages(). This has
 *    the disadvantage that it's very hard to get more than 1 page, and it is
 *    likely to fail :-(
 * 
 */
void *atari_stram_alloc( long size, unsigned long *start_mem,
						 const char *owner )
{
	void *addr = NULL;
	BLOCK *block;
	int flags;

	DPRINTK( "atari_stram_alloc(size=%08lx,*start_mem=%08lx,owner=%s)\n",
			 size, start_mem ? *start_mem : 0xffffffff, owner );
	
	if (start_mem && mem_init_done) {
		printk( KERN_ERR "atari_stram_alloc called with start_mem!=NULL "
				"after mem_init() from %p\n", __builtin_return_address(0) );
		return( NULL );
	}
	if (!start_mem && !mem_init_done) {
		printk( KERN_ERR "atari_stram_alloc called with start_mem==NULL "
				"before mem_init() from %p\n", __builtin_return_address(0) );
		return( NULL );
	}

	size = ALIGN_IF_SWAP(size);
	DPRINTK( "atari_stram_alloc: rounded size = %08lx\n", size );
	if (!mem_init_done) {
		/* before mem_init(): allocate "statically", i.e. either in the kernel
		 * data space (current end in *start_mem), or at the end of currently
		 * reserved ST-RAM. */
		if (kernel_in_stram) {
			/* Get memory from kernel data space */
			*start_mem = ALIGN_IF_SWAP(*start_mem);
			addr = (void *)*start_mem;
			*start_mem += size;
			DPRINTK( "atari_stram_alloc: pre-mem_init and k/ST: "
					 "shifted start_mem to %08lx, addr=%p\n",
					 *start_mem, addr );
		}
		else {
			/* Get memory from rsvd_stram_beg */
			if (rsvd_stram_end + size < stram_end) {
				addr = (void *) rsvd_stram_end;
				rsvd_stram_end += size;
				DPRINTK( "atari_stram_alloc: pre-mem_init and k/TT: "
						 "shifted rsvd_stram_end to %08lx, addr=%p\n",
						 rsvd_stram_end, addr );
			}
		}
		flags = BLOCK_STATIC;
	}
#ifdef CONFIG_STRAM_SWAP
	else if (max_swap_size) {
		/* If swapping is active (can only be the case after mem_init()!):
		 * make some free space in the swap "device". */
		DPRINTK( "atari_stram_alloc: after mem_init, swapping ok, "
				 "calling get_region\n" );
		addr = get_stram_region( N_PAGES(size) );
		flags = BLOCK_INSWAP;
	}
#endif
	else {
		/* After mem_init() and no swapping: can only resort to
		 * __get_dma_pages() */
		addr = (void *)__get_dma_pages(GFP_KERNEL, get_gfp_order(size));
		flags = BLOCK_GFP;
		DPRINTK( "atari_stram_alloc: after mem_init, swapping off, "
				 "get_pages=%p\n", addr );
	}

	if (addr) {
		if (!(block = add_region( addr, size ))) {
			/* out of memory for BLOCK structure :-( */
			DPRINTK( "atari_stram_alloc: out of mem for BLOCK -- "
					 "freeing again\n" );
			if (flags == BLOCK_STATIC)
				rsvd_stram_end -= size;
#ifdef CONFIG_STRAM_SWAP
			else if (flags == BLOCK_INSWAP)
				free_stram_region( SWAP_NR(addr), N_PAGES(size) );
#endif
			else
				free_pages( (unsigned long)addr, get_gfp_order(size));
			return( NULL );
		}
		block->owner = owner;
		block->flags |= flags;
	}
	return( addr );
}

void atari_stram_free( void *addr )

{
	BLOCK *block;

	DPRINTK( "atari_stram_free(addr=%p)\n", addr );

	if (!(block = find_region( addr ))) {
		printk( KERN_ERR "Attempt to free non-allocated ST-RAM block at %p "
				"from %p\n", addr, __builtin_return_address(0) );
		return;
	}
	DPRINTK( "atari_stram_free: found block (%p): size=%08lx, owner=%s, "
			 "flags=%02x\n", block, block->size, block->owner, block->flags );
	
#ifdef CONFIG_STRAM_SWAP
	if (!max_swap_size) {
#endif
		if (block->flags & BLOCK_GFP) {
			DPRINTK( "atari_stram_free: is kmalloced, order_size=%d\n",
					 get_gfp_order(block->size) );
			free_pages( (unsigned long)addr, get_gfp_order(block->size) );
		}
		else
			goto fail;
#ifdef CONFIG_STRAM_SWAP
	}
	else if (block->flags & (BLOCK_INSWAP|BLOCK_STATIC)) {
		DPRINTK( "atari_stram_free: is swap-alloced\n" );
		free_stram_region( SWAP_NR(block->start), N_PAGES(block->size) );
	}
	else
		goto fail;
#endif
	remove_region( block );
	return;

  fail:
	printk( KERN_ERR "atari_stram_free: cannot free block at %p "
			"(called from %p)\n", addr, __builtin_return_address(0) );
}


#ifdef CONFIG_STRAM_SWAP


/* ------------------------------------------------------------------------ */
/*						   Main Swapping Functions							*/
/* ------------------------------------------------------------------------ */


/*
 * Initialize ST-RAM swap device
 * (lots copied and modified from sys_swapon() in mm/swapfile.c)
 */
static int __init swap_init(unsigned long start_mem, unsigned long swap_data)
{
	static struct dentry fake_dentry[3];
	struct swap_info_struct *p;
	struct inode swap_inode;
	unsigned int type;
	unsigned long addr;
	int i, j, k, prev;

	DPRINTK( "swap_init(start_mem=%08lx, swap_data=%08lx)\n",
			 start_mem, swap_data );
	
	/* need at least one page for swapping to (and this also isn't very
	 * much... :-) */
	if (swap_end - swap_start < 2*PAGE_SIZE) {
		printk( KERN_WARNING "stram_swap_init: swap space too small\n" );
		return( 0 );
	}
	
	/* find free slot in swap_info */
	for( p = swap_info, type = 0; type < nr_swapfiles; type++, p++ )
		if (!(p->flags & SWP_USED))
			break;
	if (type >= MAX_SWAPFILES) {
		printk( KERN_WARNING "stram_swap_init: max. number of "
				"swap devices exhausted\n" );
		return( 0 );
	}
	if (type >= nr_swapfiles)
		nr_swapfiles = type+1;

	stram_swap_info = p;
	stram_swap_type = type;

	/* fake some dir cache entries to give us some name in /dev/swaps */
	fake_dentry[0].d_covers = &fake_dentry[1];
	fake_dentry[0].d_parent = &fake_dentry[0];
	fake_dentry[1].d_parent = &fake_dentry[2];
	fake_dentry[1].d_name.name = "stram (internal)";
	fake_dentry[1].d_name.len = 16;
	fake_dentry[2].d_covers = &fake_dentry[2];
	fake_dentry[2].d_parent = &fake_dentry[2];
	
	p->flags        = SWP_USED;
	p->swap_file    = &fake_dentry[0];
	p->swap_device  = 0;
	p->swap_map	= (unsigned short *)swap_data;
	p->cluster_nr   = 0;
	p->next         = -1;
	p->prio         = 0x7ff0;	/* a rather high priority, but not the higest
								 * to give the user a chance to override */

	/* call stram_open() directly, avoids at least the overhead in
	 * constructing a dummy file structure... */
	p->swap_device = MKDEV( STRAM_MAJOR, STRAM_MINOR );
	swap_inode.i_rdev = p->swap_device;
	stram_open( &swap_inode, MAGIC_FILE_P );
	p->max = SWAP_NR(swap_end);

	/* initialize swap_map: set regions that are already allocated or belong
	 * to kernel data space to SWAP_MAP_BAD, otherwise to free */
	j = 0; /* # of free pages */
	k = 0; /* # of already allocated pages (from pre-mem_init stram_alloc()) */
	p->lowest_bit = 0;
	p->highest_bit = 0;
	for( i = 1, addr = (unsigned long)SWAP_ADDR(1); i < p->max;
		 i++, addr += PAGE_SIZE ) {
		if (in_some_region( addr )) {
			p->swap_map[i] = SWAP_MAP_BAD;
			++k;
		}
		else if (kernel_in_stram && addr < start_mem ) {
			p->swap_map[i] = SWAP_MAP_BAD;
		}
		else {
			p->swap_map[i] = 0;
			++j;
			if (!p->lowest_bit) p->lowest_bit = i;
			p->highest_bit = i;
		}
	}
	/* first page always reserved (and doesn't really belong to swap space) */
	p->swap_map[0] = SWAP_MAP_BAD;

	/* now swapping to this device ok */
	p->pages = j + k;
	nr_swap_pages += j;
	p->flags = SWP_WRITEOK;

	/* insert swap space into swap_list */
	prev = -1;
	for (i = swap_list.head; i >= 0; i = swap_info[i].next) {
		if (p->prio >= swap_info[i].prio) {
			break;
		}
		prev = i;
	}
	p->next = i;
	if (prev < 0) {
		swap_list.head = swap_list.next = p - swap_info;
	} else {
		swap_info[prev].next = p - swap_info;
	}

	printk( KERN_INFO "Using %dk (%d pages) of ST-RAM as swap space.\n",
			p->pages << 2, p->pages );
	return( 1 );
}


/*
 * The swap entry has been read in advance, and we return 1 to indicate
 * that the page has been used or is no longer needed.
 *
 * Always set the resulting pte to be nowrite (the same as COW pages
 * after one process has exited).  We don't know just how many PTEs will
 * share this swap entry, so be cautious and let do_wp_page work out
 * what to do if a write is requested later.
 */
static inline void unswap_pte(struct vm_area_struct * vma, unsigned long
			      address, pte_t *dir, unsigned long entry,
			      unsigned long page /*, int isswap */)
{
	pte_t pte = *dir;

	if (pte_none(pte))
		return;
	if (pte_present(pte)) {
		/* If this entry is swap-cached, then page must already
                   hold the right address for any copies in physical
                   memory */
		if (pte_page(pte) != page)
			return;
		if (0 /* isswap */)
			mem_map[MAP_NR(pte_page(pte))].offset = page;
		else
			/* We will be removing the swap cache in a moment, so... */
			set_pte(dir, pte_mkdirty(pte));
		return;
	}
	if (pte_val(pte) != entry)
		return;

	if (0 /* isswap */) {
		DPRINTK( "unswap_pte: replacing entry %08lx by %08lx", entry, page );
		set_pte(dir, __pte(page));
	}
	else {
		DPRINTK( "unswap_pte: replacing entry %08lx by new page %08lx",
				 entry, page );
		set_pte(dir, pte_mkdirty(mk_pte(page,vma->vm_page_prot)));
		atomic_inc(&mem_map[MAP_NR(page)].count);
		++vma->vm_mm->rss;
	}
	swap_free(entry);
}

static inline void unswap_pmd(struct vm_area_struct * vma, pmd_t *dir,
			      unsigned long address, unsigned long size,
			      unsigned long offset, unsigned long entry,
			      unsigned long page /* , int isswap */)
{
	pte_t * pte;
	unsigned long end;

	if (pmd_none(*dir))
		return;
	if (pmd_bad(*dir)) {
		printk("unswap_pmd: bad pmd (%08lx)\n", pmd_val(*dir));
		pmd_clear(dir);
		return;
	}
	pte = pte_offset(dir, address);
	offset += address & PMD_MASK;
	address &= ~PMD_MASK;
	end = address + size;
	if (end > PMD_SIZE)
		end = PMD_SIZE;
	do {
		unswap_pte(vma, offset+address-vma->vm_start, pte, entry,
			   page /* , isswap */);
		address += PAGE_SIZE;
		pte++;
	} while (address < end);
}

static inline void unswap_pgd(struct vm_area_struct * vma, pgd_t *dir,
			      unsigned long address, unsigned long size,
			      unsigned long entry, unsigned long page
			      /* , int isswap */)
{
	pmd_t * pmd;
	unsigned long offset, end;

	if (pgd_none(*dir))
		return;
	if (pgd_bad(*dir)) {
		printk("unswap_pgd: bad pgd (%08lx)\n", pgd_val(*dir));
		pgd_clear(dir);
		return;
	}
	pmd = pmd_offset(dir, address);
	offset = address & PGDIR_MASK;
	address &= ~PGDIR_MASK;
	end = address + size;
	if (end > PGDIR_SIZE)
		end = PGDIR_SIZE;
	do {
		unswap_pmd(vma, pmd, address, end - address, offset, entry,
			   page /* , isswap */);
		address = (address + PMD_SIZE) & PMD_MASK;
		pmd++;
	} while (address < end);
}

static void unswap_vma(struct vm_area_struct * vma, pgd_t *pgdir,
		       unsigned long entry, unsigned long page
		       /* , int isswap */)
{
	unsigned long start = vma->vm_start, end = vma->vm_end;

	while (start < end) {
		unswap_pgd(vma, pgdir, start, end - start, entry, page
			   /* , isswap */);
		start = (start + PGDIR_SIZE) & PGDIR_MASK;
		pgdir++;
	}
}

static void unswap_process(struct mm_struct * mm, unsigned long entry, 
			   unsigned long page /* , int isswap */)
{
	struct vm_area_struct* vma;

	/*
	 * Go through process' page directory.
	 */
	if (!mm)
		return;
	for (vma = mm->mmap; vma; vma = vma->vm_next) {
		pgd_t * pgd = pgd_offset(mm, vma->vm_start);
		unswap_vma(vma, pgd, entry, page /* , isswap */);
	}
}


#if 0
static int unswap_by_move(unsigned short *map, unsigned long max,
			  unsigned long start, unsigned long n_pages)
{
	struct task_struct *p;
	unsigned long entry, rover = (start == 1) ? n_pages+1 : 1;
	unsigned long i, j;

	DPRINTK( "unswapping %lu..%lu by moving in swap\n",
			 start, start+n_pages-1 );
	
	/* can free the allocated pages by moving them to other swap pages */
	for( i = start; i < start+n_pages; ++i ) {
		if (!map[i]) {
			map[i] = SWAP_MAP_BAD;
			DPRINTK( "unswap: page %lu was free\n", i );
			continue;
		}
		else if (map[i] == SWAP_MAP_BAD) {
			printk( KERN_ERR "get_stram_region: page %lu already "
					"reserved??\n", i );
		}
		DPRINTK( "unswap: page %lu is alloced, count=%u\n", i, map[i] );

		/* find a free page not in our region */
		for( j = rover; j != rover-1; j = (j == max-1) ? 1 : j+1 ) {
			if (j >= start && j < start+n_pages)
				continue;
			if (!map[j]) {
				rover = j+1;
				break;
			}
		}
		if (j == rover-1) {
			printk( KERN_ERR "get_stram_region: not enough free swap "
					"pages now??\n" );
			return( -ENOMEM );
		}
		DPRINTK( "unswap: map[i=%lu]=%u map[j=%lu]=%u nr_swap=%u\n",
				 i, map[i], j, map[j], nr_swap_pages );
		
		--nr_swap_pages;
		entry = SWP_ENTRY( stram_swap_type, j );
		if (stram_swap_info->lowest_bit == j)
			stram_swap_info->lowest_bit++;
		if (stram_swap_info->highest_bit == j)
			stram_swap_info->highest_bit--;
		
		memcpy( SWAP_ADDR(j), SWAP_ADDR(i), PAGE_SIZE );
#ifdef DO_PROC
		stat_swap_move++;
#endif

		while( map[i] ) {
			read_lock(&tasklist_lock);
			for_each_task(p) {
				if (unswap_process( p->mm, SWP_ENTRY( stram_swap_type, i ),
									entry, 1 )) {
					read_unlock(&tasklist_lock);
					map[j]++;
					goto repeat;
				}
			}
			read_unlock(&tasklist_lock);
			if (map[i] && map[i] != SWAP_MAP_MAX) {
				printk( KERN_ERR "get_stram_region: ST-RAM swap page %lu "
						"not used by any process\n", i );
				/* quit while loop and overwrite bad map entry */
				break;
			}
			else if (!map[i]) {
				/* somebody else must have swapped in that page, so free the
				 * new one (we're moving to) */
				DPRINTK( "unswap: map[i] became 0, also clearing map[j]\n" );
				map[j] = 0;
			}
		  repeat:
		}

		DPRINTK( "unswap: map[i=%lu]=%u map[j=%lu]=%u nr_swap=%u\n",
				 i, map[i], j, map[j], nr_swap_pages );
		map[i] = SWAP_MAP_BAD;
		if (stram_swap_info->lowest_bit == i)
			stram_swap_info->lowest_bit++;
		if (stram_swap_info->highest_bit == i)
			stram_swap_info->highest_bit--;
		--nr_swap_pages;
	}
	return( 0 );
}
#endif

static int unswap_by_read(unsigned short *map, unsigned long max,
			  unsigned long start, unsigned long n_pages)
{
	struct task_struct *p;
	unsigned long entry, page;
	unsigned long i;
	struct page *page_map;

	DPRINTK( "unswapping %lu..%lu by reading in\n",
			 start, start+n_pages-1 );

	for( i = start; i < start+n_pages; ++i ) {
		if (map[i] == SWAP_MAP_BAD) {
			printk( KERN_ERR "get_stram_region: page %lu already "
					"reserved??\n", i );
			continue;
		}

		if (map[i]) {
			entry = SWP_ENTRY(stram_swap_type, i);
			DPRINTK("unswap: map[i=%lu]=%u nr_swap=%u\n",
				i, map[i], nr_swap_pages);

			/* Get a page for the entry, using the existing
			   swap cache page if there is one.  Otherwise,
			   get a clean page and read the swap into it. */
			page_map = read_swap_cache(entry);
			if (page_map) {
				page = page_address(page_map);
				read_lock(&tasklist_lock);
				for_each_task(p)
					unswap_process(p->mm, entry, page
						       /* , 0 */);
				read_unlock(&tasklist_lock);
				shm_unuse(entry, page);
				/* Now get rid of the extra reference to
				   the temporary page we've been using. */
				if (PageSwapCache(page_map))
					delete_from_swap_cache(page_map);
				__free_page(page_map);
	#ifdef DO_PROC
				stat_swap_force++;
	#endif
			}
			else if (map[i])
				return -ENOMEM;
		}

		DPRINTK( "unswap: map[i=%lu]=%u nr_swap=%u\n",
				 i, map[i], nr_swap_pages );
		map[i] = SWAP_MAP_BAD;
		if (stram_swap_info->lowest_bit == i)
			stram_swap_info->lowest_bit++;
		if (stram_swap_info->highest_bit == i)
			stram_swap_info->highest_bit--;
		--nr_swap_pages;
	}

	return 0;
}

/*
 * reserve a region in ST-RAM swap space for an allocation
 */
static void *get_stram_region( unsigned long n_pages )
{
	unsigned short *map = stram_swap_info->swap_map;
	unsigned long max = stram_swap_info->max;
	unsigned long start, total_free, region_free;
	int err;
	void *ret = NULL;
	
	DPRINTK( "get_stram_region(n_pages=%lu)\n", n_pages );

	down(&stram_swap_sem);

	/* disallow writing to the swap device now */
	stram_swap_info->flags = SWP_USED;

	/* find a region of n_pages pages in the swap space including as much free
	 * pages as possible (and excluding any already-reserved pages). */
	if (!(start = find_free_region( n_pages, &total_free, &region_free )))
		goto end;
	DPRINTK( "get_stram_region: region starts at %lu, has %lu free pages\n",
			 start, region_free );

#if 0
	err = ((total_free-region_free >= n_pages-region_free) ?
		   unswap_by_move( map, max, start, n_pages ) :
		   unswap_by_read( map, max, start, n_pages ));
#else
	err = unswap_by_read(map, max, start, n_pages);
#endif

	if (err)
		goto end;

	ret = SWAP_ADDR(start);
  end:
	/* allow using swap device again */
	stram_swap_info->flags = SWP_WRITEOK;
	up(&stram_swap_sem);
	DPRINTK( "get_stram_region: returning %p\n", ret );
	return( ret );
}


/*
 * free a reserved region in ST-RAM swap space
 */
static void free_stram_region( unsigned long offset, unsigned long n_pages )
{
	unsigned short *map = stram_swap_info->swap_map;

	DPRINTK( "free_stram_region(offset=%lu,n_pages=%lu)\n", offset, n_pages );

	if (offset < 1 || offset + n_pages > stram_swap_info->max) {
		printk( KERN_ERR "free_stram_region: Trying to free non-ST-RAM\n" );
		return;
	}

	/* un-reserve the freed pages */
	for( ; n_pages > 0; ++offset, --n_pages ) {
		if (map[offset] != SWAP_MAP_BAD)
			printk( KERN_ERR "free_stram_region: Swap page %lu was not "
					"reserved\n", offset );
		map[offset] = 0;
	}

	/* update swapping meta-data */
	if (offset < stram_swap_info->lowest_bit)
		stram_swap_info->lowest_bit = offset;
	if (offset+n_pages-1 > stram_swap_info->highest_bit)
		stram_swap_info->highest_bit = offset+n_pages-1;
	if (stram_swap_info->prio > swap_info[swap_list.next].prio)
		swap_list.next = swap_list.head;
	nr_swap_pages += n_pages;
}


/* ------------------------------------------------------------------------ */
/*						Utility Functions for Swapping						*/
/* ------------------------------------------------------------------------ */


/* is addr in some of the allocated regions? */
static int in_some_region( unsigned long addr )
{
	BLOCK *p;
	
	for( p = alloc_list; p; p = p->next ) {
		if (p->start <= addr && addr < p->start + p->size)
			return( 1 );
	}
	return( 0 );
}


static unsigned long find_free_region(unsigned long n_pages,
				      unsigned long *total_free,
				      unsigned long *region_free)
{
	unsigned short *map = stram_swap_info->swap_map;
	unsigned long max = stram_swap_info->max;
	unsigned long head, tail, max_start;
	long nfree, max_free;

	/* first scan the swap space for a suitable place for the allocation */
	head = 1;
	max_start = 0;
	max_free = -1;
	*total_free = 0;

  start_over:
	/* increment tail until final window size reached, and count free pages */
	nfree = 0;
	for( tail = head; tail-head < n_pages && tail < max; ++tail ) {
		if (map[tail] == SWAP_MAP_BAD) {
			head = tail+1;
			goto start_over;
		}
		if (!map[tail]) {
			++nfree;
			++*total_free;
		}
	}
	if (tail-head < n_pages)
		goto out;
	if (nfree > max_free) {
		max_start = head;
		max_free  = nfree;
		if (max_free >= n_pages)
			/* don't need more free pages... :-) */
			goto out;
	}
	
	/* now shift the window and look for the area where as much pages as
	 * possible are free */
	while( tail < max ) {
		nfree -= (map[head++] == 0);
		if (map[tail] == SWAP_MAP_BAD) {
			head = tail+1;
			goto start_over;
		}
		if (!map[tail]) {
			++nfree;
			++*total_free;
		}
		++tail;
		if (nfree > max_free) {
			max_start = head;
			max_free  = nfree;
			if (max_free >= n_pages)
				/* don't need more free pages... :-) */
				goto out;
		}
	}

  out:
	if (max_free < 0) {
		printk( KERN_NOTICE "get_stram_region: ST-RAM too full or fragmented "
				"-- can't allocate %lu pages\n", n_pages );
		return( 0 );
	}

	*region_free = max_free;
	return( max_start );
}


/* setup parameters from command line */
void __init stram_swap_setup(char *str, int *ints)
{
	if (ints[0] >= 1)
		max_swap_size = ((ints[1] < 0 ? 0 : ints[1]) * 1024) & PAGE_MASK;
}


/* ------------------------------------------------------------------------ */
/*								ST-RAM device								*/
/* ------------------------------------------------------------------------ */

static int stram_blocksizes[14] = {
	0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 4096 };
static int stram_sizes[14] = {
	0, 0, 0, 0, 0, 0, 0, 0,	0, 0, 0, 0, 0, 0 };
static int refcnt = 0;

static void do_stram_request( void )
{
	unsigned long start, len;

	while( CURRENT ) {
		if (MAJOR(CURRENT->rq_dev) != MAJOR_NR)
			panic("stram: request list destroyed");
		if (CURRENT->bh) {
			if (!buffer_locked(CURRENT->bh))
				panic("stram: block not locked");
		}
		
		start = swap_start + (CURRENT->sector << 9);
		len   = CURRENT->current_nr_sectors << 9;
		if ((start + len) > swap_end) {
			printk( KERN_ERR "stram: bad access beyond end of device: "
					"block=%ld, count=%ld\n",
					CURRENT->sector,
					CURRENT->current_nr_sectors );
			end_request( 0 );
			continue;
		}

		if (CURRENT->cmd == READ) {
			memcpy( CURRENT->buffer, (char *)start, len );
#ifdef DO_PROC
			stat_swap_read += N_PAGES(len);
#endif
		}
		else {
			memcpy( (char *)start, CURRENT->buffer, len );
#ifdef DO_PROC
			stat_swap_write += N_PAGES(len);
#endif
		}
		end_request( 1 );
	}
}


static int stram_open( struct inode *inode, struct file *filp )
{
	if (filp != MAGIC_FILE_P) {
		printk( KERN_NOTICE "Only kernel can open ST-RAM device\n" );
		return( -EPERM );
	}
	if (MINOR(inode->i_rdev) != STRAM_MINOR)
		return( -ENXIO );
	if (refcnt)
		return( -EBUSY );
	++refcnt;
	return( 0 );
}

static int stram_release( struct inode *inode, struct file *filp )
{
	if (filp != MAGIC_FILE_P) {
		printk( KERN_NOTICE "Only kernel can close ST-RAM device\n" );
		return( -EPERM );
	}
	if (refcnt > 0)
		--refcnt;
	return( 0 );
}


static struct file_operations stram_fops = {
	NULL,                   /* lseek - default */
	block_read,             /* read - general block-dev read */
	block_write,            /* write - general block-dev write */
	NULL,                   /* readdir - bad */
	NULL,                   /* select */
	NULL,                   /* ioctl */
	NULL,                   /* mmap */
	stram_open,             /* open */
	NULL,			/* flush */
	stram_release,          /* release */
	block_fsync             /* fsync */
};

int __init stram_device_init(void)
{

    if (!MACH_IS_ATARI)
    	/* no point in initializing this, I hope */
	return( -ENXIO );

    if (!max_swap_size)
	/* swapping not enabled */
	return( -ENXIO );
	
    if (register_blkdev( STRAM_MAJOR, "stram", &stram_fops)) {
	printk( KERN_ERR "stram: Unable to get major %d\n", STRAM_MAJOR );
	return( -ENXIO );
    }

    blk_dev[STRAM_MAJOR].request_fn = do_stram_request;
    blksize_size[STRAM_MAJOR] = stram_blocksizes;
	stram_sizes[STRAM_MINOR] = (swap_end - swap_start)/1024;
    blk_size[STRAM_MAJOR] = stram_sizes;
	do_z2_request(); /* to avoid warning */
	return( 0 );
}

/* to avoid warning */
static void do_z2_request( void ) { }

#endif /* CONFIG_STRAM_SWAP */


/* ------------------------------------------------------------------------ */
/*							Misc Utility Functions							*/
/* ------------------------------------------------------------------------ */


/* return log2 of #pages for size */
static int get_gfp_order( unsigned long size )
{
	int order;

	size = N_PAGES( size + PAGE_SIZE -1 );
	order = -1;
	do {
		size >>= 1;
		order++;
	} while (size);

	return( order );
}


/* reserve a range of pages in mem_map[] */
static void reserve_region( unsigned long addr, unsigned long end )
{
	mem_map_t *mapp = &mem_map[MAP_NR(addr)];

	for( ; addr < end; addr += PAGE_SIZE, ++mapp )
		set_bit( PG_reserved, &mapp->flags );
}



/* ------------------------------------------------------------------------ */
/*							  Region Management								*/
/* ------------------------------------------------------------------------ */


/* insert a region into the alloced list (sorted) */
static BLOCK *add_region( void *addr, unsigned long size )
{
	BLOCK **p, *n = NULL;
	int i;

	for( i = 0; i < N_STATIC_BLOCKS; ++i ) {
		if (static_blocks[i].flags & BLOCK_FREE) {
			n = &static_blocks[i];
			n->flags = 0;
			break;
		}
	}
	if (!n && mem_init_done) {
		/* if statics block pool exhausted and we can call kmalloc() already
		 * (after mem_init()), try that */
		n = kmalloc( sizeof(BLOCK), GFP_KERNEL );
		if (n)
			n->flags = BLOCK_KMALLOCED;
	}
	if (!n) {
		printk( KERN_ERR "Out of memory for ST-RAM descriptor blocks\n" );
		return( NULL );
	}
	n->start = (unsigned long)addr;
	n->size  = size;

	for( p = &alloc_list; *p; p = &((*p)->next) )
		if ((*p)->start > (unsigned long)addr) break;
	n->next = *p;
	*p = n;

	return( n );
}


/* find a region (by start addr) in the alloced list */
static BLOCK *find_region( void *addr )
{
	BLOCK *p;
	
	for( p = alloc_list; p; p = p->next ) {
		if (p->start == (unsigned long)addr)
			return( p );
		if (p->start > (unsigned long)addr)
			break;
	}
	return( NULL );
}


/* remove a block from the alloced list */
static int remove_region( BLOCK *block )
{
	BLOCK **p;
	
	for( p = &alloc_list; *p; p = &((*p)->next) )
		if (*p == block) break;
	if (!*p)
		return( 0 );

	*p = block->next;
	if (block->flags & BLOCK_KMALLOCED)
		kfree( block );
	else
		block->flags |= BLOCK_FREE;
	return( 1 );
}



/* ------------------------------------------------------------------------ */
/*						 /proc statistics file stuff						*/
/* ------------------------------------------------------------------------ */

#ifdef DO_PROC

#define	PRINT_PROC(fmt,args...) len += sprintf( buf+len, fmt, ##args )

int get_stram_list( char *buf )
{
	int len = 0;
	BLOCK *p;
#ifdef CONFIG_STRAM_SWAP
	int i;
	unsigned short *map = stram_swap_info->swap_map;
	unsigned long max = stram_swap_info->max;
	unsigned free = 0, used = 0, rsvd = 0;
#endif

#ifdef CONFIG_STRAM_SWAP
	if (max_swap_size) {
		for( i = 1; i < max; ++i ) {
			if (!map[i])
				++free;
			else if (map[i] == SWAP_MAP_BAD)
				++rsvd;
			else
				++used;
		}
		PRINT_PROC(
			"Total ST-RAM:      %8lu kB\n"
			"Total ST-RAM swap: %8lu kB\n"
			"Free swap:         %8u kB\n"
			"Used swap:         %8u kB\n"
			"Allocated swap:    %8u kB\n"
			"Swap Reads:        %8u\n"
			"Swap Writes:       %8u\n"
			"Swap Moves:        %8u\n"
			"Swap Forced Reads: %8u\n",
			(stram_end - stram_start) >> 10,
			(max-1) << (PAGE_SHIFT-10),
			free << (PAGE_SHIFT-10),
			used << (PAGE_SHIFT-10),
			rsvd << (PAGE_SHIFT-10),
			stat_swap_read,
			stat_swap_write,
			stat_swap_move,
			stat_swap_force );
	}
	else {
#endif
		PRINT_PROC( "ST-RAM swapping disabled\n" );
		PRINT_PROC(
			"Total ST-RAM:      %8lu kB\n"
			"Reserved ST-RAM:   %8lu kB\n",
			(stram_end - stram_start) >> 10,
			(rsvd_stram_end - rsvd_stram_beg) >> 10 );
#ifdef CONFIG_STRAM_SWAP
	}
#endif

	PRINT_PROC( "Allocated regions:\n" );
	for( p = alloc_list; p; p = p->next ) {
		if (len + 50 >= PAGE_SIZE)
			break;
		PRINT_PROC("0x%08lx-0x%08lx: %s (",
			   virt_to_phys((void *)p->start),
			   virt_to_phys((void *)p->start+p->size-1),
			   p->owner);
		if (p->flags & BLOCK_STATIC)
			PRINT_PROC( "static)\n" );
		else if (p->flags & BLOCK_GFP)
			PRINT_PROC( "page-alloced)\n" );
		else if (p->flags & BLOCK_INSWAP)
			PRINT_PROC( "in swap)\n" );
		else
			PRINT_PROC( "??)\n" );
	}

	return( len );
}

#endif


/*
 * Local variables:
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
