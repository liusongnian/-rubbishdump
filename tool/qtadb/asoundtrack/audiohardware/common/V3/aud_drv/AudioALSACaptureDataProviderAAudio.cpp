#include "AudioUtility.h"
#include "AudioALSACaptureDataProviderAAudio.h"
#include "AudioALSADriverUtility.h"
#include "AudioALSAHardwareResourceManager.h"

#if defined(MTK_AUDIO_KS)
#include "AudioALSADeviceConfigManager.h"
#endif

#include <audio_utils/clock.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "AudioALSACaptureDataProviderAAudio"


namespace android {


/*==============================================================================
 *                     Constant
 *============================================================================*/



/*==============================================================================
 *                     Utility
 *============================================================================*/



/*==============================================================================
 *                     Implementation
 *============================================================================*/

int AudioALSACaptureDataProviderAAudio::mUsageCount = 0;
AudioLock AudioALSACaptureDataProviderAAudio::mLock;


AudioALSACaptureDataProviderAAudio *AudioALSACaptureDataProviderAAudio::mAudioALSACaptureDataProviderAAudio = NULL;
AudioALSACaptureDataProviderAAudio *AudioALSACaptureDataProviderAAudio::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSACaptureDataProviderAAudio == NULL) {
        ALOGD("%s, new instance", __func__);
        mAudioALSACaptureDataProviderAAudio = new AudioALSACaptureDataProviderAAudio();
    }
    ASSERT(mAudioALSACaptureDataProviderAAudio != NULL);
    return mAudioALSACaptureDataProviderAAudio;
}


AudioALSACaptureDataProviderAAudio *AudioALSACaptureDataProviderAAudio::requestInstance() {

    ALOGD("%s, mAudioALSACaptureDataProviderAAudio %p, mUsageCount %d", __func__,
          mAudioALSACaptureDataProviderAAudio, mUsageCount);

    AL_AUTOLOCK(mLock);
    mUsageCount += 1;
    AL_UNLOCK(mLock);

    return getInstance();
}


void AudioALSACaptureDataProviderAAudio::freeInstance() {
    static AudioLock mFreeInstanceLock;
    AL_AUTOLOCK(mFreeInstanceLock);

    AL_AUTOLOCK(mLock);
    mUsageCount -= 1;
    AL_UNLOCK(mLock);

    ALOGD("%s, mAudioALSACaptureDataProviderAAudio %p, mUsageCount %d", __func__,
          mAudioALSACaptureDataProviderAAudio, mUsageCount);
    if (mAudioALSACaptureDataProviderAAudio != NULL && mUsageCount == 0) {
        ALOGD("%s, delete instance", __func__);
        delete mAudioALSACaptureDataProviderAAudio;
        mAudioALSACaptureDataProviderAAudio = NULL;
    }
}


AudioALSACaptureDataProviderAAudio::AudioALSACaptureDataProviderAAudio():
    mMixer(AudioALSADriverUtility::getInstance()->getMixer()),
    mTime_nanoseconds(0),
    mPosition_frames(0),
    mMin_size_frames(0),
    mPmicEnable(false),
    isHwSrcNeed(false),
    mPcmHwGainAAudioIn(NULL),
    mPcmSrcAAudioIn(NULL),
    mPcmSrcAAudioOut(NULL) {
    ALOGD("%s()", __FUNCTION__);

    memset((void *)&mCreateMmapTime, 0, sizeof(mCreateMmapTime));
    memset((void *)&mHostlessConfig, 0, sizeof(mHostlessConfig));

    mCaptureDataProviderType = CAPTURE_PROVIDER_NORMAL;
}


AudioALSACaptureDataProviderAAudio::~AudioALSACaptureDataProviderAAudio() {
    ALOGD("%s()", __FUNCTION__);
}

