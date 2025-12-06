#!/usr/bin/env bash

qemu-system-x86_64 \
-machine q35 \
-nographic -no-reboot -net none \
-chardev stdio,mux=on,id=char0 \
-mon chardev=char0,mode=readline \
-serial chardev:char0 \
-drive file=/usr/share/OVMF/OVMF_CODE.fd,if=pflash,format=raw,readonly=on \
"$@"
