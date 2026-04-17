# 调用栈

为了简化演示代码，我们没有额外添加错误处理逻辑，而是直接使用了assert。以assert.c为例，其代码如下：

```c
#include <assert.h>

void
fire(void) {
  assert(0);
}

int
main(void) {
  fire();
  return 1;
}
```

将上述代码编译为assert.elf并运行，会输出如下报错信息：

```
assert.elf: assert.c:5: fire: Assertion `0' failed.
Aborted                    (core dumped) ./assert.elf
```

不过，这种方式的不足之处在于：错误信息仅指出了触发断言的具体行号，在复杂调用场景下并不利于快速定位问题的根源。

为此，我们特意将一个Ada空包编译为stacktrace.so，利用Ada运行时自带的异常处理机制来捕获并输出调用栈信息。执行以下命令：

```
LD_PRELOAD=./stacktrace.so ./assert.elf
```

即可看到比较直观的调用栈回溯信息

```
assert.elf: assert.c:5: fire: Assertion `0' failed.

raised PROGRAM_ERROR : unhandled signal
Load address: 0x5584c5df6000
[/lib64/libgnat-15.so]
0x7f50227113df
[/lib64/libc.so.6]
0x7f502298e28e
0x7f50229e83ca
0x7f502298e15c
0x7f50229756ce
0x7f5022975637
[./assert.elf]
0x5584c5df64a0 fire at assert.c:5
0x5584c5df64a9 main at assert.c:10
[/lib64/libc.so.6]
0x7f50229775b3
0x7f5022977666
[./assert.elf]
0x5584c5df63b3 _start at ???
0xfffffffffffffffe
```