status_t AudioALSACaptureDataProviderAAudio::openPcmDriverWithFlag(const unsigned int device, unsigned int flag,
                                                                   struct pcm **mPcmIn, struct pcm_config *mConfigIn) {
    ALOGD("+%s(), pcm device = %d", __FUNCTION__, device);

    ASSERT(*mPcmIn == NULL);
    mPcmflag = flag;
    *mPcmIn = pcm_open(AudioALSADeviceParser::getInstance()->GetCardIndex(), device, flag, mConfigIn);
    if (*mPcmIn == NULL) {
        ALOGE("%s(), mPcm == NULL!!", __FUNCTION__);
    } else if (pcm_is_ready(*mPcmIn) == false) {
        ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, *mPcmIn, pcm_get_error(*mPcmIn));
        pcm_close(*mPcmIn);
        *mPcmIn = NULL;
    }

    if (flag & PCM_MMAP) {
        audio_pcm_read_wrapper_fp = pcm_mmap_read;
    } else {
        audio_pcm_read_wrapper_fp = pcm_read;
    }

    ALOGD("-%s(), mPcm = %p", __FUNCTION__, *mPcmIn);
    ASSERT(*mPcmIn != NULL);
    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderAAudio::open() {

    ALOGV("%s(+), source format %d, rate %d, ch %d", __FUNCTION__,
          mStreamAttributeSource.audio_format,
          mStreamAttributeSource.sample_rate,
          mStreamAttributeSource.num_channels);

    ASSERT(mEnable == false);

    int cardindex = 0;
    int pcmindex = 0;
    int hwGainAAudioPcmIdx = 0;
    int srcAAudioPcmIdx = 0;

#if defined(MTK_AUDIO_KS)
    pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture7);
    cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCapture7);
    hwGainAAudioPcmIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmHostlessHwGainAAudio);
    srcAAudioPcmIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmHostlessSrcAAudio);
#else
    ALOGE("%s(), MMAP only support KS!", __FUNCTION__);
    ASSERT(0);
#endif

    mConfig.channels = mStreamAttributeSource.num_channels;
    mConfig.rate = mStreamAttributeSource.sample_rate;
    mConfig.format = (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_16_BIT) ?
                     PCM_FORMAT_S16_LE : PCM_FORMAT_S32_LE;

    int bytesPerSample = (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_16_BIT) ? 2 : 4;

    // period_size & period_count
    int max_framecount = MAX_MMAP_HW_BUFFER_SIZE / mConfig.channels / bytesPerSample;
    int min_framecount = MMAP_UL_PERIOD_SIZE * MIN_MMAP_UL_PERIOD_COUNT;

    if (mMin_size_frames < min_framecount) {
        mMin_size_frames = min_framecount;
    } else if (mMin_size_frames > MAX_MMAP_FRAME_COUNT) {
        mMin_size_frames = MAX_MMAP_FRAME_COUNT;
    }

    mConfig.period_size = MMAP_UL_PERIOD_SIZE;
    mConfig.period_count = (mMin_size_frames - 1) / MMAP_UL_PERIOD_SIZE + 1;
    mMin_size_frames = mConfig.period_count * mConfig.period_size;

    int scenario = (mMin_size_frames <= max_framecount) ? 1 : 0;
    ALOGD("%s(), set mmap_record_scenario %d", __FUNCTION__, scenario);
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "mmap_record_scenario"), 0, scenario)) {
        ALOGW("%s(), mmap_record_scenario enable fail", __FUNCTION__);
    }

    mConfig.start_threshold = 0;
    mConfig.stop_threshold = INT32_MAX;
    mConfig.silence_threshold = 0;

    mStreamAttributeSource.buffer_size = mConfig.period_size * mConfig.period_count * mConfig.channels *
                                         bytesPerSample;

    memcpy(&mHostlessConfig, &mConfig, sizeof(struct pcm_config));
    mHostlessConfig.rate = getInputSampleRate(mStreamAttributeSource.input_device,
                                              mStreamAttributeSource.output_devices);
    mHostlessConfig.channels = 2;

