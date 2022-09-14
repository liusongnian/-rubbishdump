#include "AudioALSAStreamOut.h"

#include <inttypes.h>
#include <math.h>
#include <media/TypeConverter.h>
#include <time.h>

#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
#include "AudioParamParser.h"
#endif
#include "AudioALSAPlaybackHandlerBase.h"
#include "AudioUtility.h"

#include "AudioALSASampleRateController.h"
#include "AudioALSAFMController.h"
#include "AudioALSAHardwareResourceManager.h"
#include "AudioSmartPaController.h"
#include "AudioALSASpeechPhoneCallController.h"

#include <hardware/audio_mtk.h>
#include <cutils/str_parms.h>
#include <hardware/audio.h>

#if defined(MTK_HIFIAUDIO_SUPPORT)
#include "AudioALSADeviceConfigManager.h"
#endif
#if defined(MTK_AUDIODSP_SUPPORT)
#include "AudioALSAPlaybackHandlerOffload.h"
#include "AudioDspStreamManager.h"
#endif
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
#include <audio_dsp_controller.h>
#endif

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
#include <AudioIEMsController.h>
#endif

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <aurisys_controller.h>
#include <aurisys_scenario.h>
#include <aurisys_utility.h>
#include <aurisys_config.h>
#endif

extern "C" {
#include "alsa_device_profile.h"
#include "alsa_device_proxy.h"
#include "alsa_logging.h"
#include <audio_utils/channels.h>
}

#if defined(PRIMARY_USB)
#include <AudioUSBCenter.h>
#endif

#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif


#define BUFFER_FRAME_COUNT_PER_ACCESS (1024)

// ALPS03595920 : CTS limits hal buffer size
#ifndef MAX_BUFFER_FRAME_COUNT_PER_ACCESS
#define MAX_BUFFER_FRAME_COUNT_PER_ACCESS (2048)
#endif

#define BUFFER_FRAME_COUNT_PER_ACCESSS_HDMI (1024)

#ifndef FRAME_COUNT_MIN_PER_ACCESSS
#define FRAME_COUNT_MIN_PER_ACCESSS (256)
#endif

#define FRAME_COUNT_FAST_AND_RAW (256)

//#define DOWNLINK_LOW_LATENCY_CPU_SPEED  (1300000)
//#define DOWNLINK_NORMAL_CPU_SPEED       ( 715000)

#define USB_HIFI_WRITE_BYTES (32768)


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG  "AudioALSAStreamOut"


#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)

namespace android {

uint32_t AudioALSAStreamOut::mDumpFileNum = 0;

// TODO(Harvey): Query this
static const audio_format_t       kDefaultOutputSourceFormat      = AUDIO_FORMAT_PCM_16_BIT;
static const audio_channel_mask_t kDefaultOutputSourceChannelMask = AUDIO_CHANNEL_OUT_STEREO;
static const uint32_t             kDefaultOutputSourceSampleRate  = 44100;

uint32_t AudioALSAStreamOut::mSuspendStreamOutHDMIStereoCount = 0;

AudioALSAStreamOut *AudioALSAStreamOut::mStreamOutHDMIStereo = NULL;
uint32_t mStreamOutHDMIStereoCount = 0;


AudioALSAStreamOut::AudioALSAStreamOut() :
    mStreamManager(AudioALSAStreamManager::getInstance()),
    mPlaybackHandler(NULL),
    mPCMDumpFile(NULL),
    mDumpFile(NULL),
    mLockCount(0),
    mIdentity(0xFFFFFFFF),
    mSuspendCount(0),
    mSuspendIEMsForVoIP(false),
    mMuteForRouting(0),
    mStandby(true),
    mStreamOutType(STREAM_OUT_PRIMARY),
    mPresentedBytes(0),
    mPresentFrames(0),
    mPreviousFrames(0),
    mLowLatencyMode(true),
    mOffload(false),
    mPaused(false),
    mStreamCbk(NULL),
    mCbkCookie(NULL),
    mOffloadVol(0x10000),
    mLatency(0),
    mStart(false),
    mDestroy(false) {

    mLogEnable = AudioALSADriverUtility::getInstance()->GetPropertyValue(streamout_log_propty);

    ALOGV("%s(), mLogEnable %d", __FUNCTION__, mLogEnable);

    memset(&mStreamAttributeSource, 0, sizeof(mStreamAttributeSource));
    memset(&mPresentedTime, 0, sizeof(timespec));

    memset(&mMuteTime, 0, sizeof(struct timespec));
    memset(&mMuteCurTime, 0, sizeof(struct timespec));

    memset(&mPretimestamp, 0, sizeof(struct timespec));

#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
    if (isAdspRecoveryEnable()) {
        mAdspRecoveryLock = new_adsp_rlock();
    } else
#endif
    {
        mAdspRecoveryLock = NULL;
    }

    mBytesWavDumpWritten = 0;

}


AudioALSAStreamOut::~AudioALSAStreamOut() {
    int oldCount;
    oldCount = android_atomic_inc(&mLockCount);
    AL_AUTOLOCK(mLock);
    AL_AUTOLOCK(mSuspendLock);
    oldCount = android_atomic_dec(&mLockCount);

    ALOGD("%s(), flag %d, mStreamOutHDMIStereoCount %d", __FUNCTION__,
          mStreamAttributeSource.mAudioOutputFlags, mStreamOutHDMIStereoCount);

    mDestroy = true;

    if (!mStandby) {
        ALOGW("%s(), not standby, mStandby %d, mPlaybackHandler %p",
              __FUNCTION__, mStandby, mPlaybackHandler);
        standby_l(true);
    }

    if (mStreamOutHDMIStereo == this) {
        mStreamOutHDMIStereoCount--;
    }

    if ((mStreamOutHDMIStereo == this) && (mStreamOutHDMIStereoCount <= 0)) {
        mStreamOutHDMIStereo = NULL;
        ALOGD("%s() mStreamOutHDMIStereo = NULL %d", __FUNCTION__, mStreamOutHDMIStereoCount);
    }

#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
    free_adsp_rlock(mAdspRecoveryLock);
    mAdspRecoveryLock = NULL;
#endif
}


status_t AudioALSAStreamOut::set(
    uint32_t devices,
    int *format,
    uint32_t *channels,
    uint32_t *sampleRate,
    status_t *status,
    uint32_t flags) {

    AL_AUTOLOCK(mLock);

    *status = NO_ERROR;

    // device
    mStreamAttributeSource.output_devices = static_cast<audio_devices_t>(devices);
    mStreamAttributeSource.policyDevice = mStreamAttributeSource.output_devices;
#if defined(PRIMARY_USB)
    alsa_device_profile *profile = NULL;
    if (audio_is_usb_out_device(mStreamAttributeSource.output_devices)) {
        profile = AudioUSBCenter::getInstance()->getProfile(PCM_OUT);
    }
#endif

    // check format
    if (*format == AUDIO_FORMAT_MP3 || *format == AUDIO_FORMAT_AAC_LC) {
        ALOGD("%s(), format %s", __FUNCTION__, *format == AUDIO_FORMAT_MP3 ? "mp3" : "aac");
        mStreamAttributeSource.audio_offload_format = *format;
#if defined(PRIMARY_USB)
    } else if (audio_is_usb_out_device(mStreamAttributeSource.output_devices)) {
        if (*format == 0) {
            *format = AUDIO_FORMAT_PCM_32_BIT; // default 32 bit and do bcv later for usb device(ex 24 pack)
            ALOGD("%s(), get default USB format 0 => 0x%x.", __FUNCTION__, *format);
        }
#endif
    } else {
        if (*format != AUDIO_FORMAT_PCM_16_BIT &&
            *format != AUDIO_FORMAT_PCM_8_24_BIT &&
            *format != AUDIO_FORMAT_PCM_32_BIT) {
            ALOGE("%s(), wrong format 0x%x, use 0x%x instead.", __FUNCTION__, *format, kDefaultOutputSourceFormat);
            *format = kDefaultOutputSourceFormat;
        }
    }
    mStreamAttributeSource.audio_format = static_cast<audio_format_t>(*format);

    // check channel mask
    if (mStreamAttributeSource.output_devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) { // HDMI
        if (*channels == AUDIO_CHANNEL_OUT_2POINT1) {
            *channels = AUDIO_CHANNEL_OUT_2POINT0POINT2;
        } else if (*channels == AUDIO_CHANNEL_OUT_3POINT0POINT2 ||
                   *channels == AUDIO_CHANNEL_OUT_5POINT1 ||
                   *channels == AUDIO_CHANNEL_OUT_6POINT1) {
            *channels = AUDIO_CHANNEL_OUT_7POINT1;
        }

        if (*channels == AUDIO_CHANNEL_OUT_STEREO) {
            mStreamOutType = STREAM_OUT_HDMI_STEREO;

            mStreamOutHDMIStereo = this;
            mStreamOutHDMIStereoCount++;
            ALOGD("%s(), mStreamOutHDMIStereoCount =%d", __FUNCTION__, mStreamOutHDMIStereoCount);
        } else if (*channels == AUDIO_CHANNEL_OUT_2POINT0POINT2 ||
                   *channels == AUDIO_CHANNEL_OUT_7POINT1) {
            mStreamOutType = STREAM_OUT_HDMI_MULTI_CHANNEL;
        } else {
            ALOGE("%s(), wrong channels 0x%x, use 0x%x instead.", __FUNCTION__, *channels, kDefaultOutputSourceChannelMask);

            *channels = kDefaultOutputSourceChannelMask;
        }
    } else if (devices == AUDIO_DEVICE_OUT_SPEAKER_SAFE) { // Primary
        mStreamOutType = STREAM_OUT_VOICE_DL;
#if defined(PRIMARY_USB)
    } else if (audio_is_usb_out_device(mStreamAttributeSource.output_devices) && (*channels == 0) && profile) {
        if (profile_is_channel_count_valid(profile, 2)) {
            *channels = AUDIO_CHANNEL_OUT_STEREO;
        } else {
            *channels = AUDIO_CHANNEL_OUT_MONO;
        }
        ALOGD("%s(), get default USB channels 0 => 0x%x.", __FUNCTION__, *channels);
#endif
    } else if (*channels != AUDIO_CHANNEL_OUT_MONO && *channels != kDefaultOutputSourceChannelMask) {
        ALOGE("%s(), wrong channels 0x%x, use 0x%x instead.", __FUNCTION__, *channels, kDefaultOutputSourceChannelMask);

        *channels = kDefaultOutputSourceChannelMask;
    }
    mStreamAttributeSource.audio_channel_mask = (audio_channel_mask_t)*channels;
    mStreamAttributeSource.num_channels = popcount(*channels);

    // check sample rate
    if (SampleRateSupport(*sampleRate) == true) {
        if ((mStreamOutType == STREAM_OUT_PRIMARY || mStreamOutType == STREAM_OUT_VOICE_DL) && ((flags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) == 0)) {
            AudioALSASampleRateController::getInstance()->setPrimaryStreamOutSampleRate(*sampleRate);
        }
#if defined(PRIMARY_USB)
        if (audio_is_usb_out_device(mStreamAttributeSource.output_devices) && profile) {
            uint32_t highestSampleRate = AudioUSBCenter::getInstance()->getHighestSampleRate(PCM_OUT);
            if (*sampleRate > highestSampleRate) {
                ALOGD("%s(), get highest USB sampleRate %u => %u.", __FUNCTION__, *sampleRate, highestSampleRate);
                *sampleRate = highestSampleRate;
            }
        }
#endif
    } else {
#if defined(PRIMARY_USB)
        if (audio_is_usb_out_device(mStreamAttributeSource.output_devices) && *sampleRate == 0 && profile) {
            *sampleRate = AudioUSBCenter::getInstance()->getHighestSampleRate(PCM_OUT);
            ALOGD("%s(), get default USB sampleRate 0 => %u.", __FUNCTION__, *sampleRate);
            if (*sampleRate == 0) {
                *sampleRate = kDefaultOutputSourceSampleRate;
                ALOGW("%s(), rate 0!! use %u", __FUNCTION__, kDefaultOutputSourceSampleRate);
            }
        } else
#endif
        {
            ALOGE("%s(), wrong sampleRate %d, use %d instead.", __FUNCTION__, *sampleRate, kDefaultOutputSourceSampleRate);
            *sampleRate = kDefaultOutputSourceSampleRate;
        }
    }
    mStreamAttributeSource.sample_rate = *sampleRate;


    mStreamAttributeSource.mAudioOutputFlags = (audio_output_flags_t)flags;
    collectPlatformOutputFlags(mStreamAttributeSource.mAudioOutputFlags);

    if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
        mStreamAttributeSource.usePolicyDevice = true;
        char result[PROPERTY_VALUE_MAX] = {0};
        property_get(allow_offload_propty, result, "1");
        mStreamAttributeSource.offload_codec_info.disable_codec = atoi(result) ? 0 : 1;
        ALOGD("%s(),mStreamAttributeSource.offload_codec_info.disable_codec =%d ", __FUNCTION__, mStreamAttributeSource.offload_codec_info.disable_codec);
    }

#ifdef MTK_HYBRID_NLE_SUPPORT
    if (isIsolatedDeepBuffer(mStreamAttributeSource.mAudioOutputFlags)) {
        AudioALSAHyBridNLEManager::setSupportRunNLEHandler(PLAYBACK_HANDLER_DEEP_BUFFER);
    }
#endif

