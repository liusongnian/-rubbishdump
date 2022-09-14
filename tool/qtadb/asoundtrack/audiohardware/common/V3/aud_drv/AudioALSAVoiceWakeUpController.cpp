#include "AudioALSAVoiceWakeUpController.h"

#include "AudioLock.h"
#include "AudioALSADriverUtility.h"

#include "AudioMTKHeadsetMessager.h"

#include "AudioCustParamClient.h"
#include "AudioUtility.h"
#include "AudioALSAHardwareResourceManager.h"
#include "AudioALSADeviceConfigManager.h"
#include "AudioALSADeviceParser.h"
#include "AudioALSAStreamManager.h"
#include "AudioALSADeviceParser.h"
#include "AudioALSASampleRateController.h"
#include "AudioSmartPaController.h"
#if defined(MTK_AUDIODSP_SUPPORT)
#include "AudioDspStreamManager.h"
#endif
#include <linux/vow.h>


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAVoiceWakeUpController"
#define VOW_POWER_ON_SLEEP_MS 50
namespace android {

#define XML_VAD_AUDIOTYPE_NAME "VoWHwVad"
#define XML_DSP_AEC_AUDIOTYPE_NAME "VoWDspAec"
#define XML_VOICE_COMMAND_AUDIOTYPE_NAME "VoWVoiceCommand"

/*==============================================================================
 *                     Property keys
 *============================================================================*/
const char *PROPERTY_KEY_VOW_BARGEIN_STATE = "persist.vendor.audiohal.vow_bargein_state";

AudioALSAVoiceWakeUpController *AudioALSAVoiceWakeUpController::mAudioALSAVoiceWakeUpController = NULL;
AudioALSAVoiceWakeUpController *AudioALSAVoiceWakeUpController::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSAVoiceWakeUpController == NULL) {
        mAudioALSAVoiceWakeUpController = new AudioALSAVoiceWakeUpController();
    }
    ASSERT(mAudioALSAVoiceWakeUpController != NULL);
    return mAudioALSAVoiceWakeUpController;
}

/*==============================================================================
 *                     Callback Function
 *============================================================================*/
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
void callbackVowXmlChanged(AppHandle *appHandle, const char *audioTypeName)
{
    AppOps *appOps = appOpsGetInstance();
    ALOGD("+%s(), audioType = %s", __FUNCTION__, audioTypeName);

    if (appOps == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return;
    }
    // reload XML file
    if (appOps->appHandleReloadAudioType(appHandle, audioTypeName) == APP_ERROR) {
        ALOGE("%s(), Reload xml fail!(audioType = %s)", __FUNCTION__, audioTypeName);
    } else {
        if (strcmp(audioTypeName, XML_VAD_AUDIOTYPE_NAME) == 0) {
             AudioALSAVoiceWakeUpController::getInstance()->updateVadParam();
        } else if (strcmp(audioTypeName, XML_DSP_AEC_AUDIOTYPE_NAME) == 0) {
            AudioALSAVoiceWakeUpController::getInstance()->updateDspAecParam();
        } else if (strcmp(audioTypeName, XML_VOICE_COMMAND_AUDIOTYPE_NAME) == 0) {
            AudioALSAVoiceWakeUpController::getInstance()->updateVoiceCommandParam();
        }
    }
}
#endif

AudioALSAVoiceWakeUpController::AudioALSAVoiceWakeUpController() :
    mDebug_Enable(false),
    mMixer(AudioALSADriverUtility::getInstance()->getMixer()),
    mPcm(NULL),
    mEnable(false),
    mWakeupEnableOngoing(false),
    mBargeInEnable(false),
    mBargeInEnableOngoing(false),
    mBargeInBypass(false),
    mBargeInForceOff(false),
    mBargeInPcm(NULL),
    mPcmHostlessUl(NULL),
    mPcmHostlessDl(NULL),
    mIsUseHeadsetMic(false),
    mHardwareResourceManager(AudioALSAHardwareResourceManager::getInstance()),
    mIsNeedToUpdateVadParamToKernel(true) {

    mHandsetMicMode = mHardwareResourceManager->getPhoneMicMode();
    mHeadsetMicMode = mHardwareResourceManager->getHeadsetMicMode();

    mIsSpeakerPlaying = false;
    mSpeakerSampleRate = 48000;  // default speaker sample rate 48K

    memset(&mSrcDlConfig, 0, sizeof(mSrcDlConfig));
    memset(&mSrcUlConfig, 0, sizeof(mSrcUlConfig));

    mFd_vow = 0;
    mFd_vow = ::open("/dev/vow", O_RDONLY);

    //mVOWCaptureDataProvider = AudioALSACaptureDataProviderVOW::getInstance();
    mDeviceConfigManager = AudioALSADeviceConfigManager::getInstance();

    stream_attribute_target = new stream_attribute_t;
    memset(stream_attribute_target, 0, sizeof(stream_attribute_t));
    stream_attribute_target->input_source = AUDIO_SOURCE_HOTWORD;
    // Init input stream attribute here
    stream_attribute_target->audio_mode = AUDIO_MODE_NORMAL; // set mode to stream attribute for mic gain setting
    stream_attribute_target->output_devices = AUDIO_DEVICE_NONE; // set output devices to stream attribute for mic gain setting and BesRecord parameter
    hDumyReadThread = 0;
    mDumpReadStart = false;
    mFd_dnn = -1;
    // BesRecordInfo

    besrecord_info_struct_t besrecord;
    //native_preprocess_info_struct_t nativePreprocess_Info;
    memset(&besrecord, 0, sizeof(besrecord_info_struct_t));
    stream_attribute_target->BesRecord_Info = besrecord;

    stream_attribute_target->BesRecord_Info.besrecord_enable = false; // default set besrecord off
    stream_attribute_target->BesRecord_Info.besrecord_bypass_dualmicprocess = false;  // bypass dual MIC preprocess
    stream_attribute_target->NativePreprocess_Info.PreProcessEffect_Update = false;
    stream_attribute_target->sample_rate = 16000;
    stream_attribute_target->audio_format = AUDIO_FORMAT_PCM_16_BIT;
    //stream_attribute_target->audio_channel_mask = AUDIO_CHANNEL_IN_MONO;
    //mStreamAttributeSource.num_channels = popcount(mStreamAttributeSource.audio_channel_mask);
    //mStreamAttributeSource.sample_rate = 16000;
    ALOGD("%s() , stream_attribute_target->BesRecord_Info.besrecord_enable %d", __FUNCTION__, stream_attribute_target->BesRecord_Info.besrecord_enable);
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    /* Init AppHandle */
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
    } else {
        mAppHandle = appOps->appHandleGetInstance();
        /* XML changed callback process */
        appOps->appHandleRegXmlChangedCb(mAppHandle, callbackVowXmlChanged);
    }
#endif
    // set MTK_VOW_DUAL_MIC_SUPPORT to kernel
    int ioctl_ret = -1;
    if (IsAudioSupportFeature(AUDIO_SUPPORT_VOW_DUAL_MIC)) {
        ioctl_ret = ::ioctl(mFd_vow, VOW_SET_CONTROL, (unsigned int) VOWControlCmd_Mic_Dual);
    } else {
        ioctl_ret = ::ioctl(mFd_vow, VOW_SET_CONTROL, (unsigned int) VOWControlCmd_Mic_Single);
    }
    if (ioctl_ret != 0) {
        ALOGE("%s(), VOW_SET_CONTROL mic error, ioctl_ret = %d", __FUNCTION__, ioctl_ret);
    }

    updateVadParamToKernel();
    updateDspAecParam();
    updateVoiceCommandParam();
}

