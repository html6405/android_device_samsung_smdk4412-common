import common
import struct

def FullOTA_InstallEnd(info):
	info.script.AppendExtra('ifelse(is_mounted("/system"), unmount("/system"));')
	info.script.AppendExtra('mount("ext4", "EMMC", "/dev/block/platform/dw_mmc/by-name/SYSTEM", "/system", "");')
	info.script.AppendExtra('run_program("/sbin/busybox", "rm", "/system/vendor");')
	info.script.AppendExtra('run_program("/sbin/busybox", "mkdir", "/system/vendor");')
	info.script.AppendExtra('unmount("/system");')

def FullOTA_PostValidate(info):
	# run e2fsck
	info.script.AppendExtra('run_program("/sbin/e2fsck", "-fy", "/dev/block/platform/dw_mmc/by-name/USERDATA");')
	# resize2fs: run and delete
	info.script.AppendExtra('run_program("/tmp/install/bin/resize2fs_static", "/dev/block/platform/dw_mmc/by-name/USERDATA");')
	# run e2fsck
	info.script.AppendExtra('run_program("/sbin/e2fsck", "-fy", "/dev/block/platform/dw_mmc/by-name/USERDATA");')