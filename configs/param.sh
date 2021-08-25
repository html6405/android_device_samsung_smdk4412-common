#!/system/bin/sh

FILE=/data/.param.info
file_exists=0

if test -f "$FILE"; then
  echo "$FILE exists."
  file_exists=1
fi

if [ "$file_exists" = "0" ] ; then
  dd if=/vendor/param.lfs of=/dev/block/platform/dw_mmc/by-name/PARAM
  touch /data/.param.info 0
fi