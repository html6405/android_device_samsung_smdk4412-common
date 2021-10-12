#!/bin/bash
#
# Copyright (C) 2016 The CyanogenMod Project
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

set -e
DEVICE_COMMON=smdk4412-common
DEVICE=${DEVICE:-nodevice}
VENDOR=samsung

if [ "y$INITIAL_COPYRIGHT_YEAR" '==' "y" ]; then
    INITIAL_COPYRIGHT_YEAR=2012
fi

# Load extract_utils and do some sanity checks
MY_DIR="${BASH_SOURCE%/*}"
if [[ ! -d "$MY_DIR" ]]; then MY_DIR="$PWD"; fi

LINEAGE_ROOT="$MY_DIR"/../../..

HELPER="$LINEAGE_ROOT"/vendor/lineage/build/tools/extract_utils.sh
if [ ! -f "$HELPER" ]; then
    echo "Unable to find helper script at $HELPER"
    exit 1
fi
. "$HELPER"

# Initialize the helper for common device
setup_vendor "$DEVICE_COMMON" "$VENDOR" "$LINEAGE_ROOT" true

# Copyright headers and common guards
write_headers "i605 i925 i9300 i9305 l900 n5100 n5110 n5120 n7100 n8000 n8010 n8013 n8020 n80xx r950 t0lte t0lteatt t0ltejpn t0ltekor t0ltetmo"

write_makefiles "$MY_DIR"/proprietary-files.txt

write_footers

if [ "$DEVICE" "!=" "nodevice" ]; then
    # Reinitialize the helper for the device-specific blobs
    setup_vendor "$DEVICE" "$VENDOR" "$LINEAGE_ROOT"

    # Copyright headers and guards
    write_headers

    # The device-specific blobs
    write_makefiles "$MY_DIR"/../$DEVICE/proprietary-files.txt

    # Done
    write_footers
fi