AudioALSAVoiceWakeUpController::~AudioALSAVoiceWakeUpController() {
    delete stream_attribute_target;
    stream_attribute_target = NULL;
    if (mFd_vow > 0) {
        ::close(mFd_vow);
        mFd_vow = 0;
    }
    ALOGD("%s()", __FUNCTION__);
}

status_t AudioALSAVoiceWakeUpController::SeamlessRecordEnable() {
    int ret = NO_ERROR;

    AL_AUTOLOCK(mSeamlessLock);
    ALOGD("+%s()", __FUNCTION__);
    if (mFd_dnn < 0) {
        mFd_dnn = open("/dev/vow", O_RDONLY);
    }
    if (mFd_dnn < 0) {
        ALOGI("open device fail!%s\n", strerror(errno));
    }

    ret = ::ioctl(mFd_dnn, VOW_SET_CONTROL, (unsigned int)VOWControlCmd_EnableSeamlessRecord);
    if (ret != 0) {
        ALOGE("%s(), VOWControlCmd_EnableHotwordRecord error, ret = %d", __FUNCTION__, ret);
    }

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

bool AudioALSAVoiceWakeUpController::getVoiceWakeUpEnable() {
    AL_AUTOLOCK(mLock);
    return mEnable;
}


status_t AudioALSAVoiceWakeUpController::setVoiceWakeUpEnable(const bool enable) {
    int ret = -1;
    unsigned int temp;
    unsigned int cnt;

    ALOGD("+%s(), mEnable: %d => %d, mIsUseHeadsetMic = %d, mHeadsetMicMode = %d, mHandsetMicMode = %d",
          __FUNCTION__, mEnable, enable, mIsUseHeadsetMic, mHeadsetMicMode, mHandsetMicMode);
    AL_AUTOLOCK(mLock);

    if (mEnable == enable) {
        ALOGW("-%s(), enable(%d) == mEnable(%d), return", __FUNCTION__, enable, mEnable);
        return INVALID_OPERATION;
    }
    mWakeupEnableOngoing = true;

    if (enable == true) {
        unsigned int mic_type = 0;
        unsigned int mtkif_type = 0;

        setVoiceWakeUpDebugDumpEnable(true);

        updateVadParamToKernel();
#if defined(MTK_AUDIO_KS) && defined(MTK_VOW_SUPPORT)
        struct pcm_config config;

        memset(&config, 0, sizeof(config));
        config.rate = 16000;
        config.period_count = 2;
        config.format = PCM_FORMAT_S16_LE;
        config.stop_threshold = ~(0U);
        if (IsAudioSupportFeature(AUDIO_SUPPORT_VOW_DUAL_MIC) && !mIsUseHeadsetMic) {
            config.channels = 2;
            config.period_size = 512;
        } else {
            config.channels = 1;
            config.period_size = 1024;
        }

        mVowChannel = config.channels;

        int cardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmVOWCapture);
        int pcmIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmVOWCapture);


        if (mIsUseHeadsetMic) {
            mDeviceConfigManager->ApplyDeviceTurnonSequenceByName(AUDIO_DEVICE_HEADSET_VOW_MIC);
            // single mic mode, select headset mic.
            setMixerCtrlMicId(MIC_INDEX_HEADSET);
            ALOGD("-%s, VOW1 HEADSET MODE: %d", __FUNCTION__, mHeadsetMicMode);
        } else {
            if (AudioALSADriverUtility::getInstance()->mixerCtrlGetValueByName("DMic Used", 0) > 0) {
                mDeviceConfigManager->ApplyDeviceTurnonSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_MIC);
                mDeviceConfigManager->ApplyDeviceSettingByName(VOWMIC_TYPE_DMICMODE);
                ALOGD("-%s, VOW DMIC MODE: %d", __FUNCTION__, mHandsetMicMode);
            } else {
                if (IsAudioSupportFeature(AUDIO_SUPPORT_VOW_DUAL_MIC)) {
                    mDeviceConfigManager->ApplyDeviceTurnonSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_DUAL_MIC);
                    ALOGD("-%s, VOW DUL MIC MODE: %d", __FUNCTION__, mHandsetMicMode);
                } else {
                    int selectedMicId = getSingleMicId();
                    // single mic mode, set selected mic from ProjectConfig.
                    setMixerCtrlMicId(selectedMicId);
                    if (selectedMicId == MIC_INDEX_MAIN) {
                        mDeviceConfigManager->ApplyDeviceTurnonSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_MIC);
                    } else if (selectedMicId == MIC_INDEX_THIRD) {
                        mDeviceConfigManager->ApplyDeviceTurnonSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_MIC2);
                    } else {
                        ALOGW("%s(), vow single mic mode, unsupported mic index : %d", __FUNCTION__, selectedMicId);
                    }
                    ALOGD("-%s, VOW SINGLE MIC MODE: %d, SELECT MIC: %d", __FUNCTION__, mHandsetMicMode, selectedMicId);
                }
            }
        }

        mPcm = pcm_open(cardIndex, pcmIndex , PCM_IN, &config);
        if (mPcm == NULL || pcm_is_ready(mPcm) == false) {
            ALOGE("%s(), Unable to open pcm device %u (%s)", __FUNCTION__, pcmIndex, pcm_get_error(mPcm));
        } else {
            if (pcm_start(mPcm)) {
                ALOGE("%s(), pcm_start %p fail due to %s", __FUNCTION__, mPcm, pcm_get_error(mPcm));
            }
        }
