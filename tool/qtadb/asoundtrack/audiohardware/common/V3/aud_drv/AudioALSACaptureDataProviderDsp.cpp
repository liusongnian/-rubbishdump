#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "AudioALSACaptureDataProviderDsp"

#include "AudioALSACaptureDataProviderDsp.h"

#include <pthread.h>
#include <sys/prctl.h>

#include "AudioALSADriverUtility.h"
#include "AudioType.h"
#include "AudioSpeechEnhanceInfo.h"
#include "AudioALSASpeechPhoneCallController.h"
#include "AudioALSAStreamManager.h"
#include "AudioALSAHardwareResourceManager.h"
#include "AudioDspStreamManager.h"
#include <audio_task.h>
#include "AudioUtility.h"
#include <AudioMessengerIPI.h>
#include <AudioVolumeInterface.h>
#include <AudioVolumeFactory.h>
#include "AudioSmartPaController.h"
#include "audio_dsp_service.h"

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <aurisys_scenario_dsp.h>
#include <arsi_type.h>
#include <aurisys_config.h>
#include <aurisys_controller.h>
#endif

#if defined(MTK_AUDIO_KS)
#include "AudioALSADeviceConfigManager.h"
#endif

#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif

#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)


#define AUDIO_CHANNEL_IN_3MIC (AUDIO_CHANNEL_IN_LEFT | AUDIO_CHANNEL_IN_RIGHT | AUDIO_CHANNEL_IN_BACK)
#define AUDIO_CHANNEL_IN_4MIC (AUDIO_CHANNEL_IN_LEFT | AUDIO_CHANNEL_IN_RIGHT | AUDIO_CHANNEL_IN_BACK) // ch4 == ch3

#define UPLINK_SET_AMICDCC_BUFFER_TIME_MS 80

namespace android {


/*==============================================================================
 *                     Constant
 *============================================================================*/


static uint32_t kReadBufferSize = 0;
static uint32_t kReadBufferSizeNormal = 0;
static const uint32_t kDCRReadBufferSize = 0x2EE00; //48K\stereo\1s data , calculate 1time/sec

//static FILE *pDCCalFile = NULL;
static bool btempDebug = false;


/*==============================================================================
 *                     Utility
 *============================================================================*/

#define LINEAR_4CH_TO_3CH(linear_buf, data_size, type) \
    ({ \
        uint32_t __channel_size = (data_size >> 2); \
        uint32_t __num_sample = __channel_size / sizeof(type); \
        uint32_t __data_size_3ch = __channel_size * 3; \
        type    *__linear_4ch = (type *)(linear_buf); \
        type    *__linear_3ch = (type *)(linear_buf); \
        uint32_t __idx_sample = 0; \
        for (__idx_sample = 0; __idx_sample < __num_sample; __idx_sample++) { \
            memmove(__linear_3ch, __linear_4ch, 3 * sizeof(type)); \
            __linear_3ch += 3; \
            __linear_4ch += 4; \
        } \
        __data_size_3ch; \
    })


static uint32_t doDownMixFrom4chTo3ch(void *linear_buf, uint32_t data_size, uint32_t audio_format) {
    if (audio_format == AUDIO_FORMAT_PCM_16_BIT) {
        return LINEAR_4CH_TO_3CH(linear_buf, data_size, int16_t);
    } else if (audio_format == AUDIO_FORMAT_PCM_32_BIT ||
               audio_format == AUDIO_FORMAT_PCM_8_24_BIT) {
        return LINEAR_4CH_TO_3CH(linear_buf, data_size, int32_t);
    } else {
        return 0;
    }
}


/*==============================================================================
 *                     Implementation
 *============================================================================*/

AudioALSACaptureDataProviderDsp *AudioALSACaptureDataProviderDsp::mAudioALSACaptureDataProviderDsp = NULL;
AudioALSACaptureDataProviderDsp *AudioALSACaptureDataProviderDsp::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSACaptureDataProviderDsp == NULL) {
        mAudioALSACaptureDataProviderDsp = new AudioALSACaptureDataProviderDsp();
    }
    ASSERT(mAudioALSACaptureDataProviderDsp != NULL);
    return mAudioALSACaptureDataProviderDsp;
}

AudioALSACaptureDataProviderDsp::AudioALSACaptureDataProviderDsp():
    mMixer(AudioALSADriverUtility::getInstance()->getMixer()),
    mDspHwPcm(NULL),
    mDspHwRefPcm(NULL),
    hReadThread(0),
    mCaptureDropSize(0),
    mUpdateInputSource(false),
    mUpdateVolumeIndex(false),
    mAudioALSAVolumeController(AudioVolumeFactory::CreateAudioVolumeController()) {
    ALOGD("%s()", __FUNCTION__);

    mCaptureDataProviderType = CAPTURE_PROVIDER_DSP;
    memset(&mNewtime, 0, sizeof(mNewtime));
    memset(&mOldtime, 0, sizeof(mOldtime));
    memset(timerec, 0, sizeof(timerec));
    memset(&mDsphwConfig, 0, sizeof(mDsphwConfig));
    memset(&mDsphwRefConfig, 0, sizeof(mDsphwRefConfig));
    mPCMDumpFileDsp = NULL;
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    mAurisysLibManager = NULL;
    mAurisysDspConfig = NULL;
#endif
    mUseWavDump = true;
}

