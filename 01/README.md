# Hello, world!

## hello.c

C语言Hello, world!

用串口来实现write

参考

- [UEFI Specification](https://uefi.org/specs/UEFI/2.11/index.html)
- [Serial Ports](https://wiki.osdev.org/Serial_Ports)
- [Linux/x86 Boot Protocol](https://www.kernel.org/doc/html/latest/arch/x86/boot.html)

## stack.c

程序的运行状态主要是存在栈上的。

用gdb查看函数调用栈的内存布局。

参考

- [Debugging with GDB](https://sourceware.org/gdb/current/onlinedocs/gdb.html/)

## yield.c

yield使用__builtin_setjmp, __builtin_longjmp切栈。

程序通过yield主动让出CPU。

参考

- [Nonlocal Gotos](https://gcc.gnu.org/onlinedocs/gcc/Nonlocal-Gotos.html)

## timer.c

定时器

参考

- [timer_create](https://www.man7.org/linux/man-pages/man2/timer_create.2.html)
- [timer_settime](https://www.man7.org/linux/man-pages/man2/timer_settime.2.html)
- [8254 PROGRAMMABLE INTERVAL TIMER](https://www.scs.stanford.edu/10wi-cs140/pintos/specs/8254.pdf)
- [Programmable Interval Timer](https://wiki.osdev.org/Programmable_Interval_Timer)
- [signal](https://www.man7.org/linux/man-pages/man7/signal.7.html)
- [Intel Software Developer's Manual](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [40332: AMD64 Architecture Programmer's Manual](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/40332.pdf)
- [x86 Function Attributes](https://gcc.gnu.org/onlinedocs/gcc/x86-Function-Attributes.html)

## preempt.c

抢占式调度，通过定时器打断程序执行
