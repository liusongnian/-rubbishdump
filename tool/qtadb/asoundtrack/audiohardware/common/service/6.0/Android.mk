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

include $(CLEAR_VARS)
LOCAL_MODULE := android.hardware.audio@6.0-impl-mediatek
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
LOCAL_SRC_FILES := \
    Conversions.cpp \
    Device.cpp \
    DevicesFactory.cpp \
    ParametersUtil.cpp \
    PrimaryDevice.cpp \
    Stream.cpp \
    StreamIn.cpp \
    StreamOut.cpp \
    MTKPrimaryDevice.cpp

LOCAL_CFLAGS += -DMAJOR_VERSION=6
LOCAL_CFLAGS += -DMINOR_VERSION=0
LOCAL_CFLAGS += -include VersionMacro.h

LOCAL_SHARED_LIBRARIES := \
    libaudiofoundation \
    libbase \
    libcutils \
    libfmq \
    libhardware \
    libhidlbase \
    libhidltransport \
    liblog \
    libutils \
    android.hardware.audio@6.0 \
    android.hardware.audio.common-util \
    android.hardware.audio.common@6.0 \
    android.hardware.audio.common@6.0-util \
    vendor.mediatek.hardware.audio@6.1 \
    libmedia_helper

LOCAL_HEADER_LIBRARIES := \
    libaudioclient_headers \
    libaudio_system_headers \
    libhardware_headers \
    libmedia_headers \
    audio_hal_common_includes \

include $(BUILD_SHARED_LIBRARY)

