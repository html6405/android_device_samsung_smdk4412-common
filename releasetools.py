import common
import struct

def FullOTA_PostValidate(info):
	# run e2fsck
	info.script.AppendExtra('run_program("/sbin/e2fsck", "-fy", "/dev/block/platform/dw_mmc/by-name/USERDATA");');
	# resize2fs: run and delete
	info.script.AppendExtra('run_program("/tmp/install/bin/resize2fs_static", "/dev/block/platform/dw_mmc/by-name/USERDATA");');
	# run e2fsck
	info.script.AppendExtra('run_program("/sbin/e2fsck", "-fy", "/dev/block/platform/dw_mmc/by-name/USERDATA");');

	# run e2fsck
	info.script.AppendExtra('run_program("/sbin/e2fsck", "-fy", "/dev/block/platform/dw_mmc/by-name/SYSTEM");');
	# resize2fs: run and delete
	info.script.AppendExtra('run_program("/tmp/install/bin/resize2fs_static", "/dev/block/platform/dw_mmc/by-name/SYSTEM");');
	# run e2fsck
	info.script.AppendExtra('run_program("/sbin/e2fsck", "-fy", "/dev/block/platform/dw_mmc/by-name/SYSTEM");');