#include "AudioALSAUltrasoundOutOfCallController.h"

#include <tinyalsa/asoundlib.h>

#include "AudioALSADriverUtility.h"
#include "AudioALSADeviceParser.h"
#include "AudioALSAStreamManager.h"
#include "AudioALSADeviceConfigManager.h"
#include "AudioSmartPaController.h"
#include "AudioALSAGainController.h"
#include "AudioGainTableParamParser.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "AudioALSAUltrasoundOutOfCallController"

#define MIXER_CTL_NAME_ULTRASOUND_ENABLE         "mtk_scp_ultra_engine_state"
#define MIXER_CTL_NAME_ULTRASOUND_GAIN           "mtk_scp_ultra_gain_config"
#define MIXER_CTL_NAME_ULTRASOUND_PARAM          "mtk_scp_ultra_param_config"

#define MIXER_CTL_NAME_ULTRASOUND_RAMP_DOWN      "Ultrasound RampDown"
#define MIXER_CTL_NAME_ULTRASOUND_SUSPEND        "Ultrasound Suspend"
#define MIXER_CTL_NAME_ULTRASOUND_USND_CHANNEL   "Ultrasound Custom Setting 1"
#define MIXER_CTL_NAME_ULTRASOUND_RX_PORT   "Ultrasound Rx Port"

#define ULTRASOUND_FUNCTION_NAME    "Ultrasound"

// Define engine mode, normal mode or calibration mode.
const char* PROPERTY_ULTRASOUND_MODE = "vendor.audio.ultrasound.mode";
// Engine mode mixer control
#define MIXER_CTRL_NAME_ULTRASOUND_MODE "Ultrasound Mode"

// Only enable in product line
//#define SUPPORT_FACTORY_PCT


enum {
    MUTE_THREAD_STATE_IDLE,
    MUTE_THREAD_STATE_MUTING
};

enum {
    MUTE_CTRL_IDLE,
    MUTE_CTRL_START,
    MUTE_CTRL_STOP,
};

enum {
    DELAY_START_THREAD_IDLE,
    DELAY_START_THREAD_SLEEPING,
};

enum {
    DELAY_START_CTRL_IDLE,
    DELAY_START_CTRL_START,
    DELAY_START_CTRL_STOP,
};

enum {
    USND_ENGINE_STATE_ON,
    USND_ENGINE_STATE_START,
    USND_ENGINE_STATE_STOP,
    USND_ENGINE_STATE_OFF
};


const std::string paramCommonPath = "UltrasoundParam,Common";
const char *ultrasound2In1 = "2in1_speaker";
const char *rampdownDelay = "ultrasound_rampdown_delay";
const char *delayStartDuration = "delay_start_duration";
const char *muteDuration = "mute_duration";


namespace android
{

void ultrasoundXmlChangedCallback(AppHandle *_appHandle, const char *_audioTypeName) {
    // reload XML file
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGD("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return;
    }

    if (appOps->appHandleReloadAudioType(_appHandle, _audioTypeName) == APP_ERROR) {
        ALOGD("%s(), Reload xml fail!(audioType = %s)", __FUNCTION__, _audioTypeName);
    } else {
        AudioALSAUltrasoundOutOfCallController::getInstance()->updateXmlParam(_audioTypeName);
    }
}

void outputDevicesChangeCb(const audio_devices_t device, const DeviceStatus status) {
    ALOGV("%s(), device=%d, status = %d", __FUNCTION__, device, status);
    AudioALSAUltrasoundOutOfCallController::getInstance()->ouputDeviceChanged(device, status);
}

void UltrasoundDeviceConfig::Dump() {
    ALOGV("msPeriod:%d,rateIn:%d,rateOut:%d,channelsIn:%d,channelsOut:%d,OutChannel:%d,rampdon:%d",
          msPerPeriod, rateIn, rateOut, channelsIn, channelsOut, usndOutChannel, ramp_down_delay);
}

AudioALSAUltrasoundOutOfCallController *AudioALSAUltrasoundOutOfCallController::mInstance = NULL;
AudioALSAUltrasoundOutOfCallController *AudioALSAUltrasoundOutOfCallController::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mInstance == NULL) {
        mInstance = new AudioALSAUltrasoundOutOfCallController();
    }
    ASSERT(mInstance != NULL);
    return mInstance;
}

AudioALSAUltrasoundOutOfCallController::AudioALSAUltrasoundOutOfCallController() :
    mHardwareResourceManager(AudioALSAHardwareResourceManager::getInstance()),
    mPcmDL(NULL),
    mPcmUL(NULL),
    mMixer(AudioALSADriverUtility::getInstance()->getMixer()),
    mUsndState(USND_STATE_IDLE),
    mAvailableOutputDevices(AUDIO_DEVICE_NONE),
    mAvailableInputDevices(AUDIO_DEVICE_NONE) {
    memset(&mGainConfigToSCP, 0, sizeof(mGainConfigToSCP));
    memset(&mParam, 0, sizeof(struct ultra_audio_param));
    mPhoneCallRouting = false;
    mInputDeviceRouting = false;
    mOutputDeviceRouting = false;
    mTargetOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
    mTargetInputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
    mOpenedInputDevice = AUDIO_DEVICE_NONE;
    mOpenedOutputDevice = AUDIO_DEVICE_NONE;
    mDeviceOpened = false;
    mLogFlag = property_get_int32(ultrasound_log_propty, 3);
    mWaitingForTargetOutputOn = false;
    mWaitingOutputDevice = AUDIO_DEVICE_NONE;
    mEngineSuspended = false;
    mMuteDlForRoutingCtrl = MUTE_CTRL_IDLE;
    mMuteDlForRoutingState = MUTE_THREAD_STATE_IDLE;
    mDelayStartUsndState = DELAY_START_THREAD_IDLE;
    mDelayStartUsndCtrl = DELAY_START_CTRL_IDLE;
    mDelayStartUsndThreadEnable = false;
    mMuteDlForRoutingThreadEnable = false;
    mHardwareResourceManager->setOutputDevicesChangeCb(outputDevicesChangeCb);
    mUsndSupported = (mixer_get_ctl_by_name(mMixer, MIXER_CTL_NAME_ULTRASOUND_ENABLE) == NULL) ? false : true;
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGD("Error %s %d", __FUNCTION__, __LINE__);
    } else {
        appOps->appHandleRegXmlChangedCb(appOps->appHandleGetInstance(), ultrasoundXmlChangedCallback);
    }
    loadUltrasoundParam();
}

AudioALSAUltrasoundOutOfCallController::~AudioALSAUltrasoundOutOfCallController() {
    mHardwareResourceManager->setOutputDevicesChangeCb(NULL);
    close();
}

bool AudioALSAUltrasoundOutOfCallController::isEnabled() {
    return mUsndState >= USND_STATE_ENABLED;
}

bool AudioALSAUltrasoundOutOfCallController::isStarted() {
    return mUsndState == USND_STATE_STARTED;
}

bool AudioALSAUltrasoundOutOfCallController::isUsndSupported() {
    return mUsndSupported;
}

