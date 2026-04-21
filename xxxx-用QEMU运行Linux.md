# 用QEMU运行Linux

安装QEMU

```
dnf install --setopt=install_weak_deps=False qemu-system-x86-core
```

QEMU启动Linux需要kernel和initrd两个文件。由于容器环境中的Fedora缺少内核文件，为统一环境，我们可以自行构建一个Fedora的rootfs。受容器权限限制，这里需要借助fakechroot和fakeroot，先安装这两个工具：

```
dnf install --setopt=install_weak_deps=False fakechroot fakeroot
```

导入GPG公钥

```
rpm --root="$(pwd)/rootfs" --import /etc/pki/rpm-gpg/RPM-GPG-KEY-fedora-43-primary
```

Fedora的软件源分为`fedora.repo`与`fedora-updates.repo`。其中`fedora.repo`在大版本发布后内容即固定不变，因此若网络条件不佳，可以直接将宿主机上已有的`fedora.repo`缓存复制到目标rootfs中，后续dnf操作即可跳过包信息下载。

```
mkdir -p rootfs/var/cache/libdnf5
cp -r /var/cache/libdnf5/fedora-fe4b228be7d3c150 rootfs/var/cache/libdnf5
```

`fedora-release`是Fedora系统的基础包，软件包在打包时都假定它已存在，因此不会显式声明依赖。但在一个完全空白的rootfs环境中，若将普通包与`fedora-release`一同安装，由于缺少依赖关系约束，DNF可能将普通包排在`fedora-release`之前安装，而普通包又依赖`fedora-release`提供的环境，从而导致事务失败。因此我们先将`fedora-release`单独安装，确保基础环境就绪：

```
fakechroot fakeroot -- dnf --installroot="$(pwd)/rootfs" --disable-repo=fedora-cisco-openh264,updates --use-host-config install --releasever=43 --setopt=install_weak_deps=False --setopt=tsflags=noscripts fedora-release
```

initrd中需要一个`init`程序来引导系统。这里会用`busybox`来实现init ，这里就和内核包一起安装了：

```
fakechroot fakeroot -- dnf --installroot="$(pwd)/rootfs" --disable-repo=fedora-cisco-openh264,updates install --setopt=install_weak_deps=False --setopt=tsflags=noscripts kernel busybox
```

因为前面跳过了脚本执行，需要自己运行depmod生成modules.dep

```
fakechroot fakeroot -- chroot "$(pwd)/rootfs" depmod $(rpm --root="$(pwd)/rootfs" -q kernel --qf "%{VERSION}-%{RELEASE}.%{ARCH}\n" | head -n 1)
```

这样我们就能用modinfo来看模块的依赖关系了

```
$ fakechroot fakeroot -- chroot "$(pwd)/rootfs" modinfo -k $(rpm --root="$(pwd)/rootfs" -q kernel --qf "%{VERSION}-%{RELEASE}.%{ARCH}\n" | head -n 1) -F depends 9p
netfs,9pnet
```

自己来生成initrd

```
./mkinitrd.py -m 9p 9pnet_virtio overlay
```

运行QEMU

```
./runqemu.py
```
