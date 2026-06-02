# 自制virgl_test_server

```
./mkinitrd.py -m 9p 9pnet_virtio overlay bochs vfio-pci vfio_iommu_type1
```

在weston里开一个终端

```
./virgl-virtio.elf
```

在weston里再开一个终端

```
GALLIUM_DRIVER=virpipe ./clock-egl.elf
```