void AudioALSAUltrasoundOutOfCallController::SendMixerControl(const char *mixer_ctrl_name,
                                                              int value) {
    struct mixer_ctl *ctl;
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
        "%s(), ctrl:%s value:0x%x", __FUNCTION__, mixer_ctrl_name, value);
    ctl = mixer_get_ctl_by_name(mMixer, mixer_ctrl_name);
    if (ctl != NULL) {
        int retval = mixer_ctl_set_value(ctl, 0, value);
    } else {
        ALOGE("%s(), ctrl:%s not support", __FUNCTION__, mixer_ctrl_name);
    }
}

void AudioALSAUltrasoundOutOfCallController::setUsecaseName(const char *name) {
    strncpy(mUsecaseName, name, MAX_USECASE_NAME);
    sprintf(mUsecaseSpkrName, "%s-speaker", name);
}

void AudioALSAUltrasoundOutOfCallController::updateAnalogGain(bool nolock) {
    if (!nolock) {
        AL_AUTOLOCK(mUltraSndLock);
    }
    if (!isStarted()) {
        return;
    }
    GainTableSpec *spec;
    uint32_t micGainIdx = AudioMTKGainController::getInstance()->GetMicGain();
    uint32_t receiverGainIdx = AudioMTKGainController::getInstance()->GetReceiverGain();
    uint32_t speakerGainIdx = AudioMTKGainController::getInstance()->GetSPKGain();
    GainTableParamParser::getInstance()->getGainTableSpec(&spec);
    audio_devices_t outpuDevice =mHardwareResourceManager->getOutputDevice();
    //mic gain
    GAIN_DEVICE gainDevice;
    if (outpuDevice == AUDIO_DEVICE_NONE) {
        gainDevice = AudioMTKGainController::getInstance()->getGainDevice(mHardwareResourceManager->getInputDevice());
    } else {
        gainDevice = AudioMTKGainController::getInstance()->getGainDevice(outpuDevice);
    }
    if (micGainIdx > spec->ulPgaGainMap[gainDevice].size()) {
        micGainIdx = spec->ulPgaGainMap[gainDevice].size() - 1;
    }
    if (micGainIdx < 0) {
        micGainIdx = 0;
    }
    int mic_gain = micGainIdx * spec->ulHwPgaIdxMax;
    int speaker_receiver_gain = 0;
    if  (mParam.speaker_2in1 == NO_SPEAKER_2IN1) {
        if (receiverGainIdx > spec->voiceBufferGainDb.size()) {
            receiverGainIdx = spec->voiceBufferGainDb.size() - 1;
        }
        if (receiverGainIdx < 0) {
            receiverGainIdx = 0;
        }
        speaker_receiver_gain = spec->voiceBufferGainDb[receiverGainIdx];
    } else if ((mParam.speaker_2in1 == SPEAKER_2IN1_ANALOG_PA) && outpuDevice != AUDIO_DEVICE_NONE &&
            (AudioMTKGainController::getInstance()->isSpeakerCategory(gainDevice) ||
            AudioMTKGainController::getInstance()->isEarpieceCategory(gainDevice))) {
        std::vector<short> *enumGainDb;
        switch (spec->spkAnaType) {
        case GAIN_ANA_HEADPHONE:
            enumGainDb = &spec->audioBufferGainDb;
            break;
        case GAIN_ANA_LINEOUT:
            enumGainDb = &spec->lineoutBufferGainDb;
            break;
        case GAIN_ANA_SPEAKER:
        default:
            enumGainDb = &spec->spkGainDb;
            break;
        }
        if (speakerGainIdx > enumGainDb->size()) {
            speakerGainIdx = enumGainDb->size() - 1;
        }
        if (speakerGainIdx < 0) {
            speakerGainIdx = 0;
        }
        speaker_receiver_gain = (*enumGainDb)[speakerGainIdx];
    } else if ((mParam.speaker_2in1 == SPEAKER_2IN1_SMART_PA) && outpuDevice != AUDIO_DEVICE_NONE &&
            (AudioMTKGainController::getInstance()->isSpeakerCategory(gainDevice) ||
            AudioMTKGainController::getInstance()->isEarpieceCategory(gainDevice))) {
        speaker_receiver_gain = 0;
    } else if ((mParam.speaker_2in1 == SPEAKER_2IN1_SMART_PA_ANALOG_EARPIECE) && outpuDevice != AUDIO_DEVICE_NONE) {
        if (AudioMTKGainController::getInstance()->isSpeakerCategory(gainDevice)) {
            speaker_receiver_gain = 0;
        }
        else if (AudioMTKGainController::getInstance()->isEarpieceCategory(gainDevice)) {
            if (receiverGainIdx > spec->voiceBufferGainDb.size()) {
                receiverGainIdx = spec->voiceBufferGainDb.size() - 1;
            }
            if (receiverGainIdx < 0) {
                receiverGainIdx = 0;
            }
            speaker_receiver_gain = spec->voiceBufferGainDb[receiverGainIdx];
        }
    }
    if (mGainConfigToSCP.receiver_gain != speaker_receiver_gain || mGainConfigToSCP.mic_gain != mic_gain) {
        struct mixer_ctl *ctl;
        mGainConfigToSCP.receiver_gain = speaker_receiver_gain;
        mGainConfigToSCP.mic_gain = mic_gain;
        ALOGD("%s(), micGain=%d, speaker_receiver_gain=%d, gainDevice=%d",
                __FUNCTION__, mic_gain, speaker_receiver_gain, gainDevice);
        ctl = mixer_get_ctl_by_name(mMixer, MIXER_CTL_NAME_ULTRASOUND_GAIN);
        if (ctl == NULL) {
            ALOGE("mtk_scp_ultra_gain_config not support");
            return;
        }
        mixer_ctl_set_array(ctl, &mGainConfigToSCP, sizeof(mGainConfigToSCP));
    }
}

status_t AudioALSAUltrasoundOutOfCallController::openScpUltraPcmDriverWithFlag
                                                       (const unsigned int device,
                                                        unsigned int flag) {
    struct pcm *mScpUltraPcm = NULL;

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
        "+%s(), pcm device = %d, flag = 0x%x", __FUNCTION__, device, flag);

    if (flag & PCM_IN) {
        ASSERT(mScpUltraPcm == NULL);
        mPcmUL = pcm_open(AudioALSADeviceParser::getInstance()->GetCardIndex(),
                                  device, flag, &mConfig);
        mScpUltraPcm = mPcmUL;
        ASSERT(mScpUltraPcm != NULL);
    } else {
        ASSERT(mScpUltraPcm == NULL);
        mPcmDL = pcm_open(AudioALSADeviceParser::getInstance()->GetCardIndex(),
                                  device, flag, &mOutConfig);
        mScpUltraPcm = mPcmDL;
        ASSERT(mScpUltraPcm != NULL);
    }

    if (mScpUltraPcm == NULL) {
        ALOGE("%s(),  pcm_open() failed due to memory allocation", __FUNCTION__);
    } else if (pcm_is_ready(mScpUltraPcm) == false) {
        ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.",
              __FUNCTION__, mScpUltraPcm, pcm_get_error(mScpUltraPcm));
        pcm_close(mScpUltraPcm);
        mScpUltraPcm = NULL;
    } else if (pcm_prepare(mScpUltraPcm) != 0) {
        ALOGE("%s(), pcm_prepare(%p) == false due to %s, close pcm.",
              __FUNCTION__, mScpUltraPcm, pcm_get_error(mScpUltraPcm));
        pcm_close(mScpUltraPcm);
        mScpUltraPcm = NULL;
    }

    ASSERT(mScpUltraPcm != NULL);
    if (pcm_start(mScpUltraPcm) != 0) {
        ALOGE("%s(), pcm_start(%p) == false due to %s.",
              __FUNCTION__, mScpUltraPcm, pcm_get_error(mScpUltraPcm));
    }

    return NO_ERROR;
}

