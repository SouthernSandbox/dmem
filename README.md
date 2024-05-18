# 一、简介
dmem (dynamic memory) 是一个简易的动态内存管理库，适用于单片机等嵌入式平台。

# 二、使用方法
## 2.1 初始化
调用 dmem_init() 对指定内存池进行初始化，这片将作为堆区使用。
```c
#include "dmem.h"

static char mempool[1024] = {0};

int main(void)
{
    //...
    dmem_init(mempool, sizeof(mempool));
    //...
}
```

## 2.2 申请内存
调用 dmem_alloc()/dmem_realloc()/dmem_calloc() 即可申请内存，使用方法与标准 C 库的 malloc()/realloc()/calloc() 一致。
```c
void* p = NULL;
p = dmem_alloc(sizeof(int) * 3);
p = dmem_realloc(p, 1024);
p = dmem_calloc(3, sizeof(int)); 
```
## 2.3 释放内存
不同于标准 C 库的 free()，dmem 的 dmem_free() 带有返回值，用户可以依据返回值查看内存释放的结果。
```c
void* p = dmem_alloc(1024);
switch(dmem_free(p))
{
case 0:   printf("释放成功"); break;
case -1:  printf("输入的地址为 NULL"); break;
case -2:  printf("无法查询到该内存的内存块信息，内存可能被非法修改"); break;
case -3:  printf("无法释放已释放的内存"); break;
}
```
## 2.4 内存使用报告
可以调用 dmem_get_use_report() 获取当前内存的使用情况，便于用户更好地优化内存的使用。
```c
struct dmem_use_report* report = dmem_get_use_report();
printf("free: %d\r\n", report->free);
printf("max usage: %d\r\n", report->max_usage);
printf("leak: %d\r\n", report->used_count);
```
## 2.5 线程安全
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
# 三、如何选定堆区？
一般来说，堆区可以通过创建一个静态数组来实现，例如：
```c
static char mempool[1024];
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