    // debug for PowerHAL
    char value[PROPERTY_VALUE_MAX] = {0};
    if (mStreamAttributeSource.mAudioOutputFlags & (AUDIO_OUTPUT_FLAG_FAST | AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        (void) property_get("vendor.audio.powerhal.latency.dl", value, "1");
    } else {
        (void) property_get("vendor.audio.powerhal.power.dl", value, "1");
    }
    int debuggable = atoi(value);
    mStreamAttributeSource.mPowerHalEnable = debuggable ? true : false;


    // audio low latency param - playback - hal buffer size
    setBufferSize();

    updateLatency_l();

    ALOGD("%s(), devices: 0x%x, format: 0x%x/0x%x, channels: 0x%x/0x%x, sampleRate: %d/%d, flags: 0x%x, latency: %d, buffer_size: %d",
          __FUNCTION__,
          devices, *format, mStreamAttributeSource.audio_format,
          *channels, mStreamAttributeSource.num_channels,
          *sampleRate, mStreamAttributeSource.sample_rate, flags,
          mStreamAttributeSource.latency, mStreamAttributeSource.buffer_size);

    return *status;
}


uint32_t AudioALSAStreamOut::sampleRate() const {
    ALOGV("%s(), return %d", __FUNCTION__, mStreamAttributeSource.sample_rate);
    return mStreamAttributeSource.sample_rate;
}


size_t AudioALSAStreamOut::bufferSize() const {
    ALOGV("%s(), return %d, flag %x", __FUNCTION__, mStreamAttributeSource.buffer_size,
          mStreamAttributeSource.mAudioOutputFlags);
    return mStreamAttributeSource.buffer_size;
}

uint32_t AudioALSAStreamOut::bufferSizeTimeUs() const {
    uint32_t bufferTime = 0;

    const uint8_t size_per_channel = (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_8_BIT ? 1 :
                                      (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_16_BIT ? 2 :
                                       (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_32_BIT ? 4 :
                                        (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_8_24_BIT ? 4 :
                                         2))));
    const uint8_t size_per_frame = mStreamAttributeSource.num_channels * size_per_channel;
    bufferTime = (mStreamAttributeSource.buffer_size * 1000 * 1000) / (mStreamAttributeSource.sample_rate * size_per_frame);

    ALOGV("%s(), return %d", __FUNCTION__, bufferTime);
    return bufferTime;
}

uint32_t AudioALSAStreamOut::channels() const {
    ALOGV("%s(), return 0x%x", __FUNCTION__, mStreamAttributeSource.audio_channel_mask);
    return mStreamAttributeSource.audio_channel_mask;
}


int AudioALSAStreamOut::format() const {
    ALOGV("%s(), return 0x%x", __FUNCTION__, mStreamAttributeSource.audio_format);
    return mStreamAttributeSource.audio_format;
}


uint32_t AudioALSAStreamOut::latency() {
    ALOGV("%s(), flags %d, return %d", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, mLatency);
    return mLatency;
}

void AudioALSAStreamOut::updateLatency_l() {
    ASSERT(AL_TRYLOCK(mLock) != 0);

    if (mPlaybackHandler == NULL) {
        mLatency = mStreamAttributeSource.latency;
    } else {
        int latencyPlayHandler = mPlaybackHandler->getLatency();
        if (latencyPlayHandler > 0) {
            mLatency = (uint32_t)latencyPlayHandler;
        } else {
            const stream_attribute_t *pStreamAttributeTarget = mPlaybackHandler->getStreamAttributeTarget();
            const uint8_t size_per_channel = (pStreamAttributeTarget->audio_format == AUDIO_FORMAT_PCM_8_BIT ? 1 :
                                              (pStreamAttributeTarget->audio_format == AUDIO_FORMAT_PCM_16_BIT ? 2 :
                                               (pStreamAttributeTarget->audio_format == AUDIO_FORMAT_PCM_32_BIT ? 4 :
                                                (pStreamAttributeTarget->audio_format == AUDIO_FORMAT_PCM_8_24_BIT ? 4 :
                                                 2))));
            const uint8_t size_per_frame = pStreamAttributeTarget->num_channels * size_per_channel;

            mLatency = (pStreamAttributeTarget->buffer_size * 1000) / (pStreamAttributeTarget->sample_rate * size_per_frame) +
                      pStreamAttributeTarget->dspLatency;
        }
    }
}

status_t AudioALSAStreamOut::setVolume(float left, float right) {
    if (left < 0.0f || left > 1.0f || isnan(left) ||
        right < 0.0f || right > 1.0f || isnan(right)) {
        ALOGE("%s(), invalid volume, left %f, right %f", __FUNCTION__, left, right);
        return BAD_VALUE;
    }

    if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        return INVALID_OPERATION;
    }

    uint32_t ret = NO_ERROR;

    ALOGV("%s(), vl = %lf, vr = %lf", __FUNCTION__, left, right);

    // make as 8_24 fotmat
    uint32_t vl = (uint32_t)(left * (1 << 24));
    uint32_t vr = (uint32_t)(right * (1 << 24));
    mOffloadVol = vl;

    if (mPlaybackHandler != NULL) {
        ret = mPlaybackHandler->setVolume(vl);
        return ret;
    } else if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        ret = INVALID_OPERATION;
    }

    ALOGE("%s(), playbackhandler NULL, ret = %d", __FUNCTION__, ret);
    return ret;
}


ssize_t AudioALSAStreamOut::write(const void *buffer, size_t bytes) {
    ALOGV("%s(), buffer = %p, bytes = %zu, flags %d", __FUNCTION__, buffer, bytes, mStreamAttributeSource.mAudioOutputFlags);

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    /* suspend IEMs when voip open */
    if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        if (mSuspendIEMsForVoIP == false) {
            mSuspendIEMsForVoIP = true;
            AudioIEMsController::getInstance()->suspend(IEMS_SUSPEND_USER_VOIP_RX);
        }
    }
#endif

    if ((mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_INCALL_MUSIC)  && (mStreamManager->isModeInPhoneCall() == false)) {
        ALOGW("%s(), streamout flag:0x%x should only write data during phonecall, return",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags);
        return bytes;
    }

#if defined(PRIMARY_USB)
    if (audio_is_usb_out_device(mStreamAttributeSource.output_devices) &&
        mStreamManager->isPhoneCallOpen() == false &&
        AudioUSBCenter::getInstance()->getUSBConnectionState(mStreamAttributeSource.output_devices) == false) {
        if (mStandby == false) {
            ALOGD("%s(), usb disconn.. standby", __FUNCTION__);
            standby(false); // disconnect from policy... use policy ver standby
        }
        usleep(getBufferLatencyUs(&mStreamAttributeSource, bytes));
        mPresentedBytes += bytes;
        return bytes;
    }
#endif

#ifdef MTK_LATENCY_DETECT_PULSE
    stream_attribute_t *attribute = &mStreamAttributeSource;
    AudioDetectPulse::doDetectPulse(TAG_STREAMOUT, PULSE_LEVEL, 0, (void *)buffer,
                                    bytes, attribute->audio_format, attribute->num_channels,
                                    attribute->sample_rate);
#endif

#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
    bool isDspRecovery = false;
    if (isAdspRecoveryEnable()) {
        LOCK_ADSP_RLOCK(mAdspRecoveryLock);
        isDspRecovery = get_audio_dsp_recovery_mode();
    }