AudioALSACaptureDataProviderDsp::~AudioALSACaptureDataProviderDsp() {
    ALOGD("%s()", __FUNCTION__);
}

int AudioALSACaptureDataProviderDsp::setAfeDspRefShareMem(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_ref_sharemem_scenario"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

int AudioALSACaptureDataProviderDsp::setDspRuntimeEn(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_captureul1_runtime_en"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

int AudioALSACaptureDataProviderDsp::setDspRefRuntimeEn(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_captureul1_ref_runtime_en"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    return 0;
}


void AudioALSACaptureDataProviderDsp::setApHwPcm() {

    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCaptureDspUl1);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCaptureDspUl1);
    struct pcm_params *params;
    params = pcm_params_get(cardindex, pcmindex, PCM_IN);
    if (params == NULL) {
        ALOGD("Device does not exist.\n");
    }
    unsigned int buffersizemax = pcm_params_get_max(params, PCM_PARAM_BUFFER_BYTES);
    pcm_params_free(params);

    if (get_aurisys_on()) {
        mConfig.rate = mStreamAttributeTargetDSP.sample_rate;
    } else { // if no aurisys, use AP side SRC
        mStreamAttributeTargetDSP.sample_rate = mStreamAttributeSource.sample_rate;
        mConfig.rate = mStreamAttributeTargetDSP.sample_rate;
    }
#ifdef RECORD_INPUT_24BITS
    mConfig.format = PCM_FORMAT_S24_LE;
#else
    mConfig.format = PCM_FORMAT_S16_LE;
#endif

#ifndef MTK_AURISYS_FRAMEWORK_SUPPORT
     mConfig.channels = 2; // non aurisys... use 2ch
#else
    if (mStreamAttributeSource.input_device == AUDIO_DEVICE_IN_WIRED_HEADSET ||
        mStreamAttributeSource.input_source == AUDIO_SOURCE_UNPROCESSED ||
        AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport() == 1) {
        mConfig.channels = 1;
    } else {
        mConfig.channels = 2;
    }
#endif

    mConfig.period_count = 6;

    if (mlatency == UPLINK_HIFI3_LOW_LATENCY_MS) {
        mConfig.period_count = mConfig.period_count * UPLINK_NORMAL_LATENCY_MS / UPLINK_HIFI3_LOW_LATENCY_MS;
    }
    mStreamAttributeTargetDSP.num_channels = mConfig.channels;
    mStreamAttributeTargetDSP.audio_format = mStreamAttributeSource.audio_format;
    mStreamAttributeTargetDSP.latency = mlatency;

    /* config attribute (will used in client SRC/Enh/... later) */
    switch (mConfig.channels) {
    case 1:
        mStreamAttributeTargetDSP.audio_channel_mask = AUDIO_CHANNEL_IN_MONO;
        break;
    case 2:
        mStreamAttributeTargetDSP.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
        break;
    case 3:
        mStreamAttributeTargetDSP.audio_channel_mask = (audio_channel_mask_t)AUDIO_CHANNEL_IN_3MIC;
        break;
    case 4:
        mStreamAttributeTargetDSP.audio_channel_mask = (audio_channel_mask_t)AUDIO_CHANNEL_IN_4MIC;
        break;
    default:
        mStreamAttributeTargetDSP.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
    }

#ifdef UPLINK_LOW_LATENCY
    mConfig.period_size = getPeriodBufSize(&mStreamAttributeTargetDSP, mlatency) / mConfig.channels / (pcm_format_to_bits(mConfig.format) / 8);  //period size will impact the interrupt interval
#else
    mConfig.period_size = (buffersizemax / mConfig.channels / (pcm_format_to_bits(mConfig.format) / 8) / mConfig.period_count;
#endif

    mStreamAttributeTargetDSP.buffer_size = mConfig.period_size *  mConfig.period_count * mConfig.channels * (pcm_format_to_bits(mConfig.format) / 8);

    mConfig.start_threshold = 0;
    mConfig.stop_threshold = mConfig.period_size * mConfig.period_count;
    mConfig.silence_threshold = 0;
    ALOGD("buffersizemax %d, mConfig format: %d, channels: %d, rate: %d, period_size: %d, period_count: %d, latency: %d,  mStreamAttributeTargetDSP.buffer_size: %u",
           buffersizemax, mConfig.format, mConfig.channels, mConfig.rate, mConfig.period_size, mConfig.period_count, mlatency, mStreamAttributeTargetDSP.buffer_size);

}

void AudioALSACaptureDataProviderDsp::setDspHwRefPcm() {
    /* allocate the same with dsp platform drver */
    memcpy(&mDsphwRefConfig, &mConfig, sizeof(struct pcm_config));

    mDsphwRefConfig.rate = mStreamAttributeSource.sample_rate;
    mDsphwRefConfig.channels = 2;
    mDsphwRefConfig.format = PCM_FORMAT_S24_LE;
    mDsphwRefConfig.period_size = ((audio_bytes_per_sample(AUDIO_FORMAT_PCM_8_24_BIT) * mDsphwRefConfig.channels * mDsphwRefConfig.rate * 20) / 1000) / mDsphwRefConfig.channels / (pcm_format_to_bits(mDsphwRefConfig.format) / 8);
    mDsphwRefConfig.period_count = 6;
    if (mlatency == UPLINK_HIFI3_LOW_LATENCY_MS) {
        mDsphwRefConfig.period_count = mDsphwRefConfig.period_count * UPLINK_NORMAL_LATENCY_MS / UPLINK_HIFI3_LOW_LATENCY_MS;
    }
    mDsphwRefConfig.start_threshold = 0;
    mDsphwRefConfig.stop_threshold = mDsphwRefConfig.period_size * mDsphwRefConfig.period_count;
    mDsphwRefConfig.silence_threshold = 0;

    ALOGD("mDsphwRefConfig format: %d, channels: %d, rate: %d, period_size: %d, period_count: %d, ",
          mDsphwRefConfig.format, mDsphwRefConfig.channels, mDsphwRefConfig.rate, mDsphwRefConfig.period_size, mDsphwRefConfig.period_count);
}

status_t AudioALSACaptureDataProviderDsp::openApHwPcm() {

    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCaptureDspUl1);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCaptureDspUl1);

