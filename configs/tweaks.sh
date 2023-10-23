#!/system/bin/sh

echo 1 > /proc/sys/vm/dirty_background_ratio
echo 300 > /proc/sys/vm/dirty_expire_centisecs
echo 400000> /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
