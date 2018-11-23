#!/bin/sh
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
mv out/image/rootfs.tar.gz ../rootfs_build/rootfs-first.tar.gz