#if defined(CAPTURE_MMAP) // must be after pcm open
    unsigned int flag = PCM_IN | PCM_MONOTONIC | PCM_MMAP;
    openPcmDriverWithFlag(pcmindex, flag);
#else
    mPcm = pcm_open(cardindex, pcmindex, PCM_IN | PCM_MONOTONIC, &mConfig);
#endif
    ASSERT(mPcm != NULL && pcm_is_ready(mPcm) == true);
    ALOGD("%s(), mPcm = %p", __FUNCTION__, mPcm);

    int prepare_error = pcm_prepare(mPcm);
    if (prepare_error != 0) {
        ASSERT(0);
        pcm_close(mPcm);
        mPcm = NULL;
        return UNKNOWN_ERROR;
    }
    return 0;
}

status_t AudioALSACaptureDataProviderDsp::openDspHwPcm() {

    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture1);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCapture1);

    /* allocate the same with dsp platform drver */
    mDsphwConfig.stop_threshold = ~(0U);
    ALOGD("mDsphwConfig format: %d, channels: %d, rate: %d, period_size: %d, period_count: %d",
          mDsphwConfig.format, mDsphwConfig.channels, mDsphwConfig.rate, mDsphwConfig.period_size, mDsphwConfig.period_count);

    mDspHwPcm = pcm_open(cardindex, pcmindex, PCM_IN | PCM_MONOTONIC, &mDsphwConfig);
    ASSERT(mDspHwPcm != NULL);
    ALOGD("%s(), mDspHwPcm = %p", __FUNCTION__, mDspHwPcm);

    if (pcm_prepare(mDspHwPcm) != 0) {
        ALOGE("%s(), pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mDspHwPcm, pcm_get_error(mDspHwPcm));
        ASSERT(0);
        pcm_close(mDspHwPcm);
        mDspHwPcm = NULL;
        return UNKNOWN_ERROR;
    }

    if (pcm_start(mDspHwPcm) != 0) {
        ASSERT(0);
        pcm_close(mDspHwPcm);
        mDspHwPcm = NULL;
        return UNKNOWN_ERROR;
    }

    return 0;
}


status_t AudioALSACaptureDataProviderDsp::openDspHwRefPcm() {

    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture2);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCapture2);


    mDsphwRefConfig.stop_threshold = ~(0U);

