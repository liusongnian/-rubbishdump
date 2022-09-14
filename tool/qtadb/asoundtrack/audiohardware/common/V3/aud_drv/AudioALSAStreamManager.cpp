#include "AudioALSAStreamManager.h"

#include <tinyalsa/asoundlib.h> // TODO(Harvey): move it

#include "WCNChipController.h"

#include "AudioALSAStreamOut.h"
#include "AudioALSAStreamIn.h"
#include "AudioALSAPlaybackHandlerBase.h"
#if defined(HAVE_SW_MIXER)
#include "AudioALSAPlaybackHandlerMixer.h"
#endif
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
#include <AudioIEMsController.h>
#include <AudioALSACaptureHandlerIEMs.h>
#endif

#include "AudioALSAPlaybackHandlerNormal.h"
#include "AudioALSAPlaybackHandlerFast.h"
#include "AudioALSAPlaybackHandlerVoice.h"
#include "AudioALSAPlaybackHandlerBTSCO.h"
#include "AudioALSAPlaybackHandlerBTCVSD.h"
#include "AudioALSAPlaybackHandlerFMTransmitter.h"
#include "AudioALSAPlaybackHandlerHDMI.h"
#include "AudioALSAPlaybackHandlerTVOut.h"
#if defined(PRIMARY_USB)
#include "AudioALSAPlaybackHandlerUsb.h"
#endif

#ifdef MTK_AUDIODSP_SUPPORT
#include "AudioALSAPlaybackHandlerDsp.h"
#include "AudioALSACaptureHandlerDsp.h"
#include "AudioDspStreamManager.h"
#include "AudioALSAPlaybackHandlerOffload.h"
#endif

#include "AudioALSACaptureHandlerBase.h"
#include "AudioALSACaptureHandlerNormal.h"
#include "AudioALSACaptureHandlerSyncIO.h"
#include "AudioALSACaptureHandlerVoice.h"
#include "AudioALSACaptureHandlerFMRadio.h"
#include "AudioALSACaptureHandlerBT.h"
#if defined(PRIMARY_USB)
#include "AudioALSACaptureHandlerUsb.h"
#endif

#ifdef MTK_VOW_SUPPORT
#include "AudioALSACaptureHandlerVOW.h"
#include "AudioALSAVoiceWakeUpController.h"
#endif
#include "AudioALSACaptureHandlerAEC.h"
#include "AudioALSACaptureHandlerTDM.h"

#include "AudioALSACaptureHandlerModemDai.h"

#include "AudioALSACaptureDataProviderNormal.h"

#include "AudioALSASpeechPhoneCallController.h"
#include "AudioALSAFMController.h"

#include "AudioVolumeFactory.h"
#include "AudioDeviceInt.h"
#ifdef MTK_AUDIO_TVOUT_SUPPORT
#include <linux/mediatek_drm.h>
#endif

#include "AudioALSAHardwareResourceManager.h" // TODO(Harvey): move it
#include "AudioALSASpeechStreamController.h"
#include "AudioALSASampleRateController.h"

#include "AudioCompFltCustParam.h"
#include "SpeechDriverInterface.h"
#include "SpeechDriverFactory.h"
#include "AudioALSADriverUtility.h"
#include "SpeechEnhancementController.h"
#include "SpeechVMRecorder.h"
#include "AudioSmartPaController.h"
#include <AudioParamParser.h>

#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
#include <AudioALSAParamTuner.h>
#include <SpeechConfig.h>
#if !defined(MTK_COMBO_MODEM_SUPPORT)
#include <SpeechParamParser.h>
#endif
#endif

#if defined(MTK_HYBRID_NLE_SUPPORT)
#include "AudioALSANLEController.h"
#endif

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <aurisys_controller.h>
#endif

#ifdef MTK_AUDIODSP_SUPPORT
#include <audio_task.h>
#include <AudioDspType.h>
#include "audio_a2dp_msg_id.h"
#endif

#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
#include <audio_dsp_controller.h>
#endif

#include <AudioEventThreadManager.h>

#if defined(MTK_AUDIO_AAUDIO_SUPPORT)
#include "AudioALSAPlaybackHandlerAAudio.h"
#include "AudioALSACaptureHandlerAAudio.h"
#endif

#include <SpeechUtility.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAStreamManager"
//#define FM_HIFI_NOT_CONCURRENT
#define AUDIO_HIFI_RATE_DEFAULT (48000)

/* For UplinkCustomization information query */
#define UPLINK_CUSTOMIZATION_AUDIO_TYPE "UplinkConfiguration"
#define OFFLOAD_PARAM_NAME              "offload"
#define NUM_OF_MIC_PARAM_NAME           "num of mic"
#define VOIP_PATH                       "InputSource,VoIP"
#define FAST_RECORD_PATH                "InputSource,FastRecord"
#define CAMCORDER_PATH                  "InputSource,Camcorder"
#define VOICE_RECOGNITION_PATH          "InputSource,VoiceRecognition"
#define UNPROCESSED_PATH                "InputSource,Unprocessed"
#define VOICE_PERFORMANCE_PATH          "InputSource,VoicePerformance"
#define MIC_PATH                        "InputSource,MIC"

static struct pcm_config mLoopbackConfig; // TODO(Harvey): move it to AudioALSAHardwareResourceManager later
static struct pcm *mLoopbackPcm = NULL; // TODO(Harvey): move it to AudioALSAHardwareResourceManager & AudioALSAPlaybackDataDispatcher later
static struct pcm_config mLoopbackUlConfig; // TODO(Harvey): move it to AudioALSAHardwareResourceManager later
static struct pcm *mLoopbackUlPcm = NULL; // TODO(Harvey): move it to AudioALSAHardwareResourceManager & AudioALSAPlaybackDataDispatcher later
static struct pcm *mHdmiPcm = NULL; // TODO(Harvey): move it to AudioALSAHardwareResourceManager & AudioALSAPlaybackDataDispatcher later

namespace android {

/*==============================================================================
 *                     XML Path
 *============================================================================*/
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
static const char *SOUND_ENHANCEMENT_AUDIO_TYPE = "SoundEnhancement";
static const char *SOUND_ENHANCEMENT_CATEGORY_PATH = "SoundEnhancement,Common";
static const char *SOUND_ENHANCEMENT_PARAM_BESLOUDNESS = "besloudness";
#endif

/*==============================================================================
 *                     Property keys
 *============================================================================*/
const char *PROPERTY_KEY_SET_BT_NREC = "persist.vendor.debug.set_bt_aec";
const char *PROPERTY_KEY_BESLOUDNESS_SWITCH_STATE = "persist.vendor.audiohal.besloudness_state";
const char *PROPERTY_KEY_FIRSTBOOT_STATE = "persist.vendor.audiohal.firstboot";
#define BESLOUDNESS_SWITCH_DEFAULT_STATE (1)


/*==============================================================================
 *                     Singleton Pattern
 *============================================================================*/

AudioALSAStreamManager *AudioALSAStreamManager::mStreamManager = NULL;
AudioALSAStreamManager *AudioALSAStreamManager::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mStreamManager == NULL) {
        mStreamManager = new AudioALSAStreamManager();
    }
    ASSERT(mStreamManager != NULL);
    return mStreamManager;
}


/*==============================================================================
 *                     Callback Function
 *============================================================================*/
void callbackPhoneCallReopen(int audioEventType, void *caller, void *arg) {
    ALOGD("%s(), audioEventType = %d, caller(%p), arg(%p)",
          __FUNCTION__, audioEventType, caller, arg);
    AudioALSAStreamManager *streamManager = NULL;
    streamManager = static_cast<AudioALSAStreamManager *>(caller);
    if (streamManager == NULL) {
        ALOGE("%s(), streamManager is NULL!!", __FUNCTION__);
    } else {
        streamManager->phoneCallReopen();
    }
}

bool speakerStatusChangeCb(const DeviceStatus status, const uint32_t sampleRate) {
    ALOGD("%s(), status = %d, sampleRate = %d", __FUNCTION__, status, sampleRate);
    bool ret = true;
#ifdef MTK_VOW_SUPPORT
    bool isSpeakerPlaying = (status == DEVICE_STATUS_ON) ? true : false;
    if (isSpeakerPlaying) {
        AudioALSAVoiceWakeUpController::getInstance()->setSpeakerSampleRate(sampleRate);
    }
    ret = AudioALSAVoiceWakeUpController::getInstance()->updateSpeakerPlaybackStatus(isSpeakerPlaying);
#endif
    return ret;
}

void callbackVolumeChange(int audioEventType, void *caller, void *arg) {
    ALOGD("%s(), audioEventType = %d, caller(%p), arg(%p)",
          __FUNCTION__, audioEventType, caller, arg);
    AudioALSAStreamManager *streamManager = NULL;
    streamManager = static_cast<AudioALSAStreamManager *>(caller);
    if (streamManager == NULL) {
        ALOGE("%s(), streamManager is NULL!!", __FUNCTION__);
    } else {
        streamManager->volumeChangedCallback();
    }
}

/*==============================================================================
 *                     Constructor / Destructor / Init / Deinit
 *============================================================================*/

AudioALSAStreamManager::AudioALSAStreamManager() :
    mStreamOutIndex(0),
    mStreamInIndex(0),
    mPlaybackHandlerIndex(0),
    mCaptureHandlerIndex(0),
    mSpeechPhoneCallController(AudioALSASpeechPhoneCallController::getInstance()),
    mSmartPaController(AudioSmartPaController::getInstance()),
    mFMController(AudioALSAFMController::getInstance()),
    mAudioALSAVolumeController(AudioVolumeFactory::CreateAudioVolumeController()),
    mSpeechDriverFactory(SpeechDriverFactory::GetInstance()),
    mMicMute(false),
    mAudioMode(AUDIO_MODE_NORMAL),
    mAudioModePolicy(AUDIO_MODE_NORMAL),
    mEnterPhoneCallMode(false),
    mPhoneCallControllerStatusPolicy(false),
    mResumeAllStreamsAtRouting(false),
    mIsNeedResumeStreamOut(false),
    mLoopbackEnable(false),
    mHdmiEnable(false),
    mBesLoudnessStatus(false),
    mBesLoudnessControlCallback(NULL),
    mAudioSpeechEnhanceInfoInstance(AudioSpeechEnhanceInfo::getInstance()),
    mHeadsetChange(false),
#ifdef MTK_VOW_SUPPORT
    mAudioALSAVoiceWakeUpController(AudioALSAVoiceWakeUpController::getInstance()),
#else
    mAudioALSAVoiceWakeUpController(0),
#endif
    mVoiceWakeUpNeedOn(false),
    mForceDisableVoiceWakeUpForPhoneCall(false),
    mBypassPostProcessDL(false),
    mBypassDualMICProcessUL(false),
    mBtHeadsetName(NULL),
    mBtCodec(-1),
#if defined(NON_SCENE_3GVT_VOWIFI_SUPPORT)
    mIsVoWifi(false),
    mIsVoWifiWbAmr(false),
    mIs3GVT(false),
#endif
    mAvailableOutputDevices(AUDIO_DEVICE_OUT_SPEAKER | AUDIO_DEVICE_OUT_EARPIECE),
    mCustScene(""),
    mStreamManagerDestructing(false),
    mVolumeIndex(0),
    mVoiceVolumeIndex(-1),
    mVoiceStream(-1),
    mVoiceDevice(-1),
    mStreamType(AUDIO_STREAM_DEFAULT),
    mAvailableOutputFlags(0),
    mSwBridgeEnable(0),
    mHDRRecordOn(false) {
    ALOGD("%s()", __FUNCTION__);

    AudioPlatformInfo::getInstance();  // init and parsing platform FOs
    mOutputStreamForCall = NULL;
    mCurrentOutputDevicesForCall = AUDIO_DEVICE_NONE;
    mOutputDevicesForCall = AUDIO_DEVICE_NONE;

#ifdef MTK_AUDIO_DSP_RECOVERY_SUPPORT
    if (isAdspRecoveryEnable()) {
        audio_dsp_cbk_register(
            audioDspReadyWrap,
            audioDspStopWrap,
            this);
    }
#endif

#ifdef CONFIG_MT_ENG_BUILD
    mLogEnable = 1;
#else
    mLogEnable = __android_log_is_loggable(ANDROID_LOG_DEBUG, LOG_TAG, ANDROID_LOG_INFO);
#endif

    mStreamOutVector.clear();
    mStreamInVector.clear();

    mPlaybackHandlerVector.clear();
    mCaptureHandlerVector.clear();
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    mFilterManagerVector.clear();
#endif

#if !defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    char property_value[PROPERTY_VALUE_MAX];
#endif

    mAudioCustParamClient = NULL;
    mAudioCustParamClient = AudioCustParamClient::GetInstance();

    bool foValue_MTK_BESLOUDNESS_SUPPORT = appIsFeatureOptionEnabled("MTK_BESLOUDNESS_SUPPORT");
    int firstboot = 0;
#ifdef MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
    int besLoudnessInXML = getBesLoudnessStateFromXML();
    firstboot = (int)(besLoudnessInXML != 0 && besLoudnessInXML != 1);
#else
    firstboot = property_get(PROPERTY_KEY_FIRSTBOOT_STATE, property_value, "1"); //"1": first boot, "0": not first boot
#endif

    if (foValue_MTK_BESLOUDNESS_SUPPORT) {
        if (firstboot) {
            // Boots at first time
            if (AudioSmartPaController::getInstance()->isSmartPAUsed()) {
                mBesLoudnessStatus = false;
            } else {
                mBesLoudnessStatus = true;
            }
#ifdef MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
            setBesLoudnessStateToXML(mBesLoudnessStatus);
#else
            property_set(PROPERTY_KEY_FIRSTBOOT_STATE, "0");
#endif
            property_set(PROPERTY_KEY_BESLOUDNESS_SWITCH_STATE, mBesLoudnessStatus ? "1" : "0");
        } else {
#ifdef MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
            mBesLoudnessStatus = besLoudnessInXML;
#else
            mBesLoudnessStatus = property_get_int32(PROPERTY_KEY_BESLOUDNESS_SWITCH_STATE, BESLOUDNESS_SWITCH_DEFAULT_STATE);
#endif
        }
    } else {
        mBesLoudnessStatus = false;
        ALOGD("%s(), Unsupport besLoudness! mBesLoudnessStatus [%d] (Always) \n", __FUNCTION__, mBesLoudnessStatus);
        property_set(PROPERTY_KEY_BESLOUDNESS_SWITCH_STATE, "0");
    }
    AudioEventThreadManager::getInstance()->registerCallback(AUDIO_EVENT_PHONECALL_REOPEN,
                                                             callbackPhoneCallReopen, this);
#if defined(MTK_TC10_FEATURE) && defined(MTK_TC10_IN_HOUSE)
    AudioEventThreadManager::getInstance()->registerCallback(AUDIO_EVENT_VOLUME_CHANGE,
                                                             callbackVolumeChange, this);
#endif

    // get power hal service first to reduce time for later usage of power hal
    initPowerHal();

    // Set speaker change callback
    AudioALSAHardwareResourceManager::getInstance()->setSpeakerStatusChangeCb(speakerStatusChangeCb);

#ifdef MTK_AUDIODSP_SUPPORT
    if (isAdspOptionEnable()) {
        AudioDspStreamManager::getInstance()->doRecoveryState();
    }
#endif

#ifdef MTK_VOW_SUPPORT
    // turn off barge in if audio restart during barge in
    mAudioALSAVoiceWakeUpController->doBargeInRecovery();
#endif

    mIsMicMuteBeforeCallFwd = false;
    mIsDlMuteBeforeCallFwd = false;
    mIsCallFwdEnabled = false;
    mCallMemoState = CALL_MEMO_STATE_INIT;
    mIsDlMuteBeforeCallMemo = false;
    mIsMicMuteBeforeCallMemo = false;

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    mForceDisableVoiceWakeUpForUsnd = false;
#endif
}


AudioALSAStreamManager::~AudioALSAStreamManager() {
    ALOGD("%s()", __FUNCTION__);

    mStreamManagerDestructing = true;

    if (mBtHeadsetName) {
        free((void *)mBtHeadsetName);
        mBtHeadsetName = NULL;
    }

    mStreamManager = NULL;

    AudioEventThreadManager::getInstance()->unregisterCallback(AUDIO_EVENT_PHONECALL_REOPEN);
#if defined(MTK_TC10_FEATURE) && defined(MTK_TC10_IN_HOUSE)
    AudioEventThreadManager::getInstance()->unregisterCallback(AUDIO_EVENT_VOLUME_CHANGE);
#endif
}


/*==============================================================================
 *                     Implementations
 *============================================================================*/

AudioMTKStreamOutInterface *AudioALSAStreamManager::openOutputStream(
    uint32_t devices,
    int *format,
    uint32_t *channels,
    uint32_t *sampleRate,
    status_t *status,
    uint32_t output_flag) {
    AL_AUTOLOCK(mStreamVectorLock);
    AL_AUTOLOCK(mLock);

    if (format == NULL || channels == NULL || sampleRate == NULL || status == NULL) {
        ALOGE("%s(), NULL pointer!! format = %p, channels = %p, sampleRate = %p, status = %p",
              __FUNCTION__, format, channels, sampleRate, status);
        if (status != NULL) { *status = INVALID_OPERATION; }
        return NULL;
    }

    ALOGV("+%s(), devices = 0x%x, format = 0x%x, channels = 0x%x, sampleRate = %d, status = 0x%x",
          __FUNCTION__, devices, *format, *channels, *sampleRate, *status);

    // stream out flags
#if 1 // TODO(Harvey): why.........
    mStreamOutIndex = (uint32_t)(*status);
#endif
    //const uint32_t flags = 0; //(uint32_t)(*status);

    // create stream out
    AudioALSAStreamOut *pAudioALSAStreamOut = new AudioALSAStreamOut();
    pAudioALSAStreamOut->set(devices, format, channels, sampleRate, status, output_flag);
    if (*status != NO_ERROR) {
        ALOGE("-%s(), set fail, return NULL", __FUNCTION__);
        delete pAudioALSAStreamOut;
        pAudioALSAStreamOut = NULL;
        return NULL;
    }

    // save stream out object in vector
#if 0 // TODO(Harvey): why.........
    pAudioALSAStreamOut->setIdentity(mStreamOutIndex);
    mStreamOutVector.add(mStreamOutIndex, pAudioALSAStreamOut);
    mStreamOutIndex++;
#else
    pAudioALSAStreamOut->setIdentity(mStreamOutIndex);
    mStreamOutVector.add(mStreamOutIndex, pAudioALSAStreamOut);
#endif

#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    // setup Filter for ACF/HCF/AudEnh/VibSPK // TODO Check return status of pAudioALSAStreamOut->set
    AudioMTKFilterManager *pAudioFilterManagerHandler = new AudioMTKFilterManager(*sampleRate, popcount(*channels), *format, pAudioALSAStreamOut->bufferSize());
    if (pAudioFilterManagerHandler != NULL) {
        if (pAudioFilterManagerHandler->init(output_flag) == NO_ERROR) {
            mFilterManagerVector.add(mStreamOutIndex, pAudioFilterManagerHandler);
        } else {
            delete pAudioFilterManagerHandler;
        }
    }
#endif

    mAvailableOutputFlags |= output_flag;

    ALOGD_IF(mLogEnable, "-%s(), out = %p, status = 0x%x, mStreamOutVector.size() = %zu",
             __FUNCTION__, pAudioALSAStreamOut, *status, mStreamOutVector.size());


    return pAudioALSAStreamOut;
}

void AudioALSAStreamManager::closeOutputStream(AudioMTKStreamOutInterface *out) {
    ALOGD("+%s(), out = %p, mStreamOutVector.size() = %zu", __FUNCTION__, out, mStreamOutVector.size());
    AL_AUTOLOCK(mStreamVectorLock);
    AL_AUTOLOCK(mLock);

    if (out == NULL) {
        ALOGE("%s(), Cannot close null output stream!! return", __FUNCTION__);
        return;
    }

    AudioALSAStreamOut *pAudioALSAStreamOut = static_cast<AudioALSAStreamOut *>(out);
    ASSERT(pAudioALSAStreamOut != 0);

    uint32_t streamOutId = pAudioALSAStreamOut->getIdentity();

    mStreamOutVector.removeItem(streamOutId);
    delete pAudioALSAStreamOut;
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    uint32_t dFltMngindex = mFilterManagerVector.indexOfKey(streamOutId);

    if (dFltMngindex < mFilterManagerVector.size()) {
        AudioMTKFilterManager *pAudioFilterManagerHandler = static_cast<AudioMTKFilterManager *>(mFilterManagerVector[dFltMngindex]);
        ALOGD("%s, remove mFilterManagerVector Success [%d]/[%zu] [%d], pAudioFilterManagerHandler=%p",
              __FUNCTION__, dFltMngindex, mFilterManagerVector.size(), streamOutId, pAudioFilterManagerHandler);
        ASSERT(pAudioFilterManagerHandler != 0);
        mFilterManagerVector.removeItem(streamOutId);
        delete pAudioFilterManagerHandler;
    } else {
        ALOGD("%s, Remove mFilterManagerVector Error [%d]/[%zu]", __FUNCTION__, dFltMngindex, mFilterManagerVector.size());
    }
#endif
    ALOGD_IF(mLogEnable, "-%s(), mStreamOutVector.size() = %zu", __FUNCTION__, mStreamOutVector.size());
}


