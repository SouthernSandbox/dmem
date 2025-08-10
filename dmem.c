/**
 * @file dmem.c
 * @author Southern Sandbox
 * @brief dmem core
 * @version 0.1
 * @date 2025-08-09
 * 
 * @copyright Copyright (c) 2025
 * 
 */

#include "dmem.h"
#include "stdio.h"

/**
 * @brief 辅助宏定义
 */
#define MAKE_ALLOC_SIZE_ALIGN(size)        \
        (size + (DMEM_DEFINE_ALIGN_SIZE - 1)) & ~(DMEM_DEFINE_ALIGN_SIZE - 1)       // 计算比 size 大且最接近 size 的 n 字节对齐值
#define MAKE_POOL_SIZE_ALIGN(size)         \
        ((size) < DMEM_DEFINE_ALIGN_SIZE ? 0 : \
        ((size) - (DMEM_DEFINE_ALIGN_SIZE - 1)) & ~(DMEM_DEFINE_ALIGN_SIZE - 1))    // 计算比 size 小且最接近 size 的 n 字节对齐值


/**
 * @brief 线程锁函数声明
 */
extern int dmem_get_lock(void);
extern int dmem_rel_lock(void);

struct dmem_block;
typedef struct dmem_block* dmem_block_t;

/**
 * @brief 内存块管理器
 */
struct dmem_mgr
{
    char* pool;                 /** 内存池 **/
    uint32_t size;              /** 内存池大小 **/
    uint32_t free;              /** 当前空闲的内存大小 **/
    uint32_t max_usage;         /** 记录内存消耗的最大值 @note 记录所有的非空闲内存的占用，包括内存块消息结构体 **/
    uint32_t inited_free;       /** 记录初始化时，空闲内存块的大小 **/
    dmem_block_t bhead;         /** 首内存块且始终指向首内存块 **/
    dmem_block_t btail;         /** 尾内存块且始终指向尾内存块 **/
    dmem_block_t bfree;         /** 始终指向第一个空闲内存块 **/
};
static struct dmem_mgr mgr = {0};

/**
 * @brief 内存块信息结构体
 */
struct dmem_block
{
    uint16_t magic;         /** 幻数 **/
    uint16_t used;          /** 是否已使用 **/
    uint16_t prev;          /** 前一个节点的偏移量 **/
    uint16_t next;          /** 后一个后节点的偏移量 **/
};

#define dmem_pool_at(offset)            (mgr.pool + (offset))
#define dmem_pool_size()                (mgr.size)
#define dmem_block_magic()              (0xf00d)
#define dmem_block_size()               (sizeof(struct dmem_block))
#define dmem_head_block()               (mgr.bhead)
#define dmem_tail_block()               (mgr.btail)
#define dmem_free_block()               (mgr.bfree)
#define dmem_block_offset(block)        (unsigned int)((char*)(block) - (char*)(dmem_head_block()))
#define dmem_min_alloc_size()           (DMEM_MIN_ALLOC_SIZE)
#define dmem_block_mem_size(block)      ((block)->next - dmem_block_offset(block) - dmem_block_size())
#define dmem_block_mem_addr(block)      (((char*)(block)) + dmem_block_size())
#define dmem_block_prev(block)          ((dmem_block_t) dmem_pool_at((block)->prev))
#define dmem_block_next(block)          ((dmem_block_t) dmem_pool_at((block)->next))
#define dmem_block_entry(mem)           ((dmem_block_t)(((char*)mem) - dmem_block_size()))
#define dmem_block_is_valid(block)      ((block)->magic == dmem_block_magic())
#define dmem_block_is_unused(block)     ((!(block)->used) && dmem_block_is_valid(block))

/**
 * @brief 合并相邻的空闲内存块
 * @param prev 前一个空闲内存块 
 * @param next 后一个空闲内存块
 */
