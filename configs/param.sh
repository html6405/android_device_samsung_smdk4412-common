#!/system/bin/sh

FILE=/data/.param.info
PARAM_FILE=/data/param.bin
PARAM_PATH=/dev/block/platform/dw_mmc/by-name/PARAM

file_exists=0
param_file_exists=0

if test -f "$FILE"; then
  echo "$FILE exists."
  file_exists=1
fi

if [ "$file_exists" = "0" ] ; then
 dd if=$PARAM_PATH of=$PARAM_FILE

  if test -f "$PARAM_FILE"; then
    echo "$FPARAM_FILE exists."
    param_file_exists=1
  fi

  if [ "$param_file_exists" = "1" ] ; then
    dd if=/system/vendor/param.lfs of=$PARAM_PATH
    touch /data/.param.info 0
    touch /data/param_restore.sh 0
    echo "#!/system/bin/sh

    PARAM_FILE=/data/param.bin
    param_file_exists=0
    PARAM_PATH=/dev/block/platform/dw_mmc/by-name/PARAM

    if test -f "$PARAM_FILE"; then
      echo "$FPARAM_FILE exists."
      param_file_exists=1
    fi

    if [ "$param_file_exists" = "1" ] ; then
      dd if=$PARAM_FILE of=$PARAM_PATH
    fi" > /data/param_restore.sh
  fi

fi