#include "AudioALSAPlaybackHandlerMixer.h"

#include <inttypes.h>

#include <audio_sw_mixer.h>

#include <AudioALSAStreamManager.h>
#include <AudioALSASampleRateController.h>
#include <AudioALSAHardwareResourceManager.h>
#include "AudioALSADriverUtility.h"

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <audio_ringbuf.h>
#include <audio_pool_buf_handler.h>

#include <aurisys_controller.h>
#include <aurisys_lib_manager.h>
#endif

#if defined(PRIMARY_USB)
#include <AudioUSBCenter.h>
#endif


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAPlaybackHandlerMixer"


#define HAL_PLAYBACK_MIXER_ID (AUD_SW_MIXER_TYPE_PLAYBACK)


namespace android {

struct MixerTarget { /* one device value, one MixerTarget */
    uint32_t attachCnt;

    /* host target (ex, when 1 spk or 1 bt or 1 rcv ...) */
    AudioALSAPlaybackHandlerBase *playbackHdl;
    void *targetHdl;
    stream_attribute_t attributeTarget;

    /* second target (for earphone of dual device) */
    AudioALSAPlaybackHandlerBase *playbackHdlSec;
    void *targetHdlSec;
    stream_attribute_t attributeTargetSec;
};


std::unordered_map<audio_devices_t, struct MixerTarget *> AudioALSAPlaybackHandlerMixer::mMixerTargetList;
AudioLock AudioALSAPlaybackHandlerMixer::mMixerTargetListLock;

std::unordered_map<audio_output_flags_t, stream_attribute_t> AudioALSAPlaybackHandlerMixer::mOutputBufferAttr;

AudioALSAPlaybackHandlerMixer::AudioALSAPlaybackHandlerMixer(const stream_attribute_t *stream_attribute_source) :
    AudioALSAPlaybackHandlerBase(stream_attribute_source),
    mSourceHdl(NULL),
    mIsFirstSource(false),
    mTotalWriteFrames(0),
    mMixerTarget(NULL),
    mTargetPresentedFramesQueued(0),
    mTargetPresentedFramesOffset(0),
    mIsTargetPresentedFramesOffsetValid(false),
    mIsPreviousFramesValid(false) {
    mPlaybackHandlerType = PLAYBACK_HANDLER_MIXER;
}


AudioALSAPlaybackHandlerMixer::~AudioALSAPlaybackHandlerMixer() {

}


void AudioALSAPlaybackHandlerMixer::openMixerSourceHandler(void) {
    struct sw_mixer_attr_t attrSource;

    if (mStreamAttributeSource->isIEMsSource) {
        attrSource.host_order = AUD_SW_MIXER_PRIOR_IEM;
    } else if (mStreamAttributeSource->sample_rate > 48000) {
        attrSource.host_order = AUD_SW_MIXER_PRIOR_HIFI;
    } else if (isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags)) {
        attrSource.host_order = AUD_SW_MIXER_PRIOR_DEEP;
    } else if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        attrSource.host_order = AUD_SW_MIXER_PRIOR_VOIP;
    } else if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST) {
        attrSource.host_order = AUD_SW_MIXER_PRIOR_FAST;
    } else {
        attrSource.host_order = AUD_SW_MIXER_PRIOR_PRIMARY;
    }

    /* use target attr due to the data is after aurisys processing */
    attrSource.buffer_size = mStreamAttributeTarget.buffer_size;
    attrSource.fmt_cfg.audio_format = mStreamAttributeTarget.audio_format;
    attrSource.fmt_cfg.num_channels = mStreamAttributeTarget.num_channels;
    attrSource.fmt_cfg.sample_rate = mStreamAttributeTarget.sample_rate;

    mSourceHdl = sw_mixer_source_attach(HAL_PLAYBACK_MIXER_ID, &attrSource);
}


void AudioALSAPlaybackHandlerMixer::closeMixerSourceHandler(void) {
    sw_mixer_source_detach(mSourceHdl);
    mSourceHdl = NULL;
}