static void _merge_free_blocks(dmem_block_t prev, dmem_block_t next)
{
    /** 检查内存块是否被使用 **/
    if(!dmem_block_is_unused(prev) || !dmem_block_is_unused(next))
        return;

    dmem_trace (DMEM_LEVEL_DEBUG, 
                "Merging blocks | Prev: %p (%u bytes) | Next: %p (%u bytes)", 
                prev, dmem_block_mem_size(prev),
                next, dmem_block_mem_size(next));

    dmem_block_t next_next = dmem_block_next(next);
    prev->next = dmem_block_offset(next_next);
    next_next->prev = dmem_block_offset(prev);

    mgr.free += dmem_block_size();

    dmem_trace (DMEM_LEVEL_DEBUG,
                "Merged result | Block: %p | Size: %u bytes | Total free: %u bytes", 
                prev, dmem_block_mem_size(prev), mgr.free);
}

/**
 * @brief 更新最大内存消耗
 */
static void _update_max_usage(void)
{
    int usage = dmem_pool_size() - mgr.free;
    if(usage > mgr.max_usage)
        mgr.max_usage = usage;
}

/**
 * @brief 查找第一个空闲内存块
 * @note 该函数仅在 _alloc() 中调用
 * @param start 当前的内存块信息结构体
 * @return dmem_block_t 
 */
static dmem_block_t _search_free_block_for_alloc(dmem_block_t start)
{
    if(dmem_block_is_unused(dmem_free_block()))
        return dmem_free_block();
    else
    {
        dmem_block_t pos = dmem_block_next(start);
        if(dmem_block_is_unused(pos))
            return pos;
        else 
        {
            for( ; pos != dmem_tail_block(); pos = dmem_block_next(pos))
                if(dmem_block_is_unused(pos))
                    return pos;
            return NULL;
        }
    }
}

/**
 * @brief 依据指定的大小分配连续的内存空间
 * @note 该函数不具备线程安全
 * @param size 待分配的内存的大小
 * @return void* 若分配成功则返回非 NULL 内存地址，反之则返回 NULL
 */
static void* _alloc(unsigned int size)
{
    dmem_block_t pos = NULL;
    int free_size = 0;

    if(size == 0)
        return NULL;
    if(!IS_DMEM_VAR_ALIGNED(size, DMEM_DEFINE_ALIGN_SIZE))   // 如果分配的内存大小不符合对齐要求, 则数值向上分配内存对齐的待大小（如127->128）
        size = MAKE_ALLOC_SIZE_ALIGN(size);
    if(size < dmem_min_alloc_size())
        size = dmem_min_alloc_size();
    if((pos = dmem_free_block()) == NULL)
        goto _ALLOC_FAILED_;

    /** 遍历，搜寻可用的内存块 **/
    for( ; pos != dmem_tail_block(); pos = dmem_block_next(pos))
    {
        if(!dmem_block_is_unused(pos))
            continue;
        if(dmem_block_mem_size(pos) < size)
            continue;
        
        /**
         * 匹配成功，剩余空间是否还可以创建新的空闲内存块，
         * 如果无法创建新的内存块，则将剩余的空闲内存全部分配，
         * 避免出现无法被管理的内存碎片
         */
        free_size = dmem_block_mem_size(pos) - size;
        if(free_size < (dmem_min_alloc_size() + dmem_block_size()))
        {

        }
        else 
        {
            /** 创建新的空闲内存块 **/
            dmem_block_t next = (dmem_block_t)(((char*)pos) + dmem_block_size() + size);
            dmem_block_t next_next = dmem_block_next(pos);
            next->magic = dmem_block_magic();
            next->used = false;
            next->prev = dmem_block_offset(pos);
            next->next = dmem_block_offset(next_next);
            pos->next = dmem_block_offset(next);
            next_next->prev = dmem_block_offset(next);

            mgr.free -= dmem_block_size();
        }
        pos->used = true;

        /** 更新 bfree **/
        dmem_free_block() = _search_free_block_for_alloc(pos);

        /** 更新管理器记录 **/
        mgr.free -= dmem_block_mem_size(pos);
        _update_max_usage();

        dmem_trace( DMEM_LEVEL_DEBUG, 
                    "Allocated %u bytes at %p | Block: %p | Remaining free: %u bytes", 
                    dmem_block_mem_size(pos), dmem_block_mem_addr(pos), 
                    pos, mgr.free);

        return dmem_block_mem_addr(pos);
    }

_ALLOC_FAILED_:;
    dmem_trace(DMEM_LEVEL_WARNING, "Allocation failed | Requested: %u bytes | Free: %u bytes", size, mgr.free);
    return NULL;
}

