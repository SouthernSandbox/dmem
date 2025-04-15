#include "dmem.h"
#include "stdio.h"

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
    char* pool;                     /** 内存池 **/
    unsigned int size;              /** 内存池大小 **/
    unsigned int free;              /** 当前空闲的内存大小 **/
    unsigned int max_usage;         /** 记录内存消耗的最大值 @note 记录所有的非空闲内存的占用，包括内存块消息结构体 **/
    unsigned int inited_free;       /** 记录初始化时，空闲内存块的大小 **/
    dmem_block_t bhead;             /** 首内存块且始终指向首内存块 **/
    dmem_block_t btail;             /** 尾内存块且始终指向尾内存块 **/
    dmem_block_t bfree;             /** 始终指向第一个空闲内存块 **/
};
static struct dmem_mgr mgr = {0};

/**
 * @brief 内存块信息结构体
 */
struct dmem_block
{
    unsigned short magic;           /** 幻数 **/
    bool used;                      /** 是否已使用 **/
    unsigned int prev, next;        /** 先后节点的偏移量 **/
};
#define dmem_pool_at(offset)            (mgr.pool + (offset))
#define dmem_pool_size()                (mgr.size)
#define dmem_block_magic()              (0xf00d)
#define dmem_block_size()               (sizeof(struct dmem_block))
#define dmem_head_block()               (mgr.bhead)
#define dmem_tail_block()               (mgr.btail)
#define dmem_free_block()               (mgr.bfree)
#define dmem_block_offset(block)        (unsigned int)((char*)(block) - (char*)(dmem_head_block()))
#define dmem_min_alloc_size()           (dmem_block_size())
#define dmem_block_mem_size(block)      ((block)->next - dmem_block_offset(block) - dmem_block_size())
#define dmem_block_mem_addr(block)      (((char*)(block)) + dmem_block_size())
#define dmem_block_prev(block)          ((dmem_block_t) dmem_pool_at((block)->prev))
#define dmem_block_next(block)          ((dmem_block_t) dmem_pool_at((block)->next))
#define dmem_block_entry(mem)           ((dmem_block_t)(((char*)mem) - dmem_block_size()))
#define dmem_block_is_valid(block)      ((block)->magic == dmem_block_magic())
#define dmem_block_is_unused(block)     ((!(block)->used) && dmem_block_is_valid(block))

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
    if(size < dmem_min_alloc_size())
        size = dmem_min_alloc_size();
    if(dmem_free_block() == NULL)
        return NULL;

    /** 遍历，搜寻可用的内存块 **/
    pos = dmem_free_block();
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
            dmem_block_t nn = dmem_block_next(pos);
            next->magic = dmem_block_magic();
            next->used = false;
            next->prev = dmem_block_offset(pos);
            next->next = dmem_block_offset(nn);
            pos->next = dmem_block_offset(next);
            nn->prev = dmem_block_offset(next);

            mgr.free -= dmem_block_size();
        }
        pos->used = true;

        /** 更新 bfree **/
        dmem_free_block() = _search_free_block_for_alloc(pos);

        /** 更新管理器记录 **/
        mgr.free -= dmem_block_mem_size(pos);
        _update_max_usage();

        return dmem_block_mem_addr(pos);
    }
    return NULL;
}

/**
 * @brief 释放被分配的内存
 * @note 该函数不具备线程安全
 * @param mem 待释放的内存地址
 * @return int  0:  释放成功
 *              -1: mem 为 NULL
 *              -2: 内存块信息无效
 *              -3: 该内存块不可重复释放
 */