#ifdef MTK_AUDIODSP_SUPPORT
    if (isAdspOptionEnable()) {
        if (AudioSmartPaController::getInstance()->isHwDspSpkProtect(mStreamAttributeSource.output_devices)) {
            int i2sIn = AudioSmartPaController::getInstance()->getI2sInSelect();
            AudioSmartPaController::getInstance()->setI2sHD(true, i2sIn);
            mDspRefTurnOnSequence = i2sIn == AUDIO_I2S0 ?
                                AUDIO_CTL_I2S0_TO_CAPTURE2 : AUDIO_CTL_I2S2_TO_CAPTURE2;
        } else if (AudioSmartPaController::getInstance()->isSwDspSpkProtect(mStreamAttributeSource.output_devices)) {
            if (AudioDspStreamManager::getInstance()->getDspPlaybackEnable() == true) {
                mDspRefTurnOnSequence = AUDIO_CTL_DL_MEMIF_TO_CAPTURE2_SPEAKER_HIFI3;
            } else {
                mDspRefTurnOnSequence = AUDIO_CTL_DL_MEMIF_TO_CAPTURE2; //todo: remove this
            }
        } else if (mStreamAttributeSource.output_devices & AUDIO_DEVICE_OUT_SPEAKER) {
            mDspRefTurnOnSequence = AUDIO_CTL_DL_MEMIF_TO_CAPTURE2_SPEAKER_HIFI3; //SmartPA W/o DSP
        } else {
            if (AudioDspStreamManager::getInstance()->getDspPlaybackEnable() == true) {
                mDspRefTurnOnSequence = AUDIO_CTL_DL_PLAYBACK_TO_CAPTURE2_NON_SPEAKER_HIFI3;
            } else {
                mDspRefTurnOnSequence = AUDIO_CTL_DL_MEMIF_TO_CAPTURE2;
            }
        }
    } else
#endif
    {
        mDspRefTurnOnSequence = AUDIO_CTL_DL_MEMIF_TO_CAPTURE2;
    }
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(mDspRefTurnOnSequence);

    ALOGD("mDsphwRefConfig format: %d, channels: %d, rate: %d, period_size: %d, period_count: %d",
          mDsphwRefConfig.format, mDsphwRefConfig.channels, mDsphwRefConfig.rate, mDsphwRefConfig.period_size, mDsphwRefConfig.period_count);

    mDspHwRefPcm = pcm_open(cardindex, pcmindex, PCM_IN | PCM_MONOTONIC, &mDsphwRefConfig);
    ASSERT(mDspHwRefPcm != NULL);
    ALOGD("%s(), mDspHwRefPcm = %p", __FUNCTION__, mDspHwRefPcm);

    if (pcm_prepare(mDspHwRefPcm) != 0) {
        ALOGE("%s(), pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mDspHwRefPcm, pcm_get_error(mDspHwRefPcm));
        ASSERT(0);
        pcm_close(mDspHwRefPcm);
        mDspHwRefPcm = NULL;
        return UNKNOWN_ERROR;
    }

    if (pcm_start(mDspHwRefPcm) != 0) {
        ASSERT(0);
        pcm_close(mDspHwRefPcm);
        mDspHwRefPcm = NULL;
        return UNKNOWN_ERROR;
    }

    return 0;
}


status_t AudioALSACaptureDataProviderDsp::open() {

    ALOGV("%s(+)", __FUNCTION__);

    ASSERT(mEnable == false);

    unsigned int feature_id = CAPTURE_UL1_FEATURE_ID;

    mAudioMessengerIPI->registerAdspFeature(feature_id);

    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCaptureDspUl1);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCaptureDspUl1);
    bool audiomode = AudioALSAStreamManager::getInstance()->isPhoneCallOpen(); // TODO: should not use AudioALSAStreamManager.......
    bool bHDRRecord = AudioALSAHardwareResourceManager::getInstance()->getHDRRecord();
    struct pcm_config *pDspPcmConfig = AudioDspStreamManager::getInstance()->getCaptureDspConfig();

    ALOGD("%s(+), audiomode=%d, feature_id = %x", __FUNCTION__, audiomode, feature_id);
    {
        AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());
        //debug++
        btempDebug = AudioSpeechEnhanceInfo::getInstance()->GetDebugStatus();
        ALOGD("btempDebug: %d", btempDebug);
        mlatency = UPLINK_NORMAL_LATENCY_MS ; //20ms
#ifdef UPLINK_LOW_LATENCY
        if (HasLowLatencyCapture()) { mlatency = UPLINK_HIFI3_LOW_LATENCY_MS; }
#endif

#if defined(MTK_AUDIO_KS)
        if (AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport() > 2 && mStreamAttributeSource.mVoIPEnable) {
            mApTurnOnSequence = AUDIO_CTL_ADDA_TO_CAPTURE1_4CH;
        } else {
            mApTurnOnSequence = AUDIO_CTL_ADDA_TO_CAPTURE1;
        }

        if (bHDRRecord && mStreamAttributeSource.input_device != AUDIO_DEVICE_IN_WIRED_HEADSET) {
            mApTurnOnSequence = AUDIO_CTL_ADDA_TO_CAPTURE1_4CH;
        }
#ifdef UPLINK_DSP_RAW_FORCE_4CH
        mApTurnOnSequence = AUDIO_CTL_ADDA_TO_CAPTURE1_4CH;