#endif

    if (mStandby && mSuspendCount == 0
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
        && isDspRecovery == false
#endif
       ) {
        // update policy device without holding stream out lock.
        mStreamManager->updateOutputDeviceForAllStreamIn(mStreamAttributeSource.output_devices);

        if (mStreamAttributeSource.usePolicyDevice) {
            mStreamAttributeSource.output_devices = mStreamAttributeSource.policyDevice;
            mStreamAttributeSource.usePolicyDevice = false;
            mStreamManager->syncSharedOutDevice(mStreamAttributeSource.output_devices, this);
        }

        // set volume before open device to avoid pop, and without holding stream out lock.
        mStreamManager->setMasterVolume(mStreamManager->getMasterVolume(), getIdentity());
    }


    /* fast output is RT thread and keep streamout lock for write kernel.
       so other thread can't get streamout lock. if necessary, output will active release CPU. */
    int tryCount = 10;
    while (mLockCount && tryCount--) {
        ALOGD_IF(tryCount == 0 && (mLogEnable&AUD_OUT_WRITE_LOG), "%s, free CPU, mLockCount %d, tryCount %d", __FUNCTION__, mLockCount, tryCount);
        usleep(300);
    }

    AL_AUTOLOCK(mSuspendLock);

    size_t outputSize = 0;
    if (mSuspendCount > 0 ||
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
        isDspRecovery ||
#endif
        (mStreamOutType == STREAM_OUT_HDMI_STEREO && mSuspendStreamOutHDMIStereoCount > 0) ||
        (mStreamManager->isPhoneCallOpen() == true && mStreamOutType  != STREAM_OUT_PRIMARY && mStreamOutType  != STREAM_OUT_VOICE_DL)) {
        // here to sleep a buffer size latency and return.
        ALOGV("%s(), mStreamOutType = %d, mSuspendCount = %u, mSuspendStreamOutHDMIStereoCount = %d",
              __FUNCTION__, mStreamOutType, mSuspendCount, mSuspendStreamOutHDMIStereoCount);
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
        if (isAdspRecoveryEnable()) {
            UNLOCK_ADSP_RLOCK(mAdspRecoveryLock);
        }
#endif
        usleep(bufferSizeTimeUs());
        mPresentedBytes += bytes;
        return bytes;
    }
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
    if (isAdspRecoveryEnable()) {
        UNLOCK_ADSP_RLOCK(mAdspRecoveryLock);
    }
#endif

    AL_AUTOLOCK(mLock);

    status_t status = NO_ERROR;

    /// check open
    if (mStandby == true) {
        status = open();
        setScreenState_l();

        if (mPlaybackHandler == NULL) {
            ASSERT(false);
            return -EINVAL;
        }

#if defined(MTK_AUDIODSP_SUPPORT)
        if (mPlaybackHandler->getPlaybackHandlerType() == PLAYBACK_HANDLER_OFFLOAD &&
            status != NO_ERROR) {
            mStreamCbk(STREAM_CBK_EVENT_ERROR, 0, mCbkCookie);
            return 0;
        }
#endif
        mPlaybackHandler->setFirstDataWriteFlag(true);
        mStreamManager->logStreamDumpSize();
    } else {
        if (mPlaybackHandler == NULL) {
            ASSERT(false);
            return -EINVAL;
        }
        mPlaybackHandler->setFirstDataWriteFlag(false);
    }

    if (bytes == 0) {
        return 0;
    }
    if (mPlaybackHandler->getPlaybackHandlerType() != PLAYBACK_HANDLER_OFFLOAD) {
        writeWavDumpData(buffer, bytes);
    } else {
        WritePcmDumpData(buffer, bytes);
    }

    dataProcessForMute(buffer, bytes);

    /// write pcm data
    mPlaybackHandler->preWriteOperation(buffer, bytes);
    outputSize = mPlaybackHandler->write(buffer, bytes);

    mPaused = false;
    mPresentedBytes += outputSize;
    //ALOGD("%s(), outputSize = %zu, bytes = %zu, mPresentedBytes=%" PRIu64, __FUNCTION__, outputSize, bytes, mPresentedBytes);
    return outputSize;
}

status_t AudioALSAStreamOut::standby(bool halRequest) {
    // standby is used by framework to close stream out.
    ALOGD("%s(), flag %d", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags);
    status_t status = NO_ERROR;

    mStreamAttributeSource.usePolicyDevice = true;
    status = standbyStreamOut(halRequest);

#if defined(HAVE_IEMS) && defined(HAVE_SW_MIXER)
    /* resume IEMs when voip close */
    if (!halRequest && mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        if (mSuspendIEMsForVoIP == true) {
            mSuspendIEMsForVoIP = false;
            AudioIEMsController::getInstance()->resume(IEMS_SUSPEND_USER_VOIP_RX);
        }
    }
#endif

    return status;
}

status_t AudioALSAStreamOut::syncPolicyDevice() {
    mStreamAttributeSource.usePolicyDevice = true;
    return NO_ERROR;
}

status_t AudioALSAStreamOut::standbyStreamOut(bool halRequest) {
    ALOGD("%s(), halRequest %d, mDestroy %d, flag %d", __FUNCTION__, halRequest,
          mDestroy, mStreamAttributeSource.mAudioOutputFlags);
    status_t status = NO_ERROR;
    int oldCount;

    oldCount = android_atomic_inc(&mLockCount);
    AL_AUTOLOCK(mSuspendLock);
    AL_AUTOLOCK(mLock);
    oldCount = android_atomic_dec(&mLockCount);

    status = standby_l(halRequest);

    return status;
}

