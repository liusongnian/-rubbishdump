#include <AudioALSAPlaybackHandlerUsb.h>

#include "AudioSystemLibCUtil.h"

#include <fstream>


extern "C" {
#include <alsa_device_profile.h>
#include <alsa_device_proxy.h>
#include <alsa_logging.h>
#include <audio_utils/channels.h>
}

#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
#include <AudioParamParser.h>
#endif

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <audio_memory_control.h>
#include <audio_lock.h>
#include <audio_ringbuf.h>
#include <audio_task.h>
#include <aurisys_scenario.h>
#include <arsi_type.h>
#include <audio_pool_buf_handler.h>
#include <aurisys_controller.h>
#include <aurisys_lib_manager.h>
#endif

#include <audio_fmt_conv_hal.h>

#include <AudioUtility.h>

#if defined(PRIMARY_USB)
#include <AudioUSBCenter.h>
#endif

#include <AudioALSAStreamManager.h>
#include <AudioALSACaptureDataProviderEchoRefUsb.h>

#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif



#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAPlaybackHandlerUsb"



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

//#define DEBUG_TIMESTAMP

#ifdef DEBUG_TIMESTAMP
#define SHOW_TIMESTAMP(format, args...) ALOGD(format, ##args)
#else
#define SHOW_TIMESTAMP(format, args...)
#endif

#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)

#define USB_SPH_DEVICE_PARAM_AUDIOTYPE_NAME "USBDevice"

// Latency Detect
//#define DEBUG_LATENCY

#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)
static const char *PROPERTY_KEY_EXTDAC = "vendor.audiohal.resource.extdac.support";