#endif

        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(mApTurnOnSequence);
        enablePmicInputDevice(true);
        // need to set after query pcm_params_get, since shutdown will clear this state
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "record_xrun_assert"), 0, 0)) {
            ALOGW("%s(), record_xrun_assert enable fail", __FUNCTION__);
        }
#else
        mApTurnOnSequence = AUDIO_CTL_ADDA_TO_CAPTURE1;
        AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnonSequenceByName(mApTurnOnSequence);
#endif

        /* Reset frames readed counter */
        mStreamAttributeSource.Time_Info.total_frames_readed = 0;
        mStreamAttributeSource.sample_rate = getInputSampleRate(mStreamAttributeSource.input_device,
                                                                mStreamAttributeSource.output_devices);
#ifdef RECORD_INPUT_24BITS
        mStreamAttributeSource.audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
#else
        mStreamAttributeSource.audio_format = AUDIO_FORMAT_PCM_16_BIT;
#endif

        if (mStreamAttributeSource.input_device == AUDIO_DEVICE_IN_WIRED_HEADSET ||
            mStreamAttributeSource.input_source == AUDIO_SOURCE_UNPROCESSED) {
            mStreamAttributeSource.num_channels = 1;
        } else if (AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport() > 2) {
            mStreamAttributeSource.num_channels = 3;
        } else {
            mStreamAttributeSource.num_channels = 2;
        }

        if (bHDRRecord) {
            if (mStreamAttributeSource.input_device == AUDIO_DEVICE_IN_WIRED_HEADSET) {
                mStreamAttributeSource.num_channels = 2;
            } else {
                mStreamAttributeSource.num_channels = 4;
            }
            ALOGD("%s(), HDR_RECORD on, device: %d, channels: %d", __FUNCTION__,
                  mStreamAttributeSource.input_device, mStreamAttributeSource.num_channels);
        }

        switch (mStreamAttributeSource.num_channels) {
        case 1:
            mStreamAttributeSource.audio_channel_mask = AUDIO_CHANNEL_IN_MONO;
            break;
        case 2:
            mStreamAttributeSource.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
            break;
        case 3:
            mStreamAttributeSource.audio_channel_mask = (audio_channel_mask_t)AUDIO_CHANNEL_IN_3MIC;
            break;
        case 4:
            mStreamAttributeSource.audio_channel_mask = (audio_channel_mask_t)AUDIO_CHANNEL_IN_4MIC;
            break;
        default:
            mStreamAttributeSource.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
        }
        mStreamAttributeSource.latency = mlatency;

        setApHwPcm();
        if ((mStreamAttributeSource.mVoIPEnable == true) || (mStreamAttributeSource.input_source == AUDIO_SOURCE_CUSTOMIZATION2)) {
            setDspRefRuntimeEn(true);
            setAfeDspRefShareMem(true);
            setDspHwRefPcm();
        }
        mAudioMessengerIPI->registerDmaCbk(
            TASK_SCENE_CAPTURE_UL1,
            0x10000,
            0x48000,
            processDmaMsgWrapper,
            this);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
        stream_attribute_t *streamAttributeEchoRef = NULL;
        struct stream_attribute_t mstreamAttributeEchoRef;
        memset(&mstreamAttributeEchoRef, 0, sizeof(struct stream_attribute_t));

        uint32_t aurisys_scenario = 0xFFFFFFFF;
        uint8_t arsi_process_type = ARSI_PROCESS_TYPE_UL_ONLY;
        struct aurisys_gain_config_t gain_config;
        memset(&gain_config, 0, sizeof(gain_config));

        if (mStreamAttributeSource.mVoIPEnable || (mStreamAttributeSource.input_source == AUDIO_SOURCE_CUSTOMIZATION2)) {
            //streamAttributeEchoRef = &mStreamAttributeSource;
            memcpy(&mstreamAttributeEchoRef, &mStreamAttributeTargetDSP, sizeof(struct stream_attribute_t));
            mstreamAttributeEchoRef.num_channels = mDsphwRefConfig.channels;
            mstreamAttributeEchoRef.audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
            mstreamAttributeEchoRef.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
            mstreamAttributeEchoRef.sample_rate = mDsphwRefConfig.rate;
            streamAttributeEchoRef = &mstreamAttributeEchoRef;
            aurisys_scenario = AURISYS_SCENARIO_DSP_VOIP;
            arsi_process_type = ARSI_PROCESS_TYPE_UL_ONLY; //ARSI_PROCESS_TYPE_UL_AND_DL;
        } else if (bHDRRecord) {
            streamAttributeEchoRef = NULL;
            aurisys_scenario = AURISYS_SCENARIO_DSP_HDR_RECORD;
#ifdef UPLINK_LOW_LATENCY
        } else if (HasLowLatencyCapture()) {
            streamAttributeEchoRef = NULL;
            aurisys_scenario = AURISYS_SCENARIO_DSP_RECORD_FAST;
#endif
        } else {
            streamAttributeEchoRef = NULL;
            aurisys_scenario = AURISYS_SCENARIO_DSP_RECORD;
        }

        mAudioALSAVolumeController->SetCaptureGain(mStreamAttributeSource.audio_mode,
                                                   mStreamAttributeSource.input_source,
                                                   mStreamAttributeSource.input_device,
                                                   mStreamAttributeSource.output_devices);
        gain_config.ul_digital_gain = mAudioALSAVolumeController->GetSWMICGain() << 2; // (unit: 0.25 db)
        gain_config.ul_analog_gain = ((mAudioALSAVolumeController->GetULTotalGain() - 192) / 4 + 34
                                      - mAudioALSAVolumeController->GetSWMICGain()) << 2; // (unit: 0.25 db)

        kReadBufferSize = getPeriodBufSize(&mStreamAttributeTargetDSP, mlatency);
        kReadBufferSizeNormal = getPeriodBufSize(&mStreamAttributeTargetDSP, UPLINK_NORMAL_LATENCY_MS);


        AudioDspStreamManager::getInstance()->CreateAurisysLibManager(
            &mAurisysLibManager,
            &mAurisysDspConfig,
            TASK_SCENE_CAPTURE_UL1,
            aurisys_scenario,
            arsi_process_type,
            mStreamAttributeSource.audio_mode,
            &mStreamAttributeSource,
            &mStreamAttributeTargetDSP,
            streamAttributeEchoRef,
            &gain_config);
