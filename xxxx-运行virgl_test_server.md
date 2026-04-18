# 运行virgl test server

通过设置环境变量`LIBGL_ALWAYS_SOFTWARE=true`，可以强制使用llvmpipe进行软件渲染：

```
EGL Version 1.5
OpenGL Version: 4.5 (Core Profile) Mesa 25.3.6
OpenGL Shading Language Version: 4.50
OpenGL Vendor: Mesa
OpenGL Renderer: llvmpipe (LLVM 21.1.8, 256 bits)
```

启动`virgl_test_server`后，在运行OpenGL程序时指定以下环境变量：

```
LIBGL_ALWAYS_SOFTWARE=true GALLIUM_DRIVER=virpipe
```

此时可观察到类似这样的输出信息：

```
EGL Version 1.5
OpenGL Version: 4.3 (Core Profile) Mesa 25.3.6
OpenGL Shading Language Version: 4.30
OpenGL Vendor: Mesa
OpenGL Renderer: virgl (...)
```

`virgl_test_server`也支持自定义socket路径，例如：

```
virgl_test_server --socket-path=/tmp/virgl.sock
```

相应地，OpenGL程序的环境变量应设置为：

```
LIBGL_ALWAYS_SOFTWARE=true GALLIUM_DRIVER=virpipe VTEST_SOCKET_NAME=/tmp/virgl.sock
```

由于我们自制的Wayland Server并未实现Linux DMA‑BUF机制，当指定`wayland-1`作为 Wayland显示服务时，无需添加`LIBGL_ALWAYS_SOFTWARE=true`：

```
WAYLAND_DISPLAY=wayland-1 GALLIUM_DRIVER=virpipe VTEST_SOCKET_NAME=/tmp/virgl.sock
```
