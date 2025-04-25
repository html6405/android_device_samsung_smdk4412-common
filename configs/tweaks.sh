#!/system/bin/sh

#CPUSETS
echo 0-1 > /dev/cpuset/background/cpus
echo 0-1 > /dev/cpuset/system-background/cpus
echo 1 > /dev/cpuset/restricted/cpus
echo 0-2 > /dev/cpuset/foreground/cpus
echo 0-3 > /dev/cpuset/top-app/cpus