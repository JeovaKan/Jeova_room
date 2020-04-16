/*
 * 物理页面的使用和扩展
 */

   虚拟内存页面：

   		虚拟地址空间中(例如32位机器，可寻址范围4GB = 2^32)

   		一个固定大小、边界与页面大小(4KB)对齐的区间及其内容

   		虚拟页面最终是要通过pgd、pte映射到某种物理介质上。

   		物理页面可以在内存中，也可以在磁盘上


   	数据结构

   		每一个物理页面都有一个page数据结构

   		初始化阶段读取装载的内存信息，形成page结构的数组，全局量mem_map指向它

   		同时将这些页面合成物理地址连续的许多内存页面 块，根据块的大小建立几个管理区ZONE

   		每个管理区中设置一个空闲块队列

   		swap_info_struct数据结构用来描述和管理用于页面交换的文件以及设备

   			struct swap_info_struct {
				unsigned int flags;
				kdev_t swap_device;
				spinlock_t sdev_lock;
				struct dentry * swap_file;
				struct vfsmount *swap_vfsmnt;

				/*
				 * 指向一个数组，数组的每一项代表一个物理页面
				 * 数组的下标决定了页面在盘上或文件中的位置
				 */
				unsigned short * swap_map;

				/*
				 * 说明文件中从什么地方开始到什么地方为止
				 * 是用于页面交换使用的
				 */
				unsigned int lowest_bit;
				unsigned int highest_bit;

				/* 分配在页面空间上是按集群的方式进行的 */
				unsigned int cluster_next;
				unsigned int cluster_nr;
				int prio;			/* swap priority */、
				
				/*
				 * 决定了上述数组的大小，表示该页面交换设备或文件的大小
				 */
				int pages;

				/* 设备的最大页面号，设备或者文件的物理大小 */
				unsigned long max;
				int next;			/* next entry on swap list */
			};


	struct swap_list_struct swap_info[MAX_SWAPFILES];
	
		设立了一个队列swap_list，将各个可以分配物理页面的磁盘设备或者文件的数据结构按照优先级高低链接在一起。

	typedef struct {
		unsigned long val;
	}swp_entry_t;

				offset 24位				type 7位
		—————————————————————————————————————————————
        |							|			|	|
        —————————————————————————————————————————————


    释放一个磁盘页面
    
    -->__swap_free();
		void __swap_free(swp_entry_t entry, unsigned short count)
		{
			struct swap_info_struct * p;
			unsigned long offset, type;

			/*
			 * 第0个页面用于描述信息，不用于文件的页面
			 * 显然页面0时应该出口
			 */
			if (!entry.val)
				goto out;

			/*
			 * 获得数组swap_info的下标
			 * 宏操作提取type位
			 */		
			type = SWP_TYPE(entry);
			if (type >= nr_swapfiles) /* nr_swapfiles 最大127个 */
				goto bad_nofile;
			p = & swap_info[type];

			if (!(p->flags & SWP_USED))
				goto bad_device;
			offset = SWP_OFFSET(entry);
			if (offset >= p->max)
				goto bad_offset;
			if (!p->swap_map[offset])
				goto bad_free;
			swap_list_lock();
			if (p->prio > swap_info[swap_list.next].prio)
				swap_list.next = type;
			swap_device_lock(p);
			if (p->swap_map[offset] < SWAP_MAP_MAX) {
				if (p->swap_map[offset] < count)
					goto bad_count;
				if (!(p->swap_map[offset] -= count)) {
					if (offset < p->lowest_bit)
						p->lowest_bit = offset;
					if (offset > p->highest_bit)
						p->highest_bit = offset;
					nr_swap_pages++;
				}
			}
			swap_device_unlock(p);
			swap_list_unlock();
		out:
			return;

		bad_nofile:
			printk("swap_free: Trying to free nonexistent swap-page\n");
			goto out;
		bad_device:
			printk("swap_free: Trying to free swap from unused swap-device\n");
			goto out;
		bad_offset:
			printk("swap_free: offset exceeds max\n");
			goto out;
		bad_free:
			printk("VM: Bad swap entry %08lx\n", entry.val);
			goto out;
		bad_count:
			swap_device_unlock(p);
			swap_list_unlock();
			printk(KERN_ERR "VM: Bad count %hd current count %hd\n", count, p->swap_map[offset]);
			goto out;
		}    

	内存页面的周转

		-- 页面的分配、使用和回收，不一定涉及盘区的交换

		-- 盘区交换，交换的目的也是为了页面回收

		-- 只有用户空间的页面才会被换出

	用户页面的种类

		普通的用户空间页面，代码段、数据段、堆栈段，动态分配的存储堆

		通过系统调用mmap()映射到空间的已打开文件的内容

		进程间的共享内存区


	系统空间中的页面

		--内核中全局量占用的内存页面，静态的，既不需要分配也不需要释放

		--一旦使用完便没有保存价值的内存，立即可以释放、回收
			
			kmalloc()或者vmalloc()分配，用于临时使用的数据结构，
			一个页面中往往包含多个同种数据结构，要等到整个页面
			空闲时，将整个页面释放

			alloc_page()分配的内存页面

		--老化队列LRU

			文件系统中用来缓冲存储一些文件目录结构dentry的空间

			文件系统中用来缓冲存储一些inode结构

			文件系统读/写的缓冲区


	物理内存周转的策略

		-- 空闲。 页面的page数据结构通过队列list链入某一个内存管理区ZONE的空闲队列
				  free_area。页面的使用计数count为0.

		-- 分配。 通过函数__alloc_pages()或__get_free_page()从某个空闲队列中分配内存页面
				  使用计数count变为1，page数据结构的list变为空闲

		-- 活跃状态。 page数据结构通过队列头结构lru链入活跃页面队列active_list，并且至少
		              有一个进程的页面表指向该页面，每当页面建立或恢复映射时，count++

		-- 不活跃状态(脏)。 page数据结构通过队列头结构lru链入不活跃脏页面队列
		                    inactive_dirty_list，原则上不再有任何进程的页面表指向该页面
		                    每次断开都使页面的count--
       
        -- 不活跃状态(脏)页面内容写入交换设备，将page数据结构从inactive_dirty_list转移到
           某个不活跃干净页面队列中

     	-- 不活跃状态干净。 page数据结构通过队列头结构lru链入不活跃干净页面队列
		                    inactive_clean_list

		-- 如果转入不活跃状态一段时间以后，页面再次受到访问，则恢复到活跃状态并再次映射

		-- 当有需要时，就从干净页面队列中回收页面，可退回到空闲队列，或直接分配


    当分配一个空闲内存页面以后，通过以下调用将page加入数据结构

    --> add_to_swap_cache

		void add_to_swap_cache(struct page *page, swp_entry_t entry)
		{
			unsigned long flags;

		#ifdef SWAP_CACHE_INFO
			swap_cache_add_total++;
		#endif
			if (!PageLocked(page))
				BUG();
			if (PageTestandSetSwapCache(page))
				BUG();
			if (page->mapping)
				BUG();
			flags = page->flags & ~((1 << PG_error) | (1 << PG_arch_1));
			page->flags = flags | (1 << PG_uptodate);
			add_to_page_cache_locked(page, &swapper_space, entry.val);
		}
	
		--> add_to_page_cache_locked()

			void add_to_page_cache_locked(struct page * page, struct address_space *mapping, unsigned long index)
			{
				if (!PageLocked(page))
					BUG();

				page_cache_get(page);
				spin_lock(&pagecache_lock);
				page->index = index;
				add_page_to_inode_queue(mapping, page);
				add_page_to_hash_queue(page, page_hash(mapping, index));
				lru_cache_add(page);
				spin_unlock(&pagecache_lock);
			}