void AudioALSAPlaybackHandlerMixer::configPrimaryAttribute(stream_attribute_t *attr) {
    if (!attr) {
        AUD_ASSERT(0);
        return;
    }

    attr->isMixerOut = true;

    /* config primary device as speaker for dual device */
    if (attr->dualDevice != AUDIO_DEVICE_NONE) {
        attr->output_devices = AUDIO_DEVICE_OUT_SPEAKER;

        attr->isBypassAurisys = false; /* apply effect for spk when ringtone */

        attr->sample_rate = 48000;
        attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_PRIMARY;
        attr->isIEMsSource = false;

        attr->buffer_size = getOutputBufferSize(mStreamAttributeSource, attr);
        attr->frame_count = getFramesByAttr(attr);
        attr->periodUs    = getBufferLatencyUs(attr, attr->buffer_size);

        return;
    }



    /* config single device spk / bt / usb / ... */
    attr->isBypassAurisys = true;

    if (!attr->isIEMsSource) {
        attr->sample_rate = chooseTargetSampleRate(
                                AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate(),
                                attr->output_devices);
    }

    if (attr->isIEMsSource) {
        /* IEMs */
        attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_FAST;
    } else if (audio_is_bluetooth_out_sco_device(attr->output_devices)) {
        /* BT */
        attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_PRIMARY;
        attr->sample_rate = 48000;
    } else if (attr->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST) {
        /* FAST */
        attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_FAST;
    } else if (isIsolatedDeepBuffer(attr->mAudioOutputFlags)) {
        /* DEEP */
        attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
    } else if (mStreamAttributeSource->sample_rate > 48000) {
        /* HiFi playback */
        if (audio_is_usb_out_device(attr->output_devices)) {
            attr->mAudioOutputFlags = (audio_output_flags_t)0; /* dynamic out */
        } else {
            attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
        }
    } else if (attr->sample_rate > 48000 &&
               !audio_is_usb_out_device(attr->output_devices)) { /* usb always HiFi for other flags */
        attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_DEEP_BUFFER;
    } else {
        attr->mAudioOutputFlags = mStreamAttributeSource->mAudioOutputFlags;
    }

    attr->buffer_size = getOutputBufferSize(mStreamAttributeSource, attr);
    attr->frame_count = getFramesByAttr(attr);
    attr->periodUs    = getBufferLatencyUs(attr, attr->buffer_size);
}


void AudioALSAPlaybackHandlerMixer::configEarphoneRingtoneAttribute(stream_attribute_t *attr) {
    if (!attr) {
        AUD_ASSERT(0);
        return;
    }

    attr->isMixerOut = true;

    attr->output_devices = (audio_devices_t)(attr->output_devices & (~AUDIO_DEVICE_OUT_SPEAKER));

    if (audio_is_usb_out_device(attr->output_devices)) {
        attr->isBypassAurisys = true; /* bypass effect for usb when ringtone */
    }

    attr->sample_rate = 48000;
    attr->mAudioOutputFlags = AUDIO_OUTPUT_FLAG_PRIMARY;
    attr->isIEMsSource = false;

    attr->buffer_size = getOutputBufferSize(mStreamAttributeSource, attr);
    attr->frame_count = getFramesByAttr(attr);
    attr->periodUs    = getBufferLatencyUs(attr, attr->buffer_size);
}


void AudioALSAPlaybackHandlerMixer::lunchMixerTargetHandler(
    AudioALSAPlaybackHandlerBase **playbackHdl,
    void **targetHdl,
    stream_attribute_t *attr,
    const uint32_t mixer_id,
    const uint32_t host_order) {
    struct sw_mixer_attr_t attrTarget;

    if (!playbackHdl || !targetHdl || !attr) {
        AUD_ASSERT(0);
        return;
    }

    *playbackHdl = AudioALSAStreamManager::getInstance()->createPlaybackHandler(attr);
    (*playbackHdl)->open();
    (*playbackHdl)->setFirstDataWriteFlag(true);

    attrTarget.host_order = host_order;
    attrTarget.buffer_size = attr->buffer_size;
    attrTarget.fmt_cfg.audio_format = attr->audio_format;
    attrTarget.fmt_cfg.num_channels = attr->num_channels;
    attrTarget.fmt_cfg.sample_rate = attr->sample_rate;

    *targetHdl = sw_mixer_target_attach(
                     mixer_id,
                     &attrTarget,
                     wrapSwMixerWrite,
                     *playbackHdl);
}