/**
 * @brief 释放被分配的内存
 * @note 该函数不具备线程安全
 * @param mem 待释放的内存地址
 * @return int  - DMEM_ERR_NONE           : 释放成功
 *              - DMEM_FREE_NULL          : mem 为 NULL
 *              - DMEM_FREE_INVALID_MEM   : 内存块信息无效
 *              - DMEM_FREE_REPEATED      : 该内存块不可重复释放
 */
static int _free(void* mem)
{
    dmem_block_t block = NULL;

    /** 检查 mem 的合法性 **/
    if(!mem)
    {
        dmem_trace(DMEM_LEVEL_ERROR, "Address is NULL");
        return DMEM_FREE_NULL;
    }
        
    /** 检查内存块合法性 **/
    block = dmem_block_entry(mem);
    if(!dmem_block_is_valid(block))
    {
        dmem_trace(DMEM_LEVEL_ERROR, "Block is invalid");
        return DMEM_FREE_INVALID_MEM;
    }

    /** 检查内存释放被占用 **/
    if(!block->used)
    {
        dmem_trace(DMEM_LEVEL_ERROR, "Double free detected | Addr: %p | Block: %p", mem, block);
        return DMEM_FREE_REPEATED;
    }

    /** 重置标志位 **/
    block->used = false;

    /** 更新管理器记录 **/
    mgr.free += (dmem_block_mem_size(block));
    dmem_trace(DMEM_LEVEL_DEBUG, "Freed %u bytes at %p | Block: %p | New free: %u bytes", dmem_block_mem_size(block), mem, block, mgr.free);

    // 检查上一个节点，如果空闲，则进行合并
    if(dmem_head_block() != block)      // 忽略当前内存块是首节点的情况
    {
        dmem_block_t prev = dmem_block_prev(block);
        if (dmem_block_is_unused(prev)) 
        {
            _merge_free_blocks(prev, block);
            block = prev;       // 合并后，block 指向合并后的内存块
        }   
    }

    // 检查下一个节点，如果空闲，则进行合并
    {
        dmem_block_t next = dmem_block_next(block);
        if (dmem_block_is_unused(next))
            _merge_free_blocks(block, next);
    }

    /** 重置 bfree **/
    if (dmem_free_block() == NULL || 
        dmem_block_offset(block) < dmem_block_offset(dmem_free_block()))
    {

        dmem_free_block() = block;
    }



    /** 更新管理器记录 **/
    _update_max_usage();

    return DMEM_ERR_NONE;
}

/**
 * @brief 将已分配的内存块拆分为更小的已用内存卡和空闲内存块(并对相连的内存块进行合并操作),
 *        若无法拆分，则不进行拆分.
 * @param block 
 * @param size 
 * @return int 
 */
static void _split(dmem_block_t block, unsigned int new_size)
{
    /** 如果当前的内存块的实际可用内存大小大于新指定的内存大小，若可进行内存块的拆分, 则需要进行内存块的拆分 **/
    if(dmem_block_mem_size(block) - new_size > (dmem_min_alloc_size() + dmem_block_size()))
    {   
        /** 获取当前内存块后一个内存块 **/
        dmem_block_t next = dmem_block_next(block);
        uint32_t old_used_mem_size = dmem_block_mem_size(block);

        /** 将剩余部分变为空闲内存块 **/
        dmem_block_t new_free = (dmem_block_t)(((char*)block) + dmem_block_size() + new_size);
        new_free->magic = dmem_block_magic();
        new_free->used = false;
        new_free->prev = dmem_block_offset(block);
        new_free->next = dmem_block_offset(next);

        /** 调整节点指向 **/
        block->next = dmem_block_offset(new_free);
        next->prev = dmem_block_offset(new_free);

        /** 重新计算内存块大小 **/
        mgr.free += (old_used_mem_size - (new_size + dmem_block_size()));

        /** 如果后方内存块是空闲的, 则将新的空闲内存块与其进行合并 **/
        _merge_free_blocks(new_free, next);

        /** 更新管理器记录 **/
        _update_max_usage();

        dmem_trace( DMEM_LEVEL_DEBUG, 
                    "Split block: %p | Old: %u -> New: %u + Free: %u", 
                    block, old_used_mem_size, new_size, 
                    dmem_block_mem_size(new_free));
    }
    else
        dmem_trace( DMEM_LEVEL_DEBUG, "Block can not be splitted");
}

