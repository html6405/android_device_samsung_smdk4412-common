#
# Copyright (C) 2020 The LineageOS Project
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
PRODUCT_PROPERTY_OVERRIDES += \
	ro.treble.enabled=false \
	ro.lcd_min_brightness=20 \
	ro.egl.destroy_after_detach=true \
	debug.hwui.renderer=opengl \
	ro.hardware.egl=mali \
    debug.renderengine.backend=gles \
    ro.hwui.render_ahead=20 \
    persist.sys.sf.color_saturation=1.0

PRODUCT_PROPERTY_OVERRIDES += \
	bluetooth.device.class_of_device?=90,2,12 \
	bluetooth.profile.asha.central.enabled?=true \
	bluetooth.profile.a2dp.source.enabled?=true \
	bluetooth.profile.avrcp.target.enabled?=true \
	bluetooth.profile.bas.client.enabled?=true \
	bluetooth.profile.gatt.enabled?=true \
	bluetooth.profile.hfp.ag.enabled?=true \
	bluetooth.profile.hid.device.enabled?=true \
	bluetooth.profile.hid.host.enabled?=true \
	bluetooth.profile.map.server.enabled?=true \
	bluetooth.profile.opp.enabled?=true \
	bluetooth.profile.pan.nap.enabled?=true \
	bluetooth.profile.pan.panu.enabled?=true \
	bluetooth.profile.pbap.server.enabled?=true \
	bluetooth.profile.sap.server.enabled?=true \
	bluetooth.core.le.vendor_capabilities.enabled=false