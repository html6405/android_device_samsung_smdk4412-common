#!/system/bin/sh

echo 1 > /proc/sys/vm/dirty_background_ratio
echo 300 > /proc/sys/vm/dirty_expire_centisecs

#CPUSETS
echo 0-1 > /dev/cpuset/background/cpus
echo 0-1 > /dev/cpuset/system-background/cpus
echo 1 > /dev/cpuset/restricted/cpus
echo 0-2 > /dev/cpuset/foreground/cpus
echo 0-3 > /dev/cpuset/top-app/cpus