#else
        mVowChannel = 1;

        //set input MIC type
        if (mIsUseHeadsetMic) {
            //use headset mic
            if (mHeadsetMicMode == AUDIO_MIC_MODE_ACC) {
                if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HeadsetMIC")) {
                    ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HeadsetMIC");
                }
            } else if (mHeadsetMicMode == AUDIO_MIC_MODE_DCC) {
                if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HeadsetMIC_DCC")) {
                    ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HeadsetMIC_DCC");
                }
            } else if (mHeadsetMicMode == AUDIO_MIC_MODE_DCCECMDIFF) {
                if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HeadsetMIC_DCCECM")) {
                    ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value AUDIO_MIC_MODE_DCCECMDIFF");
                }
            } else if (mHeadsetMicMode == AUDIO_MIC_MODE_DCCECMSINGLE) {
                if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HeadsetMIC_DCCECM")) {
                    ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value AUDIO_MIC_MODE_DCCECMSINGLE");
                }
            } else {
                if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HeadsetMIC")) {
                    ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HeadsetMIC");
                }
            }
        } else {
            //DMIC device
            if (IsAudioSupportFeature(AUDIO_SUPPORT_DMIC)) {
                if (mHandsetMicMode == AUDIO_MIC_MODE_DMIC_LP) {
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetDMIC_800K")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HandsetDMIC_800K");
                    }
                } else if(mHandsetMicMode == AUDIO_MIC_MODE_DMIC_VENDOR01) {
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetDMIC_VENDOR01")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HandsetDMIC_VENDOR01");
                    }
                } else { //normal DMIC
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetDMIC")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HandsetDMIC");
                    }
                }
            } else { //analog MIC device
                if (mHandsetMicMode == AUDIO_MIC_MODE_ACC) {
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetAMIC")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HandsetAMIC");
                    }
                } else if (mHandsetMicMode == AUDIO_MIC_MODE_DCC) { //DCC mems mic
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetAMIC_DCC")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HandsetAMIC_DCC");
                    }
                } else if (mHandsetMicMode == AUDIO_MIC_MODE_DCCECMDIFF) { //DCC ecm mic
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetAMIC_DCCECM")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value AUDIO_MIC_MODE_DCCECMDIFF");
                    }
                } else if (mHandsetMicMode == AUDIO_MIC_MODE_DCCECMSINGLE) { //DCC ecm mic
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetAMIC_DCCECM")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value AUDIO_MIC_MODE_DCCECMSINGLE");
                    }
                } else {
                    if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_MIC_Type_Select"), "HandsetAMIC")) {
                        ALOGE("Error: Audio_Vow_MIC_Type_Select invalid value HandsetAMIC");
                    }
                }
            }
        }


        if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_ADC_Func_Switch"), "On")) {
            ALOGE("Error: Audio_Vow_ADC_Func_Switch invalid value");
        }

        usleep(VOW_POWER_ON_SLEEP_MS * 1000);

        if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_Digital_Func_Switch"), "On")) {
            ALOGE("Error: Audio_Vow_Digital_Func_Switch invalid value");
        }
#endif
        mic_type = getVOWMicType();
        switch (mic_type) {
            case AUDIO_MIC_MODE_DMIC:
                mtkif_type = VOW_MTKIF_DMIC;
                break;
            case AUDIO_MIC_MODE_DMIC_LP:
            case AUDIO_MIC_MODE_DMIC_VENDOR01:
                mtkif_type = VOW_MTKIF_DMIC_LP;
                break;
            case AUDIO_MIC_MODE_ACC:
            case AUDIO_MIC_MODE_DCC:
            case AUDIO_MIC_MODE_DCCECMDIFF:
            case AUDIO_MIC_MODE_DCCECMSINGLE:
                mtkif_type = VOW_MTKIF_AMIC;
                break;
            default:
                ALOGI("err: get wrong mic type, need check!\n");
                ASSERT(0);
                break;
        }
        temp = ((mVowChannel - 1) << 4) | mtkif_type;
        ALOGD("%s(), vow ch = %d", __FUNCTION__, mVowChannel);
        ret = ::ioctl(mFd_vow, VOW_RECOG_ENABLE, temp);
        if (ret != 0) {
            ALOGE("%s(), VOW_RECOG_ENABLE error, ret = %d", __FUNCTION__, ret);
        }
        if (mIsSpeakerPlaying == true) {
            cnt = 0;
            while (mBargeInEnableOngoing) {
                usleep(10 * 1000); // wait for BargeInEnable working
                cnt++;
                if (cnt > 300) { // if > 3sec not over then, assert it
                    ASSERT(0);
                    break;
                }
            }
            if (!mBargeInEnable) {
                setBargeInEnable(true);
            }
        }
    } else {
        setVoiceWakeUpDebugDumpEnable(false);
        temp = ((mVowChannel - 1) << 4) | VOW_MTKIF_NONE;
        ALOGD("%s(), vow ch = %d", __FUNCTION__, mVowChannel);
        ret = ::ioctl(mFd_vow, VOW_RECOG_DISABLE, temp);
        if (ret != 0) {
            ALOGE("%s(), VOW_RECOG_DISABLE error, ret = %d", __FUNCTION__, ret);
        }
#if defined(MTK_AUDIO_KS)
        if (mPcm) {
            pcm_stop(mPcm);
            pcm_close(mPcm);
            mPcm = NULL;
        }

        if (mIsUseHeadsetMic) {
            setMixerCtrlMicId(MIC_INDEX_HEADSET);
            mDeviceConfigManager->ApplyDeviceTurnoffSequenceByName(AUDIO_DEVICE_HEADSET_VOW_MIC);
        } else {
            if (IsAudioSupportFeature(AUDIO_SUPPORT_DMIC)) {
                mDeviceConfigManager->ApplyDeviceTurnoffSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_MIC);
            } else {
                if (IsAudioSupportFeature(AUDIO_SUPPORT_VOW_DUAL_MIC)) {
                    mDeviceConfigManager->ApplyDeviceTurnoffSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_DUAL_MIC);
                } else {
                    int selectedMicId = getSingleMicId();
                    setMixerCtrlMicId(selectedMicId);
                    if (selectedMicId == MIC_INDEX_MAIN) {
                       mDeviceConfigManager->ApplyDeviceTurnoffSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_MIC);
                    } else if (selectedMicId == MIC_INDEX_THIRD) {
                       mDeviceConfigManager->ApplyDeviceTurnoffSequenceByName(AUDIO_DEVICE_BUILTIN_MIC_VOW_MIC2);
                    } else {
                       ALOGW("%s(), vow single mic mode, unsupported mic index : %d", __FUNCTION__, selectedMicId);
                    }
                }
            }
        }
#else
        if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_Digital_Func_Switch"), "Off")) {
            ALOGE("Error: Audio_Vow_Digital_Func_Switch invalid value");
        }


        if (mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mMixer, "Audio_Vow_ADC_Func_Switch"), "Off")) {
            ALOGE("Error: Audio_Vow_ADC_Func_Switch invalid value");
        }
#endif
        cnt = 0;
        while (mBargeInEnableOngoing) {
            usleep(10 * 1000); // wait for BargeInEnable working
            cnt++;
            if (cnt > 300) { // if > 3sec not over then, assert it
                ASSERT(0);
                break;
            }
        }
        if (mBargeInEnable) {
            setBargeInEnable(false);
        }
    }

    mEnable = enable;
    mWakeupEnableOngoing = false;

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSAVoiceWakeUpController::updateDeviceInfoForVoiceWakeUp() {

    bool bIsUseHeadsetMic = AudioALSAStreamManager::getInstance()->getDeviceConnectionState(AUDIO_DEVICE_OUT_WIRED_HEADSET);

    if (bIsUseHeadsetMic != mIsUseHeadsetMic) {
        if (mEnable == false) {
            mIsUseHeadsetMic = bIsUseHeadsetMic;
        } else {
            setVoiceWakeUpEnable(false);
            mIsUseHeadsetMic = bIsUseHeadsetMic;
            ALOGD("%s(), mIsUseHeadsetMic = %d", __FUNCTION__, mIsUseHeadsetMic);
            setVoiceWakeUpEnable(true);
        }
    }

    return NO_ERROR;
}

status_t AudioALSAVoiceWakeUpController::updateVadParam() {
    AL_AUTOLOCK(mLock);

    mIsNeedToUpdateVadParamToKernel = true;
    return NO_ERROR;
}

unsigned int AudioALSAVoiceWakeUpController::getVOWMicType() {

    if (mIsUseHeadsetMic) {
        /* Headset */
        return mHeadsetMicMode;
    } else {
        /* DMIC or Handset */
        return mHandsetMicMode;
    }
}