#if defined(MTK_AUDIO_KS)
        isHwSrcNeed = (mHostlessConfig.rate != mConfig.rate) && isHwSrcSupport();
        mApTurnOnSeqHwGain = AUDIO_CTL_ADDA_TO_HW_GAIN2;
        if (isHwSrcNeed) {
            mApTurnOnSeqHwSrc = AUDIO_CTL_HW_GAIN2_TO_HW_SRC2;
            mApTurnOnSequence = AUDIO_CTL_HW_SRC2_TO_CAPTURE7;
        } else {
            mApTurnOnSequence = AUDIO_CTL_HW_GAIN2_TO_CAPTURE7;
        }
        AudioALSAHardwareResourceManager::getInstance()->enableTurnOnSequence(mApTurnOnSeqHwGain);
        AudioALSAHardwareResourceManager::getInstance()->enableTurnOnSequence(mApTurnOnSeqHwSrc);
        AudioALSAHardwareResourceManager::getInstance()->enableTurnOnSequence(mApTurnOnSequence);

#if !defined(CONFIG_MT_ENG_BUILD)
    // need to set after query pcm_params_get, since shutdown will clear this state
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "record_xrun_assert"), 0, 1)) {
        ALOGW("%s(), record_xrun_assert enable fail", __FUNCTION__);
    }
#endif
#endif

    ALOGD("mConfig format: %d channels: %d rate: %d period_size: %d period_count: %d latency: %d mMin_size_frames %d hostless rate: %d channels: %d, isHwSrcNeed %d",
          mConfig.format, mConfig.channels, mConfig.rate,
          mConfig.period_size, mConfig.period_count, mlatency, mMin_size_frames,
          mHostlessConfig.rate, mHostlessConfig.channels, isHwSrcNeed);

    unsigned int flag = PCM_IN | PCM_MONOTONIC | PCM_MMAP | PCM_NOIRQ;
    // enable pcm
    if (mPcm == NULL) {
        openPcmDriverWithFlag(pcmindex, flag, &mPcm, &mConfig);
    }

    if (mPcmHwGainAAudioIn == NULL) {
        openPcmDriverWithFlag(hwGainAAudioPcmIdx, PCM_IN, &mPcmHwGainAAudioIn, &mHostlessConfig);
    }

    if (isHwSrcNeed) {
        if (mPcmSrcAAudioIn == NULL) {
            openPcmDriverWithFlag(srcAAudioPcmIdx, PCM_IN, &mPcmSrcAAudioIn, &mHostlessConfig);
        }

        if (mPcmSrcAAudioOut == NULL) {
            openPcmDriverWithFlag(srcAAudioPcmIdx, PCM_OUT, &mPcmSrcAAudioOut, &mConfig);
        }
    }

    preparePcmDriver(&mPcm);
    preparePcmDriver(&mPcmHwGainAAudioIn);
    if (isHwSrcNeed) {
        preparePcmDriver(&mPcmSrcAAudioIn);
        preparePcmDriver(&mPcmSrcAAudioOut);
    }
    mStart = false;

    ALOGV("%s(-)", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSACaptureDataProviderAAudio::close() {
    ALOGD("%s()", __FUNCTION__);

#if defined(MTK_AUDIO_KS) && !defined(CONFIG_MT_ENG_BUILD)
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "record_xrun_assert"), 0, 0)) {
        ALOGW("%s(), record_xrun_assert disable fail", __FUNCTION__);
    }
#endif

    mEnable = false;

    AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

    pcm_stop(mPcm);
    pcm_close(mPcm);
    mPcm = NULL;

    pcm_stop(mPcmHwGainAAudioIn);
    pcm_close(mPcmHwGainAAudioIn);
    mPcmHwGainAAudioIn = NULL;

    if (isHwSrcNeed) {
        pcm_stop(mPcmSrcAAudioIn);
        pcm_close(mPcmSrcAAudioIn);
        mPcmSrcAAudioIn = NULL;

        pcm_stop(mPcmSrcAAudioOut);
        pcm_close(mPcmSrcAAudioOut);
        mPcmSrcAAudioOut = NULL;

        isHwSrcNeed = false;
    }

#if defined(MTK_AUDIO_KS)
    AudioALSAHardwareResourceManager::getInstance()->disableTurnOnSequence(mApTurnOnSequence);
    AudioALSAHardwareResourceManager::getInstance()->disableTurnOnSequence(mApTurnOnSeqHwSrc);
    AudioALSAHardwareResourceManager::getInstance()->disableTurnOnSequence(mApTurnOnSeqHwGain);

    ALOGD("%s(), set mmap_record_scenario 0", __FUNCTION__);
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "mmap_record_scenario"), 0, 0)) {
        ALOGW("%s(), mmap_record_scenario disable fail", __FUNCTION__);
    }
