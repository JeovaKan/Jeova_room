 /*
 * 页面的换入
 */

   CPU在映射过程中首先查看页面表项或者目录项中的P标志

   只要P标志为0，其他位段的数值就没有意义了

   至于当一个页面不在内存中，利用页面表项指向一个盘上页面。是完全由软件实现的

   访问失败的原因到底是没有建立映射还是页面不在内存中，也是软件的实现

   mm/memory.c

   -->do_swap_page()
   								/*
   								 * mm：指向进程的结构的mm_struct结构
   								 * vma：指向所属虚拟内存结构
   								 * address： 映射失败的线性地址
   								 * page_table：指向映射失败的页面表项
   								 * entry：该表项的内容
   								 * write_access：读写权限
   							     */
	    static int do_swap_page(struct mm_struct * mm, struct vm_area_struct * vma, unsigned long address,
			                    pte_t * page_table, swp_entry_t entry, int write_access)
		{
			/*
			 * entry第一部分是页面交换设备的序号，第二部分是页面在这个设备的位移
			 * 首先看看这个页面是否还在swap_cache换入/换出队列中还没有被释放
			 */
			struct page *page = lookup_swap_cache(entry);
			pte_t pte;

			if (!page) {
				lock_kernel();
				/*
				 * 如果没有在队列中找到需要的页面，说明内容不在内存中
				 * 需要从盘上读取对应的页面，在读入之前，首先调用swapin_readahead()
				 * read ahead即为预读，因为磁盘上读取文件的时间大部分都用在第一次磁盘寻道上
				 * 所以对于磁盘的读取，都是按照页面集群（cluster）为单位进行的。
				 * 预读进来的页面都暂时链入swap_cache队列中。如果实际上不需要就由守护者
				 * kswapd、kreclaimd进程回收
				 */
				swapin_readahead(entry);
				/*
				 * 如果前一步操作的内存分配失败
				 * 那么就真的按照一个页面来读取
				 */
				page = read_swap_cache(entry);
				unlock_kernel();
				if (!page)
					return -1;

				flush_page_to_ram(page);
				flush_icache_page(vma, page);
			}

			mm->rss++;

			pte = mk_pte(page, vma->vm_page_prot);

			/*
			 * Freeze the "shared"ness of the page, ie page_count + swap_count.
			 * Must lock page before transferring our swap count to already
			 * obtained page count.
			 */
			lock_page(page);
			swap_free(entry);
			if (write_access && !is_page_shared(page))
				pte = pte_mkwrite(pte_mkdirty(pte));
			UnlockPage(page);

			set_pte(page_table, pte);
			/* No need to invalidate - it was non-present before */
			update_mmu_cache(vma, address, pte);
			return 1;	/* Minor fault */
		}   		

			do_swap_page-->lookup_swap_cache()

			struct page * lookup_swap_cache(swp_entry_t entry)
			{
				struct page *found;

			#ifdef SWAP_CACHE_INFO
				swap_cache_find_total++;
			#endif
				while (1) {
					/*
					 * Right now the pagecache is 32-bit only.  But it's a 32 bit index. =)
					 */
			repeat:
					/*
					 * 根据入参的swapper_space，以及偏移值在hash表中查找对应的表项
					 */
					found = find_lock_page(&swapper_space, entry.val);
					if (!found)
						return 0;
					/*
					 * 找到了页面以后，也可能这个页面是早些时期在swap_cache中，
					 * 页面也可能已经被refill_inactive移出队列。
					 * 如果发生了上述情况，那么就再次寻找一次。
					 */
					if (!PageSwapCache(found)) {
						UnlockPage(found);
						page_cache_release(found);
						goto repeat;
					}
					if (found->mapping != &swapper_space)
						goto out_bad;
			#ifdef SWAP_CACHE_INFO
					swap_cache_find_success++;
			#endif
					UnlockPage(found);
					return found;
				}

			out_bad:
				printk (KERN_ERR "VM: Found a non-swapper swap page!\n");
				UnlockPage(found);
				page_cache_release(found);
				return 0;
			}

			do_swap_page-->lookup_swap_cache-->read_swap_cache(entry)-->read_swap_cache_async(entry,1)

			struct page * read_swap_cache_async(swp_entry_t entry, int wait)
			{
				struct page *found_page = 0, *new_page;
				unsigned long new_page_addr;
				
				/*
				 * Make sure the swap entry is still in use.
				 */
				if (!swap_duplicate(entry))	/* Account for the swap cache */
					goto out;
				/*
				 * Look for the page in the swap cache.
				 */
				found_page = lookup_swap_cache(entry);
				if (found_page)
					goto out_free_swap;

				new_page_addr = __get_free_page(GFP_USER);
				if (!new_page_addr)
					goto out_free_swap;	/* Out of memory */
				new_page = virt_to_page(new_page_addr);

				/*
				 * 如果另一个进程已经把需要的页面读入了
				 * 所以再检查一次是否真的需要读入
				 */
				found_page = lookup_swap_cache(entry);
				if (found_page)
					goto out_free_page;
				/* 
				 * Add it to the swap cache and read its contents.
				 */
				lock_page(new_page);
				add_to_swap_cache(new_page, entry);
				rw_swap_page(READ, new_page, wait);
				return new_page;

			out_free_page:
				page_cache_release(new_page);
			out_free_swap:
				swap_free(entry);
			out:
				return found_page;
			}
