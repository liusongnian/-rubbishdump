#include <AudioALSACaptureDataProviderUsb.h>


#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>

#include <log/log.h>

#include <sys/prctl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <system/audio.h>

#include <cutils/str_parms.h>
#include "AudioSystemLibCUtil.h"

#include <hardware/audio.h>
#include <hardware/hardware.h>

extern "C" {
#include <alsa_device_profile.h>
#include <alsa_device_proxy.h>
#include <alsa_logging.h>
#include <audio_utils/channels.h>
}

#include <AudioALSADriverUtility.h>

#include <IAudioALSACaptureDataClient.h>
#include <AudioALSAStreamManager.h>

#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif


#if defined(PRIMARY_USB)
#include <AudioUSBCenter.h>
#endif




#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "AudioALSACaptureDataProviderUsb"


/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */



/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */



/*
 * =============================================================================
 *                     private global members
 * =============================================================================
 */

#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)
#ifdef DEBUG_TIMESTAMP
#define SHOW_TIMESTAMP(format, args...) ALOGD(format, ##args)
#else
#define SHOW_TIMESTAMP(format, args...)
#endif

namespace android {


/*==============================================================================
 *                     Constant
 *============================================================================*/


static uint32_t kReadBufferSize = 0;
//static FILE *pDCCalFile = NULL;
static bool btempDebug = false;


/*==============================================================================
 *                     Implementation
 *============================================================================*/

AudioALSACaptureDataProviderUsb *AudioALSACaptureDataProviderUsb::mAudioALSACaptureDataProviderUsb = NULL;

AudioALSACaptureDataProviderUsb *AudioALSACaptureDataProviderUsb::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSACaptureDataProviderUsb == NULL) {
        mAudioALSACaptureDataProviderUsb = new AudioALSACaptureDataProviderUsb();
    }
    ASSERT(mAudioALSACaptureDataProviderUsb != NULL);
    return mAudioALSACaptureDataProviderUsb;
}

AudioALSACaptureDataProviderUsb::AudioALSACaptureDataProviderUsb():
    hReadThread(0) {
    memset(&mNewtime, 0, sizeof(mNewtime));
    memset(&mOldtime, 0, sizeof(mOldtime));
    memset(timerec, 0, sizeof(timerec));
    memset((void *)&mCaptureStartTime, 0, sizeof(mCaptureStartTime));
    memset((void *)&mEstimatedBufferTimeStamp, 0, sizeof(mEstimatedBufferTimeStamp));
    memset(&mProxy, 0, sizeof(alsa_device_proxy));
    usbProxy = NULL;
    mCaptureDataProviderType = CAPTURE_PROVIDER_USB;
    mlatency = AudioUSBCenter::getInstance()->getInPeriodUs(true);
    usbVoipMode = false;
    mIsUsbHAL = false;
}

AudioALSACaptureDataProviderUsb::~AudioALSACaptureDataProviderUsb() {
}

void AudioALSACaptureDataProviderUsb::initUsbInfo(stream_attribute_t stream_attribute_source_usb, alsa_device_proxy *proxy, size_t buffer_size, bool dl_on) {
    mIsUsbHAL = true;

    usbProxy = proxy;

    kReadBufferSize = (uint32_t)buffer_size;

    usbVoipMode = false;

    mStreamAttributeSource = stream_attribute_source_usb;
    mStreamAttributeSource.BesRecord_Info.besrecord_voip_enable = false;
    mStreamAttributeSource.mVoIPEnable = false;
    mStreamAttributeSource.audio_mode = AUDIO_MODE_NORMAL;

    mPcmStatus = NO_ERROR;


    bool audiomode = AudioALSAStreamManager::getInstance()->isModeInVoipCall();

    if ((mStreamAttributeSource.input_source == AUDIO_SOURCE_VOICE_COMMUNICATION) || (audiomode == true)) {
        usbVoipMode = true;
        if (dl_on == true) {
            mStreamAttributeSource.BesRecord_Info.besrecord_voip_enable = true;
            mStreamAttributeSource.mVoIPEnable = true;
            mStreamAttributeSource.audio_mode = AUDIO_MODE_IN_COMMUNICATION;
            mStreamAttributeSource.mAudioInputFlags = AUDIO_INPUT_FLAG_NONE;
        } else {
            // LIB Parser error handling
            mStreamAttributeSource.input_source = AUDIO_SOURCE_MIC;
        }
    }

    mlatency = (1000 * proxy->alsa_config.period_size) / proxy->alsa_config.rate;

    ALOGD("%s(), rate %d, format %d, channels %d sz %d, dl_on %d, flag %d, input_source %d, mlatency %u",
          __FUNCTION__,
          mStreamAttributeSource.sample_rate,
          mStreamAttributeSource.audio_format,
          mStreamAttributeSource.num_channels,
          kReadBufferSize,
          dl_on,
          mStreamAttributeSource.mAudioInputFlags,
          mStreamAttributeSource.input_source,
          mlatency);

    return;
}


