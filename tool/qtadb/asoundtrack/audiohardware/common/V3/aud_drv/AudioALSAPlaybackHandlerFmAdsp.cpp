#include "AudioALSAPlaybackHandlerFmAdsp.h"

#include "WCNChipController.h"

#include "AudioALSADeviceParser.h"
#include "AudioALSADriverUtility.h"
#include "AudioALSAFMController.h"
#include "AudioALSAPlaybackHandlerBase.h"
#include "AudioALSAHardwareResourceManager.h"
#if defined(MTK_AUDIO_KS)
#include "AudioALSADeviceConfigManager.h"
#endif

#include "AudioMTKFilter.h"
#include "AudioALSADeviceParser.h"
#include "AudioALSADriverUtility.h"
#include "AudioALSAStreamManager.h"
#include "AudioUtility.h"


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAPlaybackHandlerFmAdsp"

#include "AudioDspStreamManager.h"
#include <audio_task.h>
#include <audio_dsp_service.h>
#include <AudioMessengerIPI.h>

namespace android {
/*==============================================================================
 *                     Singleton Pattern
 *============================================================================*/

AudioALSAPlaybackHandlerFmAdsp *AudioALSAPlaybackHandlerFmAdsp::mPlaybackHandlerFmAdsp = NULL;
AudioALSAPlaybackHandlerFmAdsp *AudioALSAPlaybackHandlerFmAdsp::getInstance(const stream_attribute_t *stream_attribute_source) {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mPlaybackHandlerFmAdsp == NULL) {
        mPlaybackHandlerFmAdsp = new AudioALSAPlaybackHandlerFmAdsp(stream_attribute_source);
    }
    ASSERT(mPlaybackHandlerFmAdsp != NULL);
    return mPlaybackHandlerFmAdsp;
}

AudioALSAPlaybackHandlerFmAdsp::AudioALSAPlaybackHandlerFmAdsp(const stream_attribute_t *stream_attribute_source) :
    AudioALSAPlaybackHandlerBase(stream_attribute_source),
    mWCNChipController(WCNChipController::GetInstance()),
    mTaskScene(TASK_SCENE_INVALID),
    mFmAdspUlPcm(NULL),
    mFmAdspDlPcm(NULL),
    mFmAdspPcm(NULL),
    mDspHwPcm(NULL),
    mFmAdspUlIndex(0),
    mFmAdspDlIndex(0),
    mFmAdspIndex(0),
    mStreamCardIndex(0),
    mMixer(AudioALSADriverUtility::getInstance()->getMixer()) {
    mPlaybackHandlerType = PLAYBACK_HANDLER_FM_ADSP;
    ALOGD("%s() mPlaybackHandlerType = %d", __FUNCTION__, mPlaybackHandlerType);

    memset(&mFmAdspUlConfig, 0, sizeof(mFmAdspUlConfig));
    memset(&mFmAdspDlConfig, 0, sizeof(mFmAdspDlConfig));
    memset(&mFmAdspConfig, 0, sizeof(mFmAdspConfig));
}


AudioALSAPlaybackHandlerFmAdsp::~AudioALSAPlaybackHandlerFmAdsp() {
    ALOGD("%s()", __FUNCTION__);
}


