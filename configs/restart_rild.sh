#!/system/bin/sh

simState=$(getprop gsm.sim.state)
bootCompleted=$(getprop sys.boot_completed)
setprop gsm.resetreason $simState

do_reset_radio() {
  resetCount=$(getprop gsm.resetcount)
  if [ -z $resetCount ] ; then
    resetCount=0
  fi

  if [ "$bootCompleted" == 1 ] ; then
    if [ "$simState" == "UNKNOWN" ] ; then
      setprop gsm.sim.state "RESET RILD"
      PID=$(ps -A | grep "rild" | awk '{ print $2 }')
      kill $PID
      setprop gsm.resetcount $(( $resetCount + 1 ))
    fi
  fi
  sleep 10
  simState=$(getprop gsm.sim.state)
  if [ "$simState" != "LOADED" ] ; then
    if [ "$simState" != "READY" ] ; then
      setprop gsm.sim.state "RESET_QMUXD"
      PIDQ=$(ps -A | grep "qmuxd" | awk '{ print $2 }')
      kill $PIDQ
      setprop gsm.resetcount $(( $resetCount + 1 ))
    fi
  fi
  sleep 30
  simState=$(getprop gsm.sim.state)
  if [ "$simState" == "RESET_QMUXD" ] ; then
    setprop gsm.sim.state "UNKNOWN"
  fi

}

do_reset_radio