static int _free(void* mem)
{
    dmem_block_t block = NULL;

    /** 检查 mem 的合法性 **/
    if(!mem)
        return -1;
    /** 检查内存块合法性 **/
    block = dmem_block_entry(mem);
    if(!dmem_block_is_valid(block))
        return -2;
    /** 检查内存释放被占用 **/
    if(!block->used)
        return -3;
    /** 重置标志位 **/
    block->used = false;
    /** 更新管理器记录 **/
    mgr.free += (dmem_block_mem_size(block));

    // 检查上一个节点，如果空闲，则进行合并
    if(dmem_head_block() != block)   // 忽略当前内存块是首节点的情况
    {
        dmem_block_t prev = dmem_block_prev(block);
        if(dmem_block_is_unused(prev))
        {
            dmem_block_t bn = dmem_block_next(block);
            block->prev = prev->prev;
            *prev = *block;
            block = prev;
            bn->prev = dmem_block_offset(block);
            /** 更新管理器记录 **/
            mgr.free += dmem_block_size();
        }
    }

    // 检查下一个节点，如果空闲，则进行合并
    {
        dmem_block_t next = dmem_block_next(block);
        // 忽略下一个节点是尾节点的情况
        if(dmem_block_is_unused(next))   
        {
            dmem_block_t nn = dmem_block_next(next);
            block->next = next->next;
            nn->prev = dmem_block_offset(block);
            /** 更新管理器记录 **/
            mgr.free += dmem_block_size();
        }
    }

    /** 重置 bfree **/
    if(dmem_free_block() - block > 0 )
        dmem_free_block() = block;

    /** 更新管理器记录 **/
    _update_max_usage();

    return 0;
}



/**
 * @brief 初始化动态内存分配管理
 * @param pool 内存池地址
 * @param size 内存池可使用的大小
 * @return int  0:  分配成功
 *              -1: 指定的内存池地址为 NULL
 *              -2: 内存池大小必须大于 0
 */
int dmem_init(void* pool, unsigned int size)
{
    /** 检查输入参数的合法性 **/
    if(pool == NULL)
        return -1;
    if(size == 0)
        return -2;
    mgr.pool = (char*) pool;
    mgr.size = size;

    mgr.bhead = (dmem_block_t) dmem_pool_at(0);
    mgr.bhead->magic = dmem_block_magic();
    mgr.bhead->prev = dmem_block_offset(dmem_head_block());
    mgr.bhead->used = false;

    mgr.btail = (dmem_block_t) dmem_pool_at(dmem_pool_size() - dmem_block_size());
    mgr.btail->magic = dmem_block_magic();
    mgr.btail->prev = dmem_block_offset(dmem_head_block());
    mgr.btail->used = true;

    mgr.bhead->next = dmem_block_offset(dmem_tail_block());
    mgr.btail->next = dmem_block_offset(dmem_tail_block());

    mgr.bfree = dmem_head_block();

    mgr.free = dmem_block_mem_size(dmem_head_block());
    mgr.max_usage = dmem_pool_size() - mgr.free;
    mgr.inited_free = mgr.free;

    return 0;
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
    dmem_block_t block = dmem_block_entry(old_mem);
    void* p = old_mem;

    dmem_get_lock();
    /** 检查输入参数的合法性 **/
    if(!old_mem)
    {
        dmem_rel_lock();
        return NULL;
    }
    /** 如果当前的内存块的实际可用内存大小符合新指定的内存大小，则直接返回 **/
    if(dmem_block_mem_size(block) < new_size)
    {
        /** 尝试分配新的内存 **/
        p = _alloc(new_size);
        if(p)
        {
            /** 如果分到了新的内存，则需要将旧的内存的数据转存到新分配的内存中 **/
            if(p != old_mem)
            {
                memcpy(p, old_mem, dmem_block_mem_size(block));
                _free(old_mem);
            }
        }
            
    }
    dmem_rel_lock();

    return p;
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
 * @brief 获取内存使用报告
 * @return struct dmem_use_report* 
 */
struct dmem_use_report* dmem_get_use_report(void)
{
    static struct dmem_use_report report = {0};
    dmem_block_t pos = NULL;

    dmem_get_lock();
    report.free = mgr.free;
    report.max_usage = mgr.max_usage;
    report.initf = mgr.inited_free;
    report.used_count = 0;
    for(pos = dmem_head_block(); pos != dmem_tail_block(); pos = dmem_block_next(pos))
        if(!dmem_block_is_unused(pos))
            report.used_count++;
    dmem_rel_lock();
    return &report;
}
