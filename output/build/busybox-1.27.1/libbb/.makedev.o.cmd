cmd_libbb/makedev.o := /home/dean/sunxi-v3s/buildroot-2017.08/output/host/bin/arm-linux-gnueabihf-gcc -Wp,-MD,libbb/.makedev.o.d   -std=gnu99 -Iinclude -Ilibbb  -include include/autoconf.h -D_GNU_SOURCE -DNDEBUG -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 -D"BB_VER=KBUILD_STR(1.27.1)" -DBB_BT=AUTOCONF_TIMESTAMP -D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64  -Os  -Wall -Wshadow -Wwrite-strings -Wundef -Wstrict-prototypes -Wunused -Wunused-parameter -Wunused-function -Wunused-value -Wmissing-prototypes -Wmissing-declarations -Wno-format-security -Wdeclaration-after-statement -Wold-style-definition -fno-builtin-strlen -finline-limit=0 -fomit-frame-pointer -ffunction-sections -fdata-sections -fno-guess-branch-probability -funsigned-char -static-libgcc -falign-functions=1 -falign-jumps=1 -falign-labels=1 -falign-loops=1 -fno-unwind-tables -fno-asynchronous-unwind-tables -fno-builtin-printf -Os     -D"KBUILD_STR(s)=\#s" -D"KBUILD_BASENAME=KBUILD_STR(makedev)"  -D"KBUILD_MODNAME=KBUILD_STR(makedev)" -c -o libbb/makedev.o libbb/makedev.c

deps_libbb/makedev.o := \
  libbb/makedev.c \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/stdc-predef.h \
  include/platform.h \
    $(wildcard include/config/werror.h) \
    $(wildcard include/config/big/endian.h) \
    $(wildcard include/config/little/endian.h) \
    $(wildcard include/config/nommu.h) \
  /opt/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/lib/gcc/arm-linux-gnueabihf/6.3.1/include-fixed/limits.h \
  /opt/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/lib/gcc/arm-linux-gnueabihf/6.3.1/include-fixed/syslimits.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/limits.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/features.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/sys/cdefs.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/wordsize.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/gnu/stubs.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/gnu/stubs-hard.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/posix1_lim.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/local_lim.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/linux/limits.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/posix2_lim.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/xopen_lim.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/stdio_lim.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/byteswap.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/byteswap.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/types.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/typesizes.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/byteswap-16.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/endian.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/endian.h \
  /opt/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/lib/gcc/arm-linux-gnueabihf/6.3.1/include/stdint.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/stdint.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/wchar.h \
  /opt/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/lib/gcc/arm-linux-gnueabihf/6.3.1/include/stdbool.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/unistd.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/posix_opt.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/environments.h \
  /opt/gcc-linaro-6.3.1-2017.05-x86_64_arm-linux-gnueabihf/lib/gcc/arm-linux-gnueabihf/6.3.1/include/stddef.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/bits/confname.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/getopt.h \
  /home/dean/sunxi-v3s/buildroot-2017.08/output/host/arm-buildroot-linux-gnueabihf/sysroot/usr/include/sys/sysmacros.h \

libbb/makedev.o: $(deps_libbb/makedev.o)

$(deps_libbb/makedev.o):
