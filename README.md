# 一、简介
dmem (dynamic memory) 是一个简易的动态内存管理库，适用于单片机等嵌入式平台。

# 二、文件结构
- `dmem.h` dmem 库头文件
- `dmem_conf.h` dmem 配置头文件
- `dmem.c` dmem 核心功能实现源文件
- `dmem_porting.c` dmem 可移植接口源文件
- `test.c` 测试示例

# 三、V2.0更新变化
dmem V2.0 相较于 V1.x 有了非常大的变化，解决不少BUG，并补充了此前未有加入的内存对齐检查，具体更多变化如下:
## 3.0 重要变化
1. 当前不支持 8051 平台和 C89 标准, 主要面向 32位平台 和 C99 以上标准;
2. 各个库函数补充有关内存对齐的检查, dmem_init() 强制要求内存池地址符合内存对齐, 同时库其他接口的内存分配都会强制进行内存对齐和内存矫正;
3. dmem_realloc() 补充了更多的功能, 使其更加贴合 C 标准的 realloc(), 同时优化内存分配的实现;
4. 在 dmem_conf.h 中添加了更多的宏开关以及和内存对齐有关的宏定义;
5. 内存使用报告推荐使用新加入接口 dmem_read_use_report(), 但原接口 dmem_get_use_report() 仍予以保留;
6. 新增函数错误码的宏定义;
7. 修改部分注释, 优化部分内部函数实现.

## 3.1 dmem_conf.h
1. 新增宏开关 `ENABLE_DMEM_TRACE` , 用于调试追踪程序的运行;
2. 新增宏开关 `ENABLE_DMEM_GET_USER_REPORT_API` , 由用户决定是否保留原有接口 dmem_read_use_report() 及其辅助宏定义;
3. 新增内存对齐宏:
   - `DMEM_MULTI_4` 得到结果为 4 的倍数的值;
   - `DMEM_DEFINE_ALIGN_SIZE` 库使用此宏作为默认对齐宏定义, 默认内存对齐大小为 4 字节, 即 DMEM_MULTI_4(1);
   - `DMEM_MIN_ALLOC_SIZE` 库将以此宏作为最小内存分配大小.
4. 新增 `IS_DMEM_VAR_ALIGNED` 宏, 用以检查变量地址是否符合内存对齐, 以 DMEM_DEFINE_ALIGN_SIZE 作为内存对齐参考标准;
5. 新增 `DMEM_ALIGNED` 和 `DMEM_DEFAULT_ALIGNED`, 用以辅助定义符合默认内存对齐的变量.

## 3.2 dmem.h
1. 修改 bool 的定义, 改用 C 标准实现;
2. 新增函数错误码的宏定义;
3. 新增接口 `dmem_read_use_report()` 以替代原有的 `dmem_get_use_report()`;
4. 将原 dmem_get_use_report() 及其辅助宏函数交由 ENABLE_DMEM_GET_USER_REPORT_API 管理.

## 3.3 dmem.c
1. 新增 `_merge_free_blocks()`, `_split()`, `_expand_inplace()` 用于辅助实现库功能;
2. 新增 `dmem_read_use_report()` 接口实现;
3. 重写 `dmem_realloc()` 的实现, 以支持以下功能:
   - 支持 dmem_realloc(NULL, size), 相当于 dmem_alloc(size);
   - 支持 dmem_realloc(p, 0), 相当于 dmem_free(p);
   - 支持内存就地扩展功能, 在分配更大内存时优先就地扩展而非另外开辟新的内存空间;
   - 支持内存收缩, 在分配更小内存时优先就地缩小当前内存, 并依据情况将多出的空间作为空闲内存;
4. 增加库函数追踪日志;
5. 不再支持 C89 标准, 支持 C99 标准;
6. 添加内存对齐检查与对齐矫正功能;
7. dmem_init() 现在必须要求内存池首地址需字节对齐, 否则会拒绝初始化;
8. 修改部分函数的实现.


# 四、使用方法
## 4.1 初始化
调用 dmem_init() 对指定内存池进行初始化，这片将作为堆区使用。
```c
#include "dmem.h"

DMEM_DEFAULT_ALIGNED(static char mempool[1024] = {0});        // 可使用 dmem 自带的内存对齐辅助定义宏, 也可用户自行实现内存对齐.

int main(void)
{
    //...
    dmem_init(mempool, sizeof(mempool));        // mempool 必须满足内存对齐, size 若不满足对齐则将向下对齐予以矫正.
    //...
}
```

