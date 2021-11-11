#!/system/bin/sh

NavbarState=$(getprop qemu.hw.mainkeys)

do_navbar_switch() {

  if [ "$NavbarState" != "0" ] ; then
    echo qemu.hw.mainkeys=0 >> /vendor/build.prop
  fi
  if [ "$NavbarState" == "0" ] ; then
    echo "$(grep -v "qemu" /vendor/build.prop)" >/vendor/build.prop
    chmod 755 /vendor/build.prop
  fi
reboot
}

do_navbar_switch