status_t AudioALSAStreamOut::standby_l(bool halRequest) {
    // call standby_l() only when mLock mSuspendLock is locked.
    ASSERT(AL_TRYLOCK(mLock) != 0);
    ASSERT(AL_TRYLOCK(mSuspendLock) != 0);

    status_t status = NO_ERROR;

    // mmap don't support hal standby
    if ((halRequest == true) && (mDestroy == false) && (mStreamAttributeSource.mAudioOutputFlags &
                                                        AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        return status;
    }

    /// check close
    if (mStandby == false) {
        if ((halRequest == true) && (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
            int tryLockRet = TRYLOCK_ADSP_RLOCK(mAdspRecoveryLock);
            if (get_audio_dsp_recovery_mode() == true) {
                mStreamCbk(STREAM_CBK_EVENT_ERROR, 0, mCbkCookie);
            } else
#endif
            {
                ALOGD("%s(), offload mAudioOutputFlags = %d, reopen by framework", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags);
                mPlaybackHandler->setOffloadRoutingFlag(true);
                status = close();
            }
#if defined(MTK_AUDIO_DSP_RECOVERY_SUPPORT)
            if (tryLockRet == 0) {
                UNLOCK_ADSP_RLOCK(mAdspRecoveryLock);
            }
#endif
        } else {
            status = close();
        }
    }

    return status;
}

status_t AudioALSAStreamOut::dump(int fd __unused, const Vector<String16> &args __unused) {
    ALOGV("%s()", __FUNCTION__);
    return NO_ERROR;
}

int AudioALSAStreamOut::setStreamOutOutputFlags(const audio_output_flags_t flag, const bool enable) {
    AL_AUTOLOCK(mLock);
    int ret = 0;
    audio_output_flags_t oriFlag = mStreamAttributeSource.mAudioOutputFlags;
    if (enable) {
        mStreamAttributeSource.mAudioOutputFlags =
            (audio_output_flags_t)(mStreamAttributeSource.mAudioOutputFlags | flag);
    } else {
        mStreamAttributeSource.mAudioOutputFlags =
            (audio_output_flags_t)(mStreamAttributeSource.mAudioOutputFlags & ~flag);
    }
    ALOGD("%s(), flag: 0x%x -> 0x%x",
          __FUNCTION__, oriFlag, mStreamAttributeSource.mAudioOutputFlags);
    return ret;
}

int AudioALSAStreamOut::setStreamOutSampleRate(const uint32_t sampleRate) {
    AL_AUTOLOCK(mLock);
    int ret = 0;
    /* primary and deep buffer support hifi playback */
    if (((mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY) ||
         (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) > 0) {
        mStreamAttributeSource.sample_rate = sampleRate;
        ALOGD("%s(), flag:0x%x, HIFI_AUDIO_SAMPLERATE = %u",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, sampleRate);
    }
    return ret;
}

bool AudioALSAStreamOut::SampleRateSupport(uint32_t sampleRate) {
    // ALPS02409284, fast output don't support HIFI
#ifdef DOWNLINK_LOW_LATENCY
    if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST) {
        return (sampleRate == 44100 || sampleRate == 48000) ? true : false;
    } else if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        return (sampleRate == 48000) ? true : false;
    }
#endif

    if (sampleRate == 8000  || sampleRate == 11025 || sampleRate == 12000
        || sampleRate == 16000 || sampleRate == 22050 || sampleRate == 24000
        || sampleRate == 32000 || sampleRate == 44100 || sampleRate == 48000
        || sampleRate == 88200 || sampleRate == 96000 || sampleRate == 176400 || sampleRate == 192000) {
        return true;
    } else {
        return false;
    }
}

status_t AudioALSAStreamOut::UpdateSampleRate(int sampleRate) {
    ALOGD("%s() sampleRate = %d", __FUNCTION__, sampleRate);
    // check sample rate
    if (SampleRateSupport(sampleRate) == true) {
        AudioALSASampleRateController::getInstance()->setPrimaryStreamOutSampleRate(sampleRate);
        mStreamAttributeSource.sample_rate = sampleRate;
        setBufferSize();
    } else {
        ALOGE("%s(), wrong sampleRate %d, use %d instead.", __FUNCTION__, sampleRate, kDefaultOutputSourceSampleRate);
        sampleRate = kDefaultOutputSourceSampleRate;
    }
    return NO_ERROR;
}



status_t AudioALSAStreamOut::setParameters(const String8 &keyValuePairs) {
    AudioParameter param = AudioParameter(keyValuePairs);

    /// keys
    const String8 keyRouting = String8(AudioParameter::keyRouting);
    const String8 keySampleRate = String8(AudioParameter::keySamplingRate);

    const String8 keyFormat = String8(AudioParameter::keyFormat);
    const String8 keyDynamicSampleRate = String8("DynamicSampleRate");
    const String8 keyLowLatencyMode = String8("LowLatencyMode");
    const String8 keyDSM = String8("DSM");

    const String8 keySET_SOFTWARE_BRIDGE_ENABLE = String8("SwBridge"); // Hearing Aid Device Phonecall

#ifdef MTK_HIFIAUDIO_SUPPORT
    const String8 keyHiFiState = String8("hifi_state");
#endif

#if defined(MTK_AUDIODSP_SUPPORT)
    const String8 kKeySampleRate = String8("music_offload_sample_rate");
    const String8 kKeyBitRate = String8("music_offload_avg_bit_rate");
#endif

#if 1
    const String8 keyRoutingToNone = String8(AUDIO_PARAMETER_KEY_ROUTING_TO_NONE);
    const String8 keyFmDirectControl = String8(AUDIO_PARAMETER_KEY_FM_DIRECT_CONTROL);
#else
    const String8 keyRoutingToNone = String8(AudioParameter::keyRoutingToNone);
    const String8 keyFmDirectControl = String8(AudioParameter::keyFmDirectControl);
#endif
    /// parse key value pairs
    status_t status = NO_ERROR;
    int value = 0;
    String8 value_str;
    int oldCount;

    // routing
    if (param.getInt(keyRouting, value) == NO_ERROR) {
        param.remove(keyRouting);

        if (mStreamAttributeSource.output_devices == AUDIO_DEVICE_OUT_TELEPHONY_TX) {
            ALOGD("%s(), bypass routing device:0x%x, mAudioOutputFlags: 0x%x",
                  __FUNCTION__, value, mStreamAttributeSource.mAudioOutputFlags);
            return NO_ERROR;
        }

        oldCount = android_atomic_inc(&mLockCount);
        if (mStreamOutType == STREAM_OUT_PRIMARY || mStreamOutType == STREAM_OUT_VOICE_DL) {
            updatePolicyDevice(static_cast<audio_devices_t>(value));
            status = mStreamManager->routingOutputDevice(this, mStreamAttributeSource.output_devices, static_cast<audio_devices_t>(value));

            if (mMuteForRouting) {
                setMuteForRouting(false);
            }
        } else if ((mStreamOutType == STREAM_OUT_HDMI_STEREO) || (mStreamOutType == STREAM_OUT_HDMI_MULTI_CHANNEL)) {
            ALOGD("%s(), HDMI  \"%s\"", __FUNCTION__, param.toString().string());
            updatePolicyDevice(static_cast<audio_devices_t>(value));
            status = mStreamManager->routingOutputDevice(this, mStreamAttributeSource.output_devices, static_cast<audio_devices_t>(value));
        } else {
            ALOGW("%s(), NUM_STREAM_OUT_TYPE \"%s\"", __FUNCTION__, param.toString().string());
            status = INVALID_OPERATION;
        }
        oldCount = android_atomic_dec(&mLockCount);
    } else {
        ALOGD("+%s(): flag %d, %s", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, keyValuePairs.string());
    }

    if (param.getInt(keyFmDirectControl, value) == NO_ERROR) {
        param.remove(keyFmDirectControl);

        oldCount = android_atomic_inc(&mLockCount);
        AL_AUTOLOCK(mLock);
        AudioALSAFMController::getInstance()->setUseFmDirectConnectionMode(value ? true : false);
        oldCount = android_atomic_dec(&mLockCount);
    }
    // routing none, for no stream but has device change. e.g. vow path change
    if (param.getInt(keyRoutingToNone, value) == NO_ERROR) {
        param.remove(keyRoutingToNone);

        oldCount = android_atomic_inc(&mLockCount);
        AL_AUTOLOCK(mLock);
        status = mStreamManager->DeviceNoneUpdate();
        oldCount = android_atomic_dec(&mLockCount);
    }
    /// sample rate
    if (param.getInt(keyDynamicSampleRate, value) == NO_ERROR) {
        param.remove(keyDynamicSampleRate);
        oldCount = android_atomic_inc(&mLockCount);
        AL_AUTOLOCK(mLock);
        if (mStreamOutType == STREAM_OUT_PRIMARY || mStreamOutType == STREAM_OUT_VOICE_DL) {
            status = NO_ERROR; //AudioALSASampleRateController::getInstance()->setPrimaryStreamOutSampleRate(value); // TODO(Harvey): enable it later
        } else {
            ALOGW("%s(), HDMI bypass \"%s\"", __FUNCTION__, param.toString().string());
            status = INVALID_OPERATION;
        }
        oldCount = android_atomic_dec(&mLockCount);
    }

#if defined(MTK_HIFIAUDIO_SUPPORT)
    if (param.getInt(keySampleRate, value) == NO_ERROR) {
        param.remove(keySampleRate);
        bool bHiFiState = AudioALSAHardwareResourceManager::getInstance()->getHiFiStatus();
        if (bHiFiState) {
            ALOGD("%s(), hifi mode, setAllStreamHiFi, set sampling_rate=%d", __FUNCTION__, value);
            status = mStreamManager->setAllStreamHiFi(this, value);
        } else {
            ALOGD("%s(), non-hifi mode, UpdateSampleRate, set sampling_rate=%d", __FUNCTION__, value);
            oldCount = android_atomic_inc(&mLockCount);
            AL_AUTOLOCK(mLock);
            if (mStandby) {
                UpdateSampleRate(value);
            } else {
                status = INVALID_OPERATION;
            }
            oldCount = android_atomic_dec(&mLockCount);
        }
    }
#else
    if (param.getInt(keySampleRate, value) == NO_ERROR) {
        param.remove(keySampleRate);
        oldCount = android_atomic_inc(&mLockCount);
        AL_AUTOLOCK(mLock);
        if (mStandby) {
            UpdateSampleRate(value);
        } else {
            status = INVALID_OPERATION;
        }
        oldCount = android_atomic_dec(&mLockCount);
    }
#endif

#if defined(MTK_AUDIODSP_SUPPORT)
    if (param.getInt(kKeySampleRate, value) == NO_ERROR) {
        mStreamAttributeSource.offload_codec_info.codec_samplerate = value;
        param.remove(kKeySampleRate);
    }
    if (param.getInt(kKeyBitRate, value) == NO_ERROR) {
        mStreamAttributeSource.offload_codec_info.codec_bitrate = value;
        param.remove(kKeyBitRate);
    }
#endif

#if defined(MTK_AUDIO_GAIN_TABLE) || defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
    static String8 keyVolumeStreamType    = String8("volumeStreamType");
    static String8 keyVolumeDevice        = String8("volumeDevice");
    static String8 keyVolumeIndex         = String8("volumeIndex");
    if (param.getInt(keyVolumeStreamType, value) == NO_ERROR) {
        int device = 0;
        int index = 0;

        if (param.getInt(keyVolumeDevice, device) == NO_ERROR) {
            if (param.getInt(keyVolumeIndex, index) == NO_ERROR) {
#ifdef MTK_AUDIO_GAIN_TABLE
                mStreamManager->setAnalogVolume(value, device, index, 0);
#endif
#if defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT)
                mStreamManager->setVolumeIndex(value, device, index);
#endif
            }
        }
        mStreamManager->setStreamType(value);

        param.remove(keyVolumeStreamType);
        param.remove(keyVolumeDevice);
        param.remove(keyVolumeIndex);
    }
#endif

#if defined(PRIMARY_USB)
    AudioUSBCenter::getInstance()->outSetParameter(keyValuePairs.string());
#endif

    if (param.get(String8(AUDIO_PARAMETER_DEVICE_CONNECT), value_str) == NO_ERROR) {
        if (param.getInt(String8(AUDIO_PARAMETER_DEVICE_CONNECT), value) == NO_ERROR) {
            audio_devices_t device = (audio_devices_t)value;
            mStreamManager->updateDeviceConnectionState(device, true);
        }

        if (param.get(String8("card"), value_str) == NO_ERROR) {
            param.remove(String8("card"));
        }

        if (param.get(String8("device"), value_str) == NO_ERROR) {
            param.remove(String8("device"));
        }

        param.remove(String8(AUDIO_PARAMETER_DEVICE_CONNECT));
    }

    if (param.get(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT), value_str) == NO_ERROR) {
        if (param.getInt(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT), value) == NO_ERROR) {
            audio_devices_t device = (audio_devices_t)value;
            mStreamManager->updateDeviceConnectionState(device, false);
        }

        if (param.get(String8("card"), value_str) == NO_ERROR) {
            param.remove(String8("card"));
        }

        if (param.get(String8("device"), value_str) == NO_ERROR) {
            param.remove(String8("device"));
        }

        param.remove(String8(AUDIO_PARAMETER_DEVICE_DISCONNECT));
    }

    if (param.getInt(keyFormat, value) == NO_ERROR) {
        param.remove(keyFormat);
        oldCount = android_atomic_inc(&mLockCount);

        AL_AUTOLOCK(mLock);
        status = -ENOSYS;

        oldCount = android_atomic_dec(&mLockCount);
    }
    // Hearing Aid Device Phonecall(HAP)
#if defined(MTK_COMBO_MODEM_SUPPORT) && defined(MTK_BT_HEARING_AID_SUPPORT)
    if (param.getInt(keySET_SOFTWARE_BRIDGE_ENABLE, value) == NO_ERROR) {
        if (appIsFeatureOptionEnabled("MTK_BT_HEARING_AID_SUPPORT")) {
            ALOGD("%s(), SwBridge = %d", __FUNCTION__, value);
            mStreamManager->setSoftwareBridgeEnable(value);
            param.remove(keySET_SOFTWARE_BRIDGE_ENABLE);
        }
    }
#endif

    if (param.size()) {
        ALOGW("%s(), still have param.size() = %zu, remain param = \"%s\"",
              __FUNCTION__, param.size(), param.toString().string());
        status = BAD_VALUE;
    }

    ALOGV("-%s(): %s ", __FUNCTION__, keyValuePairs.string());
    return status;
}


String8 AudioALSAStreamOut::getParameters(const String8 &keys) {
    ALOGD("%s, keyvalue %s", __FUNCTION__, keys.string());

    String8 value = String8();
    AudioParameter param = AudioParameter(keys);
    AudioParameter returnParam = AudioParameter();

#if defined(PRIMARY_USB)
    char *result_str = NULL;
    if (audio_is_usb_out_device(mStreamAttributeSource.output_devices)) {
        result_str = AudioUSBCenter::getInstance()->outGetParameter(keys.string());
        if (result_str) {
            value = result_str;
            ALOGD("-%s(), return \"%s\"", __FUNCTION__, value.string());
            free(result_str);
            result_str = NULL;
        }
        return value;
    }
#endif

    struct str_parms *query = str_parms_create_str(keys.string());
    if (query == NULL) {
        ALOGE("-%s(), keyvalue %s, query == NULL", __FUNCTION__, keys.string());
        return value;
    }

    String8 keyGetParam;
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        keyGetParam = String8(AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES);

        if (mStreamAttributeSource.output_devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            value = mStreamManager->getTVOutCapability(AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES);
        }
        returnParam.add(keyGetParam, value);
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        keyGetParam = String8(AUDIO_PARAMETER_STREAM_SUP_CHANNELS);

        if (mStreamAttributeSource.output_devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            value = mStreamManager->getTVOutCapability(AUDIO_PARAMETER_STREAM_SUP_CHANNELS);
        }
        returnParam.add(keyGetParam, value);
    }

    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        keyGetParam = String8(AUDIO_PARAMETER_STREAM_SUP_FORMATS);

        if (mStreamAttributeSource.output_devices == AUDIO_DEVICE_OUT_AUX_DIGITAL) {
            value = mStreamManager->getTVOutCapability(AUDIO_PARAMETER_STREAM_SUP_FORMATS);
        } else {
            std::string literalFormat;
            audio_format_t format = AUDIO_FORMAT_DEFAULT;
            if (mPlaybackHandler == NULL || mStandby) {
                format = mStreamAttributeSource.audio_format;
            } else {
                const stream_attribute_t *pStreamAttributeTarget = mPlaybackHandler->getStreamAttributeTarget();
                format = pStreamAttributeTarget->audio_format;
            }
            FormatConverter::toString(format, literalFormat);
            value = literalFormat.c_str();
        }
        returnParam.add(keyGetParam, value);
    }

    str_parms_destroy(query);

    const String8 keyValuePairs = returnParam.toString();
    ALOGD("-%s(), return \"%s\"", __FUNCTION__, keyValuePairs.string());
    return keyValuePairs;
}

