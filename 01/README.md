# Hello, world!

## hello.c

C语言Hello, world!，用gdb查看函数调用栈的内存布局。

参考

- [Debugging with GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/)

## yield.c

程序通过yield主动让出CPU

参考

- [Nonlocal Gotos](https://gcc.gnu.org/onlinedocs/gcc/Nonlocal-Gotos.html)

## preempt.c

抢占式调度，通过定时器打断程序执行

- [Intel Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [40332: AMD64 Architecture Programmer's Manual](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/40332.pdf)
- [x86 Function Attributes](https://gcc.gnu.org/onlinedocs/gcc/x86-Function-Attributes.html)

## interval.c

定时器与信号

参考

- [timer_create](https://www.man7.org/linux/man-pages/man2/timer_create.2.html)
- [timer_settime](https://www.man7.org/linux/man-pages/man2/timer_settime.2.html)
- [signal](https://www.man7.org/linux/man-pages/man7/signal.7.html)

## mask.c

模拟中断屏蔽标识

## ld.c

程序从ELF加载

参考

- [ELF and ABI standards](https://refspecs.linuxfoundation.org/elf/index.html)
- [dlsym](https://www.man7.org/linux/man-pages/man3/dlsym.3.html)