bool AudioALSACaptureDataProviderUsb::isNeedEchoRefData() {
    ALOGD("%s(), usbVoipMode = %d, mStreamAttributeSource.input_source = %d", __FUNCTION__, usbVoipMode, mStreamAttributeSource.input_source);
    if (usbVoipMode == true) {
        return true;
    }
    return false;
}

int AudioALSACaptureDataProviderUsb::prepareUsb(void) {
#if !defined(PRIMARY_USB)
    return 0;
#else
    uint32_t period_us = 0;
    uint32_t period_count = 0;

    int ret = 0;

    /* latency & period count */
    if (mStreamAttributeSource.isIEMsSource) {
        period_us    = mStreamAttributeSource.periodUs;
        period_count = mStreamAttributeSource.periodCnt;
    } else {
        period_us    = AudioUSBCenter::getInstance()->getInPeriodUs(mStreamAttributeSource.mAudioInputFlags == AUDIO_INPUT_FLAG_FAST);
        period_count = 2;
    }

    /* get usb proxy config */
    ret = AudioUSBCenter::getInstance()->prepareUsb(&mProxy, &mStreamAttributeSource, PCM_IN, period_us, period_count);
    if (ret != 0) {
        return ret;
    }

    kReadBufferSize = mProxy.alsa_config.period_size * getSizePerFrameByAttr(&mStreamAttributeSource);
    mlatency = mStreamAttributeSource.periodUs / 1000;
    mPeriodUs = mStreamAttributeSource.periodUs;

    ALOGD("%s(), mStreamAttributeSource, rate %d, format %d, channels %d, buffer_size %u, period_us %u, kReadBufferSize %u, mlatency %u",
          __FUNCTION__,
          mStreamAttributeSource.sample_rate,
          mStreamAttributeSource.audio_format,
          mStreamAttributeSource.num_channels,
          mStreamAttributeSource.buffer_size,
          mStreamAttributeSource.periodUs,
          kReadBufferSize,
          mlatency);

    return 0;
#endif /* end of PRIMARY_USB */
}


status_t AudioALSACaptureDataProviderUsb::open() {
    ALOGD("%s()", __FUNCTION__);

    mPcmStatus = NO_ERROR;

    ASSERT(mEnable == false);
    mCaptureDataProviderType = CAPTURE_PROVIDER_USB;

#if defined(PRIMARY_USB)
    if (mIsUsbHAL == false) {
        int ret = prepareUsb();
        if (ret != 0) {
            mPcmStatus = BAD_VALUE;
            return ret;
        }

        usbProxy = &mProxy;

        ret = proxy_open(usbProxy);
        if (ret != 0) {
            ALOGD("%s(), proxy_open fail, ret %d", __FUNCTION__, ret);
            mPcmStatus = BAD_VALUE;
            return ret;
        }
    }
#endif


    /* Reset frames readed counter & mCaptureStartTime */
    mStreamAttributeSource.Time_Info.total_frames_readed = 0;
    memset((void *)&mCaptureStartTime, 0, sizeof(mCaptureStartTime));
    memset((void *)&mEstimatedBufferTimeStamp, 0, sizeof(mEstimatedBufferTimeStamp));

    mEnable = true;

    int ret_thread = pthread_create(&hReadThread, NULL, AudioALSACaptureDataProviderUsb::readThread, (void *)this);
    if (ret_thread != 0) {
        ALOGD("%s(),pthread_create fail", __FUNCTION__);
        mEnable = false;
        pthread_join(hReadThread, NULL);
        proxy_close(usbProxy);
        //ASSERT((ret == 0)&&(ret_thread == 0));
        mPcmStatus = BAD_VALUE;
        return mPcmStatus;
    } else {
        mPcmStatus = NO_ERROR;
    }
    mPcm = usbProxy->pcm;
    OpenPCMDump(LOG_TAG);
    return NO_ERROR;
}


