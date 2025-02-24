/*
 *	linux/mm/remap.c
 *
 *	(C) Copyright 1996 Linus Torvalds
 */

#include <linux/slab.h>
#include <linux/smp_lock.h>
#include <linux/shm.h>
#include <linux/mman.h>
#include <linux/swap.h>

#include <asm/uaccess.h>
#include <asm/pgalloc.h>

extern int vm_enough_memory(long pages);

static inline pte_t *get_one_pte(struct mm_struct *mm, unsigned long addr)
{
	pgd_t * pgd;
	pmd_t * pmd;
	pte_t * pte = NULL;

	pgd = pgd_offset(mm, addr);
	if (pgd_none(*pgd))
		goto end;
	if (pgd_bad(*pgd)) {
		pgd_ERROR(*pgd);
		pgd_clear(pgd);
		goto end;
	}

	pmd = pmd_offset(pgd, addr);
	if (pmd_none(*pmd))
		goto end;
	if (pmd_bad(*pmd)) {
		pmd_ERROR(*pmd);
		pmd_clear(pmd);
		goto end;
	}

	pte = pte_offset(pmd, addr);
	if (pte_none(*pte))
		pte = NULL;
end:
	return pte;
}

static inline pte_t *alloc_one_pte(struct mm_struct *mm, unsigned long addr)
{
	pmd_t * pmd;
	pte_t * pte = NULL;

	pmd = pmd_alloc(pgd_offset(mm, addr), addr);
	if (pmd)
		pte = pte_alloc(pmd, addr);
	return pte;
}

static inline int copy_one_pte(struct mm_struct *mm, pte_t * src, pte_t * dst)
{
	int error = 0;
	pte_t pte;

	spin_lock(&mm->page_table_lock);
	pte = *src;
	if (!pte_none(pte)) {
		error++;
		if (dst) {
			pte_clear(src);
			set_pte(dst, pte);
			error--;
		}
	}
	spin_unlock(&mm->page_table_lock);
	return error;
}

static int move_one_page(struct mm_struct *mm, unsigned long old_addr, unsigned long new_addr)
{
	int error = 0;
	pte_t * src;

	src = get_one_pte(mm, old_addr);
	if (src)
		error = copy_one_pte(mm, src, alloc_one_pte(mm, new_addr));
	return error;
}

static int move_page_tables(struct mm_struct * mm,
	unsigned long new_addr, unsigned long old_addr, unsigned long len)
{
	unsigned long offset = len;

	flush_cache_range(mm, old_addr, old_addr + len);

	/*
	 * This is not the clever way to do this, but we're taking the
	 * easy way out on the assumption that most remappings will be
	 * only a few pages.. This also makes error recovery easier.
	 */
	while (offset) {
		offset -= PAGE_SIZE;
		if (move_one_page(mm, old_addr + offset, new_addr + offset))
			goto oops_we_failed;
	}
	flush_tlb_range(mm, old_addr, old_addr + len);
	return 0;

	/*
	 * Ok, the move failed because we didn't have enough pages for
	 * the new page table tree. This is unlikely, but we have to
	 * take the possibility into account. In that case we just move
	 * all the pages back (this will work, because we still have
	 * the old page tables)
	 */
oops_we_failed:
	flush_cache_range(mm, new_addr, new_addr + len);
	while ((offset += PAGE_SIZE) < len)
		move_one_page(mm, new_addr + offset, old_addr + offset);
	zap_page_range(mm, new_addr, len);
	flush_tlb_range(mm, new_addr, new_addr + len);
	return -1;
}