bool AudioALSAUltrasoundOutOfCallController::isCodecOutputDevices(audio_devices_t devices) {
    return ((devices & AUDIO_DEVICE_BIT_IN) == 0) &&
           (!!(devices & AUDIO_DEVICE_OUT_SPEAKER) ||
           !!(devices & AUDIO_DEVICE_OUT_EARPIECE) ||
           !!(devices & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
           !!(devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE));
}

bool AudioALSAUltrasoundOutOfCallController::isCodecInputDevices(audio_devices_t devices) {
    return ((devices & AUDIO_DEVICE_BIT_IN) != 0) &&
           (((devices & AUDIO_DEVICE_IN_BUILTIN_MIC) == AUDIO_DEVICE_IN_BUILTIN_MIC) ||
           ((devices & AUDIO_DEVICE_IN_BACK_MIC) == AUDIO_DEVICE_IN_BACK_MIC) ||
           ((devices & AUDIO_DEVICE_IN_WIRED_HEADSET) == AUDIO_DEVICE_IN_WIRED_HEADSET));
}

bool AudioALSAUltrasoundOutOfCallController::isHeadPhoneConnected() {
    return !!(mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE) ||
        !!(mAvailableOutputDevices & AUDIO_DEVICE_OUT_WIRED_HEADSET) ||
        !!((mAvailableInputDevices & AUDIO_DEVICE_IN_WIRED_HEADSET) == AUDIO_DEVICE_IN_WIRED_HEADSET);
}

void AudioALSAUltrasoundOutOfCallController::beforeInputDeviceRouting(
        const audio_devices_t input_device) {
    AL_AUTOLOCK(mUltraSndLock);
    mInputDeviceRouting = true;

    bool isCodecInputDevice = isCodecInputDevices(input_device);
    audio_devices_t newPmicInputDevice = (audio_devices_t)(input_device &
            (AUDIO_DEVICE_IN_BUILTIN_MIC |
            AUDIO_DEVICE_IN_BACK_MIC |
            AUDIO_DEVICE_IN_WIRED_HEADSET));

    if (isStarted() && isCodecInputDevice) {
        ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
                 "%s(), mTargetInputDevice=0x%x, newPmicInputDevice=0x%x, mUsndState=%d",
                 __FUNCTION__, mTargetInputDevice, newPmicInputDevice, mUsndState);
        if (mTargetInputDevice != newPmicInputDevice) {
            if (mUsndState == USND_STATE_STARTED) {
                stop(true, true);
            }
        }
    }

    if (isCodecInputDevice) {
        if (newPmicInputDevice != AUDIO_DEVICE_NONE && newPmicInputDevice != AUDIO_DEVICE_BIT_IN) {
            mTargetInputDevice = newPmicInputDevice;
        } else {
            mTargetInputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
    }

    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
          "%s(), targetInput=0x%x,input_device=0x%x,mUsndState=%d",
          __FUNCTION__, mTargetInputDevice, input_device, mUsndState);
}

void AudioALSAUltrasoundOutOfCallController::afterInputDeviceRouting(
        const audio_devices_t input_device) {
    AL_AUTOLOCK(mUltraSndLock);
    mInputDeviceRouting = false;
    updateUltrasoundState(true);
    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
          "%s(), mTargetInputDevice=0x%x,input_device=0x%x,mUsndState=%d,isCodecInputDevices=%d",
          __FUNCTION__, mTargetInputDevice, input_device, mUsndState,
          isCodecInputDevices(input_device));
}

void AudioALSAUltrasoundOutOfCallController::beforeOutputDeviceRouting(
        const audio_devices_t current_output_devices,
        const audio_devices_t output_device) {
    AL_AUTOLOCK(mUltraSndLock);
    mOutputDeviceRouting = true;
    audio_devices_t curOutputDevice = mHardwareResourceManager->getOutputDevice();

    // Calculate new target output device
    if (mParam.speaker_2in1) {
        if ((output_device & AUDIO_DEVICE_OUT_EARPIECE) == AUDIO_DEVICE_OUT_EARPIECE) {
            mTargetOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
        } else if ((output_device & AUDIO_DEVICE_OUT_SPEAKER) == AUDIO_DEVICE_OUT_SPEAKER) {
            mTargetOutputDevice = AUDIO_DEVICE_OUT_SPEAKER;
        } else {
            mTargetOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
        }
    }

    // If usnd enable and user routing to another output device, then suspend engine and
    // resume after new output device opened
    if (isStarted() && (current_output_devices != output_device) &&
            (curOutputDevice != output_device) &&
            (!mParam.speaker_2in1 || (mParam.speaker_2in1 && output_device != mOpenedOutputDevice))) {

        // Cancel delay start request first
        delayStartUsndAsyncRequest(false);

        // non-codec output device will not start by HardwareResourceManager, should not
        // wait for it's start notify.
        if (isCodecOutputDevices(output_device)) {
            mWaitingForTargetOutputOn = true;
            mWaitingOutputDevice = output_device;
            updateUltrasoundState(true);
        } else {
            // If routing to non-codec output device, alwyas use earpice to save power
            stop(true);
        }

    }

    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
          "%s(), cur_out_dev=0x%x, out_dev=0x%x, mTargetOutputDevice=0x%x, mWaitingForTargetOutputOn=%d, mWaitingOutputDevice=%d",
          __FUNCTION__, current_output_devices, output_device, mTargetOutputDevice, mWaitingForTargetOutputOn, mWaitingOutputDevice);
}

void AudioALSAUltrasoundOutOfCallController::afterOutputDeviceRouting(
        const audio_devices_t current_output_devices,
        const audio_devices_t output_device) {
    AL_AUTOLOCK(mUltraSndLock);
    UNUSED(current_output_devices);
    mOutputDeviceRouting = false;

    // start usnd with delay after non-codec device routing done, since there is no callback
    // notification after it is opened
    if (isEnabled() && !isStarted() && !isCodecOutputDevices(output_device)) {
        delayStartUsndAsyncRequest(true);
    }

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
             "%s(), output_device=0x%x, mTargetOutputDevice=0x%x",
             __FUNCTION__, output_device, mTargetOutputDevice);
}

