#!/bin/bash
# Wait for ping-r4L to finish, then start ping-r4L2 on the same FM-1 boot.
cd ~/fm1-base-open
until grep -qE "^[0-9:]+ done$" out/open-ble/ping-r4L/events.txt; do sleep 20; done
mkdir -p ~/fm1-sniff/r4L2
exec bash out/open-ble/ping-r4L2/run.sh > out/open-ble/ping-r4L2/run.out 2>&1
