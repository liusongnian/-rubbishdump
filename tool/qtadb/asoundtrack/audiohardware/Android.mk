# Copyright Statement:
#
# This software/firmware and related documentation ("MediaTek Software") are
# protected under relevant copyright laws. The information contained herein
# is confidential and proprietary to MediaTek Inc. and/or its licensors.
# Without the prior written permission of MediaTek inc. and/or its licensors,
# any reproduction, modification, use or disclosure of MediaTek Software,
# and information contained herein, in whole or in part, shall be strictly prohibited.
#
# MediaTek Inc. (C) 2010. All rights reserved.
#
# BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
# THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
# RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
# AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
# NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
# SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
# SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
# THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
# THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
# CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
# SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
# STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
# CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
# AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
# OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
# MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
#
# The following software/firmware and/or related documentation ("MediaTek Software")
# have been modified by MediaTek Inc. All revisions are subject to any receiver's
# applicable license agreements with MediaTek Inc.

#Split build wrap
ifdef MTK_TARGET_PROJECT

ifneq ($(MTK_GENERIC_HAL),yes)
ifneq (,$(filter $(strip $(MTK_PLATFORM_DIR)), elbrus mt2601 mt3886 mt6570 mt6572 mt6580 mt6582 mt6592 mt6735 mt6752 mt6755 mt6757 mt6759 mt6795 mt6797 mt6799 mt7623 mt8127 mt8163 mt8167 mt8173 mt8168))
include $(call all-subdir-makefiles)
else

### ============================================================================
### new chips (after mt6763)
### ============================================================================

ifeq ($(strip $(BOARD_USES_MTK_AUDIO)),true)

AUDIO_COMMON_DIR := common

### ============================================================================
### platform audio HAL
### ============================================================================

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
include $(LOCAL_PATH)/$(MTK_PLATFORM_DIR)/Android.mk


LOCAL_CFLAGS += -Werror -Wno-error=undefined-bool-conversion
#LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -fexceptions

### ============================================================================
### include files
### ============================================================================

LOCAL_C_INCLUDES := \
    $(call include-path-for, audio-utils) \
    $(call include-path-for, audio-effects) \
    $(call include-path-for, alsa-utils) \
    $(TOPDIR)external/tinyxml2 \
    $(TOPDIR)external/tinyalsa/include  \
    $(TOPDIR)external/tinycompress/include \
    $(TOPDIR)vendor/mediatek/proprietary/hardware/ccci/include \
    $(TOPDIR)vendor/mediatek/proprietary/external/AudioCompensationFilter \
    $(TOPDIR)vendor/mediatek/proprietary/external/AudioComponentEngine \
    $(TOPDIR)vendor/mediatek/proprietary/external/audiocustparam \
    $(TOPDIR)vendor/mediatek/proprietary/external/AudioSpeechEnhancement/V3/inc \
    $(TOPDIR)vendor/mediatek/proprietary/external/libudf/libladder \
    $(TOPDIR)vendor/mediatek/proprietary/hardware/power/config/common/intf_types \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_headers \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_utils/AudioToolkit \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_headers/audiopolicy_parameters \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_headers/cgen/cfgfileinc \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_headers/custom_volume \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_headers/gain_table \
    $(MTK_PATH_CUSTOM)/hal/audioflinger/audio \
    $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/V3/include \
    $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/include \
    $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/utility \
    $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/utility/uthash \
    $(MTK_PATH_SOURCE)/external/audio_utils/common_headers/customization \
    $(TOPDIR)vendor/mediatek/proprietary/hardware/power/include \
    $(TOP)/system/core/libutils/include


### ============================================================================
### library
### ============================================================================

LOCAL_SHARED_LIBRARIES += \
    libc \
    liblog \
    libcutils \
    libutils \
    libalsautils \
    libhardware_legacy \
    libhardware \
    libdl \
    libaudioutils \
    libtinyalsa \
    libtinyxml2 \
    libaudiotoolkit_vendor \
    libmedia_helper \
    libladder \
    android.hardware.audio@7.0 \
    android.hardware.audio.common-util \
    android.hardware.audio.common@7.0 \
    android.hardware.audio.common@7.0-util \
    vendor.mediatek.hardware.audio@7.1

LOCAL_HEADER_LIBRARIES += libaudioclient_headers libaudio_system_headers libmedia_headers audio_hal_common_includes

### ============================================================================
### project config depends
### ============================================================================

ifeq ($(strip $(TARGET_BUILD_VARIANT)),eng)
  LOCAL_CFLAGS += -DCONFIG_MT_ENG_BUILD
endif

### ============================================================================
### hardware project config
### ============================================================================