status_t AudioALSACaptureDataProviderUsb::close() {
    ALOGD("%s(), kReadBufferSize = %d", __FUNCTION__, kReadBufferSize);
    if (mEnable == true) {
        mEnable = false;
        pthread_join(hReadThread, NULL);
        ALOGD("pthread_join hReadThread done");
        ClosePCMDump();
        mPcm = NULL;

#if defined(PRIMARY_USB)
        if (mIsUsbHAL == false) {
            proxy_close(&mProxy);
        }
#endif
    }
    return NO_ERROR;
}

void *AudioALSACaptureDataProviderUsb::readThread(void *arg) {
    ALOGD("+%s1(), kReadBufferSize = %d", __FUNCTION__, kReadBufferSize);
    int ret = 0;

    status_t retval = NO_ERROR;
    AudioALSACaptureDataProviderUsb *pDataProvider = static_cast<AudioALSACaptureDataProviderUsb *>(arg);

    uint32_t open_index = pDataProvider->mOpenIndex;

    char nameset[32];
    ret = snprintf(nameset, sizeof(nameset), "%s%d", __FUNCTION__, pDataProvider->mCaptureDataProviderType);
    if (ret < 0 || ret >= sizeof(nameset)) {
        ALOGE("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
              nameset, sizeof(nameset), ret);
    }

    prctl(PR_SET_NAME, (unsigned long)nameset, 0, 0, 0);
    audio_sched_setschedule(0, SCHED_RR, sched_get_priority_min(SCHED_RR));
    ALOGD("+%s2(), pid: %d, tid: %d, kReadBufferSize = %d, open_index=%d", __FUNCTION__, getpid(), gettid(), kReadBufferSize, open_index);

    clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mOldtime);

    // read raw data from alsa driver
    char linear_buffer[kReadBufferSize];
    //char linear_buffer_bcv[kReadBufferSize];
    uint32_t Read_Size = kReadBufferSize;
    while (pDataProvider->mEnable == true) {
        if (open_index != pDataProvider->mOpenIndex) {
            ALOGD("%s(), open_index(%d) != mOpenIndex(%d), return", __FUNCTION__, open_index, pDataProvider->mOpenIndex);
            break;
        }

        clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mNewtime);
        pDataProvider->timerec[0] = calc_time_diff(pDataProvider->mNewtime, pDataProvider->mOldtime);
        pDataProvider->mOldtime = pDataProvider->mNewtime;

        int retval = proxy_read(pDataProvider->usbProxy, linear_buffer, kReadBufferSize);
        if (retval != 0) {
            ALOGD("%s(), proxy_read fail", __FUNCTION__);
            usleep(pDataProvider->mStreamAttributeSource.periodUs);
            pDataProvider->mPcmStatus = BAD_VALUE;
            continue;
        }
        pDataProvider->mPcmStatus = NO_ERROR;

#ifdef MTK_LATENCY_DETECT_PULSE
        stream_attribute_t *attribute = &(pDataProvider->mStreamAttributeSource);
        AudioDetectPulse::doDetectPulse(TAG_CAPTURE_DATA_PROVIDER, PULSE_LEVEL, 0, (void *)linear_buffer,
                                        kReadBufferSize, attribute->audio_format, attribute->num_channels,
                                        attribute->sample_rate);
