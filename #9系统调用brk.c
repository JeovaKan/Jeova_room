/*
 * brk()系统调用
 */

虚拟内存中（32位）：

#1 系统空间1GB，用户空间3GB。

#2 进程可用的空间是3GB，不代表进程可以用到全部的3GB空间。

#3 进程从磁盘加载到内存时，elf格式的可执行文件至少具有代码段、数据段（data和bss）
   代码段（映射）放置在3GB空间的底部，数据段中包含全局变量和static类型变量必须分配内存
   的，以及可能用到的堆空间，栈空间为预先分配好的。

#4 堆空间可以扩展，自高地址往低地址，代码段以及数据段必要空间从低地址向高地址生长。这两者之间有
   空洞，数据段end_data到当前堆空间底部，就是运行时可动态分配的空间。

#5 系统调用 brk()既可以分配，也可以释放这段空间。

#6 描述一下这个空间的生长规律吧，综上可知，虚拟空间的空洞呢，进程代码段、数据段的结束位置ptr1这个位置是
   根据进程本身而固定的，进程堆的下方ptr2这个位置是可以扩展的（因为中间本身就是空洞嘛，为啥不给别人用呢），
   所以呢，系统根据当前ptr1位置，ptr2位置，确定当前空洞，vmalloc从空洞中申请虚拟空间以后，就要重新调整ptr1~2的
   空洞的大小了。就是移动ptr1上方，ptr3的位置。

    mmap.c-->sys_brk()

	asmlinkage unsigned long sys_brk(unsigned long brk)
	{
		unsigned long rlim, retval;
		unsigned long newbrk, oldbrk;
		struct mm_struct *mm = current->mm;

		down(&mm->mmap_sem);

		if (brk < mm->end_code)
			goto out;
		newbrk = PAGE_ALIGN(brk);
		oldbrk = PAGE_ALIGN(mm->brk);
		if (oldbrk == newbrk)
			goto set_brk;

		/*
		 * 新的边界小于原有的边界
		 * 那么收缩这一段空间
		 */
		if (brk <= mm->brk) {
			if (!do_munmap(mm, newbrk, oldbrk-newbrk))
				goto set_brk;
			goto out;
		}

		/* Check against rlimit.. */
		rlim = current->rlim[RLIMIT_DATA].rlim_cur;
		if (rlim < RLIM_INFINITY && brk - mm->start_data > rlim)
			goto out;

		/* Check against existing mmap mappings. */
		if (find_vma_intersection(mm, oldbrk, newbrk+PAGE_SIZE))
			goto out;

		/* Check if we have enough memory.. */
		if (!vm_enough_memory((newbrk-oldbrk) >> PAGE_SHIFT))
			goto out;

		/* Ok, looks good - let it rip. */
		if (do_brk(oldbrk, newbrk-oldbrk) != oldbrk)
			goto out;
	set_brk:
		mm->brk = brk;
	out:
		retval = mm->brk;
		up(&mm->mmap_sem);
		return retval;
	}

		sys_brk-->do_munmap()

		int do_munmap(struct mm_struct *mm, unsigned long addr, size_t len)
		{
			struct vm_area_struct *mpnt, *prev, **npp, *free, *extra;

			if ((addr & ~PAGE_MASK) || addr > TASK_SIZE || len > TASK_SIZE-addr)
				return -EINVAL;

			if ((len = PAGE_ALIGN(len)) == 0)
				return -EINVAL;

			/* 
			 * 通过返回值返回找到的区间
			 * 通过&prev返回找到的“上一个”
			 */
			mpnt = find_vma_prev(mm, addr, &prev);
			if (!mpnt)
				return 0;
			/* 
			 * 如果找到的地址空间，起始地址比现在需要的终点位置
			 * 还要高的话，说明本次的空间原本就没有映射
			 */
			if (mpnt->vm_start >= addr+len)
				return 0;

			/*
			 * 如果释放的空间恰好在找到空间的中部
			 * 就会制造出一个内部的空洞，原有的空间被分割
			 * 此时检查申请是否超过最大的数量限制？
			 * 因为一个进程拥有的虚拟空间的个数是受限的
			 */
			if ((mpnt->vm_start < addr && mpnt->vm_end > addr+len)
			    && mm->map_count >= MAX_MAP_COUNT)
				return -ENOMEM;

			/*
			 * 需要额外的vma来完成
			 */
			extra = kmem_cache_alloc(vm_area_cachep, SLAB_KERNEL);
			if (!extra)
				return -ENOMEM;

			npp = (prev ? &prev->vm_next : &mm->mmap);
			free = NULL;

			/*
			 * 通过一个循环将涉及到的空间
			 * 从队列中转移到free队列中，在avl树上的要摘除
			 */
			spin_lock(&mm->page_table_lock);
			for ( ; mpnt && mpnt->vm_start < addr+len; mpnt = *npp) {
				*npp = mpnt->vm_next;
				mpnt->vm_next = free;
				free = mpnt;
				if (mm->mmap_avl)
					avl_remove(mpnt, &mm->mmap_avl);
			}
			mm->mmap_cache = NULL;	/* Kill the cache. */
			spin_unlock(&mm->page_table_lock);

			/* Ok - we have the memory areas we should free on the 'free' list,
			 * so release them, and unmap the page range..
			 * If the one of the segments is only being partially unmapped,
			 * it will put new vm_area_struct(s) into the address space.
			 * In that case we have to be careful with VM_DENYWRITE.
			 */
			while ((mpnt = free) != NULL) {
				unsigned long st, end, size;
				struct file *file = NULL;

				free = free->vm_next;

				st = addr < mpnt->vm_start ? mpnt->vm_start : addr;
				end = addr+len;
				end = end > mpnt->vm_end ? mpnt->vm_end : end;
				size = end - st;

				if (mpnt->vm_flags & VM_DENYWRITE &&
				    (st != mpnt->vm_start || end != mpnt->vm_end) &&
				    (file = mpnt->vm_file) != NULL) {
					atomic_dec(&file->f_dentry->d_inode->i_writecount);
				}
				remove_shared_vm_struct(mpnt);
				mm->map_count--;

				flush_cache_range(mm, st, end);
				zap_page_range(mm, st, size);
				flush_tlb_range(mm, st, end);

				/*
				 * 由于已经对虚拟内存区间的进行了操作
				 * 所以需要对涉及到的数据结构进行调整
				 */
				extra = unmap_fixup(mm, mpnt, st, size, extra);
				if (file)
					atomic_inc(&file->f_dentry->d_inode->i_writecount);
			}

			/* Release the extra vma struct if it wasn't used */
			if (extra)
				kmem_cache_free(vm_area_cachep, extra);

			free_pgtables(mm, prev, addr, addr+len);

			return 0;
		}

			sys_brk-->do_munmap-->find_vma_prev()
			/*
			 * 找到第一个结束地址大于输入地址的位置
			 */
			struct vm_area_struct * find_vma_prev(struct mm_struct * mm, unsigned long addr,
				      struct vm_area_struct **pprev)
			{
				if (mm) {
					if (!mm->mmap_avl) {
						/* Go through the linear list. */
						struct vm_area_struct * prev = NULL;
						struct vm_area_struct * vma = mm->mmap;
						while (vma && vma->vm_end <= addr) {
							prev = vma;
							vma = vma->vm_next;
						}
						*pprev = prev;
						return vma;
					} else {
						/* Go through the AVL tree quickly. */
						struct vm_area_struct * vma = NULL;
						struct vm_area_struct * last_turn_right = NULL;
						struct vm_area_struct * prev = NULL;
						struct vm_area_struct * tree = mm->mmap_avl;
						for (;;) {
							if (tree == vm_avl_empty)
								break;
							if (tree->vm_end > addr) {
								vma = tree;
								prev = last_turn_right;
								if (tree->vm_start <= addr)
									break;
								tree = tree->vm_avl_left;
							} else {
								last_turn_right = tree;
								tree = tree->vm_avl_right;
							}
						}
						if (vma) {
							if (vma->vm_avl_left != vm_avl_empty) {
								prev = vma->vm_avl_left;
								while (prev->vm_avl_right != vm_avl_empty)
									prev = prev->vm_avl_right;
							}
							if ((prev ? prev->vm_next : mm->mmap) != vma)
								printk("find_vma_prev: tree inconsistent with list\n");
							*pprev = prev;
							return vma;
						}
					}
				}
				*pprev = NULL;
				return NULL;
			}

			sys_brk-->do_munmap-->find_vma_prev-->zap_page_range()

			/*
			 * 空间收缩以后，缩减的空间结构被移动到free队列上。
			 * 对于free list上的每个空间，此调用真正解除映射
			 */
			void zap_page_range(struct mm_struct *mm, unsigned long address, unsigned long size)
			{
				pgd_t * dir;
				unsigned long end = address + size;
				int freed = 0;

				dir = pgd_offset(mm, address);

				/*
				 * This is a long-lived spinlock. That's fine.
				 * There's no contention, because the page table
				 * lock only protects against kswapd anyway, and
				 * even if kswapd happened to be looking at this
				 * process we _want_ it to get stuck.
				 */
				if (address >= end)
					BUG();
				spin_lock(&mm->page_table_lock);
				do {
					freed += zap_pmd_range(mm, dir, address, end - address);
					address = (address + PGDIR_SIZE) & PGDIR_MASK;
					dir++;
				} while (address && (address < end));
				spin_unlock(&mm->page_table_lock);
				/*
				 * Update rss for the mm_struct (not necessarily current->mm)
				 * Notice that rss is an unsigned long.
				 */
				if (mm->rss > freed)
					mm->rss -= freed;
				else
					mm->rss = 0;
			}

				sys_brk-->do_munmap-->find_vma_prev-->zap_page_range-->zap_pmd_range()

				static inline int zap_pmd_range(struct mm_struct *mm, pgd_t * dir, unsigned long address, unsigned long size)
				{
					pmd_t * pmd;
					unsigned long end;
					int freed;

					if (pgd_none(*dir))
						return 0;
					if (pgd_bad(*dir)) {
						pgd_ERROR(*dir);
						pgd_clear(dir);
						return 0;
					}
					/* 如果使用两级页表，就直接转换 */
					pmd = pmd_offset(dir, address);

					address &= ~PGDIR_MASK;
					end = address + size;
					if (end > PGDIR_SIZE)
						end = PGDIR_SIZE;
					freed = 0;
					do {
						freed += zap_pte_range(mm, pmd, address, end - address);
						address = (address + PMD_SIZE) & PMD_MASK; 
						pmd++;
					} while (address < end);
					return freed;
				}

					sys_brk-->do_munmap-->find_vma_prev-->zap_page_range-->zap_pmd_range-->zap_pte_range()

					static inline int zap_pte_range(struct mm_struct *mm, pmd_t * pmd, unsigned long address, unsigned long size)
					{
						pte_t * pte;
						int freed;

						if (pmd_none(*pmd))
							return 0;
						if (pmd_bad(*pmd)) {
							pmd_ERROR(*pmd);
							pmd_clear(pmd);
							return 0;
						}
						pte = pte_offset(pmd, address);
						address &= ~PMD_MASK;
						if (address + size > PMD_SIZE)
							size = PMD_SIZE - address;
						size >>= PAGE_SHIFT;
						freed = 0;
						for (;;) {
							pte_t page;
							if (!size)
								break;
							page = ptep_get_and_clear(pte);
							pte++;
							size--;
							if (pte_none(page))
								continue;

							/* 到这里开始，释放页表 */
							freed += free_pte(page);
						}
						return freed;
					}

						sys_brk-->do_munmap-->find_vma_prev-->zap_page_range-->zap_pmd_range-->zap_pte_range-->free_pte()

						static inline int free_pte(pte_t pte)
						{
							if (pte_present(pte)) {
								struct page *page = pte_page(pte);
								if ((!VALID_PAGE(page)) || PageReserved(page))
									return 0;
								/* 
								 * free_page() used to be able to clear swap cache
								 * entries.  We may now have to do it manually.  
								 */
								if (pte_dirty(pte) && page->mapping)
									set_page_dirty(page);
								free_page_and_swap_cache(page);
								return 1;
							}
							swap_free(pte_to_swp_entry(pte));
							return 0;
						}

							sys_brk-->do_munmap-->find_vma_prev-->zap_page_range-->zap_pmd_range-->zap_pte_range-->free_pte
							-->free_page_and_swap_cache()

							void free_page_and_swap_cache(struct page *page)
							{
								/* 
								 * If we are the only user, then try to free up the swap cache. 
								 */
								if (PageSwapCache(page) && !TryLockPage(page)) {
									if (!is_page_shared(page)) {
										delete_from_swap_cache_nolock(page);
									}
									UnlockPage(page);
								}
								page_cache_release(page);
							}
	

							sys_brk-->do_munmap-->find_vma_prev-->zap_page_range-->zap_pmd_range-->zap_pte_range-->free_pte
							-->free_page_and_swap_cache-->delete_from_swap_cache_nolock()

							/*
							 * 一个有用户空间映射、可换出的内存页面，同时在三个队列中
							 * 1.队列头struct list_head list链入的某个换入、换出队列中
							     相应的address_space结构中的，clean_pages、dirty_pages、locked_pages；
							 * 2.struct list_head lru队列，active_list、inactive_list、inactive_clean_list；
							 * 3.struct page *next_hash，链入的hash队列；
							 */

							void delete_from_swap_cache_nolock(struct page *page)
							{
								if (!PageLocked(page))
									BUG();

								if (block_flushpage(page, 0))
									lru_cache_del(page);

								spin_lock(&pagecache_lock);
								ClearPageDirty(page);
								__delete_from_swap_cache(page);
								spin_unlock(&pagecache_lock);
								page_cache_release(page);
							}