void AudioALSAUltrasoundOutOfCallController::beforePhoneCallRouting(
        const audio_devices_t output_devices, const audio_devices_t input_device) {
    AL_AUTOLOCK(mUltraSndLock);
    // Since phone call is top priority, set flag to disable usnd before phone call routing done.
    mPhoneCallRouting = true;
    // Calculate new target output device
    if (mParam.speaker_2in1) {
        if ((output_devices & AUDIO_DEVICE_OUT_EARPIECE) == AUDIO_DEVICE_OUT_EARPIECE) {
            mTargetOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
        } else if ((output_devices & AUDIO_DEVICE_OUT_SPEAKER) == AUDIO_DEVICE_OUT_SPEAKER) {
            mTargetOutputDevice = AUDIO_DEVICE_OUT_SPEAKER;
        } else {
            mTargetOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
        }
    }
    if (isCodecInputDevices(input_device)) {
        audio_devices_t newPmicInputDevice =
                (audio_devices_t)(input_device &
                (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC |
                AUDIO_DEVICE_IN_WIRED_HEADSET));
        if (newPmicInputDevice != AUDIO_DEVICE_NONE && newPmicInputDevice != AUDIO_DEVICE_BIT_IN) {
            mTargetInputDevice = newPmicInputDevice;
        } else {
            mTargetInputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
        }
    } else {
        mTargetInputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
    }
    updateUltrasoundState(true);
    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
             "%s(), output_devices=0x%x, mTargetOutputDevice=0x%x, input_device=0x%x, mTargetInputDevice=0x%x",
             __FUNCTION__, output_devices, mTargetOutputDevice, input_device, mTargetInputDevice);
}

void AudioALSAUltrasoundOutOfCallController::afterPhoneCallRouting(
        const audio_devices_t output_devices, const audio_devices_t input_device) {
    AL_AUTOLOCK(mUltraSndLock);
    UNUSED(output_devices);
    UNUSED(input_device);
    mPhoneCallRouting = false;
    updateUltrasoundState(true);
    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
             "%s(), output_devices=0x%x, mTargetOutputDevice=0x%x, input_device=0x%x, mTargetInputDevice=0x%x",
             __FUNCTION__, output_devices, mTargetOutputDevice, input_device, mTargetInputDevice);
}

usnd_state_t AudioALSAUltrasoundOutOfCallController::getUltrasoundState() {
    return mUsndState;
}

bool AudioALSAUltrasoundOutOfCallController::isInCall() {
    audio_mode_t audioMode = AudioALSAStreamManager::getInstance()->getMode();
    return audioMode == AUDIO_MODE_IN_CALL || audioMode == AUDIO_MODE_CALL_SCREEN;
}

void AudioALSAUltrasoundOutOfCallController::ouputDeviceChanged(
    const audio_devices_t device, const DeviceStatus status) {
    if (isEnabled() && status == DEVICE_STATUS_ON && mOpenedOutputDevice != device) {
        delayStartUsndAsyncRequest(true);
    }
}

// 1. Turn off when 3.5mm heardset or headphone connected
// 2. Turn off during input device routing, since only one active input device in allow
// 3. Turn off during phone call routing, disallow any open usnd request to avoid pop
// 4. Trun off during output devide routing to avoid pop
// 5. Turn off if in call but SpeechPhoneControler not open, to avoid pop

// Calling streamManager->isPhoneCallOpen() may cause deadlock, should be careful

bool AudioALSAUltrasoundOutOfCallController::calUsndOnOffState() {
    AudioALSAStreamManager* streamManager = AudioALSAStreamManager::getInstance();
    bool inCall = isInCall();
    bool uSndOn = isEnabled() && !isHeadPhoneConnected() &&
                  !mInputDeviceRouting &&
                  !mPhoneCallRouting &&
                  (!mOutputDeviceRouting || inCall) && // disallow if output routing except in call
                  !mWaitingForTargetOutputOn &&
                  (!inCall || streamManager->isPhoneCallOpen());
    ALOGD("%s(), On=%d, hp=%d, in_routing=%d, mUsndState=%d, out_routing=%d, "
          "waitingOutOn=%d, mPhoneCallRouting=%d, inCall=%d",
          __FUNCTION__, uSndOn, isHeadPhoneConnected(), mInputDeviceRouting, mUsndState,
          mOutputDeviceRouting, mWaitingForTargetOutputOn, mPhoneCallRouting, inCall);
    return uSndOn;
}

void AudioALSAUltrasoundOutOfCallController::updateDeviceConnectionState(audio_devices_t device,
                                                                         bool connect) {
    AL_AUTOLOCK(mUltraSndLock);
    if (device == AUDIO_DEVICE_OUT_WIRED_HEADSET ||
            device == AUDIO_DEVICE_OUT_WIRED_HEADPHONE ||
            device == AUDIO_DEVICE_IN_WIRED_HEADSET) {
        if ((device & AUDIO_DEVICE_BIT_IN) == false) {
            mAvailableOutputDevices = (audio_devices_t)(connect ?
                                      mAvailableOutputDevices | device :
                                      mAvailableOutputDevices & !device);
        } else {
            mAvailableInputDevices = (audio_devices_t)(connect ?
                                     mAvailableInputDevices | device :
                                     mAvailableInputDevices & !device);
        }

        // After headset connected, stop usnd immediately.
        // While headset disconnect, should delay start usnd; since framework may still start
        // StreamIn using headset mic and cause input device open confliction.
        if (connect) {
            updateUltrasoundState(true);
        }
    } else if (audio_is_usb_out_device(device)) {
        // Stop usnd when usb device connect to avoid pop, device routing will reopen usnd later
        if (connect && isStarted()) {
            ALOGD("%s(), stop usnd for usb device connect", __FUNCTION__);
            stop(true, true);
        }
    } else if (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET ||
               device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO) {

        // Stop usnd when bt device connect to avoid pop, device routing will reopen usnd later
        if (connect && isStarted()) {
            ALOGD("%s(), stop usnd for bt device connect", __FUNCTION__);
            stop(true, true);
            // If BT only connect without routing to, should delay start usnd again.
            delayStartUsndAsyncRequest(true);
        } else {
            if (isEnabled() && !isStarted()) {
                delayStartUsndAsyncRequest(true);
            }
        }
    }
    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
             "%s() device=0x%x,connect=%d,mAvailableOutputDevices=0x%x,mAvailableInputDevices=0x%x",
             __FUNCTION__, device, connect, mAvailableOutputDevices, mAvailableInputDevices);
}

void AudioALSAUltrasoundOutOfCallController::delayStartUsndAsyncRequest(bool start) {
    AL_LOCK(mDelayStartUsndLock);
    mDelayStartUsndCtrl = start ? DELAY_START_CTRL_START : DELAY_START_CTRL_STOP;
    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
            "%s(), delay start usnd request: start=%d", __FUNCTION__, start);
    AL_SIGNAL(mDelayStartUsndLock);
    AL_UNLOCK(mDelayStartUsndLock);
}

void AudioALSAUltrasoundOutOfCallController::muteUsndAsyncRequest(bool start) {
    AL_LOCK(mMuteDlForRoutingLock);
    mMuteDlForRoutingCtrl = start ? MUTE_CTRL_START : MUTE_CTRL_STOP;
    ALOGD_IF(mLogFlag & USND_ROUTING_DEBUG,
            "%s(), mute usnd request: start=%d", __FUNCTION__, start);
    AL_SIGNAL(mMuteDlForRoutingLock);
    AL_UNLOCK(mMuteDlForRoutingLock);
}