#endif

        clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mNewtime);
        pDataProvider->timerec[1] = calc_time_diff(pDataProvider->mNewtime, pDataProvider->mOldtime);
        pDataProvider->mOldtime = pDataProvider->mNewtime;

        retval = pDataProvider->GetCaptureTimeStamp(&pDataProvider->mStreamAttributeSource.Time_Info, kReadBufferSize);
        if (retval != NO_ERROR) {
            /* Update capture start time if needed */
            pDataProvider->updateStartTimeStamp(pDataProvider->mStreamAttributeSource.Time_Info.timestamp_get);
        }

        //use ringbuf format to save buffer info
        pDataProvider->mPcmReadBuf.pBufBase = linear_buffer;
        pDataProvider->mPcmReadBuf.bufLen   = kReadBufferSize + 1; // +1: avoid pRead == pWrite
        pDataProvider->mPcmReadBuf.pRead    = linear_buffer;
        pDataProvider->mPcmReadBuf.pWrite   = linear_buffer + kReadBufferSize;

        if (retval != NO_ERROR) {
            /* update capture timestamp by start time */
            pDataProvider->updateCaptureTimeStampByStartTime(kReadBufferSize);
        }
        pDataProvider->provideCaptureDataToAllClients(open_index);

        clock_gettime(CLOCK_MONOTONIC, &pDataProvider->mNewtime);
        pDataProvider->timerec[2] = calc_time_diff(pDataProvider->mNewtime, pDataProvider->mOldtime);
        pDataProvider->mOldtime = pDataProvider->mNewtime;

        double logTimeout = (double)pDataProvider->mStreamAttributeSource.periodUs / 1000000;
        logTimeout += 0.002; /* +2ms: avoid log too much */

        double totalTime = pDataProvider->timerec[0] + pDataProvider->timerec[1] + pDataProvider->timerec[2];

        if (totalTime > logTimeout) {
            ALOGW("%s, latency_in_s,%1.6lf,%1.6lf,%1.6lf, totalTime %1.6lf > logTimeout %1.6lf TIMEOUT!!", __FUNCTION__, pDataProvider->timerec[0], pDataProvider->timerec[1], pDataProvider->timerec[2], totalTime, logTimeout);
        } else if (pDataProvider->mPCMDumpFile || btempDebug) {
            ALOGD("%s, latency_in_s,%1.6lf,%1.6lf,%1.6lf, totalTime %1.6lf, logTimeout %1.6lf", __FUNCTION__, pDataProvider->timerec[0], pDataProvider->timerec[1], pDataProvider->timerec[2], totalTime, logTimeout);
        }
    }

    ALOGD("-%s(), pid: %d, tid: %d", __FUNCTION__, getpid(), gettid());
    pthread_exit(NULL);
    return NULL;


}

status_t AudioALSACaptureDataProviderUsb::updateStartTimeStamp(struct timespec timeStamp) {
    if (mCaptureStartTime.tv_sec == 0 && mCaptureStartTime.tv_nsec == 0) {
        mCaptureStartTime = timeStamp;

        ALOGD("%s(), set start timestamp = %ld.%09ld",
              __FUNCTION__, mCaptureStartTime.tv_sec, mCaptureStartTime.tv_nsec);

        return NO_ERROR;
    }

    return INVALID_OPERATION;
}

status_t AudioALSACaptureDataProviderUsb::updateCaptureTimeStampByStartTime(uint32_t readBytes) {
    ALOGV("%s()", __FUNCTION__);

    if (mCaptureStartTime.tv_sec == 0 && mCaptureStartTime.tv_nsec == 0) {
        ALOGW("No valid mCaptureStartTime! Don't update timestamp info.");
        return BAD_VALUE;
    }

    /* Update timeInfo structure */
    uint32_t bytesPerSample = audio_bytes_per_sample(mStreamAttributeSource.audio_format);
    if (bytesPerSample == 0) {
        ALOGW("audio_format is invalid! (%d)", mStreamAttributeSource.audio_format);
        return BAD_VALUE;
    }
    uint32_t readFrames = readBytes / bytesPerSample / mStreamAttributeSource.num_channels;
    time_info_struct_t *timeInfo = &mStreamAttributeSource.Time_Info;

    timeInfo->frameInfo_get = 0;    // Already counted in mCaptureStartTime
    timeInfo->buffer_per_time = 0;  // Already counted in mCaptureStartTime
    timeInfo->kernelbuffer_ns = 0;
    calculateTimeStampByFrames(mCaptureStartTime, timeInfo->total_frames_readed, mStreamAttributeSource, &timeInfo->timestamp_get);

    /* Update total_frames_readed after timestamp calculation */
    timeInfo->total_frames_readed += readFrames;

    ALOGV("%s(), read size = %d, readFrames = %d (bytesPerSample = %d, ch = %d, new total_frames_readed = %d), timestamp = %ld.%09ld -> %ld.%09ld",
          __FUNCTION__,
          readBytes, readFrames, bytesPerSample, mStreamAttributeSource.num_channels, timeInfo->total_frames_readed,
          mCaptureStartTime.tv_sec, mCaptureStartTime.tv_nsec,
          timeInfo->timestamp_get.tv_sec, timeInfo->timestamp_get.tv_nsec);

    /* Write time stamp to cache to avoid getCapturePosition performance issue */
    AL_LOCK(mTimeStampLock);
    mCaptureFramesReaded = timeInfo->total_frames_readed;
    mCaptureTimeStamp = timeInfo->timestamp_get;
    AL_UNLOCK(mTimeStampLock);

    return NO_ERROR;
}

} // end of namespace android