/**
 * @brief 适用于 dmem_realloc() 函数，在当前内存块的后方尝试就地扩展内存
 * @param block 
 * @param new_size 
 * @return true 
 * @return false 
 */
static bool _expand_inplace(dmem_block_t block, uint32_t new_size) 
{
    uint32_t needed = new_size - dmem_block_mem_size(block);        // 需要扩展的内存大小(不包含原已分配内存块的大小)
    dmem_block_t next = dmem_block_next(block);                     
    
    /** 检查后一个内存块是否为空闲块 **/
    if (dmem_block_is_unused(next)) 
    {
        uint32_t total_avail = dmem_block_mem_size(next) + dmem_block_size();
        dmem_trace( DMEM_LEVEL_DEBUG, "total_avail: %u bytes, needed: %u bytes", total_avail, needed);
        
        /** 如果新加入的空闲内存块大小足够，则就地扩展 **/
        if (total_avail >= needed) 
        {
            dmem_trace( DMEM_LEVEL_DEBUG, 
                        "In-place expand: %u -> %u bytes", 
                        dmem_block_mem_size(block), new_size);
            
            /** 移除空闲块 **/
            dmem_block_t next_next = dmem_block_next(next);
            block->next = dmem_block_offset(next_next);
            next_next->prev = dmem_block_offset(block);
            
            /** 更新空闲统计 **/
            uint32_t remined = total_avail - needed;
            mgr.free += dmem_block_size();
            mgr.free -= needed;
            dmem_trace( DMEM_LEVEL_DEBUG, "Free: %u bytes, Remined: %u bytes", mgr.free, remined);
            
            /** 若有剩余空间，创建新空闲块 **/
            if (remined >= dmem_min_alloc_size() + dmem_block_size()) 
            {
                dmem_block_t new_free = (dmem_block_t)((char*)block + dmem_block_size() + new_size);
                new_free->magic = dmem_block_magic();
                new_free->used = false;
                new_free->prev = dmem_block_offset(block);
                new_free->next = dmem_block_offset(next_next);

                block->next = dmem_block_offset(new_free);
                next_next->prev = dmem_block_offset(new_free);

                mgr.free -= dmem_block_size();
            }

            dmem_trace( DMEM_LEVEL_DEBUG,
                        "After in-place expand, Free: %u ytes",
                        mgr.free);

            _update_max_usage();

            return true;
        }
    }
    return false;
}









/**
 * @brief 初始化动态内存分配管理
 * @param pool 内存池地址
 * @param size 内存池可使用的大小
 * @return int  - DMEM_ERR_NONE           : 分配成功
 *              - DMEM_INIT_POOL_NULL     : 指定的内存池地址为 NULL
 *              - DMEM_INIT_SIZE_SMALL    : 内存池大小过小
 *              - DMEM_INIT_POOL_ALIGN    : 内存池地址未对齐
 */