AudioMTKStreamInInterface *AudioALSAStreamManager::openInputStream(
    uint32_t devices,
    int *format,
    uint32_t *channels,
    uint32_t *sampleRate,
    status_t *status,
    audio_in_acoustics_t acoustics,
    uint32_t input_flag) {
    AL_AUTOLOCK(mStreamVectorLock);
    AL_AUTOLOCK(mLock);

    if (format == NULL || channels == NULL || sampleRate == NULL || status == NULL) {
        ALOGE("%s(), NULL pointer!! format = %p, channels = %p, sampleRate = %p, status = %p",
              __FUNCTION__, format, channels, sampleRate, status);
        if (status != NULL) { *status = INVALID_OPERATION; }
        return NULL;
    }

    ALOGD("%s(), devices = 0x%x, format = 0x%x, channels = 0x%x, sampleRate = %d, status = %d, acoustics = 0x%x, input_flag 0x%x",
          __FUNCTION__, devices, *format, *channels, *sampleRate, *status, acoustics, input_flag);

#if 1 // TODO(Harvey): why.........
    mStreamInIndex = (uint32_t)(*status);
#endif

    // create stream in
    AudioALSAStreamIn *pAudioALSAStreamIn = new AudioALSAStreamIn();
    audio_devices_t input_device = static_cast<audio_devices_t>(devices);
    int mNumPhoneMicSupport = AudioCustParamClient::GetInstance()->getNumMicSupport();

    if ((input_device == AUDIO_DEVICE_IN_BACK_MIC) && (mNumPhoneMicSupport < 2)) {
        input_device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        devices = static_cast<uint32_t>(input_device);
        ALOGW("%s(), not support back_mic if mic < 2, force to set input_device = 0x%x", __FUNCTION__, input_device);
    }

    bool sharedDevice = (input_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
    //In PhonecallMode and the new input device is sharedDevice,we may do some check
    if ((isPhoneCallOpen() == true) && (sharedDevice == true)) {
        input_device = mSpeechPhoneCallController->getAdjustedInputDevice();
        sharedDevice = (input_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
        if (sharedDevice == true) { //if phonecall_device also use sharedDevice, set the input_device = phonecall_device
            devices = static_cast<uint32_t>(input_device);
            ALOGD("+%s(), isPhoneCallOpen, force to set input_device = 0x%x", __FUNCTION__, input_device);
        }
    }

#ifdef UPLINK_LOW_LATENCY
    pAudioALSAStreamIn->set(devices, format, channels, sampleRate, status, acoustics, input_flag);
#else
    pAudioALSAStreamIn->set(devices, format, channels, sampleRate, status, acoustics);
#endif
    if (*status != NO_ERROR) {
        ALOGE("-%s(), set fail, return NULL", __FUNCTION__);
        delete pAudioALSAStreamIn;
        pAudioALSAStreamIn = NULL;
        return NULL;
    }

    // save stream in object in vector
#if 0 // TODO(Harvey): why.........
    pAudioALSAStreamIn->setIdentity(mStreamInIndex);
    mStreamInVector.add(mStreamInIndex, pAudioALSAStreamIn);
    mStreamInIndex++;
#else
    pAudioALSAStreamIn->setIdentity(mStreamInIndex);
    mStreamInVector.add(mStreamInIndex, pAudioALSAStreamIn);
#endif

    ALOGD_IF(mLogEnable, "-%s(), in = %p, status = 0x%x, mStreamInVector.size() = %zu",
             __FUNCTION__, pAudioALSAStreamIn, *status, mStreamInVector.size());
    return pAudioALSAStreamIn;
}


void AudioALSAStreamManager::closeInputStream(AudioMTKStreamInInterface *in) {
    ALOGD("+%s(), in = %p, size() = %zu", __FUNCTION__, in, mStreamInVector.size());
    AL_AUTOLOCK(mStreamVectorLock);
    AL_AUTOLOCK(mLock);

    if (in == NULL) {
        ALOGE("%s(), Cannot close null input stream!! return", __FUNCTION__);
        return;
    }

    AudioALSAStreamIn *pAudioALSAStreamIn = static_cast<AudioALSAStreamIn *>(in);
    ASSERT(pAudioALSAStreamIn != 0);

#ifdef MTK_VOW_SUPPORT
    // close HOTWORD training
    if ((mStreamInVector.size() == 1) && (mVoiceWakeUpNeedOn == false)) {
        const stream_attribute_t *streamInAttr = mStreamInVector[0]->getStreamAttribute();
        if (streamInAttr != NULL) {
            if (streamInAttr->input_source == AUDIO_SOURCE_HOTWORD) {
                setVoiceWakeUpEnable(false);
            }
        } else {
            ALOGE("%s(), mStreamInVector[0]->getStreamAttribute() = NULL", __FUNCTION__);
        }
    }
#endif

    mStreamInVector.removeItem(pAudioALSAStreamIn->getIdentity());
    delete pAudioALSAStreamIn;

    if (mStreamInVector.size() == 0) {
        mAudioSpeechEnhanceInfoInstance->SetHifiRecord(false);
    } else {
        bool bClear = true;
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            pAudioALSAStreamIn = mStreamInVector[i];

            if (pAudioALSAStreamIn->getStreamInCaptureHandler() == NULL) {
                ALOGD("%s(), mStreamInVector[%zu] capture handler close already", __FUNCTION__, i);
                continue;
            }

            if (pAudioALSAStreamIn->getStreamInCaptureHandler()->getCaptureHandlerType() == CAPTURE_HANDLER_NORMAL) {
                bClear = false;
                break;
            }
        }
        if (bClear) { //if still has Normal capture handler, not to reset hifi record status.
            mAudioSpeechEnhanceInfoInstance->SetHifiRecord(false);
        }
    }

    ALOGD_IF(mLogEnable, "-%s(), mStreamInVector.size() = %zu", __FUNCTION__, mStreamInVector.size());
}

void dlStreamAttributeSourceCustomization(stream_attribute_t *streamAttribute) {
    if (!strcmp(streamAttribute->mCustScene, "App1")) {
        /* App1 Scene customization */
    }
}

AudioALSAPlaybackHandlerBase *AudioALSAStreamManager::createPlaybackHandler(
    stream_attribute_t *stream_attribute_source) {

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    bool isIEMsOn = AudioIEMsController::getInstance()->isIEMsOn();
#else
    bool isIEMsOn = false;
#endif

    ALOGD("+%s(), mAudioMode = %d, output_devices = 0x%x, isMixerOut = %d, isBypassAurisys = %d, flag = %d, isIEMsSource = %d, isIEMsOn = %d",
          __FUNCTION__, mAudioMode, stream_attribute_source->output_devices,
          stream_attribute_source->isMixerOut, stream_attribute_source->isBypassAurisys, stream_attribute_source->mAudioOutputFlags,
          stream_attribute_source->isIEMsSource, isIEMsOn);

    AL_AUTOLOCK(mAudioModeLock);

    // Init input stream attribute here
    stream_attribute_source->audio_mode = mAudioMode; // set mode to stream attribute for mic gain setting
    stream_attribute_source->mVoIPEnable = needEnableVoip(stream_attribute_source);
    stream_attribute_source->stream_type = mStreamType;

    if (stream_attribute_source->mAudioOutputFlags == AUDIO_OUTPUT_FLAG_NONE){
        if (audio_is_usb_out_device(stream_attribute_source->output_devices) ||
            isUsbSpkDevice(stream_attribute_source->output_devices)) {
            ALOGD("%s(), dynamic stream out for usb hifi playback!!", __FUNCTION__);
        } else {
            ALOGD("%s(), mAudioOutputFlags none => fix to primary", __FUNCTION__);
            stream_attribute_source->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_PRIMARY;
        }
    }

#if defined(NON_SCENE_3GVT_VOWIFI_SUPPORT)
    stream_attribute_source->mIsVoWifi = mIsVoWifi;
    stream_attribute_source->mIsVoWifiWbAmr = mIsVoWifiWbAmr;
    stream_attribute_source->mIs3GVT = mIs3GVT;
    ALOGD("%s(), stream_attribute_source->mIsVoWifi = %d(WB=%d), mVoIPEnable = %d, mIs3GVT = %d\n", __FUNCTION__, stream_attribute_source->mIsVoWifi, stream_attribute_source->mIsVoWifiWbAmr, stream_attribute_source->mVoIPEnable, stream_attribute_source->mIs3GVT);
#endif

    // just use what stream out is ask to use
    //stream_attribute_source->sample_rate = AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate();

    //for DMNR tuning
    stream_attribute_source->BesRecord_Info.besrecord_dmnr_tuningEnable = mAudioSpeechEnhanceInfoInstance->IsAPDMNRTuningEnable();
    stream_attribute_source->bBypassPostProcessDL = mBypassPostProcessDL;
    strncpy(stream_attribute_source->mCustScene, mCustScene.string(), SCENE_NAME_LEN_MAX - 1);

    //todo:: enable ACF if support
    if (stream_attribute_source->sample_rate > 48000) {
        stream_attribute_source->bBypassPostProcessDL = true;
    }

    dlStreamAttributeSourceCustomization(stream_attribute_source);

    // create
    AudioALSAPlaybackHandlerBase *pPlaybackHandler = NULL;
    if (isPhoneCallOpen() == true) {
        switch (stream_attribute_source->output_devices) {
        case AUDIO_DEVICE_OUT_AUX_DIGITAL:
#if defined(MTK_AUDIO_TVOUT_SUPPORT) && defined(MTK_AUDIO_KS)
            pPlaybackHandler = new AudioALSAPlaybackHandlerTVOut(stream_attribute_source);
#else
            pPlaybackHandler = new AudioALSAPlaybackHandlerHDMI(stream_attribute_source);
#endif
            break;
        default:
            pPlaybackHandler = new AudioALSAPlaybackHandlerVoice(stream_attribute_source);
            break;
        }
    } else {
        switch (stream_attribute_source->output_devices) {
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET:
        case AUDIO_DEVICE_OUT_BLUETOOTH_SCO_CARKIT: {
#if defined(HAVE_SW_MIXER)
            if (!stream_attribute_source->isMixerOut) {
                pPlaybackHandler = new AudioALSAPlaybackHandlerMixer(stream_attribute_source);
            } else
#endif
            {
                if (WCNChipController::GetInstance()->IsBTMergeInterfaceSupported() == true) {
                    pPlaybackHandler = new AudioALSAPlaybackHandlerBTSCO(stream_attribute_source);
                } else {
                    pPlaybackHandler = new AudioALSAPlaybackHandlerBTCVSD(stream_attribute_source);
                }
            }
            break;
        }
        case AUDIO_DEVICE_OUT_AUX_DIGITAL: {
#if defined(MTK_AUDIO_TVOUT_SUPPORT) && defined(MTK_AUDIO_KS)
            pPlaybackHandler = new AudioALSAPlaybackHandlerTVOut(stream_attribute_source);
#else
            pPlaybackHandler = new AudioALSAPlaybackHandlerHDMI(stream_attribute_source);
#endif
            break;
        }
        case AUDIO_DEVICE_OUT_FM: {
            pPlaybackHandler = new AudioALSAPlaybackHandlerFMTransmitter(stream_attribute_source);
            break;
        }
#if defined(PRIMARY_USB)
        case AUDIO_DEVICE_OUT_USB_DEVICE:
        case AUDIO_DEVICE_OUT_USB_HEADSET: {
#if defined(HAVE_SW_MIXER)
            if (!stream_attribute_source->isMixerOut) {
                pPlaybackHandler = new AudioALSAPlaybackHandlerMixer(stream_attribute_source);
            } else
#endif
            {
                pPlaybackHandler = new AudioALSAPlaybackHandlerUsb(stream_attribute_source);
            }
            break;
        }
#endif
        case AUDIO_DEVICE_OUT_EARPIECE:
        case AUDIO_DEVICE_OUT_WIRED_HEADSET:
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
        case AUDIO_DEVICE_OUT_SPEAKER:
        default: {
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
            if (AudioIEMsController::getInstance()->isIEMsOn() &&
                !stream_attribute_source->isMixerOut &&
                !(AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD & stream_attribute_source->mAudioOutputFlags) &&
                !(AUDIO_OUTPUT_FLAG_MMAP_NOIRQ & stream_attribute_source->mAudioOutputFlags)) {
                pPlaybackHandler = new AudioALSAPlaybackHandlerMixer(stream_attribute_source);
                break;
            }
#endif

#if defined(MTK_AUDIODSP_SUPPORT)
            if (isAdspOptionEnable() &&
                (AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD & stream_attribute_source->mAudioOutputFlags) &&
                AudioDspStreamManager::getInstance()->getDspOutHandlerEnable(stream_attribute_source->mAudioOutputFlags,
                                                                             stream_attribute_source->output_devices)) {
                pPlaybackHandler = new AudioALSAPlaybackHandlerOffload(stream_attribute_source);
                break;
            }
#endif

            if (isBtSpkDevice(stream_attribute_source->output_devices) ||
                isUsbSpkDevice(stream_attribute_source->output_devices) ||
                isEarphoneSpkDevice(stream_attribute_source->output_devices)) {
#if defined(HAVE_SW_MIXER)
                if (!stream_attribute_source->isMixerOut) {
                    pPlaybackHandler = new AudioALSAPlaybackHandlerMixer(stream_attribute_source);
                    break;
                }
#else
                stream_attribute_source->output_devices = AUDIO_DEVICE_OUT_SPEAKER;
#endif
            }

            /* use normal playback for earphone when ringtone/alarm with spk+hs/hp */
            if (stream_attribute_source->dualDevice != AUDIO_DEVICE_NONE &&
                (stream_attribute_source->output_devices == AUDIO_DEVICE_OUT_WIRED_HEADSET ||
                 stream_attribute_source->output_devices == AUDIO_DEVICE_OUT_WIRED_HEADPHONE)) {
                pPlaybackHandler = new AudioALSAPlaybackHandlerNormal(stream_attribute_source);
                break;
            }

#if defined(HAVE_SW_MIXER)
            if (!stream_attribute_source->isMixerOut &&
                AudioALSAHardwareResourceManager::getInstance()->isSpkApSwMixSupport(stream_attribute_source->output_devices)) {
                pPlaybackHandler = new AudioALSAPlaybackHandlerMixer(stream_attribute_source);
                break;
            }
#endif

#ifdef DOWNLINK_LOW_LATENCY
            if (AUDIO_OUTPUT_FLAG_FAST & stream_attribute_source->mAudioOutputFlags &&
                !(AUDIO_OUTPUT_FLAG_PRIMARY & stream_attribute_source->mAudioOutputFlags)) {
#if defined(MTK_AUDIODSP_SUPPORT)
                if (isAdspOptionEnable() &&
                    AudioDspStreamManager::getInstance()->getDspOutHandlerEnable(stream_attribute_source->mAudioOutputFlags,
                                                                                 stream_attribute_source->output_devices)) {
                        pPlaybackHandler = new AudioALSAPlaybackHandlerDsp(stream_attribute_source);
                    } else
#endif
                    {
                        pPlaybackHandler = new AudioALSAPlaybackHandlerFast(stream_attribute_source);
                    }
                    break;
                }
#if defined(MTK_AUDIO_AAUDIO_SUPPORT)
                else if (AUDIO_OUTPUT_FLAG_MMAP_NOIRQ & stream_attribute_source->mAudioOutputFlags) {
                    pPlaybackHandler = new AudioALSAPlaybackHandlerAAudio(stream_attribute_source);
                    break;
                }
#endif
                else
#endif
                {
#if defined(MTK_AUDIODSP_SUPPORT)
                    if (isAdspOptionEnable() &&
                        !AudioSmartPaController::getInstance()->isInCalibration() &&
                        AudioDspStreamManager::getInstance()->getDspOutHandlerEnable(stream_attribute_source->mAudioOutputFlags,
                                                                                     stream_attribute_source->output_devices)) {
                        pPlaybackHandler = new AudioALSAPlaybackHandlerDsp(stream_attribute_source);
                    } else
#endif
                    {
                        pPlaybackHandler = new AudioALSAPlaybackHandlerNormal(stream_attribute_source);
                    }
                    break;
                }
        }
        }
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
        uint32_t dFltMngindex = mFilterManagerVector.indexOfKey(stream_attribute_source->mStreamOutIndex);
        ALOGV("%s(), ApplyFilter [%u]/[%zu] Device [0x%x]", __FUNCTION__, dFltMngindex, mFilterManagerVector.size(), stream_attribute_source->output_devices);

        if (dFltMngindex < mFilterManagerVector.size()) {
            pPlaybackHandler->setFilterMng(static_cast<AudioMTKFilterManager *>(mFilterManagerVector[dFltMngindex]));
            mFilterManagerVector[dFltMngindex]->setDevice(stream_attribute_source->output_devices);
        }
#endif
    }

    // save playback handler object in vector
    if (pPlaybackHandler) {
        pPlaybackHandler->setIdentity(mPlaybackHandlerIndex);

        AL_LOCK(mPlaybackHandlerVectorLock);
        mPlaybackHandlerVector.add(mPlaybackHandlerIndex, pPlaybackHandler);
        AL_UNLOCK(mPlaybackHandlerVectorLock);

        mPlaybackHandlerIndex++;

#if defined(MTK_AUDIO_SW_DRE) && defined(MTK_NEW_VOL_CONTROL)
        AudioMTKGainController::getInstance()->registerPlaybackHandler(pPlaybackHandler->getIdentity());
#endif
    } else {
        ASSERT(pPlaybackHandler != NULL);
    }

    ALOGD_IF(mLogEnable, "-%s(), mPlaybackHandlerVector.size() = %zu", __FUNCTION__, mPlaybackHandlerVector.size());
    return pPlaybackHandler;
}


status_t AudioALSAStreamManager::destroyPlaybackHandler(AudioALSAPlaybackHandlerBase *pPlaybackHandler) {
    ALOGV("+%s(), mode = %d, pPlaybackHandler = %p", __FUNCTION__, mAudioMode, pPlaybackHandler);
    //AL_AUTOLOCK(mLock); // TODO(Harvey): setparam -> routing -> close -> destroy deadlock

    status_t status = NO_ERROR;

#if defined(MTK_AUDIO_SW_DRE) && defined(MTK_NEW_VOL_CONTROL)
    AudioMTKGainController::getInstance()->removePlaybackHandler(pPlaybackHandler->getIdentity());
#endif

    AL_LOCK(mPlaybackHandlerVectorLock);
    mPlaybackHandlerVector.removeItem(pPlaybackHandler->getIdentity());
    AL_UNLOCK(mPlaybackHandlerVectorLock);

    ALOGD_IF(mLogEnable, "-%s(), mode = %d, pPlaybackHandler = %p, mPlaybackHandlerVector.size() = %zu",
             __FUNCTION__, mAudioMode, pPlaybackHandler, mPlaybackHandlerVector.size());

    delete pPlaybackHandler;

    return status;
}

void ulStreamAttributeTargetCustomization(stream_attribute_t *streamAttribute) {
    if (!strcmp((char *)streamAttribute->mCustScene, "App1")) {
        /* App1 Scene customization */
    } else if (!strcmp((char *)streamAttribute->mCustScene, "App2")) {
        /* App2 Scene customization: normal record will using VoIP processing */
        if (streamAttribute->input_source == AUDIO_SOURCE_MIC) {
            streamAttribute->input_source = AUDIO_SOURCE_VOICE_COMMUNICATION;
            ALOGD("%s(), Scene is App2, replace MIC input source with communication", __FUNCTION__);
        }
    } else if (!strcmp(streamAttribute->mCustScene, "ASR")) {
        /* ASR Scene customization: Voice recognition + AEC processing */
        if (streamAttribute->input_source == AUDIO_SOURCE_VOICE_RECOGNITION) {
            // Change VR input source to MagiASR(81), ASR parameter is already built-in in XML
            streamAttribute->input_source = AUDIO_SOURCE_CUSTOMIZATION1;
        }
        ALOGD("%s(), Scene is ASR, input source is %d", __FUNCTION__, streamAttribute->input_source);
    } else if (!strcmp(streamAttribute->mCustScene, "AEC_REC")) {
        /* AEC_REC Scene customization: Normal record + AEC processing */
        if (streamAttribute->input_source == AUDIO_SOURCE_MIC) {
            // Change normal record input source with CUSTOMIZATION2(82), CUSTOMIZATION2 parameter is already built-in in XML
            streamAttribute->input_source = AUDIO_SOURCE_CUSTOMIZATION2;
        }
        ALOGD("%s(), Scene is AEC_REC, input source is %d", __FUNCTION__, streamAttribute->input_source);
    }
}

bool AudioALSAStreamManager::isCaptureOffload(stream_attribute_t *stream_attribute_target __unused) {
    bool offload = false;

    if (!isAdspOptionEnable()) {
        return offload;
    }

#ifdef MTK_AUDIODSP_SUPPORT
    if (AudioDspStreamManager::getInstance()->getDspInHandlerEnable(stream_attribute_target->mAudioInputFlags)) {
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
        const char *paramPath = "";
        if (stream_attribute_target->mVoIPEnable) {
            paramPath = VOIP_PATH;
        } else if (stream_attribute_target->mAudioInputFlags & AUDIO_INPUT_FLAG_FAST) {
            switch (stream_attribute_target->input_source) {
            case AUDIO_SOURCE_VOICE_PERFORMANCE:
                paramPath = VOICE_PERFORMANCE_PATH;
                break;
            default:
                paramPath = FAST_RECORD_PATH;
                break;
            }
        } else {
            switch (stream_attribute_target->input_source) {
            case AUDIO_SOURCE_CAMCORDER:
                paramPath = CAMCORDER_PATH;
                break;
            case AUDIO_SOURCE_VOICE_RECOGNITION:
                paramPath = VOICE_RECOGNITION_PATH;
                break;
            case AUDIO_SOURCE_VOICE_COMMUNICATION:
                paramPath = VOIP_PATH;
                break;
            case AUDIO_SOURCE_UNPROCESSED:
                paramPath = UNPROCESSED_PATH;
                break;
            case AUDIO_SOURCE_VOICE_PERFORMANCE:
                paramPath = VOICE_PERFORMANCE_PATH;
                break;
            case AUDIO_SOURCE_DEFAULT:
            case AUDIO_SOURCE_MIC:
            default:
                paramPath = MIC_PATH;
                break;
            }
        }

        AppOps *appOps = appOpsGetInstance();
        if (appOps == NULL) {
            ASSERT(appOps);
            return true;
        }

        AudioType *audioType = appOps->appHandleGetAudioTypeByName(appOps->appHandleGetInstance(), UPLINK_CUSTOMIZATION_AUDIO_TYPE);
        if (!audioType) {
            return true;
        }

        ParamUnit *paramUnit = appOps->audioTypeGetParamUnit(audioType, paramPath);
        if (!paramUnit) {
            return true;
        }

        Param *param = appOps->paramUnitGetParamByName(paramUnit, OFFLOAD_PARAM_NAME);
        if (!param) {
            offload = true;
        } else {
            offload = *((int *)param->data);
            ALOGD("%s(), offload customization is %d (%s)\n", __FUNCTION__, offload, paramPath);
        }
#else
        /* No XML support, default offload enabled */
        offload = true;
#endif
    }
#endif /* end of #ifdef MTK_AUDIODSP_SUPPORT */

    return offload;
}


bool AudioALSAStreamManager::checkHalIEMsInOn(const stream_attribute_t *attr) {
#if !defined(HAVE_IEMS)
    (void)attr;
    return false;
#else
    if (!AudioIEMsController::getInstance()->isIEMsOn()) {
        return false;
    }
    if (attr->audio_mode != AUDIO_MODE_NORMAL) {
        return false;
    }
    if (attr->input_source == AUDIO_SOURCE_VOICE_UPLINK ||
        attr->input_source == AUDIO_SOURCE_VOICE_DOWNLINK ||
        attr->input_source == AUDIO_SOURCE_VOICE_CALL ||
        attr->input_source == AUDIO_SOURCE_CAMCORDER ||
        attr->input_source == AUDIO_SOURCE_VOICE_RECOGNITION ||
        attr->input_source == AUDIO_SOURCE_VOICE_COMMUNICATION ||
        attr->input_source == AUDIO_SOURCE_REMOTE_SUBMIX ||
        attr->input_source == AUDIO_SOURCE_UNPROCESSED ||
        attr->input_source == AUDIO_SOURCE_VOICE_UNLOCK ||
        attr->input_source == AUDIO_SOURCE_CUSTOMIZATION1 || // VOIP
        attr->input_source == AUDIO_SOURCE_CUSTOMIZATION2 || // VOIP
        attr->input_source == AUDIO_SOURCE_CUSTOMIZATION3 || // Speech Voice
        attr->input_source == AUDIO_SOURCE_ECHO_REFERENCE ||
        attr->input_source == AUDIO_SOURCE_FM_TUNER ||
        attr->input_source == AUDIO_SOURCE_HOTWORD) {
        return false;
    }
    if (attr->mAudioInputFlags == AUDIO_INPUT_FLAG_HW_HOTWORD ||
        attr->mAudioInputFlags == AUDIO_INPUT_FLAG_RAW ||
        attr->mAudioInputFlags == AUDIO_INPUT_FLAG_SYNC ||
        attr->mAudioInputFlags == AUDIO_INPUT_FLAG_MMAP_NOIRQ ||
        attr->mAudioInputFlags == AUDIO_INPUT_FLAG_VOIP_TX ||
        attr->mAudioInputFlags == AUDIO_INPUT_FLAG_HW_AV_SYNC ||
        attr->mAudioInputFlags == AUDIO_INPUT_FLAG_DIRECT) {
        return false;
    }

    // usb & bt & headset only
    if (audio_is_usb_in_device(attr->input_device) ||
        audio_is_bluetooth_in_sco_device(attr->input_device) ||
        attr->input_device == AUDIO_DEVICE_IN_WIRED_HEADSET) {
        return true;
    }

    return false;
#endif
}


AudioALSACaptureHandlerBase *AudioALSAStreamManager::createCaptureHandler(
    stream_attribute_t *stream_attribute_target) {
    ALOGD("+%s(), mAudioMode = %d, input_source = %d, input_device = 0x%x, mBypassDualMICProcessUL=%d, rate=%d, flag=0x%x",
          __FUNCTION__, mAudioMode, stream_attribute_target->input_source,
          stream_attribute_target->input_device, mBypassDualMICProcessUL,
          stream_attribute_target->sample_rate,
          stream_attribute_target->mAudioInputFlags);
    //AL_AUTOLOCK(mLock);
    bool bReCreate = false;
    status_t retval = AL_LOCK_MS_NO_ASSERT(mLock, 1000);
    if (retval != NO_ERROR) {
        ALOGD("mLock timeout : 1s , return NULL");
        return NULL;
    }

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    bool isIEMsOn = AudioIEMsController::getInstance()->isIEMsOn();
#else
    bool isIEMsOn = false;
#endif

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->beforeInputDeviceRouting(
            stream_attribute_target->input_device);
#endif

    bool sharedDevice = (stream_attribute_target->input_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
    if ((sharedDevice == true) && (mStreamInVector.size() > 0)) {
#ifdef MTK_CHECK_INPUT_DEVICE_PRIORITY
        stream_attribute_target->input_device = CheckInputDevicePriority(stream_attribute_target->input_device);
#endif
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            if (mStreamInVector[i] == NULL ||
                mStreamInVector[i]->getStreamAttribute() == NULL) {
                ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                continue;
            }
            //Need to close AAudio old device before other streamin routing
            if ((mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) &&
                (mStreamInVector[i]->getStreamAttribute()->input_device != stream_attribute_target->input_device)) {
                ALOGD("%s(), AAudio record(0x%x) input_device: 0x%x => 0x%x", __FUNCTION__,
                    mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags,
                    mStreamInVector[i]->getStreamAttribute()->input_device,
                    stream_attribute_target->input_device);

                if (mStreamInVector[i]->getStreamInCaptureHandler() == NULL) {
                    ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                    continue;
                }
                if (mStreamInVector[i]->isActive() == true) {
                    ALOGW("%s(), AAudio record(0x%x), switch to new device directly",
                          __FUNCTION__, mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags);

                    mStreamInVector[i]->getStreamInCaptureHandler()->routing(stream_attribute_target->input_device);
                } else {
                    ALOGW("%s(), AAudio record(0x%x) not active, skip routing",
                          __FUNCTION__, mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags);
                }

                mStreamInVector[i]->getStreamInCaptureHandler()->updateInputDevice(stream_attribute_target->input_device);
            }
        }
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            if (mStreamInVector[i] == NULL || mStreamInVector[i]->getStreamAttribute() == NULL) {
                ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                continue;
            }
            sharedDevice = ((mStreamInVector[i]->getStreamAttribute()->input_device) & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
            if ((sharedDevice == true) && ((mStreamInVector[i]->getStreamAttribute()->input_device) != stream_attribute_target->input_device)) {
                mStreamInVector[i]->routing(stream_attribute_target->input_device);
            }
        }
    }

    // use primary stream out device as default
    audio_devices_t current_output_devices = (mStreamOutVector.size() > 0) ?
                                             mStreamOutVector[0]->getStreamAttribute()->output_devices :
                                             AUDIO_DEVICE_NONE;

    // use active stream out device
    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        if (mStreamOutVector[i]->isOutPutStreamActive()) {
            current_output_devices = mStreamOutVector[i]->getStreamAttribute()->output_devices;
            break;
        }
    }

    if (isBtSpkDevice(current_output_devices)) {
        // use SPK setting for BTSCO + SPK
        current_output_devices = (audio_devices_t)(current_output_devices & (~AUDIO_DEVICE_OUT_ALL_SCO));
    } else if (isUsbSpkDevice(current_output_devices)) {
        // use SPK setting for USB + SPK
        current_output_devices = (audio_devices_t)(current_output_devices & (~AUDIO_DEVICE_OUT_ALL_USB));
    } else if (isEarphoneSpkDevice(current_output_devices)) {
        // use SPK setting for USB + SPK
        current_output_devices = (audio_devices_t)(current_output_devices & (~(AUDIO_DEVICE_OUT_WIRED_HEADSET | AUDIO_DEVICE_OUT_WIRED_HEADPHONE)));
    }

    // Init input stream attribute here
    stream_attribute_target->audio_mode = mAudioMode; // set mode to stream attribute for mic gain setting
    stream_attribute_target->output_devices = current_output_devices; // set output devices to stream attribute for mic gain setting and BesRecord parameter
    stream_attribute_target->micmute = mMicMute;
    strncpy(stream_attribute_target->mCustScene, mCustScene.string(), SCENE_NAME_LEN_MAX - 1);
#if defined(NON_SCENE_3GVT_VOWIFI_SUPPORT)
    stream_attribute_target->mIs3GVT = mIs3GVT;
    stream_attribute_target->mIsVoWifi = mIsVoWifi;
    stream_attribute_target->mIsVoWifiWbAmr = mIsVoWifiWbAmr;
    ALOGD("%s(), stream_attribute_target->mIsVoWifi = %d(WB=%d), mIs3GVT = %d\n", __FUNCTION__, stream_attribute_target->mIsVoWifi, stream_attribute_target->mIsVoWifiWbAmr, stream_attribute_target->mIs3GVT);
#endif

    // BesRecordInfo
    stream_attribute_target->BesRecord_Info.besrecord_enable = false; // default set besrecord off
    stream_attribute_target->BesRecord_Info.besrecord_bypass_dualmicprocess = mBypassDualMICProcessUL;  // bypass dual MIC preprocess
    stream_attribute_target->BesRecord_Info.besrecord_voip_enable = false;
    stream_attribute_target->mVoIPEnable = false;
    ALOGD_IF(mLogEnable, "%s(), ulStreamAttributeTargetCustomization", __FUNCTION__);

    /* StreamAttribute customization for scene */
    ulStreamAttributeTargetCustomization(stream_attribute_target);

    ALOGD_IF(mLogEnable, "%s(), ulStreamAttributeTargetCustomization done", __FUNCTION__);
    // create
    AudioALSACaptureHandlerBase *pCaptureHandler = NULL;
#if 0
#if defined(MTK_SPEAKER_MONITOR_SPEECH_SUPPORT)
    if (stream_attribute_target->input_device == AUDIO_DEVICE_IN_SPK_FEED) {
        pCaptureHandler = new AudioALSACaptureHandlerModemDai(stream_attribute_target);
    } else
#endif
#endif
    {
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
        if (checkHalIEMsInOn(stream_attribute_target)) {
            pCaptureHandler = new AudioALSACaptureHandlerIEMs(stream_attribute_target);
        } else
#endif
        if (stream_attribute_target->input_source == AUDIO_SOURCE_FM_TUNER
#if defined(SUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL)
            || stream_attribute_target->input_source == AUDIO_SOURCE_ODM_FM_RX
#endif
           ) {
            if (isEchoRefUsing() == true) {
                ALOGD("%s(), not support FM record in VoIP mode, return NULL", __FUNCTION__);
                AL_UNLOCK(mLock);
                return NULL;
            }

            pCaptureHandler = new AudioALSACaptureHandlerFMRadio(stream_attribute_target);
#if defined(MTK_VOW_SUPPORT)
        } else if (stream_attribute_target->input_source == AUDIO_SOURCE_HOTWORD) {
            if (mForceDisableVoiceWakeUpForPhoneCall == true) {
                ALOGD("%s(), ForceDisableVoiceWakeUpForSetMode, return", __FUNCTION__);
                AL_UNLOCK(mLock);
                return NULL;
            } else {
                if (mAudioALSAVoiceWakeUpController->getVoiceWakeUpEnable() == false) {
                    mAudioALSAVoiceWakeUpController->setVoiceWakeUpEnable(true);
                }
                if (mVoiceWakeUpNeedOn == true) {
                    mAudioALSAVoiceWakeUpController->SeamlessRecordEnable();
                }
                pCaptureHandler = new AudioALSACaptureHandlerVOW(stream_attribute_target);
            }
#endif
        } else if (stream_attribute_target->input_source == AUDIO_SOURCE_VOICE_UNLOCK ||
                   stream_attribute_target->input_source == AUDIO_SOURCE_ECHO_REFERENCE) {
            if (stream_attribute_target->input_source == AUDIO_SOURCE_ECHO_REFERENCE && isEchoRefUsing() == true) {
                ALOGD("%s(), not support EchoRef  record in VoIP mode, return NULL", __FUNCTION__);
                AL_UNLOCK(mLock);
                return NULL;
            }
            pCaptureHandler = new AudioALSACaptureHandlerSyncIO(stream_attribute_target);
        } else if (isPhoneCallOpen() == true) {
#if defined(MTK_BT_HEARING_AID_SUPPORT)
            if (appIsFeatureOptionEnabled("MTK_BT_HEARING_AID_SUPPORT") &&
                (getSoftwareBridgeEnable() == true) &&
                (get_uint32_from_property("persist.vendor.audiohal.asha_test") ||
                (stream_attribute_target->input_source == AUDIO_SOURCE_MIC &&
                stream_attribute_target->input_device == AUDIO_DEVICE_IN_TELEPHONY_RX))) {
                pCaptureHandler = new AudioALSACaptureHandlerSyncIO(stream_attribute_target);
            } else
#endif
            {
                pCaptureHandler = new AudioALSACaptureHandlerVoice(stream_attribute_target);
            }
        } else if ((stream_attribute_target->NativePreprocess_Info.PreProcessEffect_AECOn == true) ||
                   (stream_attribute_target->input_source == AUDIO_SOURCE_VOICE_COMMUNICATION) ||
                   (stream_attribute_target->input_source == AUDIO_SOURCE_CUSTOMIZATION1) || //MagiASR enable AEC
                   (stream_attribute_target->input_source == AUDIO_SOURCE_CUSTOMIZATION2)) { //Normal REC with AEC
            stream_attribute_target->BesRecord_Info.besrecord_enable = EnableBesRecord();
            if (mStreamInVector.size() > 1) {
                for (size_t i = 0; i < mStreamInVector.size(); i++) {
                    if (mStreamInVector[i] == NULL || mStreamInVector[i]->getStreamAttribute() == NULL) {
                        ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                        continue;
                    }
                    if (mStreamInVector[i]->getStreamAttribute()->input_source == AUDIO_SOURCE_FM_TUNER
#if defined(SUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL)
                        || mStreamInVector[i]->getStreamAttribute()->input_source == AUDIO_SOURCE_ODM_FM_RX
#endif
                       ) {
                        mStreamInVector[i]->standby();
                    }
                }
            }
            if ((stream_attribute_target->input_source == AUDIO_SOURCE_VOICE_COMMUNICATION) ||
                (stream_attribute_target->input_source == AUDIO_SOURCE_CUSTOMIZATION1) || //MagiASR enable AEC
                (stream_attribute_target->input_source == AUDIO_SOURCE_CUSTOMIZATION2)) { //Normal REC with AEC
                stream_attribute_target->BesRecord_Info.besrecord_voip_enable = true;
                stream_attribute_target->mVoIPEnable = true;
#if 0
                if (current_output_devices == AUDIO_DEVICE_OUT_SPEAKER) {
                    if (stream_attribute_target->input_device == AUDIO_DEVICE_IN_BUILTIN_MIC) {
                        if (USE_REFMIC_IN_LOUDSPK == 1) {
                            ALOGD("%s(), routing changed!! input_device: 0x%x => 0x%x",
                                  __FUNCTION__, stream_attribute_target->input_device, AUDIO_DEVICE_IN_BACK_MIC);
                            stream_attribute_target->input_device = AUDIO_DEVICE_IN_BACK_MIC;
                        }
                    }
                }
#endif
            }

            AL_LOCK(mCaptureHandlerVectorLock);
            if ((mCaptureHandlerVector.size() > 0) &&
                (stream_attribute_target->input_source == AUDIO_SOURCE_VOICE_COMMUNICATION)) { //already has another streamin ongoing with CAPTURE_HANDLER_DSP
                for (size_t i = 0; i < mCaptureHandlerVector.size(); i++) {
                    if (mCaptureHandlerVector[i]->getCaptureHandlerType() & (CAPTURE_HANDLER_DSP | CAPTURE_HANDLER_NORMAL)) {
                        if (mCaptureHandlerVector[i]->getStreamAttributeTarget() == NULL) {
                            ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                            continue;
                        }
                        if (mCaptureHandlerVector[i]->getStreamAttributeTarget()->input_source == AUDIO_SOURCE_VOICE_COMMUNICATION) {
                            bReCreate = false;
                            break;
                        } else {
                            bReCreate = true;
                        }
                    }
                }
            }
            AL_UNLOCK(mCaptureHandlerVectorLock);

            AudioALSAHardwareResourceManager::getInstance()->setHDRRecord(false); // turn off HRD record for VoIP
            if ((stream_attribute_target->mVoIPEnable == true) && (bReCreate == true)) {
                standbyAllInputStreams(false, (capture_handler_t)(CAPTURE_HANDLER_DSP | CAPTURE_HANDLER_NORMAL));
            }

#if defined(MTK_AURISYS_FRAMEWORK_SUPPORT) && defined(MTK_CUS2_AUDIO_SOURCE_REPLACE_AEC_EFFECT)
            else if (stream_attribute_target->NativePreprocess_Info.PreProcessEffect_AECOn == true
                     && stream_attribute_target->input_source == AUDIO_SOURCE_MIC) {
                // Not VoIP/AEC input source but AEC effect enabled, using customization2 to do AEC processing
                stream_attribute_target->input_source = AUDIO_SOURCE_CUSTOMIZATION2;
                ALOGD("Normal record && AEC enabled, set the input source with %d", stream_attribute_target->input_source);
            }
#endif
            switch (stream_attribute_target->input_device) {
            case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET: {
#if defined(MTK_AURISYS_FRAMEWORK_SUPPORT) || defined(MTK_SUPPORT_BTCVSD_ALSA)
                /* Only BT ALSA+Aurisys arch support AEC processing */
                if (stream_attribute_target->output_devices & AUDIO_DEVICE_OUT_ALL_SCO) {
                    pCaptureHandler = new AudioALSACaptureHandlerAEC(stream_attribute_target);
                } else {
                    pCaptureHandler = new AudioALSACaptureHandlerBT(stream_attribute_target);
                }
#else
                pCaptureHandler = new AudioALSACaptureHandlerBT(stream_attribute_target);
#endif
                break;
            }
#if defined(PRIMARY_USB)
            case AUDIO_DEVICE_IN_USB_DEVICE:
            case AUDIO_DEVICE_IN_USB_HEADSET:
#if defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
                pCaptureHandler = new AudioALSACaptureHandlerAEC(stream_attribute_target);
#else
                pCaptureHandler = new AudioALSACaptureHandlerUsb(stream_attribute_target);
#endif
                break;
#endif
            default: {
#if defined(MTK_AUDIO_AAUDIO_SUPPORT)
                if (AUDIO_INPUT_FLAG_MMAP_NOIRQ & stream_attribute_target->mAudioInputFlags) {
                    if (stream_attribute_target->input_device &
                       (AUDIO_DEVICE_IN_BUILTIN_MIC|AUDIO_DEVICE_IN_BACK_MIC|AUDIO_DEVICE_IN_WIRED_HEADSET)) {
                        pCaptureHandler = new AudioALSACaptureHandlerAAudio(stream_attribute_target);
                        break;
                    }
                }
#endif

#if defined(MTK_AUDIODSP_SUPPORT)
                if (isAdspOptionEnable() &&
                    isCaptureOffload(stream_attribute_target) && !isIEMsOn &&
                    !AudioALSACaptureDataProviderNormal::getInstance()->getNormalOn()) {
                    pCaptureHandler = new AudioALSACaptureHandlerDsp(stream_attribute_target);
                } else
#endif
                {
                    pCaptureHandler = new AudioALSACaptureHandlerAEC(stream_attribute_target);
                }
                break;
            }
            }
        } else {
            //enable BesRecord if not these input sources
            if ((stream_attribute_target->input_source != AUDIO_SOURCE_VOICE_UNLOCK) &&
                (stream_attribute_target->input_source != AUDIO_SOURCE_FM_TUNER
#if defined(SUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL)
                 && stream_attribute_target->input_source != AUDIO_SOURCE_ODM_FM_RX
#endif
                )) { // TODO(Harvey, Yu-Hung): never go through here?
#if 0   //def UPLINK_LOW_LATENCY
                if ((stream_attribute_target->mAudioInputFlags & AUDIO_INPUT_FLAG_FAST) || (stream_attribute_target->sample_rate > 48000)) {
                    stream_attribute_target->BesRecord_Info.besrecord_enable = false;
                } else {
                    stream_attribute_target->BesRecord_Info.besrecord_enable = EnableBesRecord();
                }
#else
                if (stream_attribute_target->sample_rate > 48000) { //no uplink preprocess for sample rate higher than 48k
                    stream_attribute_target->BesRecord_Info.besrecord_enable = false;
                } else {
                    stream_attribute_target->BesRecord_Info.besrecord_enable = EnableBesRecord();
                }
#endif
            }

            bReCreate = false;
            if ((stream_attribute_target->sample_rate > 48000) && !mAudioSpeechEnhanceInfoInstance->GetHifiRecord()) { //no HifiRecord ongoing, and need to create HiFiRecord
                mAudioSpeechEnhanceInfoInstance->SetHifiRecord(true);
                AL_LOCK(mCaptureHandlerVectorLock);
                if (mCaptureHandlerVector.size() > 0) { //already has another streamin ongoing with CAPTURE_HANDLER_NORMAL
                    for (size_t i = 0; i < mCaptureHandlerVector.size(); i++) {
                        if (mCaptureHandlerVector[i]->getCaptureHandlerType() == CAPTURE_HANDLER_NORMAL) {
                            bReCreate = true;
                            break;
                        }
                    }
                }
                AL_UNLOCK(mCaptureHandlerVectorLock);
                if (bReCreate) { //need to re-create related capture handler for dataprovider reopen and dataclient SRC set.
                    ALOGD("%s(), reCreate streamin for hifi record +", __FUNCTION__);
                    //only suspend and standby CAPTURE_HANDLER_NORMAL streamin
                    setAllInputStreamsSuspend(true, false, CAPTURE_HANDLER_NORMAL);
                    standbyAllInputStreams(false, CAPTURE_HANDLER_NORMAL);
                    setAllInputStreamsSuspend(false, false, CAPTURE_HANDLER_NORMAL);
                    ALOGD("%s(), reCreate streamin for hifi record -", __FUNCTION__);
                }
            }

            switch (stream_attribute_target->input_device) {
            case AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET: {
                pCaptureHandler = new AudioALSACaptureHandlerBT(stream_attribute_target);
                break;
            }
#if defined(PRIMARY_USB)
            case AUDIO_DEVICE_IN_USB_DEVICE:
            case AUDIO_DEVICE_IN_USB_HEADSET:
                pCaptureHandler = new AudioALSACaptureHandlerUsb(stream_attribute_target);
                break;
#endif
#if defined(MTK_TDM_SUPPORT)
            case AUDIO_DEVICE_IN_HDMI: {
                pCaptureHandler = new AudioALSACaptureHandlerTDM(stream_attribute_target);
                break;
            }
#endif
            case AUDIO_DEVICE_IN_BUILTIN_MIC:
            case AUDIO_DEVICE_IN_BACK_MIC:
            case AUDIO_DEVICE_IN_WIRED_HEADSET:
            default: {
                if (AudioSmartPaController::getInstance()->isInCalibration()) {
                    pCaptureHandler = new AudioALSACaptureHandlerNormal(stream_attribute_target);
                    break;
                }
#ifdef MTK_AUDIODSP_SUPPORT
                if (isAdspOptionEnable() &&
                    isCaptureOffload(stream_attribute_target) &&
                    !(AUDIO_INPUT_FLAG_MMAP_NOIRQ & stream_attribute_target->mAudioInputFlags) &&
                    !isIEMsOn && !AudioALSACaptureDataProviderNormal::getInstance()->getNormalOn()) {
                    if (isPhoneCallOpen() == true) {
                        pCaptureHandler = new AudioALSACaptureHandlerVoice(stream_attribute_target);
                    } else {
                        pCaptureHandler = new AudioALSACaptureHandlerDsp(stream_attribute_target);
                    }
                } else
#endif
                {
#if defined(MTK_AUDIO_AAUDIO_SUPPORT)
                    if (AUDIO_INPUT_FLAG_MMAP_NOIRQ & stream_attribute_target->mAudioInputFlags) {
                        pCaptureHandler = new AudioALSACaptureHandlerAAudio(stream_attribute_target);
                    } else
#endif
                    {
                        pCaptureHandler = new AudioALSACaptureHandlerNormal(stream_attribute_target);
                    }
                }
                break;
            }
            }
        }
    }
#if defined(MTK_VOW_SUPPORT)
    if (stream_attribute_target->input_source != AUDIO_SOURCE_HOTWORD) {
        if (mAudioALSAVoiceWakeUpController->getVoiceWakeUpEnable() == true) {
            ALOGI("Not Hotword Record, Actually Force Close VOW");
            mAudioALSAVoiceWakeUpController->setVoiceWakeUpEnable(false);
        }
    }
#endif
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->afterInputDeviceRouting(
        stream_attribute_target->input_device);
#endif
    // save capture handler object in vector
    ASSERT(pCaptureHandler != NULL);
    pCaptureHandler->setIdentity(mCaptureHandlerIndex);
    AL_LOCK(mCaptureHandlerVectorLock);
    mCaptureHandlerVector.add(mCaptureHandlerIndex, pCaptureHandler);
    AL_UNLOCK(mCaptureHandlerVectorLock);
    mCaptureHandlerIndex++;
    AL_UNLOCK(mLock);
    ALOGD_IF(mLogEnable, "-%s(), mCaptureHandlerVector.size() = %zu", __FUNCTION__, mCaptureHandlerVector.size());
    return pCaptureHandler;
}