void AudioALSAPlaybackHandlerMixer::openMixerTargetHandler(void) {
    uint32_t host_order = 0;

    AL_LOCK(mMixerTargetListLock);

    if (mMixerTargetList.find(mStreamAttributeTarget.output_devices) != mMixerTargetList.end()) {
        mMixerTarget = mMixerTargetList[mStreamAttributeTarget.output_devices];
        mIsFirstSource = false;
    } else {
        AUDIO_ALLOC_STRUCT(struct MixerTarget, mMixerTarget);

        if (mMixerTarget == NULL) {
            ALOGE("%s(), mMixerTarget == NULL, return", __FUNCTION__);
            ASSERT(0);
            AL_UNLOCK(mMixerTargetListLock);
            return;
        }

        mMixerTargetList.insert({mStreamAttributeTarget.output_devices, mMixerTarget});
        mIsFirstSource = true;

        /* target playback handler config */
        memcpy(&mMixerTarget->attributeTarget, &mStreamAttributeTarget, sizeof(mMixerTarget->attributeTarget));
        configPrimaryAttribute(&mMixerTarget->attributeTarget);

        /* create host playback handler & sw mixer mixerTarget handler */
        if (mMixerTarget->attributeTarget.isIEMsSource) {
            host_order = AUD_SW_MIXER_PRIOR_IEM;
        } else {
            host_order = AUD_SW_MIXER_PRIOR_PRIMARY;
        }
        lunchMixerTargetHandler(
            &mMixerTarget->playbackHdl,
            &mMixerTarget->targetHdl,
            &mMixerTarget->attributeTarget,
            HAL_PLAYBACK_MIXER_ID,
            host_order);

        /* create second handler for earphone of dual device */
        if (mStreamAttributeTarget.dualDevice != AUDIO_DEVICE_NONE) {
            memcpy(&mMixerTarget->attributeTargetSec, &mStreamAttributeTarget, sizeof(mMixerTarget->attributeTargetSec));
            configEarphoneRingtoneAttribute(&mMixerTarget->attributeTargetSec);

            if (isBtSpkDevice(mStreamAttributeTarget.output_devices)) {
                host_order = AUD_SW_MIXER_PRIOR_BT;
            } else if (isUsbSpkDevice(mStreamAttributeTarget.output_devices)) {
                host_order = AUD_SW_MIXER_PRIOR_USB;
            } else {
                host_order = AUD_SW_MIXER_PRIOR_PLAYBACK;
            }
            lunchMixerTargetHandler(
                &mMixerTarget->playbackHdlSec,
                &mMixerTarget->targetHdlSec,
                &mMixerTarget->attributeTargetSec,
                HAL_PLAYBACK_MIXER_ID,
                host_order);
        }
    }

    mMixerTarget->attachCnt++;

    AL_UNLOCK(mMixerTargetListLock);
}


void AudioALSAPlaybackHandlerMixer::closeMixerTargetHandler(void) {
    AudioALSAStreamManager *streamManager = NULL;

    AL_LOCK(mMixerTargetListLock);

    if (mMixerTargetList.find(mStreamAttributeTarget.output_devices) == mMixerTargetList.end() ||
        mMixerTarget != mMixerTargetList[mStreamAttributeTarget.output_devices]) {
        ASSERT(0);
    } else {
        mMixerTarget->attachCnt--;

        if (mMixerTarget->attachCnt == 0) {
            streamManager = AudioALSAStreamManager::getInstance();

            /* close second target if exist */
            if (mMixerTarget->targetHdlSec != NULL) {
                sw_mixer_target_detach(mMixerTarget->targetHdlSec);
                mMixerTarget->targetHdlSec = NULL;
            }
            if (mMixerTarget->playbackHdlSec != NULL) {
                mMixerTarget->playbackHdlSec->close();
                streamManager->destroyPlaybackHandler(mMixerTarget->playbackHdlSec);
                mMixerTarget->playbackHdlSec = NULL;
            }

            /* close host target */
            sw_mixer_target_detach(mMixerTarget->targetHdl);
            mMixerTarget->targetHdl = NULL;

            mMixerTarget->playbackHdl->close();
            streamManager->destroyPlaybackHandler(mMixerTarget->playbackHdl);
            mMixerTarget->playbackHdl = NULL;

            mMixerTargetList.erase(mStreamAttributeTarget.output_devices);

            AUDIO_FREE_POINTER(mMixerTarget);
        }
    }

    AL_UNLOCK(mMixerTargetListLock);
}


