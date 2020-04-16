/*
 * 缓冲区管理
 */

 如果采用malloc()的方式，从堆空间中分配一块内存出去，仍然需要有改进的地方：

 #1 如果随意分配内存块，随着时间的推移，碎片化现象就越严重。

 	--分配时以2^n为基准。 

 #2 分配的数据结构往往都需要初始化，如果释放的数据结构可以在下一次分配时“重用”，就可以提高分配效率。

 #3 缓冲区的组织和管理方式直接影响了高速缓冲的命中率。

 #4 不适合多处理器共用内存的情况

/*
 * slab内存管理
 */

 slab方法中，包含数据结构的对象的构造和拆除调用

 #1 一个slab可能由1个、2个...最多32个连续的物理页面构成，slab的具体大小和所容纳的对象有关
    初始化时候通过计算获得最合适的大小。

 #2 每个slab的前端是该slab的描述结构的slab_t，同一个对象的多个slab通过描述结构中的队列头形成
    一条双向链队列。每个slab双向链队列在逻辑上分为3部分。第一部分是各个slab上所有对象都已经分配
    使用；第二部分是各个slab上的对象已经部分使用；第三部分是各个slab上的全部对象都是空闲的。

 #3 每个slab都有一个对象区，是对象数据结构的数组，以对象序号作为下标就可以得到具体对象的地址。

 #4 每个slab有对象链接数组，用于实现一个空闲对象链。

 #5 每个slab描述结构中都有一个字段，表明该slab上的第一个空闲对象，该字段与对象链接数组组成了空闲对象链

 #6 slab描述结构中有一个已经分配使用的对象的计数器，当一个空闲的对象分配使用时，就将该计数器值+1

 #7 当释放一个对象时，需要调整链接数组中的相应元素以及slab描述结构中的计数器，最后根据该slab的使用情况调整其在队列中的位置

 #8 着色区，为缓冲区对齐作用。其大小使得slab每个对象的起始地址都按照高速缓存中的缓冲行大小对齐
    80386一级cache缓冲行大小为16字节，pentium为32字节。每一个slab都是从页面边界开始，自然按照高速缓存
    的缓存行对齐，着色区的作用，是将对象的起始地址，推到另一个高速缓存行对齐的边界。

 #9 slab最后一个对象的之后也有一个废料区的没有功能上的使用，是对着色区的补偿
    该区域大小与着色区大小的总和对于同一个对象的各个slab是常数。

 #10 每个对象的大小基本上就是所需数据结构的大小，只有当数据结构大小不与高速缓冲行对齐时，
     才增加若干字节使其对齐。  

/*
 * slab_t
 *
 * Manages the objs in a slab. Placed either at the beginning of mem allocated
 * for a slab, or allocated from an general cache.
 * Slabs are chained into one ordered list: fully used, partial, then fully
 * free slabs.
 */
typedef struct slab_s {
	/* 链入一个slab队列 */
	struct list_head	list;
	/* 着色区的大小 */
	unsigned long		colouroff;
	/* 指向对象区的起始位置 */
	void			*s_mem;		/* including colour offset */
	/* 已分配对象的计数器 */
	unsigned int		inuse;		/* num of objs active in slab */
	/* 指明了空闲对象链中的第一个对象，是一个整数 */
	kmem_bufctl_t		free;
} slab_t;


/*
 * 维护slab队列的数据结构
 * 除了信息以外，还有构造、拆除结构的函数指针 
 */
typedef kmem_cache_s kmem_cache_t;

struct kmem_cache_s {
/* 1) each alloc & free */
	/* full, partial first, then free */
	/*
	 * slabs用来维持一个队列
	 */
	struct list_head	slabs;
	/*
	 * 指向队列中第一个含有空闲对象的slab，即指向队列的第二段
	 * 如果指向了队列头slabs时，表明队列中不存在含有空闲对象的slab
	 */
	struct list_head	*firstnotfull;
	/* 单个对象的大小 */
	unsigned int		objsize;
	unsigned int	 	flags;	/* constant flags */
	/* slab上缓冲区的数量 */
	unsigned int		num;	/* # of objs per slab */
	spinlock_t		spinlock;
#ifdef CONFIG_SMP
	unsigned int		batchcount;
#endif

/* 2) slab additions /removals */
	/* order of pgs per slab (2^n) */
	/* 每个slab的大小 */
	unsigned int		gfporder;