status_t AudioALSAStreamManager::destroyCaptureHandler(AudioALSACaptureHandlerBase *pCaptureHandler) {
    ALOGD_IF(mLogEnable, "%s(), mode = %d, pCaptureHandler = %p", __FUNCTION__, mAudioMode, pCaptureHandler);
    //AL_AUTOLOCK(mLock); // TODO(Harvey): setparam -> routing -> close -> destroy deadlock

    status_t status = NO_ERROR;
    AL_AUTOLOCK(mCaptureHandlerVectorLock);

    mCaptureHandlerVector.removeItem(pCaptureHandler->getIdentity());
    delete pCaptureHandler;

#ifdef MTK_VOW_SUPPORT
    // make sure voice wake up is resume when all capture stream stop if need
    if (mVoiceWakeUpNeedOn == true) {
        ALOGD("%s(), resume voice wake up", __FUNCTION__);
        setVoiceWakeUpEnable_l(true);
    }
#endif

    ALOGD_IF(mLogEnable, "-%s(), mCaptureHandlerVector.size() = %zu", __FUNCTION__, mCaptureHandlerVector.size());
    return status;
}


status_t AudioALSAStreamManager::setVoiceVolume(float volume) {
    ALOGD("%s(), volume = %f", __FUNCTION__, volume);

    if (volume < 0.0 || volume > 1.0) {
        ALOGE("-%s(), strange volume level %f, something wrong!!", __FUNCTION__, volume);
        return BAD_VALUE;
    }

    AL_AUTOLOCK(mLock);

    if (mAudioALSAVolumeController && isPhoneCallOpen() == true) {
        // match volume to volume index
        int volumeIndex = (int)(volume * 5) + 1;
        // use primary stream out device
        const audio_devices_t current_output_devices = (mStreamOutVector.size() > 0)
                                                       ? mStreamOutVector[0]->getStreamAttribute()->output_devices
                                                       : AUDIO_DEVICE_NONE;
        mAudioALSAVolumeController->setVoiceVolumeIndex(volumeIndex);
        mAudioALSAVolumeController->setVoiceVolume(volume, getModeForGain(), current_output_devices);
        AudioALSASpeechPhoneCallController::getInstance()->updateVolume();
    } else {
        ALOGD("%s() current mode is not in phone call, ignore it", __FUNCTION__);
    }

    return NO_ERROR;
}

#ifdef MTK_AUDIO_GAIN_TABLE
status_t AudioALSAStreamManager::setAnalogVolume(int stream, int device, int index, bool force_incall) {
    ALOGV("%s(),stream=%d, device=%d, index=%d", __FUNCTION__, stream, device, index);

    AL_AUTOLOCK(mLock);

    if (mAudioALSAVolumeController) {
        if (force_incall == 0) {
            mAudioALSAVolumeController->setAnalogVolume(stream, device, index, getModeForGain());
        } else {
            mAudioALSAVolumeController->setAnalogVolume(stream, device, index, AUDIO_MODE_IN_CALL);
        }
    }

    return NO_ERROR;
}

int AudioALSAStreamManager::SetCaptureGain(void) {
    const stream_attribute_t *mStreamAttributeTarget;
    uint32_t i;
    ALOGD("%s(), mStreamInVector.size() = %zu", __FUNCTION__, mStreamInVector.size());

    for (i = 0; i < mStreamInVector.size(); i++) {
        //if(mStreamInVector[i]->getStreamAttribute()->output_devices == output_devices)
        {
            mStreamAttributeTarget = mStreamInVector[i]->getStreamAttribute();
            if (mStreamAttributeTarget != NULL && mAudioALSAVolumeController != NULL) {
                mAudioALSAVolumeController->SetCaptureGain(getModeForGain(), mStreamAttributeTarget->input_source, mStreamAttributeTarget->input_device, mStreamAttributeTarget->output_devices);
            }
        }
    }
    return 0;
}