## 4.2 申请内存
调用 dmem_alloc()/dmem_realloc()/dmem_calloc() 即可申请内存，使用方法与标准 C 库的 malloc()/realloc()/calloc() 一致。注意: 若分配的内存大小不符合内存对齐标准, 将强制分配向上内存对齐的内存大小矫正值, 如 127 字节, 实际可能分配 128 字节空间.
```c
void* p = NULL;
p = dmem_alloc(sizeof(int) * 3);
p = dmem_realloc(p, 1024);
p = dmem_calloc(3, sizeof(int)); 
```
## 4.3 释放内存
不同于标准 C 库的 free()，dmem 的 dmem_free() 带有返回值，用户可以依据返回值查看内存释放的结果。
```c
void* p = dmem_alloc(1024);
switch(dmem_free(p))
{
case DMEM_ERR_NONE:         printf("释放成功"); break;
case DMEM_FREE_NULL:        printf("输入的地址为 NULL"); break;
case DMEM_FREE_INVALID_MEM: printf("无法查询到该内存的内存块信息，内存可能被非法修改"); break;
case DMEM_FREE_REPEATED:    printf("无法释放已释放的内存"); break;
}
```
## 4.4 内存使用报告
优先使用 dmem_read_use_report() 获取当前内存的使用情况，便于用户更好地优化内存的使用。
```c
struct dmem_use_report report;
dmem_read_use_report(&report);
printf("free: %d\r\n", report.free);
printf("max usage: %d\r\n", report.max_usage);
printf("leak: %d\r\n", report.used_count);
```
也可以调用 dmem_get_use_report().
```c
struct dmem_use_report* report = dmem_get_use_report();
printf("free: %d\r\n", report->free);
printf("max usage: %d\r\n", report->max_usage);
printf("leak: %d\r\n", report->used_count);
```
## 4.5 线程安全
在 dmem_porting.c 中，提供了适用于多线程环境的线程锁接口函数，需依据实际使用的 RTOS 来实现线程安全的功能。
```c
int dmem_get_lock(void)
{
    return 0;
}

int dmem_rel_lock(void)
{
    return 0;
}
```
# 五、如何选定堆区？
一般来说，堆区可以通过创建一个静态数组来实现，例如：
```c
__attribute__((align(4))) static char mempool[1024];
dmem_init(mempool, sizeof(mempool));
```
也可以通过修改链接脚本来实现，例如：
```c
/** 忽略无关内容 **/
MEMORY
{
  FLASH (rx) : ORIGIN = 0x00000000, LENGTH = 448K
  RAM (xrw) : ORIGIN = 0x20000000, LENGTH = 32K
}
__stack_size = 2048;


SECTIONS
{
    /** 忽略无关内容 **/

    /** 栈区：官方仅提供了栈尾的位置（默认为 RAM 尾，即 RAM 剩余没有使用的部分均为栈），
     * 因此栈的大小需要用户自己设置。
     */
    .stack :
    {
      . = ALIGN(4);
      PROVIDE(_susrstack = . );		
      . = . + __stack_size;		/** 定位计数器偏移，其中的空间将用作栈区 **/
      . = ALIGN(4);
      PROVIDE(_eusrstack = . );	/** 栈尾，该变量将用于 startup_CH583.s 中，指定栈的尾部 **/
    } >RAM  
  
    /** 将剩余的 RAM 空间用于 dmem 的堆区 **/
    .dmem_heap :
    {
      . = ALIGN(4);
      PROVIDE(__dmem_start = .);
    } >RAM
}
```
在上面链接脚本中，我们指定了硬件栈区大小为 2048，并在栈区之后创建了堆区。
将定位计数器的值存储到变量 __dmem_start 中，此时 __dmem_start 的值就是现在堆区的起始地址。
回到 c 文件中，我们只需要声明 __dmem_start 变量，即可作为堆区的起始地址，堆区的大小则需要通过实际 RAM 的大小进行计算。
```c
extern int __dmem_start;

#define RAM_START_ADDR      (0x20000000)
#define RAM_SIZE            (32 * 1024)
#define HEAP_END_ADDR       (RAM_START_ADDR + RAM_SIZE)
#define HEAP_BEGIN          ((void*)&__dmem_start)
#define HEAP_SIZE           (int)(HEAP_END_ADDR - (int)HEAP_BEGIN)

int main(void)
{
    // 忽略无关代码
    dmem_init(HEAP_BEGIN, HEAP_SIZE);

    return 0;
}
```
通过此操作建立的堆区无需手动调整堆区的大小，因为此时整个未使用的 RAM 都将作为堆区。