int AudioALSAStreamOut::getRenderPosition(uint32_t *dspFrames) {
    if (mPlaybackHandler == NULL) {
        ALOGE("%s() handler NULL, frames: %" PRIu64 "", __FUNCTION__, mPresentFrames);
        *dspFrames = mPresentFrames;
        return -ENOSYS;
    }

    if (!isAdspOptionEnable()) {
        return -ENOSYS;
    }

#if defined(MTK_AUDIODSP_SUPPORT)
    // only supoort for offload
    if (mPlaybackHandler->getPlaybackHandlerType() != PLAYBACK_HANDLER_OFFLOAD) {
        ALOGE("%s() getPlaybackHandlerType[%d]", __FUNCTION__, mPlaybackHandler->getPlaybackHandlerType());
        return -ENOSYS;
    }
    unsigned long codec_io_frame;
    unsigned int codec_samplerate, frameNum;
    unsigned long long a2dpremotedelayms, a2dpdelay;

    if (NO_ERROR == mPlaybackHandler->get_timeStamp(&codec_io_frame, &codec_samplerate)) {
        if (codec_samplerate == 0) {
            ALOGE("%s(), Compress Not Ready", __FUNCTION__);
            return -ENODATA;
        }
        *dspFrames = codec_io_frame;
        mPresentFrames = codec_io_frame;

        // a2dp device frame calculate
        if (mStreamAttributeSource.output_devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
            a2dpremotedelayms = AudioDspStreamManager::getInstance()->getA2dpRemoteDelayus();
            a2dpdelay = AudioALSAHardwareResourceManager::getInstance()->getA2dpLatency();
            frameNum = (a2dpdelay * codec_samplerate) / 1000;
            ALOGD_IF((mLogEnable&AUD_OUT_OP_LOG), "%s a2dpremotedelayms[%llu] a2dpdelay[%llu] frameNum[%u]",
                     __func__, a2dpremotedelayms, a2dpdelay, frameNum);
        } else {
            frameNum = AudioDspStreamManager::getInstance()->getDspSample();
        }

        if (*dspFrames > frameNum)
            *dspFrames -= frameNum;
        else
            *dspFrames =  0;
        ALOGD_IF((mLogEnable&AUD_OUT_OP_LOG), "%s codec_samplerate[%u] dspFrames[%u] %" PRIu64 "",
                  __FUNCTION__, codec_samplerate, *dspFrames, mPresentFrames);
        return 0;
    } else {
        *dspFrames = mPresentFrames;
        ALOGE("%s(), get_tstamp fail, frame:%" PRIu64 "", __FUNCTION__, mPresentFrames);
        return -ENODATA;
    }
#endif
    return -ENOSYS;
}

#define AUD_VAL_DIFF(x,y) (((x) > (y)) ? ((x) - (y)) : ((y) - (x)))

int AudioALSAStreamOut::getPresentationPosition(uint64_t *frames, struct timespec *timestamp) {
    int ret = 0;
    uint64_t valDiff = 0;

    ALOGV("+%s()", __FUNCTION__);
    AL_AUTOLOCK(mLock);

    time_info_struct_t HW_Buf_Time_Info;
    memset(&HW_Buf_Time_Info, 0, sizeof(HW_Buf_Time_Info));

    const uint8_t size_per_channel = (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_8_BIT ? 1 :
                                      (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_16_BIT ? 2 :
                                       (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_32_BIT ? 4 :
                                        2)));
    ALOGV("%s(), size_per_channel = %u, mStreamAttributeSource.num_channels = %d, mStreamAttributeSource.audio_channel_mask = %x,mPresentedBytes = %" PRIu64 "\n",
          __FUNCTION__, size_per_channel, mStreamAttributeSource.num_channels, mStreamAttributeSource.audio_channel_mask, mPresentedBytes);

    if (mPlaybackHandler == NULL) {
        *frames = mPresentedBytes / (uint64_t)(mStreamAttributeSource.num_channels * size_per_channel);
        timestamp->tv_sec = 0;
        timestamp->tv_nsec = 0;
        ALOGV("-%s(), no playback handler, *frames = %" PRIu64 ", return", __FUNCTION__, *frames);
        return NOT_ENOUGH_DATA;
    }

    /* bypass calculation to sw mixer */
    if (mPlaybackHandler->getPlaybackHandlerType() == PLAYBACK_HANDLER_MIXER &&
        !audio_is_bluetooth_out_sco_device(mStreamAttributeSource.output_devices)) {
        ret = mPlaybackHandler->getPresentationPosition(
                     mPresentedBytes / getSizePerFrameByAttr(&mStreamAttributeSource),
                     frames,
                     timestamp);
        if (ret == 0 && getPlaybackTimestampLogOn()) {
            if (mPreviousFrames != 0) {
                uint64_t timeDiffNs = 0;
                uint64_t frameDiff = 0;

                timeDiffNs = get_time_diff_ns(&mPretimestamp, timestamp);
                frameDiff = (timeDiffNs * mStreamAttributeSource.sample_rate) / NSEC_PER_SEC;
                valDiff = AUD_VAL_DIFF(mPreviousFrames + frameDiff, *frames);

               if (valDiff > 256)
                    ALOGW("%s(), timestamp %ld.%09ld => %ld.%09ld, timeDiffNs %" PRIu64 "ns, frameDiff %" PRIu64 ", mPreviousFrames %" PRIu64 ", sum %" PRIu64 " VS *frames %" PRIu64 ", diff %" PRIu64,
                          __FUNCTION__, (long)mPretimestamp.tv_sec, (long)mPretimestamp.tv_nsec, (long)timestamp->tv_sec, (long)timestamp->tv_nsec, timeDiffNs, frameDiff, mPreviousFrames, mPreviousFrames + frameDiff, *frames, valDiff);
            }
            mPreviousFrames = *frames;
            memcpy(&mPretimestamp, timestamp, sizeof(mPretimestamp));
        }
        return ret < 0 ? NOT_ENOUGH_DATA : NO_ERROR;
    }

    //query remaining hardware buffer size
#if defined(MTK_AUDIODSP_SUPPORT)
    if (isAdspOptionEnable() &&
        mPlaybackHandler->getPlaybackHandlerType() == PLAYBACK_HANDLER_OFFLOAD) {
        uint32_t dspFrames;
        ret = getRenderPosition(&dspFrames);
        if (ret) {
            ALOGE("%s(), get_tstamp fail, frame:%" PRIu64 "", __FUNCTION__, mPresentFrames);
            timestamp->tv_sec = 0;
            timestamp->tv_nsec = 0;
            return ret < 0 ? NOT_ENOUGH_DATA : NO_ERROR;
        }
        *frames = dspFrames;
        /* this is the best we can do - comment from Q */
        clock_gettime(CLOCK_MONOTONIC, timestamp);
        ALOGD_IF((mLogEnable & AUD_OUT_OP_LOG), "%s(), get_tstamp done:%" PRIu64 "", __FUNCTION__, *frames);
    } else
#endif
    {
        if (mPlaybackHandler->getHardwareBufferInfo(&HW_Buf_Time_Info) != NO_ERROR) {
            *frames = mPresentedBytes / (uint64_t)(mStreamAttributeSource.num_channels * size_per_channel);
            ret = NOT_ENOUGH_DATA;
            ALOGV("-%s(), getHardwareBufferInfo fail, *frames = %" PRIu64 ", return -EINVAL", __FUNCTION__, *frames);
        } else {
            uint64_t presentedFrames = mPresentedBytes / (uint64_t)(mStreamAttributeSource.num_channels * size_per_channel);
            unsigned int remainExtra = mPlaybackHandler->getPlaygbackExBuffer();

            const stream_attribute_t *pStreamAttributeTarget = mPlaybackHandler->getStreamAttributeTarget();

            uint64_t remainInKernel = (uint64_t)(HW_Buf_Time_Info.buffer_per_time - HW_Buf_Time_Info.frameInfo_get);
            remainInKernel = (remainInKernel * mStreamAttributeSource.sample_rate) / pStreamAttributeTarget->sample_rate;

            long long remainInHal = HW_Buf_Time_Info.halQueuedFrame;
            remainInHal = (remainInHal * mStreamAttributeSource.sample_rate) / pStreamAttributeTarget->sample_rate;

            if (presentedFrames < (remainInKernel + remainInHal + remainExtra)) {
                *frames = presentedFrames;
                *timestamp = HW_Buf_Time_Info.timestamp_get;
                ret = NOT_ENOUGH_DATA;
                ALOGW("-%s(), timestamp invalid, remainInKernel %" PRIu64 ", remainInHal %lld, remainExtra %u presentedFrames %" PRIu64 ", return -EINVAL",
                      __FUNCTION__, remainInKernel, remainInHal, remainExtra, presentedFrames);
            } else {
                *frames = (uint64_t)presentedFrames - remainInKernel - remainInHal - remainExtra;
                *timestamp = HW_Buf_Time_Info.timestamp_get;
                /* frame is not update , do compenstate */
                if (mPreviousFrames && mPreviousFrames >= *frames ) {
                    uint64_t deltaSec =  HW_Buf_Time_Info.sys_timestamp.tv_sec - HW_Buf_Time_Info.timestamp_get.tv_sec;
                    uint64_t delta, diffFrames, deltaNanoSec;

                    /* handle for normal or increase to 1 sec unit*/
                    if (deltaSec) {
                        deltaNanoSec = HW_Buf_Time_Info.timestamp_get.tv_nsec - HW_Buf_Time_Info.sys_timestamp.tv_nsec;
                        delta = NSEC_PER_SEC - deltaNanoSec;
                    }
                    else {
                        deltaNanoSec = HW_Buf_Time_Info.sys_timestamp.tv_nsec - HW_Buf_Time_Info.timestamp_get.tv_nsec;
                        delta = deltaNanoSec;
                    }
                    diffFrames = (delta * mStreamAttributeSource.sample_rate) / NSEC_PER_SEC;

                    *frames += diffFrames;
                    timespec_add_ns(&HW_Buf_Time_Info.timestamp_get, delta);
                    *timestamp = HW_Buf_Time_Info.timestamp_get;
                    ALOGV("-%s(), deltaSec %" PRIu64 ", deltaNanoSec %" PRIu64 ", diffFrames %" PRIu64 " delta %" PRIu64 " frames %" PRIu64 "",
                      __FUNCTION__, deltaSec, deltaNanoSec, diffFrames, delta, *frames);
                }

                ret = NO_ERROR;
                ALOGD_IF((mLogEnable&AUD_OUT_OP_LOG), "-%s(), flags %d, *frames = %" PRIu64 ", timestamp %ld.%09ld, remainInKernel %" PRIu64 ", remainInHal %lld, presentedFrames %" PRIu64 ", mPreviousFrames %" PRIu64 " ret[%d]",
                         __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, *frames, (long)timestamp->tv_sec, (long)timestamp->tv_nsec, remainInKernel, remainInHal, presentedFrames, mPreviousFrames, ret);
                mPreviousFrames = *frames;
                memcpy(&mPretimestamp, &HW_Buf_Time_Info.sys_timestamp, sizeof(struct timespec));
            }
        }
    }

    return ret;
}