#endif

#if defined(NON_SCENE_3GVT_VOWIFI_SUPPORT)
void AudioALSAStreamManager::Set3GVTModeOn(bool enable) {
    mIs3GVT = enable;
    // TODO [Michael] porting
    //mAudioALSAVolumeController->Set3GVTModeOn(enable);
    // apply new voice volume setting for extravolume on/off
    audio_devices_t output_devices = AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();
    if (isModeInVoipCall()) {
        mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(), mAudioMode , output_devices);
        SetCaptureGain();
    }
}

bool AudioALSAStreamManager::Get3GVTModeOn() {
    return mIs3GVT;
}
void AudioALSAStreamManager::SetVxWifiOn(bool enable) {
    mIsVoWifi = enable;

    // TODO [Michael] porting
    //mAudioALSAVolumeController->SetVxWifiOn(enable);

    // apply new voice volume setting for extravolume on/off
    audio_devices_t output_devices = AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();
    // for VoWifi(mode 3) gain control
    if (isModeInVoipCall()) {
        mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(), mAudioMode , output_devices);
        SetCaptureGain();
    }
}

bool AudioALSAStreamManager::GetVxWifiOn() {
    return mIsVoWifi;
}

void AudioALSAStreamManager::SetVxWifiWBMode(bool enable) {
    mIsVoWifiWbAmr = enable;

    // TODO [Michael] porting
    //mAudioALSAVolumeController->SetVxWifiWBMode(enable);

    // apply new voice volume setting for extravolume on/off
    audio_devices_t output_devices = AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();
    // for VoWifi(mode 3) NB/WB gain control
    if (isModeInVoipCall()) {
        mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(), mAudioMode , output_devices);
        SetCaptureGain();
    }
}

bool AudioALSAStreamManager::GetVxWifiWBMode() {
    return mIsVoWifiWbAmr;
}

#endif//NON_SCENE_3GVT_VOWIFI_SUPPORT
void AudioALSAStreamManager::setAllInputStreamReopen(bool reopen) {
    for (int i = 0; i < mStreamInVector.size(); i++) {
        mStreamInVector[i]->setReopenState(reopen);
    }
}

void AudioALSAStreamManager::forceTelephonyTX(bool enable) {
    ALOGD("%s(), mIsCallFwdEnabled(%d->%d), mIsMicMuteBeforeCallFwd(%d), mIsDlMuteBeforeCallFwd(%d)",
          __FUNCTION__, mIsCallFwdEnabled, enable, mIsMicMuteBeforeCallFwd, mIsDlMuteBeforeCallFwd);

    if (enable != mIsCallFwdEnabled) {
        setAllOutputStreamsSuspend(true, true);
        standbyAllOutputStreams(true);
        if (enable) {
            mIsDlMuteBeforeCallFwd = AudioALSASpeechPhoneCallController::getInstance()->getDlMute();
            mIsMicMuteBeforeCallFwd = getMicMute();
            mIsCallFwdEnabled = true;
            if (!mIsDlMuteBeforeCallFwd) {
                AudioALSASpeechPhoneCallController::getInstance()->setDlMute(true);
            }
            if (!mIsMicMuteBeforeCallFwd) {
                setMicMute(true);
            }
            AL_AUTOLOCK(mLock);
            for (size_t i = 0; i < mStreamOutVector.size(); i++) {
                mStreamOutVector[i]->setStreamOutOutputFlags(AUDIO_OUTPUT_FLAG_INCALL_MUSIC, true);
            }
        } else {
            mIsCallFwdEnabled = false;
            if (!mIsDlMuteBeforeCallFwd) {
                AudioALSASpeechPhoneCallController::getInstance()->setDlMute(mIsDlMuteBeforeCallFwd);
            }
            if (!mIsMicMuteBeforeCallFwd) {
                setMicMute(mIsMicMuteBeforeCallFwd);
            }
            AL_AUTOLOCK(mLock);
            for (size_t i = 0; i < mStreamOutVector.size(); i++) {
                mStreamOutVector[i]->setStreamOutOutputFlags(AUDIO_OUTPUT_FLAG_INCALL_MUSIC, false);
            }
        }
        setAllOutputStreamsSuspend(false, true);
    }
}

void AudioALSAStreamManager::callMemo(int state) {
    ALOGD("%s(), mCallMemoState(%d ->%d), mIsMicMuteBeforeCallMemo(%d), mIsDlMuteBeforeCallMemo(%d)",
          __FUNCTION__, mCallMemoState, state, mIsMicMuteBeforeCallMemo, mIsDlMuteBeforeCallMemo);

    switch (state) {
    case CALL_MEMO_STATE_ON:
        if (mCallMemoState == CALL_MEMO_STATE_INIT) {
            forceTelephonyTX(true);
            mCallMemoState = CALL_MEMO_STATE_ON;
        } else {
            ALOGW("%s(), invalid state change(%d -> %d), return.", __FUNCTION__, mCallMemoState, state);
        }
        break;
    case CALL_MEMO_STATE_REC:
        if (mCallMemoState == CALL_MEMO_STATE_ON) {
            forceTelephonyTX(false);
            //keep mute status and force mute
            mIsDlMuteBeforeCallMemo = AudioALSASpeechPhoneCallController::getInstance()->getDynamicDlMute();
            mIsMicMuteBeforeCallMemo = getMicMute();
            AudioALSASpeechPhoneCallController::getInstance()->setDynamicDlMute(true);
            setMicMute(true);
            mCallMemoState = CALL_MEMO_STATE_REC;
        } else {
            ALOGW("%s(), invalid state change(%d -> %d), return.", __FUNCTION__, mCallMemoState, state);
        }
        break;
    case CALL_MEMO_STATE_INIT:
        if (mCallMemoState == CALL_MEMO_STATE_ON) {
            forceTelephonyTX(false);
            mCallMemoState = CALL_MEMO_STATE_INIT;
        } else if (mCallMemoState == CALL_MEMO_STATE_REC) {
            forceTelephonyTX(false);//comment: it can be ignored

            //recover mute status
            AudioALSASpeechPhoneCallController::getInstance()->setDynamicDlMute(mIsDlMuteBeforeCallMemo);
            setMicMute(mIsMicMuteBeforeCallMemo);
            mCallMemoState = CALL_MEMO_STATE_INIT;
        } else {
            ALOGW("%s(), invalid state change(%d -> %d), return.", __FUNCTION__, mCallMemoState, state);
        }
        break;
    default:
        ALOGW("%s(), wrong state(%d), return.", __FUNCTION__, state);
        break;
    }
}

float AudioALSAStreamManager::getMasterVolume(void) {
    return mAudioALSAVolumeController->getMasterVolume();
}

uint32_t AudioALSAStreamManager::GetOffloadGain(float vol_f) {
    if (mAudioALSAVolumeController != NULL) {
        return mAudioALSAVolumeController->GetOffloadGain(vol_f);
    }
    ALOGE("%s(), VolumeController Null", __FUNCTION__);
    return -1;
}

status_t AudioALSAStreamManager::setMasterVolume(float volume, uint32_t iohandle) {
    ALOGD_IF(mLogEnable, "%s(), volume = %f", __FUNCTION__, volume);

    if (volume < 0.0 || volume > 1.0) {
        ALOGE("-%s(), strange volume level %f, something wrong!!", __FUNCTION__, volume);
        return BAD_VALUE;
    }

    AL_AUTOLOCK(mLock);
    if (mAudioALSAVolumeController) {
        audio_devices_t current_output_devices;
        uint32_t index = mStreamOutVector.indexOfKey(iohandle);
        if (index < mStreamOutVector.size()) {
            current_output_devices = mStreamOutVector[index]->getStreamAttribute()->output_devices;
        } else {
            current_output_devices  = (mStreamOutVector.size() > 0)
                                      ? mStreamOutVector[0]->getStreamAttribute()->output_devices
                                      : AUDIO_DEVICE_NONE;
        }
        mAudioALSAVolumeController->setMasterVolume(volume, getModeForGain(), current_output_devices);
    }

    return NO_ERROR;
}

status_t AudioALSAStreamManager::setHeadsetVolumeMax() {
    ALOGD("%s()", __FUNCTION__);
    mAudioALSAVolumeController->setAudioBufferGain(0);
    return NO_ERROR;
}

status_t AudioALSAStreamManager::setFmVolume(float volume) {
    ALOGV("+%s(), volume = %f", __FUNCTION__, volume);

    if (volume < 0.0 || volume > 1.0) {
        ALOGE("-%s(), strange volume level %f, something wrong!!", __FUNCTION__, volume);
        return BAD_VALUE;
    }

    AL_AUTOLOCK(mLock);
    mFMController->setFmVolume(volume);

    return NO_ERROR;
}

void AudioALSAStreamManager::setStreamType(int stream) {
#if defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    char strbuffer [100];
    int ret = 0;
    if ((appIsFeatureOptionEnabled("MTK_BESLOUDNESS_RUN_WITH_HAL") == true) &&
        ((stream != AUDIO_STREAM_RING && mStreamType == AUDIO_STREAM_RING) ||
         (stream == AUDIO_STREAM_RING && mStreamType != AUDIO_STREAM_RING))) {
        /* need update stream type to BesLoudness */
        if (isAdspOptionEnable()) {
            ret = snprintf(strbuffer, sizeof(strbuffer),
                           "DSP,ALL,MTKBESSOUND,KEY_VALUE,SetStreamType,%d=SET", stream);
            if (ret < 0) {
                ALOGE("%s(), set DSP param string fail!", __FUNCTION__);
            }
            aurisys_set_parameter(strbuffer);
        }

        uint32_t aurisysScenario;

        ret = snprintf(strbuffer, sizeof(strbuffer),
                       "HAL,ALL,MTKBESSOUND,KEY_VALUE,SetStreamType,%d=SET", stream);
        if (ret < 0) {
            ALOGE("%s(), set HAL param string fail!", __FUNCTION__);
        }
        aurisys_set_parameter(strbuffer);
    }
#endif
    mStreamType = (audio_stream_type_t)stream;
}

status_t AudioALSAStreamManager::setMicMute(bool state) {
    ALOGD("%s(), mMicMute: %d => %d", __FUNCTION__, mMicMute, state);
    AL_AUTOLOCK(mLock);
    AudioALSASpeechPhoneCallController::getInstance()->setMicMute(state);
    if (isPhoneCallOpen() == false) {
        SetInputMute(state);
    }
    mMicMute = state;
    return NO_ERROR;
}


bool AudioALSAStreamManager::getMicMute() {
    ALOGD_IF(mLogEnable, "%s(), mMicMute = %d", __FUNCTION__, mMicMute);
    AL_AUTOLOCK(mLock);

    return mMicMute;
}

void AudioALSAStreamManager::SetInputMute(bool bEnable) {
    ALOGD("+%s(), %d", __FUNCTION__, bEnable);
    if (mStreamInVector.size() > 0) {
        for (size_t i = 0; i < mStreamInVector.size(); i++) { // TODO(Harvey): Mic+FM !?
            mStreamInVector[i]->SetInputMute(bEnable);
        }
    }
    ALOGD_IF(mLogEnable, "-%s(), %d", __FUNCTION__, bEnable);
}

status_t AudioALSAStreamManager::setVtNeedOn(const bool vt_on) {
    ALOGD("%s(), setVtNeedOn: %d", __FUNCTION__, vt_on);
    AudioALSASpeechPhoneCallController::getInstance()->setVtNeedOn(vt_on);

    return NO_ERROR;
}

status_t AudioALSAStreamManager::setMode(audio_mode_t new_mode) {
    bool resumeAllStreamsAtSetMode = false;
    bool updateModeToStreamOut = false;
    bool resumeInputStreamsAtSetMode = false;
    int ret = 0;
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    bool hasDonePhoneClose = false;
#endif


    // check value
    if ((new_mode < AUDIO_MODE_NORMAL) || (new_mode > AUDIO_MODE_MAX)) {
        ALOGW("%s(), new_mode: %d is BAD_VALUE, return", __FUNCTION__, new_mode);
        return BAD_VALUE;
    }

    // TODO(Harvey): modem 1 / modem 2 check

    if (new_mode == mAudioMode) {
        ALOGW("%s(), mAudioMode: %d == %d, return", __FUNCTION__, mAudioMode, new_mode);
        return NO_ERROR;
    }

    if (mStreamManagerDestructing == true) {
        ALOGW("%s(), setMode %d => %d during StreamManager destructing, return", __FUNCTION__, mAudioMode, new_mode);
        return NO_ERROR;
    }
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    /* suspend IEMs when non-normal mode */
    if (!isModeInNormal(new_mode)) { // leave normal mode
        AudioIEMsController::getInstance()->suspend(IEMS_SUSPEND_USER_SET_MODE);
        AudioIEMsController::getInstance()->suspend(IEMS_SUSPEND_USER_SET_MODE_ROUTING);
    }
#endif
#ifndef MTK_COMBO_MODEM_SUPPORT
    else if (isModeInPhoneCall(mAudioMode) == true) {
        //check if any stream out is playing during leaving IN_CALL mode
        int DelayCount = 0;
        bool IsStreamActive;
        do {
            IsStreamActive = false;
            for (size_t i = 0; i < mStreamOutVector.size(); i++) {
                if (mStreamOutVector[i]->isOutPutStreamActive() == true) {
                    IsStreamActive = true;
                }
            }
            if (IsStreamActive) {
                usleep(20 * 1000);
                ALOGD_IF(mLogEnable, "%s(), delay 20ms x(%d) for active stream out playback", __FUNCTION__, DelayCount);
            }
            DelayCount++;
        } while ((DelayCount <= 10) && (IsStreamActive));
    }
#endif
    AL_AUTOLOCK(mStreamVectorLock);

    mEnterPhoneCallMode = isModeInPhoneCall(new_mode);

    if (isModeInPhoneCall(new_mode) == true || isPhoneCallOpen() == true ||
        isModeInVoipCall(new_mode)  == true || isModeInVoipCall(mAudioMode) == true) {

        if (isModeInPhoneCall(new_mode) == true || isPhoneCallOpen() == true) {
            setAllInputStreamsSuspend(true, true);
            standbyAllInputStreams(true);
            resumeInputStreamsAtSetMode = true;
        }
        //Need to reset MicInverse when phone/VOIP call
        AudioALSAHardwareResourceManager::getInstance()->setMicInverse(0);

        if ((isModeInPhoneCall(mAudioMode) == false && // non-phone call --> voip mode
             isModeInVoipCall(new_mode) == true) ||
            (isModeInVoipCall(mAudioMode) == true && // leave voip, not enter phone call, and not 2->3->0
             isModeInPhoneCall(new_mode) == false &&
             isPhoneCallOpen() == false)) {
            mIsNeedResumeStreamOut = false;
            updateModeToStreamOut = true;
        } else {
            setAllOutputStreamsSuspend(true, true);
            standbyAllOutputStreams(true);
            mIsNeedResumeStreamOut = true;
        }

        /* Only change mode to non-call need resume streams at the end of setMode().
           Otherwise, resume streams when get the routing command. */
        /* Not use isModeInPhoneCall() because 2->3 case need to resume in routing command.*/
        if (isModeInPhoneCall(new_mode) == true) {
            mResumeAllStreamsAtRouting = true;
        } else {
            resumeAllStreamsAtSetMode = true;
        }
    }

    ALOGD("+%s(), mAudioMode: %d => %d, mEnterPhoneCallMode = %d, mResumeAllStreamsAtRouting = %d, resumeAllStreamsAtSetMode = %d",
          __FUNCTION__, mAudioMode, new_mode, mEnterPhoneCallMode, mResumeAllStreamsAtRouting, resumeAllStreamsAtSetMode);

    // TODO(Harvey): // close mATV when mode swiching

    {
        AL_AUTOLOCK(mLock);
        AL_AUTOLOCK(mAudioModeLock);

        // use primary stream out device // TODO(Harvey): add a function? get from hardware?
#ifdef FORCE_ROUTING_RECEIVER
        const audio_devices_t current_output_devices = AUDIO_DEVICE_OUT_EARPIECE;
#else
        const audio_devices_t current_output_devices = (mStreamOutVector.size() > 0)
                                                       ? mStreamOutVector[0]->getStreamAttribute()->output_devices
                                                       : AUDIO_DEVICE_NONE;
#endif

        // close previous call if needed
        /*if ((isModeInPhoneCall(mAudioMode) == true)
            && (isModeInPhoneCall(new_mode) == false))*/
        if ((isModeInNormal(new_mode) == true) && (isPhoneCallOpen() == true)) {
            mSpeechPhoneCallController->close();
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
            hasDonePhoneClose = true;
#endif
#if defined(MTK_VOW_SUPPORT)
            // make sure voice wake up is resume after phone call closed (not in IN CALL or CALL SCREEN)
            mForceDisableVoiceWakeUpForPhoneCall = false;
            if (mVoiceWakeUpNeedOn == true) {
                ALOGD("%s(), phone call controller closed (MD call closed), resume voice wake up", __FUNCTION__);
                setVoiceWakeUpEnable_l(true);
            }
#endif
            ALOGD("%s(), force unmute mic after phone call closed", __FUNCTION__);
            if (mSmartPaController->isSmartPAUsed()) {
                mSmartPaController->setPhoneCallEnable(false);
            }
#if !(defined(MTK_TC10_FEATURE))
            // tc10 project cannot reset mute status due to setMode 0 during call switch
            mSpeechPhoneCallController->setMicMute(false);
#endif
            mSpeechPhoneCallController->setDlMute(false);
            mSpeechPhoneCallController->setDynamicDlMute(false);
            mSpeechPhoneCallController->setUlMute(false);
        }
        // open next call if needed
        if ((isModeInPhoneCall(new_mode) == true) && (isPhoneCallOpen() == false)) {
            ALOGD("%s(), open next call", __FUNCTION__);
        }

        mAudioMode = new_mode;

        if (isModeInPhoneCall() == false && isPhoneCallOpen() == false) {
            mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(),
                                                        getModeForGain(), current_output_devices);
        }

#ifdef MTK_POWERHAL_WIFI_POWRER_SAVE
        if (isModeInVoipCall()) {
            power_hal_hint(POWERHAL_DISABLE_WIFI_POWER_SAVE, true);
        } else {
            power_hal_hint(POWERHAL_DISABLE_WIFI_POWER_SAVE, false);
        }
#endif
    }

    // update audio mode to stream out if not suspend/standby streamout
    if (updateModeToStreamOut) {
        audio_devices_t current_output_devices = AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();
        if (mSmartPaController->isHwDspSpkProtect(current_output_devices)) {
            mSmartPaController->updateSmartPaMode(mAudioMode);
        }
        for (size_t i = 0; i < mStreamOutVector.size(); i++) {
            ret = mStreamOutVector[i]->updateAudioMode(mAudioMode);
            ASSERT(ret == 0);
        }
#ifdef MTK_AUDIODSP_SUPPORT
        if (isAdspOptionEnable()) {
            AudioDspStreamManager::getInstance()->updateMode(mAudioMode);
        }
#endif
    }

    if (resumeAllStreamsAtSetMode == true) {
        if (mIsNeedResumeStreamOut) {
            mIsNeedResumeStreamOut = false;
            setAllOutputStreamsSuspend(false, true);
        }
        if (resumeInputStreamsAtSetMode) {
            setAllInputStreamsSuspend(false, true);
        }
    }

#if defined(MTK_HYBRID_NLE_SUPPORT)
    AudioALSAHyBridNLEManager::getInstance()->setAudioMode(mAudioMode);
#endif

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    /* resume IEMs when normal mode */
    if (isModeInNormal(new_mode)) { // back to normal mode (z->0)
        AudioIEMsController::getInstance()->resume(IEMS_SUSPEND_USER_SET_MODE);
    }

    /* resume IEMs when phone call controller close */
    if (hasDonePhoneClose) {
        AudioIEMsController::getInstance()->resume(IEMS_SUSPEND_USER_CALL_OPEN);
    }
#endif

    ALOGD("-%s(), mAudioMode = %d, mResumeAllStreamsAtRouting = %d, resumeAllStreamsAtSetMode = %d",
          __FUNCTION__, mAudioMode, mResumeAllStreamsAtRouting, resumeAllStreamsAtSetMode);

    return NO_ERROR;
}

audio_mode_t AudioALSAStreamManager::getMode() {
    AL_AUTOLOCK(mAudioModeLock);
    ALOGD("%s(), mAudioMode = %d", __FUNCTION__, mAudioMode);

    return mAudioMode;
}

bool AudioALSAStreamManager::isPhoneCallOpen() {
    return mSpeechPhoneCallController->getCallStatus();
}

status_t AudioALSAStreamManager::syncSharedOutDevice(audio_devices_t routingSharedOutDevice,
                                                     AudioALSAStreamOut *currentStreamOut) {
    AL_AUTOLOCK(mLock);

    status_t status = NO_ERROR;
    Vector<AudioALSAStreamOut *> streamOutToRoute;
    int originDevice = routingSharedOutDevice;

    if (isModeInPhoneCall()) {
        routingSharedOutDevice = mCurrentOutputDevicesForCall;
    }

    ALOGD("+%s(), originDevice/routingSharedOutDevice: 0x%x/0x%x, mAudioMode: %d",
          __FUNCTION__, originDevice, routingSharedOutDevice, mAudioMode);

    // Check if shared device
    AudioALSAHardwareResourceManager *hwResMng = AudioALSAHardwareResourceManager::getInstance();
    if (!hwResMng->isSharedOutDevice(routingSharedOutDevice)) {
        ALOGD("-%s(), this stream out is not shared out device, return.", __FUNCTION__);
        return NO_ERROR;
    }

    // suspend before routing & check which streamout need routing
    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        if (isOutputNeedRouting(mStreamOutVector[i], currentStreamOut, routingSharedOutDevice)) {
            mStreamOutVector[i]->setSuspend(true);
            streamOutToRoute.add(mStreamOutVector[i]);
        }
    }

    // routing
    for (size_t i = 0; i < streamOutToRoute.size(); i++) {
        status = streamOutToRoute[i]->routing(routingSharedOutDevice);
        ASSERT(status == NO_ERROR);
        if (streamOutToRoute[i] != currentStreamOut) {
            streamOutToRoute[i]->setMuteForRouting(true);
        }
    }

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    if (mAudioMode == AUDIO_MODE_NORMAL &&
        (currentStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY ||
         currentStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST ||
         currentStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) {
        AudioIEMsController::getInstance()->routing(routingSharedOutDevice);
    }
#endif

    // resume suspend
    for (size_t i = 0; i < streamOutToRoute.size(); i++) {
        streamOutToRoute[i]->setSuspend(false);
    }

    if (streamOutToRoute.size() > 0) {
        updateOutputDeviceForAllStreamIn_l(routingSharedOutDevice);

        // volume control
        if (!isPhoneCallOpen()) {
            mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(),
                                                        getModeForGain(), routingSharedOutDevice);
        }
    }

    ALOGV("-%s()", __FUNCTION__);
    return status;
}

bool AudioALSAStreamManager::isOutputNeedRouting(AudioALSAStreamOut *eachStreamOut,
                                                 AudioALSAStreamOut *currentStreamOut,
                                                 audio_devices_t routingSharedOutDevice) {
    audio_devices_t streamOutDevice = eachStreamOut->getStreamAttribute()->output_devices;
    bool isSharedStreamOutDevice = AudioALSAHardwareResourceManager::getInstance()->isSharedOutDevice(streamOutDevice);
    bool isSharedRoutingDevice = AudioALSAHardwareResourceManager::getInstance()->isSharedOutDevice(routingSharedOutDevice);

    if (streamOutDevice == routingSharedOutDevice) {
        return false;
    }

    if (eachStreamOut->isOutPutStreamActive()) {

        // active currentStreamOut always need routing
        if (currentStreamOut == eachStreamOut) {
            return true;
        }

        if (isSharedStreamOutDevice && isSharedRoutingDevice) {
            return true;
        }
    }

    return false;
}

