#!/system/bin/sh

rm /system/bin/linkerconfig
cp /vendor/bin/linkerconfig /system/bin/linkerconfig

chown root:shell /system/bin/linkerconfig
chmod 755 /system/bin/linkerconfig

chcon -v u:object_r:linkerconfig_exec:s0 /system/bin/linkerconfig

ln -s /vendor/lib/egl /system/lib/egl

rm /system/lib/libc.so
cp /vendor/lib/bionic/libc.so /system/lib/libc.so

chmod 755 /system/lib/libc.so

# chcon -v u:object_r:system_file:s0 /system/lib/libc.so
# cp /vendor/bin/glgps /system/bin/glgps
# chmod 755 /system/bin/glgps
# chcon -v u:object_r:glgps_exec:s0 /system/bin/glgps

# cp /vendor/lib/libgps_symbols /system/lib/libgps_symbols
# chmod 755 /system/lib/libgps_symbols

# rm system/etc/init/mediaserver.rc
# cp vendor/etc/init/mediaserver.rc system/etc/init/mediaserver.rc
# chmod 755 system/etc/init/mediaserver.rc