void AudioALSAUltrasoundOutOfCallController::updateUltrasoundState(bool nolock) {
    if (!nolock) {
        AL_AUTOLOCK(mUltraSndLock);
    }
    // Ultrasound is disabled, ignore request.
    switch (mUsndState) {
    case USND_STATE_IDLE:
        break;
    case USND_STATE_ENABLED:
        if (calUsndOnOffState()) {
            start(nolock);
        } else {
            stop(nolock, true);
        }
        break;
    case USND_STATE_STARTED:
        if (!calUsndOnOffState()) {
            stop(nolock, true);
        } else {
            audio_devices_t outpuDevice = mHardwareResourceManager->getOutputDevice();
            if (mParam.speaker_2in1 && outpuDevice != mOpenedOutputDevice) {
                // If output device changed without notify, we need to restart usnd.
                stop(nolock, true);
                mTargetOutputDevice = outpuDevice;
                ALOGD("%s(), change output, mOpenedOutputDevice=%d, mTargetOutputDevice=%d",
                      __FUNCTION__, mOpenedOutputDevice, mTargetOutputDevice);
                start(nolock);
            }
        }
        break;
    case USND_STATE_STOPPED:
        if (calUsndOnOffState()) {
            start(nolock);
        }
        break;
    default:
        break;
    }
}

status_t AudioALSAUltrasoundOutOfCallController::open(const audio_devices_t output_devices,
                                                      UltrasoundDeviceConfig& ultrasound_pcm_cfg)
{
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
             ">>> %s(), output_devices = 0x%x", __FUNCTION__, output_devices);
    AL_AUTOLOCK(mUltraSndLock);

    if(mUsndState != USND_STATE_IDLE)
    {
        ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s() ignore", __FUNCTION__);
        return 0;
    }

    mMuteDlForRoutingThreadEnable = true;
    mMuteDlForRoutingCtrl = MUTE_CTRL_IDLE;
    int ret = pthread_create(&mMuteDlForRoutingThread, NULL,
            AudioALSAUltrasoundOutOfCallController::muteDlForRoutingThread, (void *)this);
    ASSERT(ret == 0);

    mDelayStartUsndThreadEnable = true;
    mDelayStartUsndCtrl = DELAY_START_CTRL_IDLE;
    ret = pthread_create(&mDelayStartUsndThread, NULL,
            AudioALSAUltrasoundOutOfCallController::delayStartUsndThread, (void *)this);
    ASSERT(ret == 0);

    ultrasound_pcm_cfg.Dump();

    // scp engine configuration
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(mUsecaseName);
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_RX_PORT, 1);

#ifdef SUPPORT_FACTORY_PCT
    int mode = property_get_int32(PROPERTY_ULTRASOUND_MODE, 0);
    if (mode > 0) {
        SendMixerControl(MIXER_CTRL_NAME_ULTRASOUND_MODE, mode);
        /* Force to use earpiece for factory calibration */
        mTargetOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
    }
#endif
    mDefaultOutputDevice = output_devices;
    mUltrasoundDeviceConfig = ultrasound_pcm_cfg;
    mUsndState = USND_STATE_ENABLED;

    // disable vow
    AudioALSAStreamManager::getInstance()->setForceDisableVoiceWakeUpForUsnd(true);

    mParamConfigToSCP.rate_in = mUltrasoundDeviceConfig.rateIn;
    mParamConfigToSCP.rate_out = mUltrasoundDeviceConfig.rateOut;
    mParamConfigToSCP.channel_in = mUltrasoundDeviceConfig.channelsIn;
    mParamConfigToSCP.channel_out = mUltrasoundDeviceConfig.channelsOut;
    mParamConfigToSCP.format_in = PCM_FORMAT_S16_LE;
    mParamConfigToSCP.format_out = PCM_FORMAT_S16_LE;
    mParamConfigToSCP.period_in_size =
            (mUltrasoundDeviceConfig.msPerPeriod * mUltrasoundDeviceConfig.rateIn) / 1000;
    mParamConfigToSCP.period_out_size = 1024 * (mUltrasoundDeviceConfig.rateOut / 48000); // fixed to 1024
    mParamConfigToSCP.target_out_channel = mUltrasoundDeviceConfig.usndOutChannel;
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
             "%s() rate_in=%d, rate_out=%d, ch_in=%d, ch_out=%d, foramt_in=%d,\
             format_out=%d, period_in=%d, period_out=%d, out_ch=%d\n",
             __func__, mParamConfigToSCP.rate_in, mParamConfigToSCP.rate_out,
             mParamConfigToSCP.channel_in, mParamConfigToSCP.channel_out,
             mParamConfigToSCP.format_in, mParamConfigToSCP.format_out,
             mParamConfigToSCP.period_in_size, mParamConfigToSCP.period_out_size,
             mParamConfigToSCP.target_out_channel);

    // send usnd param before enable
    struct mixer_ctl *ctl;
    ctl = mixer_get_ctl_by_name(mMixer, MIXER_CTL_NAME_ULTRASOUND_PARAM);
    if (ctl == NULL) {
        ALOGE("mtk_scp_ultra_param_cofig not support");
        return -1;
    }
    int retval = mixer_ctl_set_array(ctl, &mParamConfigToSCP, sizeof(mParamConfigToSCP));
    ASSERT(retval == 0);

    // enable usnd egine in scp
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_ENABLE, USND_ENGINE_STATE_ON);

    // start usnd if necessary
    AudioALSAUltrasoundOutOfCallController::getInstance()->updateUltrasoundState(true);

    ASSERT(ret == 0);

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAUltrasoundOutOfCallController::start(bool nolock) {
    if (!nolock) {
        AL_AUTOLOCK(mUltraSndLock);
    }

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, ">>> %s()", __FUNCTION__);
    if(mUsndState == USND_STATE_STARTED)
    {
        ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s() ignore", __FUNCTION__);
        return 0;
    }
    mUsndState = USND_STATE_STARTED;

    AUD_ASSERT(mTargetInputDevice & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC));

    // start dl/ul pcm
    String8 playbackseq = String8("PLAYBACK7_TO_ADDA_DL");
    String8 captureseq = String8("ADDA_TO_CAPTURE5");
    if (AudioSmartPaController::getInstance()->isSmartPAUsed() &&
        (mTargetOutputDevice == AUDIO_DEVICE_OUT_SPEAKER)) {
        playbackseq = String8("PLAYBACK7_TO_I2S3");
    }
    if ((mParam.speaker_2in1 == SPEAKER_2IN1_SMART_PA_ANALOG_EARPIECE) &&
        (mTargetOutputDevice == AUDIO_DEVICE_OUT_EARPIECE)) {
            playbackseq = String8("PLAYBACK7_TO_ADDA_DL");
    }
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(playbackseq);
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(captureseq);
    memset(&mConfig, 0, sizeof(mConfig));
    memset(&mOutConfig, 0, sizeof(mOutConfig));
    mConfig.channels = mUltrasoundDeviceConfig.channelsIn;
    mConfig.rate = mUltrasoundDeviceConfig.rateIn;
    mConfig.period_size =
            mUltrasoundDeviceConfig.msPerPeriod * mUltrasoundDeviceConfig.rateIn / 1000;
    mConfig.period_count = 2;
    mConfig.format = PCM_FORMAT_S16_LE;
    mConfig.start_threshold = 0;
    mConfig.stop_threshold = ~(0U);
    mConfig.silence_threshold = 0;
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
             "%s(), PCM_IN mConfig.rate=%d, mConfig.period_size = %d, mConfig.channels = %d",
             __FUNCTION__, mConfig.rate, mConfig.period_size, mConfig.channels);

    mOutConfig.channels = 2;
    mOutConfig.rate = mUltrasoundDeviceConfig.rateOut;
    mOutConfig.period_size = 1024;
    mOutConfig.period_count = 2;
    mOutConfig.format = PCM_FORMAT_S16_LE;
    mOutConfig.start_threshold = 0;
    mOutConfig.stop_threshold = ~(0U);
    mOutConfig.silence_threshold = 0;
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
             "%s(), PCM_OUT mOutConfig.rate=%d,mOutConfig.period_size=%d,mOutConfig.channels=%d",
             __FUNCTION__, mOutConfig.rate, mOutConfig.period_size, mOutConfig.channels);

    int pcmInIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture5);
    int pcmOutIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback7);
    openScpUltraPcmDriverWithFlag(pcmInIdx, PCM_IN | PCM_MONOTONIC);
    openScpUltraPcmDriverWithFlag(pcmOutIdx, PCM_OUT | PCM_MONOTONIC);

    // Start engine with mute
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_SUSPEND, 1);

