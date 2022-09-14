#include "AudioALSACaptureDataClientSyncIO.h"

#include <inttypes.h>
#include <pthread.h>
#include <sys/prctl.h>


#include "AudioUtility.h"

#include "AudioALSACaptureDataProviderBase.h"

#include "AudioVolumeInterface.h"
#include "AudioVolumeFactory.h"


#include <audio_memory_control.h>
#include <audio_lock.h>
#include <audio_ringbuf.h>

#include <audio_fmt_conv_hal.h>


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSACaptureDataClientSyncIO"

#ifdef UPLINK_UNPROCESSED_DROP_CHIP_MS
#define UPLINK_UNPROCESSED_DROP_MS (UPLINK_UNPROCESSED_DROP_CHIP_MS)
#else
#define UPLINK_UNPROCESSED_DROP_MS (260)
#endif

namespace android {

/*==============================================================================
 *                     Constant
 *============================================================================*/

static const uint32_t kRawDataBufferSize = 0x10000; // 64k
static const uint32_t kProcessedBufferSize = 0x10000; // 64k

static const uint32_t kBliSrcOutputBufferSize = 0x20000;  // 128k
static const uint32_t kMaxPcmDriverBufferSize = 0x20000;  // 128k


#define MTK_STREAMIN_VOLUEM_MAX (0x1000)
#define MTK_STREAMIN_VOLUME_VALID_BIT (12)


#define MAX_LOCK_TIME_OUT_MS (500)
#define MAX_WAIT_TIME_OUT_MS (500)

/*==============================================================================
 *                     Utility
 *============================================================================*/

static uint32_t getDropMs(stream_attribute_t *mStreamAttributeTarget) {
    uint32_t dropMs = 0;

    if (audio_is_usb_in_device(mStreamAttributeTarget->input_device)) {
        /* avoid getCapturePosition error !! */
        return 0;
    }

    if ((mStreamAttributeTarget->input_source == AUDIO_SOURCE_UNPROCESSED) &&
        (mStreamAttributeTarget->input_device & AUDIO_DEVICE_IN_BUILTIN_MIC)) {
        return UPLINK_UNPROCESSED_DROP_MS;
    }

    return dropMs;
}


/*==============================================================================
 *                     Implementation
 *============================================================================*/

AudioALSACaptureDataClientSyncIO::AudioALSACaptureDataClientSyncIO(
    AudioALSACaptureDataProviderBase *pCaptureDataProvider,
    stream_attribute_t *stream_attribute_target) :
    mStreamAttributeSource(NULL),
    mStreamAttributeTarget(stream_attribute_target),
    mCaptureDataProvider(pCaptureDataProvider),
    mRawStartFrameCount(0),
    mMicMute(false),
    mMuteTransition(false),
    mEnable(false),
    mRawDataBufLinear(NULL),
    mFmtConvHdl(NULL),
    mDropPopSize(0) {

    // init var
    AudioVolumeInterface *pVolumeController = NULL;
    uint32_t latency = 0;
    uint32_t drop_ms = 0;
    uint32_t size_per_sample = 0;
    uint32_t size_per_frame = 0;
    int ret = 0;


    ALOGD("%s(+)", __FUNCTION__);


    // init raw data buf
    memset((void *)&mRawDataBuf, 0, sizeof(mRawDataBuf));

    AUDIO_ALLOC_CHAR_BUFFER(mRawDataBuf.base, kRawDataBufferSize);
    mRawDataBuf.read = mRawDataBuf.base;
    mRawDataBuf.write = mRawDataBuf.base;
    mRawDataBuf.size = kRawDataBufferSize;
    AUDIO_ALLOC_CHAR_BUFFER(mRawDataBufLinear, kRawDataBufferSize);

    // init processed data buf
    memset((void *)&mProcessedDataBuf, 0, sizeof(mProcessedDataBuf));

    AUDIO_ALLOC_CHAR_BUFFER(mProcessedDataBuf.base, kProcessedBufferSize);
    mProcessedDataBuf.read = mProcessedDataBuf.base;
    mProcessedDataBuf.write = mProcessedDataBuf.base;
    mProcessedDataBuf.size = kProcessedBufferSize;


    // config attribute for input device
    mCaptureDataProvider->configStreamAttribute(mStreamAttributeTarget);

    // get attribute before enable
    mStreamAttributeSource = mCaptureDataProvider->getStreamAttributeSource();

    // enable before attach
    mEnable = true;

    // attach client to capture data provider (after data buf ready)
    mCaptureDataProvider->attach(this);

    // Gain control // TODO: AudioMTKGainController::SetCaptureGain, multi data client !?
    pVolumeController = AudioVolumeFactory::CreateAudioVolumeController();
    AUD_ASSERT(pVolumeController != NULL);
    if (pVolumeController != NULL) {
        pVolumeController->SetCaptureGain(mStreamAttributeTarget->audio_mode,
                                          mStreamAttributeTarget->input_source,
                                          mStreamAttributeTarget->input_device,
                                          mStreamAttributeTarget->output_devices);
    }


    // SRC & bit conversion
    mFmtConvHdl = createFmtConvHdlWrap(mStreamAttributeSource, mStreamAttributeTarget);

    // depop
    latency = mCaptureDataProvider->getLatencyTime();
    drop_ms = getDropMs(stream_attribute_target);
    if (drop_ms) {
        if ((drop_ms % latency) != 0) { // drop data size need to align interrupt rate
            drop_ms = ((drop_ms / latency) + 1) * latency; // cell()
        }
        mDropPopSize = (audio_bytes_per_sample(mStreamAttributeTarget->audio_format) *
                        mStreamAttributeTarget->num_channels *
                        mStreamAttributeTarget->sample_rate *
                        drop_ms) / 1000;
        size_per_sample = audio_bytes_per_sample(mStreamAttributeTarget->audio_format);
        size_per_frame  = mStreamAttributeTarget->num_channels * size_per_sample;
        if ((mDropPopSize % size_per_frame) != 0) { // alignment
            mDropPopSize = ((mDropPopSize / size_per_frame) + 1) * size_per_frame;
        }
    }

    // processThread
    hProcessThread = 0;
    ret = pthread_create(&hProcessThread, NULL,
                         AudioALSACaptureDataClientSyncIO::processThread,
                         (void *)this);
    ASSERT(ret == 0);
    ALOGD("%s(-), drop_ms = %d, latency = %d, mDropPopSize = %d", __FUNCTION__, drop_ms, latency, mDropPopSize);
}


AudioALSACaptureDataClientSyncIO::~AudioALSACaptureDataClientSyncIO() {
    // start to close
    mEnable = false;

    // terminate processThread thread
    pthread_join(hProcessThread, NULL);
    ALOGD("pthread_join hProcessThread done");

    // detach client to capture data provider
    mCaptureDataProvider->detach(this);
    ALOGD("mCaptureDataProvider detach done");


    // close
    AL_LOCK_MS(mRawDataBufLock, MAX_LOCK_TIME_OUT_MS);
    AL_LOCK_MS(mProcessedDataBufLock, MAX_LOCK_TIME_OUT_MS);


    // SRC & bit conversion
    if (mFmtConvHdl) {
        aud_fmt_conv_hal_destroy(mFmtConvHdl);
        mFmtConvHdl = NULL;
    }


    AUDIO_FREE_POINTER(mRawDataBufLinear);
    AUDIO_FREE_POINTER(mRawDataBuf.base);
    AUDIO_FREE_POINTER(mProcessedDataBuf.base);

    AL_UNLOCK(mProcessedDataBufLock);
    AL_UNLOCK(mRawDataBufLock);
}


uint32_t AudioALSACaptureDataClientSyncIO::copyCaptureDataToClient(RingBuf pcm_read_buf) {
    audio_ringbuf_t pcm_read_buf_wrap;
    pcm_read_buf_wrap.base = pcm_read_buf.pBufBase;
    pcm_read_buf_wrap.read = pcm_read_buf.pRead;
    pcm_read_buf_wrap.write = pcm_read_buf.pWrite;
    pcm_read_buf_wrap.size = pcm_read_buf.bufLen;


    AL_LOCK_MS(mRawDataBufLock, MAX_LOCK_TIME_OUT_MS);
    if (mEnable == false) {
        ALOGD("%s(), mEnable is false. return", __FUNCTION__);
        AL_SIGNAL(mRawDataBufLock);
        AL_UNLOCK(mRawDataBufLock);
        return 0;
    }

    audio_ringbuf_copy_from_ringbuf_all(&mRawDataBuf, &pcm_read_buf_wrap);
    AL_SIGNAL(mRawDataBufLock);

    AL_UNLOCK(mRawDataBufLock);
    return 0;
}


void *AudioALSACaptureDataClientSyncIO::processThread(void *arg) {
    char thread_name[128];

    AudioALSACaptureDataClientSyncIO *client = NULL;

    audio_ringbuf_t *raw_ul = NULL;
    audio_ringbuf_t *processed = NULL;

    void *pBufferAfterBitConvertion = NULL;
    uint32_t bytesAfterBitConvertion = 0;

    uint32_t data_count = 0;
    uint32_t free_count = 0;

    int wait_result = 0;


    snprintf(thread_name, 64, "%s_%d_%d", __FUNCTION__, getpid(), gettid());
    prctl(PR_SET_NAME, (unsigned long)thread_name, 0, 0, 0);
    ALOGD("%s created", thread_name);


    client = static_cast<AudioALSACaptureDataClientSyncIO *>(arg);

    raw_ul    = &client->mRawDataBuf;
    processed = &client->mProcessedDataBuf;


    while (client->mEnable == true) {
        ALOGV("%s(+)", __FUNCTION__);

        // get data from raw buffer
        AL_LOCK_MS(client->mRawDataBufLock, MAX_LOCK_TIME_OUT_MS);
        data_count = audio_ringbuf_count(raw_ul);

        // data not reary, wait data
        if (data_count == 0) {
            wait_result = AL_WAIT_MS(client->mRawDataBufLock, MAX_WAIT_TIME_OUT_MS);
            if (wait_result != 0) {
                AL_UNLOCK(client->mRawDataBufLock);
                usleep(100);
                continue;
            }

            if (client->mEnable == false) {
                ALOGV("%s(), record stopped. return", __FUNCTION__);
                AL_UNLOCK(client->mRawDataBufLock);
                break;
            }

            data_count = audio_ringbuf_count(raw_ul);
            ALOGV("data_count %u", data_count);
        }

        // copy data
        audio_ringbuf_copy_to_linear(client->mRawDataBufLinear, raw_ul, data_count);
        AL_UNLOCK(client->mRawDataBufLock);

        // SRC & bit conversion
        aud_fmt_conv_hal_process(
            client->mRawDataBufLinear, data_count,
            &pBufferAfterBitConvertion, &bytesAfterBitConvertion,
            client->mFmtConvHdl);

        // depop
        if (client->mDropPopSize > 0) {
            ALOGV("data_count %u, mDropPopSize %u, %dL", bytesAfterBitConvertion, client->mDropPopSize, __LINE__);

            if (bytesAfterBitConvertion > client->mDropPopSize) {
                pBufferAfterBitConvertion = ((char *)pBufferAfterBitConvertion) + client->mDropPopSize;
                bytesAfterBitConvertion -= client->mDropPopSize;
                client->mDropPopSize = 0;
            } else {
                client->mDropPopSize -= bytesAfterBitConvertion;
                bytesAfterBitConvertion = 0;
            }
        }

        if (bytesAfterBitConvertion == 0) {
            ALOGV("%s(), data_count == 0, %dL", __FUNCTION__, __LINE__);
            continue;
        }

        // copy to processed buf and signal read()
        AL_LOCK_MS(client->mProcessedDataBufLock, MAX_LOCK_TIME_OUT_MS);
        free_count = audio_ringbuf_free_space(processed);
        if (bytesAfterBitConvertion > free_count) {
            AUD_LOG_W("%s(), bytesAfterBitConvertion %u > processed buf free_count %u",
                      __FUNCTION__, bytesAfterBitConvertion, free_count);
            bytesAfterBitConvertion = free_count;
        }
        audio_ringbuf_copy_from_linear(processed, (char *)pBufferAfterBitConvertion, bytesAfterBitConvertion);
        AL_SIGNAL(client->mProcessedDataBufLock);
        AL_UNLOCK(client->mProcessedDataBufLock);

        ALOGV("%s(-)", __FUNCTION__);
    }


    ALOGV("%s terminated", thread_name);
    pthread_exit(NULL);
    return NULL;
}

ssize_t AudioALSACaptureDataClientSyncIO::read(void *buffer, ssize_t bytes) {
    ALOGV("%s(+), bytes: %u", __FUNCTION__, (uint32_t)bytes);

    char *write = (char *)buffer;
    uint32_t data_count = 0;
    uint32_t left_count_to_read = bytes;

    int try_count = 8;
    int wait_result = 0;


    // clean buffer
    memset(buffer, 0, bytes);

    // copy processed data
    do {
        AL_LOCK_MS(mProcessedDataBufLock, MAX_LOCK_TIME_OUT_MS);
        data_count = audio_ringbuf_count(&mProcessedDataBuf);
        if (data_count == 0) {
            // wait for new data
            wait_result = AL_WAIT_MS(mProcessedDataBufLock, MAX_WAIT_TIME_OUT_MS);
            if (wait_result != 0) { // something error, exit
                AL_UNLOCK(mProcessedDataBufLock);
                try_count--;
                break;
            }

            if (mEnable == false) {
                ALOGD("%s(), record stopped. return", __FUNCTION__);
                AL_UNLOCK(mProcessedDataBufLock);
                break;
            }

            data_count = audio_ringbuf_count(&mProcessedDataBuf);
        }

        if (data_count >= left_count_to_read) { // ring buffer is enough, copy & exit
            audio_ringbuf_copy_to_linear(write, &mProcessedDataBuf, left_count_to_read);
            AL_UNLOCK(mProcessedDataBufLock);
            left_count_to_read = 0;
            break;
        }

        audio_ringbuf_copy_to_linear((char *)write, &mProcessedDataBuf, data_count);
        AL_UNLOCK(mProcessedDataBufLock);
        left_count_to_read -= data_count;
        write += data_count;

        try_count--;
    } while (left_count_to_read > 0 && try_count > 0 && mEnable == true);

    if (left_count_to_read > 0) {
        ALOGW("left_count_to_read %d!!", left_count_to_read);
    }

    // apply volume if need
    bytes -= left_count_to_read;

    ALOGV("%s(-), bytes: %u", __FUNCTION__, (uint32_t)bytes);
    return bytes;
}


bool AudioALSACaptureDataClientSyncIO::IsLowLatencyCapture(void) {
    bool low_latency_on = false;
    bool voip_on = mStreamAttributeTarget->BesRecord_Info.besrecord_voip_enable;

#ifdef UPLINK_LOW_LATENCY
    if ((voip_on == false) &&
        (mStreamAttributeTarget->mAudioInputFlags & AUDIO_INPUT_FLAG_FAST)) {
        low_latency_on = true;
    }
#endif

    ALOGD("%s(), low_latency_on: %d, voip_on: %d", __FUNCTION__, low_latency_on, voip_on);
    return low_latency_on;
}


int AudioALSACaptureDataClientSyncIO::getCapturePosition(int64_t *frames, int64_t *time) {
    if (mCaptureDataProvider == NULL || frames == NULL || time == NULL) {
        return -EINVAL;
    }

    /* Convert provider sample rate to streamin sample rate*/
    int ret = mCaptureDataProvider->getCapturePosition(frames, time);
    *frames = (*frames) * mStreamAttributeTarget->sample_rate / mStreamAttributeSource->sample_rate;

    /* Compensate drop ms to frames */
    uint32 dropMs = getDropMs(mStreamAttributeTarget);
    *frames = (*frames) - ((int64_t)dropMs * (int64_t)mStreamAttributeTarget->sample_rate / 1000);

    ALOGV("%s(), frames = %" PRIu64 ", tar sample = %d, src sample = %d", __FUNCTION__, *frames, mStreamAttributeTarget->sample_rate, mStreamAttributeSource->sample_rate);
    return ret;
}


void AudioALSACaptureDataClientSyncIO::AddEchoRefDataProvider(
    AudioALSACaptureDataProviderBase *pCaptureDataProvider,
    stream_attribute_t *stream_attribute_target) {

    if (!pCaptureDataProvider || !stream_attribute_target) {
        ALOGE("%s(), NULL! return", __FUNCTION__);
        return;
    }

    WARNING("INVALID_OPERATION"); /* already added in ctor */
    return;
}


uint32_t AudioALSACaptureDataClientSyncIO::copyEchoRefCaptureDataToClient(RingBuf pcm_read_buf) {

    if (!pcm_read_buf.pBufBase) {
        ALOGE("%s(), NULL! return", __FUNCTION__);
        return -1;
    }

    WARNING("INVALID_OPERATION"); /* not support */
    return -1;
}


bool AudioALSACaptureDataClientSyncIO::IsNeedApplyVolume() {
    bool ret = false;

    /* Only real input CaptureDataprovider need to apply volume for mic mute */
    switch (mCaptureDataProvider->getCaptureDataProviderType()) {
    case CAPTURE_PROVIDER_NORMAL:
    case CAPTURE_PROVIDER_BT_SCO:
    case CAPTURE_PROVIDER_BT_CVSD:
    case CAPTURE_PROVIDER_TDM_RECORD:
    case CAPTURE_PROVIDER_EXTERNAL:
        ret = true;
        break;
    default:
        ret = false;
        break;
    }

    return ret;
}


status_t AudioALSACaptureDataClientSyncIO::ApplyVolume(void *Buffer, uint32_t BufferSize) {
    if (IsNeedApplyVolume() == false) {
        return NO_ERROR;
    }

    // check if need apply mute
    if (mMicMute != mStreamAttributeTarget->micmute) {
        mMicMute =  mStreamAttributeTarget->micmute ;
        mMuteTransition = false;
    }

    if (mMicMute == true) {
        // do ramp down
        if (mMuteTransition == false) {
            uint32_t count = BufferSize >> 1;
            float Volume_inverse = ((float)MTK_STREAMIN_VOLUEM_MAX / (float)count) * -1;
            short *pPcm = (short *)Buffer;
            int ConsumeSample = 0;
            int value = 0;
            while (count) {
                value = *pPcm * (MTK_STREAMIN_VOLUEM_MAX + (Volume_inverse * ConsumeSample));
                *pPcm = clamp16(value >> MTK_STREAMIN_VOLUME_VALID_BIT);
                pPcm++;
                count--;
                ConsumeSample ++;
                //ALOGD("ApplyVolume Volume_inverse = %f ConsumeSample = %d",Volume_inverse,ConsumeSample);
            }
            mMuteTransition = true;
        } else {
            memset(Buffer, 0, BufferSize);
        }
    } else if (mMicMute == false) {
        // do ramp up
        if (mMuteTransition == false) {
            uint32_t count = BufferSize >> 1;
            float Volume_inverse = ((float)MTK_STREAMIN_VOLUEM_MAX / (float)count);
            short *pPcm = (short *)Buffer;
            int ConsumeSample = 0;
            int value = 0;
            while (count) {
                value = *pPcm * (Volume_inverse * ConsumeSample);
                *pPcm = clamp16(value >> MTK_STREAMIN_VOLUME_VALID_BIT);
                pPcm++;
                count--;
                ConsumeSample ++;
                //ALOGD("ApplyVolume Volume_inverse = %f ConsumeSample = %d",Volume_inverse,ConsumeSample);
            }
            mMuteTransition = true;
        }
    }

    return NO_ERROR;
}


} // end of namespace android