status_t AudioALSAStreamManager::DeviceNoneUpdate() {
    ALOGD("+%s()", __FUNCTION__);
    AL_AUTOLOCK(mLock);
    status_t status = NO_ERROR;
#ifdef MTK_VOW_SUPPORT
    // update the output device info for voice wakeup (even when "routing=0")
    mAudioALSAVoiceWakeUpController->updateDeviceInfoForVoiceWakeUp();
#endif
    ALOGD("-%s()", __FUNCTION__);

    return status;
}

status_t AudioALSAStreamManager::routingOutputDevicePhoneCall(const audio_devices_t output_devices) {
    AL_AUTOLOCK(mLock);
    status_t status = NO_ERROR;

    if (output_devices == AUDIO_DEVICE_NONE || mPhoneCallControllerStatusPolicy == false) {
        ALOGW("%s(), routing output_devices: 0x%08x => 0x%08x, mPhoneCallControllerStatusPolicy = %d, directly return",
              __FUNCTION__, mCurrentOutputDevicesForCall, output_devices, mPhoneCallControllerStatusPolicy);
        // ASSERT ??
        return NO_ERROR;
    }

    mOutputDevicesForCall = output_devices;

    bool checkrouting = CheckStreaminPhonecallRouting(mSpeechPhoneCallController->getInputDeviceForPhoneCall(output_devices), false);
    bool isFirstRoutingInCall = false;
    audio_devices_t input_devices = mSpeechPhoneCallController->getInputDeviceForPhoneCall(output_devices);
    Vector<AudioALSAStreamOut *> streamOutSuspendInCall;

    if (isPhoneCallOpen() == false) {
#ifdef FORCE_ROUTING_RECEIVER
        output_devices = AUDIO_DEVICE_OUT_EARPIECE;
#endif
        ALOGD("+%s(), phonecall open, mAudioMode = %d, input_devices = 0x%08x, output_devices: 0x%08x => 0x%08x, mResumeAllStreamsAtRouting = %d",
              __FUNCTION__, mAudioMode, input_devices, mCurrentOutputDevicesForCall, output_devices, mResumeAllStreamsAtRouting);

        if (mSmartPaController->isSmartPAUsed()) {
            mSmartPaController->setPhoneCallEnable(true);
        }
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
        /* suspend IEMs when phone call controller open */
        AudioIEMsController::getInstance()->suspend(IEMS_SUSPEND_USER_CALL_OPEN);
#endif
#if defined(MTK_VOW_SUPPORT)
        // make sure voice wake up is closed before entering phone call (IN CALL and CALL SCREEN)
        mForceDisableVoiceWakeUpForPhoneCall = true;
        setVoiceWakeUpEnable(false);
#endif
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
        AudioALSAUltrasoundOutOfCallController::getInstance()->beforePhoneCallRouting(output_devices, input_devices);
#endif
        mSpeechPhoneCallController->open(mAudioMode, output_devices, input_devices);
        isFirstRoutingInCall = true;
    }

    if (!isFirstRoutingInCall && (mCurrentOutputDevicesForCall != output_devices)) {
        ALOGD("+%s(), phonecall routing, mAudioMode = %d, input_devices = 0x%08x, output_devices: 0x%08x => 0x%08x, mResumeAllStreamsAtRouting = %d",
              __FUNCTION__, mAudioMode, input_devices, mCurrentOutputDevicesForCall, output_devices, mResumeAllStreamsAtRouting);

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
        AudioALSAUltrasoundOutOfCallController::getInstance()->beforePhoneCallRouting(output_devices, input_devices);
#endif

        for (size_t i = 0; i < mStreamOutVector.size(); i++) {
            mStreamOutVector[i]->setSuspend(true);
            if (mStreamOutVector[i]->isOutPutStreamActive()) {
                mStreamOutVector[i]->standbyStreamOut();
            }
            mStreamOutVector[i]->setMuteForRouting(true);
            streamOutSuspendInCall.add(mStreamOutVector[i]);
        }

        mSpeechPhoneCallController->routing(output_devices, input_devices);
#ifdef MTK_BT_HEARING_AID_SUPPORT
        if (appIsFeatureOptionEnabled("MTK_BT_HEARING_AID_SUPPORT") &&
            (mCurrentOutputDevicesForCall == AUDIO_DEVICE_OUT_HEARING_AID) && (mSwBridgeEnable == true)) {
            setSoftwareBridgeEnable(false);
        }
#endif
    }

    // Need to resume the streamin
    if (checkrouting == true) {
        CheckStreaminPhonecallRouting(mSpeechPhoneCallController->getInputDeviceForPhoneCall(output_devices), true);
    }

    // volume control
    mAudioALSAVolumeController->setVoiceVolume(mAudioALSAVolumeController->getVoiceVolume(), getModeForGain(), output_devices);

    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        mStreamOutVector[i]->syncPolicyDevice();
    }

    if (mResumeAllStreamsAtRouting == true) {
        setAllStreamsSuspend(false, true);
        mResumeAllStreamsAtRouting = false;
    }

    for (size_t i = 0; i < streamOutSuspendInCall.size(); i++) {
        streamOutSuspendInCall[i]->setSuspend(false);
    }

    mCurrentOutputDevicesForCall = mOutputDevicesForCall;

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->afterPhoneCallRouting(output_devices, input_devices);
#endif

    ALOGD("-%s(), routing done, current output_devices = 0x%08x, mResumeAllStreamsAtRouting = %d",
          __FUNCTION__, mCurrentOutputDevicesForCall, mResumeAllStreamsAtRouting);

    return status;
}

status_t AudioALSAStreamManager::routingOutputDevice(AudioALSAStreamOut *pAudioALSAStreamOut, const audio_devices_t current_output_devices, audio_devices_t output_devices) {
    AL_AUTOLOCK(mLock);

    status_t status = NO_ERROR;
    audio_devices_t streamOutDevice = pAudioALSAStreamOut->getStreamAttribute()->output_devices;

    Vector<AudioALSAStreamOut *> streamOutSuspendInCall;

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->beforeOutputDeviceRouting(
            current_output_devices, output_devices);
#endif

#if defined(MTK_BT_SPK_DUAL_PATH_SUPPORT)
    // for BTSCO + SPK dual path device
    mSpeechPhoneCallController->setBtSpkDevice(false);
    if (isBtSpkDevice(output_devices) && isPhoneCallOpen()) {
        audio_devices_t source_output_devices = output_devices;
        // use SPK setting for BTSCO + SPK
        output_devices = (audio_devices_t)(output_devices & (~AUDIO_DEVICE_OUT_ALL_SCO));
        mSpeechPhoneCallController->setBtSpkDevice(true);
        ALOGD("%s(), Use SPK setting for BTSCO + SPK, output_devices: 0x%08x => 0x%08x", __FUNCTION__, source_output_devices, output_devices);
    }
#endif

#ifdef MTK_VOW_SUPPORT
    // update the output device info for voice wakeup (even when "routing=0")
    mAudioALSAVoiceWakeUpController->updateDeviceInfoForVoiceWakeUp();
#endif
    if (output_devices == AUDIO_DEVICE_NONE) {
        ALOGW("%s(), flag: 0x%x, output_devices == AUDIO_DEVICE_NONE(0x%x), return",
              __FUNCTION__, pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags, AUDIO_DEVICE_NONE);
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
        AudioALSAUltrasoundOutOfCallController::getInstance()->afterOutputDeviceRouting(
                current_output_devices, output_devices);
#endif
        return NO_ERROR;
    } else if (output_devices == streamOutDevice) {
        if ((mPhoneCallControllerStatusPolicy == true) || (mResumeAllStreamsAtRouting == true)) {
            ALOGD("+%s(), flag: 0x%x, output_devices = current_devices(0x%08x), mResumeAllStreamsAtRouting = %d",
                  __FUNCTION__, pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags,
                  current_output_devices, mResumeAllStreamsAtRouting);
        }
#ifdef MTK_AUDIO_TTY_SPH_ENH_SUPPORT
        else if ((isPhoneCallOpen() == true) && (mSpeechPhoneCallController->checkTtyNeedOn() == true)) {
            ALOGW("+%s(), flag: 0x%x, output_devices == current_output_devices(0x%08x), but TTY call is enabled",
                  __FUNCTION__, pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags, streamOutDevice);
        }
#endif
        else {
            ALOGW("%s(), flag: 0x%x, output_devices == current_output_devices(0x%08x), return",
                  __FUNCTION__, pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags, streamOutDevice);
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
            if (mAudioMode == AUDIO_MODE_NORMAL) {
                if (pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY ||
                    pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST ||
                    pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
                    AudioIEMsController::getInstance()->routing(output_devices);
                }
                AudioIEMsController::getInstance()->resume(IEMS_SUSPEND_USER_SET_MODE_ROUTING);
            }
#endif
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
        AudioALSAUltrasoundOutOfCallController::getInstance()->afterOutputDeviceRouting(
                current_output_devices, output_devices);
#endif
            return NO_ERROR;
        }
    } else {
        ALOGD("+%s(), flag: 0x%x, normal routing, output_devices: 0x%08x => 0x%08x",
              __FUNCTION__, pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags,
              streamOutDevice, output_devices);
    }

    // close FM when mode swiching
    if (mFMController->getFmEnable() &&
        ((mPhoneCallControllerStatusPolicy == true) || mFMController->isFmAdspActive())) {
        //setFmEnable(false);
        mFMController->setFmEnable(false, current_output_devices, false, false, true);
    }

    // do routing for routing to BTSCO/USB/HearingAID phone call
    if ((mPhoneCallControllerStatusPolicy == true) &&
        (pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY)) {

        bool checkrouting = CheckStreaminPhonecallRouting(mSpeechPhoneCallController->getInputDeviceForPhoneCall(output_devices), false);
        bool isFirstRoutingInCall = false;
        audio_devices_t input_devices = mSpeechPhoneCallController->getInputDeviceForPhoneCall(output_devices);

        mOutputStreamForCall = pAudioALSAStreamOut;
        mOutputDevicesForCall = output_devices;

        if (isPhoneCallOpen() == false) {
#ifdef FORCE_ROUTING_RECEIVER
            output_devices = AUDIO_DEVICE_OUT_EARPIECE;
#endif
            ALOGD("%s(), phonecall open, mAudioMode = %d, input_devices = 0x%08x, output_devices: 0x%08x => 0x%08x, mResumeAllStreamsAtRouting = %d",
                  __FUNCTION__, mAudioMode, input_devices, mCurrentOutputDevicesForCall, output_devices, mResumeAllStreamsAtRouting);

            if (mSmartPaController->isSmartPAUsed()) {
                mSmartPaController->setPhoneCallEnable(true);
            }
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
            /* suspend IEMs when phone call controller open */
            AudioIEMsController::getInstance()->suspend(IEMS_SUSPEND_USER_CALL_OPEN);
#endif
#if defined(MTK_VOW_SUPPORT)
            // make sure voice wake up is closed before entering phone call (IN CALL and CALL SCREEN)
            mForceDisableVoiceWakeUpForPhoneCall = true;
            setVoiceWakeUpEnable(false);
#endif
            mSpeechPhoneCallController->open(mAudioMode, output_devices, input_devices);
            isFirstRoutingInCall = true;
        }


        if (!isFirstRoutingInCall && (mCurrentOutputDevicesForCall != output_devices)) {
            ALOGD("%s(), phonecall routing, mAudioMode = %d, input_devices = 0x%08x, output_devices: 0x%08x => 0x%08x, mResumeAllStreamsAtRouting = %d",
                  __FUNCTION__, mAudioMode, input_devices, mCurrentOutputDevicesForCall, output_devices, mResumeAllStreamsAtRouting);

            for (size_t i = 0; i < mStreamOutVector.size(); i++) {
                mStreamOutVector[i]->setSuspend(true);
                if (mStreamOutVector[i]->isOutPutStreamActive()) {
                    mStreamOutVector[i]->standbyStreamOut();
                }
                streamOutSuspendInCall.add(mStreamOutVector[i]);
            }

            mSpeechPhoneCallController->routing(output_devices, input_devices);
#ifdef MTK_BT_HEARING_AID_SUPPORT
            if (appIsFeatureOptionEnabled("MTK_BT_HEARING_AID_SUPPORT") &&
                (mCurrentOutputDevicesForCall == AUDIO_DEVICE_OUT_HEARING_AID) && (mSwBridgeEnable == true)) {
                setSoftwareBridgeEnable(false);
            }
#endif
        }

        // Need to resume the streamin
        if (checkrouting == true) {
            CheckStreaminPhonecallRouting(mSpeechPhoneCallController->getInputDeviceForPhoneCall(output_devices), true);
        }

        // volume control
        mAudioALSAVolumeController->setVoiceVolume(mAudioALSAVolumeController->getVoiceVolume(),
                                                   getModeForGain(), output_devices);

        for (size_t i = 0; i < mStreamOutVector.size(); i++) {
            mStreamOutVector[i]->syncPolicyDevice();
        }

        mCurrentOutputDevicesForCall = mOutputDevicesForCall;
    }

    if (mResumeAllStreamsAtRouting == true) {
        setAllStreamsSuspend(false, true);
        mResumeAllStreamsAtRouting = false;
    }

    Vector<AudioALSAStreamOut *> streamOutToRoute;

    // Check if non active streamout device
    if (!pAudioALSAStreamOut->isOutPutStreamActive()) {
        ALOGD("-%s(), flag: 0x%x, stream out not active, route itself and return",
              __FUNCTION__, pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags);
        pAudioALSAStreamOut->routing(output_devices);
        status = NO_ERROR;
#if defined(SUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL)
        // FM follow Primary routing
        if (pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY
            && mFMController->getFmEnable()) {
            mFMController->routing(current_output_devices, output_devices);
        }
#endif
        goto ROUTE_OUTPUT_DONE;
    }

    // suspend before routing & check if other streamouts need routing
    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        if (isOutputNeedRouting(mStreamOutVector[i], pAudioALSAStreamOut, output_devices)) {
            mStreamOutVector[i]->setSuspend(true);
            streamOutToRoute.add(mStreamOutVector[i]);
        }
    }

    // routing
    for (size_t i = 0; i < streamOutToRoute.size(); i++) {
        status = streamOutToRoute[i]->routing(output_devices);
        ASSERT(status == NO_ERROR);
        if (streamOutToRoute[i] != pAudioALSAStreamOut) {
            streamOutToRoute[i]->setMuteForRouting(true);
        }
    }

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    if (mAudioMode == AUDIO_MODE_NORMAL &&
        (pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY ||
         pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST ||
         pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) {
        AudioIEMsController::getInstance()->routing(output_devices);
    }
#endif

#if defined(SUPPORT_FM_AUDIO_BY_PROPRIETARY_PARAMETER_CONTROL)
    if (pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY
        && mFMController->getFmEnable()) {
        mFMController->routing(current_output_devices, output_devices);
    }
#endif

    // resume suspend
    for (size_t i = 0; i < streamOutToRoute.size(); i++) {
        streamOutToRoute[i]->setSuspend(false);
    }

    if (streamOutToRoute.size() > 0) {
        updateOutputDeviceForAllStreamIn_l(output_devices);

        // volume control
        if (!isPhoneCallOpen()) {
            mAudioALSAVolumeController->setMasterVolume(mAudioALSAVolumeController->getMasterVolume(),
                                                        getModeForGain(), output_devices);
        }
    }

    ALOGD("-%s(), flag: 0x%x, output_devices = 0x%08x", __FUNCTION__,
          pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags,
          output_devices);

ROUTE_OUTPUT_DONE:
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    if (mAudioMode == AUDIO_MODE_NORMAL) {
        AudioIEMsController::getInstance()->resume(IEMS_SUSPEND_USER_SET_MODE_ROUTING);
    }
#endif
    for (size_t i = 0; i < streamOutSuspendInCall.size(); i++) {
        streamOutSuspendInCall[i]->setSuspend(false);
    }
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->afterOutputDeviceRouting(
            current_output_devices, output_devices);
#endif
    return status;
}


status_t AudioALSAStreamManager::routingInputDevice(AudioALSAStreamIn *pAudioALSAStreamIn,
                                                    const audio_devices_t current_input_device,
                                                    audio_devices_t input_device) {
    ALOGD("+%s(), input_device: 0x%x => 0x%x", __FUNCTION__, current_input_device, input_device);
    AL_AUTOLOCK(mLock);

    status_t status = NO_ERROR;

    int mNumPhoneMicSupport = AudioCustParamClient::GetInstance()->getNumMicSupport();
    if ((input_device == AUDIO_DEVICE_IN_BACK_MIC) && (mNumPhoneMicSupport < 2)) {
        input_device = AUDIO_DEVICE_IN_BUILTIN_MIC;
        ALOGW("%s(), not support back_mic if mic < 2, force to set input_device = 0x%x", __FUNCTION__, input_device);
    }

    bool sharedDevice = (input_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
    //In PhonecallMode and the new input_device / phonecall_device are both sharedDevice,we may change the input_device = phonecall_device
    if ((isPhoneCallOpen() == true) && (sharedDevice == true)) {
        audio_devices_t phonecall_device = mSpeechPhoneCallController->getAdjustedInputDevice();
        sharedDevice = (phonecall_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
        if (sharedDevice == true) {
            input_device = phonecall_device;
        }
        ALOGD("+%s(), isPhoneCallOpen, input_device = 0x%x", __FUNCTION__, input_device);
#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    } else if (AudioIEMsController::getInstance()->isIEMsOn()) {
        input_device = AudioIEMsController::getInstance()->getInputDevice();
        ALOGD("+%s(), isIEMsOn, input_device = 0x%x", __FUNCTION__, input_device);
#endif
#ifdef MTK_CHECK_INPUT_DEVICE_PRIORITY
    } else if ((sharedDevice == true) && (mStreamInVector.size() > 1)) {
        input_device = CheckInputDevicePriority(input_device);
#endif
    }

    if (input_device == AUDIO_DEVICE_NONE ||
        input_device == current_input_device) {
        ALOGW("-%s(), input_device(0x%x) is AUDIO_DEVICE_NONE(0x%x) or current_input_device(0x%x), return",
              __FUNCTION__,
              input_device, AUDIO_DEVICE_NONE, current_input_device);
        return NO_ERROR;
    }
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->beforeInputDeviceRouting(
            input_device);
#endif
    setAllInputStreamsSuspend(true, false);
    standbyAllInputStreams();
    if (mStreamInVector.size() > 0) {
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            if (mStreamInVector[i] == NULL ||
                mStreamInVector[i]->getStreamAttribute() == NULL ||
                mStreamInVector[i]->getStreamInCaptureHandler() == NULL) {
                ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                continue;
            }
            //Need to close AAudio old device before other streamin routing
            if (mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ &&
                isPmicInputDevice(input_device)) {

                if (mStreamInVector[i]->isActive() == true) {
                    ALOGW("%s(), AAudio record(0x%x), switch to new device directly",
                          __FUNCTION__, mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags);

                    mStreamInVector[i]->getStreamInCaptureHandler()->routing(input_device);
                } else {
                    ALOGW("%s(), AAudio record(0x%x) not active, skip routing",
                          __FUNCTION__, mStreamInVector[i]->getStreamAttribute()->mAudioInputFlags);
                }

                mStreamInVector[i]->getStreamInCaptureHandler()->updateInputDevice(input_device);
            }
        }
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            if ((input_device == AUDIO_DEVICE_IN_FM_TUNER) || (current_input_device == AUDIO_DEVICE_IN_FM_TUNER)) {
                if (pAudioALSAStreamIn == mStreamInVector[i]) {
                    status = mStreamInVector[i]->routing(input_device);
                    ASSERT(status == NO_ERROR);
                }
            } else {
                status = mStreamInVector[i]->routing(input_device);
                ASSERT(status == NO_ERROR);
            }
        }
    }
    setAllInputStreamsSuspend(false, false);
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    AudioALSAUltrasoundOutOfCallController::getInstance()->afterInputDeviceRouting(input_device);
#endif
    return status;
}

// check if headset has changed
bool AudioALSAStreamManager::CheckHeadsetChange(const audio_devices_t current_output_devices, audio_devices_t output_device) {
    ALOGD("+%s(), current_output_devices = %d output_device = %d ", __FUNCTION__, current_output_devices, output_device);
    if (current_output_devices == output_device) {
        return false;
    }
    if (current_output_devices == AUDIO_DEVICE_NONE || output_device == AUDIO_DEVICE_NONE) {
        return true;
    }
    if (current_output_devices == AUDIO_DEVICE_OUT_WIRED_HEADSET || current_output_devices == AUDIO_DEVICE_OUT_WIRED_HEADPHONE
        || output_device == AUDIO_DEVICE_OUT_WIRED_HEADSET || output_device == AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
        return true;
    }
    return false;
}

status_t AudioALSAStreamManager::setFmEnable(const bool enable, bool bForceControl, bool bForce2DirectConn, audio_devices_t output_device) { // TODO(Harvey)
    //AL_AUTOLOCK(mLock);

    // Reject set fm enable during phone call mode
    if (mPhoneCallControllerStatusPolicy || isPhoneCallOpen()) {
        ALOGW("-%s(), mAudioMode(%d), phonecall is opened, return.", __FUNCTION__, mAudioMode);
        return INVALID_OPERATION;
    }

    // use primary stream out device // TODO(Harvey): add a function? get from hardware?
    audio_devices_t current_output_devices;
    if (output_device == AUDIO_DEVICE_NONE) {
        audio_devices_t primary_streamout_device = ((mStreamOutVector.size() > 0)
                                                    ? mStreamOutVector[0]->getStreamAttribute()->output_devices : AUDIO_DEVICE_NONE);
        for (size_t i = 0; i < mStreamOutVector.size(); i++) {
            if (mStreamOutVector[i]->getStreamAttribute()->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY) {
                primary_streamout_device = mStreamOutVector[i]->getStreamAttribute()->output_devices;
            }
        }
        current_output_devices = primary_streamout_device;
    } else {
        current_output_devices = output_device;
    }
    mFMController->setFmEnable(enable, current_output_devices, bForceControl, bForce2DirectConn);
    return NO_ERROR;
}

status_t AudioALSAStreamManager::setHdmiEnable(const bool enable) { // TODO(George): tmp, add a class to do it
    ALOGD("+%s(), enable = %d", __FUNCTION__, enable);
    AL_AUTOLOCK(mLock);
    AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

    if (enable == mHdmiEnable) {
        return ALREADY_EXISTS;
    }
    mHdmiEnable = enable;

    if (enable == true) {
        int pcmIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmI2S0Dl1Playback);
        int cardIdx = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmI2S0Dl1Playback);

        // DL loopback setting
        mLoopbackConfig.channels = 2;
        mLoopbackConfig.rate = 44100;
        mLoopbackConfig.period_size = 512;
        mLoopbackConfig.period_count = 4;
        mLoopbackConfig.format = PCM_FORMAT_S32_LE;
        mLoopbackConfig.start_threshold = 0;
        mLoopbackConfig.stop_threshold = 0;
        mLoopbackConfig.silence_threshold = 0;
        if (mHdmiPcm == NULL) {
            mHdmiPcm = pcm_open(cardIdx, pcmIdx, PCM_OUT, &mLoopbackConfig);
            ALOGD("pcm_open mHdmiPcm = %p", mHdmiPcm);
        }
        if (!mHdmiPcm || !pcm_is_ready(mHdmiPcm)) {
            ALOGD("Unable to open mHdmiPcm device %u (%s)", pcmIdx, pcm_get_error(mHdmiPcm));
        }

        ALOGD("pcm_start(mHdmiPcm)");
        pcm_start(mHdmiPcm);
    } else {
        ALOGD("pcm_close");
        if (mHdmiPcm != NULL) {
            pcm_close(mHdmiPcm);
            mHdmiPcm = NULL;
        }
        ALOGD("pcm_close done");
    }


    ALOGD("-%s(), enable = %d", __FUNCTION__, enable);
    return NO_ERROR;
}

