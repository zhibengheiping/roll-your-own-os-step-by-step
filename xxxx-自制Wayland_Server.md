# 自制Wayland Server

为便于演示，我们假设只接收单个客户端连接。这样一来，连接建立后即可立即关闭监听socket，并在连接退出时直接结束进程。同时，假设系统仅存在单一窗口，且该窗口恰好占满整个屏幕。

运行我们自制的Wayland Server

```
./wayland-wayland.elf
```

指定wayland-1作为Wayland显示服务，可以看到此前通过共享内存绘制的时钟画面一切正常：

```
WAYLAND_DISPLAY=wayland-1 ./clock-shm.elf
```

使用EGL绘制的时钟同样能够正常显示：

```
WAYLAND_DISPLAY=wayland-1 ./clock-egl.elf
```

不过，从输出信息中可以发现，OpenGL 实际是通过 CPU 进行软件渲染的：

```
EGL Version 1.5
OpenGL Version: 4.5 (Core Profile) Mesa 25.3.6
OpenGL Shading Language Version: 4.50
OpenGL Vendor: Mesa
OpenGL Renderer: llvmpipe (LLVM 21.1.8, 256 bits)
```
