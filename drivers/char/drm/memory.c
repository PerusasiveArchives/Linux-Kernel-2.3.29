/* memory.c -- Memory management wrappers for DRM -*- linux-c -*-
 * Created: Thu Feb  4 14:00:34 1999 by faith@precisioninsight.com
 * Revised: Fri Aug 20 13:04:33 1999 by faith@precisioninsight.com
 *
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 * 
 * $PI: xc/programs/Xserver/hw/xfree86/os-support/linux/drm/generic/memory.c,v 1.4 1999/08/20 20:00:53 faith Exp $
 * $XFree86$
 *
 */

#define __NO_VERSION__
#include "drmP.h"

typedef struct drm_mem_stats {
	const char	  *name;
	int		  succeed_count;
	int		  free_count;
	int		  fail_count;
	unsigned long	  bytes_allocated;
	unsigned long	  bytes_freed;
} drm_mem_stats_t;

static spinlock_t	  drm_mem_lock	    = SPIN_LOCK_UNLOCKED;
static unsigned long	  drm_ram_available = 0;
static unsigned long	  drm_ram_used	    = 0;
static drm_mem_stats_t	  drm_mem_stats[]   = {
	[DRM_MEM_DMA]	   = { "dmabufs"  },
	[DRM_MEM_SAREA]	   = { "sareas"	  },
	[DRM_MEM_DRIVER]   = { "driver"	  },
	[DRM_MEM_MAGIC]	   = { "magic"	  },
	[DRM_MEM_IOCTLS]   = { "ioctltab" },
	[DRM_MEM_MAPS]	   = { "maplist"  },
	[DRM_MEM_VMAS]	   = { "vmalist"  },
	[DRM_MEM_BUFS]	   = { "buflist"  },
	[DRM_MEM_SEGS]	   = { "seglist"  },
	[DRM_MEM_PAGES]	   = { "pagelist" },
	[DRM_MEM_FILES]	   = { "files"	  },
	[DRM_MEM_QUEUES]   = { "queues"	  },
	[DRM_MEM_CMDS]	   = { "commands" },
	[DRM_MEM_MAPPINGS] = { "mappings" },
	[DRM_MEM_BUFLISTS] = { "buflists" },
	{ NULL, 0, }		/* Last entry must be null */
};

void drm_mem_init(void)
{
	drm_mem_stats_t *mem;
	struct sysinfo	si;
	
	for (mem = drm_mem_stats; mem->name; ++mem) {
		mem->succeed_count   = 0;
		mem->free_count	     = 0;
		mem->fail_count	     = 0;
		mem->bytes_allocated = 0;
		mem->bytes_freed     = 0;
	}
	
	si_meminfo(&si);
	drm_ram_available = si.totalram;
	drm_ram_used	  = 0;
}

/* drm_mem_info is called whenever a process reads /dev/drm/mem. */

static int _drm_mem_info(char *buf, char **start, off_t offset, int len,
			 int *eof, void *data)
{
	drm_mem_stats_t *pt;

	if (offset > 0) return 0; /* no partial requests */
	len  = 0;
	*eof = 1;
	DRM_PROC_PRINT("		  total counts			"
		       " |    outstanding  \n");
	DRM_PROC_PRINT("type	   alloc freed fail	bytes	   freed"
		       " | allocs      bytes\n\n");
	DRM_PROC_PRINT("%-9.9s %5d %5d %4d %10lu	    |\n",
		       "system", 0, 0, 0, drm_ram_available);
	DRM_PROC_PRINT("%-9.9s %5d %5d %4d %10lu	    |\n",
		       "locked", 0, 0, 0, drm_ram_used);
	DRM_PROC_PRINT("\n");
	for (pt = drm_mem_stats; pt->name; pt++) {
		DRM_PROC_PRINT("%-9.9s %5d %5d %4d %10lu %10lu | %6d %10ld\n",
			       pt->name,
			       pt->succeed_count,
			       pt->free_count,
			       pt->fail_count,
			       pt->bytes_allocated,
			       pt->bytes_freed,
			       pt->succeed_count - pt->free_count,
			       (long)pt->bytes_allocated
			       - (long)pt->bytes_freed);
	}
	
	return len;
}

int drm_mem_info(char *buf, char **start, off_t offset, int len,
		 int *eof, void *data)
{
	int ret;
	
	spin_lock(&drm_mem_lock);
	ret = _drm_mem_info(buf, start, offset, len, eof, data);
	spin_unlock(&drm_mem_lock);
	return ret;
}

void *drm_alloc(size_t size, int area)
{
	void *pt;
	
	if (!size) {
		DRM_MEM_ERROR(area, "Allocating 0 bytes\n");
		return NULL;
	}
	
	if (!(pt = kmalloc(size, GFP_KERNEL))) {
		spin_lock(&drm_mem_lock);
		++drm_mem_stats[area].fail_count;
		spin_unlock(&drm_mem_lock);
		return NULL;
	}
	spin_lock(&drm_mem_lock);
	++drm_mem_stats[area].succeed_count;
	drm_mem_stats[area].bytes_allocated += size;
	spin_unlock(&drm_mem_lock);
	return pt;
}