#ifdef SUPPORT_FACTORY_PCT
    /* Skip the setEngineMode here if valid factory mode is set */
    int mode = property_get_int32(PROPERTY_ULTRASOUND_MODE, 0);
    if (mode <= 0)
#endif
    setEngineMode();

    if (mTargetInputDevice == AUDIO_DEVICE_IN_BUILTIN_MIC) {
        SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_USND_CHANNEL, 2);
    } else {
        SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_USND_CHANNEL, 1);
    }

    // delay some time to unmute engine to avoid pop
    muteUsndAsyncRequest(true);

    // enable usnd pcm
    int pcmUltraIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmUltra);
    memset(&mOutConfig, 0, sizeof(mOutConfig));
    mOutConfig.channels = mUltrasoundDeviceConfig.channelsIn;
    mOutConfig.rate = mUltrasoundDeviceConfig.rateIn;
    mOutConfig.format = PCM_FORMAT_S16_LE;
    mOutConfig.period_size =
            mUltrasoundDeviceConfig.msPerPeriod * mUltrasoundDeviceConfig.rateIn / 1000;
    mOutConfig.period_count = 2;
    mOutConfig.start_threshold = 0;
    mOutConfig.stop_threshold = ~(0U);
    mOutConfig.silence_threshold = 0;
    mPcmUltra = pcm_open(AudioALSADeviceParser::getInstance()->GetCardIndex(),
                         pcmUltraIndex, PCM_OUT | PCM_MONOTONIC, &mOutConfig);
    if (mPcmUltra == NULL) {
        ALOGE("%s(), pcm_open(mPcmUltra) failed due to memory allocation", __FUNCTION__);
    } else if (pcm_is_ready(mPcmUltra) == false) {
        ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.",
                __FUNCTION__, mPcmUltra, pcm_get_error(mPcmUltra));
        pcm_close(mPcmUltra);
        mPcmUltra = NULL;
    } else if (pcm_prepare(mPcmUltra) != 0) {
        ALOGE("%s(), pcm_prepare(%p) == false due to %s, close pcm.",
                __FUNCTION__, mPcmUltra, pcm_get_error(mPcmUltra));
        pcm_close(mPcmUltra);
        mPcmUltra = NULL;
    } else if (pcm_start(mPcmUltra) != 0) {
        ALOGE("%s(), pcm_start(%p) == false due to %s, close pcm.",
                __FUNCTION__, mPcmUltra, pcm_get_error(mPcmUltra));
        pcm_close(mPcmUltra);
        mPcmUltra = NULL;
    }

    if (mPcmUltra != NULL) {
        SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_ENABLE, USND_ENGINE_STATE_START);
    }

    mOpenedOutputDevice = mTargetOutputDevice;
    mOpenedInputDevice = mTargetInputDevice;

    mDeviceOpened = true;
    if (mParam.speaker_2in1) {
        mHardwareResourceManager->setCustOutputDevTurnOnSeq(mTargetOutputDevice,
                                                            mTurnOnSeqCustDev1,
                                                            mTurnOnSeqCustDev2);
        mHardwareResourceManager->enableTurnOnSequence(mTurnOnSeqCustDev1);
        mHardwareResourceManager->enableTurnOnSequence(mTurnOnSeqCustDev2);
        mHardwareResourceManager->startOutputDevice(mTargetOutputDevice,
                                                    mParamConfigToSCP.rate_out);
    } else {
        mHardwareResourceManager->startForceUseReceiver(mParamConfigToSCP.rate_out);
    }

    ALOGD("%s(), mTargetInputDevice=0x%x, mTargetOutputDevice=0x%x",
          __FUNCTION__, mTargetInputDevice, mTargetOutputDevice);
    mHardwareResourceManager->startInputDevice(mTargetInputDevice);

    // Send the initial analog gain values into engine
    updateAnalogGain(true);
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,"<<< %s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAUltrasoundOutOfCallController::stop(bool nolock, bool rampDown, bool suspend)
{
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, ">>> %s()", __FUNCTION__);
    if (!nolock) {
        AL_AUTOLOCK(mUltraSndLock);
    }

    if(mUsndState != USND_STATE_STARTED)
    {
        ALOGE("mUsndState:%d, ignore\n", mUsndState);
        ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s()", __FUNCTION__);
        return 0;
    }
    mUsndState = USND_STATE_STOPPED;

    muteUsndAsyncRequest(false);
    delayStartUsndAsyncRequest(false);

    if (rampDown) {
        // set ramp down to avoid pop sound
        SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_RAMP_DOWN, 1);
        if (mParam.usnd_rampdown_delay > 0) {
            usleep(mParam.usnd_rampdown_delay * 1000);
        }
    }
    if (suspend) {
        mEngineSuspended = true;
        SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_SUSPEND, 1);
    }
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_RAMP_DOWN, 0);

    // should stop usnd pcm fisrt, avoid accessing afe after memif disabled.
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_ENABLE, USND_ENGINE_STATE_STOP);
    if (mPcmUltra != NULL) {
        pcm_stop(mPcmUltra);
        pcm_close(mPcmUltra);
        mPcmUltra = NULL;
    } else {
        ALOGE("%s(), mPcmUltra NULL, ignore", __FUNCTION__);
    }

    // stop dl/ul pcm
    pcm_stop(mPcmUL);
    pcm_stop(mPcmDL);
    pcm_close(mPcmUL);
    pcm_close(mPcmDL);
    mPcmUL = NULL;
    mPcmDL = NULL;

    if (mDeviceOpened) {
        if (mParam.speaker_2in1) {
            mHardwareResourceManager->stopOutputDevice();
            mHardwareResourceManager->disableTurnOnSequence(mTurnOnSeqCustDev1);
            mHardwareResourceManager->disableTurnOnSequence(mTurnOnSeqCustDev2);
        } else {
            mHardwareResourceManager->stopForceUseReceiver();
        }

        audio_devices_t curInDevice = mHardwareResourceManager->getInputDevice();
        AUD_ASSERT(curInDevice == mOpenedInputDevice);
        mHardwareResourceManager->stopInputDevice(mOpenedInputDevice);
        mDeviceOpened = false;
        ALOGD("%s(),mOpenedOutputDevice=0x%x, mTargetOutputDevice=0x%x, mOpenedInputDevice=0x%x, mTargetInputDevice=0x%x",
              __FUNCTION__, mOpenedOutputDevice, mTargetOutputDevice, mOpenedInputDevice, mTargetInputDevice);
    }
    mOpenedOutputDevice = AUDIO_DEVICE_NONE;
    mOpenedInputDevice = AUDIO_DEVICE_NONE;

    String8 playbackseq = String8("PLAYBACK7_TO_ADDA_DL");
    String8 captureseq = String8("ADDA_TO_CAPTURE5");
    if (AudioSmartPaController::getInstance()->isSmartPAUsed() &&
            mTargetOutputDevice == AUDIO_DEVICE_OUT_SPEAKER) {
        playbackseq = String8("PLAYBACK7_TO_I2S3");
    }

    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(playbackseq);
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(captureseq);

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAUltrasoundOutOfCallController::close()
{
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, ">>> %s()", __FUNCTION__);

    AL_AUTOLOCK(mUltraSndLock);
    int retval = 0;

    // terminate mute for routing thread
    AL_LOCK(mMuteDlForRoutingLock);
    mMuteDlForRoutingThreadEnable = false;
    mMuteDlForRoutingCtrl = MUTE_CTRL_STOP;
    AL_SIGNAL(mMuteDlForRoutingLock);
    AL_UNLOCK(mMuteDlForRoutingLock);
    pthread_join(mMuteDlForRoutingThread, NULL);

    // terminate delay start usnd thread
    AL_LOCK(mDelayStartUsndLock);
    mDelayStartUsndThreadEnable = false;
    mDelayStartUsndCtrl = DELAY_START_CTRL_STOP;
    AL_SIGNAL(mDelayStartUsndLock);
    AL_UNLOCK(mDelayStartUsndLock);
    pthread_join(mDelayStartUsndThread, NULL);

    if(mUsndState == USND_STATE_IDLE) {
        ALOGD("<<< %s() ignore", __FUNCTION__);
        return 0;
    }

    // Stop without suspend, to trigger engine report far event immediately
    if (mUsndState == USND_STATE_STARTED) {
        stop(true, true, false);
    }

    mUsndState = USND_STATE_IDLE;
    mWaitingForTargetOutputOn = false;

    // Update scp engine configuration
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_RX_PORT, 0);