#endif

        if (mUseWavDump) {
            openWavDump(LOG_TAG);
        } else {
            OpenPCMDump(LOG_TAG);
        }
        OpenPCMDumpDsp(LOG_TAG, TASK_SCENE_CAPTURE_UL1);

        openApHwPcm();
        AudioDspStreamManager::getInstance()->addCaptureDataProvider(this);
        mStreamAttributeSource.buffer_size = pDspPcmConfig->period_size * pDspPcmConfig->period_count * pDspPcmConfig->channels *
                                             (pcm_format_to_bits(pDspPcmConfig->format) / 8);

        if ((mStreamAttributeSource.mVoIPEnable == true) || (mStreamAttributeSource.input_source == AUDIO_SOURCE_CUSTOMIZATION2)) {
            openDspHwRefPcm();
        }

        mStart = false;
        mReadThreadReady = false;
    }

    // create reading thread
    mEnable = true;
    int ret = pthread_create(&hReadThread, NULL, AudioALSACaptureDataProviderDsp::readThread, (void *)this);
    if (ret != 0) {
        ALOGE("%s() create hReadThread fail!!", __FUNCTION__);
        return UNKNOWN_ERROR;
    }

    ALOGD("%s(-)", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderDsp::closeDspHwPcm() {
    ALOGD("%s()", __FUNCTION__);

    if (pcm_stop(mDspHwPcm) != 0) {
        ALOGE("%s() pcm_stop hReadThread fail!!", __FUNCTION__);
    }
    pcm_close(mDspHwPcm);

    return 0;
}

status_t AudioALSACaptureDataProviderDsp::closeDspHwRefPcm() {
    ALOGD("%s()", __FUNCTION__);

    if (pcm_stop(mDspHwRefPcm) != 0) {
        ALOGE("%s() pcm_stop hReadThread fail!!", __FUNCTION__);
    }
    pcm_close(mDspHwRefPcm);


#if defined(MTK_AUDIO_KS)
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mDspRefTurnOnSequence);
#endif

    return 0;
}


status_t AudioALSACaptureDataProviderDsp::close() {
    ALOGD("%s()", __FUNCTION__);

#if defined(MTK_AUDIO_KS)
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "record_xrun_assert"), 0, 0)) {
        ALOGW("%s(), record_xrun_assert disable fail", __FUNCTION__);
    }
#endif

    mEnable = false;
    mUpdateVolumeIndex = false;
    unsigned int feature_id = CAPTURE_UL1_FEATURE_ID;
    pthread_join(hReadThread, NULL);
    ALOGD("pthread_join hReadThread done");

    AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

    if (mUseWavDump) {
        closeWavDump();
    } else {
        ClosePCMDump();
    }
    ClosePCMDumpDsp(TASK_SCENE_CAPTURE_UL1);

    pcm_stop(mPcm);
    pcm_close(mPcm);
    if ((mStreamAttributeSource.mVoIPEnable == true) || (mStreamAttributeSource.input_source == AUDIO_SOURCE_CUSTOMIZATION2)) {
        closeDspHwRefPcm();
        setAfeDspRefShareMem(false);
        setDspRefRuntimeEn(false);
    }
    AudioDspStreamManager::getInstance()->removeCaptureDataProvider(this);
    mPcm = NULL;

