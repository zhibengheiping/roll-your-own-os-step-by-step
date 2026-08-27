# 运行Weston

为了避免自己写完整的窗口管理逻辑，我们让Weston使用wayland后端

运行我们自制的Wayland Server

```
./nested-wayland.elf
```

指定wayland-1作为Wayland显示服务，可以看到Weston正常显示

```
WAYLAND_DISPLAY=wayland-1 weston -B wayland --renderer=pixman
```
