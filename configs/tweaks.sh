#!/system/bin/sh

echo 1 > /proc/sys/vm/dirty_background_ratio
echo 300 > /proc/sys/vm/dirty_expire_centisecs
echo 1 > /sys/class/misc/boeffla_sound/boeffla_sound
echo "63 63" > /sys/class/misc/boeffla_sound/speaker_volume

