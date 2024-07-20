#!/system/bin/sh

echo 1 > /proc/sys/vm/dirty_background_ratio
echo 300 > /proc/sys/vm/dirty_expire_centisecs