status_t AudioALSAPlaybackHandlerMixer::open() {
    ALOGD("%s(+), source flag %d, mDevice = 0x%x, buffer_size %d, sample_rate %u", __FUNCTION__,
          mStreamAttributeSource->mAudioOutputFlags,
          mStreamAttributeSource->output_devices,
          mStreamAttributeSource->buffer_size,
          mStreamAttributeSource->sample_rate);

    if (!mStreamAttributeSource->isIEMsSource) {
        updateOutputBufferAttr(mStreamAttributeSource);
    }

    /* Target Config */
    memcpy(&mStreamAttributeTarget, mStreamAttributeSource, sizeof(mStreamAttributeTarget));

    /* dual device */
    if (isBtSpkDevice(mStreamAttributeSource->output_devices) ||
        isUsbSpkDevice(mStreamAttributeSource->output_devices) ||
        isEarphoneSpkDevice(mStreamAttributeSource->output_devices)) {
        mStreamAttributeTarget.dualDevice = mStreamAttributeSource->output_devices;
    } else {
        mStreamAttributeTarget.dualDevice = AUDIO_DEVICE_NONE;
    }

    /* debug pcm dump */
    openWavDump(LOG_TAG);

    bool isBypassAurisys = false; // default on
    if (mStreamAttributeTarget.dualDevice != AUDIO_DEVICE_NONE ||
        mStreamAttributeSource->isIEMsSource) {
        // no effect for ringtone dual devie in playback handler mixer & IEMs
        isBypassAurisys = true;
    } else if (audio_is_usb_out_device(mStreamAttributeSource->output_devices)) {
        // usb only do through VoIP
        isBypassAurisys = !IsVoIPEnable();
    }

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (get_aurisys_on() && !isBypassAurisys) {
        CreateAurisysLibManager();
    } else
#endif
    {
        mFmtConvHdl = createFmtConvHdlWrap(mStreamAttributeSource, &mStreamAttributeTarget);
    }

    /* attach source */
    openMixerSourceHandler();

    /* attach target */
    openMixerTargetHandler();

    ALOGD("%s(-), target flag %d, mDevice 0x%x, buffer_size %u, frame_count %zu, sample_rate %u", __FUNCTION__,
          mMixerTarget->attributeTarget.mAudioOutputFlags,
          mMixerTarget->attributeTarget.output_devices,
          mMixerTarget->attributeTarget.buffer_size,
          mMixerTarget->attributeTarget.frame_count,
          mMixerTarget->attributeTarget.sample_rate);

    return NO_ERROR;
}


status_t AudioALSAPlaybackHandlerMixer::close() {
    ALOGD("%s(+), source flag %d, mDevice = 0x%x, buffer_size %d", __FUNCTION__,
          mStreamAttributeSource->mAudioOutputFlags,
          mStreamAttributeSource->output_devices,
          mStreamAttributeSource->buffer_size);

    /* detach source */
    closeMixerSourceHandler();

    /* detach target */
    closeMixerTargetHandler();

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (mAurisysLibManager) {
        DestroyAurisysLibManager();
    } else
#endif
    {
        aud_fmt_conv_hal_destroy(mFmtConvHdl);
        mFmtConvHdl = NULL;
    }


    // debug pcm dump
    closeWavDump();

    // reset dual device
    mStreamAttributeTarget.dualDevice = AUDIO_DEVICE_NONE;

    ALOGD("%s(-)", __FUNCTION__);
    return NO_ERROR;
}