void *drm_realloc(void *oldpt, size_t oldsize, size_t size, int area)
{
	void *pt;
	
	if (!(pt = drm_alloc(size, area))) return NULL;
	if (oldpt && oldsize) {
		memcpy(pt, oldpt, oldsize);
		drm_free(oldpt, oldsize, area);
	}
	return pt;
}

char *drm_strdup(const char *s, int area)
{
	char *pt;
	int	 length = s ? strlen(s) : 0;
	
	if (!(pt = drm_alloc(length+1, area))) return NULL;
	strcpy(pt, s);
	return pt;
}

void drm_strfree(const char *s, int area)
{
	unsigned int size;
	
	if (!s) return;
	
	size = 1 + (s ? strlen(s) : 0);
	drm_free((void *)s, size, area);
}

void drm_free(void *pt, size_t size, int area)
{
	int alloc_count;
	int free_count;
	
	if (!pt) DRM_MEM_ERROR(area, "Attempt to free NULL pointer\n");
	else	 kfree_s(pt, size);
	spin_lock(&drm_mem_lock);
	drm_mem_stats[area].bytes_freed += size;
	free_count  = ++drm_mem_stats[area].free_count;
	alloc_count =	drm_mem_stats[area].succeed_count;
	spin_unlock(&drm_mem_lock);
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(area, "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
}

unsigned long drm_alloc_pages(int order, int area)
{
	unsigned long address;
	unsigned long bytes	  = PAGE_SIZE << order;
	unsigned long addr;
	unsigned int  sz;
	
	spin_lock(&drm_mem_lock);
	if (drm_ram_used > +(DRM_RAM_PERCENT * drm_ram_available) / 100) {
		spin_unlock(&drm_mem_lock);
		return 0;
	}
	spin_unlock(&drm_mem_lock);
	
	address = __get_free_pages(GFP_KERNEL, order);
	if (!address) {
		spin_lock(&drm_mem_lock);
		++drm_mem_stats[area].fail_count;
		spin_unlock(&drm_mem_lock);
		return 0;
	}
	spin_lock(&drm_mem_lock);
	++drm_mem_stats[area].succeed_count;
	drm_mem_stats[area].bytes_allocated += bytes;
	drm_ram_used		            += bytes;
	spin_unlock(&drm_mem_lock);
	
	
				/* Zero outside the lock */
	memset((void *)address, 0, bytes);
	
				/* Reserve */
	for (addr = address, sz = bytes;
	     sz > 0;
	     addr += PAGE_SIZE, sz -= PAGE_SIZE) {
		mem_map_reserve(MAP_NR(addr));
	}
	
	return address;
}

void drm_free_pages(unsigned long address, int order, int area)
{
	unsigned long bytes = PAGE_SIZE << order;
	int		  alloc_count;
	int		  free_count;
	unsigned long addr;
	unsigned int  sz;
	
	if (!address) {
		DRM_MEM_ERROR(area, "Attempt to free address 0\n");
	} else {
				/* Unreserve */
		for (addr = address, sz = bytes;
		     sz > 0;
		     addr += PAGE_SIZE, sz -= PAGE_SIZE) {
			mem_map_unreserve(MAP_NR(addr));
		}
		free_pages(address, order);
	}
	
	spin_lock(&drm_mem_lock);
	free_count  = ++drm_mem_stats[area].free_count;
	alloc_count =	drm_mem_stats[area].succeed_count;
	drm_mem_stats[area].bytes_freed += bytes;
	drm_ram_used			-= bytes;
	spin_unlock(&drm_mem_lock);
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(area,
			      "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
}

void *drm_ioremap(unsigned long offset, unsigned long size)
{
	void *pt;
	
	if (!size) {
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Mapping 0 bytes at 0x%08lx\n", offset);
		return NULL;
	}
	
	if (!(pt = ioremap(offset, size))) {
		spin_lock(&drm_mem_lock);
		++drm_mem_stats[DRM_MEM_MAPPINGS].fail_count;
		spin_unlock(&drm_mem_lock);
		return NULL;
	}
	spin_lock(&drm_mem_lock);
	++drm_mem_stats[DRM_MEM_MAPPINGS].succeed_count;
	drm_mem_stats[DRM_MEM_MAPPINGS].bytes_allocated += size;
	spin_unlock(&drm_mem_lock);
	return pt;
}

void drm_ioremapfree(void *pt, unsigned long size)
{
	int alloc_count;
	int free_count;
	
	if (!pt)
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Attempt to free NULL pointer\n");
	else
		iounmap(pt);
	
	spin_lock(&drm_mem_lock);
	drm_mem_stats[DRM_MEM_MAPPINGS].bytes_freed += size;
	free_count  = ++drm_mem_stats[DRM_MEM_MAPPINGS].free_count;
	alloc_count =	drm_mem_stats[DRM_MEM_MAPPINGS].succeed_count;
	spin_unlock(&drm_mem_lock);
	if (free_count > alloc_count) {
		DRM_MEM_ERROR(DRM_MEM_MAPPINGS,
			      "Excess frees: %d frees, %d allocs\n",
			      free_count, alloc_count);
	}
}