void AudioALSAStreamOut::updateSourceMetadata(const struct source_metadata* source_metadata) {
#ifdef MTK_AUDIODSP_SUPPORT
    if (isAdspOptionEnable() && source_metadata != NULL) {
        AudioDspStreamManager::getInstance()->updateSourceMetadata(source_metadata, getStreamAttribute()->mAudioOutputFlags);
    }
#else
    ALOGV("%s(), do nothing source_metadata = %p", __FUNCTION__, source_metadata);
#endif
}

status_t AudioALSAStreamOut::getNextWriteTimestamp(int64_t *timestamp __unused) {
    return INVALID_OPERATION;
}

status_t AudioALSAStreamOut::setCallBack(stream_callback_t callback, void *cookie) {
    //TODO : new for KK
    mStreamCbk = callback;
    mCbkCookie = cookie;
    return NO_ERROR;
}

status_t AudioALSAStreamOut::open() {
    // call open() only when mLock is locked.
    ASSERT(AL_TRYLOCK(mLock) != 0);

    ALOGD("%s(), flags %d", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags);

    status_t status = NO_ERROR;

#if defined(MTK_POWERHAL_AUDIO_DL_LATENCY)
    if ((mStreamAttributeSource.mAudioOutputFlags & (AUDIO_OUTPUT_FLAG_FAST | AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) &&
        mStreamAttributeSource.mPowerHalEnable) {
        power_hal_hint(POWERHAL_LATENCY_DL, true);
    }
#endif


    if (mStandby == true) {
        // HDMI stereo + HDMI multi-channel => disable HDMI stereo
        if (mStreamOutType == STREAM_OUT_HDMI_MULTI_CHANNEL) {
            setSuspendStreamOutHDMIStereo(true);
            if (mStreamOutHDMIStereo != NULL) {
                ALOGD("mStreamOutHDMIStereo->standby");
                mStandby = false;
                mStreamOutHDMIStereo->standbyStreamOut();
            }
        }

        AudioALSASampleRateController::getInstance()->setScenarioStatus(PLAYBACK_SCENARIO_STREAM_OUT);

        // get input_device
        mStreamAttributeSource.input_device =
            AudioALSASpeechPhoneCallController::getInstance()->getInputDeviceForPhoneCall(mStreamAttributeSource.output_devices);

        // create playback handler
        ASSERT(mPlaybackHandler == NULL);
        mPlaybackHandler = mStreamManager->createPlaybackHandler(&mStreamAttributeSource);
        if (mPlaybackHandler) {
            // open audio hardware
            status = mPlaybackHandler->open();
#if defined(MTK_AUDIODSP_SUPPORT)
            // offload allow return fail
            if (isAdspOptionEnable() &&
                mPlaybackHandler->getPlaybackHandlerType() == PLAYBACK_HANDLER_OFFLOAD) {
                if (status == NO_ERROR) {
                    mPlaybackHandler->setComprCallback(mStreamCbk, mCbkCookie);
                    mPlaybackHandler->setVolume(mOffloadVol);
                }
            } else
#endif
            {
                ASSERT(status == NO_ERROR);
            }

            if (mPlaybackHandler->getPlaybackHandlerType() != PLAYBACK_HANDLER_OFFLOAD) {
                openWavDump(LOG_TAG);
            } else {
                OpenPCMDump(LOG_TAG);
            }

            mStandby = false;
        } else {
            ASSERT(mPlaybackHandler != NULL);
            status = -ENODEV;
        }

        updateLatency_l();
    }

    return status;
}

status_t AudioALSAStreamOut::close() {
    // call close() only when mSuspendLock & mLock is locked.
    ASSERT(AL_TRYLOCK(mSuspendLock) != 0);
    ASSERT(AL_TRYLOCK(mLock) != 0);

    ALOGD("%s(), flags %d", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags);

    status_t status = NO_ERROR;

    if (mStandby == false) {
        // HDMI stereo + HDMI multi-channel => disable HDMI stereo
        if (mStreamOutType == STREAM_OUT_HDMI_MULTI_CHANNEL) {
            setSuspendStreamOutHDMIStereo(false);
        }

        // close audio hardware
        if (mPlaybackHandler != NULL) {
            if (mPlaybackHandler->getPlaybackHandlerType() != PLAYBACK_HANDLER_OFFLOAD) {
                closeWavDump();
            } else {
                ClosePCMDump();
            }
            status = mPlaybackHandler->close();
            if (status != NO_ERROR) {
                ALOGE("%s(), close() fail!!", __FUNCTION__);
            }

            // destroy playback handler
            mStreamManager->destroyPlaybackHandler(mPlaybackHandler);
            mPlaybackHandler = NULL;
        } else {
            ASSERT(false);
            ALOGE("%s(), mPlaybackHandler == NULL", __FUNCTION__);
        }

        AudioALSASampleRateController::getInstance()->resetScenarioStatus(PLAYBACK_SCENARIO_STREAM_OUT);

        mStandby = true;
        mPreviousFrames = 0;
        memset(&mPretimestamp, 0, sizeof(struct timespec));
        setMuteForRouting(false);
        updateLatency_l();
    }

#if defined(MTK_POWERHAL_AUDIO_DL_LATENCY)
    if ((mStreamAttributeSource.mAudioOutputFlags & (AUDIO_OUTPUT_FLAG_FAST | AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) &&
        mStreamAttributeSource.mPowerHalEnable) {
        power_hal_hint(POWERHAL_LATENCY_DL, false);
    }
#endif

    ASSERT(mPlaybackHandler == NULL);
    return status;
}

int AudioALSAStreamOut::pause() {
    ALOGD("%s() %p", __FUNCTION__, mPlaybackHandler);
    if (mPlaybackHandler != NULL) {
        mPaused = true;
        return mPlaybackHandler->pause();
    }
    return -ENODATA;
}

int AudioALSAStreamOut::resume() {
    ALOGD("%s() %p", __FUNCTION__, mPlaybackHandler);
    if (mPlaybackHandler != NULL) {
        mPaused = false;
        return mPlaybackHandler->resume();
    }
    return -ENODATA;
}

int AudioALSAStreamOut::flush() {
    ALOGD("%s() %p", __FUNCTION__, mPlaybackHandler);
    if (mPlaybackHandler != NULL) {
        return mPlaybackHandler->flush();
    }
    return 0;
}

bool AudioALSAStreamOut::isOutPutStreamActive() {
    int oldCount = android_atomic_inc(&mLockCount);
    AL_AUTOLOCK(mLock);
    oldCount = android_atomic_dec(&mLockCount);
    return (mStandby == false) ? true : false;
}

int AudioALSAStreamOut::drain(audio_drain_type_t type) {
    ALOGD("%s() %p", __FUNCTION__, mPlaybackHandler);
    if (mPlaybackHandler != NULL) {
        return mPlaybackHandler->drain(type);
    }
    return 0;
}

status_t AudioALSAStreamOut::routing(audio_devices_t output_devices) {
    AL_AUTOLOCK(mSuspendLock);
    AL_AUTOLOCK(mLock);

    status_t status = NO_ERROR;

    if (output_devices == mStreamAttributeSource.output_devices) {
        ALOGW("%s(), warning, flag 0x%x, routing to same device(0x%x) is not necessary",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, output_devices);
        return status;
    }

    ALOGD("+%s(), route output device from 0x%x to 0x%x, flag 0x%x", __FUNCTION__,
          mStreamAttributeSource.output_devices, output_devices, mStreamAttributeSource.mAudioOutputFlags);

    if (mStandby == false) {
        if (mPlaybackHandler != NULL) {
            bool enable = mPlaybackHandler->setOffloadRoutingFlag(true);

            // MMAP don't support hal routing
            if (!(mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
                status = close();
            }

            if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) {
                ALOGD("%s(), OFFLOAD routing reopen, enable = %d", __FUNCTION__, enable);
                mStreamCbk(STREAM_CBK_EVENT_ERROR, 0, mCbkCookie);
            }
        } else {
            ASSERT(false);
            ALOGE("%s(), mPlaybackHandler == NULL", __FUNCTION__);
        }
    }
    mStreamAttributeSource.output_devices = output_devices;

    ALOGV("-%s()", __FUNCTION__);
    return status;
}

void AudioALSAStreamOut::updatePolicyDevice(audio_devices_t outputDevice) {
    AL_AUTOLOCK(mLock);
    mStreamAttributeSource.policyDevice = outputDevice;
    ALOGD("%s(), flag: 0x%x, device: 0x%x",
          __FUNCTION__,
          mStreamAttributeSource.mAudioOutputFlags,
          mStreamAttributeSource.policyDevice);
}

status_t AudioALSAStreamOut::setScreenState(bool mode) {
    ALOGD("+%s(), flag %d, mode %d", __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, mode);
    int oldCount;
    oldCount = android_atomic_inc(&mLockCount);
    AL_AUTOLOCK(mLock);
    oldCount = android_atomic_dec(&mLockCount);

    mLowLatencyMode = mode;

    return setScreenState_l();
}

int AudioALSAStreamOut::updateAudioMode(audio_mode_t mode) {
    int ret = 0;

    ALOGV("%s(), mode %d, flags 0x%x", __FUNCTION__, mode, mStreamAttributeSource.mAudioOutputFlags);

    int oldCount;
    oldCount = android_atomic_inc(&mLockCount);
    AL_AUTOLOCK(mLock);
    oldCount = android_atomic_dec(&mLockCount);

    if (!mStandby) {
        // update source attribute
        mStreamAttributeSource.audio_mode = mode;
        mStreamAttributeSource.mVoIPEnable = mStreamManager->needEnableVoip(&mStreamAttributeSource);

        ret = mPlaybackHandler->updateAudioMode(mode);
    }
    return ret;
}

status_t AudioALSAStreamOut::updateVolumeIndex(int stream, int device, int index) {
    ALOGD("%s(), stream = %d, device = %d, index = %d", __FUNCTION__, stream, device, index);

    if (!mStandby) {
        mPlaybackHandler->updateVolumeIndex(stream, device, index);
    }

    return NO_ERROR;
}


status_t AudioALSAStreamOut::setSuspend(const bool suspend_on) {
    ALOGD("+%s(), mSuspendCount = %u, suspend_on = %d, flags 0x%x",
          __FUNCTION__, mSuspendCount, suspend_on, mStreamAttributeSource.mAudioOutputFlags);
    int oldCount;
    oldCount = android_atomic_inc(&mLockCount);
    AL_AUTOLOCK(mSuspendLock);
    oldCount = android_atomic_dec(&mLockCount);

    if (suspend_on == true) {
        mSuspendCount++;

        if (mPlaybackHandler && mSuspendCount == 1) {
            mPlaybackHandler->setSuspend(true);
        }
    } else if (suspend_on == false) {
        ASSERT(mSuspendCount > 0);
        mSuspendCount--;

        if (mPlaybackHandler && mSuspendCount == 0) {
            mPlaybackHandler->setSuspend(false);
        }
    }

    ALOGV("-%s(), mSuspendCount = %u", __FUNCTION__, mSuspendCount);
    return NO_ERROR;
}


status_t AudioALSAStreamOut::setSuspendStreamOutHDMIStereo(const bool suspend_on) {
    ALOGD("+%s(), mSuspendStreamOutHDMIStereoCount = %u, suspend_on = %d",
          __FUNCTION__, mSuspendStreamOutHDMIStereoCount, suspend_on);

    if (suspend_on) {
        mSuspendStreamOutHDMIStereoCount++;
    } else {
        ASSERT(mSuspendStreamOutHDMIStereoCount > 0);
        mSuspendStreamOutHDMIStereoCount--;
    }

    ALOGV("-%s(), mSuspendStreamOutHDMIStereoCount = %u",
          __FUNCTION__, mSuspendStreamOutHDMIStereoCount);
    return NO_ERROR;
}

status_t AudioALSAStreamOut::setMuteForRouting(bool mute) {
    ALOGD_IF((mLogEnable&AUD_OUT_OP_LOG), "%s(), mute %d, flags %d", __FUNCTION__, mute, mStreamAttributeSource.mAudioOutputFlags);
    mMuteForRouting = mute;
    if (mute) {
        clock_gettime(CLOCK_MONOTONIC, &mMuteTime);
    }
    return NO_ERROR;
}

void AudioALSAStreamOut::OpenPCMDump(const char *class_name) {
    ALOGV("%s()", __FUNCTION__);
    char mDumpFileName[128];
    int ret = 0;
    if (snprintf(mDumpFileName, sizeof(mDumpFileName), "%s.%d.%s.pid%d.tid%d.%d.%s.%dch.pcm",
                 streamout, mDumpFileNum, class_name, getpid(), gettid(),
                 mStreamAttributeSource.sample_rate,
                 transferAudioFormatToDumpString(mStreamAttributeSource.audio_format),
                 mStreamAttributeSource.num_channels) < 0) {

        ALOGE("%s(), sprintf mDumpFileName fail!!", __FUNCTION__);

    } else {

        mPCMDumpFile = NULL;
        mPCMDumpFile = AudioOpendumpPCMFile(mDumpFileName, streamout_propty);

        if (mPCMDumpFile != NULL) {
            ALOGD("%s(), flag %d, DumpFileName = %s",
                  __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, mDumpFileName);

            mDumpFileNum++;
            mDumpFileNum %= MAX_DUMP_NUM;
        }
    }
}

void AudioALSAStreamOut::ClosePCMDump() {
    ALOGV("%s()", __FUNCTION__);
    if (mPCMDumpFile) {
        AudioCloseDumpPCMFile(mPCMDumpFile);
        ALOGD("%s(), close it", __FUNCTION__);
    }
}

void  AudioALSAStreamOut::WritePcmDumpData(const void *buffer, ssize_t bytes) {
    if (mPCMDumpFile) {
        //ALOGD("%s()", __FUNCTION__);
        AudioDumpPCMData((void *)buffer, bytes, mPCMDumpFile);
    }
}

void AudioALSAStreamOut::openWavDump(const char *class_name) {
    ALOGV("%s()", __FUNCTION__);
    char mDumpFileName[256];
    int ret = 0;

    char timep_str[32];
    getCurrentTimestamp(timep_str, sizeof(timep_str));

    ret = snprintf(mDumpFileName, sizeof(mDumpFileName), "%s.%d.%s.flag%d.pid%d.tid%d.%d.%s.%dch_%s.wav",
                   streamout, mDumpFileNum, class_name, mStreamAttributeSource.mAudioOutputFlags,
                   getpid(), gettid(),
                   mStreamAttributeSource.sample_rate,
                   transferAudioFormatToDumpString(mStreamAttributeSource.audio_format),
                   mStreamAttributeSource.num_channels, timep_str);

    AL_LOCK(mAudioDumpLock);
    if (ret >= 0 && ret < sizeof(mDumpFileName)) {
        mDumpFile = NULL;
        mDumpFile = AudioOpendumpPCMFile(mDumpFileName, streamout_propty);

        if (mDumpFile) {
            ALOGD("%s(), flag %d, DumpFileName = %s, format = %d, channels = %d, sample_rate = %d",
                  __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, mDumpFileName,
                  mStreamAttributeSource.audio_format,
                  mStreamAttributeSource.num_channels,
                  mStreamAttributeSource.sample_rate);

            mBytesWavDumpWritten = 0;
            UpdateWaveHeader(mDumpFile, mBytesWavDumpWritten,
                             mStreamAttributeSource.audio_format,
                             mStreamAttributeSource.num_channels,
                             mStreamAttributeSource.sample_rate);

            mDumpFileNum++;
            mDumpFileNum %= MAX_DUMP_NUM;
        } else {
            ALOGV("%s(), mDumpFile is NULL", __FUNCTION__);
        }
    } else {
        ALOGE("%s(), sprintf mDumpFileName fail!!", __FUNCTION__);
    }
    AL_UNLOCK(mAudioDumpLock);

}

void AudioALSAStreamOut::closeWavDump() {

    ALOGV("%s()", __FUNCTION__);
    AL_LOCK(mAudioDumpLock);
    if (mDumpFile) {
        ALOGD("%s(), flag %d, mBytesWavDumpWritten = %d, format = %d, channels = %d, sample_rate = %d",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, mBytesWavDumpWritten,
              mStreamAttributeSource.audio_format,
              mStreamAttributeSource.num_channels,
              mStreamAttributeSource.sample_rate);

        UpdateWaveHeader(mDumpFile, mBytesWavDumpWritten,
                         mStreamAttributeSource.audio_format,
                         mStreamAttributeSource.num_channels,
                         mStreamAttributeSource.sample_rate);

        AudioCloseDumpPCMFile(mDumpFile);

        mBytesWavDumpWritten = 0;
		mDumpFile = NULL;
        ALOGV("%s(), close it", __FUNCTION__);
    }
    AL_UNLOCK(mAudioDumpLock);
}

void  AudioALSAStreamOut::writeWavDumpData(const void *buffer, uint32_t bytes) {

    if (mDumpFile) {
        AudioDumpPCMData((void *)buffer, bytes, mDumpFile);

        mBytesWavDumpWritten += bytes;

        UpdateWaveHeader(mDumpFile, mBytesWavDumpWritten,
                         mStreamAttributeSource.audio_format,
                         mStreamAttributeSource.num_channels,
                         mStreamAttributeSource.sample_rate);

    }
}

status_t AudioALSAStreamOut::logDumpSize() {
    uint32_t writtenFrames = 0;
    uint32_t time_ms = 0;
    writtenFrames = mBytesWavDumpWritten / getSizePerFrameByAttr(&mStreamAttributeSource);
    time_ms = (writtenFrames * 1000) / mStreamAttributeSource.sample_rate;

    if (writtenFrames > 0) {
        ALOGD("%s(), flag %d, frames = %d, time_ms = %d",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, writtenFrames, time_ms);
    }

    return NO_ERROR;
}

bool AudioALSAStreamOut::isWavDumpEnabled() {
    AL_AUTOLOCK(mAudioDumpLock);
    return (mDumpFile != NULL) ? true : false;
}

status_t AudioALSAStreamOut::dynamicSetStreamOutAudioDump() {
    bool isWavDumping = isWavDumpEnabled();

    if (isWavDumping == false) {
        ALOGD("%s(), flag = %d, isWavDumping = %d",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags, isWavDumping);

        openWavDump(LOG_TAG);

        /* Alse enable PlaybackHandlerAudioDump if mPlaybackHandler is exist */
        if (mPlaybackHandler) {
            mPlaybackHandler->dynamicSetPlaybackHandlerAudioDump();
        }
    } else {
        ALOGD("%s(), flag = %d, mDumpFile already exist, BYPASS!!!",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags);
    }

    return NO_ERROR;
}

status_t AudioALSAStreamOut::dataProcessForMute(const void *buffer, size_t bytes) {
    void *pBuffer = const_cast<void *>(buffer);

    if (mMuteForRouting) {
        // check mute timeout
        bool isNeedUnmuteRamp = false;
        clock_gettime(CLOCK_MONOTONIC, &mMuteCurTime);
        double totalMuteTime = calc_time_diff(mMuteCurTime, mMuteTime);

        ALOGV("%s(), flag %d, mMuteForRouting %d, totalMuteTime %f",
              __FUNCTION__, mStreamAttributeSource.mAudioOutputFlags,  mMuteForRouting, totalMuteTime);

        if (totalMuteTime > 0.300) {
            setMuteForRouting(false);
            isNeedUnmuteRamp = true;
        }

        if (isNeedUnmuteRamp) {
            ALOGW("%s(), mute timeout, unmute and ramp, format %d", __FUNCTION__, mStreamAttributeSource.audio_format);
            if (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_32_BIT ||
                mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_16_BIT) {
                size_t tmpBytes = bytes;
                int formatBytes = audio_bytes_per_sample(mStreamAttributeSource.audio_format);
                int32_t *sample = (int32_t *)buffer;
                int16_t *sample16 = (int16_t *)buffer;
                while (tmpBytes > 0) {
                    if (mStreamAttributeSource.audio_format == AUDIO_FORMAT_PCM_32_BIT) {
                        *sample = (*sample) * ((float)(bytes - tmpBytes) / bytes);
                        sample++;
                    } else {
                        *sample16 = (*sample16) * ((float)(bytes - tmpBytes) / bytes);
                        sample16++;
                    }
                    tmpBytes -= formatBytes;
                }
            }
        } else {
            memset(pBuffer, 0, bytes);
        }
    }

    return NO_ERROR;
}

void  AudioALSAStreamOut::setBufferSize() {

    // set default value here. and change it when open by different type of handlers
    if (mStreamAttributeSource.audio_channel_mask == AUDIO_CHANNEL_OUT_5POINT1 ||
        mStreamAttributeSource.audio_channel_mask == AUDIO_CHANNEL_OUT_7POINT1) {
        size_t sizePerFrame = getSizePerFrame(mStreamAttributeSource.audio_format,
                                              mStreamAttributeSource.num_channels);
        if (mStreamAttributeSource.sample_rate <= 48000) {
            mStreamAttributeSource.buffer_size = BUFFER_FRAME_COUNT_PER_ACCESSS_HDMI;
        } else if (mStreamAttributeSource.sample_rate <= 96000) {
            mStreamAttributeSource.buffer_size = BUFFER_FRAME_COUNT_PER_ACCESSS_HDMI << 1;
        } else if (mStreamAttributeSource.sample_rate <= 192000) {
            mStreamAttributeSource.buffer_size = BUFFER_FRAME_COUNT_PER_ACCESSS_HDMI << 2;
        } else {
            ASSERT(0);
        }
        mStreamAttributeSource.buffer_size *= sizePerFrame;
        mStreamAttributeSource.latency = (AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_BUFFER_SIZE_NORMAL) * 1000) /
                                         (mStreamAttributeSource.sample_rate * sizePerFrame);
    } else {
        mStreamAttributeSource.buffer_size = BUFFER_FRAME_COUNT_PER_ACCESS *
                                             getSizePerFrame(mStreamAttributeSource.audio_format,
                                                             mStreamAttributeSource.num_channels);

#ifdef PLAYBACK_USE_24BITS_ONLY
        audio_format_t format = (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) ?
                                mStreamAttributeSource.audio_format : AUDIO_FORMAT_PCM_8_24_BIT;
#else
        audio_format_t format = mStreamAttributeSource.audio_format;
#endif
        size_t sizePerFrame = getSizePerFrame(format, mStreamAttributeSource.num_channels);

        mStreamAttributeSource.latency = isIsolatedDeepBuffer(mStreamAttributeSource.mAudioOutputFlags) ?
                                         AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_BUFFER_SIZE_DEEP) :
                                         AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_BUFFER_SIZE_NORMAL);

        if (isIsolatedDeepBuffer(mStreamAttributeSource.mAudioOutputFlags)) {
            size_t sizePerFrame = getSizePerFrame(mStreamAttributeSource.audio_format,
                                                  mStreamAttributeSource.num_channels);

            uint32_t max_buffer_size = MAX_BUFFER_FRAME_COUNT_PER_ACCESS * sizePerFrame;

            mStreamAttributeSource.buffer_size = mStreamAttributeSource.latency -
                                                 (KERNEL_BUFFER_FRAME_COUNT_REMAIN * sizePerFrame);
            if (mStreamAttributeSource.buffer_size > max_buffer_size) {
                ALOGD("reduce hal buffer size %d -> %d", mStreamAttributeSource.buffer_size, max_buffer_size);
                mStreamAttributeSource.buffer_size = max_buffer_size;
            }
        }

        if (!(mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
             mStreamAttributeSource.latency = (mStreamAttributeSource.latency * 1000) /
                                         (mStreamAttributeSource.sample_rate * sizePerFrame);
        } else {
             // offload path
             mStreamAttributeSource.latency = (mStreamAttributeSource.latency * 1000) /
                                         (AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate() * sizePerFrame);
        }
    }

#ifdef DOWNLINK_LOW_LATENCY
    audio_output_flags_t outputFlag = mStreamAttributeSource.mAudioOutputFlags;
    if (outputFlag & AUDIO_OUTPUT_FLAG_FAST) {
        if (mStreamAttributeSource.sample_rate <= 48000) {
            mStreamAttributeSource.buffer_size = (outputFlag & AUDIO_OUTPUT_FLAG_RAW) ?
                                                 FRAME_COUNT_FAST_AND_RAW : FRAME_COUNT_MIN_PER_ACCESSS;
        } else if (mStreamAttributeSource.sample_rate <= 96000) {
            mStreamAttributeSource.buffer_size = (outputFlag & AUDIO_OUTPUT_FLAG_RAW) ?
                                                 FRAME_COUNT_FAST_AND_RAW << 1 : FRAME_COUNT_MIN_PER_ACCESSS << 1;
        } else if (mStreamAttributeSource.sample_rate <= 192000) {
            mStreamAttributeSource.buffer_size = (outputFlag & AUDIO_OUTPUT_FLAG_RAW) ?
                                                 FRAME_COUNT_FAST_AND_RAW << 2 : FRAME_COUNT_MIN_PER_ACCESSS << 2;
        } else {
            ASSERT(0);
        }

        mStreamAttributeSource.latency = (mStreamAttributeSource.buffer_size * 2 * 1000) / mStreamAttributeSource.sample_rate;
        mStreamAttributeSource.buffer_size *= mStreamAttributeSource.num_channels *
                                              audio_bytes_per_sample(mStreamAttributeSource.audio_format);
    } else if (outputFlag & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) {
        mStreamAttributeSource.buffer_size = MMAP_DL_PERIOD_SIZE;
        mStreamAttributeSource.latency = (mStreamAttributeSource.buffer_size * MIN_MMAP_DL_PERIOD_COUNT * 1000) /
                                         mStreamAttributeSource.sample_rate;
        mStreamAttributeSource.buffer_size *= mStreamAttributeSource.num_channels *
                                              audio_bytes_per_sample(mStreamAttributeSource.audio_format);
    }
#endif

#if defined(MTK_AUDIODSP_SUPPORT)
    if (isAdspOptionEnable() &&
        (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD)) {
        mStreamAttributeSource.buffer_size = OFFLOAD_BUFFER_SIZE_PER_ACCESSS;
    }
#endif

#if defined(PRIMARY_USB)
    if (audio_is_usb_out_device(mStreamAttributeSource.output_devices) &&
        mStreamAttributeSource.mAudioOutputFlags == 0 &&
        mStreamAttributeSource.sample_rate > 48000) {
        ALOGD("%s(), dynamic stream out for usb hifi playback!!", __FUNCTION__);
        if (mStreamAttributeSource.sample_rate > 96000) {
            mStreamAttributeSource.buffer_size = USB_HIFI_WRITE_BYTES * 2;
        } else {
            mStreamAttributeSource.buffer_size = USB_HIFI_WRITE_BYTES;
        }
        mStreamAttributeSource.latency = 2 * getBufferLatencyMs(
                                                 &mStreamAttributeSource,
                                                 mStreamAttributeSource.buffer_size);
    }
#endif

    size_t sizePerFrame = getSizePerFrame(mStreamAttributeSource.audio_format, mStreamAttributeSource.num_channels);
    mStreamAttributeSource.frame_count = mStreamAttributeSource.buffer_size / sizePerFrame;
}

