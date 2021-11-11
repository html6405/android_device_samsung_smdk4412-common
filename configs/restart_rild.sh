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

  if [ "$subId" != "1" ] ; then
    sleep 10
    subId=$(getprop gsm.subid)
    if [ "$subId" != "1" ] ; then
      stop ril-daemon
      sleep 5
      start ril-daemon
      setprop gsm.resetcount $(( $resetCount + 1 ))
      resetValue=1
    fi
  fi

  if [ "$resetValue" != "1" ] ; then
    if [ "$simState" != "READY" ] ; then
      sleep 10
      stop ril-daemon
      sleep 5
      start ril-daemon
      setprop gsm.resetcount $(( $resetCount + 1 ))
    fi
    sleep 10
    simState=$(getprop gsm.sim_state)
    if [ "$simState" != "READY" ] ; then
      stop ril-daemon
      sleep 5
      start ril-daemon
      setprop gsm.resetcount $(( $resetCount + 1 ))
    fi
    resetValue=0
  fi
  setprop gsm.radioreset false


}

do_reset_radio
