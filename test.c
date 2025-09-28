/**
 * @file test.c
 * @author Southern Sandbox
 * @brief 
 * @version 0.1
 * @date 2025-08-09
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "dmem.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "assert.h"
#include "stdint.h"
#include "stddef.h"
#include "time.h"


// 128字节内存池（4字节对齐）
DMEM_DEFAULT_ALIGNED(static char test_pool[128]);

// 内存块头结构（根据dmem.h中的定义）
typedef struct
{
    uint16_t magic;
    uint16_t used;
    uint16_t prev;
    uint16_t next;
} mem_block_t;

// 获取内存池使用情况
void print_mem_report(const char *title)
{
    struct dmem_use_report rpt = *dmem_get_use_report();
    printf("\n=== [%s] ===\n", title);
    printf("总内存: %d\n", 128);
    printf("空闲内存: %d\n", rpt.free);
    printf("最大使用量: %d\n", rpt.max_usage);
    printf("初始空闲: %d\n", rpt.initf);
    printf("已用块数: %d\n", rpt.used_count);
}

// 计算内存池的开销（头尾块）
int get_fixed_overhead()
{
    return 2 * sizeof(mem_block_t); // 头块 + 尾块
}

// 计算每个分配块的额外开销
int get_block_overhead()
{
    return sizeof(mem_block_t);
}

// 计算实际分配的内存大小（包括对齐）
int get_real_alloc_size(int request_size)
{
    if (request_size < DMEM_MIN_ALLOC_SIZE)
    {
        request_size = DMEM_MIN_ALLOC_SIZE;
    }
    return (request_size + (DMEM_DEFINE_ALIGN_SIZE - 1)) & ~(DMEM_DEFINE_ALIGN_SIZE - 1);
}

// 验证指针是否在内存池范围内
int is_pointer_valid(void *ptr)
{
    return (ptr >= (void *)test_pool &&
            ptr < (void *)(test_pool + sizeof(test_pool)));
}

// 验证内存对齐
int is_aligned(void *ptr)
{
    return ((uintptr_t)ptr % DMEM_DEFINE_ALIGN_SIZE) == 0;
}

static void _test_initialization()
{
    printf("\n===== [测试1: 初始化测试] =====\n");

    // 初始化内存池
    int init_result = dmem_init(test_pool, sizeof(test_pool));
    printf("初始化结果: %d\n", init_result);
    assert(init_result == 0);

    // 获取内存报告
    print_mem_report("初始化后");

    // 验证初始状态
    struct dmem_use_report rpt = *dmem_get_use_report();
    int fixed_overhead = get_fixed_overhead();
    int expected_free = sizeof(test_pool) - fixed_overhead;

    printf("固定开销: %d字节 (头块+尾块)\n", fixed_overhead);
    printf("预期初始空闲: %d字节\n", expected_free);
    printf("实际初始空闲: %d字节\n", rpt.free);

    assert(rpt.free == expected_free);
    assert(rpt.used_count == 0);
    assert(rpt.initf == expected_free);
    assert(rpt.max_usage == fixed_overhead); // 只有头尾块被使用

    // 测试非法初始化
    printf("\n测试非法初始化...\n");
    assert(dmem_init(NULL, 128) == -1);
    assert(dmem_init(test_pool, 12) == -2);    // 小于最小要求
    assert(dmem_init((void *)0x1, 128) == -3); // 未对齐地址

    printf("===== [测试1通过] =====\n");
}

static void _test_basic_allocation()
{
    printf("\n===== [测试2: 基本分配测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");

    int fixed_overhead = get_fixed_overhead();
    int block_overhead = get_block_overhead();
    int initial_free = sizeof(test_pool) - fixed_overhead;

    // 分配小于最小块大小(4字节)
    printf("\n分配2字节...\n");
    void *p1 = dmem_alloc(2);
    assert(p1 != NULL);
    assert(is_pointer_valid(p1));
    assert(is_aligned(p1));

    int real_size = get_real_alloc_size(2);
    int expected_used = block_overhead + real_size;
    int expected_free_after = initial_free - expected_used;

    struct dmem_use_report rpt = *dmem_get_use_report();
    printf("分配2字节 -> 实际分配: %d字节 (块头:%d + 用户数据:%d)\n",
           expected_used, block_overhead, real_size);
    printf("预期空闲: %d字节, 实际空闲: %d字节\n", expected_free_after, rpt.free);

    assert(rpt.free == expected_free_after);
    assert(rpt.used_count == 1);

    // 分配精确对齐大小
    printf("\n分配4字节...\n");
    void *p2 = dmem_alloc(4);
    assert(p2 != NULL);
    assert(is_pointer_valid(p2));
    assert(is_aligned(p2));

    // 分配非对齐大小
    printf("\n分配7字节...\n");
    void *p3 = dmem_alloc(7);
    assert(p3 != NULL);
    assert(is_pointer_valid(p3));
    assert(is_aligned(p3));

    // 验证分配后内存报告变化
    rpt = *dmem_get_use_report();
    printf("分配3个块后 - 已用块数: %d\n", rpt.used_count);
    assert(rpt.used_count == 3);

    // 释放内存
    printf("\n释放所有内存...\n");
    assert(dmem_free(p1) == 0);
    assert(dmem_free(p2) == 0);
    assert(dmem_free(p3) == 0);

    // 验证释放后内存报告
    rpt = *dmem_get_use_report();
    printf("释放后空闲: %d字节, 初始空闲: %d字节\n", rpt.free, rpt.initf);
    assert(rpt.used_count == 0);
    assert(rpt.free == rpt.initf); // 空闲内存应恢复到初始值

    print_mem_report("释放所有内存后");
    printf("===== [测试2通过] =====\n");
}

static void _test_boundary_conditions() {
    printf("\n===== [测试3: 边界条件测试] =====\n");
    
    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");
    
    // 测试分配0字节
    printf("\n测试分配0字节...\n");
    void *p0 = dmem_alloc(0);
    assert(p0 == NULL);
    print_mem_report("分配0字节后");
    
    // 测试释放NULL
    printf("\n测试释放NULL指针...\n");
    assert(dmem_free(NULL) == -1);
    print_mem_report("释放NULL后");
    
    // 测试重复释放
    printf("\n测试重复释放...\n");
    void *p1 = dmem_alloc(4);
    assert(dmem_free(p1) == 0);
    print_mem_report("第一次释放后");
    assert(dmem_free(p1) == -3); // 重复释放应返回错误
    print_mem_report("重复释放后");
    
    // 测试非法指针释放
    printf("\n测试非法指针释放...\n");
    char fake_ptr[10];
    assert(dmem_free(fake_ptr) == -2); // 无效指针
    print_mem_report("非法释放后");
    
    // 测试内存池耗尽
    printf("\n测试内存池耗尽...\n");
    void* ptrs[10];
    int i = 0;
    int block_size = 16; // 用户请求大小
    int real_block_size = get_real_alloc_size(block_size) + get_block_overhead();
    
    int fixed_overhead = get_fixed_overhead(); // 头尾块开销
    int available = sizeof(test_pool) - fixed_overhead; // 初始可用空间
    int max_blocks = 5; // 128/(8+16)=5个块
    
    printf("每个块实际开销: %d字节 (块头:%d + 用户数据:%d)\n", 
           real_block_size, get_block_overhead(), get_real_alloc_size(block_size));
    printf("理论最大块数: %d\n", max_blocks);
    
    // 尝试分配直到内存耗尽
    for (i = 0; i < 10; i++) {
        ptrs[i] = dmem_alloc(block_size);
        if (!ptrs[i]) {
            printf("分配块 %d 失败 (内存耗尽)\n", i+1);
            break;
        }
        printf("分配块 %d, 地址: %p\n", i+1, ptrs[i]);
    }
    printf("分配了 %d 个块后内存耗尽\n", i);
    
    print_mem_report("内存耗尽时");
    assert(i == max_blocks); // 应该分配了5个块
    
    // 释放一个块后再次分配
    printf("\n释放第一个块后重新分配...\n");
    assert(dmem_free(ptrs[0]) == 0);
    void *p_new = dmem_alloc(block_size);
    assert(p_new != NULL);
    printf("重新分配块, 地址: %p\n", p_new);
    assert(dmem_free(p_new) == 0);
    print_mem_report("重新分配后");
    
    // 清理剩余块
    printf("\n清理剩余块...\n");
    for (int j = 1; j < i; j++) {
        if (ptrs[j]) {
            dmem_free(ptrs[j]);
            printf("释放块 %d\n", j+1);
        }
    }
    print_mem_report("清理后");
    
    printf("===== [测试3通过] =====\n");
}

static void _test_realloc_behavior()
{
    printf("\n===== [测试4: REALLOC行为测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");

    // 基本realloc
    printf("\n测试realloc扩大内存...\n");
    void *p1 = dmem_alloc(8);
    memset(p1, 0xAA, 8);
    void *p2 = dmem_realloc(p1, 16);
    assert(p2 != NULL);

    // 验证数据迁移
    for (int i = 0; i < 8; i++)
    {
        assert(((char *)p2)[i] == (char)0xAA);
    }
    print_mem_report("realloc扩大后");
    
    // 缩小分配
    printf("\n测试realloc缩小内存...\n");
    void *p3 = dmem_realloc(p2, 4);
    assert(p3 != NULL);

    // 验证数据完整性
    for (int i = 0; i < 4; i++)
    {
        assert(((char *)p3)[i] == (char)0xAA);
    }
    print_mem_report("realloc缩小后");

    // realloc到0相当于free
    printf("\n测试realloc(0)相当于free...\n");
    void *p4 = dmem_realloc(p3, 0);
    assert(p4 == NULL);
    print_mem_report("realloc(0)后");

    // realloc NULL 应等同于 malloc
    printf("\n测试realloc(NULL)相当于malloc...\n");
    void *p7 = dmem_realloc(NULL, 8);
    assert(p7 != NULL);
    dmem_free(p7);
    print_mem_report("realloc(NULL)后");

    printf("===== [测试4通过] =====\n");
}

static void _test_fragmentation_handling()
{
    printf("\n===== [测试5: 碎片处理测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");

    int fixed_overhead = get_fixed_overhead();
    int block_overhead = get_block_overhead();
    int initial_free = sizeof(test_pool) - fixed_overhead;

    // 创建内存碎片
    int alloc_size = 24; // 用户请求大小
    int real_alloc_size = get_real_alloc_size(alloc_size);
    int block_total = block_overhead + real_alloc_size;

    printf("分配3个块, 每个块实际开销: %d字节\n", block_total);

    void *p1 = dmem_alloc(alloc_size);
    void *p2 = dmem_alloc(alloc_size);
    void *p3 = dmem_alloc(alloc_size);

    print_mem_report("分配3个块后");

    // 释放中间块
    printf("\n释放中间块创建碎片...\n");
    dmem_free(p2);
    print_mem_report("释放中间块后");

    // 尝试分配大块（应该失败）
    int big_alloc = 48; // 大于单个空闲块
    printf("\n尝试分配大块(%d字节)...\n", big_alloc);
    void *p4 = dmem_alloc(big_alloc);
    assert(p4 == NULL);
    printf("大块分配失败 (存在碎片)\n");
    print_mem_report("大块分配失败后");

    // 释放所有内存
    printf("\n释放所有块...\n");
    dmem_free(p1);
    dmem_free(p3);
    print_mem_report("释放所有块后");

    // 再次尝试分配大块（应该成功）
    printf("\n再次尝试分配大块(%d字节)...\n", big_alloc);
    void *p5 = dmem_alloc(big_alloc);
    assert(p5 != NULL);
    print_mem_report("大块分配成功");
    dmem_free(p5);

    printf("===== [测试5通过] =====\n");
}

static void _test_merge_behavior()
{
    printf("\n===== [测试6: 合并行为测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");

    int block_overhead = get_block_overhead();
    int alloc_size = 16;
    int real_alloc_size = get_real_alloc_size(alloc_size);
    int block_total = block_overhead + real_alloc_size;

    printf("分配3个块, 每个块实际开销: %d字节\n", block_total);

    // 分配三个连续块
    void *p1 = dmem_alloc(alloc_size);
    void *p2 = dmem_alloc(alloc_size);
    void *p3 = dmem_alloc(alloc_size);

    print_mem_report("分配3个块后");
    int free_after_alloc = dmem_get_use_report()->free;

    // 释放中间块 - 不应合并
    printf("\n释放中间块...\n");
    dmem_free(p2);
    print_mem_report("释放中间块后");

    // 验证空闲内存增加
    struct dmem_use_report rpt = *dmem_get_use_report();
    int expected_free = free_after_alloc + real_alloc_size;
    printf("预期空闲: %d, 实际空闲: %d\n", expected_free, rpt.free);
    assert(rpt.free == expected_free);

    // 释放第一块 - 应与中间空闲块合并
    printf("\n释放第一块...\n");
    dmem_free(p1);
    print_mem_report("释放第一块后");

    // 验证合并后空闲内存
    rpt = *dmem_get_use_report();
    expected_free += real_alloc_size + block_overhead; // 合并后节省一个块头
    printf("预期空闲(合并后): %d, 实际空闲: %d\n", expected_free, rpt.free);
    assert(rpt.free == expected_free);

    // 释放第三块 - 应合并所有空闲块
    printf("\n释放第三块...\n");
    dmem_free(p3);
    print_mem_report("释放第三块后");

    // 验证最终空闲内存
    rpt = *dmem_get_use_report();
    expected_free += real_alloc_size + block_overhead; // 再次合并节省一个块头
    printf("预期空闲(完全合并): %d, 实际空闲: %d\n", expected_free, rpt.free);
    assert(rpt.free == dmem_get_use_report()->initf);

    printf("===== [测试6通过] =====\n");
}

static void _test_calloc_behavior()
{
    printf("\n===== [测试7: CALLOC行为测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");

    // 分配并检查清零
    printf("\n分配并验证清零...\n");
    int *arr = dmem_calloc(4, sizeof(int));
    assert(arr != NULL);

    for (int i = 0; i < 4; i++)
    {
        assert(arr[i] == 0);
    }
    print_mem_report("calloc分配后");

    // 写入数据后释放
    printf("\n写入数据后释放...\n");
    for (int i = 0; i < 4; i++)
    {
        arr[i] = i + 1;
    }
    dmem_free(arr);
    print_mem_report("释放后");

    // 重新分配并检查是否清零
    printf("\n重新分配并验证清零...\n");
    arr = dmem_calloc(4, sizeof(int));
    for (int i = 0; i < 4; i++)
    {
        assert(arr[i] == 0);
    }
    dmem_free(arr);
    print_mem_report("再次释放后");

    printf("===== [测试7通过] =====\n");
}

static void _test_report_accuracy()
{
    printf("\n===== [测试8: 报告准确性测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    print_mem_report("初始状态");

    // 初始状态
    struct dmem_use_report rpt = *dmem_get_use_report();
    unsigned initial_free = rpt.free;
    assert(rpt.used_count == 0);

    // 分配后
    printf("\n分配第一个块...\n");
    void *p1 = dmem_alloc(16);
    rpt = *dmem_get_use_report();
    printf("已用块数: %d (预期: 1)\n", rpt.used_count);
    assert(rpt.used_count == 1);
    print_mem_report("分配第一个块后");

    // 再次分配
    printf("\n分配第二个块...\n");
    void *p2 = dmem_alloc(16);
    rpt = *dmem_get_use_report();
    printf("已用块数: %d (预期: 2)\n", rpt.used_count);
    assert(rpt.used_count == 2);
    print_mem_report("分配第二个块后");

    // 释放一个
    printf("\n释放第一个块...\n");
    dmem_free(p1);
    rpt = *dmem_get_use_report();
    printf("已用块数: %d (预期: 1)\n", rpt.used_count);
    assert(rpt.used_count == 1);
    print_mem_report("释放第一个块后");

    // 释放另一个
    printf("\n释放第二个块...\n");
    dmem_free(p2);
    rpt = *dmem_get_use_report();
    printf("已用块数: %d (预期: 0)\n", rpt.used_count);
    assert(rpt.used_count == 0);
    printf("空闲内存: %d (初始空闲: %d)\n", rpt.free, initial_free);
    assert(rpt.free == initial_free);
    print_mem_report("释放所有块后");

    printf("===== [测试8通过] =====\n");
}

static void _test_dmem_realloc_extra()
{
    printf("\n===== [测试9: dmem_realloc 补充测试] =====\n");

    // ===== 测试1：就地扩展 =====
    {
        dmem_init(test_pool, sizeof(test_pool));
        print_mem_report("初始状态");

        printf("\n[测试1] 就地扩展测试...\n");
        void* p1 = dmem_alloc(32);
        void* p2 = dmem_alloc(32); // 创建相邻块
        printf("分配 p1: %p (%d字节)\n", p1, 32);
        printf("分配 p2: %p (%d字节)\n", p2, 32);

        dmem_free(p2);  // 释放相邻块
        printf("释放 p2 创建空闲空间\n");

        printf("尝试扩展 p1 到 64字节...\n");
        void* p1_exp = dmem_realloc(p1, 64);
        assert(p1 == p1_exp);  // 应就地扩展
        printf("就地扩展成功! 地址保持不变: %p\n", p1);
        print_mem_report("就地扩展后");
    }

    // ===== 测试2：缩小内存数据完整性 =====
    {
        dmem_init(test_pool, sizeof(test_pool));
        print_mem_report("初始状态");

        printf("\n[测试2] 缩小内存数据完整性测试...\n");
        char* p3 = dmem_alloc(64);
        memset(p3, 0xAA, 64);  // 填充测试数据
        printf("分配 p3: %p, 填充数据(0xAA)\n", p3);
        
        printf("缩小 p3 到 32字节...\n");
        char* p3_shrink = dmem_realloc(p3, 32);
        assert(p3_shrink != NULL);
        printf("缩小后地址: %p\n", p3_shrink);
        
        // 验证前32字节数据完整
        for (int i = 0; i < 32; i++) 
            assert(p3_shrink[i] == (char)0xAA);

        printf("数据完整性验证通过!\n");
        print_mem_report("缩小内存后");
    }

    // ===== 测试3：内存不足时保留原指针 =====
    {
        dmem_init(test_pool, sizeof(test_pool));
        print_mem_report("初始状态");

        printf("\n[测试3] 内存不足时保留原指针...\n");
        // 填充内存池（保留最后一块）
        void* ptrs[5];
        int i = 0;
        for (; i < 4; i++) 
        {
            ptrs[i] = dmem_alloc(16);
            assert(ptrs[i] != NULL);
        }
        print_mem_report("填充内存池后");
        
        // 尝试扩展最后一块（应失败）
        printf("尝试扩展最后一块(16->48字节)...\n");
        void* last_ptr = ptrs[3];
        void* new_ptr = dmem_realloc(last_ptr, 48);
        
        assert(new_ptr == last_ptr);  // 应保留原指针
        printf("扩展失败，但保留原指针: %p\n", last_ptr);
        print_mem_report("扩展失败后状态");

        // 清理
        for (int j = 0; j < 4; j++)
            dmem_free(ptrs[j]);

        print_mem_report("清理后");
    }
    


    printf("===== [测试9通过] =====\n");
}

static void _test_stress_allocation()
{
    printf("\n===== [测试10: 压力测试] =====\n");

    dmem_init(test_pool, sizeof(test_pool));
    struct dmem_use_report initial_rpt = *dmem_get_use_report();
    print_mem_report("初始状态");

    srand((unsigned int) time(NULL));

    void *ptrs[20];
    int alloc_count = 0;
    int total_allocations = 0;
    int total_frees = 0;

    // 随机分配释放循环
    printf("\n开始随机分配/释放循环 (50次迭代)...\n");
    for (int i = 0; i < 50; i++)
    {
        if (rand() % 2 || alloc_count == 0)
        {
            // 分配随机大小 (4-32字节)
            size_t size = 4 + (rand() % 28);
            void *ptr = dmem_alloc(size);

            if (ptr)
            {
                // 写入模式数据
                memset(ptr, 0x55, size);
                ptrs[alloc_count++] = ptr;
                total_allocations++;
                printf("[%02d] 分配 %2zu 字节 -> %p, 是否内存对齐? %s\n", i + 1, size, ptr, IS_DMEM_VAR_ALIGNED(ptr, DMEM_DEFINE_ALIGN_SIZE) ? "是" : "否");
            }
            else
            {
                printf("[%02d] 分配 %2zu 字节失败\n", i + 1, size);
            }
        }
        else
        {
            // 释放随机块
            int idx = rand() % alloc_count;
            // 验证数据完整性
            assert(*(char *)ptrs[idx] == 0x55);
            dmem_free(ptrs[idx]);
            total_frees++;
            printf("[%02d] 释放块 %p\n", i + 1, ptrs[idx]);

            // 从数组中移除
            ptrs[idx] = ptrs[alloc_count - 1];
            alloc_count--;
        }
    }

    // 释放所有剩余块
    printf("\n释放所有剩余块 (%d个)...\n", alloc_count);
    for (int i = 0; i < alloc_count; i++)
    {
        dmem_free(ptrs[i]);
        total_frees++;
    }

    // 验证内存完全释放
    struct dmem_use_report final_rpt = *dmem_get_use_report();
    printf("\n分配统计: 总分配: %d, 总释放: %d\n", total_allocations, total_frees);
    printf("预期已用块数: 0, 实际已用块数: %d\n", final_rpt.used_count);
    printf("预期空闲: %d, 实际空闲: %d\n", initial_rpt.free, final_rpt.free);

    assert(final_rpt.used_count == 0);
    assert(final_rpt.free == initial_rpt.free);

    print_mem_report("压力测试后");
    printf("===== [测试10通过] =====\n");
}


void example_test(void)
{
    printf("\n===== 开始内存管理库测试 =====\n");
    printf("内存池大小: %zu字节\n", sizeof(test_pool));
    printf("内存对齐要求: %d字节\n", DMEM_DEFINE_ALIGN_SIZE);
    printf("最小分配大小: %d字节\n", DMEM_MIN_ALLOC_SIZE);
    printf("内存块头大小: %zu字节\n", sizeof(mem_block_t));
    printf("固定开销: %d字节 (头块+尾块)\n", get_fixed_overhead());
    printf("初始可用内存: %d字节\n\n",
           (int)sizeof(test_pool) - get_fixed_overhead());

    _test_initialization();              
    _test_basic_allocation();            
    _test_boundary_conditions();        
    _test_realloc_behavior();           
    _test_fragmentation_handling();      
    _test_merge_behavior();             
    _test_calloc_behavior();         
    _test_report_accuracy();        
    _test_dmem_realloc_extra();    
    _test_stress_allocation();        

    printf("\n===== 所有测试通过! =====\n");
}

int main(void)
{
    example_test();
    return 0;
}