	/* force GFP flags, e.g. GFP_DMA */
	unsigned int		gfpflags;

	/* 当前着色的序号 */
	size_t			 colour;		/* cache colouring range */
	unsigned int		colour_off;	/* colour offset */
	unsigned int		colour_next;	/* cache colouring */
	kmem_cache_t		*slabp_cache;
	unsigned int		growing;
	unsigned int		dflags;		/* dynamic flags */

	/* constructor func */
	void (*ctor)(void *, kmem_cache_t *, unsigned long);

	/* de-constructor func */
	void (*dtor)(void *, kmem_cache_t *, unsigned long);

	unsigned long		failures;

/* 3) cache creation/removal */
	char			name[CACHE_NAMELEN];
	/*
	 * slab缓冲区队列的队列
	 */
	struct list_head	next;
#ifdef CONFIG_SMP
/* 4) per-cpu data */
	cpucache_t		*cpudata[NR_CPUS];
#endif
#if STATS
	unsigned long		num_active;
	unsigned long		num_allocations;
	unsigned long		high_mark;
	unsigned long		grown;
	unsigned long		reaped;
	unsigned long 		errors;
#ifdef CONFIG_SMP
	atomic_t		allochit;
	atomic_t		allocmiss;
	atomic_t		freehit;
	atomic_t		freemiss;
#endif
#endif
};

/*
 * slab数据结构的树形结构
 */

#1  总根cache_cache是一个kmem_cache_t结构，用来维持第一层slab队列，这些slab上的对象都是kmem_cache_t结构；

#2  每一个第一层slab上的每个对象，对象kmem_cache_t结构都是队列头，用来维持第二层的slab队列；

#3  第二层slab队列上基本都是某种对象，即为数据结构；

#4  每个第二层slab上维持着一个空闲对象队列。

/*
 * 通过上述的slab描述的树形结构，当需要申请一个新的某种类型的数据结构时。
 * 只需要从总队列上指定是哪一种数据结构的队列，不需要说明缓冲区的大小、不需要初始化。
 */

api:
	void * kmem_cache_alloc (kmem_cache_t *cachep, int flags);

	void kmem_cache_free (kmem_cache_t *cachep, void *objp);

/*
 * 1.对于数据结构较大的情况，将采用统一管理的方式，不再放到具体的slab队列中；
 * 2.并非所有的使用的数据结构，都需要专用的缓冲队列。不太常用的、初始化开销不大的数据，
 *   可以合用通用的缓冲池。
 */

对于2这种情况，api:

/*
 * slab的顶层不是队列，而是个数组，数组的每一项指向一个不同的slab队列
 * 队列的不同之处在于所载对象的大小：32、64、128...直到128K（32个页面）
 */
	void * kmalloc (size_t size, int flags);

	void vfree(void * addr);
/*********************************************************************************************/

/*
 * 创建一个新的slab缓冲区 
 */

api:
/*
 * 1.name : 缓冲区使用的字符串名；
 * 2.size ：单个对象的大小；
 * 3.offset：页面内的偏移？
 * 4.flags：slab标志；
 * 5.ctor：对象构建函数指针；
 * 6.dtor：对象拆除函数指针；
 */
kmem_cache_t *
kmem_cache_create (const char *name, size_t size, size_t offset,
	unsigned long flags, void (*ctor)(void*, kmem_cache_t *, unsigned long),
	void (*dtor)(void*, kmem_cache_t *, unsigned long))
/*********************************************************************************************/