ssize_t AudioALSAPlaybackHandlerMixer::write(const void *buffer, size_t bytes) {
    uint64_t mixerQueuedFrames = 0;
    struct timespec timestamp;

    ALOGD_IF(getPlaybackTimestampLogOn(),
             "%s(+), buffer = %p, bytes = %zu", __FUNCTION__, buffer, bytes);

    // const -> to non const
    void *pBuffer = const_cast<void *>(buffer);
    ASSERT(pBuffer != NULL);

    void *finalBuffer = pBuffer;
    uint32_t finalBufferBytes = bytes;

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (mAurisysLibManager) {
        // expect library output amount smoothly
        mTransferredBufferSize = GetTransferredBufferSize(
                                     bytes,
                                     mStreamAttributeSource,
                                     &mStreamAttributeTarget);

        audio_pool_buf_copy_from_linear(
            mAudioPoolBufDlIn,
            pBuffer,
            bytes);

        // post processing + SRC + Bit conversion
        aurisys_process_dl_only(mAurisysLibManager, mAudioPoolBufDlIn, mAudioPoolBufDlOut);

        // data pending: sram is device memory, need word size align 64 byte for 64 bit platform
        uint32_t data_size = audio_ringbuf_count(&mAudioPoolBufDlOut->ringbuf);
        if (data_size > mTransferredBufferSize) {
            data_size = mTransferredBufferSize;
        }
        data_size &= 0xFFFFFFC0;
        audio_pool_buf_copy_to_linear(
            &mLinearOut->p_buffer,
            &mLinearOut->memory_size,
            mAudioPoolBufDlOut,
            data_size);

        // wrap to original playback handler
        finalBuffer = (void *)mLinearOut->p_buffer;
        finalBufferBytes = data_size;
    } else
#endif
    {
        aud_fmt_conv_hal_process(
            pBuffer, bytes,
            &finalBuffer, &finalBufferBytes,
            mFmtConvHdl);
    }

    // pcm dump
    writeWavDumpData(finalBuffer, finalBufferBytes);

    // get presented frames offset
    if (mIsFirstSource == false && mIsTargetPresentedFramesOffsetValid == false) {
        memset(&timestamp, 0, sizeof(timestamp));
        if (mTargetPresentedFramesOffset == 0) { // open & get 1st offset
            mMixerTarget->playbackHdl->getPresentationQueuedOffset(
                &mTargetPresentedFramesQueued,
                &mTargetPresentedFramesOffset,
                &timestamp);
        } else { // already open & get 1st offset, check valid or not
            mixerQueuedFrames = sw_mixer_get_queued_frames(mSourceHdl);
            if (mStreamAttributeSource->sample_rate != mStreamAttributeTarget.sample_rate) {
                mixerQueuedFrames *= mStreamAttributeSource->sample_rate;
                mixerQueuedFrames /= mStreamAttributeTarget.sample_rate;
            }
            if (mixerQueuedFrames != mTotalWriteFrames) { // already join mixing
                mIsTargetPresentedFramesOffsetValid = true;
            } else { // update offset until mixed
                mMixerTarget->playbackHdl->getPresentationQueuedOffset(
                    &mTargetPresentedFramesQueued,
                    &mTargetPresentedFramesOffset,
                    &timestamp);
            }
        }
    }

    // write data to Mixer
    mTotalWriteFrames += (bytes / getSizePerFrameByAttr(mStreamAttributeSource));
    sw_mixer_source_write(mSourceHdl, finalBuffer, finalBufferBytes);

    ALOGD_IF(getPlaybackTimestampLogOn(),
             "%s(-), buffer = %p, bytes = %zu", __FUNCTION__, buffer, bytes);

    return bytes;
}


status_t AudioALSAPlaybackHandlerMixer::getHardwareBufferInfo(time_info_struct_t *HWBuffer_Time_Info) {
    if (audio_is_bluetooth_out_sco_device(mStreamAttributeSource->output_devices)) {
        status_t ret = mMixerTarget->playbackHdl->getHardwareBufferInfo(HWBuffer_Time_Info);

        if (ret == NO_ERROR) {
            HWBuffer_Time_Info->halQueuedFrame += sw_mixer_get_queued_frames(mSourceHdl);
        } else {
            HWBuffer_Time_Info->halQueuedFrame = sw_mixer_get_queued_frames(mSourceHdl);
        }
        return ret;
    } else {
        ALOGE("%s(), should use getPresentationPosition() for sw mixer!!", __FUNCTION__);

        if (!HWBuffer_Time_Info) {
            return -1;
        }
        return INVALID_OPERATION;
    }
}


