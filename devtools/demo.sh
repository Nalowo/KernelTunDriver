#!/bin/sh
set -eu

B=/mnt/host/build

insmod $B/ktun.ko
$B/ktunctl echo &
P=$!
sleep 1

ip link set ktun0 up
ip addr add 10.0.0.1/24 dev ktun0
ping -c 3 10.0.0.2
cat /proc/ktun

kill $P
sleep 1
rmmod ktun
echo "demo: OK"