status_t AudioALSAStreamManager::setLoopbackEnable(const bool enable) { // TODO(Harvey): tmp, add a class to do it
    ALOGD("+%s(), enable = %d", __FUNCTION__, enable);
    AL_AUTOLOCK(mLock);
    AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

    if (enable == mLoopbackEnable) {
        return ALREADY_EXISTS;
    }
    mLoopbackEnable = enable;

    if (enable == true) {
        int pcmIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmUlDlLoopback);
        int cardIdx = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmUlDlLoopback);

        // DL loopback setting
        mLoopbackConfig.channels = 2;
        mLoopbackConfig.rate = 48000;
        mLoopbackConfig.period_size = 512;
        mLoopbackConfig.period_count = 4;
        mLoopbackConfig.format = PCM_FORMAT_S16_LE;
        mLoopbackConfig.start_threshold = 0;
        mLoopbackConfig.stop_threshold = 0;
        mLoopbackConfig.silence_threshold = 0;
        if (mLoopbackPcm == NULL) {
            mLoopbackPcm = pcm_open(cardIdx, pcmIdx, PCM_OUT, &mLoopbackConfig);
            ALOGD("pcm_open mLoopbackPcm = %p", mLoopbackPcm);
        }
        if (!mLoopbackPcm || !pcm_is_ready(mLoopbackPcm)) {
            ALOGD("Unable to open mLoopbackPcm device %u (%s)", pcmIdx, pcm_get_error(mLoopbackPcm));
        }

        ALOGD("pcm_start(mLoopbackPcm)");
        pcm_start(mLoopbackPcm);

        //UL loopback setting
        mLoopbackUlConfig.channels = 2;
        mLoopbackUlConfig.rate = 48000;
        mLoopbackUlConfig.period_size = 512;
        mLoopbackUlConfig.period_count = 4;
        mLoopbackUlConfig.format = PCM_FORMAT_S16_LE;
        mLoopbackUlConfig.start_threshold = 0;
        mLoopbackUlConfig.stop_threshold = 0;
        mLoopbackUlConfig.silence_threshold = 0;
        if (mLoopbackUlPcm == NULL) {
            mLoopbackUlPcm = pcm_open(cardIdx, pcmIdx, PCM_IN, &mLoopbackUlConfig);
            ALOGD("pcm_open mLoopbackPcm = %p", mLoopbackUlPcm);
        }
        if (!mLoopbackUlPcm || !pcm_is_ready(mLoopbackUlPcm)) {
            ALOGD("Unable to open mLoopbackUlPcm device %u (%s)", pcmIdx, pcm_get_error(mLoopbackUlPcm));
        }
        ALOGD("pcm_start(mLoopbackUlPcm)");
        pcm_start(mLoopbackUlPcm);
    } else {
        ALOGD("pcm_close");
        if (mLoopbackPcm != NULL) {
            pcm_close(mLoopbackPcm);
            mLoopbackPcm = NULL;
        }
        if (mLoopbackUlPcm != NULL) {
            pcm_close(mLoopbackUlPcm);
            mLoopbackUlPcm = NULL;
        }
        ALOGD("pcm_close done");
    }


    ALOGD("-%s(), enable = %d", __FUNCTION__, enable);
    return NO_ERROR;
}

bool AudioALSAStreamManager::getFmEnable() {
    return mFMController->getFmEnable();
}

status_t AudioALSAStreamManager::setAllOutputStreamsSuspend(const bool suspend_on, const bool setModeRequest __unused) {
    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        ASSERT(mStreamOutVector[i]->setSuspend(suspend_on) == NO_ERROR);
    }

    return NO_ERROR;
}

status_t AudioALSAStreamManager::setAllInputStreamsSuspend(const bool suspend_on, const bool setModeRequest, const capture_handler_t caphandler) {
    ALOGV("%s()", __FUNCTION__);

    status_t status = NO_ERROR;

    AudioALSAStreamIn *pAudioALSAStreamIn = NULL;

    for (size_t i = 0; i < mStreamInVector.size(); i++) {
        pAudioALSAStreamIn = mStreamInVector[i];

        if ((setModeRequest == true) && (mEnterPhoneCallMode == true) && (mStreamInVector[i]->getStreamInCaptureHandler() != NULL)) {
            //No need to do reopen when mode change
            if ((pAudioALSAStreamIn->isSupportConcurrencyInCall()) == true) {
                ALOGD("%s(), Enter phone call mode, mStreamInVector[%zu] support concurrency!!", __FUNCTION__, i);
                continue;
            }
        }

        if (pAudioALSAStreamIn->getStreamInCaptureHandler() == NULL) {
            ALOGD("%s(), this streamin does not have capture handler, just set suspend", __FUNCTION__);
            status = pAudioALSAStreamIn->setSuspend(suspend_on);
            continue;
        }
        if (pAudioALSAStreamIn->getCaptureHandlerType() & caphandler) {
            ALOGD("%s(), find corresponding streamin, suspend it", __FUNCTION__);
            status = pAudioALSAStreamIn->setSuspend(suspend_on);
        }

        if (status != NO_ERROR) {
            ALOGE("%s(), mStreamInVector[%zu] setSuspend() fail!!", __FUNCTION__, i);
        }
    }

    ALOGV("%s()-", __FUNCTION__);
    return status;
}

status_t AudioALSAStreamManager::setAllStreamsSuspend(const bool suspend_on, const bool setModeRequest) {
    ALOGD("%s(), suspend_on = %d", __FUNCTION__, suspend_on);

    status_t status = NO_ERROR;

    status = setAllOutputStreamsSuspend(suspend_on, setModeRequest);
    status = setAllInputStreamsSuspend(suspend_on, setModeRequest);

    return status;
}


status_t AudioALSAStreamManager::standbyAllOutputStreams(const bool setModeRequest __unused) {
    ALOGD_IF(mLogEnable, "%s()", __FUNCTION__);
    status_t status = NO_ERROR;

    AudioALSAStreamOut *pAudioALSAStreamOut = NULL;

    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        pAudioALSAStreamOut = mStreamOutVector[i];
        status = pAudioALSAStreamOut->standbyStreamOut();
        if (status != NO_ERROR) {
            ALOGE("%s(), mStreamOutVector[%zu] standbyStreamOut() fail!!", __FUNCTION__, i);
        }
    }

    return status;
}

status_t AudioALSAStreamManager::standbyAllInputStreams(const bool setModeRequest, capture_handler_t caphandler) {
    ALOGD_IF(mLogEnable, "%s()", __FUNCTION__);
    status_t status = NO_ERROR;

    AudioALSAStreamIn *pAudioALSAStreamIn = NULL;

    for (size_t i = 0; i < mStreamInVector.size(); i++) {
        pAudioALSAStreamIn = mStreamInVector[i];

        if ((setModeRequest == true) && (mEnterPhoneCallMode == true) && (mStreamInVector[i]->getStreamInCaptureHandler() != NULL)) {
            //No need to do reopen when mode change
            if ((pAudioALSAStreamIn->isSupportConcurrencyInCall()) == true) {
                ALOGD("%s(), Enter phone call mode, mStreamInVector[%zu] support concurrency!!", __FUNCTION__, i);
                continue;
            }
        }

        if (pAudioALSAStreamIn->getStreamInCaptureHandler() == NULL) {
            ALOGD("%s(), mStreamInVector[%zu] capture handler not created yet, pAudioALSAStreamIn=%p, this=%p", __FUNCTION__, i, pAudioALSAStreamIn, this);
            continue;
        }

        if ((pAudioALSAStreamIn->getStreamInCaptureHandler() != NULL) && (pAudioALSAStreamIn->getStreamInCaptureHandler()->getCaptureHandlerType() & caphandler)) {
            ALOGD("%s(), find corresponding streamin, standby it", __FUNCTION__);
            status = pAudioALSAStreamIn->standby();
        }

        if (status != NO_ERROR) {
            ALOGE("%s(), mStreamInVector[%zu] standby() fail!!", __FUNCTION__, i);
        }
    }

    ALOGV("%s()-", __FUNCTION__);
    return status;
}

status_t AudioALSAStreamManager::standbyAllStreams(const bool setModeRequest) {
    ALOGD_IF("%s()", __FUNCTION__);

    status_t status = NO_ERROR;

    status = standbyAllOutputStreams(setModeRequest);
    status = standbyAllInputStreams(setModeRequest);

    return status;
}


uint32_t AudioALSAStreamManager::getActiveStreamOutSize() {
    uint32_t size = 0;
    uint32_t i = 0;

    for (i = 0; i < mStreamOutVector.size(); i++) {
        if (mStreamOutVector[i]->isOutPutStreamActive()) {
            size++;
        }
    }

    return size;
}

void AudioALSAStreamManager::logStreamDumpSize() {
    uint32_t i = 0;

    for (i = 0; i < mStreamOutVector.size(); i++) {
        mStreamOutVector[i]->logDumpSize();
    }
}

void AudioALSAStreamManager::dynamicSetAudioDump(AUDIO_DUMP_TYPE dump_type) {

    ALOGD("%s(), dump_type = %d", __FUNCTION__, dump_type);

    if (dump_type == AUDIO_DUMP_DOWNLINK) {
        for (size_t i = 0; i < mStreamOutVector.size(); i++) {

            bool isStreamoutActive = mStreamOutVector[i]->isOutPutStreamActive();

            if (isStreamoutActive) {
                ALOGD("%s(), flag: %d, dynamicSetStreamOutAudioDump()",
                      __FUNCTION__, mStreamOutVector[i]->getStreamAttribute()->mAudioOutputFlags);
                mStreamOutVector[i]->dynamicSetStreamOutAudioDump();
            }
        }
    } else if (dump_type == AUDIO_DUMP_UPLINK) {

        for (size_t i = 0; i < mStreamInVector.size(); i++) {

            bool isStreaminActive = mStreamInVector[i]->isInputStreamActive();

            if (isStreaminActive) {
                mStreamInVector[i]->dynamicSetStreamInAudioDump();
            }
        }
    }
}

#if defined(MTK_HIFIAUDIO_SUPPORT)
int AudioALSAStreamManager::setAllStreamHiFi(AudioALSAStreamOut *pAudioALSAStreamOut, uint32_t sampleRate) {
    int status = 0;
    const audio_devices_t streamOutDevice = pAudioALSAStreamOut->getStreamAttribute()->output_devices;
    audio_output_flags_t streamOutFlag = pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags;

    bool bFMState = mFMController->getFmEnable();
    bool bHiFiState = AudioALSAHardwareResourceManager::getInstance()->getHiFiStatus();
    bool needEnableFm = false;

    uint32_t old_sampleRate = AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate();
    uint32_t new_sampleRate;

    if (streamOutDevice != AUDIO_DEVICE_OUT_WIRED_HEADSET && streamOutDevice != AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
        new_sampleRate = AUDIO_HIFI_RATE_DEFAULT;
    } else { /* only headset support hifi playback */
        new_sampleRate = sampleRate;
    }
    ALOGD("%s(), flag = %d, device = %d, mHiFiState = %d, sampleRate (%u --> %u), FM state = %d",
          __FUNCTION__, streamOutFlag, streamOutDevice, bHiFiState, old_sampleRate, new_sampleRate, bFMState);

    if (old_sampleRate == new_sampleRate) {
        ALOGD("%s(), don't need update PrimaryStreamOutSampleRate, return status = %d", __FUNCTION__, status);
        return status;
    }

    AL_AUTOLOCK(mStreamVectorLock);

    if (bFMState) {
        ALOGV("%s(), getFmEnable() = true, setFmEnable(false), sampleRate = %d", __FUNCTION__, new_sampleRate);
        mFMController->setFmEnable(false, streamOutDevice, false, false, true);
        needEnableFm = true;
    }

    setAllStreamsSuspend(true, true);
    standbyAllStreams(true);
    AL_AUTOLOCK(mLock);
    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        if (mStreamOutVector[i]->getStreamAttribute()->mAudioOutputFlags == streamOutFlag) {
            ALOGD("%s(), flag: %d, update streamout source sampling rate(%d)",
                  __FUNCTION__, mStreamOutVector[i]->getStreamAttribute()->mAudioOutputFlags, new_sampleRate);
            mStreamOutVector[i]->setStreamOutSampleRate(new_sampleRate);
        } else {
            ALOGV("%s(), flag: %d, dont update streamout source sampling rate",
                  __FUNCTION__, mStreamOutVector[i]->getStreamAttribute()->mAudioOutputFlags);
        }
    }

    // Update sample rate
    ALOGD("%s(), Update target sample rate from %d to %d", __FUNCTION__, old_sampleRate, new_sampleRate);
    AudioALSASampleRateController::getInstance()->setPrimaryStreamOutSampleRate(new_sampleRate);

    setAllStreamsSuspend(false, true);

    if (needEnableFm) {
        ALOGV("%s(), re-Enable FM", __FUNCTION__);
        mFMController->setFmEnable(true, streamOutDevice, true, true, true);
    }

    ALOGD("%s(), return status = %d", __FUNCTION__, status);
    return status;
}
#endif

audio_devices_t AudioALSAStreamManager::CheckInputDevicePriority(audio_devices_t input_device) {
    const stream_attribute_t *mStreamAttributeTarget;

    for (size_t i = 0; i < mStreamInVector.size(); i++) {
        if (setUsedDevice(input_device) == 0) {
            break;
        }

        mStreamAttributeTarget = mStreamInVector[i]->getStreamAttribute();

        if (mStreamAttributeTarget == NULL) {
            continue;
        }

        audio_devices_t old_device = mStreamAttributeTarget->input_device;
        bool sharedDevice = (old_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
        if (sharedDevice == false) {
            continue;
        }
        if ((old_device != input_device) && (setUsedDevice(old_device) < setUsedDevice(input_device))) {
            input_device = old_device;
        }
    }
    ALOGD("%s(), input_device = 0x%x", __FUNCTION__, input_device);
    return input_device;
}

uint32_t AudioALSAStreamManager::setUsedDevice(const audio_devices_t used_device) {
    uint32_t usedInputDeviceIndex = 0;
    switch (used_device) {
    case AUDIO_DEVICE_IN_BUILTIN_MIC: {
        usedInputDeviceIndex = 0;
        break;
    }
    case AUDIO_DEVICE_IN_WIRED_HEADSET: {
        usedInputDeviceIndex = 1;
        break;
    }
    case AUDIO_DEVICE_IN_BACK_MIC: {
        usedInputDeviceIndex = 2;
        break;
    }
    default: break;
    }
    return usedInputDeviceIndex;
}

bool AudioALSAStreamManager::CheckStreaminPhonecallRouting(audio_devices_t new_phonecall_device, bool checkrouting) {
    if (checkrouting == true) { //Already Routing, Need to resume streamin
        setAllInputStreamsSuspend(false, false);
    } else { //Need to check the streamin to do routing
        bool newsharedDevice = ((new_phonecall_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET));
        if ((mStreamInVector.size() > 0) && (newsharedDevice == true)) {
            status_t status = NO_ERROR;
            bool oldsharedDevice = 0;
            audio_devices_t old_device;
            for (size_t i = 0; i < mStreamInVector.size(); i++) {
                if (mStreamInVector[i] == NULL ||
                    mStreamInVector[i]->getStreamAttribute() == NULL) {
                    ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                    continue;
                }
                old_device = mStreamInVector[i]->getStreamAttribute()->input_device;
                oldsharedDevice = (old_device & ~AUDIO_DEVICE_BIT_IN) & (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
                if ((oldsharedDevice == true) && (old_device != new_phonecall_device)) {
                    if (checkrouting == false) {
                        setAllInputStreamsSuspend(true, false);
                        standbyAllInputStreams(false);
                        checkrouting = true;
                    }
                    ALOGD("+%s(),old_device = 0x%x -> new_phonecall_device = 0x%x", __FUNCTION__, oldsharedDevice, new_phonecall_device);
                    status = mStreamInVector[i]->routing(new_phonecall_device);
                    ASSERT(status == NO_ERROR);
                }
            }
        }
    }
    return checkrouting;
}

bool AudioALSAStreamManager::getPhoncallOutputDevice() {
#ifdef FORCE_ROUTING_RECEIVER
    const audio_devices_t current_output_devices = AUDIO_DEVICE_OUT_EARPIECE;
#else
    const audio_devices_t current_output_devices = (mStreamOutVector.size() > 0)
                                                   ? mStreamOutVector[0]->getStreamAttribute()->output_devices
                                                   : AUDIO_DEVICE_NONE;
#endif
    ALOGD("%s(),current_output_devices = %d ", __FUNCTION__, current_output_devices);
    bool bt_device_on = audio_is_bluetooth_sco_device(current_output_devices);
    ALOGD("%s(),bt_device_on = %d ", __FUNCTION__, bt_device_on);
    return bt_device_on;
}

size_t AudioALSAStreamManager::getInputBufferSize(uint32_t sampleRate, audio_format_t format, uint32_t channelCount) {
    size_t wordSize = 0;
    switch (format) {
    case AUDIO_FORMAT_PCM_8_BIT: {
        wordSize = sizeof(int8_t);
        break;
    }
    case AUDIO_FORMAT_PCM_16_BIT: {
        wordSize = sizeof(int16_t);
        break;
    }
    case AUDIO_FORMAT_PCM_8_24_BIT:
    case AUDIO_FORMAT_PCM_32_BIT: {
        wordSize = sizeof(int32_t);
        break;
    }
    default: {
        ALOGW("%s(), wrong format(0x%x), default use wordSize = %zu", __FUNCTION__, format, sizeof(int16_t));
        wordSize = sizeof(int16_t);
        break;
    }
    }

    size_t bufferSize = ((sampleRate * channelCount * wordSize) * 20) / 1000; // TODO (Harvey): why 20 ms here?

    ALOGD("%s(), sampleRate = %u, format = 0x%x, channelCount = %d, bufferSize = %zu",
          __FUNCTION__, sampleRate, format, channelCount, bufferSize);
    return bufferSize;
}

status_t AudioALSAStreamManager::updateOutputDeviceForAllStreamIn_l(audio_devices_t output_devices) {
    status_t status = NO_ERROR;

    if (mStreamInVector.size() > 0) {
        // update the output device info for input stream
        // (ex:for BesRecord parameters update or mic device change)
        ALOGD_IF(mLogEnable, "%s(), mStreamInVector.size() = %zu", __FUNCTION__, mStreamInVector.size());
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            status = mStreamInVector[i]->updateOutputDeviceInfoForInputStream(output_devices);
            ASSERT(status == NO_ERROR);
        }
    }

    return status;
}

status_t AudioALSAStreamManager::updateOutputDeviceForAllStreamIn(audio_devices_t output_devices) {
    AL_AUTOLOCK(mLock);

    return updateOutputDeviceForAllStreamIn_l(output_devices);
}

// set musicplus to streamout
status_t AudioALSAStreamManager::SetMusicPlusStatus(bool bEnable) {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        pTempFilter->setParamFixed(bEnable ? true : false);
    }
#else
    (void) bEnable;
#endif
    return NO_ERROR;
}

bool AudioALSAStreamManager::GetMusicPlusStatus() {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        bool musicplus_status = pTempFilter->isParamFixed();
        if (musicplus_status) {
            return true;
        }
    }
    return false;
#else
    return false;
#endif
}

status_t AudioALSAStreamManager::UpdateACFHCF(int value) {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    ALOGD("%s()", __FUNCTION__);

    AUDIO_ACF_CUSTOM_PARAM_STRUCT sACFHCFParam;

    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        if (value == 0) {
            ALOGD("setParameters Update ACF Parames");
            getAudioCompFltCustParam(AUDIO_COMP_FLT_AUDIO, &sACFHCFParam);
            pTempFilter->setParameter(AUDIO_COMP_FLT_AUDIO, &sACFHCFParam);

        } else if (value == 1) {
            ALOGD("setParameters Update HCF Parames");
            getAudioCompFltCustParam(AUDIO_COMP_FLT_HEADPHONE, &sACFHCFParam);
            pTempFilter->setParameter(AUDIO_COMP_FLT_HEADPHONE, &sACFHCFParam);

        } else if (value == 2) {
            ALOGD("setParameters Update ACFSub Parames");
            getAudioCompFltCustParam(AUDIO_COMP_FLT_AUDIO_SUB, &sACFHCFParam);
            pTempFilter->setParameter(AUDIO_COMP_FLT_AUDIO_SUB, &sACFHCFParam);

        }
    }
#else
    (void) value;
#endif
    return NO_ERROR;
}

// ACF Preview parameter
status_t AudioALSAStreamManager::SetACFPreviewParameter(void *ptr, int len __unused) {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    ALOGD("%s()", __FUNCTION__);

    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        pTempFilter->setParameter(AUDIO_COMP_FLT_AUDIO, (AUDIO_ACF_CUSTOM_PARAM_STRUCT *)ptr);
    }
#else
    (void *) ptr;
#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::SetHCFPreviewParameter(void *ptr, int len __unused) {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    ALOGD("%s()", __FUNCTION__);

    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        pTempFilter->setParameter(AUDIO_COMP_FLT_HEADPHONE, (AUDIO_ACF_CUSTOM_PARAM_STRUCT *)ptr);
    }
#else
    (void *) ptr;
#endif
    return NO_ERROR;
}


status_t AudioALSAStreamManager::SetBesLoudnessStatus(bool bEnable) {
    ALOGD("mBesLoudnessStatus() flag %d", bEnable);

    bool foValue_MTK_BESLOUDNESS_SUPPORT = appIsFeatureOptionEnabled("MTK_BESLOUDNESS_SUPPORT");
    if (foValue_MTK_BESLOUDNESS_SUPPORT) {
        mBesLoudnessStatus = bEnable;
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
        setBesLoudnessStateToXML(mBesLoudnessStatus);
        int checkvalue = getBesLoudnessStateFromXML();
        ALOGD("recheck besLoudnessInXML %d", checkvalue);
#endif
        property_set(PROPERTY_KEY_BESLOUDNESS_SWITCH_STATE, mBesLoudnessStatus ? "1" : "0");
        if (mBesLoudnessControlCallback != NULL) {
            mBesLoudnessControlCallback((void *)mBesLoudnessStatus);
        }
    } else {
        ALOGD("Unsupport set mBesLoudnessStatus()");
    }
    return NO_ERROR;
}

bool AudioALSAStreamManager::GetBesLoudnessStatus() {
    return mBesLoudnessStatus;
}

status_t AudioALSAStreamManager::SetBesLoudnessControlCallback(const BESLOUDNESS_CONTROL_CALLBACK_STRUCT *callback_data) {
    if (callback_data == NULL) {
        mBesLoudnessControlCallback = NULL;
    } else {
        mBesLoudnessControlCallback = callback_data->callback;
        if (mBesLoudnessControlCallback == NULL) {
            ALOGE("%s, mBesLoudnessControlCallback = NULL", __FUNCTION__);
            return INVALID_OPERATION;
        } else {
            mBesLoudnessControlCallback((void *)mBesLoudnessStatus);
        }
    }

    return NO_ERROR;
}

status_t AudioALSAStreamManager::setSpkOutputGain(int32_t gain, uint32_t ramp_sample_cnt) {
    ALOGD("%s(), gain = %d, ramp_sample_cnt = %u", __FUNCTION__, gain, ramp_sample_cnt);

#if 0 //K2 mark temp
    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        pTempFilter->setSpkOutputGain(gain, ramp_sample_cnt);
    }
#endif

    return NO_ERROR;
}

