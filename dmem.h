/**
 * @file dmem.h
 * @author Southern Sandbox
 * @date 2024-05-12
 * @copyright Copyright (c) 2024
 * 
 * @details
 *      1. dmem (dynamic memory manager)，是一种简易的动态内存管理器
 *      2. dmem 的内存管理参考了 rt thread 的小内存管理算法
 *      3. 提供 内存使用报告 函数，可以帮助用户更好地了解内存的使用情况
 *      4. 实际最小分配内存大小为在 dmem_conf.h 中定义，任何小于该值的内存分配请求都会以最小分配内存大小进行分配
 * 
 * 版本修改记录
 * @date 2024.05.12     @version 1.0        @note   第一个版本
 * @date 2024.05.18     @version 1.1        @note   1. 修改了 dmem_realloc() 的实现
 *                                                  2. 修改了部分局部变量的声明位置，使其兼容 C89 标准
 *                                                  3. 修改了部分注释的表达，并添加了新的注释
 *                                                  4. 为 _free() 和 dmem_free() 添加了返回值
 *                                                  5. 增加对各个函数对输入参数合法性的检查
 *                                                  6. 其他不影响算法的修改
 * @date 2025.04.16     @version 1.2        @note   1. 修复了 dmem_realloc() 重新分配内存时不转存旧内存数据的问题
 *                                                  2. 修改了部分注释的表达
 *                                                  3. 修改了 _alloc() 的的一个变量名称和声明位置，使其更符合 C89 标准
 * @date 2025.08.10     @version 2.0        @note   1. 在 dmem_conf.h 中提供了辅助内存对齐宏定义、调试开关、调试信息追踪、最小内存分配大小设置等
 *                                                  2. 在 dmem.c 中加入了内存对齐检查，具体有：
 *                                                      2.1 强制要求 dmem_init() 传入的内存池地址必须符合内存对齐要求，
 *                                                          用户可通过 DMEM_ALIGNED() 宏对内存池进行内存对齐；
 *                                                      2.2 强制检查内存池大小是否符合内存对齐，若不符合则自动调整为符合内存对齐的大小（如 127 -> 124）；
 *                                                      2.3 强制检查内存分配大小是否符合内存对齐，若不符合则自动调整为符合内存对齐的大小（如 127 -> 128）。
 *                                                      2.4 库使用 DMEM_DEFINE_ALIGN_SIZE 作为默认内存对齐大小，用户可通过修改 DMEM_DEFINE_ALIGN_SIZE 来修改默认内存对齐大小。
 *                                                  3. dmem_realloc() 函数实现修改
 *                                                      3.1 dmem_realloc() 补充支持 dmem_realloc(NULL, size) 和 dmem_realloc(p, 0) 功能；
 *                                                      3.2 dmem_realloc() 解决了重新分配更小内存时仍占用多余内存的问题，当多余的内存适合释放时，会自动转变为空闲内存。
 *                                                  4. 调整部分注释
 *                                                  5. 新增可重入获取内存使用报告的接口 dmem_read_use_report()，
 *                                                     保留旧版接口 dmem_get_use_report()，并在 dmem_config.h 中提供了开关，由用户自行选择是否使用API及其宏函数。
 *                                                  6. 修改如 unsigned char/short/int 等类型，统一使用标准 C 提供的 uint8/16/32_t 类型
 *                                                  7. 为接口加入了返回值错误宏定义
 *                                                  8. 不再兼容 C89 标准和 8051 单片机，已支持面向 C99 以上标准和32位单片机平台。
 *                                                  9. 其他不影响使用的修改
 */
#ifndef DMEM_H
#define DMEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "string.h"
#include "stdint.h"
#include "stddef.h"
#include "dmem_conf.h"

#if __STDC_VERSION__ >= 199901L && __STDC_VERSION__ < 202311L
    #include "stdbool.h" /* C99 到 C23 之间使用标准库 */
#endif

/**
 * @brief 版本
 */
#define DMEM_MAIN_VER       2
#define DMEM_SUB_VER        0
#define DMEM_UPDATE_STR     "2025.08.10"

/**
 * @brief 函数错误码
 */
#define DMEM_ERR_NONE               (0)       // 无错误
#define DMEM_INIT_POOL_NULL         (-1)      // 内存池指针为空
#define DMEM_INIT_SIZE_SMALL        (-2)      // 内存池大小过小
#define DMEM_INIT_POOL_ALIGN        (-3)      // 内存池地址未对齐
#define DMEM_FREE_NULL              (-1)      // 内存地址为空
#define DMEM_FREE_INVALID_MEM       (-2)      // 无效的内存地址
#define DMEM_FREE_REPEATED          (-3)      // 重复释放内存


/**
 * @brief 内存使用报告结构体
 */
struct dmem_use_report
{
    uint32_t free;              /** 当前空闲内存的总大小（即使是不连续的空闲内存块也会纳入统计），单位：字节 **/
    uint32_t max_usage;         /** 内存的最大消耗量（包含内存块信息头的占用），单位：字节 **/
    uint32_t initf;             /** 初始化时空闲内存的大小，单位：字节 **/
    uint32_t used_count;        /** 当前尚未释放的内存块数量 **/
};

int dmem_init(void* pool, unsigned int size);
void* dmem_alloc(unsigned int size);
void* dmem_realloc(void* old_mem, unsigned int new_size);
void* dmem_calloc(unsigned int count, unsigned int size);
int dmem_free(void* mem);
void dmem_read_use_report(struct dmem_use_report* result);

#if ENABLE_DMEM_GET_USER_REPORT_API
    const struct dmem_use_report* dmem_get_use_report(void);
    #define dmem_get_free()         (dmem_get_use_report()->free)
    #define dmem_get_max_usage()    (dmem_get_use_report()->max_usage)
    #define dmem_get_initf()        (dmem_get_use_report()->initf)
    #define dmem_get_used_count()   (dmem_get_use_report()->used_count)
#endif

#ifdef __cplusplus
}
#endif

#endif