#endif

    if (mPmicEnable) {
        enablePmicInputDevice(false);
        mPmicEnable = false;
    }

    ALOGV("-%s()", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSACaptureDataProviderAAudio::start() {
    ALOGD("+%s", __FUNCTION__);

    int ret = INVALID_OPERATION;

    if (mPcm == NULL) {
        ALOGW("%s, mPcm == NULL !", __FUNCTION__);
        return ret;
    }

    if (!mPmicEnable) {
        // set pmic before pcm prepare
        enablePmicInputDevice(true);
        mPmicEnable = true;
    }


    // drop glitch
    uint64_t sleepUs = 0;
    uint64_t sleepUsMax = 30 * 1000; // 30ms
    struct timespec     curTime;

    clock_gettime(CLOCK_MONOTONIC, &curTime);
    sleepUs = get_time_diff_ns(&mCreateMmapTime, &curTime) / 1000;

    if (sleepUs < sleepUsMax) {
        sleepUs = sleepUsMax - sleepUs;
        ALOGD("%s, drop glitch %ld ms", __FUNCTION__, (long)sleepUs);
        usleep(sleepUs);
    }

    mTime_nanoseconds = 0;

    ret = pcm_start(mPcm);
    if (ret < 0) {
        ALOGE("%s: mPcm pcm_start fail %d, %s",
              __FUNCTION__, ret, pcm_get_error(mPcm));
    }

    ret = pcm_start(mPcmHwGainAAudioIn);
    if (ret < 0) {
        ALOGE("%s: mPcmHwGainAAudioIn pcm_start fail %d, %s",
              __FUNCTION__, ret, pcm_get_error(mPcmHwGainAAudioIn));
    }

    if (isHwSrcNeed) {
        ret = pcm_start(mPcmSrcAAudioIn);
        if (ret < 0) {
            ALOGE("%s: mPcmSrcAAudioIn pcm_start fail %d, %s",
                  __FUNCTION__, ret, pcm_get_error(mPcmSrcAAudioIn));
        }

        ret = pcm_start(mPcmSrcAAudioOut);
        if (ret < 0) {
            ALOGE("%s: mPcmSrcAAudioOut pcm_start fail %d, %s",
                  __FUNCTION__, ret, pcm_get_error(mPcmSrcAAudioOut));
        }
    }

    ALOGV("-%s", __FUNCTION__);
    return ret;
}


status_t AudioALSACaptureDataProviderAAudio::stop() {
    int ret = INVALID_OPERATION;
    ALOGD("+%s", __FUNCTION__);

    if (mPcm == NULL) {
        ALOGW("%s, mPcm == NULL !", __FUNCTION__);
        return ret;
    }

    ret = pcm_stop(mPcm);
    if (ret < 0) {
        ALOGE("%s: mPcm pcm_stop fail %d", __FUNCTION__, ret);
    }

    ret = pcm_stop(mPcmHwGainAAudioIn);
    if (ret < 0) {
        ALOGE("%s: mPcmHwGainAAudioIn pcm_stop fail %d", __FUNCTION__, ret);
    }

    if (isHwSrcNeed) {
        ret = pcm_stop(mPcmSrcAAudioIn);
        if (ret < 0) {
            ALOGE("%s: mPcmSrcAAudioIn pcm_stop fail %d", __FUNCTION__, ret);
        }

        ret = pcm_stop(mPcmSrcAAudioOut);
        if (ret < 0) {
            ALOGE("%s: mPcmSrcAAudioOut pcm_stop fail %d", __FUNCTION__, ret);
        }
    }

    if (mPmicEnable) {
        enablePmicInputDevice(false);
        mPmicEnable = false;
    }

    ALOGV("-%s", __FUNCTION__);
    return ret;
}


status_t AudioALSACaptureDataProviderAAudio::createMmapBuffer(int32_t min_size_frames,
                                                              struct audio_mmap_buffer_info *info) {
    unsigned int offset = 0;
    unsigned int frames = 0;
    uint32_t buffer_size = 0;
    int ret = INVALID_OPERATION;
    ALOGD("+%s, min_size_frames %d", __FUNCTION__, min_size_frames);

    mMin_size_frames = min_size_frames;

    // open pcm
    open();

    if (mPcm == NULL) {
        ALOGW("%s, mPcm == NULL !", __FUNCTION__);
        return ret;
    }
    // drop glitch
    clock_gettime(CLOCK_MONOTONIC, &mCreateMmapTime);


    ret = pcm_mmap_begin(mPcm, &info->shared_memory_address, &offset, &frames);
    if (ret < 0)  {
        goto exit;
    }

    info->buffer_size_frames = pcm_get_buffer_size(mPcm);
    info->burst_size_frames = MMAP_UL_PERIOD_SIZE;
    buffer_size = pcm_frames_to_bytes(mPcm, info->buffer_size_frames);

    info->shared_memory_fd = mixer_ctl_get_value(mixer_get_ctl_by_name(mMixer, "aaudio_ul_mmap_fd"), 0);
    if (info->shared_memory_fd == 0) {
        // share mode
        info->shared_memory_fd = pcm_get_poll_fd(mPcm);
        ALOGD("%s+, shared fd %d", __FUNCTION__, info->shared_memory_fd);
    } else {
        info->buffer_size_frames *= -1;
    }

    memset(info->shared_memory_address, 0, buffer_size);

    mTime_nanoseconds = 0;

    ALOGD("%s: fd %d, buffer address %p,  buffer_size_frames %d %d, burst_size_frames %d", __FUNCTION__,
          info->shared_memory_fd, info->shared_memory_address, info->buffer_size_frames,
          buffer_size, info->burst_size_frames);

exit:
    if (ret != 0) {
        if (mPcm != NULL) {
            pcm_close(mPcm);
            mPcm = NULL;
        }
    }

    ALOGD("-%s", __FUNCTION__);
    return ret;
}


status_t AudioALSACaptureDataProviderAAudio::getMmapPosition(struct audio_mmap_position *position) {
    int ret = INVALID_OPERATION;

    if (mPcm == NULL) {
        ALOGW("%s, mPcm == NULL !", __FUNCTION__);
        return ret;
    }

    struct timespec ts = { 0, 0 };
    ret = pcm_mmap_get_hw_ptr(mPcm, (unsigned int *)&position->position_frames, &ts);
    if (ret < 0) {
        ALOGE("%s: %s", __FUNCTION__, pcm_get_error(mPcm));
        return ret;
    }
    position->time_nanoseconds = audio_utils_ns_from_timespec(&ts);

    if (position->position_frames > MMAP_UL_POSITION_OFFSET) {
        position->position_frames -= MMAP_UL_POSITION_OFFSET;
    }
    else {
        if (position->position_frames < 0) {
            ALOGD("%s, time_nanoseconds %lld, mPosition_frames %d", __FUNCTION__,
                  (long long)position->time_nanoseconds, position->position_frames);
        }
        else {
            position->position_frames = 0;
        }
    }


#if 0
    // correction
    if (mTime_nanoseconds == 0) {
        mTime_nanoseconds = position->time_nanoseconds;
        mPosition_frames = position->position_frames;
        ALOGD("%s, assign time_nanoseconds %lld, mPosition_frames %d", __func__, (long long)mTime_nanoseconds, mPosition_frames);
    } else {
        position->position_frames = (position->time_nanoseconds - mTime_nanoseconds) * 48 / 1000000 + mPosition_frames;
    }
#endif


    return ret;
}


status_t AudioALSACaptureDataProviderAAudio::updateInputDevice(const audio_devices_t input_device) {
    ALOGD("+%s, input_device: 0x%x => 0x%x", __FUNCTION__, mStreamAttributeSource.input_device, input_device);

    mStreamAttributeSource.input_device = input_device;

    ALOGV("-%s, input_device: 0x%x", __FUNCTION__, mStreamAttributeSource.input_device);
    return NO_ERROR;
}


} // end of namespace android
