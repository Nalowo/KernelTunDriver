#!/bin/sh
# devtools/demo.sh -- defence demo, run INSIDE the guest (test T5, stage E6).
# Usage (guest): /mnt/host/devtools/demo.sh
set -eu

B=/mnt/host/build

# TODO: fill in at stage E6, following acceptance test T5:
#   insmod $B/ktun.ko
#   $B/ktunctl echo &            # prints ktun0
#   ip addr add 10.0.0.1/24 dev ktun0 && ip link set ktun0 up
#   ping -c 3 10.0.0.2
#   cat /proc/ktun
#   kill %1 && rmmod ktun
echo "demo.sh: not implemented yet" >&2
exit 1