#if defined(MTK_AUDIO_KS)
    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(mApTurnOnSequence);
#endif

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    AudioDspStreamManager::getInstance()->DestroyAurisysLibManager(
        &mAurisysLibManager, &mAurisysDspConfig, TASK_SCENE_CAPTURE_UL1);
#endif
    mAudioMessengerIPI->deregisterDmaCbk(TASK_SCENE_CAPTURE_UL1);
    mAudioMessengerIPI->deregisterAdspFeature(feature_id);

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

void *AudioALSACaptureDataProviderDsp::readThread(void *arg) {
    status_t retval = NO_ERROR;
    int ret = 0;
    uint32_t counter = 1;
    struct ipi_msg_t ipi_msg;
    stream_attribute_t *attribute = NULL;
    stream_attribute_t *attributeDSP = NULL; // to calculate latency by buffer size

    AudioALSACaptureDataProviderDsp *pDataProvider = static_cast<AudioALSACaptureDataProviderDsp *>(arg);

    uint32_t open_index = pDataProvider->mOpenIndex;

    char nameset[32];
    if (sprintf(nameset, "%s%d", __FUNCTION__, pDataProvider->mCaptureDataProviderType) < 0) {
        ALOGW("%s(), sprintf failed", __FUNCTION__);
    }
    prctl(PR_SET_NAME, (unsigned long)nameset, 0, 0, 0);
    pDataProvider->setThreadPriority();
    attribute = &(pDataProvider->mStreamAttributeSource);
    attributeDSP = &(pDataProvider->mStreamAttributeTargetDSP);

    ALOGD("+%s(), pid: %d, tid: %d, kReadBufferSize=0x%x, kReadBufferSizeNormal=0x%x, open_index=%d, UPLINK_SET_AMICDCC_BUFFER_TIME_MS = %d, counter=%d ", __FUNCTION__, getpid(), gettid(), kReadBufferSize, kReadBufferSizeNormal, open_index, UPLINK_SET_AMICDCC_BUFFER_TIME_MS, counter);

    pDataProvider->waitPcmStart();

    // read raw data from alsa driver
    char linear_buffer[kReadBufferSizeNormal];
    uint32_t Read_Size = kReadBufferSize;

    pDataProvider->mNewBufSize = kReadBufferSize;

    while (pDataProvider->mEnable == true) {
        if (open_index != pDataProvider->mOpenIndex) {
            ALOGD("%s(), open_index(%d) != mOpenIndex(%d), return", __FUNCTION__, open_index, pDataProvider->mOpenIndex);
            break;
        }
        ASSERT(pDataProvider->mPcm != NULL);

        if (kReadBufferSize != pDataProvider->mNewBufSize) {
            kReadBufferSize = pDataProvider->mNewBufSize;
            pDataProvider->mAudioMessengerIPI->sendIpiMsg(
                &ipi_msg,
                TASK_SCENE_CAPTURE_UL1,
                AUDIO_IPI_LAYER_TO_DSP,
                AUDIO_IPI_MSG_ONLY,
                AUDIO_IPI_MSG_NEED_ACK,
                AUDIO_DSP_TASK_UL_UPDATE_LATENCY,
                pDataProvider->mNewBufSize / audio_bytes_per_sample(attributeDSP->audio_format) / attributeDSP->num_channels / (attributeDSP->sample_rate / 1000),
                0,
                NULL);
        }

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
        if (pDataProvider->mUpdateInputSource || pDataProvider->mUpdateVolumeIndex) {
            ALOGD("%s() UpdateAurisysConfig (mUpdateInputSource: %d, mUpdateVolumeIndex: %d)",
                __FUNCTION__, pDataProvider->mUpdateInputSource, pDataProvider->mUpdateVolumeIndex);

            pDataProvider->mUpdateInputSource = false;
            pDataProvider->mUpdateVolumeIndex = false;

            AudioDspStreamManager::getInstance()->UpdateAurisysConfig(
                pDataProvider->mAurisysLibManager,
                pDataProvider->mAurisysDspConfig,
                pDataProvider->mStreamAttributeSource.audio_mode,
                &pDataProvider->mStreamAttributeSource,
                &pDataProvider->mStreamAttributeTargetDSP);
        }
#endif
        if (btempDebug) {
            clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mNewtime);
            pDataProvider->timerec[0] = calc_time_diff(pDataProvider->mNewtime, pDataProvider->mOldtime);
            pDataProvider->mOldtime = pDataProvider->mNewtime;
        }

        ret = pDataProvider->pcmRead(pDataProvider->mPcm, linear_buffer, kReadBufferSize);
        if (ret != 0) {
            ALOGE("%s(), pcm_read() error, retval = %d", __FUNCTION__, retval);
            clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mOldtime);
            continue;
        }

        //struct timespec tempTimeStamp;
        retval = pDataProvider->GetCaptureTimeStamp(&pDataProvider->mStreamAttributeSource.Time_Info, kReadBufferSize);

        if (btempDebug) {
            clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mNewtime);
            pDataProvider->timerec[1] = calc_time_diff(pDataProvider->mNewtime, pDataProvider->mOldtime);
            pDataProvider->mOldtime = pDataProvider->mNewtime;
        }