status_t AudioALSAPlaybackHandlerFmAdsp::open() {
    ALOGD("+%s(), mDevice = 0x%x", __FUNCTION__, mStreamAttributeSource->output_devices);

    mStreamAttributeTarget.audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
    mStreamAttributeTarget.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
    mStreamAttributeTarget.num_channels = popcount(mStreamAttributeTarget.audio_channel_mask);
    mStreamAttributeTarget.sample_rate = 48000;
    mStreamAttributeTarget.output_devices = mStreamAttributeSource->output_devices;
    mStreamAttributeTarget.buffer_size = AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_BUFFER_SIZE_NORMAL);
    mStreamAttributeTarget.mAudioOutputFlags = AUDIO_OUTPUT_FLAG_FM_ADSP;

    ALOGD("%s(), buffer_size(s/t) = %d/%d, sample_rate(s/t) = %d/%d, channels(s/t) = %d/%d, format(s/t) = %d/%d",
          __FUNCTION__,
          mStreamAttributeSource->buffer_size, mStreamAttributeTarget.buffer_size,
          mStreamAttributeSource->sample_rate, mStreamAttributeTarget.sample_rate,
          mStreamAttributeSource->num_channels, mStreamAttributeTarget.num_channels,
          mStreamAttributeSource->audio_format, mStreamAttributeTarget.audio_format);

    mTaskScene = TASK_SCENE_FM_ADSP;

    AudioDspStreamManager::getInstance()->addPlaybackHandler(this);
    OpenPCMDumpDSP(LOG_TAG, mTaskScene);

    startFmAdspTask(mStreamAttributeSource);

    // open codec driver
    mHardwareResourceManager->startOutputDevice(mStreamAttributeSource->output_devices, mStreamAttributeTarget.sample_rate);

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSAPlaybackHandlerFmAdsp::close() {
    ALOGD("+%s(), mDevice = 0x%x", __FUNCTION__, mStreamAttributeSource->output_devices);

    // close codec driver
    // For DAPM sequence when barge-in and playback use the same front-end
    mHardwareResourceManager->stopOutputDeviceWithoutNotify();

    stopFmAdspTask();

    AudioDspStreamManager::getInstance()->removePlaybackHandler(this);
    ClosePCMDumpDSP(mTaskScene);

    // For DAPM sequence when barge-in and playback use the same front-end
    mHardwareResourceManager->notifyOutputDeviceStatusChange(mStreamAttributeSource->output_devices, DEVICE_STATUS_OFF, 0);

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerFmAdsp::routing(const audio_devices_t output_devices __unused) {
    return INVALID_OPERATION;
}

ssize_t AudioALSAPlaybackHandlerFmAdsp::write(const void *buffer, size_t bytes) {
    ALOGV("%s(), buffer = %p, bytes = %zu", __FUNCTION__, buffer, bytes);

    if (mPcm == NULL) {
        ALOGE("%s(), mPcm == NULL, return", __FUNCTION__);
        return bytes;
    }

    return bytes;
}

void AudioALSAPlaybackHandlerFmAdsp::startFmAdspTask(const stream_attribute_t *attribute) {

    int retval = 0;
    struct ipi_msg_t ipi_msg;
    stream_attribute_t attributeFmAdsp;

    ALOGD("%s+()", __FUNCTION__);

    setFmDspShareMem(true);

    memset((void *)&mFmAdspUlConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mFmAdspDlConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mFmAdspConfig, 0, sizeof(struct pcm_config));

    memcpy(&attributeFmAdsp, attribute, sizeof(stream_attribute_t));
    attributeFmAdsp.num_channels = 2;
    //unsigned int multiplier = (attributeFmAdsp.sample_rate / 96000) + 1;

    mFmAdspUlConfig.rate = attributeFmAdsp.sample_rate;
    mFmAdspUlConfig.channels = attributeFmAdsp.num_channels;
    mFmAdspUlConfig.format = PCM_FORMAT_S16_LE;
    mFmAdspUlConfig.period_count = 4;
    mFmAdspUlConfig.period_size = 2048;
    mFmAdspUlConfig.stop_threshold = ~(0U);
    mFmAdspUlConfig.silence_threshold = 0;

    mFmAdspDlConfig.rate = attributeFmAdsp.sample_rate;
    mFmAdspDlConfig.channels = attributeFmAdsp.num_channels;
    mFmAdspDlConfig.format = PCM_FORMAT_S16_LE;
    mFmAdspDlConfig.period_count = 4;
    mFmAdspDlConfig.period_size = 2048;
    mFmAdspDlConfig.start_threshold = (mFmAdspDlConfig.period_count * mFmAdspDlConfig.period_size);
    mFmAdspDlConfig.stop_threshold = ~(0U);
    mFmAdspDlConfig.silence_threshold = 0;

    mFmAdspConfig.rate = attributeFmAdsp.sample_rate;
    mFmAdspConfig.channels = attributeFmAdsp.num_channels;
    mFmAdspConfig.format = PCM_FORMAT_S16_LE;
    mFmAdspConfig.period_count = 4;
    mFmAdspConfig.period_size = 2048;
    mFmAdspConfig.start_threshold = (mFmAdspConfig.period_count * mFmAdspConfig.period_size);
    mFmAdspConfig.stop_threshold = ~(0U);
    mFmAdspConfig.silence_threshold = 0;

    mStreamCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmHostlessFm);
    mFmAdspDlIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmHostlessFm);
    mFmAdspUlIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmHostlessFm);
    mFmAdspIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDspFm);

    setDspRuntimeEn(true);

    mApTurnOnSequence = AUDIO_CTL_HW_GAIN1_TO_CAPTURE6;
    AudioALSAHardwareResourceManager::getInstance()->enableTurnOnSequence(mApTurnOnSequence);

    mAudioMessengerIPI->registerAdspFeature(FM_ADSP_FEATURE_ID);

    mAudioMessengerIPI->registerDmaCbk(
        TASK_SCENE_FM_ADSP,
        0x08000,
        0x10000,
        processDmaMsgWrapper,
        this);

    openDspHwPcm();

    ASSERT(mFmAdspUlPcm == NULL);
    mFmAdspUlPcm = pcm_open(mStreamCardIndex,
                              mFmAdspUlIndex, PCM_IN | PCM_MONOTONIC, &mFmAdspUlConfig);

    ALOGD("%s(), mFmAdspUlConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d",
          __FUNCTION__, mFmAdspUlConfig.channels, mFmAdspUlConfig.rate, mFmAdspUlConfig.period_size,
          mFmAdspUlConfig.period_count, mFmAdspUlConfig.format);

    if (mFmAdspUlPcm == NULL) {
        ALOGE("%s(), mFmAdspUlPcm == NULL!!", __FUNCTION__);
        ASSERT(mFmAdspUlPcm != NULL);
    } else if (pcm_is_ready(mFmAdspUlPcm) == false) {
        ALOGE("%s(), mFmAdspUlPcm pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mFmAdspUlPcm, pcm_get_error(mFmAdspUlPcm));
        pcm_close(mFmAdspUlPcm);
        mFmAdspUlPcm = NULL;
    } else if (pcm_prepare(mFmAdspUlPcm) != 0) {
        ALOGE("%s(), mFmAdspUlPcm pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mFmAdspUlPcm, pcm_get_error(mFmAdspUlPcm));
        pcm_close(mFmAdspUlPcm);
        mFmAdspUlPcm = NULL;
    } else if (pcm_start(mFmAdspUlPcm) != 0) {
        ALOGE("%s(), mFmAdspUlPcm pcm_start(%p) fail due to %s", __FUNCTION__, mFmAdspUlPcm, pcm_get_error(mFmAdspUlPcm));
        pcm_close(mFmAdspUlPcm);
        mFmAdspUlPcm = NULL;
    }

    ASSERT(mFmAdspDlPcm == NULL);
    mFmAdspDlPcm = pcm_open(mStreamCardIndex,
                              mFmAdspDlIndex, PCM_OUT | PCM_MONOTONIC, &mFmAdspDlConfig);

    ALOGD("%s(), mFmAdspDlConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d",
          __FUNCTION__, mFmAdspDlConfig.channels, mFmAdspDlConfig.rate, mFmAdspDlConfig.period_size,
          mFmAdspDlConfig.period_count, mFmAdspDlConfig.format);

    if (mFmAdspDlPcm == NULL) {
        ALOGE("%s(), mFmAdspDlPcm == NULL!!", __FUNCTION__);
        ASSERT(mFmAdspDlPcm != NULL);
    } else if (pcm_is_ready(mFmAdspDlPcm) == false) {
        ALOGE("%s(), mFmAdspDlPcm pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mFmAdspDlPcm, pcm_get_error(mFmAdspDlPcm));
        pcm_close(mFmAdspDlPcm);
        mFmAdspDlPcm = NULL;
    } else if (pcm_prepare(mFmAdspDlPcm) != 0) {
        ALOGE("%s(), mFmAdspDlPcm pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mFmAdspDlPcm, pcm_get_error(mFmAdspDlPcm));
        pcm_close(mFmAdspDlPcm);
        mFmAdspDlPcm = NULL;
    } else if (pcm_start(mFmAdspDlPcm) != 0) {
        ALOGE("%s(), mFmAdspDlPcm pcm_start(%p) fail due to %s", __FUNCTION__, mFmAdspDlPcm, pcm_get_error(mFmAdspDlPcm));
        pcm_close(mFmAdspDlPcm);
        mFmAdspDlPcm = NULL;
    }


    ASSERT(mFmAdspPcm == NULL);
    mFmAdspPcm = pcm_open(mStreamCardIndex,
                              mFmAdspIndex, PCM_OUT | PCM_MONOTONIC, &mFmAdspConfig);

    ALOGD("%s(), mFmAdspConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d",
          __FUNCTION__, mFmAdspConfig.channels, mFmAdspConfig.rate, mFmAdspConfig.period_size,
          mFmAdspConfig.period_count, mFmAdspConfig.format);

    if (mFmAdspPcm == NULL) {
        ALOGE("%s(), mFmAdspPcm == NULL!!", __FUNCTION__);
        ASSERT(mFmAdspPcm != NULL);
    } else if (pcm_is_ready(mFmAdspPcm) == false) {
        ALOGE("%s(), mFmAdspPcm pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mFmAdspPcm, pcm_get_error(mFmAdspPcm));
        pcm_close(mFmAdspPcm);
        mFmAdspPcm = NULL;
    } else if (pcm_prepare(mFmAdspPcm) != 0) {
        ALOGE("%s(), mFmAdspPcm pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mFmAdspPcm, pcm_get_error(mFmAdspPcm));
        pcm_close(mFmAdspPcm);
        mFmAdspPcm = NULL;
    } else if (pcm_start(mFmAdspPcm) != 0) {
        ALOGE("%s(), mFmAdspPcm pcm_start(%p) fail due to %s", __FUNCTION__, mFmAdspPcm, pcm_get_error(mFmAdspPcm));
        pcm_close(mFmAdspPcm);
        AudioDspStreamManager::getInstance()->triggerDsp(TASK_SCENE_FM_ADSP, AUDIO_DSP_TASK_START);
    }

}

void AudioALSAPlaybackHandlerFmAdsp::stopFmAdspTask(void) {
    int retval;
    struct ipi_msg_t ipi_msg;
    ALOGD("%s+()", __FUNCTION__);

    AudioALSADeviceConfigManager::getInstance()->ApplyDeviceTurnoffSequenceByName(AUDIO_CTL_HW_GAIN1_TO_CAPTURE6);

    if (mFmAdspUlPcm != NULL) {
        pcm_stop(mFmAdspUlPcm);
        pcm_close(mFmAdspUlPcm);
        mFmAdspUlPcm = NULL;
    }

    if (mFmAdspDlPcm != NULL) {
        pcm_stop(mFmAdspDlPcm);
        pcm_close(mFmAdspDlPcm);
        mFmAdspDlPcm = NULL;
    }

    if (mFmAdspPcm != NULL) {
        pcm_stop(mFmAdspPcm);
        pcm_close(mFmAdspPcm);
        mFmAdspPcm = NULL;
    }

    closeDspHwPcm();
    setFmDspShareMem(false);
    setDspRuntimeEn(false);

    mAudioMessengerIPI->deregisterDmaCbk(TASK_SCENE_FM_ADSP);
    mAudioMessengerIPI->deregisterAdspFeature(FM_ADSP_FEATURE_ID);

}

void AudioALSAPlaybackHandlerFmAdsp::setFmDspShareMem(bool enable) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_fm_sharemem_scenario"), 0, enable)) {
        ALOGW("%s(), enable sharemem fail", __FUNCTION__);
    }
}

status_t AudioALSAPlaybackHandlerFmAdsp::openDspHwPcm() {

    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture6);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCapture6);

    /* allocate the same with dsp platform drver */
    mFmAdspUlConfig.stop_threshold = ~(0U);
    ALOGD("%s(), mFmAdspUlConfig format: %d, channels: %d, rate: %d, period_size: %d, period_count: %d",
          __FUNCTION__, mFmAdspUlConfig.format, mFmAdspUlConfig.channels, mFmAdspUlConfig.rate, mFmAdspUlConfig.period_size, mFmAdspUlConfig.period_count);

    mDspHwPcm = pcm_open(cardindex, pcmindex, PCM_IN | PCM_MONOTONIC, &mFmAdspUlConfig);
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

status_t AudioALSAPlaybackHandlerFmAdsp::closeDspHwPcm() {
    ALOGD("%s()", __FUNCTION__);

    if (pcm_stop(mDspHwPcm) != 0) {
        ALOGE("%s() pcm_stop hReadThread fail!!", __FUNCTION__);
    }
    pcm_close(mDspHwPcm);

    return 0;
}

int AudioALSAPlaybackHandlerFmAdsp::setDspRuntimeEn(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_fm_runtime_en"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

} // end of namespace android