status_t AudioALSAVoiceWakeUpController::updateVadParamToKernel() {
    if (mIsNeedToUpdateVadParamToKernel == true) {
        mIsNeedToUpdateVadParamToKernel = false;

        int vowCfg0 = 0;
        int vowCfg1 = 0;
        int vowCfg2 = 0;
        int vowCfg3 = 0;
        int vowCfg4 = 0;
        int vowCfg5 = 0;
        int vowPeriodicOnOffIdx = 0;

#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
        int custParam[10] = {0}; // all elements 0
        unsigned long long *vowPeriodicOnOffParam;
        unsigned int arraySize = 0;
        struct mixer_ctl *ctl;
        unsigned int i = 0;
        unsigned int *ptr;

        /* Get the vow common parameter from XML */
        AppOps *appOps = appOpsGetInstance();
        if (appOps == NULL) {
            ALOGE("%s(), get appOps fail, line = %d", __FUNCTION__, __LINE__);
            return UNKNOWN_ERROR;
        } else {
            AudioType *audioType;
            // define xml names
            char audioTypeName[] = XML_VAD_AUDIOTYPE_NAME;
            audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, audioTypeName);
            if (!audioType) {
                ALOGE("%s(), get audioType fail, audioTypeName = %s",
                      __FUNCTION__, audioTypeName);
                return BAD_VALUE;
            }
            Param *param = NULL;
            ALOGD("%s(), get vow param from XML: %p", __FUNCTION__, audioType);

            std::string paramCommonPath = "VAD,common";

            ParamUnit *paramUnit;
            paramUnit = appOps->audioTypeGetParamUnit(audioType, paramCommonPath.c_str());
            if (!paramUnit) {
                ALOGD("%s(), get paramUnit fail, paramPath = %s, use common",
                      __FUNCTION__,
                      paramCommonPath.c_str());
                return BAD_VALUE;
            }
            // Read lock
            appOps->audioTypeReadLock(audioType, __FUNCTION__);

            param = appOps->paramUnitGetParamByName(paramUnit, "Par_01");
            if (!param) {
                ALOGE("%s(), get parameter Par_01 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[0] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_02");
            if (!param) {
                ALOGE("%s(), get parameter Par_02 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[1] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_03");
            if (!param) {
                ALOGE("%s(), get parameter Par_03 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[2] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_04");
            if (!param) {
                ALOGE("%s(), get parameter Par_04 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[3] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_05");
            if (!param) {
                ALOGE("%s(), get parameter Par_05 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[4] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_06");
            if (!param) {
                ALOGE("%s(), get parameter Par_06 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[5] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_07");
            if (!param) {
                ALOGE("%s(), get parameter Par_07 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[6] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_08");
            if (!param) {
                ALOGE("%s(), get parameter Par_08 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[7] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_09");
            if (!param) {
                ALOGE("%s(), get parameter Par_09 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[8] = *(int *)param->data;
            param = appOps->paramUnitGetParamByName(paramUnit, "Par_10");
            if (!param) {
                ALOGE("%s(), get parameter Par_10 fail", __FUNCTION__);
                return BAD_VALUE;
            }
            custParam[9] = *(int *)param->data;
            /* Set Preiodic On/Off */
            switch (custParam[9]) {
                case 1:
                    param = appOps->paramUnitGetParamByName(paramUnit, "PeriodicOnOff_90");
                    break;
                case 2:
                    param = appOps->paramUnitGetParamByName(paramUnit, "PeriodicOnOff_80");
                    break;
                case 3:
                    param = appOps->paramUnitGetParamByName(paramUnit, "PeriodicOnOff_70");
                    break;
                case 4:
                    param = appOps->paramUnitGetParamByName(paramUnit, "PeriodicOnOff_60");
                    break;
                case 5:
                    param = appOps->paramUnitGetParamByName(paramUnit, "PeriodicOnOff_50");
                    break;
                default:
                    param = appOps->paramUnitGetParamByName(paramUnit, "PeriodicOnOff_50");
                    break;
            }
            if (!param) {
                ALOGE("%s(), get parameter PeriodicOnOff fail", __FUNCTION__);
                return BAD_VALUE;
            }
            arraySize = (unsigned int)param->arraySize;
            vowPeriodicOnOffParam = new unsigned long long[arraySize];

            ptr = (unsigned int *)param->data;
            for (i = 0; i < arraySize; i++) {
                vowPeriodicOnOffParam[i] = (unsigned long long)*ptr++;
            }
            // Unlock
            appOps->audioTypeUnlock(audioType);

            ALOGD("%s(), vow periodic param size = %d", __FUNCTION__, arraySize);
            if (arraySize == 16) {
                ALOGD("vow Perio= 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x 0x%x",
                      (unsigned int)vowPeriodicOnOffParam[0], (unsigned int)vowPeriodicOnOffParam[1],
                      (unsigned int)vowPeriodicOnOffParam[2], (unsigned int)vowPeriodicOnOffParam[3],
                      (unsigned int)vowPeriodicOnOffParam[4], (unsigned int)vowPeriodicOnOffParam[5],
                      (unsigned int)vowPeriodicOnOffParam[6], (unsigned int)vowPeriodicOnOffParam[7],
                      (unsigned int)vowPeriodicOnOffParam[8], (unsigned int)vowPeriodicOnOffParam[9],
                      (unsigned int)vowPeriodicOnOffParam[10], (unsigned int)vowPeriodicOnOffParam[11],
                      (unsigned int)vowPeriodicOnOffParam[12], (unsigned int)vowPeriodicOnOffParam[13],
                      (unsigned int)vowPeriodicOnOffParam[14], (unsigned int)vowPeriodicOnOffParam[15]);
            }

            if (mixer_ctl_set_array(mixer_get_ctl_by_name(mMixer, "Audio_VOW_Periodic_Param"),
                                                          &vowPeriodicOnOffParam[0],
                                                          sizeof(unsigned long long) * arraySize)) {
                ALOGE("Error: Audio VOW Periodic On Off Param Data invalid value");
            }
            delete[] vowPeriodicOnOffParam;
        }

        ctl = mixer_get_ctl_by_name(mMixer, "Audio VOWCFG4 Data");
        vowCfg4 = mixer_ctl_get_value(ctl, 0);
        ALOGV("%s(), vowCfg4 load = 0x%x", __FUNCTION__, vowCfg4);
        vowCfg0 = 0x0000;
        vowCfg1 = 0x0000;
        vowCfg2 = ((custParam[5] & 0x0007) << 12) |
                  ((custParam[6] & 0x0007) << 8)  |
                  ((custParam[7] & 0x0007) << 4)  |
                  ((custParam[8] & 0x0007));
        vowCfg3 = ((custParam[0] & 0x000f) << 12) |
                  ((custParam[1] & 0x000f) << 8)  |
                  ((custParam[2] & 0x000f) << 4)  |
                  ((custParam[3] & 0x000f));
        vowCfg4 &= 0xFFF0;
        vowCfg4 |= custParam[4];
        vowCfg5 = 0x0001;
        vowPeriodicOnOffIdx = custParam[9];
#endif
        ALOGD("%s(), CFG0=0x%x, CFG1=0x%x, CFG2=0x%x, CF3=0x%x, CFG4=0x%x, CFG5=0x%x, Perio=0x%x",
              __FUNCTION__,
              vowCfg0,
              vowCfg1,
              vowCfg2,
              vowCfg3,
              vowCfg4,
              vowCfg5,
              vowPeriodicOnOffIdx);

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio VOWCFG0 Data"), 0, vowCfg0)) {
            ALOGE("Error: Audio VOWCFG0 Data invalid value");
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio VOWCFG1 Data"), 0, vowCfg1)) {
            ALOGE("Error: Audio VOWCFG1 Data invalid value");
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio VOWCFG2 Data"), 0, vowCfg2)) {
            ALOGE("Error: Audio VOWCFG2 Data invalid value");
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio VOWCFG3 Data"), 0, vowCfg3)) {
            ALOGE("Error: Audio VOWCFG3 Data invalid value");
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio VOWCFG4 Data"), 0, vowCfg4)) {
            ALOGE("Error: Audio VOWCFG4 Data invalid value");
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio VOWCFG5 Data"), 0, vowCfg5)) {
            ALOGE("Error: Audio VOWCFG5 Data invalid value");
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio_VOW_Periodic"), 0, vowPeriodicOnOffIdx)) {
            ALOGE("Error: Audio VOW Periodic On Off Data invalid value");
        }
    }
    return NO_ERROR;
}

status_t AudioALSAVoiceWakeUpController::updateDspAecParam() {
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    char audioTypeName[] = XML_DSP_AEC_AUDIOTYPE_NAME;  // define xml names
    Param *param = NULL;
    AppOps *appOps = appOpsGetInstance();
    if (!appOps) {
        ALOGE("%s(), get appOps fail", __FUNCTION__);
        return BAD_VALUE;
    }
    AudioType *audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, audioTypeName);
    if (!audioType) {
        ALOGE("%s(), get audioType fail, audioTypeName = %s",
                __FUNCTION__, audioTypeName);
        return BAD_VALUE;
    }
    ALOGD("%s(), get vow param from XML: %p", __FUNCTION__, audioType);

    std::string paramCommonPath = "AEC,common";
    ParamUnit *paramUnit;
    paramUnit = appOps->audioTypeGetParamUnit(audioType, paramCommonPath.c_str());
    if (!paramUnit) {
        ALOGE("%s(), get paramUnit fail, paramPath = %s, use common",
                __FUNCTION__,
                paramCommonPath.c_str());
        return BAD_VALUE;
    }

    // Read lock
    appOps->audioTypeReadLock(audioType, __FUNCTION__);

    // get parameter version
    param = appOps->paramUnitGetParamByName(paramUnit, "parameter_version");
    ASSERT(param);
    const unsigned int parameterVersionStrSize = 4;
    char parameterVersion[parameterVersionStrSize] = "";
    size_t xmlParameterStrSize = strlen((char *)param->data);  // unit: Byte
    if (parameterVersionStrSize <= xmlParameterStrSize) {
        ALOGE("%s(), parameter version string too long: %zu",
                __FUNCTION__, xmlParameterStrSize);
        return BAD_VALUE;
    }
    memcpy(parameterVersion, param->data, xmlParameterStrSize);
    ALOGD("%s(), parameterVersion = %s, xmlParameterStrSize = %zu", __FUNCTION__, parameterVersion, xmlParameterStrSize);

    // get parameter content
    param = appOps->paramUnitGetParamByName(paramUnit, "parameter_content");
    ASSERT(param);
    unsigned int parameterContentSize = (unsigned int)param->arraySize;  // unit: Byte
    char *parameterContent = new char[parameterContentSize];
    if (parameterContent == NULL) {
        ALOGE("%s(), allocate parameterContent fail!", __FUNCTION__);
        appOps->audioTypeUnlock(audioType);
        return BAD_VALUE;
    }
    memset((void *)parameterContent, 0, parameterContentSize);
    char *ptr = (char *)param->data;
    ALOGD("%s(), parameterContentSize = %d", __FUNCTION__, parameterContentSize);
    for (int i = 0; i < parameterContentSize; i++) {
        parameterContent[i] = (char)*ptr++;
        ALOGV("%s(), parameterContent[%d] = 0x%x", __FUNCTION__, i, parameterContent[i]);
    }

    // Unlock
    appOps->audioTypeUnlock(audioType);

    // set AEC parameter to kernel
    vow_engine_info_t dataToKernel;
    dataToKernel.return_size_addr = (long)&parameterContentSize;
    dataToKernel.data_addr = (long)parameterContent;
    ALOGV("%s(), ioctl VOW_SET_DSP_AEC_PARAMETER, dataToKernel.data_addr = %lu, dataToKernel.return_size_addr = %lu", __FUNCTION__, dataToKernel.data_addr, dataToKernel.return_size_addr);
    int ret = ::ioctl(mFd_vow, VOW_SET_DSP_AEC_PARAMETER, (unsigned long)&dataToKernel);
    if (ret != 0) {
        ALOGE("%s(), VOW_SET_DSP_AEC_PARAMETER error, ret = %d", __FUNCTION__, ret);
    }

    // release resource
    delete[] parameterContent;

    return NO_ERROR;
#else
    ALOGE("%s(), MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT not support", __FUNCTION__);
    return INVALID_OPERATION;
#endif
}

status_t AudioALSAVoiceWakeUpController::updateVoiceCommandParam() {
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    char audioTypeName[] = XML_VOICE_COMMAND_AUDIOTYPE_NAME;  // define xml names
    Param *param = NULL;
    AppOps *appOps = appOpsGetInstance();
    if (!appOps) {
        ALOGE("%s(), get appOps fail", __FUNCTION__);
        return BAD_VALUE;
    }
    AudioType *audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, audioTypeName);
    if (!audioType) {
        ALOGE("%s(), get audioType fail, audioTypeName = %s",
                __FUNCTION__, audioTypeName);
        return BAD_VALUE;
    }
    ALOGD("%s(), get voice command from XML: %p", __FUNCTION__, audioType);

    std::string paramCommonPath = "VoiceCommand,common";
    ParamUnit *paramUnit;
    paramUnit = appOps->audioTypeGetParamUnit(audioType, paramCommonPath.c_str());
    if (!paramUnit) {
        ALOGE("%s(), get paramUnit fail, paramPath = %s, use common",
                __FUNCTION__,
                paramCommonPath.c_str());
        return BAD_VALUE;
    }

    // Read lock
    appOps->audioTypeReadLock(audioType, __FUNCTION__);

    // get parameter voice command
    param = appOps->paramUnitGetParamByName(paramUnit, "voice_command");
    if (!param) {
        ALOGE("%s(), get parameter voice_command fail", __FUNCTION__);
        return BAD_VALUE;
    }
    mVoiceCommand = String8((char *)param->data);
    ALOGD("%s(), mVoiceCommand = %s", __FUNCTION__, mVoiceCommand.string());

    // get parameter voice command second
    param = appOps->paramUnitGetParamByName(paramUnit, "voice_command_2nd");
    if (!param) {
        ALOGE("%s(), get parameter voice_command_2nd fail", __FUNCTION__);
        return BAD_VALUE;
    }
    mVoiceCommand2nd = String8((char *)param->data);
    ALOGD("%s(), mVoiceCommand2nd = %s", __FUNCTION__, mVoiceCommand2nd.string());

    // get parameter voice command third
    param = appOps->paramUnitGetParamByName(paramUnit, "voice_command_3rd");
    if (!param) {
        ALOGE("%s(), get parameter voice_command_3rd fail", __FUNCTION__);
        return BAD_VALUE;
    }
    mVoiceCommand3rd = String8((char *)param->data);
    ALOGD("%s(), mVoiceCommand3rd = %s", __FUNCTION__, mVoiceCommand3rd.string());

    // Unlock
    appOps->audioTypeUnlock(audioType);
#else
    ALOGE("%s(), MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT not support", __FUNCTION__);
    return INVALID_OPERATION;
#endif
    return NO_ERROR;
}

void *AudioALSAVoiceWakeUpController::dumyReadThread(void *arg) {
    AudioALSAVoiceWakeUpController *pWakeupController = static_cast<AudioALSAVoiceWakeUpController *>(arg);
    short *pbuf = new short[160];

    if (arg == 0) {
       ALOGD("%s(), Error, arg=NULL", __FUNCTION__);
       goto exit;
    }
    if (pWakeupController == 0) {
       ALOGD("%s(), Error, pWakeupController=NULL", __FUNCTION__);
       goto exit;
    }
    if (pWakeupController->hDumyReadThread == 0) {
        goto exit;
    }
    if (!pWakeupController->mCaptureHandler) {
        ALOGD("%s(), Error, mCaptureHandler not here", __FUNCTION__);
        goto exit;
    }
    ALOGD("+%s(), dumyReadThread end, arg=%p", __FUNCTION__, arg);
    while(pWakeupController->mDumpReadStart) {
        pWakeupController->mCaptureHandler->read(&pbuf[0], 320);
        ALOGV("read once");
    }
exit:
    delete[] pbuf;
    ALOGD("-%s(), dumyReadThread end", __FUNCTION__);
    pthread_exit(NULL);
    return NULL;
}

status_t AudioALSAVoiceWakeUpController::setVoiceWakeUpDebugDumpEnable(const bool enable) {
    ALOGD("+%s(), mDebug_Enable: %d => %d", __FUNCTION__, mDebug_Enable, enable);
    status_t status;
    int ret = -1;

    AL_AUTOLOCK(mDebugDumpLock);
    if (mDebug_Enable == enable) {
        ALOGW("-%s(), enable(%d) == mDebug_Enable(%d), return", __FUNCTION__, enable, mDebug_Enable);
        return INVALID_OPERATION;
    }

    char value[PROPERTY_VALUE_MAX] = "";
    property_get(streamin_propty, value, "0");
    int bflag = atoi(value);

    if (bflag && enable) {
        if (!mDebug_Enable) {
            //enable VOW debug dump
            mCaptureHandler = new AudioALSACaptureHandlerVOW(stream_attribute_target);
            status = mCaptureHandler->open();
            if (mDumpReadStart == false) {
                mDumpReadStart = true;
                ret = pthread_create(&hDumyReadThread, NULL, AudioALSAVoiceWakeUpController::dumyReadThread, (void *)this);
                // if pthread_create pass will return 0, otherwise is error message
                if (ret != 0) {
                    ALOGE("%s() create thread fail!!", __FUNCTION__);
                    return UNKNOWN_ERROR;
                }
            }

            ret = ::ioctl(mFd_vow, VOW_SET_CONTROL, (unsigned int)VOWControlCmd_EnableDump);
            ALOGD("%s(), EnableDump set, ret = %d", __FUNCTION__, ret);
            if (ret != 0) {
                ALOGE("%s(), EnableDump error, ret = %d", __FUNCTION__, ret);
            }

            mDebug_Enable = true;
        }
    } else {
        if (mDebug_Enable) {
            //disable VOW debug dump
            if (mDumpReadStart == true) {
                mDumpReadStart = false;
                pthread_join(hDumyReadThread, NULL);
            }
            status = mCaptureHandler->close();
            delete mCaptureHandler;

            ret = ::ioctl(mFd_vow, VOW_SET_CONTROL, (unsigned int)VOWControlCmd_DisableDump);
            ALOGD("%s(), DisableDump set, ret = %d", __FUNCTION__, ret);
            if (ret != 0) {
                ALOGE("%s(), DisableDump error, ret = %d", __FUNCTION__, ret);
            }

            mDebug_Enable = false;
        }
    }
    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

bool AudioALSAVoiceWakeUpController::getVoiceWakeUpStateFromKernel() {
    ALOGD("%s()+", __FUNCTION__);
    bool bRet = false;
    struct mixer_ctl *ctl;
    ctl = mixer_get_ctl_by_name(mMixer, "Audio_VOW_State");
    bRet = mixer_ctl_get_value(ctl, 0);
    ALOGD("%s(), state = 0x%x", __FUNCTION__, bRet);

    return bRet;
}


bool AudioALSAVoiceWakeUpController::updateSpeakerPlaybackStatus(bool isSpeakerPlaying) {
    ALOGD("%s(), isSpeakerPlaying = %d", __FUNCTION__, isSpeakerPlaying);
    bool ret = true;
    unsigned int cnt;
    mIsSpeakerPlaying = isSpeakerPlaying;

    cnt = 0;
    while (mWakeupEnableOngoing) {
        usleep(10 * 1000); // wait for mWakeupEnable working
        cnt++;
        if (cnt > 300) { // if > 3sec not over then, assert it
            ASSERT(0);
            break;
        }
    }

    if (mEnable) {
        cnt = 0;
        while (mBargeInEnableOngoing) {
            usleep(10 * 1000); // wait for BargeInEnable working
            cnt++;
            if (cnt > 300) { // if > 3sec not over then, assert it
                ASSERT(0);
                break;
            }
        }
        if ((isSpeakerPlaying == true) && !mBargeInEnable) {
            ret = setBargeInEnable(true);
        } else if ((isSpeakerPlaying == false) && mBargeInEnable) {
            ret = setBargeInEnable(false);
        }
    }
    return ret;
}

String8 AudioALSAVoiceWakeUpController::getAlexaDspLibVersion(void) {
    uint32_t dspLibVersionSize = 64;
    char dspLibVersion[64] = "";
    vow_engine_info_t libInfoData;
    libInfoData.return_size_addr = (long)&dspLibVersionSize;
    libInfoData.data_addr = (long)dspLibVersion;
    ALOGV("%s(), ioctl VOW_GET_ALEXA_ENGINE_VER, libInfoData.data_addr = %lu, libInfoData.return_size_addr = %lu", __FUNCTION__, libInfoData.data_addr, libInfoData.return_size_addr);
    int ret = ::ioctl(mFd_vow, VOW_GET_ALEXA_ENGINE_VER, (unsigned long)&libInfoData);
    if (ret != 0) {
        ALOGE("%s(), VOW_GET_ALEXA_ENGINE_VER error, ret = %d", __FUNCTION__, ret);
    }
    String8 alexaLibVersion = String8(dspLibVersion);
    ALOGD("%s(), alexaLibVersion = %s, lib string length = %d", __FUNCTION__, alexaLibVersion.string(), dspLibVersionSize);
    return alexaLibVersion;
}

String8 AudioALSAVoiceWakeUpController::getVoiceCommand(VOICE_COMMAND voiceCommand) {
    String8 ret = String8("none");
    switch (voiceCommand) {
    case VOICE_COMMAND_1ST:
        ret = mVoiceCommand;
        break;
    case VOICE_COMMAND_2ND:
        ret = mVoiceCommand2nd;
        break;
    case VOICE_COMMAND_3RD:
        ret = mVoiceCommand3rd;
        break;
    default:
        ALOGE("%s(), invalid voiceCommand = %d, return none", __FUNCTION__, voiceCommand);
        break;
    }
    ALOGD("%s(), voiceCommand = %d, ret = %s", __FUNCTION__, voiceCommand, ret.string());
    return ret;
}

bool AudioALSAVoiceWakeUpController::setSpeakerSampleRate(uint32_t sampleRate) {
    ALOGD("%s(), sampleRate = %d", __FUNCTION__, sampleRate);
    mSpeakerSampleRate = sampleRate;
    return true;
}

void AudioALSAVoiceWakeUpController::setBargeInBypass(const bool enable) {
    ALOGD("+%s(), enable = %d", __FUNCTION__, enable);
    mBargeInBypass = enable;
}

void AudioALSAVoiceWakeUpController::setBargeInForceOff(const bool enable) {
    ALOGD("+%s(), enable = %d", __FUNCTION__, enable);
    mBargeInForceOff = enable;
}

void AudioALSAVoiceWakeUpController::doBargeInRecovery(void) {
    ALOGD("+%s(),", __FUNCTION__);
    bool bargeIn_state = false;
    char value[PROPERTY_VALUE_MAX] = "";
    property_get(PROPERTY_KEY_VOW_BARGEIN_STATE, value, "0");
    bargeIn_state = atoi(value);
    if (bargeIn_state) {
        ALOGD("%s(), bargeIn_state property: %d", __FUNCTION__, bargeIn_state);
        setBargeInEnable(false);
    }
}


bool AudioALSAVoiceWakeUpController::setBargeInEnable(const bool enable) {
    ALOGD("+%s(), enable = %d", __FUNCTION__, enable);
    int ret;
    unsigned int irq_id;
    struct mixer_ctl *ctl;

#if defined(MTK_AUDIO_KS)
    AudioALSASampleRateController *pAudioALSASampleRateController = AudioALSASampleRateController::getInstance();
    AudioSmartPaController *pAudioSmartPaController = AudioSmartPaController::getInstance();
#endif

    if (mBargeInForceOff) {
        ALOGD("-%s(), barge-in is force off, return", __FUNCTION__);
        return true;
    }

    if (mBargeInBypass) {
        ALOGD("-%s(), now is training, then bypass", __FUNCTION__);
        return true;
    }

    if (enable) {
        ASSERT(mBargeInPcm == NULL);
        mBargeInEnableOngoing = true;

        // open and start barge-in PCM device
        int pcmUlIndex = 0;
        int cardUlIndex = 0;
        int pcmUlRet = 0;
        const int interruptIntervalMs = 10; // Interrupt rate from AP to SCP is 10ms

#if defined(MTK_AUDIO_KS)
        int pcmHostlessSrcIndex = 0;
        int cardHostlessSrcIndex = 0;
        int pcmHostlessUlRet = 0;
        int pcmHostlessDlRet = 0;

        pAudioALSASampleRateController->setScenarioStatus(PLAYBACK_SCENARIO_VOW_BARGE_IN);

        mSrcUlConfig.stop_threshold = ~(0U);
        mSrcUlConfig.rate = mSpeakerSampleRate;
        mSrcUlConfig.format = PCM_FORMAT_S16_LE;
        mSrcUlConfig.channels = 2;
        mSrcUlConfig.period_size = interruptIntervalMs * mSrcUlConfig.rate / 1000;
        mSrcUlConfig.period_count = 4;
        mSrcUlConfig.start_threshold = 0;
        mSrcUlConfig.silence_size = 0;
        mSrcUlConfig.silence_threshold = 0;
        mSrcUlConfig.avail_min = 0;

        ALOGD("%s(), format = %d, channels=%d, rate=%d, period_size=%d, period_count=%d", __FUNCTION__,
              mSrcUlConfig.format, mSrcUlConfig.channels, mSrcUlConfig.rate, mSrcUlConfig.period_size, mSrcUlConfig.period_count);

#ifdef MTK_AUDIODSP_SUPPORT
        if (isAdspOptionEnable()) {
            if (pAudioSmartPaController->isSwDspSpkProtect(AUDIO_DEVICE_OUT_SPEAKER)) {
                mBargeInTurnOnSequence = AUDIO_CTL_VOW_BARGE_IN_ECHO_SPEAKER_HIFI3;
            } else if (pAudioSmartPaController->isHwDspSpkProtect(AUDIO_DEVICE_OUT_SPEAKER)) {
                int i2sIn = pAudioSmartPaController->getI2sInSelect();
                pAudioSmartPaController->setI2sHD(true, i2sIn);
                mBargeInTurnOnSequence = AUDIO_CTL_VOW_BARGE_IN_ECHO_DSP_SMARTPA;
            } else {
                mBargeInTurnOnSequence = AUDIO_CTL_VOW_BARGE_IN_ECHO;
            }
        } else
#endif
        {
            if (pAudioSmartPaController->isHwDspSpkProtect(AUDIO_DEVICE_OUT_SPEAKER)) {
                int i2sIn = pAudioSmartPaController->getI2sInSelect();
                pAudioSmartPaController->setI2sHD(true, i2sIn);
                mBargeInTurnOnSequence = AUDIO_CTL_VOW_BARGE_IN_ECHO_DSP_SMARTPA;
            } else {
                mBargeInTurnOnSequence = AUDIO_CTL_VOW_BARGE_IN_ECHO;
            }
        }

        ALOGD("%s(), Interconn %s", __FUNCTION__, mBargeInTurnOnSequence.string());
        mDeviceConfigManager->ApplyDeviceTurnonSequenceByName(mBargeInTurnOnSequence);

        if (pAudioSmartPaController->isHwDspSpkProtect(AUDIO_DEVICE_OUT_SPEAKER)) {
            pcmHostlessSrcIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmHostlessSRCBargein);
            cardHostlessSrcIndex  = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmHostlessSRCBargein);

            mPcmHostlessUl = pcm_open(cardHostlessSrcIndex, pcmHostlessSrcIndex, PCM_IN, &mSrcUlConfig);
            mPcmHostlessDl = pcm_open(cardHostlessSrcIndex, pcmHostlessSrcIndex, PCM_OUT, &mSrcUlConfig);

            if (mPcmHostlessUl == NULL) {
                ALOGE("%s(), mPcmHostlessUl == NULL!! mPcmHostlessUl = %p, pcmUlIndex = %d, cardUlIndex = %d", __FUNCTION__, mPcmHostlessUl, pcmHostlessSrcIndex, cardHostlessSrcIndex);
            } else if (pcm_is_ready(mPcmHostlessUl) == false) {
                ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mPcmHostlessUl, pcm_get_error(mPcmHostlessUl));
                pcm_close(mPcmHostlessUl);
                mPcmHostlessUl = NULL;
            } else {
                pcmHostlessUlRet = pcm_start(mPcmHostlessUl);
            }

            if (mPcmHostlessDl == NULL) {
                ALOGE("%s(), mPcmHostlessDl == NULL!! mPcmHostlessDl = %p, pcmUlIndex = %d, cardUlIndex = %d", __FUNCTION__, mPcmHostlessDl, pcmHostlessSrcIndex, cardHostlessSrcIndex);
            } else if (pcm_is_ready(mPcmHostlessDl) == false) {
                ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mPcmHostlessDl, pcm_get_error(mPcmHostlessDl));
                pcm_close(mPcmHostlessDl);
                mPcmHostlessDl = NULL;
            } else {
                pcmHostlessDlRet = pcm_start(mPcmHostlessDl);
            }
            ALOGD("-%s(),mPcmHostlessUl = %p, pcmHostlessUlRet = %d, mPcmHostlessDl = %p, pcmHostlessDlRet = %d", __FUNCTION__, mPcmHostlessUl, pcmHostlessUlRet, mPcmHostlessDl, pcmHostlessDlRet);
        }

        pcmUlIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture2);
        cardUlIndex  = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCapture2);

#else
        pcmUlIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmVOWBargeInCapture);
        cardUlIndex  = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmVOWBargeInCapture);
#endif

        memset((void *)&mSrcDlConfig, 0, sizeof(mSrcDlConfig));
        mSrcDlConfig.channels = 2;
#ifdef VOW_ECHO_SW_SRC
        mSrcDlConfig.rate = 48000;
#else
        mSrcDlConfig.rate = 16000;
#endif
        mSrcDlConfig.format = PCM_FORMAT_S16_LE;

        //mSrcDlConfig.period_size = 640;
        mSrcDlConfig.period_size = interruptIntervalMs * mSrcDlConfig.rate / 1000;
        mSrcDlConfig.period_count = 4; // Allocate 40ms DMA buffer

        mSrcDlConfig.start_threshold = 0;
        mSrcDlConfig.stop_threshold = ~(0U); // Set some AFE RG at SCP
        mSrcDlConfig.silence_threshold = 0;

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Vow_bargein_echo_ref"), 0, 1)) {
            ALOGE("Error: Vow_bargein_echo_ref");
        }
        // let audio use scp reserved memory
        mBargeInPcm = pcm_open(cardUlIndex, pcmUlIndex, PCM_IN, &mSrcDlConfig);
        if (mBargeInPcm == NULL) {
            ALOGE("%s(), mBargeInPcm == NULL!! mBargeInPcm = %p, pcmUlIndex = %d, cardUlIndex = %d", __FUNCTION__, mBargeInPcm, pcmUlIndex, cardUlIndex);
        } else if (pcm_is_ready(mBargeInPcm) == false) {
            ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mBargeInPcm, pcm_get_error(mBargeInPcm));
            pcm_close(mBargeInPcm);
            mBargeInPcm = NULL;
        } else {
            pcmUlRet = pcm_start(mBargeInPcm);
        }
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Vow_bargein_echo_ref"), 0, 0)) {
            ALOGE("Error: Vow_bargein_echo_ref");
        }
        ALOGD("%s(), mBargeInPcm = %p, pcmUlRet = %d", __FUNCTION__, mBargeInPcm, pcmUlRet);
        ASSERT(mBargeInPcm != NULL);
        ctl = mixer_get_ctl_by_name(mMixer, "vow_barge_in_irq_id");
        irq_id = mixer_ctl_get_value(ctl, 0);
        ret = ::ioctl(mFd_vow, VOW_BARGEIN_ON, irq_id);
        ALOGD("%s(), VOW_BARGEIN_ON set, irq = %d, ret = %d", __FUNCTION__, irq_id, ret);
        if (ret != 0) {
            ALOGE("%s(), VOW_BARGEIN_ON error, ret = %d", __FUNCTION__, ret);
        }
        mBargeInEnable = true;
        mBargeInEnableOngoing = false;
        property_set(PROPERTY_KEY_VOW_BARGEIN_STATE, "1");
    } else {
        mBargeInEnableOngoing = true;
        ctl = mixer_get_ctl_by_name(mMixer, "vow_barge_in_irq_id");
        irq_id = mixer_ctl_get_value(ctl, 0);
        ret = ::ioctl(mFd_vow, VOW_BARGEIN_OFF, irq_id);
        ALOGD("%s(), VOW_BARGEIN_OFF set, irq = %d, ret = %d", __FUNCTION__, irq_id, ret);
        if (mBargeInPcm != NULL) {
            if (ret != 0) {
                ALOGE("%s(), VOW_BARGEIN_OFF error, ret = %d", __FUNCTION__, ret);
            }
            pcm_stop(mBargeInPcm);
            pcm_close(mBargeInPcm);
            if (mPcmHostlessUl != NULL) {
                pcm_stop(mPcmHostlessUl);
                pcm_close(mPcmHostlessUl);
            }
            if (mPcmHostlessDl != NULL) {
                pcm_stop(mPcmHostlessDl);
                pcm_close(mPcmHostlessDl);
            }

#if defined(MTK_AUDIO_KS)
            mDeviceConfigManager->ApplyDeviceTurnoffSequenceByName(mBargeInTurnOnSequence);
            if (pAudioSmartPaController->isHwDspSpkProtect(AUDIO_DEVICE_OUT_SPEAKER)) {
                pAudioSmartPaController->setI2sHD(false, pAudioSmartPaController->getI2sInSelect());
            }
            pAudioALSASampleRateController->resetScenarioStatus(PLAYBACK_SCENARIO_VOW_BARGE_IN);
#endif

            mBargeInPcm = NULL;
        }
        mBargeInEnable = false;
        mBargeInEnableOngoing = false;
        property_set(PROPERTY_KEY_VOW_BARGEIN_STATE, "0");
    }
    ALOGD("-%s(), mBargeInPcm = %p, mBargeInEnable = %d", __FUNCTION__, mBargeInPcm, mBargeInEnable);

    return true;
}

int AudioALSAVoiceWakeUpController::getSingleMicId() {
    const char *micIndexString = appGetFeatureOptionValue("MTK_VOW_SINGLE_MIC_SELECT");
    if (micIndexString != NULL && *micIndexString !='\0') {
        int micIndex = atoi(micIndexString);
        if (micIndex < MIC_INDEX_NUM && micIndex > MIC_INDEX_IDLE)
            return micIndex;
        else
            return MIC_INDEX_THIRD; // Default third mic.
    } else {
       ALOGW("-%s(), MTK_VOW_SINGLE_MIC_SELECT not set, use default MIC_INDEX_THIRD.",
            __FUNCTION__);
       return MIC_INDEX_THIRD;
    }
}

int AudioALSAVoiceWakeUpController::setMixerCtrlMicId(const int mic_id) {
    int ret = mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "Audio_Vow_SINGLE_MIC_Select"), 0, mic_id);
    if (ret) {
        ALOGW("%s() fail, %s id, value %d", __FUNCTION__, "Audio_Vow_SINGLE_MIC_Select", mic_id);
    }
    return ret;
}

} // end of namespace android
