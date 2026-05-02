#!/usr/bin/env python3

import os
from pty import openpty
from subprocess import check_output, run

def main(args):
    rootdir = args.root
    initrd = args.initrd
    kernel_version = check_output(("rpm", f"--root={rootdir}", "-q", "kernel", "--qf", "%{VERSION}-%{RELEASE}.%{ARCH}\n")).splitlines()[0].decode()
    kernel = os.path.join(rootdir, 'usr/lib/modules', kernel_version, "vmlinuz")

    kernel_args = 'console=ttyS0 panic=-1 intel_iommu=on'
    if args.quiet:
        kernel_args += ' quiet'

    assert kernel is not None

    pass_fds = ()

    master_fd, slave_fd = openpty()
    ttyname = os.ttyname(slave_fd)
    os.close(slave_fd)
    pass_fds += (master_fd,)
    run(('flatpak-spawn', '--host', 'ptyxis', '--tab', '--', 'toolbox', 'run', '-r', '43', os.path.join(rootdir, 'usr/bin/busybox'), 'microcom', ttyname))

    qemu_args = (
        'qemu-system-x86_64',
        '-machine', 'q35,accel=kvm,kernel-irqchip=split',
        '-device', 'intel-iommu,intremap=on',
        '-nographic', '-no-reboot', '-net', 'none',
        '-virtfs', 'local,path=/,mount_tag=rootfs,security_model=none,readonly=on',
        '-virtfs', 'local,path=.,mount_tag=pwd,security_model=none,readonly=on',
        '-chardev', 'stdio,id=char0',
        '-mon', 'chardev=char0,mode=readline',
        '-add-fd', f'fd={master_fd},set=1,opaque=rdwr:monitor',
        '-chardev', f'serial,id=char1,path=/dev/fdset/1',
        '-serial', 'chardev:char1',
        '-kernel', kernel,
        '-initrd', initrd,
        '-append', kernel_args + ' -- ' + os.getcwd()
    )

    if args.serial:
        master_fd, slave_fd = openpty()
        ttyname = os.ttyname(slave_fd)
        os.close(slave_fd)
        pass_fds += (master_fd,)
        run(('flatpak-spawn', '--host', 'ptyxis', '--tab', '--', 'toolbox', 'run', '-r', '43', os.path.join(rootdir, 'usr/bin/busybox'), 'microcom', ttyname))

        qemu_args += (
            '-add-fd', f'fd={master_fd},set=2,opaque=rdwr:monitor',
            '-chardev', f'serial,id=char2,path=/dev/fdset/2',
            '-device', 'virtio-serial-pci-non-transitional,id=virtio-serial0,iommu_platform=on,max_ports=1,vectors=5',
            '-device', 'virtconsole,chardev=char2,bus=virtio-serial0.0')

    if args.vga or args.gpu:
        qemu_args += ('-display', 'gtk,gl=on,zoom-to-fit=off', '-vga', 'none')

    if args.vga is True:
        display = True
        qemu_args += ('-device', 'VGA')
    elif args.vga:
        display = True
        xres, yres = args.vga.split('x', 1)
        xres = int(xres)
        yres = int(yres)
        qemu_args += ('-device', f'VGA,xres={xres},yres={yres}')

    if args.gpu is True:
        display = True
        qemu_args += ('-device', f'virtio-gpu-gl-pci,disable-legacy=on,iommu_platform=on')
    elif args.gpu:
        display = True
        xres, yres = args.gpu.split('x', 1)
        xres = int(xres)
        yres = int(yres)
        qemu_args += ('-device', f'virtio-gpu-gl-pci,disable-legacy=on,iommu_platform=on,xres={xres},yres={yres}')

    if args.edu:
        qemu_args += ('-device', 'edu')

    env = os.environ.copy()
    env['LANG'] = 'C.utf8'

    print(qemu_args)
    run(qemu_args, pass_fds=pass_fds, env=env)

if __name__ == '__main__':
    rootdir = os.path.join(os.path.dirname(__file__), 'rootfs')
    from argparse import ArgumentParser
    parser = ArgumentParser()
    parser.add_argument('-r', '--root', default=rootdir)
    parser.add_argument("--initrd", default=os.path.join(rootdir, 'boot/initrd'))
    parser.add_argument('-q', '--quiet', action='store_true')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--vga', nargs='?', const=True)
    group.add_argument('--gpu', nargs='?', const=True)
    parser.add_argument('--edu', action='store_true')
    parser.add_argument('--serial', action='store_true')

    args = parser.parse_args()
    main(args)
