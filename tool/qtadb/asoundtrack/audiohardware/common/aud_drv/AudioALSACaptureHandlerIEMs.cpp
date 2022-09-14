#include <AudioALSACaptureHandlerIEMs.h>

#include <stdint.h>
#include <stdlib.h>

#include <AudioAssert.h>

#include <AudioType.h>

#include <audio_memory_control.h>
#include <audio_fmt_conv_hal.h>

#include <AudioUtility.h>



#include <AudioALSACaptureDataClientIEMs.h>



#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "AudioALSACaptureHandlerIEMs"


namespace android {

AudioALSACaptureHandlerIEMs::AudioALSACaptureHandlerIEMs(stream_attribute_t *stream_attribute_target) :
    AudioALSACaptureHandlerBase(stream_attribute_target),
    mEnable(false),
    mMicMute(false),
    mMuteTransition(false),
    mCaptureDataClientIEMs(AudioALSACaptureDataClientIEMs::getInstance()),
    mStreamAttributeSource(NULL),
    mBufIEMs(NULL),
    mBufIEMsSz(0),
    mFmtConvHdl(NULL) {
    mCaptureHandlerType = CAPTURE_HANDLER_IEM;

    memset(&mIEMsDataBufTmp, 0, sizeof(mIEMsDataBufTmp));
    memset(&mIEMsDataBuf, 0, sizeof(mIEMsDataBuf));
    memset(&mSrcDataBuf, 0, sizeof(mSrcDataBuf));

    mCaptureDataClient = mCaptureDataClientIEMs; // for base operation
}


AudioALSACaptureHandlerIEMs::~AudioALSACaptureHandlerIEMs() {
    mCaptureDataClient = NULL;
}


status_t AudioALSACaptureHandlerIEMs::open() {
    ALOGD("%s(), in_dev 0x%x rate %d fmt %d ch %d flags 0x%x",
          __FUNCTION__,
          mStreamAttributeTarget->input_device,
          mStreamAttributeTarget->sample_rate,
          mStreamAttributeTarget->audio_format,
          mStreamAttributeTarget->num_channels,
          mStreamAttributeTarget->mAudioInputFlags);


    AL_LOCK(mLock);

    if (mEnable) {
        AL_UNLOCK(mLock);
        return 0;
    }

    mStreamAttributeSource = mCaptureDataClientIEMs->getAttrIEMsIn();
    if (!mStreamAttributeSource) {
        AL_UNLOCK(mLock);
        ASSERT(0);
        return -1;
    }
    mFmtConvHdl = createFmtConvHdlWrap(mStreamAttributeSource, mStreamAttributeTarget);

    mEnable = true;
    AL_UNLOCK(mLock);


    mCaptureDataClientIEMs->hookCaptureHandler(wrapReadCbk, this);

    return NO_ERROR;
}


status_t AudioALSACaptureHandlerIEMs::close() {
    // unhook first s.t. IEMs thread stop copy data to handler
    mCaptureDataClientIEMs->unhookCaptureHandler();

    AL_AUTOLOCK(mLock);

    if (!mEnable) {
        return 0;
    }
    mEnable = false;

    ALOGD("%s()", __FUNCTION__);

    AL_LOCK(mIEMsDataBufLock);
    AL_SIGNAL(mIEMsDataBufLock);
    AL_UNLOCK(mIEMsDataBufLock);

    if (mFmtConvHdl) {
        aud_fmt_conv_hal_destroy(mFmtConvHdl);
        mFmtConvHdl = NULL;
    }

    AUDIO_FREE_POINTER(mIEMsDataBuf.base);
    AUDIO_FREE_POINTER(mIEMsDataBufTmp.base);
    AUDIO_FREE_POINTER(mBufIEMs);
    AUDIO_FREE_POINTER(mSrcDataBuf.base);

    return NO_ERROR;
}


status_t AudioALSACaptureHandlerIEMs::routing(const audio_devices_t input_device) {
    (void)input_device;
    return NO_ERROR;
}


int AudioALSACaptureHandlerIEMs::wrapReadCbk(void *buffer,
                                             uint32_t bytes,
                                             void *arg) {
    AudioALSACaptureHandlerIEMs *pCaptureHandler = NULL;

    pCaptureHandler = static_cast<AudioALSACaptureHandlerIEMs *>(arg);
    if (!pCaptureHandler) {
        ALOGE("%s(), pCaptureHandler NULL!!", __FUNCTION__);
        return 0;
    }

    return pCaptureHandler->copyCaptureDataToHandler(buffer, bytes);
}


/* run on IEMs RT thread */
uint32_t AudioALSACaptureHandlerIEMs::copyCaptureDataToHandler(
    void *buffer,
    uint32_t bytes) {
    uint32_t data_count_tmp = 0;

    data_count_tmp = audio_ringbuf_count(&mIEMsDataBufTmp);
    if (AL_TRYLOCK(mIEMsDataBufLock) != 0) { // cannot get lock... copy to tmp
        if (data_count_tmp >= 7 * bytes) { /* too much data in ring buf... drop !! */
            ALOGW("%s(), data_count_tmp %u >= 7 * bytes %u!! drop!!",
                  __FUNCTION__, data_count_tmp, bytes);
            audio_ringbuf_drop_data(&mIEMsDataBufTmp, bytes);
        }
        audio_ringbuf_copy_from_linear(&mIEMsDataBufTmp, (char *)buffer, bytes);
    } else {
        if (data_count_tmp) { // locked, copy from tmp
            audio_ringbuf_copy_from_ringbuf_all(&mIEMsDataBuf, &mIEMsDataBufTmp);
        }
        audio_ringbuf_copy_from_linear(&mIEMsDataBuf, (char *)buffer, bytes);
        AL_SIGNAL(mIEMsDataBufLock);
        AL_UNLOCK(mIEMsDataBufLock);
    }

    return bytes;
}


/* run on stream in normal/fast thread */
ssize_t AudioALSACaptureHandlerIEMs::read(void *buffer, ssize_t bytes) {
    uint32_t read_lens_ms = 0;

    uint32_t data_count = 0;

    int wait_result = 0;
    int try_count = 8;

    void *buf_src = NULL;
    uint32_t buf_src_sz = 0;

    char *write = (char *)buffer;
    uint32_t left_count_to_read = bytes;

    if (bytes <= 0) {
        return 0;
    }

    // clean buffer
    memset(buffer, 0, bytes);

    // wait time
    read_lens_ms = getBufferLatencyMs(mStreamAttributeTarget, bytes);

    if (!mCaptureDataClientIEMs->isEnabled()) {
        usleep(read_lens_ms * 1000);
        return bytes;
    }


    // push clean up handlder for read thread termination
    CLEANUP_PUSH_ALOCK(mIEMsDataBufLock.getAlock());

    // copy processed data
    do {
        // Get from IEMs
        AL_LOCK(mIEMsDataBufLock);
        data_count = audio_ringbuf_count(&mIEMsDataBuf);
        if (data_count == 0) {
            wait_result = AL_WAIT_MS(mIEMsDataBufLock, read_lens_ms);
            if (!mEnable) {
                AL_UNLOCK(mIEMsDataBufLock);
                break;
            }
            data_count = audio_ringbuf_count(&mIEMsDataBuf);
            if (wait_result != 0 || data_count == 0) {
                AL_UNLOCK(mIEMsDataBufLock);
                try_count--;
                usleep(100);
                continue;
            }
        }

        if (data_count > mBufIEMsSz) {
            mBufIEMs = REALLOC_SAFE(mBufIEMs, data_count);
            if (!mBufIEMs) {
                AL_UNLOCK(mIEMsDataBufLock);
                break;
            }
        }
        audio_ringbuf_copy_to_linear((char *)mBufIEMs, &mIEMsDataBuf, data_count);
        AL_UNLOCK(mIEMsDataBufLock);

        // SRC
        aud_fmt_conv_hal_process(
            mBufIEMs, data_count,
            &buf_src, &buf_src_sz,
            mFmtConvHdl);
        audio_ringbuf_copy_from_linear(&mSrcDataBuf, (char *)buf_src, buf_src_sz);
        data_count = audio_ringbuf_count(&mSrcDataBuf);


        if (data_count >= left_count_to_read) { // ring buffer is enough, copy & exit
            audio_ringbuf_copy_to_linear(write, &mSrcDataBuf, left_count_to_read);
            left_count_to_read = 0;
            break;
        }

        audio_ringbuf_copy_to_linear((char *)write, &mSrcDataBuf, data_count);
        left_count_to_read -= data_count;
        write += data_count;

        try_count--;
    } while (left_count_to_read > 0 && try_count > 0 && mEnable == true);

    // pop clean up handlder for read thread termination
    CLEANUP_POP_ALOCK(mIEMsDataBufLock.getAlock());

    if (left_count_to_read > 0) {
        ALOGW("left_count_to_read %d!!", left_count_to_read);
        bytes -= left_count_to_read;
    }

    if (isNeedApplyVolume(mCaptureDataClientIEMs->getDataProviderType())) {
        applyVolume(&mMicMute,
                    mStreamAttributeTarget->micmute,
                    &mMuteTransition,
                    buffer,
                    bytes);
    }

    return bytes;
}


int64_t AudioALSACaptureHandlerIEMs::getRawStartFrameCount() {
    int64_t rawStartFrameCount = 0;

    if (!mCaptureDataClientIEMs || !mStreamAttributeTarget || !mStreamAttributeSource) {
        return 0;
    }
    rawStartFrameCount = mCaptureDataClientIEMs->getRawStartFrameCount();
    rawStartFrameCount = rawStartFrameCount * mStreamAttributeTarget->sample_rate / mStreamAttributeSource->sample_rate;

    return rawStartFrameCount;
}


int AudioALSACaptureHandlerIEMs::getCapturePosition(int64_t *frames, int64_t *time) {
    int ret = 0;

    if (!frames || !time) {
        return -EINVAL;
    }
    if (!mCaptureDataClientIEMs || !mStreamAttributeTarget || !mStreamAttributeSource) {
        return -ENODEV;
    }

    ret = mCaptureDataClientIEMs->getCapturePosition(frames, time);
    *frames = (*frames) * mStreamAttributeTarget->sample_rate / mStreamAttributeSource->sample_rate;

    ALOGV("%s(), frames = %u", __FUNCTION__, (uint32_t)(*frames & 0xFFFFFFFF));
    return ret;
}



} // end of namespace android