/*
 * 缓冲区的分配和释放
 */

 	slab.c-->kmem_cache_alloc()

	void * kmem_cache_alloc (kmem_cache_t *cachep, int flags)
	{
		return __kmem_cache_alloc(cachep, flags);
	}

		kmem_cache_alloc-->__kmem_cache_alloc()

		static inline void * __kmem_cache_alloc (kmem_cache_t *cachep, int flags)
		{
			unsigned long save_flags;
			void* objp;

			/* 调试函数，实际运行时为空 */
			kmem_cache_alloc_head(cachep, flags);
		try_again:
			local_irq_save(save_flags);
		#ifdef CONFIG_SMP
			{
				cpucache_t *cc = cc_data(cachep);

				if (cc) {
					if (cc->avail) {
						STATS_INC_ALLOCHIT(cachep);
						objp = cc_entry(cc)[--cc->avail];
					} else {
						STATS_INC_ALLOCMISS(cachep);
						objp = kmem_cache_alloc_batch(cachep,flags);
						if (!objp)
							goto alloc_new_slab_nolock;
					}
				} else {
					spin_lock(&cachep->spinlock);
					objp = kmem_cache_alloc_one(cachep);
					spin_unlock(&cachep->spinlock);
				}
			}
		#else
			/*
			 * 不考虑SMP对称多处理器结构时，
			 * 使用如下调用
			 */
			objp = kmem_cache_alloc_one(cachep);
		#endif
			local_irq_restore(save_flags);
			return objp;
		alloc_new_slab:
		#ifdef CONFIG_SMP
			spin_unlock(&cachep->spinlock);
		alloc_new_slab_nolock:
		#endif
			local_irq_restore(save_flags);
			if (kmem_cache_grow(cachep, flags))
				/* Someone may have stolen our objs.  Doesn't matter, we'll
				 * just come back here again.
				 */
				goto try_again;
			return NULL;
		}

			kmem_cache_alloc-->__kmem_cache_alloc-->kmem_cache_alloc_one(cachep)

			/*
			 * 首先确定第一个有空闲的队列
			 * 如果已经指向满的队列了，说明缓冲区的大小不够了
			 * 转向alloc_new_skab,否则将当前对象加到队列的末尾
			 */
			#define kmem_cache_alloc_one(cachep)				\
			({								\
				slab_t	*slabp;					\
											\
				/* Get slab alloc is to come from. */			\
				{							\
					struct list_head* p = cachep->firstnotfull;	\
					if (p == &cachep->slabs)			\
						goto alloc_new_slab;			\
					slabp = list_entry(p,slab_t, list);	\
				}							\
				kmem_cache_alloc_one_tail(cachep, slabp);		\
			})

				kmem_cache_alloc-->__kmem_cache_alloc-->kmem_cache_alloc_one-->kmem_cache_alloc_one_tail()

				static inline void * kmem_cache_alloc_one_tail (kmem_cache_t *cachep,
								 slab_t *slabp)
				{
					void *objp;

					STATS_INC_ALLOCED(cachep);
					STATS_INC_ACTIVE(cachep);
					STATS_SET_HIGH(cachep);

					/* get obj pointer */
					slabp->inuse++;
					objp = slabp->s_mem + slabp->free*cachep->objsize;
					slabp->free=slab_bufctl(slabp)[slabp->free];

					/* 如果到达了slab的末尾BUFCTL_END，调整指向下一个slab的空闲 */
					if (slabp->free == BUFCTL_END)
						/* slab now full: move to next slab for next alloc */
						cachep->firstnotfull = slabp->list.next;
				#if DEBUG
					if (cachep->flags & SLAB_POISON)
						if (kmem_check_poison_obj(cachep, objp))
							BUG();
					if (cachep->flags & SLAB_RED_ZONE) {
						/* Set alloc red-zone, and check old one. */
						if (xchg((unsigned long *)objp, RED_MAGIC2) !=
											 RED_MAGIC1)
							BUG();
						if (xchg((unsigned long *)(objp+cachep->objsize -
							  BYTES_PER_WORD), RED_MAGIC2) != RED_MAGIC1)
							BUG();
						objp += BYTES_PER_WORD;
					}
				#endif
					return objp;
				}

				/*
				 * kmem_bufctl_t数组位于slab中数据结构slab_t的上方
				 * 数组以当前对象的序号为下标，数组元素的值表明下一个空闲对象的序号
				 */
				#define slab_bufctl(slabp) \
					((kmem_bufctl_t *)(((slab_t*)slabp)+1))



	kmem_cache_alloc-->kmem_cache_grow()

	/*
	 * 如果slab队列中已经不再有空闲对象的slab，调用kmem_cache_grow
	 * 让slab生长一段，跳至标号alloc_new_slab
	 */
	static int kmem_cache_grow (kmem_cache_t * cachep, int flags)
	{
		slab_t	*slabp;
		struct page	*page;
		void		*objp;
		size_t		 offset;
		unsigned int	 i, local_flags;
		unsigned long	 ctor_flags;
		unsigned long	 save_flags;

		/* Be lazy and only check for valid flags here,
	 	 * keeping it out of the critical path in kmem_cache_alloc().
		 */
		if (flags & ~(SLAB_DMA|SLAB_LEVEL_MASK|SLAB_NO_GROW))
			BUG();
		if (flags & SLAB_NO_GROW)
			return 0;

		/*
		 * The test for missing atomic flag is performed here, rather than
		 * the more obvious place, simply to reduce the critical path length
		 * in kmem_cache_alloc(). If a caller is seriously mis-behaving they
		 * will eventually be caught here (where it matters).
		 */
		if (in_interrupt() && (flags & SLAB_LEVEL_MASK) != SLAB_ATOMIC)
			BUG();

		ctor_flags = SLAB_CTOR_CONSTRUCTOR;
		local_flags = (flags & SLAB_LEVEL_MASK);
		if (local_flags == SLAB_ATOMIC)
			/*
			 * Not allowed to sleep.  Need to tell a constructor about
			 * this - it might need to know...
			 */
			ctor_flags |= SLAB_CTOR_ATOMIC;

		/* About to mess with non-constant members - lock. */
		spin_lock_irqsave(&cachep->spinlock, save_flags);

		/*
		 * 获取当前着色区的值
		 * 如果超过色号，就再从0开始
		 * 获得着色偏移量 
		 */
		offset = cachep->colour_next;
		cachep->colour_next++;
		if (cachep->colour_next >= cachep->colour)
			cachep->colour_next = 0;
		offset *= cachep->colour_off;
		cachep->dflags |= DFLGS_GROWN;

		cachep->growing++;
		spin_unlock_irqrestore(&cachep->spinlock, save_flags);

		/* A series of memory allocations for a new slab.
		 * Neither the cache-chain semaphore, or cache-lock, are
		 * held, but the incrementing c_growing prevents this
		 * cache from being reaped or shrunk.
		 * Note: The cache could be selected in for reaping in
		 * kmem_cache_reap(), but when the final test is made the
		 * growing value will be seen.
		 */

		/* 根据当前slab对象的大小，分配物理页面 */
		if (!(objp = kmem_getpages(cachep, flags)))
			goto failed;

		/* slab扩展以后，重新管理 */
		if (!(slabp = kmem_cache_slabmgmt(cachep, objp, offset, local_flags)))
			goto opps1;

		/* slab页面加入队列，相关标志设置 */
		i = 1 << cachep->gfporder;
		page = virt_to_page(objp);
		do {
			SET_PAGE_CACHE(page, cachep);
			SET_PAGE_SLAB(page, slabp);
			PageSetSlab(page);
			page++;
		} while (--i);

		/*
		 * 初始化slab
		 */
		kmem_cache_init_objs(cachep, slabp, ctor_flags);

		spin_lock_irqsave(&cachep->spinlock, save_flags);
		cachep->growing--;

		/* Make slab active. */
		list_add_tail(&slabp->list,&cachep->slabs);
		if (cachep->firstnotfull == &cachep->slabs)
			cachep->firstnotfull = &slabp->list;
		STATS_INC_GROWN(cachep);
		cachep->failures = 0;

		spin_unlock_irqrestore(&cachep->spinlock, save_flags);
		return 1;
	opps1:
		kmem_freepages(cachep, objp);
	failed:
		spin_lock_irqsave(&cachep->spinlock, save_flags);
		cachep->growing--;
		spin_unlock_irqrestore(&cachep->spinlock, save_flags);
		return 0;
	}


		kmem_cache_alloc-->kmem_cache_grow-->kmem_cache_slabmgmt()
		/*
		 * 由于slab的生长，重新管理
		 */
		static inline slab_t * kmem_cache_slabmgmt (kmem_cache_t *cachep,
					void *objp, int colour_off, int local_flags)
		{
			slab_t *slabp;
			
			/*
			 * 如果是大对象的slab，申请一个新的slab_t
			 * 如果是小对象的slab，就把它加入当前slab的控制区
			 */
			if (OFF_SLAB(cachep)) {
				/* Slab management obj is off-slab. */
				slabp = kmem_cache_alloc(cachep->slabp_cache, local_flags);
				if (!slabp)
					return NULL;
			} else {
				/* FIXME: change to
					slabp = objp
				 * if you enable OPTIMIZE
				 */
				slabp = objp+colour_off;
				colour_off += L1_CACHE_ALIGN(cachep->num *
						sizeof(kmem_bufctl_t) + sizeof(slab_t));
			}
			slabp->inuse = 0;
			slabp->colouroff = colour_off;
			slabp->s_mem = objp+colour_off;

			return slabp;
		}

		kmem_cache_alloc-->kmem_cache_grow-->kmem_cache_init_objs()
		static inline void kmem_cache_init_objs (kmem_cache_t * cachep,
			slab_t * slabp, unsigned long ctor_flags)
		{
			int i;

			for (i = 0; i < cachep->num; i++) {
				void* objp = slabp->s_mem+cachep->objsize*i;

				/*
				 * Constructors are not allowed to allocate memory from
				 * the same cache which they are a constructor for.
				 * Otherwise, deadlock. They must also be threaded.
				 */
				if (cachep->ctor)
					cachep->ctor(objp, cachep, ctor_flags);

				slab_bufctl(slabp)[i] = i+1;
			}
			slab_bufctl(slabp)[i-1] = BUFCTL_END;
			slabp->free = 0;
		}