int AudioALSAPlaybackHandlerMixer::getPresentationPosition(uint64_t totalWriteFrames, uint64_t *presentedFrames, struct timespec *timestamp) {
    uint64_t hdlWriteFrames = 0;

    uint64_t targetQueuedFrames = 0;
    uint64_t mixerQueuedFrames = 0;
    uint64_t totalQueuedFrames = 0;

    uint64_t targetPresentedFrames = 0;
    uint64_t targetPresentedFramesDiff = 0;

    AudioALSAPlaybackHandlerBase *playbackHdl = NULL;
    stream_attribute_t *attrMixerOut = NULL;

    uint64_t startTimeUs = 0;
    uint32_t spendTimeUs = 0;

    int ret = 0;

    if (!totalWriteFrames) {
        ALOGE("%s(), totalWriteFrames 0 error!!", __FUNCTION__);
        return -1;
    }
    if (!presentedFrames || !timestamp) {
        ALOGE("%s(), presentedFrames %p timestamp %p error!!", __FUNCTION__, presentedFrames, timestamp);
        return -1;
    }

    startTimeUs = get_mono_time_us();

    if (AL_TRYLOCK(mMixerTargetListLock) != 0) {
        ret = -EBUSY;
        if (mIsPreviousFramesValid == true) {
            clock_gettime(CLOCK_MONOTONIC, timestamp);
            ret = compensatePresentationPosition(presentedFrames, timestamp);
        }
        if (ret) {
            ALOGW("%s(), trylock => compensate error!!", __FUNCTION__);
        }
        return ret;
    }

    playbackHdl = mMixerTarget->playbackHdl;
    if (!playbackHdl) {
        ret = -ENODEV;
        goto GET_POS_EXIT;
    }
    attrMixerOut = &mMixerTarget->attributeTarget;

    mixerQueuedFrames = sw_mixer_get_queued_frames(mSourceHdl);
    if (mStreamAttributeSource->sample_rate != mStreamAttributeTarget.sample_rate) {
        mixerQueuedFrames *= mStreamAttributeSource->sample_rate;
        mixerQueuedFrames /= mStreamAttributeTarget.sample_rate;
    }

    if (mIsFirstSource == true || mMixerTarget->attachCnt == 1) {
        ret = playbackHdl->getQueuedFramesInfo(&hdlWriteFrames, &targetQueuedFrames, timestamp);
        if (ret != 0) {
            if (ret == -ETIMEDOUT && mIsPreviousFramesValid == true) {
                clock_gettime(CLOCK_MONOTONIC, timestamp);
                ret = compensatePresentationPosition(presentedFrames, timestamp);
            }
            goto GET_POS_EXIT;
        }

        if (mStreamAttributeSource->sample_rate != attrMixerOut->sample_rate) {
            targetQueuedFrames *= mStreamAttributeSource->sample_rate;
            targetQueuedFrames /= attrMixerOut->sample_rate;
        }
        totalQueuedFrames = mixerQueuedFrames + targetQueuedFrames;


        if (totalWriteFrames < totalQueuedFrames) {
            ALOGE("%s(), totalWriteFrames %" PRIu64 " < totalQueuedFrames %" PRIu64  "(%" PRIu64 ")!! ret %d",
                  __FUNCTION__, totalWriteFrames, totalQueuedFrames, (totalQueuedFrames - totalWriteFrames), ret);
            ret = -1;
            goto GET_POS_EXIT;
        }
        *presentedFrames = totalWriteFrames - totalQueuedFrames;

        ret = compensatePresentationPosition(presentedFrames, timestamp);

        ALOGD_IF(getPlaybackTimestampLogOn(),
                 "%s(), totalWriteFrames %" PRIu64 ", targetQueuedFrames %" PRIu64 ", mixerQueuedFrames %" PRIu64 ", totalQueuedFrames %" PRIu64 ", *presentedFrames %" PRIu64 ", timestamp %ld.%09ld",
                 __FUNCTION__, totalWriteFrames, targetQueuedFrames, mixerQueuedFrames, totalQueuedFrames, *presentedFrames, (long)timestamp->tv_sec, (long)timestamp->tv_nsec);
    } else {
        if (mIsTargetPresentedFramesOffsetValid == false) {
            ret = -1;
            goto GET_POS_EXIT;
        }

        ret = playbackHdl->getPresentationQueuedOffset(
                  &targetQueuedFrames,
                  &targetPresentedFrames,
                  timestamp);
        if (ret != 0) {
            if (ret == -ETIMEDOUT && mIsPreviousFramesValid == true) {
                clock_gettime(CLOCK_MONOTONIC, timestamp);
                ret = compensatePresentationPosition(presentedFrames, timestamp);
            }
            goto GET_POS_EXIT;
        }

        /* count diff from start */
        if (targetPresentedFrames < mTargetPresentedFramesOffset) {
            ALOGE("%s(), targetPresentedFrames %" PRIu64 " < mTargetPresentedFramesOffset %" PRIu64 "(%" PRIu64 ")",
                  __FUNCTION__, targetPresentedFrames, mTargetPresentedFramesOffset, (mTargetPresentedFramesOffset - targetPresentedFrames));
            ret = -1;
            goto GET_POS_EXIT;
        }
        targetPresentedFramesDiff = targetPresentedFrames - mTargetPresentedFramesOffset;

        /* need to remove queued data count when start */
        if (targetPresentedFramesDiff < mTargetPresentedFramesQueued) {
            ret = -1;
            goto GET_POS_EXIT;
        }
        targetPresentedFramesDiff -= mTargetPresentedFramesQueued;

        if (mStreamAttributeSource->sample_rate != attrMixerOut->sample_rate) {
            targetPresentedFramesDiff *= mStreamAttributeSource->sample_rate;
            targetPresentedFramesDiff /= attrMixerOut->sample_rate;
        }

        /* from write & local presented to get queued count */
        if (mTotalWriteFrames < targetPresentedFramesDiff) {
            ALOGE("%s(), mTotalWriteFrames %" PRIu64 " < targetPresentedFramesDiff %" PRIu64 "(%" PRIu64 ")",
                  __FUNCTION__, mTotalWriteFrames, targetPresentedFramesDiff, (targetPresentedFramesDiff - mTotalWriteFrames));
            /* reset */
            mTotalWriteFrames = 0;
            mIsPreviousFramesValid = false;
            mTargetPresentedFramesQueued = targetQueuedFrames;
            mTargetPresentedFramesOffset = targetPresentedFrames;
            ret = -1;
            goto GET_POS_EXIT;
        }
        totalQueuedFrames = mTotalWriteFrames - targetPresentedFramesDiff;

        /* get overall presented frames */
        if (totalWriteFrames < totalQueuedFrames) {
            ALOGE("%s(), totalWriteFrames %" PRIu64 " < totalQueuedFrames %" PRIu64  "(%" PRIu64 ")!! ret %d",
                  __FUNCTION__, totalWriteFrames, totalQueuedFrames, (totalQueuedFrames - totalWriteFrames), ret);
            ret = -1;
            goto GET_POS_EXIT;
        }
        *presentedFrames = totalWriteFrames - totalQueuedFrames;

        ret = compensatePresentationPosition(presentedFrames, timestamp);

        ALOGD_IF(getPlaybackTimestampLogOn(),
                 "%s(), totalWriteFrames %" PRIu64 ", mTotalWriteFrames %" PRIu64 ", mTargetPresentedFramesOffset %" PRIu64 ", targetPresentedFramesDiff %" PRIu64 ", totalQueuedFrames %" PRIu64 ", *presentedFrames %" PRIu64 ", timestamp %ld.%09ld",
                 __FUNCTION__, totalWriteFrames, mTotalWriteFrames, mTargetPresentedFramesOffset, targetPresentedFramesDiff, totalQueuedFrames, *presentedFrames, (long)timestamp->tv_sec, (long)timestamp->tv_nsec);
    }

    mIsPreviousFramesValid = true;

    spendTimeUs = get_mono_time_us() - startTimeUs;
    if (spendTimeUs > 1000) { // 1ms
        ALOGW("%s(), flag %d, spendTimeUs %u", __FUNCTION__,
              mStreamAttributeSource->mAudioOutputFlags, spendTimeUs);
    }

GET_POS_EXIT:
    AL_UNLOCK(mMixerTargetListLock);
    return ret;
}


