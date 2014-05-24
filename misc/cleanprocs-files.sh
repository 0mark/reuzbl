#!/bin/bash
killall reuzbl; killall strace;
killall -9 reuzbl; killall -9 strace

rm -rf /tmp/reuzbl_*

echo "reUzbl processes:"
ps aux | grep reuzbl | grep -v grep
echo "reUzbl /tmp entries:"
ls -alh /tmp/reuzbl* 2>/dev/null
