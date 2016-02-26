#! /bin/sh

#arg1 = META DEVICE
#arg2 = DATA DEVICE
#arg3 = HASH ALGORITHM

META_DEV=$1
DATA_DEV=$2
HASH_ALG=$3

DATA_DEV_SIZE=`blockdev --getsz $DATA_DEV`
TARGET_SIZE=`expr $DATA_DEV_SIZE \* 15 / 10`

# Reset metadata dev
dd if=/dev/zero of=$META_DEV bs=4096 count=1

echo "0 $TARGET_SIZE ssddup $META_DEV $DATA_DEV $HASH_ALG"
echo "0 $TARGET_SIZE ssddup $META_DEV $DATA_DEV $HASH_ALG" | dmsetup create dm-ssddup
