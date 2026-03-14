# EDU

再尝试vfio-user，最后改进edu输出格式

## rootfs与initrd

因为rootfs会反复只用，建在上层目录

若在toolbox里构建rootfs会遇到问题，因为toolbox里`/etc/subuid`, `/etc/subgid`这两个文件为空。需要自行设置，而且命令没有必要的cap。

```
sudo setcap cap_setuid+ep /usr/bin/newuidmap
sudo setcap cap_setgid+ep /usr/bin/newgidmap
```

因为有些包依赖关系似乎没写对，得分两步安装先装fedora-release，再装别的

```
unshare -U -m --map-users 0:1000:64537 --map-groups 0:1000:64537 --map-user=0 --map-group=0 /bin/bash

dnf --installroot="$(pwd)/rootfs" --use-host-config install --releasever=43 --setopt=install_weak_deps=False --setopt=tsflags=noscripts fedora-release

dnf --installroot="$(pwd)/rootfs" install -C --setopt=install_weak_deps=False --setopt=tsflags=noscripts --setopt=keepcache=1 busybox kernel

chroot "$(pwd)/rootfs" depmod $(rpm --root="$(pwd)/rootfs" -q kernel --qf "%{VERSION}-%{RELEASE}.%{ARCH}\n" | head -n 1)
```

创建initrd

```
../mkinitrd.py 9p 9pnet_virtio overlay vfio-pci vfio_iommu_type1
```

虚拟机只读挂载宿主机目录，避免污染宿主机

## VFIO

## VFIO-user