status_t AudioALSAPlaybackHandlerMixer::setScreenState(bool mode,
                                                       size_t buffer_size,
                                                       size_t reduceInterruptSize,
                                                       bool bforce) {
    if (mMixerTarget->attributeTarget.isIEMsSource) {
        return 0;
    }
    if (buffer_size != mMixerTarget->attributeTarget.buffer_size) {
        ALOGD("%s(), buffer_size %zu => %u", __FUNCTION__,
              buffer_size, mMixerTarget->attributeTarget.buffer_size);
    }
    /* no need to set to BT */
    return mMixerTarget->playbackHdl->setScreenState(mode, mMixerTarget->attributeTarget.buffer_size, reduceInterruptSize, bforce);
}


int AudioALSAPlaybackHandlerMixer::setSuspend(bool suspend) {
    int ret = 0;

    ret = mMixerTarget->playbackHdl->setSuspend(suspend);

    if (mMixerTarget->playbackHdlSec) {
        mMixerTarget->playbackHdlSec->setSuspend(suspend);
    }
    return ret;
}

status_t AudioALSAPlaybackHandlerMixer::routing(const audio_devices_t output_devices __unused) {
    return NO_ERROR;
}

int AudioALSAPlaybackHandlerMixer::getLatency() {
    return mMixerTarget->playbackHdl->getLatency();
}


void AudioALSAPlaybackHandlerMixer::updateOutputBufferAttr(const stream_attribute_t *attr) {
    if (!attr) {
        return;
    }
    mOutputBufferAttr[attr->mAudioOutputFlags] = *attr;
}