#ifdef SUPPORT_FACTORY_PCT
    SendMixerControl(MIXER_CTRL_NAME_ULTRASOUND_MODE, 0);
#endif

    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mUsecaseName);
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mUsecaseSpkrName);

    // disable usnd engine in scp
    SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_ENABLE, USND_ENGINE_STATE_OFF);

    // resume vow if necessary
    AudioALSAStreamManager::getInstance()->setForceDisableVoiceWakeUpForUsnd(false);

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s()", __FUNCTION__);
    return NO_ERROR;
}

void *AudioALSAUltrasoundOutOfCallController::delayStartUsndThread(void *arg) {
    AudioALSAUltrasoundOutOfCallController *usnd_controller = NULL;
    AudioLock *lock = NULL;

    char thread_name[128] = {0};
    int retvalWait = 0;

    CONFIG_USND_THREAD(thread_name, ANDROID_PRIORITY_AUDIO);

    usnd_controller = static_cast<AudioALSAUltrasoundOutOfCallController *>(arg);
    if (usnd_controller == NULL) {
        ALOGE("%s(), usnd_controller is NULL!!", __FUNCTION__);
        goto DELAY_START_USND_THREAD_DONE;
    }

    lock = &usnd_controller->mDelayStartUsndLock;
    usnd_controller->mDelayStartUsndState = DELAY_START_THREAD_IDLE;

    AL_LOCK(lock);

    while (usnd_controller->mDelayStartUsndThreadEnable == true) {
        ALOGD_IF(usnd_controller->mLogFlag & USND_CTL_FLOW_DEBUG,
                 "%s(), sleep to wait for next cmd", __FUNCTION__);
        // sleep until signal comes
        AL_WAIT_NO_TIMEOUT(lock);
        if (usnd_controller->mDelayStartUsndCtrl != DELAY_START_CTRL_START) {
            ALOGD_IF(usnd_controller->mLogFlag & USND_CTL_FLOW_DEBUG,
                     "%s(), wake up without start cmd, ctrl=%d",
                     __FUNCTION__, usnd_controller->mDelayStartUsndCtrl);
            continue;
        }

DELAY_START_USND_THREAD_START:
        usnd_controller->mDelayStartUsndState = DELAY_START_THREAD_SLEEPING;
        // wait 150ms for device routing
        int mute_dur = usnd_controller->mParam.delay_start_duration;
        ALOGD_IF(usnd_controller->mLogFlag & USND_CTL_FLOW_DEBUG,
                "%s(), enter sleep, duration=%d", __FUNCTION__, mute_dur);
        retvalWait = AL_WAIT_MS(lock, mute_dur);
        if (retvalWait == -ETIMEDOUT) { //time out, start engine
            AL_UNLOCK(lock);
            AL_LOCK_MS(usnd_controller->mUltraSndLock, MAX_AUDIO_LOCK_TIMEOUT_MS);
            audio_devices_t outpuDevice =
                    AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();
            audio_devices_t waitingOutputDevice = usnd_controller->mWaitingOutputDevice;
            if (usnd_controller->mWaitingForTargetOutputOn &&
                    ((outpuDevice & waitingOutputDevice) == waitingOutputDevice)) {
                ALOGD("%s(), mWaitingForTargetOutputOn=false and update", __FUNCTION__);
                usnd_controller->mWaitingForTargetOutputOn = false;
            }
            usnd_controller->updateUltrasoundState(true);
            AL_UNLOCK(usnd_controller->mUltraSndLock);
            AL_LOCK(lock);
            ALOGV("%s(), wait timeout, recover", __FUNCTION__);
        } else {
            //disturb wait
            ALOGV("%s(), force wake up, ctrl=%d",
                     __FUNCTION__, usnd_controller->mDelayStartUsndCtrl);
            if (usnd_controller->mDelayStartUsndCtrl == DELAY_START_CTRL_START) {
                goto DELAY_START_USND_THREAD_START;
            }
        }
        usnd_controller->mDelayStartUsndState = DELAY_START_THREAD_IDLE;
    }

    AL_UNLOCK(lock);

DELAY_START_USND_THREAD_DONE:
    ALOGV("%s terminated", thread_name);
    pthread_exit(NULL);
    return NULL;
}