#ifdef MTK_LATENCY_DETECT_PULSE
        AudioDetectPulse::doDetectPulse(TAG_CAPTURE_DATA_PROVIDER, PULSE_LEVEL, 0, (void *)linear_buffer,
                                        kReadBufferSize, attribute->audio_format, attribute->num_channels,
                                        attribute->sample_rate);
#endif

        if (pDataProvider->mConfig.channels == 4 && pDataProvider->mPCMDumpFile4ch) {
            AudioDumpPCMData(linear_buffer, kReadBufferSize, pDataProvider->mPCMDumpFile4ch);
            if (pDataProvider->mUseWavDump) {
                pDataProvider->mBytesWavDumpWritten4ch += kReadBufferSize;
            }
        }

        // 4ch to 3ch
        if (pDataProvider->mConfig.channels == 4 &&
            pDataProvider->mStreamAttributeSource.num_channels == 3) {
            Read_Size = doDownMixFrom4chTo3ch(
                            linear_buffer,
                            kReadBufferSize,
                            pDataProvider->mStreamAttributeSource.audio_format);
        } else {
            Read_Size = kReadBufferSize;
        }

        // Adjust AMIC 3DB Corner clock setting
        if (counter <= (UPLINK_SET_AMICDCC_BUFFER_TIME_MS / pDataProvider->mlatency)) {
            if (counter == (UPLINK_SET_AMICDCC_BUFFER_TIME_MS / pDataProvider->mlatency)) {
                pDataProvider->adjustSpike();
            }
            counter++;
        }

        // use ringbuf format to save buffer info
        pDataProvider->mPcmReadBuf.pBufBase = linear_buffer;
        pDataProvider->mPcmReadBuf.bufLen   = Read_Size + 1; // +1: avoid pRead == pWrite
        pDataProvider->mPcmReadBuf.pRead    = linear_buffer;
        pDataProvider->mPcmReadBuf.pWrite   = linear_buffer + Read_Size;
        pDataProvider->provideCaptureDataToAllClients(open_index);

        if (btempDebug) {
            clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mNewtime);
            pDataProvider->timerec[2] = calc_time_diff(pDataProvider->mNewtime, pDataProvider->mOldtime);
            pDataProvider->mOldtime = pDataProvider->mNewtime;

            ALOGD("%s, latency_in_us,%1.6lf,%1.6lf,%1.6lf", __FUNCTION__, pDataProvider->timerec[0], pDataProvider->timerec[1], pDataProvider->timerec[2]);
        }
    }

    ALOGD("-%s(), pid: %d, tid: %d", __FUNCTION__, getpid(), gettid());
    pthread_exit(NULL);
    return NULL;
}

void  AudioALSACaptureDataProviderDsp::adjustSpike() {
    status_t retval = mixer_ctl_set_enum_by_string(mixer_get_ctl_by_name(mAudioALSACaptureDataProviderDsp->mMixer, "Audio_AMIC_DCC_Setting"), "On");
    if (retval != 0) {
        ALOGV("%s(), Can not find Audio_AMIC_DCC_Setting!", __FUNCTION__);
    }
}

status_t AudioALSACaptureDataProviderDsp::updateVolumeIndex(int stream, int device, int index) {
    if (!mEnable || !mAurisysDspConfig) {
        ALOGD("%s(), mAurisysDspConfig =%p, enable = %d, ignore", __FUNCTION__, mAurisysDspConfig, mEnable);
        return NO_ERROR;
    }

    ALOGD("%s(), stream = %d, device = %d, index = %d, current scenario = %d (voip: 8)", __FUNCTION__, stream, device, index, mAurisysDspConfig->manager_config.aurisys_scenario);

    if (mAurisysDspConfig->manager_config.aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP && stream == AUDIO_STREAM_VOICE_CALL) {
        setupCustomInfoStr(mAurisysDspConfig->manager_config.custom_info, MAX_CUSTOM_INFO_LEN, mStreamAttributeSource.mCustScene, index,
            audio_is_bluetooth_sco_device((audio_devices_t)(mAurisysDspConfig->manager_config.task_config.input_device_info.devices)) ? AudioALSAStreamManager::getInstance()->GetBtCodec() : -1);
        mUpdateVolumeIndex = true;
    }

    return NO_ERROR;

}

} // end of namespace android
