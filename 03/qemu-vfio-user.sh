#!/usr/bin/env bash

kernel_version=$(rpm --root="$(pwd)/../rootfs" -q kernel --qf "%{VERSION}-%{RELEASE}.%{ARCH}\n")

qemu-system-x86_64 \
-machine q35,accel=kvm,kernel-irqchip=split \
-object memory-backend-memfd,id=mem,size=128M,share=on \
-numa node,memdev=mem \
-device intel-iommu,caching-mode=on,intremap=on \
-nographic -no-reboot -net none \
-chardev stdio,mux=on,id=char0 \
-mon chardev=char0,mode=readline \
-serial chardev:char0 \
-virtfs local,path="../rootfs",mount_tag=rootfs,security_model=none,readonly=on \
-virtfs local,path=".",mount_tag=pwd,security_model=none,readonly=on \
-device '{"driver":"vfio-user-pci","socket":{"path":"/tmp/edu.sock","type":"unix"}}' \
-kernel "../rootfs/usr/lib/modules/$kernel_version/vmlinuz" \
-initrd initrd \
-append "console=ttyS0 panic=-1 intel_iommu=on quiet -- $(pwd)"
