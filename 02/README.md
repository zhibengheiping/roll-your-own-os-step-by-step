# 从文件加载程序

## initrd.c

从tar文件加载程序

参考

- [Basic Tar Format](https://www.gnu.org/software/tar/manual/html_node/Standard.html)
- [ELF and ABI standards](https://refspecs.linuxfoundation.org/elf/index.html)
- [mmap](https://www.man7.org/linux/man-pages/man2/mmap.2.html)

## adspc.c

程序运行在不同的地址空间

- [mremap](https://www.man7.org/linux/man-pages/man2/mremap.2.html)
- [atomic_load](https://en.cppreference.com/w/c/atomic/atomic_load)

## user.c

系统调用

- [Syscall User Dispatch](https://docs.kernel.org/admin-guide/syscall-user-dispatch.html)
