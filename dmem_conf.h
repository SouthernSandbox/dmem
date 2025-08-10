/**
 * @file dmem_conf.h
 * @author Southern Sandbox
 * @date 2024-05-12
 * 
 * @copyright Copyright (c) 2024
 */
#ifndef DMEM_CONF_H
#define DMEM_CONF_H

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

/**
 * @brief 启用调试追踪
 */
#define ENABLE_DMEM_TRACE       1
#if ENABLE_DMEM_TRACE == 1
    #define DMEM_LEVEL_ERROR    "\033[31;1m"
    #define DMEM_LEVEL_WARNING  "\033[33;1m"
    #define DMEM_LEVEL_INFO     "\033[32;1m"
    #define DMEM_LEVEL_DEBUG    "\033[36;1m"

    #include "stdio.h"
    #define dmem_trace(level, fmt, ...)    printf(level "%s:%d :" fmt "\033[0m\r\n", __func__, __LINE__, ## __VA_ARGS__)
#else
    #define dmem_trace(...) 
#endif

/**
 * @brief 是否保留 dmem_get_use_report() 接口
 * @warning dmem_get_use_report() 属于兼容性保留，不建议频繁使用，建议使用 dmem_read_use_report() 接口
 */
#define ENABLE_DMEM_GET_USER_REPORT_API     1

/**
 * @brief 默认最小内存分配大小，单位字节
 * @warning 请谨慎修改，在32位平台，最小内存分配大小应当是 4 的整数倍
 */
#define DMEM_MULTI_4(n)             ((n) << 2)                  // 结果为 4 的整数 n 倍
#define DMEM_DEFINE_ALIGN_SIZE      DMEM_MULTI_4(1)             // 默认内存对齐大小为 4 字节, 即 DMEM_MULTI_4(1)
#define DMEM_MIN_ALLOC_SIZE         DMEM_DEFINE_ALIGN_SIZE      // 以默认内存对齐大小为最小内存分配大小

/**
 * @brief 内存对齐检查
 */
#define IS_DMEM_VAR_ALIGNED(var, n)     ((((uintmax_t)(var)) & (DMEM_DEFINE_ALIGN_SIZE - 1)) == 0)

/**
 * @brief 内存对齐
 * @warning 请谨慎填入对齐字节数，内存对齐方式建议为 4 的整数倍   
 * @note 为确保内存对齐，建议用户使用以下方式在 C 中定义内存池：
 *        - DMEM_ALIGNED(var, n)        其中 var 为变量名，n 为对齐字节数。
 *        - DMEM_DEFAULT_ALIGNED(var)   其中 var 为变量名，使用默认对齐方式（4 字节）。
 * @example
 *        -  DMEM_ALIGNED(char mem_pool[128], 4);
 *        -  DMEM_DEFAULT_ALIGNED(static char mem_pool[128] = {0});
 */
#if (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L) || (defined(__cplusplus) && __cplusplus >= 201103L)
    // 标准对齐关键字（跨编译器通用）
    #define DMEM_ALIGNED(var, n)            _Alignas(n) var
    // C语言需要包含标准头文件（C++无需额外头文件）
    #ifdef __STDC_VERSION__
        #include "stdalign.h"
    #endif
#else
    // 旧编译器兼容（保留原有编译器特定扩展）
    #if defined(__GNUC__) || defined(__clang__)
        // GCC/Clang（支持STM32等多数平台）
        #define DMEM_ALIGNED(var, n)        __attribute__((aligned(n))) var 
    #elif defined(__CC_ARM) || defined(__CC_ARM)
        // ARMCC/Keil MDK
        #define DMEM_ALIGNED(var, n)        __align(n) var 
    #elif defined(_MSC_VER)
        // MSVC（Windows平台）
        #define DMEM_ALIGNED(var, n)        __declspec(align(n)) var 
    #else
        // 未知编译器，用户需自行依据平台特性定义对齐方式
        #warning "Unknown compiler, alignment may not work correctly"
        #define DMEM_ALIGNED(var, n)        var     
    #endif
#endif

// 设置默认对齐方式
#ifndef DMEM_DEFAULT_ALIGNED
    #define DMEM_DEFAULT_ALIGNED(var)       DMEM_ALIGNED(var, DMEM_DEFINE_ALIGN_SIZE)
#endif




#ifdef __cplusplus
}
#endif  // __cplusplus

#endif  // DMEM_CONF_H