static inline unsigned long move_vma(struct vm_area_struct * vma,
	unsigned long addr, unsigned long old_len, unsigned long new_len)
{
	struct vm_area_struct * new_vma;

	new_vma = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
	if (new_vma) {
		unsigned long new_addr = get_unmapped_area(addr, new_len);

		if (new_addr && !move_page_tables(current->mm, new_addr, addr, old_len)) {
			*new_vma = *vma;
			new_vma->vm_start = new_addr;
			new_vma->vm_end = new_addr+new_len;
			new_vma->vm_pgoff = vma->vm_pgoff;
			new_vma->vm_pgoff += (addr - vma->vm_start) >> PAGE_SHIFT;
			if (new_vma->vm_file)
				get_file(new_vma->vm_file);
			if (new_vma->vm_ops && new_vma->vm_ops->open)
				new_vma->vm_ops->open(new_vma);
			vmlist_modify_lock(current->mm);
			insert_vm_struct(current->mm, new_vma);
			merge_segments(current->mm, new_vma->vm_start, new_vma->vm_end);
			vmlist_modify_unlock(vma->vm_mm);
			do_munmap(addr, old_len);
			current->mm->total_vm += new_len >> PAGE_SHIFT;
			if (new_vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += new_len >> PAGE_SHIFT;
				make_pages_present(new_vma->vm_start,
						   new_vma->vm_end);
			}
			return new_addr;
		}
		kmem_cache_free(vm_area_cachep, new_vma);
	}
	return -ENOMEM;
}

/*
 * Expand (or shrink) an existing mapping, potentially moving it at the
 * same time (controlled by the MREMAP_MAYMOVE flag and available VM space)
 */
asmlinkage unsigned long sys_mremap(unsigned long addr,
	unsigned long old_len, unsigned long new_len,
	unsigned long flags)
{
	struct vm_area_struct *vma;
	unsigned long ret = -EINVAL;

	down(&current->mm->mmap_sem);
	if (addr & ~PAGE_MASK)
		goto out;
	old_len = PAGE_ALIGN(old_len);
	new_len = PAGE_ALIGN(new_len);

	/*
	 * Always allow a shrinking remap: that just unmaps
	 * the unnecessary pages..
	 */
	ret = addr;
	if (old_len >= new_len) {
		do_munmap(addr+new_len, old_len - new_len);
		goto out;
	}

	/*
	 * Ok, we need to grow..
	 */
	ret = -EFAULT;
	vma = find_vma(current->mm, addr);
	if (!vma || vma->vm_start > addr)
		goto out;
	/* We can't remap across vm area boundaries */
	if (old_len > vma->vm_end - addr)
		goto out;
	if (vma->vm_flags & VM_LOCKED) {
		unsigned long locked = current->mm->locked_vm << PAGE_SHIFT;
		locked += new_len - old_len;
		ret = -EAGAIN;
		if (locked > current->rlim[RLIMIT_MEMLOCK].rlim_cur)
			goto out;
	}
	ret = -ENOMEM;
	if ((current->mm->total_vm << PAGE_SHIFT) + (new_len - old_len)
	    > current->rlim[RLIMIT_AS].rlim_cur)
		goto out;
	/* Private writable mapping? Check memory availability.. */
	if ((vma->vm_flags & (VM_SHARED | VM_WRITE)) == VM_WRITE &&
	    !(flags & MAP_NORESERVE)				 &&
	    !vm_enough_memory((new_len - old_len) >> PAGE_SHIFT))
		goto out;

	/* old_len exactly to the end of the area.. */
	if (old_len == vma->vm_end - addr &&
	    (old_len != new_len || !(flags & MREMAP_MAYMOVE))) {
		unsigned long max_addr = TASK_SIZE;
		if (vma->vm_next)
			max_addr = vma->vm_next->vm_start;
		/* can we just expand the current mapping? */
		if (max_addr - addr >= new_len) {
			int pages = (new_len - old_len) >> PAGE_SHIFT;
			vmlist_modify_lock(vma->vm_mm);
			vma->vm_end = addr + new_len;
			vmlist_modify_unlock(vma->vm_mm);
			current->mm->total_vm += pages;
			if (vma->vm_flags & VM_LOCKED) {
				current->mm->locked_vm += pages;
				make_pages_present(addr + old_len,
						   addr + new_len);
			}
			ret = addr;
			goto out;
		}
	}

	/*
	 * We weren't able to just expand or shrink the area,
	 * we need to create a new one and move it..
	 */
	if (flags & MREMAP_MAYMOVE)
		ret = move_vma(vma, addr, old_len, new_len);
	else
		ret = -ENOMEM;
out:
	up(&current->mm->mmap_sem);
	return ret;
}