ifeq ($(strip $(MTK_AUDIO_MIC_INVERSE)),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_MIC_INVERSE
endif

# Primary USB
LOCAL_CFLAGS += -DPRIMARY_USB
ifeq ($(MTK_AUDIO_SUPER_HIFI_USB),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SUPER_HIFI_USB
endif

LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioUSBCenter.cpp \
                   $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerUsb.cpp


ifeq ($(MTK_AUDIODSP_SUPPORT),yes)
  MTK_AUDIO_DSP_RECOVERY_SUPPORT = yes
  MTK_AUDIO_DSP_SERVICE = yes
  LOCAL_HEADER_LIBRARIES += libaudiodsp_headers
  LOCAL_CFLAGS += -DMTK_AUDIODSP_SUPPORT
  LOCAL_SHARED_LIBRARIES += libtinycompress
  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderDspRaw.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClientDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerOffload.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerFmAdsp.cpp \
    $(AUDIO_COMMON_DIR)/audiodsp/AudioDspStreamManager.cpp \
    $(AUDIO_COMMON_DIR)/audiodsp/AudioALSAHandlerKtv.cpp \
    $(AUDIO_COMMON_DIR)/audiodsp/AudioDspCallFinal.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechDriverOpenDSP.cpp
endif

ifeq ($(MTK_AUDIO_DSP_SERVICE),yes)
    LOCAL_CFLAGS += -DMTK_AUDIO_DSP_SERVICE
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/audiodsp/audio_dsp_service.c
endif

# ultrasound proximity
ifeq ($(MTK_ULTRASND_PROXIMITY),yes)
  LOCAL_C_INCLUDES += \
    $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/ultrasound
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/ultrasound/UltrasoundOutOfCallManager.cpp \
                     $(AUDIO_COMMON_DIR)/ultrasound/AudioALSAUltrasoundOutOfCallController.cpp
  LOCAL_CFLAGS += -DMTK_ULTRASOUND_PROXIMITY_SUPPORT
endif

# a2dp offload
ifeq ($(MTK_A2DP_OFFLOAD_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_A2DP_OFFLOAD_SUPPORT
#    LOCAL_CFLAGS += -DTEST_A2DP_TASK
endif

# always build ASHA code, use XML option to control HAL ASHA support
LOCAL_CFLAGS += -DMTK_BT_HEARING_AID_SUPPORT

### ============================================================================
### speaker
### ============================================================================

#LOCAL_CFLAGS += -DSMARTPA_AUTO_CALIBRATE

# Smart Pa
LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioSmartPaController.cpp
LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/AudioParamParser
LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/AudioParamParser/include
LOCAL_C_INCLUDES += $(TOPDIR)external/libxml2/include
LOCAL_C_INCLUDES += $(TOPDIR)external/icu/icu4c/source/common

ifeq ($(MTK_AUDIO_SCP_RECOVERY_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SCP_RECOVERY_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/audiodsp/audio_scp_service.c
endif

### ============================================================================
### software project config
### ============================================================================

ifeq ($(strip $(MTK_BESLOUDNESS_SUPPORT)),yes)
  LOCAL_CFLAGS += -DMTK_BESLOUDNESS_SUPPORT
ifeq ($(strip $(MTK_BESLOUDNESS_RUN_WITH_HAL)),yes)
  LOCAL_CFLAGS += -DMTK_BESLOUDNESS_RUN_WITH_HAL
endif
endif

ifeq ($(strip $(MTK_BESSURROUND_SUPPORT)),yes)
  LOCAL_CFLAGS += -DMTK_BESSURROUND_SUPPORT
endif

ifeq ($(MTK_AUDIO_HYBRID_NLE_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_HYBRID_NLE_SUPPORT
#else
#  LOCAL_CFLAGS += -DMTK_AUDIO_SW_DRE
endif

# Audio HD Record
ifeq ($(MTK_AUDIO_HD_REC_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_AUDIO_HD_REC_SUPPORT
endif

# HIFI audio
ifeq ($(MTK_HIFIAUDIO_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_HIFIAUDIO_SUPPORT
endif

# SW Mixer
LOCAL_CFLAGS += -DHAVE_SW_MIXER
LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/utility/audio_sw_mixer.c \
                   $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerMixer.cpp

# In-Ear Monitors (IEMs)
ifeq ($(MTK_AUDIO_IEMS_SUPPORT),yes)
LOCAL_CFLAGS += -DHAVE_IEMS
LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/aud_drv/AudioIEMsController.cpp \
                   $(AUDIO_COMMON_DIR)/aud_drv/AudioALSACaptureDataClientIEMs.cpp \
                   $(AUDIO_COMMON_DIR)/aud_drv/AudioALSACaptureHandlerIEMs.cpp
endif

# Phonecall record voice only
LOCAL_CFLAGS += -DMTK_PHONE_CALL_RECORD_VOICE_ONLY

# Check shared input device priority
# LOCAL_CFLAGS += -DMTK_CHECK_INPUT_DEVICE_PRIORITY

# Remove phonecall record
# LOCAL_CFLAGS += -DMTK_PHONE_CALL_RECORD_DISABLE

# Remove audio dump
# LOCAL_CFLAGS += -DMTK_AUDIO_HAL_DUMP_DISABLE

# Remove record low latency mode
# LOCAL_CFLAGS += -DMTK_UL_LOW_LATENCY_MODE_DISABLE

### ============================================================================
### vow config
### ============================================================================

ifeq ($(MTK_VOW_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_VOW_SUPPORT

  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerVOW.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVOW.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAVoiceWakeUpController.cpp

  ifeq ($(MTK_VOW_DUAL_MIC_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_VOW_DUAL_MIC_SUPPORT
  endif
endif

### ============================================================================
### speech config
### ============================================================================

# C2K
ifneq ($(MTK_COMBO_MODEM_SUPPORT),yes)
ifeq ($(MTK_ECCCI_C2K),yes)
  LOCAL_CFLAGS += -DMTK_ECCCI_C2K
endif
endif

# refactor speech driver after 93 modem
ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_COMBO_MODEM_SUPPORT
  # RTT support after 93 modem
  LOCAL_CFLAGS += -DMTK_RTT_SUPPORT
  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechDriverNormal.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechMessageQueue.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechMessengerNormal.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerTelephonyTx.cpp
else
  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechDriverLAD.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechMessengerECCCI.cpp
endif

# for CCCI share memory EMI (after 92 modem)
ifeq ($(MTK_CCCI_SHARE_BUFFER_SUPPORT),yes)
    ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
        LOCAL_CFLAGS += -DMTK_CCCI_SHARE_BUFFER_SUPPORT # for 93 modem and later
    else
        LOCAL_CFLAGS += -DUSE_CCCI_SHARE_BUFFER # for 92 modem
        LOCAL_SHARED_LIBRARIES += libccci_util
    endif
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/hardware/ccci/include
endif

# incall handfree DMNR
ifeq ($(MTK_INCALL_HANDSFREE_DMNR),yes)
  LOCAL_CFLAGS += -DMTK_INCALL_HANDSFREE_DMNR
endif

# magic conference
ifeq ($(MTK_MAGICONFERENCE_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_MAGICONFERENCE_SUPPORT
endif

# TTY always support
LOCAL_CFLAGS += -DMTK_TTY_SUPPORT

# tuning at modem side
ifeq ($(strip $(DMNR_TUNNING_AT_MODEMSIDE)),yes)
  LOCAL_CFLAGS += -DDMNR_TUNNING_AT_MODEMSIDE
endif

# Speech Loopback Tunning
ifeq ($(MTK_TC1_FEATURE),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SPH_LPBK_PARAM
else ifeq ($(MTK_TC10_FEATURE),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SPH_LPBK_PARAM
else ifeq ($(MTK_AUDIO_SPH_LPBK_PARAM),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SPH_LPBK_PARAM
endif

# Add MTK_TC10_FEATURE build option
ifeq ($(strip $(MTK_TC10_FEATURE)),yes)
  LOCAL_CFLAGS += -DMTK_TC10_FEATURE=1
  LOCAL_CFLAGS += -DMTK_SPEECH_VOLUME_0_FORCE_AUDIBLE
endif

# Add MTK_TC10_IN_HOUSE
ifeq ($(strip $(MTK_TC10_IN_HOUSE)),yes)
  LOCAL_CFLAGS += -DMTK_TC10_IN_HOUSE
  LOCAL_CFLAGS += -DUPLINK_DROP_POP_MS=120
endif

# MTK Speech ECALL Support
ifeq ($(MTK_SPEECH_ECALL_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_ECALL_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechEcallController.cpp
endif

# MTK Speech DeReverb Support
ifeq ($(MTK_SPEECH_DE_REVERB),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_DE_REVERB
endif

# Gain Tunning
ifeq ($(MTK_TC1_FEATURE),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_GAIN_TABLE_SUPPORT_CDMA
endif

# Sound Balance
ifeq ($(MTK_TC1_FEATURE),yes)
  LOCAL_CFLAGS += -DSOUND_BALANCE_SUPPORT
endif

# Dummy Speech Driver
ifeq ($(strip $(FPGA_EARLY_PORTING)),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_DUMMY
endif

# Speech Parser
ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_PARSER_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechParserBase.cpp
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechShareMemBase.cpp
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechExtMemCCCI.cpp

  ##depend on MD platform
  ifeq ($(MTK_MODEM_PLATFROM),GEN93)
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechParserGen93.cpp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechShareMemGen93.cpp
  else
    LOCAL_CFLAGS += -DMTK_SPEECH_USIP_EMI_SUPPORT
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechParserGen95.cpp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechShareMemGen95.cpp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechExtMemUSIP.cpp
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/AudioSpeechEnhancement/ENH_Parser
  endif
endif

# Speech BT_SPK Dual Path Device (93 after)
ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
  ifeq ($(MTK_BT_SPK_DUAL_PATH_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_BT_SPK_DUAL_PATH_SUPPORT
  endif
endif

### ============================================================================
### MTK Audio Tuning Tool
### ============================================================================

ifneq ($(MTK_AUDIO_TUNING_TOOL_VERSION),)
  ifneq ($(strip $(MTK_AUDIO_TUNING_TOOL_VERSION)),V1)
    MTK_AUDIO_TUNING_TOOL_V2_PHASE := $(shell echo $(MTK_AUDIO_TUNING_TOOL_VERSION) | sed 's/V2.//g')
    LOCAL_CFLAGS += -DMTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
    LOCAL_CFLAGS += -DMTK_AUDIO_TUNING_TOOL_V2_PHASE=$(MTK_AUDIO_TUNING_TOOL_V2_PHASE)
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/AudioParamParser
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/AudioParamParser/include
    LOCAL_C_INCLUDES += $(TOPDIR)external/libxml2/include
    LOCAL_C_INCLUDES += $(TOPDIR)external/icu/icu4c/source/common

    ifneq ($(MTK_AUDIO_TUNING_TOOL_V2_PHASE),1)
      LOCAL_CFLAGS += -DMTK_AUDIO_GAIN_TABLE
      LOCAL_CFLAGS += -DMTK_NEW_VOL_CONTROL

      LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAGainController.cpp
      LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioGainTableParamParser.cpp
      LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechConfig.cpp
      ifneq ($(MTK_COMBO_MODEM_SUPPORT),yes)
        LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechParamParser.cpp
      endif
    endif
  endif
endif

### ============================================================================
### a2dp offload
### ============================================================================

ifeq ($(MTK_A2DP_OFFLOAD_SUPPORT),yes)
LOCAL_SHARED_LIBRARIES += \
    libhidlbase \
    libhidltransport \
    libhwbinder \
    vendor.mediatek.hardware.bluetooth.audio@2.1

LOCAL_CFLAGS += -DMTK_A2DP_OFFLOAD_SUPPORT
endif

### ============================================================================
### FM
### ============================================================================

# support fm audio path by control of setparameters API (default disable)
ifeq ($(MTK_FM_AUDIO_SUPPORT_PARAMETER_CONTROL),yes)
LOCAL_CFLAGS += -DSUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL
endif

### ============================================================================
### BT
### ============================================================================

ifeq ($(MTK_BT_SUPPORT),yes)
  ifeq ($(MTK_BT_PROFILE_A2DP),yes)
  LOCAL_CFLAGS += -DWITH_A2DP
  endif
else
  ifeq ($(strip $(BOARD_HAVE_BLUETOOTH)),yes)
    LOCAL_CFLAGS += -DWITH_A2DP
  endif
endif

# BTCVSD Loopback Test
# LOCAL_CFLAGS += -DMTK_CVSD_LOOPBACK_SUPPORT

### ============================================================================
### Aurisys Framework
### ============================================================================

ifeq ($(strip $(MTK_AURISYS_FRAMEWORK_SUPPORT)),yes)
    LOCAL_CFLAGS += -DMTK_AURISYS_FRAMEWORK_SUPPORT
#    LOCAL_CFLAGS += -DAURISYS_BYPASS_ALL_LIBRARY
#    LOCAL_CFLAGS += -DAURISYS_DUMP_LOG_V
#    LOCAL_CFLAGS += -DAURISYS_DUMP_PCM
#    LOCAL_CFLAGS += -DAURISYS_ENABLE_LATENCY_DEBUG
#    LOCAL_CFLAGS += -DAUDIO_UTIL_PULSE_LEVEL=16000
#    LOCAL_CFLAGS += -DUPLINK_DROP_POP_MS=100
    LOCAL_CFLAGS += -DUPLINK_DROP_POP_MS_FOR_UNPROCESSED=120

    LOCAL_C_INCLUDES += \
        $(TOPDIR)external/libxml2/include/libxml \
        $(TOPDIR)vendor/mediatek/proprietary/external/aurisys/utility \
        $(TOPDIR)vendor/mediatek/proprietary/external/aurisys/interface \
        $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/aurisys/utility \
        $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/aurisys/framework

    LOCAL_SRC_FILES += \
        $(AUDIO_COMMON_DIR)/aurisys/utility/aurisys_utility.c \
        $(AUDIO_COMMON_DIR)/aurisys/utility/aurisys_adb_command.c \
        $(AUDIO_COMMON_DIR)/aurisys/utility/audio_pool_buf_handler.c \
        $(AUDIO_COMMON_DIR)/aurisys/utility/AudioAurisysPcmDump.c \
        $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_config_parser.c \
        $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_controller.c \
        $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_lib_manager.c \
        $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_lib_handler.c \
        $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClientAurisysNormal.cpp
else
    LOCAL_SRC_FILES += \
        $(AUDIO_COMMON_DIR)/aud_drv/AudioMTKFilter.cpp
endif

# ipi message
ifeq ($(MTK_AUDIO_DSP_SERVICE),yes)
    MTK_AUDIO_IPI_SUPPORT = yes
endif

ifeq ($(MTK_AUDIO_IPI_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_AUDIO_IPI_SUPPORT
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/aurisys/interface
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/audio_messenger_ipi.c
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioMessengerIPI.cpp
endif

ifneq ($(filter yes,$(MTK_AUDIODSP_SUPPORT)),)
    LOCAL_CFLAGS += -DMTK_AUDIO_IPI_DMA_SUPPORT
endif

ifeq ($(MTK_AUDIO_DSP_RECOVERY_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_AUDIO_DSP_RECOVERY_SUPPORT
    LOCAL_C_INCLUDES += $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/audiodsp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/audiodsp/audio_dsp_controller.c
endif



# Voice Mixer
ifneq ($(MTK_SPEECH_VOICE_MIXER_SUPPORT),no)
    LOCAL_CFLAGS += -DMTK_SPEECH_VOICE_MIXER_SUPPORT
    LOCAL_SRC_FILES += \
        $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerVoipRx.cpp
endif

### ============================================================================
### HDMI
### ============================================================================

ifeq ($(strip $(MTK_HDMI_MULTI_CHANNEL_SUPPORT)),yes)
  LOCAL_CFLAGS += -DMTK_HDMI_MULTI_CHANNEL_SUPPORT
endif

ifeq ($(MTK_TDM_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_TDM_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerHDMI.cpp
else
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerI2SHDMI.cpp
endif

ifeq ($(strip $(MTK_HDMI_SUPPORT)),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_TVOUT_SUPPORT
  LOCAL_SHARED_LIBRARIES += libdrm
  LOCAL_HEADER_LIBRARIES += \
        mtk_drm_headers
  LOCAL_SRC_FILES += \
      $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerTVOut.cpp
endif

### ============================================================================
### debug
### ============================================================================

# AEE
ifeq ($(HAVE_AEE_FEATURE),yes)
    LOCAL_SHARED_LIBRARIES += libaedv
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/aee/binary/inc
    LOCAL_CFLAGS += -DHAVE_AEE_FEATURE
endif

# Audio Lock 2.0
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng userdebug),)
    LOCAL_CFLAGS += -DMTK_AUDIO_LOCK_ENABLE_TRACE
    #LOCAL_CFLAGS += -DMTK_AUDIO_LOCK_ENABLE_LOG
endif

# dynamic log
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng userdebug),)
    LOCAL_CFLAGS += -DMTK_AUDIO_DYNAMIC_LOG
endif

# non-aee audio lock timeout
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng user),)
LOCAL_CFLAGS += -DUL_NON_AEE_LOCK_TIMEOUT
endif

ifeq ($(strip $(TARGET_BUILD_VARIANT)),eng)
  LOCAL_CFLAGS += -DDEBUG_AUDIO_PCM
  LOCAL_CFLAGS += -DAUDIO_HAL_PROFILE_ENTRY_FUNCTION
endif


# detect pulse
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng userdebug),)
    LOCAL_CFLAGS += -DMTK_LATENCY_DETECT_PULSE
    LOCAL_SHARED_LIBRARIES += libmtkaudio_utils_vendor
    LOCAL_C_INCLUDES += $(TOPDIR)vendor/mediatek/proprietary/external/audio_utils/common_utils/AudioDetectPulse/
endif


### ============================================================================
### regular files
### ============================================================================

LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/utility/audio_lock.c \
    $(AUDIO_COMMON_DIR)/utility/audio_time.c \
    $(AUDIO_COMMON_DIR)/utility/audio_memory_control.c \
    $(AUDIO_COMMON_DIR)/utility/audio_ringbuf.c \
    $(AUDIO_COMMON_DIR)/utility/audio_sample_rate.c \
    $(AUDIO_COMMON_DIR)/utility/audio_fmt_conv_hal.c \
    $(AUDIO_COMMON_DIR)/aud_drv/audio_hw_hal.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioMTKHeadsetMessager.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioUtility.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioFtmBase.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/WCNChipController.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/AudioALSASpeechPhoneCallController.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechDriverFactory.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechDriverDummy.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechEnhancementController.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerBase.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerBGSPlayer.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcm2way.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechUtility.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechMessageID.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAFMController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioSpeechEnhanceInfo.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioSpeechEnhLayer.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioPreProcess.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADriverUtility.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSASampleRateController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAHardware.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADataProcessor.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerNormal.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerFast.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerVoice.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerFMTransmitter.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerBTSCO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerBTCVSD.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerUsb.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerNormal.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerSyncIO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerVoice.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerFMRadio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerBT.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerAEC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerTDM.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClient.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClientSyncIO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderNormal.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoice.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoiceUL.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoiceDL.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoiceMix.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderFMRadio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderBTSCO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderBTCVSD.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRef.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefBTCVSD.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefBTSCO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefExt.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderTDM.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderUsb.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefUsb.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderHAP.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioExternWrapper.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutEarphonePMIC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutSpeakerPMIC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutSpeakerEarphonePMIC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutExtSpeakerAmp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADeviceConfigManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAParamTuner.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/LoopbackManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSALoopbackController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADeviceParser.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioBTCVSDControl.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioVolumeFactory.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/SpeechDataProcessingHandler.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderModemDai.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerModemDai.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSANLEController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioUSBPhoneCallController.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechVMRecorder.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/AudioALSASpeechLoopbackController.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/AudioALSASpeechStreamController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAStreamOut.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAStreamIn.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAHardwareResourceManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAStreamManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioBitTrueTest.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioCustParamClient.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioEventThreadManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioFtm.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioVIBSPKControl.cpp

LOCAL_ARM_MODE := arm
LOCAL_MODULE := audio.primary.$(TARGET_BOARD_PLATFORM)
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_MULTILIB := both

include $(MTK_SHARED_LIBRARY)

### ============================================================================
### common folder Android.mk (aud_policy/client/service/...)
### ============================================================================

include $(CLEAR_VARS)
include $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/Android.mk


### ============================================================================
### adsp header libs
### ============================================================================

ifeq ($(MTK_AUDIODSP_SUPPORT),yes)

include $(CLEAR_VARS)

LOCAL_MODULE := libaudiodsp_headers

LOCAL_EXPORT_C_INCLUDE_DIRS += \
    $(MTK_PATH_SOURCE)/hardware/audio/common/audiodsp

include $(BUILD_HEADER_LIBRARY)

endif


### ============================================================================

endif # end of BOARD_USES_MTK_AUDIO
endif # end of old/new chips

else
# Layer decouple 2.0

AUDIO_COMMON_DIR := common

### ============================================================================
### platform audio HAL
### ============================================================================

LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_CFLAGS += -Werror -Wno-error=undefined-bool-conversion
#LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -fexceptions

LOCAL_CFLAGS += -DMTK_AUDIO_KS
LOCAL_CFLAGS += -DMTK_GENERIC_HAL
MTK_AUDIO_TUNING_TOOL_VERSION=V2.2

# normal capture kernel buffer size
LOCAL_CFLAGS += -DKERNEL_BUFFER_SIZE_UL1_NORMAL=32*1024

# For New DC Trim Calibration Flow
LOCAL_CFLAGS += -DNEW_PMIC_DCTRIM_FLOW

# Record lock time out information
LOCAL_CFLAGS += -DMAX_RAW_DATA_LOCK_TIME_OUT_MS=3000
LOCAL_CFLAGS += -DMAX_PROCESS_DATA_LOCK_TIME_OUT_MS=3000

# low latency
LOCAL_CFLAGS += -DUPLINK_LOW_LATENCY
LOCAL_CFLAGS += -DDOWNLINK_LOW_LATENCY

# aaudio
LOCAL_CFLAGS += -DMTK_AUDIO_AAUDIO_SUPPORT

# POWR HAL Control for low latency & low power
LOCAL_CFLAGS += -DMTK_POWERHAL_AUDIO_SUPPORT
LOCAL_CFLAGS += -DMTK_POWERHAL_AUDIO_UL_LATENCY
LOCAL_CFLAGS += -DMTK_POWERHAL_WIFI_POWRER_SAVE
LOCAL_SHARED_LIBRARIES += \
    libhidlbase \
    vendor.mediatek.hardware.mtkpower@1.0

LOCAL_HEADER_LIBRARIES += libpowerhal_headers

# hifi playback kernel buffer size
LOCAL_CFLAGS += -DKERNEL_BUFFER_SIZE_IN_HIFI_MODE=64*1024

# uncomment for increase hifi playback buffer size
LOCAL_CFLAGS += -DHIFI_DEEP_BUFFER

# Temp tag for FM support WIFI-Display output
LOCAL_CFLAGS += -DMTK_FM_SUPPORT_WFD_OUTPUT

# Playback must be 24bit when using sram
LOCAL_CFLAGS += -DPLAYBACK_USE_24BITS_ONLY

# Record must be 24bit when using sram
LOCAL_CFLAGS += -DRECORD_INPUT_24BITS

# BT
LOCAL_CFLAGS += -DSW_BTCVSD_ENABLE
LOCAL_CFLAGS += -DMTK_SUPPORT_BTCVSD_ALSA
LOCAL_CFLAGS += -DSPH_BT_DELAYTIME_SUPPORT

# HW SRC support
LOCAL_CFLAGS += -DHW_SRC_SUPPORT

# USB
MTK_AUDIO_IEMS_SUPPORT = yes
USB_FIXED_IEMS_PERIOD_US = 5000

# Audio DSP
MTK_AUDIODSP_SUPPORT = yes

### ============================================================================
### include files
### ============================================================================

LOCAL_C_INCLUDES := \
    $(call include-path-for, audio-utils) \
    $(call include-path-for, audio-effects) \
    $(call include-path-for, alsa-utils) \
    $(TOPDIR)external/tinyxml2 \
    $(TOPDIR)external/tinyalsa/include  \
    $(TOPDIR)external/libxml2/include/libxml \
    $(TOPDIR)external/libxml2/include \
    $(TOPDIR)external/tinycompress/include

LOCAL_HEADER_LIBRARIES += \
    libaudiocompensationfilter_headers \
    libaudiocomponentengine_headers \
    libsph_enh_parser_headers \
    libudf_headers \
    libpowerhal_headers \
    ccci_headers \
    libaudio_hal_headers \
    libaudio_uthash_headers \
    audio_util_common_includes \
    audiopolicy_parameters_headers\
    audio_customization_common_headers \
    audio_custom_volume_headers \
    audio_gain_table_param_headers \
    libaud_cust_param_headers \
    audio_cfgfileinc_headers \
    libaudiotoolkit_headers \
    libpower_config_headers
  

### ============================================================================
### library
### ============================================================================

LOCAL_SHARED_LIBRARIES += \
    libc \
    liblog \
    libcutils \
    libutils \
    libalsautils \
    libhardware_legacy \
    libhardware \
    libdl \
    libaudioutils \
    libtinyalsa \
    libtinyxml2 \
    libaudiotoolkit_vendor \
    libmedia_helper \
    libladder \
    android.hardware.audio@7.0 \
    android.hardware.audio.common-util \
    android.hardware.audio.common@7.0 \
    android.hardware.audio.common@7.0-util \
    vendor.mediatek.hardware.audio@7.1

LOCAL_HEADER_LIBRARIES += libaudioclient_headers libaudio_system_headers libmedia_headers audio_hal_common_includes


### ============================================================================
### hardware project config
### ============================================================================

# Primary USB
LOCAL_CFLAGS += -DPRIMARY_USB
LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioUSBCenter.cpp \
                   $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerUsb.cpp

ifeq ($(MTK_AUDIO_DSP_SERVICE),yes)
    LOCAL_CFLAGS += -DMTK_AUDIO_DSP_SERVICE
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/audiodsp/audio_dsp_service.c
endif

### ============================================================================
### speaker
### ============================================================================

#LOCAL_CFLAGS += -DSMARTPA_AUTO_CALIBRATE

ifeq ($(MTK_AUDIO_SCP_RECOVERY_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SCP_RECOVERY_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/audiodsp/audio_scp_service.c
endif

### ============================================================================
### software project config
### ============================================================================

ifeq ($(strip $(MTK_BESLOUDNESS_SUPPORT)),yes)
  LOCAL_CFLAGS += -DMTK_BESLOUDNESS_SUPPORT
ifeq ($(strip $(MTK_BESLOUDNESS_RUN_WITH_HAL)),yes)
  LOCAL_CFLAGS += -DMTK_BESLOUDNESS_RUN_WITH_HAL
endif
endif

ifeq ($(strip $(MTK_BESSURROUND_SUPPORT)),yes)
  LOCAL_CFLAGS += -DMTK_BESSURROUND_SUPPORT
endif

# HIFI audio
LOCAL_CFLAGS += -DMTK_HIFIAUDIO_SUPPORT

# SW Mixer
LOCAL_CFLAGS += -DHAVE_SW_MIXER
LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/utility/audio_sw_mixer.c \
                   $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerMixer.cpp

# In-Ear Monitors (IEMs)
ifeq ($(MTK_AUDIO_IEMS_SUPPORT),yes)
LOCAL_CFLAGS += -DHAVE_IEMS
LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/aud_drv/AudioIEMsController.cpp \
                   $(AUDIO_COMMON_DIR)/aud_drv/AudioALSACaptureDataClientIEMs.cpp \
                   $(AUDIO_COMMON_DIR)/aud_drv/AudioALSACaptureHandlerIEMs.cpp
endif

# Phonecall record voice only
LOCAL_CFLAGS += -DMTK_PHONE_CALL_RECORD_VOICE_ONLY

### ============================================================================
### vow config
### ============================================================================

ifeq ($(MTK_VOW_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_VOW_SUPPORT

  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerVOW.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVOW.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAVoiceWakeUpController.cpp
endif

### ============================================================================
### speech config
### ============================================================================

# MD Platform
MTK_MODEM_PLATFROM = GEN97

# MD1 and MD2 use the same MD
MTK_COMBO_MODEM_SUPPORT = yes

# ccci share memory
MTK_CCCI_SHARE_BUFFER_SUPPORT = yes

# ap sidetone
LOCAL_CFLAGS += -DSPH_AP_SET_SIDETONE
LOCAL_CFLAGS += -DSPH_POSITIVE_SIDETONE_GAIN

# magic clarity
LOCAL_CFLAGS += -DMTK_SPH_MAGICLARITY_SHAPEFIR_SUPPORT

# refactor speech driver after 93 modem
ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_COMBO_MODEM_SUPPORT
  # RTT support after 93 modem
  LOCAL_CFLAGS += -DMTK_RTT_SUPPORT
  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechDriverNormal.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechMessageQueue.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechMessengerNormal.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerTelephonyTx.cpp
else
  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechDriverLAD.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechMessengerECCCI.cpp
endif

# for CCCI share memory EMI (after 92 modem)
ifeq ($(MTK_CCCI_SHARE_BUFFER_SUPPORT),yes)
    ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
        LOCAL_CFLAGS += -DMTK_CCCI_SHARE_BUFFER_SUPPORT # for 93 modem and later
    else
        LOCAL_CFLAGS += -DUSE_CCCI_SHARE_BUFFER # for 92 modem
        LOCAL_SHARED_LIBRARIES += libccci_util
    endif
endif

# incall handfree DMNR
ifeq ($(MTK_INCALL_HANDSFREE_DMNR),yes)
  LOCAL_CFLAGS += -DMTK_INCALL_HANDSFREE_DMNR
endif

# magic conference
ifeq ($(MTK_MAGICONFERENCE_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_MAGICONFERENCE_SUPPORT
endif

# TTY always support
LOCAL_CFLAGS += -DMTK_TTY_SUPPORT

# tuning at modem side
ifeq ($(strip $(DMNR_TUNNING_AT_MODEMSIDE)),yes)
  LOCAL_CFLAGS += -DDMNR_TUNNING_AT_MODEMSIDE
endif

# Speech Loopback Tunning
ifeq ($(MTK_TC1_FEATURE),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SPH_LPBK_PARAM
else ifeq ($(MTK_TC10_FEATURE),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SPH_LPBK_PARAM
else ifeq ($(MTK_AUDIO_SPH_LPBK_PARAM),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_SPH_LPBK_PARAM
endif

# Add MTK_TC10_FEATURE build option
ifeq ($(strip $(MTK_TC10_FEATURE)),yes)
  LOCAL_CFLAGS += -DMTK_TC10_FEATURE=1
  LOCAL_CFLAGS += -DMTK_SPEECH_VOLUME_0_FORCE_AUDIBLE
endif

# Add MTK_TC10_IN_HOUSE
ifeq ($(strip $(MTK_TC10_IN_HOUSE)),yes)
  LOCAL_CFLAGS += -DMTK_TC10_IN_HOUSE
  LOCAL_CFLAGS += -DUPLINK_DROP_POP_MS=120
endif

# MTK Speech ECALL Support
ifeq ($(MTK_SPEECH_ECALL_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_ECALL_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechEcallController.cpp
endif

# MTK Speech DeReverb Support
ifeq ($(MTK_SPEECH_DE_REVERB),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_DE_REVERB
endif

# Gain Tunning
ifeq ($(MTK_TC1_FEATURE),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_GAIN_TABLE_SUPPORT_CDMA
endif

# Dummy Speech Driver
ifeq ($(strip $(FPGA_EARLY_PORTING)),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_DUMMY
endif

# Speech Parser
ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_SPEECH_PARSER_SUPPORT
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechParserBase.cpp
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechShareMemBase.cpp
  LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechExtMemCCCI.cpp

  ##depend on MD platform
  ifeq ($(MTK_MODEM_PLATFROM),GEN93)
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechParserGen93.cpp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechShareMemGen93.cpp
  else
    LOCAL_CFLAGS += -DMTK_SPEECH_USIP_EMI_SUPPORT
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechParserGen95.cpp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechShareMemGen95.cpp
    LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechExtMemUSIP.cpp
  endif
endif

# Speech BT_SPK Dual Path Device (93 after)
ifeq ($(MTK_COMBO_MODEM_SUPPORT),yes)
  ifeq ($(MTK_BT_SPK_DUAL_PATH_SUPPORT),yes)
    LOCAL_CFLAGS += -DMTK_BT_SPK_DUAL_PATH_SUPPORT
  endif
endif

### ============================================================================
### MTK Audio Tuning Tool
### ============================================================================

ifneq ($(MTK_AUDIO_TUNING_TOOL_VERSION),)
  ifneq ($(strip $(MTK_AUDIO_TUNING_TOOL_VERSION)),V1)
    MTK_AUDIO_TUNING_TOOL_V2_PHASE := $(shell echo $(MTK_AUDIO_TUNING_TOOL_VERSION) | sed 's/V2.//g')
    LOCAL_CFLAGS += -DMTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
    LOCAL_CFLAGS += -DMTK_AUDIO_TUNING_TOOL_V2_PHASE=$(MTK_AUDIO_TUNING_TOOL_V2_PHASE)
    LOCAL_HEADER_LIBRARIES += libaudio_param_parser_headers libicuuc_headers 

    ifneq ($(MTK_AUDIO_TUNING_TOOL_V2_PHASE),1)
      LOCAL_CFLAGS += -DMTK_AUDIO_GAIN_TABLE
      LOCAL_CFLAGS += -DMTK_NEW_VOL_CONTROL

      LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAGainController.cpp
      LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioGainTableParamParser.cpp
      LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/speech_driver/SpeechConfig.cpp
      ifneq ($(MTK_COMBO_MODEM_SUPPORT),yes)
        LOCAL_SRC_FILES += $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechParamParser.cpp
      endif
    endif
  endif
endif

### ============================================================================
### a2dp offload
### ============================================================================

ifeq ($(MTK_A2DP_OFFLOAD_SUPPORT),yes)
LOCAL_SHARED_LIBRARIES += \
    libhidlbase \
    libhidltransport \
    libhwbinder \
    vendor.mediatek.hardware.bluetooth.audio@2.1

LOCAL_CFLAGS += -DMTK_A2DP_OFFLOAD_SUPPORT
endif

### ============================================================================
### FM
### ============================================================================

# support fm audio path by control of setparameters API (default disable)
ifeq ($(MTK_FM_AUDIO_SUPPORT_PARAMETER_CONTROL),yes)
LOCAL_CFLAGS += -DSUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL
endif

### ============================================================================
### BT
### ============================================================================

ifeq ($(MTK_BT_SUPPORT),yes)
  ifeq ($(MTK_BT_PROFILE_A2DP),yes)
  LOCAL_CFLAGS += -DWITH_A2DP
  endif
else
  ifeq ($(strip $(BOARD_HAVE_BLUETOOTH)),yes)
    LOCAL_CFLAGS += -DWITH_A2DP
  endif
endif

### ============================================================================
### Aurisys Framework
### ============================================================================

LOCAL_CFLAGS += -DMTK_AURISYS_FRAMEWORK_SUPPORT
LOCAL_CFLAGS += -DUPLINK_DROP_POP_MS_FOR_UNPROCESSED=120

LOCAL_HEADER_LIBRARIES += lib_aurisys_interface libaurisys_hal_headers

LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/aurisys/utility/aurisys_utility.c \
    $(AUDIO_COMMON_DIR)/aurisys/utility/aurisys_adb_command.c \
    $(AUDIO_COMMON_DIR)/aurisys/utility/audio_pool_buf_handler.c \
    $(AUDIO_COMMON_DIR)/aurisys/utility/AudioAurisysPcmDump.c \
    $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_config_parser.c \
    $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_controller.c \
    $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_lib_manager.c \
    $(AUDIO_COMMON_DIR)/aurisys/framework/aurisys_lib_handler.c \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClientAurisysNormal.cpp

# Voice Mixer
ifneq ($(MTK_SPEECH_VOICE_MIXER_SUPPORT),no)
    LOCAL_CFLAGS += -DMTK_SPEECH_VOICE_MIXER_SUPPORT
    LOCAL_SRC_FILES += \
        $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerVoipRx.cpp
endif

### ============================================================================
### Audio DSP
### ============================================================================

ifeq ($(MTK_AUDIODSP_SUPPORT),yes)
  LOCAL_CFLAGS += -DMTK_AUDIO_DSP_SERVICE
  LOCAL_CFLAGS += -DMTK_AUDIO_IPI_SUPPORT
  LOCAL_CFLAGS += -DMTK_AUDIODSP_SUPPORT
  LOCAL_CFLAGS += -DMTK_AUDIO_DSP_RECOVERY_SUPPORT

  LOCAL_HEADER_LIBRARIES += libadsp_hal_headers
  LOCAL_SHARED_LIBRARIES += libtinycompress
  LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/audiodsp/audio_dsp_controller.c \
    $(AUDIO_COMMON_DIR)/audiodsp/audio_dsp_service.c \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/audio_messenger_ipi.c \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioMessengerIPI.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderDspRaw.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClientDsp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerOffload.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerFmAdsp.cpp \
    $(AUDIO_COMMON_DIR)/audiodsp/AudioDspStreamManager.cpp \
    $(AUDIO_COMMON_DIR)/audiodsp/AudioALSAHandlerKtv.cpp \
    $(AUDIO_COMMON_DIR)/audiodsp/AudioDspCallFinal.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechDriverOpenDSP.cpp
endif

### ============================================================================
### debug
### ============================================================================

# AEE
ifeq ($(HAVE_AEE_FEATURE),yes)
    LOCAL_SHARED_LIBRARIES += libaedv
    LOCAL_HEADER_LIBRARIES += libaed_headers
    LOCAL_CFLAGS += -DHAVE_AEE_FEATURE
endif

# Audio Lock 2.0
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng userdebug),)
    LOCAL_CFLAGS += -DMTK_AUDIO_LOCK_ENABLE_TRACE
    #LOCAL_CFLAGS += -DMTK_AUDIO_LOCK_ENABLE_LOG
endif

# dynamic log
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng userdebug),)
    LOCAL_CFLAGS += -DMTK_AUDIO_DYNAMIC_LOG
endif

# non-aee audio lock timeout
ifneq ($(filter $(TARGET_BUILD_VARIANT),eng user),)
LOCAL_CFLAGS += -DUL_NON_AEE_LOCK_TIMEOUT
endif

ifeq ($(strip $(TARGET_BUILD_VARIANT)),eng)
  LOCAL_CFLAGS += -DDEBUG_AUDIO_PCM
  LOCAL_CFLAGS += -DAUDIO_HAL_PROFILE_ENTRY_FUNCTION
endif

# Use Gain index for codec driver gain control
# uncomment this to use Gain DB for for codec driver gain control
LOCAL_CFLAGS += -DGAIN_TABLE_USING_TLV_DB

# DPTX
LOCAL_CFLAGS += -DMTK_AUDIO_TVOUT_SUPPORT
LOCAL_SHARED_LIBRARIES += libdrm
LOCAL_HEADER_LIBRARIES += mtk_drm_headers
LOCAL_SRC_FILES += \
      $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerTVOut.cpp

### ============================================================================
### regular files
### ============================================================================

LOCAL_SRC_FILES += \
    $(AUDIO_COMMON_DIR)/utility/audio_lock.c \
    $(AUDIO_COMMON_DIR)/utility/audio_time.c \
    $(AUDIO_COMMON_DIR)/utility/audio_memory_control.c \
    $(AUDIO_COMMON_DIR)/utility/audio_ringbuf.c \
    $(AUDIO_COMMON_DIR)/utility/audio_sample_rate.c \
    $(AUDIO_COMMON_DIR)/utility/audio_fmt_conv_hal.c \
    $(AUDIO_COMMON_DIR)/aud_drv/audio_hw_hal.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioMTKHeadsetMessager.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioUtility.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioFtmBase.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/WCNChipController.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/AudioALSASpeechPhoneCallController.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechDriverFactory.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechDriverDummy.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechEnhancementController.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerBase.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcmMixerBGSPlayer.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechPcm2way.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechUtility.cpp \
    $(AUDIO_COMMON_DIR)/speech_driver/SpeechMessageID.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAFMController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioSpeechEnhanceInfo.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioSpeechEnhLayer.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioPreProcess.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADriverUtility.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSASampleRateController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAHardware.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADataProcessor.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerNormal.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerFast.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerVoice.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerFMTransmitter.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerBTSCO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerBTCVSD.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerUsb.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerNormal.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerSyncIO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerVoice.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerFMRadio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerBT.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerAEC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerTDM.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClient.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataClientSyncIO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderNormal.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoice.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoiceUL.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoiceDL.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderVoiceMix.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderFMRadio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderBTSCO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderBTCVSD.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRef.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefBTCVSD.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefBTSCO.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefExt.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderTDM.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderUsb.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderEchoRefUsb.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderHAP.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioExternWrapper.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceBase.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutEarphonePMIC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutSpeakerPMIC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutSpeakerEarphonePMIC.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACodecDeviceOutExtSpeakerAmp.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADeviceConfigManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAParamTuner.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/LoopbackManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSALoopbackController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSADeviceParser.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioBTCVSDControl.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioVolumeFactory.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/SpeechDataProcessingHandler.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderModemDai.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerModemDai.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSANLEController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioUSBPhoneCallController.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/SpeechVMRecorder.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/AudioALSASpeechLoopbackController.cpp \
    $(AUDIO_COMMON_DIR)/V3/speech_driver/AudioALSASpeechStreamController.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAStreamOut.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAStreamIn.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAHardwareResourceManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAStreamManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioBitTrueTest.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioCustParamClient.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioEventThreadManager.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioFtm.cpp \
    $(AUDIO_COMMON_DIR)/aud_drv/AudioVIBSPKControl.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureDataProviderAAudio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSACaptureHandlerAAudio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioALSAPlaybackHandlerAAudio.cpp \
    $(AUDIO_COMMON_DIR)/V3/aud_drv/AudioSmartPaController.cpp

LOCAL_ARM_MODE := arm
LOCAL_MODULE := audio.primary.mediatek
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_OWNER := mtk
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_TAGS := optional
LOCAL_MULTILIB := both

include $(MTK_SHARED_LIBRARY)

### ============================================================================
### common folder Android.mk (aud_policy/client/service/...)
### ============================================================================

include $(CLEAR_VARS)
include $(LOCAL_PATH)/$(AUDIO_COMMON_DIR)/Android.mk

endif # end of MTK_GENERIC_HAL
endif # end of MTK_TARGET_PROJECT