status_t AudioALSAStreamOut::start() {
    int ret = INVALID_OPERATION;
    ALOGD("+%s()", __FUNCTION__);

    AL_AUTOLOCK(mLock);

    if ((mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ) &&
        (mPlaybackHandler != NULL) && !mStandby && !mStart) {
        ret = mPlaybackHandler->start();
        if (ret == 0) {
            mStart = true;
        }
    } else {
        ALOGW("%s fail, flags %d, !mStandby %d, !mStart %d, !mPlaybackHandler %d", __func__,
              mStreamAttributeSource.mAudioOutputFlags, !mStandby, !mStart, mPlaybackHandler != NULL);
    }

    ALOGD("-%s()", __FUNCTION__);
    return ret;
}

status_t AudioALSAStreamOut::stop() {
    int ret = INVALID_OPERATION;
    ALOGD("+%s()", __FUNCTION__);

    AL_AUTOLOCK(mLock);

    if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ && mPlaybackHandler != NULL
        && !mStandby && mStart) {
        ret = mPlaybackHandler->stop();
        if (ret == 0) {
            mStart = false;
        }
    } else {
        ALOGW("%s fail, flags %d, !mStandby %d, !mStart %d, !mPlaybackHandler %d", __func__,
              mStreamAttributeSource.mAudioOutputFlags, !mStandby, !mStart, mPlaybackHandler != NULL);
    }

    ALOGD("-%s", __FUNCTION__);
    return ret;
}

