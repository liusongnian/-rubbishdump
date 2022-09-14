#include "AudioALSAPlaybackHandlerTVOut.h"
#include "AudioALSADriverUtility.h"
#include "AudioALSAStreamManager.h"
#include "AudioALSADeviceConfigManager.h"
#include "AudioALSAHardwareResourceManager.h"

#include <linux/mediatek_drm.h>
#include <sys/ioctl.h>
#include <stdio.h>
#include <fcntl.h>
#include <string>

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <audio_ringbuf.h>
#include <audio_pool_buf_handler.h>

#include <aurisys_controller.h>
#include <aurisys_lib_manager.h>
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAPlaybackHandlerTVOut"
//#define DEBUG_LATENCY
#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)

namespace android {

enum {
    TV_OUT_TYPE_HDMI,
    TV_OUT_TYPE_DPTX,
    TV_OUT_TYPE_NUM,
};

static const char *const tvOutTypeStr[] = {"HDMI", "DPTX"};

AudioALSAPlaybackHandlerTVOut::AudioALSAPlaybackHandlerTVOut(const stream_attribute_t *mAttributeSource) :
    AudioALSAPlaybackHandlerBase(mAttributeSource) {
    mPlaybackHandlerType = PLAYBACK_HANDLER_HDMI;
    memset((void *)&mNewtime, 0, sizeof(mNewtime));
    memset((void *)&mOldtime, 0, sizeof(mOldtime));
    mMixer = NULL;
    mTvOutFd = mHardwareResourceManager->getTVOutFileDescriptor();
    ALOGD("%s(), mTvOutFd: %d", __FUNCTION__, mTvOutFd);
}

AudioALSAPlaybackHandlerTVOut::~AudioALSAPlaybackHandlerTVOut() {
    ALOGD("%s(), mTvOutFd: %d", __FUNCTION__, mTvOutFd);

    if (mTvOutFd > 0) {
        ::close(mTvOutFd);
        mTvOutFd = -1;
        mHardwareResourceManager->setTVOutFileDescriptor(mTvOutFd);
    }
}

status_t AudioALSAPlaybackHandlerTVOut::setTVOutPlaybackInfo(int channels, int format, int sampleRate) {
    int ret = 0;
    int sampleRateBit = 0;
    int formatBit = 0;
    int channel = 0;
    int configValue = 0;

    switch (channels) {
    case 1:
    case 2:
        channel = MTK_DRM_CHANNEL_2_BIT;
        break;
    case 3:
        channel = MTK_DRM_CHANNEL_3_BIT;
        break;
    case 4:
        channel = MTK_DRM_CHANNEL_4_BIT;
        break;
    case 5:
        channel = MTK_DRM_CHANNEL_5_BIT;
        break;
    case 6:
        channel = MTK_DRM_CHANNEL_6_BIT;
        break;
    case 7:
        channel = MTK_DRM_CHANNEL_7_BIT;
        break;
    case 8:
        channel = MTK_DRM_CHANNEL_8_BIT;
        break;
    default:
        ALOGE("%s(), invalid channel num, use stereo", __FUNCTION__);
        channel = MTK_DRM_CHANNEL_2_BIT;
        break;
    }

    switch (sampleRate) {
    case 32000:
        sampleRateBit = MTK_DRM_SAMPLERATE_32K_BIT;
        break;
    case 44100:
        sampleRateBit = MTK_DRM_SAMPLERATE_44K_BIT;
        break;
    case 48000:
        sampleRateBit = MTK_DRM_SAMPLERATE_48K_BIT;
        break;
    case 96000:
        sampleRateBit = MTK_DRM_SAMPLERATE_96K_BIT;
        break;
    case 192000:
        sampleRateBit = MTK_DRM_SAMPLERATE_192K_BIT;
        break;
    default:
        ALOGE("%s(), invalid sample rate, use 48K as default", __FUNCTION__);
        sampleRateBit = MTK_DRM_SAMPLERATE_48K_BIT;
        break;
    }

    switch (format) {
    case AUDIO_FORMAT_PCM_32_BIT:
        formatBit = MTK_DRM_BITWIDTH_24_BIT;
        break;
    case AUDIO_FORMAT_PCM_8_24_BIT:
        formatBit = MTK_DRM_BITWIDTH_24_BIT;
        break;
    case AUDIO_FORMAT_PCM_16_BIT:
        formatBit = MTK_DRM_BITWIDTH_16_BIT;
        break;
    default:
        ALOGE("%s(), invalid sample rate, use 24bit as default", __FUNCTION__);
        formatBit = MTK_DRM_BITWIDTH_24_BIT;
        break;
    }
    configValue = (channel << MTK_DRM_CAPABILITY_CHANNEL_SFT) |
                  (sampleRateBit << MTK_DRM_CAPABILITY_SAMPLERATE_SFT) |
                  (formatBit << MTK_DRM_CAPABILITY_BITWIDTH_SFT);
    ALOGD("%s(), channels %d, format %d sampleRate %d configValue 0x%x",
          __FUNCTION__, channels, format, sampleRate, configValue);
    ret = ::ioctl(mTvOutFd, DRM_IOCTL_MTK_HDMI_AUDIO_CONFIG, &configValue);
    if (ret < 0) {
        ALOGE("%s(), audio config error: %s", __FUNCTION__, strerror(errno));
    }

    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerTVOut::setTVOutEnable(int enable) {
    int ret;
    ALOGD("%s(), enable %d", __FUNCTION__, enable);
    ret = ::ioctl(mTvOutFd, DRM_IOCTL_MTK_HDMI_AUDIO_ENABLE, &enable);
    if (ret < 0) {
        ALOGE("%s(), audio enable error: %s", __FUNCTION__, strerror(errno));
    }

    return NO_ERROR;
}

String8 AudioALSAPlaybackHandlerTVOut::getPlaybackSequence(unsigned int turnOnSeqType,
                                                           const char *playbackSeq) {
    String8 sequence = String8();
    String8 displayType = String8();
    String8 tvOutChannelStr = String8();
    mtk_dispif_info dispif_info;
    int channels = mStreamAttributeTarget.num_channels;
    int ret;

    memset(&dispif_info, 0, sizeof(dispif_info));

    switch (turnOnSeqType) {
    case SETTING_SEQUENCE:
        if (channels == 1 || channels == 2) {
            tvOutChannelStr = "2CH";
        } else if (channels == 3 || channels == 4) {
            tvOutChannelStr = "4CH";
        } else if (channels == 5 || channels == 6) {
            tvOutChannelStr = "6CH";
        } else if (channels == 7 || channels == 8) {
            tvOutChannelStr = "8CH";
        } else {
            ALOGE("%s(), invalid channels: %d", __FUNCTION__, channels);
        }

        sequence = String8(playbackSeq) + tvOutChannelStr;
        break;
    case TURN_ON_SEQUENCE_1:
        ret = ::ioctl(mTvOutFd, DRM_IOCTL_MTK_HDMI_GET_DEV_INFO, &dispif_info);
        if (ret < 0) {
            ALOGE("%s(), DRM get dev info fail: %s, Use DPTX as default",
                  __FUNCTION__, strerror(errno));
            displayType = tvOutTypeStr[TV_OUT_TYPE_DPTX];
            sequence = displayType + String8(playbackSeq);  //"XXXX_TDM_OUT"
            break;
        }

        if (dispif_info.displayType == HDMI) {
            displayType = tvOutTypeStr[TV_OUT_TYPE_HDMI];
        } else {
            displayType = tvOutTypeStr[TV_OUT_TYPE_DPTX];
        }

        sequence = displayType + String8(playbackSeq);  //"XXXX_TDM_OUT"
        break;
    default:
        ASSERT(0);
        break;
    }

    return sequence;
}

status_t AudioALSAPlaybackHandlerTVOut::open() {
    ALOGD("+%s(), flag = %d, source output_devices = 0x%x, audio_format = %x, buffer_size = %d, sample_rate = %d",
          __FUNCTION__,
          mStreamAttributeSource->mAudioOutputFlags,
          mStreamAttributeSource->output_devices,
          mStreamAttributeSource->audio_format,
          mStreamAttributeSource->buffer_size,
          mStreamAttributeSource->sample_rate);

    String8 settingSeq;
    int pcmIdx = 0;

    AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

#ifdef PLAYBACK_USE_24BITS_ONLY
    mStreamAttributeTarget.audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
#else
    mStreamAttributeTarget.audio_format = (mStreamAttributeSource->audio_format == AUDIO_FORMAT_PCM_32_BIT) ?
                                          AUDIO_FORMAT_PCM_8_24_BIT : AUDIO_FORMAT_PCM_16_BIT;
#endif
    mStreamAttributeTarget.audio_channel_mask = mStreamAttributeSource->audio_channel_mask;
    mStreamAttributeTarget.num_channels = popcount(mStreamAttributeTarget.audio_channel_mask);
    mStreamAttributeTarget.sample_rate = mStreamAttributeSource->sample_rate;
    mStreamAttributeTarget.buffer_size = mStreamAttributeSource->buffer_size * 2;

    // HW pcm config
    memset(&mConfig, 0, sizeof(mConfig));
    mConfig.channels = mStreamAttributeTarget.num_channels;
    mConfig.rate = mStreamAttributeTarget.sample_rate;
    mConfig.format = transferAudioFormatToPcmFormat(mStreamAttributeTarget.audio_format);
    mConfig.period_count = 2;
    mConfig.start_threshold = 0;
    mConfig.stop_threshold = 0;
    mConfig.silence_threshold = 0;

    mConfig.period_size = (mStreamAttributeTarget.buffer_size / (mConfig.channels * mConfig.period_count)) /
                          ((mStreamAttributeTarget.audio_format == AUDIO_FORMAT_PCM_16_BIT) ? 2 : 4);

    mStreamAttributeTarget.mInterrupt = (mConfig.period_size + 0.0) / mStreamAttributeTarget.sample_rate;

    // debug pcm dump
    OpenPCMDump(LOG_TAG);

#if defined(MTK_AUDIO_KS)
    pcmIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackHDMI);

    settingSeq = getPlaybackSequence(SETTING_SEQUENCE, AUDIO_CTL_PLAYBACK_TO_TDM);
    mApTurnOnSequence = getPlaybackSequence(TURN_ON_SEQUENCE_1, AUDIO_CTL_TDM_OUT);
    mHardwareResourceManager->applySettingSequence(settingSeq);
    mHardwareResourceManager->enableTurnOnSequence(mApTurnOnSequence);
#else
    ALOGE("%s(), pcmIdx not assigned", __FUNCTION__);
#endif

    ALOGD("%s(), mConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d, mInterrupt: %f",
          __FUNCTION__, mConfig.channels, mConfig.rate, mConfig.period_size, mConfig.period_count, mConfig.format,
          mStreamAttributeTarget.mInterrupt);

    // SRC
    initBliSrc();

    // bit conversion
    initBitConverter();

    initDataPending();

    // open pcm driver before set hw config of DPTX due to clk need to be stable first.
    openPcmDriver(pcmIdx);

    setTVOutPlaybackInfo(mConfig.channels, mStreamAttributeTarget.audio_format, mConfig.rate);

    setTVOutEnable(true);

    return NO_ERROR;
}


status_t AudioALSAPlaybackHandlerTVOut::close() {
    ALOGD("%s()", __FUNCTION__);
    AL_AUTOLOCK(*AudioALSADriverUtility::getInstance()->getStreamSramDramLock());

    setTVOutEnable(false);

    // close pcm driver
    closePcmDriver();

#if defined(MTK_AUDIO_KS)
    mHardwareResourceManager->disableTurnOnSequence(mApTurnOnSequence);
#endif

    // bit conversion
    deinitBitConverter();

    // SRC
    deinitBliSrc();

    DeinitDataPending();

    // debug pcm dump
    ClosePCMDump();

    return NO_ERROR;
}

ssize_t AudioALSAPlaybackHandlerTVOut::write(const void *buffer, size_t bytes) {
    ALOGV("%s(), buffer = %p, bytes = %zu", __FUNCTION__, buffer, bytes);

    if (mPcm == NULL) {
        ALOGE("%s(), mPcm == NULL, return", __FUNCTION__);
        return bytes;
    }

    // const -> to non const
    void *pBuffer = const_cast<void *>(buffer);
    ASSERT(pBuffer != NULL);

#ifdef MTK_AUDIO_DYNAMIC_LOG
    calHoldTime(AUD_AF_THRES);
#endif

    // SRC
    void *pBufferAfterBliSrc = NULL;
    uint32_t bytesAfterBliSrc = 0;
    doBliSrc(pBuffer, bytes, &pBufferAfterBliSrc, &bytesAfterBliSrc);

    // bit conversion
    void *pBufferAfterBitConvertion = NULL;
    uint32_t bytesAfterBitConvertion = 0;
    doBitConversion(pBufferAfterBliSrc, bytesAfterBliSrc, &pBufferAfterBitConvertion, &bytesAfterBitConvertion);

    // data pending
    void *pBufferAfterPending = NULL;
    uint32_t bytesAfterpending = 0;
    if (pBufferAfterBitConvertion != NULL) {
        dodataPending(pBufferAfterBitConvertion, bytesAfterBitConvertion, &pBufferAfterPending, &bytesAfterpending);
    }

    WritePcmDumpData(pBufferAfterPending, bytesAfterpending);

#ifdef MTK_AUDIO_DYNAMIC_LOG
    calHoldTime(AUD_HAL_THRES);
#endif

    // write data to pcm driver
    int retval = pcm_write(mPcm, pBufferAfterPending, bytesAfterpending);

#ifdef MTK_AUDIO_DYNAMIC_LOG
    calHoldTime(AUD_KERNEL_THRES);
#endif

    if (retval != 0) {
        ALOGE("%s(), pcm_write() error, retval = %d", __FUNCTION__, retval);
    }

#ifdef MTK_AUDIO_DYNAMIC_LOG
    checkThresTime(mStreamAttributeSource->mAudioOutputFlags);
#endif

    ALOGD_IF((mlog_flag & AUD_OUT_WRITE_LOG), "-%s(), buffer = %p, bytes = %zu flag = %d",
             __FUNCTION__, buffer, bytes, mStreamAttributeSource->mAudioOutputFlags);

    return bytes;
}

status_t AudioALSAPlaybackHandlerTVOut::setFilterMng(AudioMTKFilterManager *pFilterMng) {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    ALOGD("+%s() mAudioFilterManagerHandler [%p]", __FUNCTION__, pFilterMng);
    mAudioFilterManagerHandler = pFilterMng;
#else
    (void *)pFilterMng;
#endif
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerTVOut::routing(const audio_devices_t output_devices) {
    mHardwareResourceManager->changeOutputDevice(output_devices);
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    if (mAudioFilterManagerHandler) { mAudioFilterManagerHandler->setDevice(output_devices); }
#endif
    return NO_ERROR;
}

} // end of namespace android