int dmem_init(void* pool, unsigned int size)
{
    /** 内存管理器初始化 **/
    memset(&mgr, 0, sizeof(mgr));

    /** 内存池不可为 NULL **/
    if(pool == NULL)
    {
        dmem_trace(DMEM_LEVEL_ERROR, "Pool's address is NULL!");
        return DMEM_INIT_POOL_NULL;
    }

    /** 内存池地址未对齐 **/
    if(!IS_DMEM_VAR_ALIGNED(pool, DMEM_DEFINE_ALIGN_SIZE))
    {
        dmem_trace(DMEM_LEVEL_WARNING, "Current pool address is not aligned(%p)", pool);
        return DMEM_INIT_POOL_ALIGN;
    } 

    /** 检查内存池大小对齐情况，若未对齐，则尝试对齐 **/
    if(!IS_DMEM_VAR_ALIGNED(size, DMEM_DEFINE_ALIGN_SIZE))
    {
        dmem_trace(DMEM_LEVEL_WARNING, "Current pool size is not aligned(%d bytes), dmem will adjust other size...", size);
        size = MAKE_POOL_SIZE_ALIGN(size);
        dmem_trace(DMEM_LEVEL_INFO, "New pool size: %d bytes", size);
    }

    /** 内存池大小过小 **/
    if(size < dmem_min_alloc_size() + dmem_block_size() * 2)
    {
        dmem_trace(DMEM_LEVEL_ERROR, "Pool size is too small!");
        return DMEM_INIT_SIZE_SMALL;
    }

    /** 保存内存池 **/
    mgr.pool = (char*) pool;
    mgr.size = size;

    dmem_head_block() = (dmem_block_t) dmem_pool_at(0);
    dmem_head_block()->magic = dmem_block_magic();
    dmem_head_block()->prev = dmem_block_offset(dmem_head_block());
    dmem_head_block()->used = false;

    dmem_tail_block() = (dmem_block_t) dmem_pool_at(dmem_pool_size() - dmem_block_size());
    dmem_tail_block()->magic = dmem_block_magic();
    dmem_tail_block()->prev = dmem_block_offset(dmem_head_block());
    dmem_tail_block()->used = true;

    dmem_head_block()->next = dmem_block_offset(dmem_tail_block());
    dmem_tail_block()->next = dmem_block_offset(dmem_tail_block());

    dmem_free_block() = dmem_head_block();

    mgr.free = dmem_block_mem_size(dmem_head_block());
    mgr.max_usage = dmem_pool_size() - mgr.free;
    mgr.inited_free = mgr.free;
    
    dmem_trace(DMEM_LEVEL_INFO, "Initialized memory pool | Addr: %p | Size: %u bytes", pool, size);
    dmem_trace(DMEM_LEVEL_DEBUG, "Head block: %p | Tail block: %p | Free: %u bytes", dmem_head_block(), dmem_tail_block(), mgr.free);

    return DMEM_ERR_NONE;
}

/**
 * @brief 依据指定的大小安全地分配连续的空间
 * @param size 需要分配的内存的大小
 * @return void* 若分配成功则返回非 NULL 内存地址，反之则返回 NULL
 */
void* dmem_alloc(unsigned int size)
{
    void* p = NULL;
    dmem_get_lock();
    p = _alloc(size);
    dmem_rel_lock();
    return p;
}

/**
 * @brief 依据指定的大小重新分配新的连续的空间，并释放旧的已分配内存
 * @param old_mem 旧的被分配的内存
 * @param new_size 新的被指定的内存大小
 * @return void* 若分配成功则返回非 NULL 内存地址，反之则返回 NULL
 */
