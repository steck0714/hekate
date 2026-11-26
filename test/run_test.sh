#!/bin/sh
# woa_fat32.h のホスト検証(要 gcc, dosfstools)
set -e
cd "$(dirname "$0")"
mkdir -p stub/utils
echo 'typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32; typedef unsigned long long u64;' > stub/utils/types.h
gcc -w -Istub -I../files/nyx/nyx_gui/frontend -o /tmp/woa_fat32_test woa_fat32_test.c
dd if=/dev/zero of=/tmp/esp.img bs=1M count=256 status=none
/tmp/woa_fat32_test 0 524288 /tmp/esp.img
fsck.fat -n /tmp/esp.img