void *AudioALSAUltrasoundOutOfCallController::muteDlForRoutingThread(void *arg) {
    AudioALSAUltrasoundOutOfCallController *usnd_controller = NULL;
    AudioLock *lock = NULL;

    char thread_name[128] = {0};
    int retvalWait = 0;

    CONFIG_USND_THREAD(thread_name, ANDROID_PRIORITY_AUDIO);

    usnd_controller = static_cast<AudioALSAUltrasoundOutOfCallController *>(arg);
    if (usnd_controller == NULL) {
        ALOGE("%s(), usnd_controller is NULL!!", __FUNCTION__);
        goto MUTE_DL_FOR_ROUTING_THREAD_DONE;
    }

    lock = &usnd_controller->mMuteDlForRoutingLock;
    usnd_controller->mMuteDlForRoutingState = MUTE_THREAD_STATE_IDLE;

    AL_LOCK(lock);

    while (usnd_controller->mMuteDlForRoutingThreadEnable == true) {
        ALOGD_IF(usnd_controller->mLogFlag & USND_ROUTING_DEBUG,
                 "%s(), sleep to wait for next mute", __FUNCTION__);
        // sleep until signal comes
        AL_WAIT_NO_TIMEOUT(lock);
        if (usnd_controller->mMuteDlForRoutingCtrl != MUTE_CTRL_START) {
            ALOGD_IF(usnd_controller->mLogFlag & USND_ROUTING_DEBUG,
                     "%s(), wake up without unmute, ctrl=%d",
                     __FUNCTION__, usnd_controller->mMuteDlForRoutingCtrl);
            continue;
        }

MUTE_DL_FOR_ROUTING_THREAD_START:
        usnd_controller->mMuteDlForRoutingState = MUTE_THREAD_STATE_MUTING;
        // wait 150ms for device routing
        int mute_dur = usnd_controller->mParam.mute_duration;
        ALOGD_IF(usnd_controller->mLogFlag & USND_ROUTING_DEBUG,
                 "%s(), enter sleep, duration=%d", __FUNCTION__, mute_dur);
        retvalWait = AL_WAIT_MS(lock, mute_dur);
        if (retvalWait == -ETIMEDOUT) { //time out, do unmute
            // resume engine
            AL_UNLOCK(lock);
            AL_LOCK_MS(usnd_controller->mUltraSndLock, MAX_AUDIO_LOCK_TIMEOUT_MS);
            usnd_controller->mEngineSuspended = false;
            usnd_controller->SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_RAMP_DOWN, 0);
            usnd_controller->SendMixerControl(MIXER_CTL_NAME_ULTRASOUND_SUSPEND, 0);
            AL_UNLOCK(usnd_controller->mUltraSndLock);
            AL_LOCK(lock);
            ALOGV("%s(), wait timeout, recover", __FUNCTION__);
        } else {
            //disturb wait
            ALOGV("%s(), force wake up, ctrl=%d",
                     __FUNCTION__, usnd_controller->mMuteDlForRoutingCtrl);
            if (usnd_controller->mMuteDlForRoutingCtrl == MUTE_CTRL_START) {
                goto MUTE_DL_FOR_ROUTING_THREAD_START;
            }
        }
        usnd_controller->mMuteDlForRoutingState = MUTE_THREAD_STATE_IDLE;
    }

    AL_UNLOCK(lock);

MUTE_DL_FOR_ROUTING_THREAD_DONE:
    ALOGV("%s terminated", thread_name);
    pthread_exit(NULL);
    return NULL;
}

void AudioALSAUltrasoundOutOfCallController::setEngineMode()
{
    if(mTargetOutputDevice == AUDIO_DEVICE_OUT_SPEAKER) {
        ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "set engine mode for SPEAKER\n");
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mUsecaseName);
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mUsecaseSpkrName);
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(mUsecaseSpkrName);
    } else {
        ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "set engine mode for the default EARPIECE\n");
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mUsecaseName);
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mUsecaseSpkrName);
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(mUsecaseName);
    }
}

template<class T>
status_t getParam(AppOps *appOps, ParamUnit *_paramUnit, T *_param, const char *_paramName) {
    Param *param;
    param = appOps->paramUnitGetParamByName(_paramUnit, _paramName);
    if (!param) {
        ALOGE("error: get param fail, param_name = %s", _paramName);
        return BAD_VALUE;
    } else {
        *_param = *(T *)param->data;
    }

    return NO_ERROR;
}

status_t AudioALSAUltrasoundOutOfCallController::loadUltrasoundParam() {
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGD("%s(), Error: AppOps == NULL", __FUNCTION__);
        ASSERT(false);
        return UNKNOWN_ERROR;
    }

    // define xml names
    char audioTypeName[] = ULTRASOUND_FUNCTION_NAME;

    // extract parameters from xml
    AudioType *audioType;
    audioType = appOps->appHandleGetAudioTypeByName(appOps->appHandleGetInstance(), audioTypeName);
    if (!audioType) {
        ALOGD("%s(), get audioType fail, audioTypeName = %s", __FUNCTION__, audioTypeName);
        return BAD_VALUE;
    }

    const char *platformChar = appOps->appHandleGetFeatureOptionValue(appOps->appHandleGetInstance(), "MTK_PLATFORM");
    std::string paramPath = "UltrasoundFunctionParam,";
    if (platformChar) {
        paramPath += std::string(platformChar);
    }

    ParamUnit *paramUnit;
    paramUnit = appOps->audioTypeGetParamUnit(audioType, paramPath.c_str());
    if (!paramUnit) {
        ALOGV("%s(), get paramUnit fail, paramPath = %s, use common", __FUNCTION__, paramPath.c_str());

        paramUnit = appOps->audioTypeGetParamUnit(audioType, paramCommonPath.c_str());
        if (!paramUnit) {
            ALOGE("%s(), get paramUnit fail, paramCommonPath = %s", __FUNCTION__, paramCommonPath.c_str());
            return BAD_VALUE;
        }
    }

    // Read lock
    appOps->audioTypeReadLock(audioType, __FUNCTION__);

    // spec
    getParam<int>(appOps, paramUnit, &mParam.speaker_2in1, ultrasound2In1);
    getParam<int>(appOps, paramUnit, &mParam.usnd_rampdown_delay, rampdownDelay);
    getParam<int>(appOps, paramUnit, &mParam.delay_start_duration, delayStartDuration);
    getParam<int>(appOps, paramUnit, &mParam.mute_duration, muteDuration);

    ALOGD("-%s(), speaker_2in1=%d, usnd_rampdown_delay=%d, delay_start_duration=%d, mute_duration=%d",
        __FUNCTION__, mParam.speaker_2in1, mParam.usnd_rampdown_delay,
        mParam.delay_start_duration, mParam.mute_duration);

    // Unlock
    appOps->audioTypeUnlock(audioType);

    return NO_ERROR;
}

void AudioALSAUltrasoundOutOfCallController::updateXmlParam(const char *_audioTypeName) {
    ALOGD("%s(), audioType=%s", __FUNCTION__, _audioTypeName);

    if (strcmp(_audioTypeName, ULTRASOUND_FUNCTION_NAME) == 0) {
        loadUltrasoundParam();
    }
}

} // end of namespace android
