/*
 * 对外部设备的访问
 */

#1 内存映射式：外部设备的各类寄存器作为内存的一部分出现，CPU像访问控制寄存器、内存单元一样

#2 IO映射式：  需要使用特殊的CPU访问指令，输入输出要对应有IN或者OUT

#3 内核通过 ioremap() 方式将外部设备映射在内存中，通过虚拟内存方式访问


/*
 * 1.对外设访问是一个反向的过程，因为外设通过PIC总线链入CPU；
 * 2.也就是说CPU已经有了当前设备物理地址，对于设备芯片来说是0地址，对于CPU
     来说是PCI总线的物理地址（设备插槽的位置）；
 * 3.ioremap为一个反向的过程，相当于为当前物理地址建立虚拟地址空间
 */

ioremap.c-->ioremap()

	extern inline void * ioremap (unsigned long offset, unsigned long size)
	{
		return __ioremap(offset, size, 0);
	}

	ioremap-->__ioremap()

	void * __ioremap(unsigned long phys_addr, unsigned long size, unsigned long flags)
	{
		void * addr;
		struct vm_struct * area;
		unsigned long offset, last_addr;

		/* Don't allow wraparound or zero size */
		last_addr = phys_addr + size - 1;
		if (!size || last_addr < phys_addr)
			return NULL;

		/*
		 * 0xA0000至0x100000属于BIOS和VGA卡
		 */
		if (phys_addr >= 0xA0000 && last_addr < 0x100000)
			return phys_to_virt(phys_addr);

		/*
		 * 不允许使用高端内存区
		 */
		if (phys_addr < virt_to_phys(high_memory)) {
			char *t_addr, *t_end;
			struct page *page;

			t_addr = __va(phys_addr);
			t_end = t_addr + (size - 1);
		   
			for(page = virt_to_page(t_addr); page <= virt_to_page(t_end); page++)
				if(!PageReserved(page))
					return NULL;
		}

		/*
		 * 页面边界对齐
		 */
		offset = phys_addr & ~PAGE_MASK;
		phys_addr &= PAGE_MASK;
		size = PAGE_ALIGN(last_addr) - phys_addr;

		/*
		 * 申请虚拟内存，flag: VM_IOREMAP
		 */
		area = get_vm_area(size, VM_IOREMAP);
		if (!area)
			return NULL;

		/*
		 * 到这里，开始remap
		 */
		addr = area->addr;
		if (remap_area_pages(VMALLOC_VMADDR(addr), phys_addr, size, flags)) {
			vfree(addr);
			return NULL;
		}
		return (void *) (offset + (char *)addr);
	}

		ioremap-->__ioremap-->get_vm_area()

		struct vm_struct * get_vm_area(unsigned long size, unsigned long flags)
		{
			unsigned long addr;
			struct vm_struct **p, *tmp, *area;

			area = (struct vm_struct *) kmalloc(sizeof(*area), GFP_KERNEL);
			if (!area)
				return NULL;
			size += PAGE_SIZE;
			addr = VMALLOC_START;

			/*
			 * 使用的vmlist、vm_struct均为内核自己维持的专用队列
			 * 所以不是通过mm_struct访问
			 * 	#define VMALLOC_OFFSET	(8*1024*1024)
				#define VMALLOC_START	(((unsigned long) high_memory + 2*VMALLOC_OFFSET-1) & \
										~(VMALLOC_OFFSET-1))
				#define VMALLOC_VMADDR(x) ((unsigned long)(x))
				#define VMALLOC_END	(FIXADDR_START)

			 	struct vm_struct {
					unsigned long flags;
					void * addr;
					unsigned long size;
					struct vm_struct * next;
				};
			 */
			write_lock(&vmlist_lock);
			for (p = &vmlist; (tmp = *p) ; p = &tmp->next) {
				if ((size + addr) < addr) {
					write_unlock(&vmlist_lock);
					kfree(area);
					return NULL;
				}
				/*
				 * 大小+起始位置得到的地址小于下一个区域的起始位置
				 * 就插入这个位置，作为返回的虚拟内存区域
				 */
				if (size + addr < (unsigned long) tmp->addr)
					break;
				addr = tmp->size + (unsigned long) tmp->addr;
				if (addr > VMALLOC_END-size) {
					write_unlock(&vmlist_lock);
					kfree(area);
					return NULL;
				}
			}
			area->flags = flags;
			area->addr = (void *)addr;
			area->size = size;
			area->next = *p;
			*p = area;
			write_unlock(&vmlist_lock);
			return area;
		}

		ioremap-->__ioremap-->remap_area_pages()

		static int remap_area_pages(unsigned long address, unsigned long phys_addr,
				 unsigned long size, unsigned long flags)
		{
			pgd_t * dir;
			unsigned long end = address + size;

			/*
			 * 由于address是在循环中变化的
			 * 第一步先去掉address，调用中使用(phys_addr + address)
			 */
			phys_addr -= address;
			dir = pgd_offset(&init_mm, address);
			flush_cache_all();
			if (address >= end)
				BUG();
			do {
				pmd_t *pmd;
				pmd = pmd_alloc_kernel(dir, address);
				if (!pmd)
					return -ENOMEM;
				if (remap_area_pmd(pmd, address, end - address,
							 phys_addr + address, flags))
					return -ENOMEM;
				address = (address + PGDIR_SIZE) & PGDIR_MASK;
				dir++;
			} while (address && (address < end));
			flush_tlb_all();
			return 0;
		}

			ioremap-->__ioremap-->remap_area_pages-->remap_area_pmd-->remap_area_pte()

			static inline void remap_area_pte(pte_t * pte, unsigned long address, unsigned long size,
				unsigned long phys_addr, unsigned long flags)
			{
				unsigned long end;

				address &= ~PMD_MASK;
				end = address + size;
				if (end > PMD_SIZE)
					end = PMD_SIZE;
				if (address >= end)
					BUG();
				do {
					if (!pte_none(*pte)) {
						printk("remap_area_pte: page already exists\n");
						BUG();
					}
					set_pte(pte, mk_pte_phys(phys_addr, __pgprot(_PAGE_PRESENT | _PAGE_RW | 
								_PAGE_DIRTY | _PAGE_ACCESSED | flags)));
					address += PAGE_SIZE;
					phys_addr += PAGE_SIZE;
					pte++;
				} while (address && (address < end));
			}


