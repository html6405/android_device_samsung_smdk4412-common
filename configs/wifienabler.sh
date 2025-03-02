#!/system/bin/sh

Enabled=$(getprop vendor.wifikiller)

if [ "$Enabled" == "on" ] ; then
  svc wifi enable
fi