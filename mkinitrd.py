#!/usr/bin/env python3

import sys
import os
from subprocess import check_output, Popen, PIPE
from graphlib import TopologicalSorter
from tempfile import TemporaryDirectory

def main(*mods):
    rootdir = os.path.join(os.path.dirname(__file__), 'rootfs')
    kernel_version = check_output(("rpm", f"--root={rootdir}", "-q", "kernel", "--qf", "%{VERSION}-%{RELEASE}.%{ARCH}\n")).splitlines()[0].decode()

    def modinfo(field, mod):
        return check_output(("modinfo", "-F", field, "-b", os.path.join(rootdir, "usr"), "-k", kernel_version, mod)).splitlines()[0].decode()

    added = set()
    sorter = TopologicalSorter()

    queue = list(mods)
    while queue:
        mod = queue.pop(0)
        if mod in added:
            continue
        added.add(mod)
        d = modinfo("depends", mod)
        depends = d.split(",") if d else ()
        sorter.add(mod, *depends)
        queue.extend(depends)

    mods = list(sorter.static_order())

    with TemporaryDirectory() as tmpdir:
        filenames = []

        for mod in mods:
            filename = modinfo("filename", mod)
            basename = os.path.basename(filename)
            filenames.append(basename)
            os.symlink(filename, os.path.join(tmpdir, basename))

        init_path = os.path.join(tmpdir, "init")
        with open(init_path, "w") as f:
            f.write("""#!/busybox sh
/busybox mkdir -p /bin
/busybox --install /bin
""")
            for filename in filenames:
                f.write(f"insmod {filename}\n")

            f.write("""mkdir /rootfs
mount -t 9p -o trans=virtio,version=9p2000.L,ro rootfs /rootfs
mkdir /upper
mkdir /work
mkdir /sysroot
mount -t overlay overlay -o lowerdir=/rootfs,upperdir=/upper,workdir=/work /sysroot
cd /sysroot
mkdir dev
mount -t devtmpfs none dev
mkdir proc
mount -t proc none proc
mkdir sys
mount -t sysfs none sys
mkdir -p /sysroot$1
mount -t 9p -o trans=virtio,version=9p2000.L,ro pwd /sysroot$1
mount -t devtmpfs none /dev
exec setsid cttyhack switch_root -c /dev/console /sysroot /bin/bash -c "trap 'poweroff -f' EXIT; cd $1; /bin/bash"
""")

        os.chmod(init_path, 0o555)
        filenames.append("init")

        filenames.append("busybox")
        os.symlink(os.path.join(rootdir, "usr/bin/busybox"), os.path.join(tmpdir, "busybox"))

        with Popen(("cpio", "--null", "--dereference", "-ov", "--format=newc", "--reproducible", "--reset-access-time"), stdin=PIPE, stdout=PIPE, cwd=tmpdir) as cpio:
            with Popen(("xz", "--compress", "--stdout", "--format=lzma"), stdin=cpio.stdout, stdout=PIPE) as xz:
                cpio.stdout.close()
                with cpio.stdin:
                    for name in filenames:
                        cpio.stdin.write(name.encode() + b'\x00')
                stdout = xz.communicate()[0]

        assert cpio.returncode == 0
        assert xz.returncode == 0

        with open("initrd", "wb") as initrd:
            initrd.write(stdout)

if __name__ == '__main__':
    main(*sys.argv[1:])