status_t AudioALSAStreamOut::createMmapBuffer(int32_t min_size_frames,
                                              struct audio_mmap_buffer_info *info) {
    int ret = INVALID_OPERATION;
    ALOGD("+%s(), min_size_frames %d", __FUNCTION__, min_size_frames);

    AL_AUTOLOCK(mLock);

    if (info == NULL || min_size_frames == 0 || min_size_frames > MAX_MMAP_FRAME_COUNT) {
        ALOGE("%s(): info = %p, min_size_frames = %d", __func__, info, min_size_frames);
        return INVALID_OPERATION;
    }

    if (mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ && mStandby) {
        if (mPlaybackHandler == NULL) {
            mPlaybackHandler = mStreamManager->createPlaybackHandler(&mStreamAttributeSource);
        }
        ret = mPlaybackHandler->createMmapBuffer(min_size_frames, info);
        mStandby = false;
    } else {
        ALOGW("%s() fail, flags %d, mStandby %d, !mPlaybackHandler %d", __func__,
              mStreamAttributeSource.mAudioOutputFlags, mStandby, mPlaybackHandler != NULL);
    }

    ALOGD("-%s()", __FUNCTION__);
    return ret;
}

status_t AudioALSAStreamOut::getMmapPosition(struct audio_mmap_position *position) {
    int ret = INVALID_OPERATION;
    //ALOGD("%s+", __FUNCTION__);

    AL_AUTOLOCK(mLock);

    if (position == NULL || mPlaybackHandler == NULL) {
        ALOGD("%s(), mPlaybackHandler or position == NULL!!", __func__);
        return INVALID_OPERATION;
    }
    if (!(mStreamAttributeSource.mAudioOutputFlags & AUDIO_OUTPUT_FLAG_MMAP_NOIRQ)) {
        return INVALID_OPERATION;
    }

    ret = mPlaybackHandler->getMmapPosition(position);

    //ALOGD("%s-", __FUNCTION__);
    return ret;
}

}