status_t AudioALSAStreamManager::setSpkFilterParam(uint32_t fc, uint32_t bw, int32_t th) {
    ALOGD("%s(), fc %d, bw %d, th %d", __FUNCTION__, fc, bw, th);

#if 0 //K2 mark temp
    for (size_t i = 0; i < mFilterManagerVector.size() ; i++) {
        AudioMTKFilterManager  *pTempFilter = mFilterManagerVector[i];
        pTempFilter->setSpkFilterParam(fc, bw, th);
    }
#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::SetEMParameter(AUDIO_CUSTOM_PARAM_STRUCT *pSphParamNB) {
#if !defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    ALOGD("%s()", __FUNCTION__);
    mAudioCustParamClient->SetNBSpeechParamToNVRam(pSphParamNB);
    SpeechEnhancementController::GetInstance()->SetNBSpeechParametersToAllModem(pSphParamNB);
    // Speech Enhancement, VM, Speech Driver
    // update VM/EPL/TTY record capability & enable if needed
    SpeechVMRecorder::getInstance()->configVm(pSphParamNB);
#else
    (void) pSphParamNB;
    ALOGW("%s(), speech param not supported in nvram. return", __FUNCTION__);
#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::updateSpeechNVRAMParam(const int speech_band) {
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    (void) speech_band;
#else
    ALOGD("%s(), speech_band=%d", __FUNCTION__, speech_band);

    //speech_band: 0:Narrow Band, 1: Wide Band, 2: Super Wideband, ..., 8: All
    if (speech_band == 0) { //Narrow Band
        AUDIO_CUSTOM_PARAM_STRUCT eSphParamNB;
        mAudioCustParamClient->GetNBSpeechParamFromNVRam(&eSphParamNB);
        SpeechEnhancementController::GetInstance()->SetNBSpeechParametersToAllModem(&eSphParamNB);
        ALOGD("JT:================================");
        for (int i = 0; i < SPEECH_COMMON_NUM ; i++) {
            ALOGD("JT:speech_common_para[%d] = %d", i, eSphParamNB.speech_common_para[i]);
        }
        for (int i = 0; i < SPEECH_PARA_MODE_NUM; i++) {
            for (int j = 0; j < SPEECH_PARA_NUM; j++) {
                ALOGD("JT:speech_mode_para[%d][%d] = %d", i, j, eSphParamNB.speech_mode_para[i][j]);
            }
        }
        for (int i = 0; i < 4; i++) {
            ALOGD("JT:speech_volume_para[%d] = %d", i, eSphParamNB.speech_volume_para[i]);
        }
    }
    else if (speech_band == 1) { //Wide Band
        AUDIO_CUSTOM_WB_PARAM_STRUCT eSphParamWB;
        mAudioCustParamClient->GetWBSpeechParamFromNVRam(&eSphParamWB);
        SpeechEnhancementController::GetInstance()->SetWBSpeechParametersToAllModem(&eSphParamWB);
    }
    else if (speech_band == 8) { //set all mode parameters
        AUDIO_CUSTOM_PARAM_STRUCT eSphParamNB;
        AUDIO_CUSTOM_WB_PARAM_STRUCT eSphParamWB;
        mAudioCustParamClient->GetNBSpeechParamFromNVRam(&eSphParamNB);
        SpeechEnhancementController::GetInstance()->SetNBSpeechParametersToAllModem(&eSphParamNB);
        mAudioCustParamClient->GetWBSpeechParamFromNVRam(&eSphParamWB);
        SpeechEnhancementController::GetInstance()->SetWBSpeechParametersToAllModem(&eSphParamWB);
    }

    if (isPhoneCallOpen() == true) { // get output device for in_call, and set speech mode
        UpdateSpeechMode();
    }
#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::UpdateSpeechLpbkParams() {
#if !defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    ALOGD("%s()", __FUNCTION__);
    AUDIO_CUSTOM_PARAM_STRUCT eSphParamNB;
    AUDIO_CUSTOM_SPEECH_LPBK_PARAM_STRUCT  eSphParamNBLpbk;
    mAudioCustParamClient->GetNBSpeechParamFromNVRam(&eSphParamNB);
    mAudioCustParamClient->GetNBSpeechLpbkParamFromNVRam(&eSphParamNBLpbk);
    SpeechEnhancementController::GetInstance()->SetNBSpeechLpbkParametersToAllModem(&eSphParamNB, &eSphParamNBLpbk);
    //no need to set speech mode, only for loopback parameters update
#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::UpdateMagiConParams() {
    if (AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport() < 2) {
        ALOGW("-%s(), MagiConference Not Support", __FUNCTION__);
        return INVALID_OPERATION;
    }

    ALOGD("%s()", __FUNCTION__);
#if defined(MTK_MAGICONFERENCE_SUPPORT)
#ifndef MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
    AUDIO_CUSTOM_MAGI_CONFERENCE_STRUCT eSphParamMagiCon;
    mAudioCustParamClient->GetMagiConSpeechParamFromNVRam(&eSphParamMagiCon);
    SpeechEnhancementController::GetInstance()->SetMagiConSpeechParametersToAllModem(&eSphParamMagiCon);

    if (isPhoneCallOpen() == true) { // get output device for in_call, and set speech mode
        UpdateSpeechMode();
    }

#endif
    return NO_ERROR;
#else
    ALOGW("-%s(), MagiConference Not Support", __FUNCTION__);
    return INVALID_OPERATION;
#endif
}

status_t AudioALSAStreamManager::UpdateHACParams() {
    ALOGD("%s()", __FUNCTION__);
#ifndef MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
    AUDIO_CUSTOM_HAC_PARAM_STRUCT eSphParamHAC;
    mAudioCustParamClient->GetHACSpeechParamFromNVRam(&eSphParamHAC);
    SpeechEnhancementController::GetInstance()->SetHACSpeechParametersToAllModem(&eSphParamHAC);

    if (isPhoneCallOpen() == true) { // get output device for in_call, and set speech mode
        UpdateSpeechMode();
    }

#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::UpdateDualMicParams() {
    ALOGD("%s()", __FUNCTION__);
#ifndef MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT
    AUDIO_CUSTOM_EXTRA_PARAM_STRUCT eSphParamDualMic;
    mAudioCustParamClient->GetDualMicSpeechParamFromNVRam(&eSphParamDualMic);
    if (AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport() >= 2) {
        SpeechEnhancementController::GetInstance()->SetDualMicSpeechParametersToAllModem(&eSphParamDualMic);
    }

    if (isPhoneCallOpen() == true) { // get output device for in_call, and set speech mode
        UpdateSpeechMode();
    }

#endif
    return NO_ERROR;
}

void AudioALSAStreamManager::volumeChangedCallback() {
    ALOGD("%s() stream= %x, device= %x, index= %x", __FUNCTION__, mVoiceStream, mVoiceDevice, mVoiceVolumeIndex);

    AL_AUTOLOCK(mStreamVectorLock);
    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        ALOGD("%s() update Streamout[%zu]", __FUNCTION__, i);
        mStreamOutVector[i]->updateVolumeIndex(mVoiceStream, mVoiceDevice, mVoiceVolumeIndex);
    }

    // Disable UL volume change update to improve performance
    //for (size_t i = 0; i < mStreamInVector.size(); i++) {
    //    mStreamInVector[i]->updateVolumeIndex(mVoiceStream, mVoiceDevice, mVoiceVolumeIndex);
    //}
}

int AudioALSAStreamManager::setVolumeIndex(int stream, int device, int index) {
    ALOGD("%s() stream= %x, device= %x, index= %x", __FUNCTION__, stream, device, index);

    // 0:voice_call/voip_call, 6:BTSCO
    if (stream == 0 || stream == 6) {
        if (isPhoneCallOpen() == true) {
#if !defined(MTK_SPEECH_VOLUME_0_FORCE_AUDIBLE)
            if (index == 0) {
                mSpeechPhoneCallController->setDynamicDlMute(true);
            } else if (mSpeechPhoneCallController->getDynamicDlMute() == true) {
                mSpeechPhoneCallController->setDynamicDlMute(false);
            }
#endif
            mSpeechDriverFactory->GetSpeechDriver()->setMDVolumeIndex(stream, device, index);
        } else {
            SpeechDriverInterface *pSpeechDriver = NULL;
            for (int mdIndex = MODEM_1; mdIndex < NUM_MODEM; mdIndex++) {
                pSpeechDriver = mSpeechDriverFactory->GetSpeechDriverByIndex((modem_index_t)mdIndex);
                if (pSpeechDriver != NULL) { // Might be single talk and some speech driver is NULL
                    pSpeechDriver->setMDVolumeIndex(stream, device, index);
                }
            }

            /* Update voice volume information to AP Call SWIP */
            mVoiceVolumeIndex = index;
            mVoiceStream = stream;
            mVoiceDevice = device;
#if defined(MTK_TC10_FEATURE) && defined(MTK_TC10_IN_HOUSE)
            AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_VOLUME_CHANGE, this);
#endif
        }
    }
    mVolumeIndex = index;
    return NO_ERROR;
}

status_t AudioALSAStreamManager::UpdateSpeechMode() {
    ALOGD("%s()", __FUNCTION__);
    //tina todo
    const audio_devices_t output_device = (audio_devices_t)AudioALSASpeechPhoneCallController::getInstance()->getAdjustedOutputDevice();
    const audio_devices_t input_device  = (audio_devices_t)AudioALSASpeechPhoneCallController::getInstance()->getAdjustedInputDevice();
    mSpeechDriverFactory->GetSpeechDriver()->SetSpeechMode(input_device, output_device);

    return NO_ERROR;
}

status_t AudioALSAStreamManager::UpdateSpeechVolume() {
    ALOGD("%s()", __FUNCTION__);
    mAudioALSAVolumeController->initVolumeController();

    if (isPhoneCallOpen() == true) {
        //TINA TODO GET DEVICE
        int32_t outputDevice = (audio_devices_t)AudioALSASpeechPhoneCallController::getInstance()->getOutputDevice();
        AudioALSASpeechPhoneCallController *pSpeechPhoneCallController = AudioALSASpeechPhoneCallController::getInstance();
#ifndef MTK_AUDIO_GAIN_TABLE
        mAudioALSAVolumeController->setVoiceVolume(mAudioALSAVolumeController->getVoiceVolume(),
                                                   getModeForGain(), (uint32)outputDevice);
#endif
        switch (outputDevice) {
        case AUDIO_DEVICE_OUT_WIRED_HEADSET : {
#ifdef  MTK_TTY_SUPPORT
            if (pSpeechPhoneCallController->getTtyMode() == AUD_TTY_VCO) {
                mAudioALSAVolumeController->ApplyMicGain(Normal_Mic, getModeForGain());
            } else if (pSpeechPhoneCallController->getTtyMode() == AUD_TTY_HCO || pSpeechPhoneCallController->getTtyMode() == AUD_TTY_FULL) {
                mAudioALSAVolumeController->ApplyMicGain(TTY_CTM_Mic, getModeForGain());
            } else {
                mAudioALSAVolumeController->ApplyMicGain(Headset_Mic, getModeForGain());
            }
#else
            mAudioALSAVolumeController->ApplyMicGain(Headset_Mic, getModeForGain());
#endif
            break;
        }
        case AUDIO_DEVICE_OUT_WIRED_HEADPHONE : {
#ifdef  MTK_TTY_SUPPORT
            if (pSpeechPhoneCallController->getTtyMode() == AUD_TTY_VCO) {
                mAudioALSAVolumeController->ApplyMicGain(Normal_Mic, getModeForGain());
            } else if (pSpeechPhoneCallController->getTtyMode() == AUD_TTY_HCO || pSpeechPhoneCallController->getTtyMode() == AUD_TTY_FULL) {
                mAudioALSAVolumeController->ApplyMicGain(TTY_CTM_Mic, getModeForGain());
            } else {
                mAudioALSAVolumeController->ApplyMicGain(Headset_Mic, getModeForGain());
            }
#else
            mAudioALSAVolumeController->ApplyMicGain(Headset_Mic, getModeForGain());
#endif
            break;
        }
        case AUDIO_DEVICE_OUT_SPEAKER: {
#ifdef  MTK_TTY_SUPPORT
            if (pSpeechPhoneCallController->getTtyMode() == AUD_TTY_VCO) {
                mAudioALSAVolumeController->ApplyMicGain(Normal_Mic, getModeForGain());
            } else if (pSpeechPhoneCallController->getTtyMode() == AUD_TTY_HCO || pSpeechPhoneCallController->getTtyMode() == AUD_TTY_FULL) {
                mAudioALSAVolumeController->ApplyMicGain(TTY_CTM_Mic, getModeForGain());
            } else {
                mAudioALSAVolumeController->ApplyMicGain(Handfree_Mic, getModeForGain());
            }
#else
            mAudioALSAVolumeController->ApplyMicGain(Handfree_Mic, getModeForGain());
#endif
            break;
        }
        case AUDIO_DEVICE_OUT_EARPIECE: {
            mAudioALSAVolumeController->ApplyMicGain(Normal_Mic, getModeForGain());
            break;
        }
        default: {
            break;
        }
        }
    } else {
        setMasterVolume(mAudioALSAVolumeController->getMasterVolume());
    }
    return NO_ERROR;

}

status_t AudioALSAStreamManager::SetVCEEnable(bool bEnable) {
    ALOGD("%s()", __FUNCTION__);
    SpeechEnhancementController::GetInstance()->SetDynamicMaskOnToAllModem(SPH_ENH_DYNAMIC_MASK_VCE, bEnable);
    return NO_ERROR;

}

status_t AudioALSAStreamManager::SetMagiConCallEnable(bool bEnable) {
    ALOGD("%s(), bEnable=%d", __FUNCTION__, bEnable);

    // enable/disable flag
    SpeechEnhancementController::GetInstance()->SetMagicConferenceCallOn(bEnable);
    if (isPhoneCallOpen() == true) { // get output device for in_call, and set speech mode
        UpdateSpeechMode();
    }

    return NO_ERROR;

}

bool AudioALSAStreamManager::GetMagiConCallEnable(void) {
    bool bEnable = SpeechEnhancementController::GetInstance()->GetMagicConferenceCallOn();
    ALOGD("-%s(), bEnable=%d", __FUNCTION__, bEnable);

    return bEnable;
}

status_t AudioALSAStreamManager::SetHACEnable(bool bEnable) {
    ALOGD("%s(), bEnable=%d", __FUNCTION__, bEnable);

    // enable/disable flag
    SpeechEnhancementController::GetInstance()->SetHACOn(bEnable);
    if (isPhoneCallOpen() == true) { // get output device for in_call, and set speech mode
        UpdateSpeechMode();
    }

    return NO_ERROR;

}

bool AudioALSAStreamManager::GetHACEnable(void) {
    bool bEnable = SpeechEnhancementController::GetInstance()->GetHACOn();
    ALOGD("-%s(), bEnable=%d", __FUNCTION__, bEnable);

    return bEnable;
}

status_t AudioALSAStreamManager::setSoftwareBridgeEnable(bool bEnable) {
    ALOGD("%s(), mSwBridgeEnable: %d -> %d", __FUNCTION__, mSwBridgeEnable, bEnable);
    mSwBridgeEnable = bEnable;
    return NO_ERROR;
}

/**
 * reopen Phone Call audio path according to RIL mapped modem notify
 */
int AudioALSAStreamManager::phoneCallRefreshModem(const char *rilMappedMDName) {
    bool isPhoneCallNeedReopen = false;
    modem_index_t rilMappedMDIdx = MODEM_1;
    audio_mode_t currentAudioMode = getMode();

    if (rilMappedMDName != NULL) {
        if (isPhoneCallOpen()) {
            if (strcmp("MD1", rilMappedMDName) == 0) {
                rilMappedMDIdx = MODEM_1;
            } else if (strcmp("MD3", rilMappedMDName) == 0) {
                rilMappedMDIdx = MODEM_EXTERNAL;
            } else {
                ALOGW("%s(), Invalid rilMappedMDName=%s, currentAudioMode(%d), isPhoneCallOpen(%d)",
                      __FUNCTION__, rilMappedMDName, currentAudioMode, isPhoneCallOpen());
                return -EINVAL;
            }
            isPhoneCallNeedReopen = mSpeechPhoneCallController->checkReopen(rilMappedMDIdx);
        }
        ALOGV("%s(), rilMappedMDName=%s, currentAudioMode(%d), isPhoneCallNeedReopen(%d)",
              __FUNCTION__, rilMappedMDName, currentAudioMode, isPhoneCallNeedReopen);
    } else {
        ALOGW("%s(), Invalid rilMappedMDName=NULL, currentAudioMode(%d)", __FUNCTION__, currentAudioMode);
        return -EINVAL;
    }
    if (isPhoneCallNeedReopen) {
        ALOGD("%s(), rilMappedMDIdx(%d), currentAudioMode(%d), start to reopen",
              __FUNCTION__, rilMappedMDIdx, currentAudioMode);
        phoneCallReopen();
    } else {
        ALOGD("-%s(), rilMappedMDName=%s, currentAudioMode(%d), no need to reopen",
              __FUNCTION__, rilMappedMDName, currentAudioMode);
    }
    return 0;
}

/**
 * reopen Phone Call audio path
 */
int AudioALSAStreamManager::phoneCallReopen() {
    AL_AUTOLOCK(mStreamVectorLock);
    audio_mode_t currentAudioMode = getMode();
    ALOGD("+%s(), currentAudioMode(%d)", __FUNCTION__, currentAudioMode);

    if (isPhoneCallOpen()) {
        setAllStreamsSuspend(true, true);
        standbyAllStreams(true);
        AL_AUTOLOCK(mLock);
        mSpeechPhoneCallController->setMicMute(true);
        const audio_devices_t phonecallOutputDevice = mSpeechPhoneCallController->getOutputDevice();
        const audio_devices_t phonecallInputputDevice = mSpeechPhoneCallController->getAdjustedInputDevice();

        mSpeechPhoneCallController->close();
        mSpeechPhoneCallController->open(currentAudioMode, phonecallOutputDevice, phonecallInputputDevice);

        mAudioALSAVolumeController->setVoiceVolume(mAudioALSAVolumeController->getVoiceVolume(),
                                                   getModeForGain(), phonecallOutputDevice);
        mSpeechPhoneCallController->setMicMute(mMicMute);
        setAllStreamsSuspend(false, true);
        ALOGD("-%s(), currentAudioMode(%d), phonecallOutputDevice(0x%x), reopen end",
              __FUNCTION__, currentAudioMode, phonecallOutputDevice);
        return 0;
    } else {
        ALOGW("-%s(), isPhoneCallOpen(%d) skip reopen.", __FUNCTION__, isPhoneCallOpen());
        return -EINVAL;
    }
}

/**
 * update Phone Call phone id
 */
int AudioALSAStreamManager::phoneCallUpdatePhoneId(const phone_id_t phoneId) {
    if (phoneId != PHONE_ID_0 && phoneId != PHONE_ID_1) {
        return -EINVAL;
    }
    if (isPhoneCallOpen()) {
        phone_id_t currentPhoneId = mSpeechPhoneCallController->getPhoneId();

        if (phoneId != currentPhoneId) {
            ALOGD("%s(), phoneId(%d->%d), mAudioMode(%d), isModeInPhoneCall(%d)",
                  __FUNCTION__, currentPhoneId, phoneId, mAudioMode, isPhoneCallOpen());
            mSpeechPhoneCallController->setPhoneId(phoneId);
            modem_index_t newMDIdx = mSpeechPhoneCallController->getIdxMDByPhoneId(phoneId);
            if (newMDIdx == MODEM_EXTERNAL) {
                phoneCallRefreshModem("MD3");
            } else {
                phoneCallRefreshModem("MD1");
            }
        }
    } else {
        mSpeechPhoneCallController->setPhoneId(phoneId);
    }
    return 0;
}

status_t AudioALSAStreamManager::SetBtHeadsetName(const char *btHeadsetName) {
    if (mBtHeadsetName) {
        free((void *)mBtHeadsetName);
        mBtHeadsetName = NULL;
    }
    if (btHeadsetName) {
        mBtHeadsetName = strdup(btHeadsetName);
        ALOGV("%s(), mBtHeadsetName = %s", __FUNCTION__, mBtHeadsetName);
    }
    return NO_ERROR;
}

const char *AudioALSAStreamManager::GetBtHeadsetName() {
    return mBtHeadsetName;
}

status_t AudioALSAStreamManager::SetBtHeadsetNrec(bool bEnable) {
#if defined(CONFIG_MT_ENG_BUILD)
    // Used for testing the BT_NREC_OFF case
    char property_value[PROPERTY_VALUE_MAX] = {0};
    property_get(PROPERTY_KEY_SET_BT_NREC, property_value, "-1");
    int btNrecProp = atoi(property_value);
    if (btNrecProp != -1) {
        bEnable = btNrecProp;
        ALOGD("%s(), force set the BT headset NREC = %d", __FUNCTION__, bEnable);
    }
#endif

    ALOGV("%s(), bEnable=%d", __FUNCTION__, bEnable);

    // enable/disable flag
    if (SpeechEnhancementController::GetInstance()->GetBtHeadsetNrecOn() != bEnable) {
        SpeechEnhancementController::GetInstance()->SetBtHeadsetNrecOnToAllModem(bEnable);
    }

    return NO_ERROR;

}

bool AudioALSAStreamManager::GetBtHeadsetNrecStatus(void) {
    bool bEnable = SpeechEnhancementController::GetInstance()->GetBtHeadsetNrecOn();
    ALOGD("-%s(), bEnable=%d", __FUNCTION__, bEnable);

    return bEnable;
}

status_t AudioALSAStreamManager::SetBtCodec(int btCodec) {
    ALOGD("%s(), mBtCodec = %d", __FUNCTION__, btCodec);
    mBtCodec = btCodec;

    return NO_ERROR;

}

int AudioALSAStreamManager::GetBtCodec(void) {

    return mBtCodec;
}

status_t AudioALSAStreamManager::Enable_DualMicSettng(sph_enh_dynamic_mask_t sphMask, bool bEnable) {
    ALOGD("%s(), bEnable=%d", __FUNCTION__, bEnable);

    SpeechEnhancementController::GetInstance()->SetDynamicMaskOnToAllModem(sphMask, bEnable);
    return NO_ERROR;

}

status_t AudioALSAStreamManager::Set_LSPK_DlMNR_Enable(sph_enh_dynamic_mask_t sphMask, bool bEnable) {
    ALOGD("%s(), bEnable=%d", __FUNCTION__, bEnable);

    Enable_DualMicSettng(sphMask, bEnable);

    if (SpeechEnhancementController::GetInstance()->GetMagicConferenceCallOn() == true &&
        SpeechEnhancementController::GetInstance()->GetDynamicMask(sphMask) == true) {
        ALOGE("Cannot open MagicConCall & LoudSpeaker DMNR at the same time!!");
    }
    return NO_ERROR;

}

#ifdef MTK_VOW_SUPPORT
status_t AudioALSAStreamManager::setVoiceWakeUpEnable_l(const bool enable) {
    ALOGD("%s(), enable = %d", __FUNCTION__, enable);
    if (enable == true) {
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
        if (mForceDisableVoiceWakeUpForUsnd == true) {
            ALOGD("%s(), mForceDisableVoiceWakeUpForUsnd = %d, return",
                    __FUNCTION__, mForceDisableVoiceWakeUpForUsnd);
            return NO_ERROR;
        }
#endif
        if (mForceDisableVoiceWakeUpForPhoneCall == true) {
            ALOGD("%s(), mForceDisableVoiceWakeUpForPhoneCall = %d, return", __FUNCTION__, mForceDisableVoiceWakeUpForPhoneCall);
        } else if (mCaptureHandlerVector.size() == 0) {
            if (mAudioALSAVoiceWakeUpController->getVoiceWakeUpEnable() == false) {
                ALOGD("%s(), No capture handler, enable VoW", __FUNCTION__);
                mAudioALSAVoiceWakeUpController->setVoiceWakeUpEnable(true);
            }
        } else {
            ALOGD("%s(), mCaptureHandlerVector.size() = %zu, return",
                    __FUNCTION__, mCaptureHandlerVector.size());
        }
    } else {
        if (mAudioALSAVoiceWakeUpController->getVoiceWakeUpEnable() == true) {
            mAudioALSAVoiceWakeUpController->setVoiceWakeUpEnable(false);
        }
    }
    return NO_ERROR;
}

status_t AudioALSAStreamManager::setVoiceWakeUpEnable(const bool enable) {
    ALOGV("%s(), enable = %d", __FUNCTION__, enable);
    AL_AUTOLOCK(mCaptureHandlerVectorLock);
    return setVoiceWakeUpEnable_l(enable);
}

bool AudioALSAStreamManager::getVoiceWakeUpNeedOn() {
    return mVoiceWakeUpNeedOn;
}

status_t AudioALSAStreamManager::setVoiceWakeUpNeedOn(const bool enable) {
    ALOGD("%s(), mVoiceWakeUpNeedOn: %d => %d ", __FUNCTION__, mVoiceWakeUpNeedOn, enable);
    if (enable == mVoiceWakeUpNeedOn) {
        ALOGW("-%s(), enable(%d) == mVoiceWakeUpNeedOn(%d), return", __FUNCTION__, enable, mVoiceWakeUpNeedOn);
        return INVALID_OPERATION;
    }
    status_t ret = setVoiceWakeUpEnable(enable);
    mVoiceWakeUpNeedOn = enable;
    return ret;
}
#endif

