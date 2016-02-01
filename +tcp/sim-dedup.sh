#!/bin/sh

size1=`blockdev --getsize $1`
size2=`blockdev --getsize $2`
echo "0 $size1 sim_dedup $1 0
$size1 $size2 sim_dedup $2 0" | dmsetup create sim_dedup