/*************************************************************************************************************************************/

	/*
	 * 当完成空间收缩部分的工作以后
	 * 需要对涉及到的虚拟空间数据结构做出一定的调整
	 * 回到do_unmmap以后，通过调用
	 * 
	 */

	sys_brk-->do_munmap-->unmap_fixup()

	static struct vm_area_struct * unmap_fixup(struct mm_struct *mm, 
											   struct vm_area_struct *area, unsigned long addr, size_t len, 
											   struct vm_area_struct *extra)
	{
		struct vm_area_struct *mpnt;
		unsigned long end = addr + len;

		area->vm_mm->total_vm -= len >> PAGE_SHIFT;
		if (area->vm_flags & VM_LOCKED)
			area->vm_mm->locked_vm -= len >> PAGE_SHIFT;

		/* Unmapping the whole area. */
		if (addr == area->vm_start && end == area->vm_end) {
			if (area->vm_ops && area->vm_ops->close)
				area->vm_ops->close(area);
			if (area->vm_file)
				fput(area->vm_file);
			kmem_cache_free(vm_area_cachep, area);
			return extra;
		}

		/* Work out to one of the ends. */
		if (end == area->vm_end) {
			area->vm_end = addr;
			lock_vma_mappings(area);
			spin_lock(&mm->page_table_lock);
		} else if (addr == area->vm_start) {
			area->vm_pgoff += (end - area->vm_start) >> PAGE_SHIFT;
			area->vm_start = end;
			lock_vma_mappings(area);
			spin_lock(&mm->page_table_lock);
		} else {
		/* Unmapping a hole: area->vm_start < addr <= end < area->vm_end */
			/* Add end mapping -- leave beginning for below */
			mpnt = extra;
			extra = NULL;

			mpnt->vm_mm = area->vm_mm;
			mpnt->vm_start = end;
			mpnt->vm_end = area->vm_end;
			mpnt->vm_page_prot = area->vm_page_prot;
			mpnt->vm_flags = area->vm_flags;
			mpnt->vm_raend = 0;
			mpnt->vm_ops = area->vm_ops;
			mpnt->vm_pgoff = area->vm_pgoff + ((end - area->vm_start) >> PAGE_SHIFT);
			mpnt->vm_file = area->vm_file;
			mpnt->vm_private_data = area->vm_private_data;
			if (mpnt->vm_file)
				get_file(mpnt->vm_file);
			if (mpnt->vm_ops && mpnt->vm_ops->open)
				mpnt->vm_ops->open(mpnt);
			area->vm_end = addr;	/* Truncate area */

			/* Because mpnt->vm_file == area->vm_file this locks
			 * things correctly.
			 */
			lock_vma_mappings(area);
			spin_lock(&mm->page_table_lock);
			__insert_vm_struct(mm, mpnt);
		}

		__insert_vm_struct(mm, area);
		spin_unlock(&mm->page_table_lock);
		unlock_vma_mappings(area);
		return extra;
	}