void AudioALSAStreamManager::UpdateDynamicFunctionMask(void) {
    ALOGD("+%s()", __FUNCTION__);
    if (mStreamInVector.size() > 0) {
        for (size_t i = 0; i < mStreamInVector.size(); i++) {
            mStreamInVector[i]->UpdateDynamicFunctionMask();
        }
    }
    ALOGD("-%s()", __FUNCTION__);
}

bool AudioALSAStreamManager::EnableBesRecord(void) {
    bool bRet = false;
    if ((mAudioCustParamClient->QueryFeatureSupportInfo()& SUPPORT_HD_RECORD) > 0) {
        bRet = true;
        ALOGD_IF(mLogEnable, "%s(), %x", __FUNCTION__, bRet);
    }

    return bRet;
}

status_t AudioALSAStreamManager::setScreenState(bool mode) {
    AL_AUTOLOCK(mStreamVectorLock);
    AudioALSAStreamOut *pAudioALSAStreamOut = NULL;
#ifndef MTK_UL_LOW_LATENCY_MODE_DISABLE
    AudioALSAStreamIn *pAudioALSAStreamIn = NULL;
#endif

    for (size_t i = 0; i < mStreamOutVector.size(); i++) {
        pAudioALSAStreamOut = mStreamOutVector[i];
        pAudioALSAStreamOut->setScreenState(mode);
    }

#ifndef MTK_UL_LOW_LATENCY_MODE_DISABLE
    for (size_t i = 0; i < mStreamInVector.size(); i++) {
        pAudioALSAStreamIn = mStreamInVector[i];
        // Update IRQ period when all streamin are Normal Record
        if ((pAudioALSAStreamIn->getStreamInCaptureHandler() != NULL) &&
            (((pAudioALSAStreamIn->getInputFlags() & (AUDIO_INPUT_FLAG_FAST | AUDIO_INPUT_FLAG_MMAP_NOIRQ)) != 0) ||
             (pAudioALSAStreamIn->getStreamInCaptureHandler()->getCaptureHandlerType() != CAPTURE_HANDLER_NORMAL))) {
            //break here because sharing the same dataprovider
            break;
        }
        if (i == (mStreamInVector.size() - 1)) {
            ALOGE("%s, mStreamInVector[%zu]->getInputFlags() = 0x%x\n", __FUNCTION__, i, mStreamInVector[i]->getInputFlags());
            pAudioALSAStreamIn->setLowLatencyMode(mode);
        }

    }
#endif
    return NO_ERROR;
}

status_t AudioALSAStreamManager::setBypassDLProcess(bool flag) {
    AL_AUTOLOCK(mLock);
    AudioALSAStreamOut *pAudioALSAStreamOut = NULL;
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    set_aurisys_on(!(bool)flag);
#endif
    mBypassPostProcessDL = flag;
    return NO_ERROR;
}

status_t AudioALSAStreamManager::EnableSphStrmByDevice(audio_devices_t output_devices) {
    AudioALSASpeechStreamController::getInstance()->SetStreamOutputDevice(output_devices);
    if (isPhoneCallOpen()) {
        if ((output_devices & AUDIO_DEVICE_OUT_SPEAKER) != 0) {
            AudioALSASpeechStreamController::getInstance()->EnableSpeechStreamThread(true);
        }
    }
    return NO_ERROR;
}

status_t AudioALSAStreamManager::DisableSphStrmByDevice(audio_devices_t output_devices) {
    AudioALSASpeechStreamController::getInstance()->SetStreamOutputDevice(output_devices);
    if (isModeInPhoneCallSupportEchoRef(mAudioMode) == true) {
        if (AudioALSASpeechStreamController::getInstance()->IsSpeechStreamThreadEnable() == true) {
            AudioALSASpeechStreamController::getInstance()->EnableSpeechStreamThread(false);
        }
    }
    return NO_ERROR;
}

status_t AudioALSAStreamManager::EnableSphStrm(audio_mode_t new_mode) {
    ALOGD("%s new_mode = %d", __FUNCTION__, new_mode);
    if ((new_mode < AUDIO_MODE_NORMAL) || (new_mode > AUDIO_MODE_MAX)) {
        return BAD_VALUE;
    }

    if (isPhoneCallOpen() == true) {
        if ((AudioALSASpeechStreamController::getInstance()->GetStreamOutputDevice() & AUDIO_DEVICE_OUT_SPEAKER) != 0 &&
            (AudioALSASpeechStreamController::getInstance()->IsSpeechStreamThreadEnable() == false)) {
            AudioALSASpeechStreamController::getInstance()->EnableSpeechStreamThread(true);
        }
    }
    return NO_ERROR;
}

status_t AudioALSAStreamManager::DisableSphStrm(audio_mode_t new_mode) {
    ALOGD("%s new_mode = %d", __FUNCTION__, new_mode);
    if ((new_mode < AUDIO_MODE_NORMAL) || (new_mode > AUDIO_MODE_MAX)) {
        return BAD_VALUE;
    }
    if (new_mode == mAudioMode) {
        ALOGW("-%s(), mAudioMode: %d == %d, return", __FUNCTION__, mAudioMode, new_mode);
        return BAD_VALUE;
    }

    if (isModeInPhoneCallSupportEchoRef(mAudioMode) == true) {
        if (AudioALSASpeechStreamController::getInstance()->IsSpeechStreamThreadEnable() == true) {
            AudioALSASpeechStreamController::getInstance()->EnableSpeechStreamThread(false);
        }
    }
    return NO_ERROR;
}

bool AudioALSAStreamManager::IsSphStrmSupport(void) {
    char property_value[PROPERTY_VALUE_MAX] = {0};
    bool Currentsupport = false;
    property_get("vendor.streamout.speech_stream.enable", property_value, "1");
    int speech_stream = atoi(property_value);
#if defined(MTK_SPEAKER_MONITOR_SPEECH_SUPPORT)
    Currentsupport = true;
#endif
    ALOGD("%s = %d Currentsupport = %d", __FUNCTION__, speech_stream, Currentsupport);
    return (speech_stream & Currentsupport);
}

bool AudioALSAStreamManager::isModeInPhoneCallSupportEchoRef(const audio_mode_t audio_mode) {
    if (audio_mode == AUDIO_MODE_IN_CALL || audio_mode == AUDIO_MODE_CALL_SCREEN) {
        return true;
    } else {
        return false;
    }
}

status_t AudioALSAStreamManager::setParametersToStreamOut(const String8 &keyValuePairs) { // TODO(Harvey
    if (mStreamOutVector.size() == 0) {
        return INVALID_OPERATION;
    }

    AudioALSAStreamOut *pAudioALSAStreamOut = NULL;
    for (size_t i = 0; i < mStreamOutVector.size() ; i++) {
        pAudioALSAStreamOut = mStreamOutVector[i];
        pAudioALSAStreamOut->setParameters(keyValuePairs);
    }

    return NO_ERROR;
}

status_t AudioALSAStreamManager::setParameters(const String8 &keyValuePairs, int IOport) { // TODO(Harvey)
    status_t status = PERMISSION_DENIED;
    ssize_t index = -1;

    ALOGD("+%s(), IOport = %d, keyValuePairs = %s", __FUNCTION__, IOport, keyValuePairs.string());

    index = mStreamOutVector.indexOfKey(IOport);
    if (index >= 0) {
        ALOGV("Send to mStreamOutVector[%zu]", index);
        AudioALSAStreamOut *pAudioALSAStreamOut = mStreamOutVector.valueAt(index);
        audio_output_flags_t streamOutFlag = pAudioALSAStreamOut->getStreamAttribute()->mAudioOutputFlags;
        ALOGD("%s(), Send to mStreamOutVector[%zu], flag %d", __FUNCTION__, index, streamOutFlag);
        status = pAudioALSAStreamOut->setParameters(keyValuePairs);
        ALOGV("-%s()", __FUNCTION__);
        return status;
    }

    index = mStreamInVector.indexOfKey(IOport);
    if (index >= 0) {
        ALOGV("Send to mStreamInVector [%zu]", index);
        AudioALSAStreamIn *pAudioALSAStreamIn = mStreamInVector.valueAt(index);
        status = pAudioALSAStreamIn->setParameters(keyValuePairs);
        ALOGV("-%s()", __FUNCTION__);
        return status;
    }

    ALOGE("-%s(), do nothing, return", __FUNCTION__);
    return status;
}

void AudioALSAStreamManager::updateDeviceConnectionState(audio_devices_t device, bool connect) {
    if ((device & AUDIO_DEVICE_BIT_IN) == false) {
        mAvailableOutputDevices = connect ? mAvailableOutputDevices | device : mAvailableOutputDevices & !device;
    }
}

bool AudioALSAStreamManager::getDeviceConnectionState(audio_devices_t device) {
    if ((device & AUDIO_DEVICE_BIT_IN) == false) {
        return !!(mAvailableOutputDevices & device);
    }
    return false;
}

void AudioALSAStreamManager::setCustScene(const String8 scene) {
    mCustScene = scene;
#if defined(MTK_NEW_VOL_CONTROL)
    AudioMTKGainController::getInstance()->setScene(scene.string());
#endif
    mSpeechPhoneCallController->setCustScene(scene.string());
}

bool AudioALSAStreamManager::isEchoRefUsing() {
    if (mCaptureHandlerVector.size() > 0) {
        for (size_t i = 0; i < mCaptureHandlerVector.size(); i++) {
            if (mCaptureHandlerVector[i] == NULL ||
                mCaptureHandlerVector[i]->getStreamAttributeTarget() == NULL) {
                ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                continue;
            }
            if (mCaptureHandlerVector[i]->getStreamAttributeTarget()->NativePreprocess_Info.PreProcessEffect_AECOn == true) {
                return true;
            }
            switch (mCaptureHandlerVector[i]->getStreamAttributeTarget()->input_source) {
            case AUDIO_SOURCE_VOICE_COMMUNICATION:
            case AUDIO_SOURCE_CUSTOMIZATION1: //MagiASR enable AEC
            case AUDIO_SOURCE_CUSTOMIZATION2: {
                return true;
            }
            default:
                break;
            }
        }
    }
    return false;
}

bool AudioALSAStreamManager::isNormalRecordUsing() {
    if (mCaptureHandlerVector.size() > 0) {
        for (size_t i = 0; i < mCaptureHandlerVector.size(); i++) {
            if (mCaptureHandlerVector[i] == NULL ||
                mCaptureHandlerVector[i]->getStreamAttributeTarget() == NULL) {
                ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                continue;
            }
            switch (mCaptureHandlerVector[i]->getStreamAttributeTarget()->input_source) {
            case AUDIO_SOURCE_MIC:
            case AUDIO_SOURCE_CAMCORDER:
            case AUDIO_SOURCE_VOICE_RECOGNITION: 
            case AUDIO_SOURCE_UNPROCESSED:
            case AUDIO_SOURCE_VOICE_PERFORMANCE: {
                return true;
            }
            default:
                break;
            }
        }
    }
    return false;
}

status_t AudioALSAStreamManager::updateInputSource(int IOport) {
    ssize_t index = -1;
    status_t status = NO_ERROR;
    const stream_attribute_t *pStreamAttrTarget = NULL;
    audio_source_t inputSource = AUDIO_SOURCE_DEFAULT;
    AL_AUTOLOCK(mLock);

    index = mStreamInVector.indexOfKey(IOport);
    if (index >= 0) {
        ALOGD("%s(), Update input source form mStreamInVector [%zu]", __FUNCTION__, index);
        AudioALSAStreamIn *pAudioALSAStreamIn = mStreamInVector.valueAt(index);
        pStreamAttrTarget = pAudioALSAStreamIn->getStreamAttribute();

        if ((pStreamAttrTarget != NULL) &&
            ((pStreamAttrTarget->input_source == AUDIO_SOURCE_VOICE_COMMUNICATION) ||
             (pStreamAttrTarget->input_source == AUDIO_SOURCE_CUSTOMIZATION1) ||
             (pStreamAttrTarget->input_source == AUDIO_SOURCE_CUSTOMIZATION2))) {
            AudioALSAHardwareResourceManager::getInstance()->setHDRRecord(mHDRRecordOn); // resume HDR record status when voip end
            ALOGD("%s(), input source: %d, reopen stream in", __FUNCTION__, pStreamAttrTarget->input_source);
            status = standbyAllInputStreams(false, (capture_handler_t)(CAPTURE_HANDLER_DSP | CAPTURE_HANDLER_NORMAL));
        }
    }
    return status;
}

/*==============================================================================
 *                     ADSP Recovery
 *============================================================================*/

void AudioALSAStreamManager::audioDspStopWrap(void *arg) {
#ifndef MTK_AUDIO_DSP_RECOVERY_SUPPORT
    ALOGD("%s() not support!! arg %p", __FUNCTION__, arg);
#else
    AudioALSAStreamManager *mgr = static_cast<AudioALSAStreamManager *>(arg);

    if (mgr != NULL) {
        mgr->audioDspStatusUpdate(false);
    }
#endif
}


void AudioALSAStreamManager::audioDspReadyWrap(void *arg) {
#ifndef MTK_AUDIO_DSP_RECOVERY_SUPPORT
    ALOGD("%s() not support!! arg %p", __FUNCTION__, arg);
#else
    AudioALSAStreamManager *mgr = static_cast<AudioALSAStreamManager *>(arg);

    if (mgr != NULL) {
        mgr->audioDspStatusUpdate(true);
    }
#endif
}


void AudioALSAStreamManager::audioDspStatusUpdate(const bool adspReady) {
#ifndef MTK_AUDIO_DSP_RECOVERY_SUPPORT
    ALOGD("%s() not support!! adspReady %d", __FUNCTION__, adspReady);
#else
    static AudioLock updateLock;
    AL_AUTOLOCK(updateLock);

    // competetor: stream out write() / stream in read() / alsa hw setMode()
    lock_all_adsp_rlock();

    // (in recovery && not ready) || (not in recovery && ready)
    if (get_audio_dsp_recovery_mode() != adspReady) {
        ALOGE("%s(), recovery %d ready %d. bypass", __FUNCTION__,
              get_audio_dsp_recovery_mode(), adspReady);
        unlock_all_adsp_rlock();
        return;
    }


    if (adspReady == false) { // adsp reboot
        set_audio_dsp_recovery_mode(true);
        standbyAllStreams(false); // stop all in/out streams
        setMode(AUDIO_MODE_NORMAL);
    } else { // adsp ready
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
        wrap_config_to_dsp();
#endif
        // if setMode 2->3
        if (mPhoneCallControllerStatusPolicy == true && isModeInPhoneCall(mAudioModePolicy) == false) {
            setMode(AUDIO_MODE_IN_CALL);
        }
        setMode(mAudioModePolicy);

        if (mPhoneCallControllerStatusPolicy == true) {
            if (audio_is_bluetooth_sco_device(mCurrentOutputDevicesForCall) ||
                audio_is_usb_out_device(mCurrentOutputDevicesForCall) ||
                audio_is_hearing_aid_out_device(mCurrentOutputDevicesForCall)) {
                routingOutputDevice(mOutputStreamForCall,
                                    mCurrentOutputDevicesForCall,
                                    mOutputDevicesForCall);
            } else {
                routingOutputDevicePhoneCall(mOutputDevicesForCall);
            }
            mSpeechPhoneCallController->setMicMute(mMicMute);
        }
        set_audio_dsp_recovery_mode(false);
    }

    unlock_all_adsp_rlock();
#endif
}


void AudioALSAStreamManager::updateAudioModePolicy(const audio_mode_t new_mode) {
    if (new_mode == AUDIO_MODE_IN_CALL || new_mode == AUDIO_MODE_CALL_SCREEN) {
        mPhoneCallControllerStatusPolicy = true;
    } else if (new_mode == AUDIO_MODE_NORMAL) {
        mPhoneCallControllerStatusPolicy = false;
    }

#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
    ALOGD("%s(), mAudioModePolicy: %d => %d, mPhoneCallControllerStatusPolicy: %d",
          __FUNCTION__,
          mAudioModePolicy,
          new_mode,
          mPhoneCallControllerStatusPolicy);
#endif

    mAudioModePolicy = new_mode;
}


bool AudioALSAStreamManager::needEnableVoip(const stream_attribute_t *streamAttribute) {
    bool ret = false;

    if (mAvailableOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        ret = ((streamAttribute->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) != 0);
#if defined(MTK_TC10_FEATURE)
    } else if (streamAttribute->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        ret = isModeInVoipCall();
#else
    } else if (!(streamAttribute->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) {
        ret = isModeInVoipCall();
#endif
    } else {
        ret = false;
    }

    ALOGD("%s(), output_devices = 0x%x, flags: 0x%x, mAvailableOutputFlags: 0x%x, enable: %d",
          __FUNCTION__, streamAttribute->output_devices, streamAttribute->mAudioOutputFlags, mAvailableOutputFlags, ret);

    return ret;
}

String8 AudioALSAStreamManager::getTVOutCapability(const char *capabilityType __unused) {
    String8 returnStr = String8();

#if defined(MTK_AUDIO_TVOUT_SUPPORT)
    unsigned int capabilityValue = 0;
    int ret = 0;
    int fd = 0;
    unsigned int supportedChannel, supportedSampleRate, supportedFormat;
    int i, numEntry = 0;

    fd = AudioALSAHardwareResourceManager::getInstance()->getTVOutFileDescriptor();
    ALOGD("%s(), fd %d, %s", __FUNCTION__, fd, capabilityType);

    ret = ioctl(fd, DRM_IOCTL_MTK_HDMI_GET_CAPABILITY, &capabilityValue);
    if (ret != 0) {
        ALOGE("%s(), ioctrl error, ret %s", __FUNCTION__, strerror(errno));
        ASSERT(0);
    }

    int sampleRateBitArray[MTK_DRM_SAMPLERATE_NUM] = {MTK_DRM_SAMPLERATE_32K_BIT, MTK_DRM_SAMPLERATE_44K_BIT, MTK_DRM_SAMPLERATE_48K_BIT,
                                                      MTK_DRM_SAMPLERATE_96K_BIT, MTK_DRM_SAMPLERATE_192K_BIT
                                                     };
    int formatBitArray[MTK_DRM_BITWIDTH_NUM] = {MTK_DRM_BITWIDTH_16_BIT, MTK_DRM_BITWIDTH_24_BIT};
    int channelBitArray[MTK_DRM_CHANNEL_NUM] = {MTK_DRM_CHANNEL_2_BIT, MTK_DRM_CHANNEL_3_BIT, MTK_DRM_CHANNEL_4_BIT,
                                                MTK_DRM_CHANNEL_5_BIT, MTK_DRM_CHANNEL_6_BIT, MTK_DRM_CHANNEL_7_BIT, MTK_DRM_CHANNEL_8_BIT
                                               };
    char sampleRateStrArray[MTK_DRM_SAMPLERATE_NUM][8] = {AUDIO_SAMPLERATE_32K_STR, AUDIO_SAMPLERATE_44K_STR,
                                                          AUDIO_SAMPLERATE_48K_STR, AUDIO_SAMPLERATE_96K_STR, AUDIO_SAMPLERATE_192K_STR
                                                         };
    char formatStrArray[MTK_DRM_BITWIDTH_NUM][32] = {AUDIO_FORMAT_16_BIT_STR, AUDIO_FORMAT_24_BIT_STR};
    char channelStrArray[MTK_DRM_CHANNEL_NUM][32] = {AUDIO_CHANNEL_STEREO_STR, AUDIO_CHANNEL_3CH_STR, AUDIO_CHANNEL_4CH_STR,
                                                     AUDIO_CHANNEL_5CH_STR, AUDIO_CHANNEL_6CH_STR, AUDIO_CHANNEL_7CH_STR, AUDIO_CHANNEL_8CH_STR
                                                    };

    supportedSampleRate = (capabilityValue >> MTK_DRM_CAPABILITY_SAMPLERATE_SFT) & MTK_DRM_CAPABILITY_SAMPLERATE_MASK;
    supportedFormat = (capabilityValue >> MTK_DRM_CAPABILITY_BITWIDTH_SFT) & MTK_DRM_CAPABILITY_BITWIDTH_MASK;
    supportedChannel = (capabilityValue >> MTK_DRM_CAPABILITY_CHANNEL_SFT) & MTK_DRM_CAPABILITY_CHANNEL_MASK;
    ALOGD("%s(), capabilityValue: 0x%x, sample rate: %d, format: %d, channel: %d",
          __FUNCTION__, capabilityValue, supportedSampleRate, supportedFormat, supportedChannel);

    if (strcmp(capabilityType, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES) == 0) {
        numEntry = 0;
        for (i = 0; i < MTK_DRM_SAMPLERATE_NUM; i++) {
            if (supportedSampleRate & sampleRateBitArray[i]) {
                if (numEntry++ != 0) {
                    returnStr += String8("|");
                }
                returnStr += String8(sampleRateStrArray[i]);
            }
        }
    } else if (strcmp(capabilityType, AUDIO_PARAMETER_STREAM_SUP_FORMATS) == 0) {
        numEntry = 0;
        for (i = 0; i < MTK_DRM_BITWIDTH_NUM; i++) {
            if (supportedFormat & formatBitArray[i]) {
                if (numEntry++ != 0) {
                    returnStr += String8("|");
                }
                returnStr += String8(formatStrArray[i]);
            }
        }
    } else if (strcmp(capabilityType, AUDIO_PARAMETER_STREAM_SUP_CHANNELS) == 0) {
        numEntry = 0;
        for (i = 0; i < MTK_DRM_CHANNEL_NUM; i++) {
            if (supportedChannel & channelBitArray[i]) {
                if (numEntry++ != 0) {
                    returnStr += String8("|");
                }
                returnStr += String8(channelStrArray[i]);
            }
        }
    }
#endif
    return returnStr;
}

status_t AudioALSAStreamManager::setBesLoudnessStateToXML(bool value) {
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    AppOps *appOps = appOpsGetInstance();
    if (appOps != NULL) {
        appOps->utilNativeSetParam(
            SOUND_ENHANCEMENT_AUDIO_TYPE,
            SOUND_ENHANCEMENT_CATEGORY_PATH,
            SOUND_ENHANCEMENT_PARAM_BESLOUDNESS,
            (value) ? "1" : "0");
        // save to file: SoundEnhancement_AudioParam.xml
        appOps->utilNativeSaveXml(SOUND_ENHANCEMENT_AUDIO_TYPE);

        ALOGD("%s(), Set BesLoudness state to xml = %d", __FUNCTION__, value);
    } else {
        ALOGE("%s(), Error: AppOps == NULL", __FUNCTION__);
        return BAD_VALUE;
    }
    return NO_ERROR;
#else
    ALOGW("Doesn't support %s() value %d", __FUNCTION__, value);
    return INVALID_OPERATION;
#endif
}

int AudioALSAStreamManager::getBesLoudnessStateFromXML() {
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    AppOps *appOps = appOpsGetInstance();
    char *paramDataStr = NULL;
    int ret_value = 0;

    if (appOps != NULL) {
        paramDataStr = appOps->utilNativeGetParam(
                           SOUND_ENHANCEMENT_AUDIO_TYPE,
                           SOUND_ENHANCEMENT_CATEGORY_PATH,
                           SOUND_ENHANCEMENT_PARAM_BESLOUDNESS);
        if (paramDataStr) {
            ALOGD("%s(), get BesLoudness state from xml = %s", __FUNCTION__, paramDataStr);
            ret_value = atoi(paramDataStr);
            free(paramDataStr);
        }
    } else {
        ALOGE("%s(), Error: AppOps == NULL", __FUNCTION__);
    }
    return ret_value;
#else
    ALOGW("Doesn't support %s()", __FUNCTION__);
    return 0;
#endif
}

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
void AudioALSAStreamManager::setForceDisableVoiceWakeUpForUsnd(bool disable) {
    AL_AUTOLOCK(mStreamVectorLock);
    mForceDisableVoiceWakeUpForUsnd = disable;
#ifdef MTK_VOW_SUPPORT
    if (mVoiceWakeUpNeedOn == true) {
        setVoiceWakeUpEnable_l(!disable);
    }
#endif
}
#endif

void AudioALSAStreamManager::setHDRRecord(bool enable) {
    ALOGD("%s(), setHDRRecord = %d", __FUNCTION__, enable);
    mHDRRecordOn = enable;
    AudioALSAHardwareResourceManager::getInstance()->setHDRRecord(enable);
    return;
}

} // end of namespace android
