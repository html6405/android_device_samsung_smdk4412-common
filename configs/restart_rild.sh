#!/system/bin/sh

#set -x

reset_threshold=5

do_reset_radio() {
  resetCount=$(getprop gsm.resetcount)
  if [ -z $resetCount ] ; then
    resetCount=0
  fi

  # Allow up to one radio reset in airplane mode
  airplane_mode=$(settings get global airplane_mode_on)
  if [ "$airplane_mode" == "1" ] && [ $resetCount -ge 1 ] ; then
    return;
  fi

  # No radio after several resets?
  # Probably something goes wrong, we have nothing to do.
  if [ $resetCount -ge $reset_threshold ] ; then
    return
  fi

  #status=$(getprop gsm.status)

  subId=$(getprop gsm.subid)
  simState=$(getprop gsm.sim_state)

  sleep 30
  simState=$(getprop gsm.sim_state)
  if [ "$simState" != "LOADED" ] ; then
    PID=$(ps -A | grep "rild" | awk '{ print $2 }')
    kill $PID
    sleep 5
    setprop gsm.resetcount $(( $resetCount + 1 ))
  fi
  sleep 5
  simState=$(getprop gsm.sim_state)
  if [ "$simState" != "LOADED" ] ; then
    PIDQ=$(ps -A | grep "qmuxd" | awk '{ print $2 }')
    kill $PIDQ
    sleep 5
  fi

  setprop gsm.radioreset false

}

do_reset_radio