uint32_t AudioALSAPlaybackHandlerMixer::getOutputBufferSize(
    const stream_attribute_t *attr_src,
    const stream_attribute_t *attr_tgt) {
    uint32_t period_us = 0;
    const stream_attribute_t *attr_buf = NULL;
    stream_attribute_t attr_default;

    if (!attr_src || !attr_tgt) {
        return 8192;
    }

    // use IEMs configs
    if (attr_tgt->isIEMsSource) {
        return getPeriodBufSizeByUs(attr_tgt, attr_src->periodUs);
    }

#if defined(PRIMARY_USB)
    // use device dedicated configs
    if (audio_is_usb_out_device(attr_tgt->output_devices) &&
        attr_tgt->dualDevice == AUDIO_DEVICE_NONE) {
        period_us = AudioUSBCenter::getInstance()->getOutPeriodUs();
        if (period_us) {
            return getPeriodBufSizeByUs(attr_tgt, period_us);
        }
    }
#endif

    // use source configs
    if (attr_tgt->mAudioOutputFlags == attr_src->mAudioOutputFlags) {
        return getFmtConvBufferSize(attr_src,
                                    attr_tgt,
                                    attr_src->buffer_size);
    }

    // use ref configs
    if (mOutputBufferAttr.find(attr_tgt->mAudioOutputFlags) != mOutputBufferAttr.end()) {
        attr_buf = &mOutputBufferAttr[attr_tgt->mAudioOutputFlags];
        return getFmtConvBufferSize(attr_buf,
                                    attr_tgt,
                                    attr_buf->buffer_size);
    }

    // use default configs
    attr_default.audio_format = AUDIO_FORMAT_PCM_32_BIT;
    attr_default.num_channels = 2;
    attr_default.sample_rate = 48000;

    if (attr_tgt->mAudioOutputFlags == AUDIO_OUTPUT_FLAG_DEEP_BUFFER) {
        ALOGW("%s(), use default value 16384 for flags %d", __FUNCTION__, attr_tgt->mAudioOutputFlags);
        attr_default.buffer_size = 16384;
    } else if (attr_tgt->mAudioOutputFlags == AUDIO_OUTPUT_FLAG_FAST) {
        ALOGW("%s(), use default value 2048 for flags %d", __FUNCTION__, attr_tgt->mAudioOutputFlags);
        attr_default.buffer_size = 2048;
    } else if (attr_tgt->mAudioOutputFlags == 0) {
        ALOGW("%s(), use default value 32768 for flags %d", __FUNCTION__, attr_tgt->mAudioOutputFlags);
        attr_default.buffer_size = 32768;
    } else {
        ALOGW("%s(), use default value 8192 for flags %d", __FUNCTION__, attr_tgt->mAudioOutputFlags);
        attr_default.buffer_size = 8192;
    }

    return getFmtConvBufferSize(&attr_default,
                                attr_tgt,
                                attr_default.buffer_size);
}


bool AudioALSAPlaybackHandlerMixer::deviceSupportHifi(audio_devices_t outputdevice) {
    // modify this to let output device support hifi audio
    if (outputdevice == AUDIO_DEVICE_OUT_WIRED_HEADSET ||
        outputdevice == AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
        return true;
    } else {
        return false;
    }
}


uint32_t AudioALSAPlaybackHandlerMixer::chooseTargetSampleRate(uint32_t SampleRate, audio_devices_t outputdevice) {
    ALOGV("ChooseTargetSampleRate SampleRate = %d outputdevice = %d", SampleRate, outputdevice);
    uint32_t TargetSampleRate = 48000;

    if (audio_is_usb_out_device(mStreamAttributeSource->output_devices)) {
#if defined(PRIMARY_USB)
        return AudioUSBCenter::getInstance()->getHighestSampleRate(PCM_OUT);
#else
        return 48000;
#endif
    }

#if defined(MTK_HIFIAUDIO_SUPPORT)
    bool hifi_enable = mHardwareResourceManager->getHiFiStatus();
    bool device_support_hifi = deviceSupportHifi(outputdevice);
    ALOGD("%s() hifi_enable = %d device_support_hifi = %d, SampleRate = %d",
          __FUNCTION__, hifi_enable, device_support_hifi, SampleRate);

    if (hifi_enable && device_support_hifi) {
        TargetSampleRate = SampleRate;
    }
#else
    if (SampleRate <=  192000 && SampleRate > 96000 && deviceSupportHifi(outputdevice)) {
        TargetSampleRate = 192000;
    } else if (SampleRate <= 96000 && SampleRate > 48000 && deviceSupportHifi(outputdevice)) {
        TargetSampleRate = 96000;
    } else if (SampleRate <= 48000 && SampleRate >= 32000) {
        TargetSampleRate = SampleRate;
    }
#endif
    return TargetSampleRate;
}




} // end of namespace android
