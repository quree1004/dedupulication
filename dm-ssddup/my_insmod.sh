#!/bin/sh

KERNEL_PATH=/usr/src/kernels/linux-3.18.26

insmod $KERNEL_PATH/lib/libcrc32c.ko
insmod $KERNEL_PATH/drivers/md/dm-bufio.ko
insmod $KERNEL_PATH/drivers/md/persistent-data/dm-persistent-data.ko
