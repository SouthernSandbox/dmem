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
 *      4. 实际最小分配内存大小为 12，例如，若尝试分配 4 字节的内存，实际将分配 12 字节
 * 
 * 版本修改记录
 * @date 2024.05.12     @version 1.0        @note   第一个版本
 * @date 2024.05.18     @version 1.1        @note   1. 修改了 dmem_realloc() 的实现
 *                                                  2. 修改了部分局部变量的声明位置，使其兼容 C89 标准
 *                                                  3. 修改了部分注释的表达，并添加了新的注释
 *                                                  4. 为 _free() 和 dmem_free() 添加了返回值
 *                                                  5. 增加对各个函数对输入参数合法性的检查
 *                                                  6. 其他不影响算法的修改
 */
#ifndef DMEM_H
#define DMEM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "string.h"
#include "dmem_conf.h"

/**
 * @brief 版本
 */
#define DMEM_MAIN_VER       1
#define DMEM_SUB_VER        1
#define DMEM_UPDATE_STR     "2024.05.18"

/**
 * @brief bool 定义
 */
#ifndef bool
    #define bool _Bool
#endif
#ifndef false
    #define false (0)
#endif
#ifndef true
    #define true (1)
#endif

/**
 * @brief 内存使用报告结构体
 */
struct dmem_use_report
{
    unsigned int free;              /** 当前空闲内存大小 **/
    unsigned int max_usage;         /** 该函数记录当前内存池总的内存消耗） **/
    unsigned int initf;             /** 初始化时空闲内存的大小 **/
    unsigned int used_count;        /** 当前尚未释放的内存块数量 **/
};
#define dmem_get_free()         (dmem_get_use_report()->free)
#define dmem_get_max_usage()    (dmem_get_use_report()->max_usage)
#define dmem_get_initf()        (dmem_get_use_report()->initf)
#define dmem_get_used_count()   (dmem_get_use_report()->used_count)


int dmem_init(void* pool, unsigned int size);
void* dmem_alloc(unsigned int size);
void* dmem_realloc(void* old_mem, unsigned int new_size);
void* dmem_calloc(unsigned int count, unsigned int size);
int dmem_free(void* mem);
struct dmem_use_report* dmem_get_use_report(void);

#ifdef __cplusplus
}
#endif

#endif