void* dmem_realloc(void* old_mem, unsigned int new_size)
{
    /** [1] 处理 NULL 和 size=0 的特殊情况 **/
    // 如果输入为 NULL, 则相当于执行新内存分配
    if(old_mem == NULL)
    {
        dmem_trace(DMEM_LEVEL_INFO, "Realloc NULL -> new allocation | Size: %u bytes", new_size);
        return dmem_alloc(new_size);
    }

    // 如果新分配内存为 0, 则执行内存释放功能
    if(new_size == 0)
    {
        dmem_trace(DMEM_LEVEL_DEBUG, "New size is 0, free old memory");
        dmem_free(old_mem);
        return NULL;
    }

    /** [2] 对齐处理（统一使用向上对齐） **/
    if(!IS_DMEM_VAR_ALIGNED(new_size, DMEM_DEFINE_ALIGN_SIZE))
    {
        dmem_trace(DMEM_LEVEL_WARNING, "Current pool size is not aligned(%d bytes), dmem will adjust other size...", new_size);
        new_size = MAKE_ALLOC_SIZE_ALIGN(new_size);
        dmem_trace(DMEM_LEVEL_INFO, "New pool size: %d bytes", new_size);
    }

    dmem_block_t block = dmem_block_entry(old_mem);
    void* new_mem = old_mem;  // 默认返回原地址
    
    dmem_get_lock();

    /** [3] 验证内存块有效性 **/
    if(!dmem_block_is_valid(block))
    {
        dmem_trace(DMEM_LEVEL_ERROR, "Old memory is invalid!");
        dmem_rel_lock();
        return NULL;
    }

    uint32_t old_size = dmem_block_mem_size(block);

    // [4] 大小不变
    if (new_size == old_size) 
    {
        dmem_trace(DMEM_LEVEL_DEBUG, "Realloc same size: %u bytes @ %p", new_size, old_mem);
        dmem_rel_lock();
        return old_mem;
    }

    // [5] 扩展内存
    if (new_size > old_size) 
    {
        // 优先尝试就地扩展
        if (_expand_inplace(block, new_size)) 
        {
            dmem_rel_lock();
            return old_mem;
        }
        
        // 无法就地扩展则分配新内存
        dmem_trace(DMEM_LEVEL_DEBUG, "Allocating new block for realloc: %u -> %u bytes", old_size, new_size);
        
        if ((new_mem = _alloc(new_size))) 
        {
            memmove(new_mem, old_mem, old_size);
            _free(old_mem);
        } 
        else 
        {
            // 分配失败：保留原内存块
            dmem_trace(DMEM_LEVEL_WARNING, "Realloc failed, keeping original block");
            new_mem = old_mem;  // 必须返回原指针
        }
    } 
    // [6] 收缩内存
    else 
    {
        dmem_trace(DMEM_LEVEL_DEBUG, "Shrinking block: %u -> %u bytes @ %p",  old_size, new_size, old_mem);
        _split(block, new_size);
    }
    
    dmem_rel_lock();
    return new_mem;
}

/**
 * @brief 分配指定大小和数量的连续空间，并自动将已分配的内存初始化为 0
 * @param count 对象的数量
 * @param size 对象的大小
 * @return void* 若分配成功则返回非 NULL 内存地址，反之则返回 NULL
 */
void* dmem_calloc(unsigned int count, unsigned int size)
{
    unsigned int total = count * size;
    void* p = NULL;

    dmem_get_lock();
    p = _alloc(total);
    if(p)
        memset(p, 0, total);
    dmem_rel_lock();

    return p;
}

/**
 * @brief 安全地释放被分配的内存
 * @param mem 待释放的内存
 * @return int  0:  释放成功
 *              -1: mem 为 NULL
 *              -2: 内存块信息无效
 *              -3: 该内存块不可重复释放
 */
int dmem_free(void* mem)
{
    int res = 0;
    dmem_get_lock();
    res = _free(mem);
    dmem_rel_lock();
    return res;
}

/**
 * @brief 读取内存使用报告
 * @note 支持可重入获取内存使用报告
 * @param result 用户填入的内存使用报告结构体，由函数内部填充
 */
void dmem_read_use_report(struct dmem_use_report* result)
{
    dmem_block_t pos = NULL;
    dmem_get_lock();
    result->free = mgr.free;
    result->max_usage = mgr.max_usage;
    result->initf = mgr.inited_free;
    result->used_count = 0;
    for(pos = dmem_head_block(); pos != dmem_tail_block(); pos = dmem_block_next(pos))
        if(!dmem_block_is_unused(pos))
            result->used_count++;
    dmem_rel_lock();
}

#if ENABLE_DMEM_GET_USER_REPORT_API
/**
 * @brief 获取内存使用报告指针
 * @warning 该函数不具备可重入性，并且每次调用会触发多次查询
 * @return struct dmem_use_report* 
 */
const struct dmem_use_report* dmem_get_use_report(void)
{
    static struct dmem_use_report report = {0};
    dmem_read_use_report(&report);
    return &report;
}
#endif
