#
# Copyright (C) 2019 The Android Open Source Project
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


LOCAL_PATH := $(call my-dir)

#
# Service
#

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.audio.service.mediatek
LOCAL_INIT_RC := android.hardware.audio.service.mediatek.rc
LOCAL_MODULE_OWNER := mtk
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_SRC_FILES := \
    service.cpp

LOCAL_CFLAGS := -Wall -Werror
LOCAL_SHARED_LIBRARIES := \
    libaudiofoundation \
    libcutils \
    libbinder \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    libhardware \
    libhwbinder \
    android.hardware.audio@6.0 \
    android.hardware.audio@7.0 \
    android.hardware.audio.common@6.0 \
    android.hardware.audio.common@7.0 \
    android.hardware.audio.effect@6.0 \
    android.hardware.audio.effect@7.0 \
    android.hardware.bluetooth.audio@2.0 \
    android.hardware.bluetooth.audio@2.1 \
    vendor.mediatek.hardware.bluetooth.audio@2.1 \
    android.hardware.soundtrigger@2.3 \
    vendor.mediatek.hardware.audio@6.1 \
    vendor.mediatek.hardware.audio@7.1 \
    vendor.mediatek.hardware.bluetooth.audio@2.2

LOCAL_HEADER_LIBRARIES := \
    audio_hal_common_includes \

# Can not switch to Android.bp until AUDIOSERVER_MULTILIB
# is deprecated as build config variable are not supported
ifeq ($(MTK_AUDIOHAL_PROCESS_DEFAULT_64BIT), yes)
LOCAL_MULTILIB := 64
else
ifeq ($(strip $(AUDIOSERVER_MULTILIB)),)
LOCAL_MULTILIB := 32
else
LOCAL_MULTILIB := $(AUDIOSERVER_MULTILIB)
endif
endif

ifneq ($(TARGET_BUILD_VARIANT), user)
#LOCAL_CFLAGS += -DFORCE_DIRECTCOREDUMP
endif

include $(BUILD_EXECUTABLE)