namespace android {


AudioALSAPlaybackHandlerUsb::AudioALSAPlaybackHandlerUsb(const stream_attribute_t *stream_attribute_source) :
    AudioALSAPlaybackHandlerBase(stream_attribute_source),
    mTotalEchoRefBufSize(0),
    mDataProviderEchoRefUsb(NULL) {
    mPlaybackHandlerType = PLAYBACK_HANDLER_USB;
    memset(&mProxy, 0, sizeof(mProxy));
    usbProxy = NULL;
    memset((void *)&mStreamAttributeTarget, 0, sizeof(mStreamAttributeTarget));
    memset((void *)&mStreamAttributeTargetEchoRef, 0, sizeof(mStreamAttributeTargetEchoRef));

    memset((void *)&mNewtime, 0, sizeof(mNewtime));
    memset((void *)&mOldtime, 0, sizeof(mOldtime));
    memset((void *)latencyTime, 0, sizeof(latencyTime));
    /* Init EchoRef Resource */
    memset(&mEchoRefStartTime, 0, sizeof(mEchoRefStartTime));
    memset(&mUSBOutStream, 0, sizeof(struct USBStream));
    memset(&mParam, 0, sizeof(struct USBCallParam));
    mIsUsbHAL = false;

    ALOGD("%s(), source attr: flag %d fmt %d ch %d rate %u buf sz %u", __FUNCTION__,
          mStreamAttributeSource->mAudioOutputFlags,
          mStreamAttributeSource->audio_format,
          mStreamAttributeSource->num_channels,
          mStreamAttributeSource->sample_rate,
          mStreamAttributeSource->buffer_size);
}


AudioALSAPlaybackHandlerUsb::~AudioALSAPlaybackHandlerUsb() {

}

void AudioALSAPlaybackHandlerUsb::initUsbInfo(stream_attribute_t *mStreamAttributeTargetUSB, alsa_device_proxy *proxy) {
    mIsUsbHAL = true;

    usbProxy = proxy;

    mStreamAttributeTarget = *mStreamAttributeTargetUSB;
    mStreamAttributeTargetEchoRef = mStreamAttributeTarget;

    if (AudioALSAStreamManager::getInstance()->isModeInVoipCall()) {
        mStreamAttributeTarget.mVoIPEnable = true;
    }
}


status_t AudioALSAPlaybackHandlerUsb::open() {
    int ret = 0;

    ALOGD("%s(+), mDevice = 0x%x", __FUNCTION__, mStreamAttributeSource->output_devices);

#if defined(PRIMARY_USB)
    if (mIsUsbHAL == false) {
        /* get device / audio mode / ... from source attr */
        memcpy(&mStreamAttributeTarget, mStreamAttributeSource, sizeof(mStreamAttributeTarget));
        ret = prepareUsb();
        if (ret != 0) {
            return ret;
        }
        usbProxy = &mProxy;
    }
#endif

    ret = proxy_open(usbProxy);
    if (ret != 0) {
        ALOGD("%s(), proxy_open fail, ret %d", __FUNCTION__, ret);
        return ret;
    }
    mPcm = usbProxy->pcm;

    // debug pcm dump
    openWavDump(LOG_TAG);

    profile_init(&mUSBOutStream.profile, PCM_OUT);
    mUSBOutStream.profile.card = usbProxy->profile->card;
    mUSBOutStream.profile.device = usbProxy->profile->device;
    loadUSBDeviceParam();
    getDeviceId(&mUSBOutStream);
    getDeviceParam(&mUSBOutStream);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (get_aurisys_on() && !mStreamAttributeSource->isBypassAurisys && IsVoIPEnable()) {
        CreateAurisysLibManager();
    } else
#endif
    {
        mFmtConvHdl = createFmtConvHdlWrap(mStreamAttributeSource, &mStreamAttributeTarget);
    }

    mTimeStampValid = false;
    mBytesWriteKernel = 0;

    /* Reset software timestamp information */
    mTotalEchoRefBufSize = 0;
    memset((void *)&mEchoRefStartTime, 0, sizeof(mEchoRefStartTime));

    mDataProviderEchoRefUsb = AudioALSACaptureDataProviderEchoRefUsb::getInstance();
    mDataProviderEchoRefUsb->attachPlaybackHandler(&mStreamAttributeTarget);

    ALOGD("%s(-)", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSAPlaybackHandlerUsb::close() {
    ALOGD("%s(+)", __FUNCTION__);

    if (!mPcm || !usbProxy) {
        ALOGD("%s(-), mPcm %p usbProxy %p error!!", __FUNCTION__, mPcm, usbProxy);
        return INVALID_OPERATION;
    }

    mDataProviderEchoRefUsb->detachPlaybackHandler();

    proxy_close(usbProxy);
    mPcm = NULL;

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

    ALOGD("%s(-)", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSAPlaybackHandlerUsb::routing(const audio_devices_t output_devices __unused) {
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerUsb::setScreenState(bool mode __unused, size_t buffer_size __unused, size_t reduceInterruptSize __unused, bool bforce __unused) {
    return NO_ERROR;
}

ssize_t AudioALSAPlaybackHandlerUsb::write(const void *buffer, size_t bytes) {
    //ALOGD("%s(), buffer = %p, bytes = %zu", __FUNCTION__, buffer, bytes);

    // const -> to non const
    void *pBuffer = const_cast<void *>(buffer);
    ASSERT(pBuffer != NULL);

    if (!mPcm) {
        ALOGW("%s(), mPcm is NULL!", __FUNCTION__);
        usleep(getBufferLatencyUs(mStreamAttributeSource, bytes));
        return bytes;
    }

    if (mBytesWriteKernel != 0) {
        clock_gettime(CLOCK_MONOTONIC, &mNewtime);
        latencyTime[0] = calc_time_diff(mNewtime, mOldtime);
        mOldtime = mNewtime;
    } else {
        clock_gettime(CLOCK_MONOTONIC, &mNewtime);
        latencyTime[0] = 0.0;
        mOldtime = mNewtime;
    }

    void *pBufferAfterPending = NULL;
    uint32_t bytesAfterpending = 0;

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (mAurisysLibManager) {
        audio_pool_buf_copy_from_linear(
            mAudioPoolBufDlIn,
            pBuffer,
            bytes);

        // post processing + SRC + Bit conversion
        aurisys_process_dl_only(mAurisysLibManager, mAudioPoolBufDlIn, mAudioPoolBufDlOut);

        uint32_t data_size = audio_ringbuf_count(&mAudioPoolBufDlOut->ringbuf);
        audio_pool_buf_copy_to_linear(
            &mLinearOut->p_buffer,
            &mLinearOut->memory_size,
            mAudioPoolBufDlOut,
            data_size);

        // wrap to original playback handler
        pBufferAfterPending = (void *)mLinearOut->p_buffer;
        bytesAfterpending = data_size;
    } else
#endif
    {
        aud_fmt_conv_hal_process(
            pBuffer, bytes,
            &pBufferAfterPending, &bytesAfterpending,
            mFmtConvHdl);
    }

#ifdef MTK_LATENCY_DETECT_PULSE
    AudioDetectPulse::doDetectPulse(TAG_PLAYERBACK_HANDLER, PULSE_LEVEL, 0, (void *)pBufferAfterPending,
                                    bytesAfterpending, mStreamAttributeTarget.audio_format,
                                    mStreamAttributeTarget.num_channels, mStreamAttributeTarget.sample_rate);
#endif

    clock_gettime(CLOCK_MONOTONIC, &mNewtime);
    latencyTime[1] = calc_time_diff(mNewtime, mOldtime);
    mOldtime = mNewtime;

    proxy_write(usbProxy, pBufferAfterPending, bytesAfterpending);

    clock_gettime(CLOCK_MONOTONIC, &mNewtime);
    latencyTime[2] = calc_time_diff(mNewtime, mOldtime);
    mOldtime = mNewtime;

    mBytesWriteKernel += bytesAfterpending;
    if (mTimeStampValid == false) {
        if (mBytesWriteKernel >= (mStreamAttributeTarget.buffer_size >> 1)) {
            mTimeStampValid = true;
        }
    }

    updateHardwareBufferInfo(bytes, bytesAfterpending);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (updateStartTimeStamp() == NO_ERROR) {
        writeEchoRefDataToDataProvider(mDataProviderEchoRefUsb,
                                       (const char *)pBufferAfterPending,
                                       bytesAfterpending);
    }
#endif

    clock_gettime(CLOCK_MONOTONIC, &mNewtime);
    latencyTime[3] = calc_time_diff(mNewtime, mOldtime);
    mOldtime = mNewtime;

    writeWavDumpData(pBufferAfterPending, bytesAfterpending);

    clock_gettime(CLOCK_MONOTONIC, &mNewtime);
    latencyTime[4] = calc_time_diff(mNewtime, mOldtime);
    mOldtime = mNewtime;

    double logTimeout = (double)getBufferLatencyMs(mStreamAttributeSource, bytes) / 1000;
    logTimeout += 0.002; /* +2ms: avoid log too much */
    double totalTime = latencyTime[0] + latencyTime[1] + latencyTime[2] + latencyTime[3] + latencyTime[4];

    struct timespec timeStamp;
    unsigned int alsa_buf_sz = 0;
    unsigned int alsa_avail = 0;
    unsigned int queue = 0;
    if (usbProxy && usbProxy->pcm) {
        if (pcm_get_htimestamp(usbProxy->pcm, &alsa_avail, &timeStamp) == 0) {
            alsa_buf_sz = pcm_get_buffer_size(usbProxy->pcm);
            if (alsa_buf_sz > alsa_avail) {
                queue = alsa_buf_sz - alsa_avail;
            }
        }
    }

    if (totalTime > logTimeout) {
        ALOGW("latency_in_s,%1.6lf,%1.6lf,%1.6lf,%1.6lf,%1.6lf, totalTime %1.6lf > logTimeout %1.6lf TIMEOUT!! queue %u", latencyTime[0], latencyTime[1], latencyTime[2], latencyTime[3], latencyTime[4], totalTime, logTimeout, queue);
    }
#ifdef DEBUG_LATENCY
    else {
        ALOGD("latency_in_s,%1.6lf,%1.6lf,%1.6lf,%1.6lf,%1.6lf, totalTime %1.6lf, logTimeout %1.6lf!! queue %u", latencyTime[0], latencyTime[1], latencyTime[2], latencyTime[3], latencyTime[4], totalTime, logTimeout, queue);
    }
#endif

    return bytes;
}


template<class T>
status_t getParam(AppOps *appOps, ParamUnit *_paramUnit, T *_param, const char *_paramName) {
    Param *param;
    param = appOps->paramUnitGetParamByName(_paramUnit, _paramName);
    if (!param) {
        ALOGE("error: get param fail, param_name = %s", _paramName);
        return BAD_VALUE;
    } else {
        *_param = *(T *)param->data;
    }

    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerUsb::loadUSBDeviceParam() {
    ALOGD("%s()", __FUNCTION__);

    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGE("%s(), Error: AppOps == NULL", __FUNCTION__);
        ASSERT(false);
        return UNKNOWN_ERROR;
    }

    // define xml names
    char audioTypeName[] = USB_SPH_DEVICE_PARAM_AUDIOTYPE_NAME;

    // extract parameters from xml
    AudioType *audioType;
    audioType = appOps->appHandleGetAudioTypeByName(appOps->appHandleGetInstance(), audioTypeName);
    if (!audioType) {
        ALOGE("%s(), get audioType fail, audioTypeName = %s", __FUNCTION__, audioTypeName);
        return BAD_VALUE;
    }

    std::string categoryTypeName = "Device";
    CategoryType *categoryType = appOps->audioTypeGetCategoryTypeByName(audioType, categoryTypeName.c_str());
    if (!audioType) {
        ALOGE("%s(), get categoryType fail, categoryTypeName = %s", __FUNCTION__, categoryTypeName.c_str());
        return BAD_VALUE;
    }

    // Read lock
    appOps->audioTypeReadLock(audioType, __FUNCTION__);

    size_t categoryNum = appOps->categoryTypeGetNumOfCategory(categoryType);
    mParam.deviceParam.resize(categoryNum);

    mParam.maxCaptureLatencyUs = 0;
    for (size_t i = 0; i < categoryNum; i++) {
        Category *category = appOps->categoryTypeGetCategoryByIndex(categoryType, i);
        if (category == NULL) {
            continue;
        }
        if (category->name == NULL) {
            continue;
        }

        mParam.deviceParam[i].id = category->name;

        std::string paramPath = categoryTypeName + "," + category->name;

        ParamUnit *paramUnit;
        paramUnit = appOps->audioTypeGetParamUnit(audioType, paramPath.c_str());
        if (!paramUnit) {
            ALOGE("%s(), get paramUnit fail, paramPath = %s", __FUNCTION__, paramPath.c_str());
            return BAD_VALUE;
        }

        // spec
        getParam<int>(appOps, paramUnit, &mParam.deviceParam[i].playbackLatencyUs, "playback_latency_us");
        getParam<int>(appOps, paramUnit, &mParam.deviceParam[i].captureLatencyUs, "capture_latency_us");

        if (mParam.deviceParam[i].captureLatencyUs > mParam.maxCaptureLatencyUs) {
            mParam.maxCaptureLatencyUs = mParam.deviceParam[i].captureLatencyUs;
        }

        ALOGD("%s(), i %zu, device id %s, playbackLatencyUs %d, captureLatencyUs %d",
              __FUNCTION__, i, mParam.deviceParam[i].id.c_str(),
              mParam.deviceParam[i].playbackLatencyUs, mParam.deviceParam[i].captureLatencyUs);
    }

    ALOGV("%s(), mParam.maxCaptureLatencyUs %d", __FUNCTION__, mParam.maxCaptureLatencyUs);

    // Unlock
    appOps->audioTypeUnlock(audioType);

    return NO_ERROR;

}

status_t AudioALSAPlaybackHandlerUsb::getDeviceId(struct USBStream *stream) {
    if (!profile_is_initialized(&stream->profile)) {
        ALOGE("%s(), dir %d not initialized", __FUNCTION__, stream->direction);
        ASSERT(0);
        stream->deviceId.clear();
        return BAD_VALUE;
    }

    // get device id
#define DEVICE_ID_SIZE 32
    char deviceId[DEVICE_ID_SIZE] = "default";
    std::string usbidPath = "/proc/asound/card";
    usbidPath += std::to_string(stream->profile.card);
    usbidPath += "/usbid";

    std::ifstream is(usbidPath.c_str(), std::ifstream::in);
    if (is) {
        is >> deviceId;
        is.close();
    } else {
        ALOGE("%s(), open path %s failed, use default", __FUNCTION__, usbidPath.c_str());
    }

    stream->deviceId = deviceId;

    return NO_ERROR;
}

uint32_t AudioALSAPlaybackHandlerUsb::getUSBDeviceLatency(size_t deviceParamIdx) {
    int delayMs = 0;
    delayMs = mParam.deviceParam[deviceParamIdx].playbackLatencyUs / 1000 ;
    ALOGD("%s(), deviceParamIdx %zu, playbackLatencyUs %d", __FUNCTION__, deviceParamIdx, delayMs);
    return delayMs;
}

status_t AudioALSAPlaybackHandlerUsb::getDeviceParam(struct USBStream *stream) {
    int dir = stream->direction;
    if (stream->deviceId.empty()) {
        ALOGE("%s(), dir %d, deviceId empty", __FUNCTION__, dir);
        ASSERT(0);
        return BAD_VALUE;
    }

    size_t defaultIdx = 9999;

    for (size_t i = 0; i < mParam.deviceParam.size(); i++) {
        if (mParam.deviceParam[i].id.compare(std::string(stream->deviceId, 0, mParam.deviceParam[i].id.size())) == 0) {
            ALOGD("%s(), dir %d, param found for deviceId %s", __FUNCTION__, dir, stream->deviceId.c_str());
            stream->deviceParamIdx = i;
            return NO_ERROR;
        }

        if (mParam.deviceParam[i].id.compare("default") == 0) {
            defaultIdx = i;
        }
    }

    if (defaultIdx >= mParam.deviceParam.size()) {
        ALOGE("%s(), dir %d, invalid defaultIdx %zu", __FUNCTION__, dir, defaultIdx);
        ASSERT(0);
        return BAD_VALUE;
    }

    ALOGD("%s(), dir %d, use default param for deviceId %s", __FUNCTION__, dir, stream->deviceId.c_str());
    stream->deviceParamIdx = defaultIdx;
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerUsb::updateStartTimeStamp() {
    if (mEchoRefStartTime.tv_sec == 0 && mEchoRefStartTime.tv_nsec == 0) {
        time_info_struct_t HW_Buf_Time_Info;
        memset(&HW_Buf_Time_Info, 0, sizeof(HW_Buf_Time_Info));

        //status_t status = getHardwareBufferInfo(&HW_Buf_Time_Info);
        int ret = pcm_get_htimestamp(mPcm, &HW_Buf_Time_Info.frameInfo_get, &HW_Buf_Time_Info.timestamp_get);
        if (ret != 0) {
            ALOGD("-%s pcm_get_htimestamp fail, ret = %d, pcm_get_error = %s", __FUNCTION__, ret, pcm_get_error(mPcm));
            return UNKNOWN_ERROR;
        }
        mStreamAttributeTarget.Time_Info.timestamp_get = HW_Buf_Time_Info.timestamp_get;
        mEchoRefStartTime = mStreamAttributeTarget.Time_Info.timestamp_get;

        int delayMs = getUSBDeviceLatency(mUSBOutStream.deviceParamIdx);

        struct timespec origStartTime = mEchoRefStartTime;
        adjustTimeStamp(&mEchoRefStartTime, delayMs);

        ALOGD("%s(), Set start timestamp (%ld.%09ld->%ld.%09ld (%ld.%09ld)), mTotalEchoRefBufSize = %d, delayMs = %d (audio_mode = %d)",
              __FUNCTION__,
              origStartTime.tv_sec,
              origStartTime.tv_nsec,
              mEchoRefStartTime.tv_sec,
              mEchoRefStartTime.tv_nsec,
              mStreamAttributeTarget.Time_Info.timestamp_get.tv_sec,
              mStreamAttributeTarget.Time_Info.timestamp_get.tv_nsec,
              mTotalEchoRefBufSize,
              delayMs,
              mStreamAttributeSource->audio_mode);
    } else {
        ALOGV("%s(), start timestamp (%ld.%09ld), mTotalEchoRefBufSize = %d", __FUNCTION__, mEchoRefStartTime.tv_sec, mEchoRefStartTime.tv_nsec, mTotalEchoRefBufSize);
    }

    return NO_ERROR;
}


int AudioALSAPlaybackHandlerUsb::prepareUsb(void) {
#if !defined(PRIMARY_USB)
    return 0;
#else
    uint32_t period_us = 0;
    uint32_t period_count = 0;

    int ret = 0;

    /* latency & period count */
    if (mStreamAttributeSource->isIEMsSource) {
        period_us    = mStreamAttributeSource->periodUs;
        period_count = mStreamAttributeSource->periodCnt;
    } else {
        period_us    = getBufferLatencyUs(mStreamAttributeSource, mStreamAttributeSource->buffer_size);
        period_count = (mStreamAttributeSource->sample_rate > 96000) ? 8 :  2;
    }

    /* get usb proxy config */
    ret = AudioUSBCenter::getInstance()->prepareUsb(&mProxy, &mStreamAttributeTarget, PCM_OUT, period_us, period_count);
    if (ret != 0) {
        return ret;
    }

    memcpy(&mStreamAttributeTargetEchoRef, &mStreamAttributeTarget, sizeof(mStreamAttributeTargetEchoRef));

    ALOGD("%s(), mStreamAttributeTarget, rate %d, format %d, channels %d, buffer_size %u, period_us %u",
          __FUNCTION__,
          mStreamAttributeTarget.sample_rate,
          mStreamAttributeTarget.audio_format,
          mStreamAttributeTarget.num_channels,
          mStreamAttributeTarget.buffer_size,
          mStreamAttributeTarget.periodUs);

    return 0;
#endif /* end of PRIMARY_USB */
}


bool AudioALSAPlaybackHandlerUsb::writeEchoRefDataToDataProvider(AudioALSACaptureDataProviderEchoRefUsb *dataProvider, const char *echoRefData, uint32_t dataSize) {
    if ((mDataProviderEchoRefUsb != NULL) && (dataProvider->isEnable())) {
        /* Calculate buffer's time stamp */
        struct timespec newTimeStamp;
        calculateTimeStampByBytes(mEchoRefStartTime, mTotalEchoRefBufSize, mStreamAttributeTargetEchoRef, &newTimeStamp);
        SHOW_TIMESTAMP("%s(), mTotalEchoRefBufSize = %d, write size = %d, newTimeStamp = %ld.%09ld -> %ld.%09ld",
                       __FUNCTION__, mTotalEchoRefBufSize, dataSize, mEchoRefStartTime.tv_sec, mEchoRefStartTime.tv_nsec,
                       newTimeStamp.tv_sec, newTimeStamp.tv_nsec);

        // TODO(JH): Consider the close case, need to free EchoRef data from provider
        dataProvider->writeData(echoRefData, dataSize, &newTimeStamp);

        //WritePcmDumpData(echoRefData, dataSize);
    } else {
        SHOW_TIMESTAMP("%s(), data provider is not enabled, Do not write echo ref data to provider", __FUNCTION__);
    }
    mTotalEchoRefBufSize += dataSize;

    return true;
}

} // end of namespace android
