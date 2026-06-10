#!/usr/bin/env python3

import sys
import os
from argparse import ArgumentParser
from subprocess import check_call

parser = ArgumentParser()
parser.add_argument('--user', required=True)
parser.add_argument('--uid', type=int, required=True)
parser.add_argument('--udevd', action='store_true')
parser.add_argument('--seatd', action='store_true')
parser.add_argument('--xdg-runtime-dir', action='store_true')
parser.add_argument('--weston', action='store_true')

args = parser.parse_args()

env = os.environ.copy()

if args.udevd:
    check_call(("/usr/lib/systemd/systemd-udevd", "-d"))
    check_call(("udevadm", "trigger"))

if args.seatd:
    check_call(("setsid", "-f", "seatd", "-u", args.user))

if args.xdg_runtime_dir:
    path = f'/run/user/{args.uid}'
    os.makedirs(path, exist_ok=True)
    os.chown(path, args.uid, 0)
    os.chmod(path, 0o700)
    env['XDG_RUNTIME_DIR'] = path

if args.weston:
    check_call(("setsid", "-f", "runuser", "-u", args.user, "weston"), env=env)

os.execv("/usr/bin/bash", ("bash",))
