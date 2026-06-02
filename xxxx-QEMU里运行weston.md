# QEMU里运行weston

我们使用Bochs VGA，需要内核模块bochs

```
./mkinitrd.py -m 9p 9pnet_virtio overlay bochs
```

QEMU默认内存是128M，weston需要的比较多。

```
./runqemu.py -m 256 --vga
```

weston使用drm后端时，依赖udevd和seatd

```
/usr/lib/systemd/systemd-udevd -d
udevadm trigger
seatd-launch -- weston
```