/*********************************************************************************************/
/*
 * slab对象的释放
 */

 	slab.c-->kmem_cache_free()

	void kmem_cache_free (kmem_cache_t *cachep, void *objp)
	{
		unsigned long flags;

		/* 开关中断 */
		local_irq_save(flags);
		__kmem_cache_free(cachep, objp);
		local_irq_restore(flags);
	}

	kmem_cache_free-->__kmem_cache_free()

	static inline void __kmem_cache_free (kmem_cache_t *cachep, void* objp)
	{
	#ifdef CONFIG_SMP
		cpucache_t *cc = cc_data(cachep);

		CHECK_PAGE(virt_to_page(objp));
		if (cc) {
			int batchcount;
			if (cc->avail < cc->limit) {
				STATS_INC_FREEHIT(cachep);
				cc_entry(cc)[cc->avail++] = objp;
				return;
			}
			STATS_INC_FREEMISS(cachep);
			batchcount = cachep->batchcount;
			cc->avail -= batchcount;
			free_block(cachep,
						&cc_entry(cc)[cc->avail],batchcount);
			cc_entry(cc)[cc->avail++] = objp;
			return;
		} else {
			free_block(cachep, &objp, 1);
		}
	#else
		kmem_cache_free_one(cachep, objp);
	#endif
	}

		kmem_cache_free-->__kmem_cache_free-->kmem_cache_free_one()

		static inline void kmem_cache_free_one(kmem_cache_t *cachep, void *objp)
		{
			slab_t* slabp;

			/* reduces memory footprint
			 *
			if (OPTIMIZE(cachep))
				slabp = (void*)((unsigned long)objp&(~(PAGE_SIZE-1)));
			 else
			 */
			slabp = GET_PAGE_SLAB(virt_to_page(objp));

			{
				unsigned int objnr = (objp-slabp->s_mem)/cachep->objsize;

				slab_bufctl(slabp)[objnr] = slabp->free;
				slabp->free = objnr;
			}
			STATS_DEC_ACTIVE(cachep);
			
			/* fixup slab chain */
			if (slabp->inuse-- == cachep->num)
				goto moveslab_partial;
			if (!slabp->inuse)
				goto moveslab_free;
			return;

		moveslab_partial:
		    	/* was full.
			 * Even if the page is now empty, we can set c_firstnotfull to
			 * slabp: there are no partial slabs in this case
			 */
			{
				struct list_head *t = cachep->firstnotfull;

				cachep->firstnotfull = &slabp->list;
				if (slabp->list.next == t)
					return;
				list_del(&slabp->list);
				list_add_tail(&slabp->list, t);
				return;
			}
		moveslab_free:
			/*
			 * was partial, now empty.
			 * c_firstnotfull might point to slabp
			 * FIXME: optimize
			 */
			{
				struct list_head *t = cachep->firstnotfull->prev;

				list_del(&slabp->list);
				list_add_tail(&slabp->list, &cachep->slabs);
				if (cachep->firstnotfull == &slabp->list)
					cachep->firstnotfull = t->next;
				return;
			}
		}
/*********************************************************************************************/
























