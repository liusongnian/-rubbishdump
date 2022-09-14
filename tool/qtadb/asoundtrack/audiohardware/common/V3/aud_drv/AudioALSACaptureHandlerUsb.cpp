#include <AudioALSACaptureHandlerUsb.h>

#include <AudioALSACaptureDataClientSyncIO.h>

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <AudioALSACaptureDataClientAurisysNormal.h>
#else
#include <AudioALSACaptureDataClient.h>
#endif

#include <AudioALSACaptureDataProviderUsb.h>



#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "AudioALSACaptureHandlerUsb"


namespace android {

AudioALSACaptureHandlerUsb::AudioALSACaptureHandlerUsb(stream_attribute_t *stream_attribute_target) :
    AudioALSACaptureHandlerBase(stream_attribute_target) {
    mCaptureHandlerType = CAPTURE_HANDLER_USB;
}


AudioALSACaptureHandlerUsb::~AudioALSACaptureHandlerUsb() {

}


status_t AudioALSACaptureHandlerUsb::open() {
    ALOGD("%s(), input_device 0x%x, input_source 0x%x, sample_rate %d, num_channels %d, audio_format %d, flag 0x%x",
          __FUNCTION__,
          mStreamAttributeTarget->input_device,
          mStreamAttributeTarget->input_source,
          mStreamAttributeTarget->sample_rate,
          mStreamAttributeTarget->num_channels,
          mStreamAttributeTarget->audio_format,
          mStreamAttributeTarget->mAudioInputFlags);

    ASSERT(mCaptureDataClient == NULL);

    if (mStreamAttributeTarget->mAudioInputFlags == AUDIO_INPUT_FLAG_FAST &&
        (mStreamAttributeTarget->input_source == AUDIO_SOURCE_VOICE_RECOGNITION ||
         mStreamAttributeTarget->input_source == AUDIO_SOURCE_UNPROCESSED)) {
        mCaptureDataClient = new AudioALSACaptureDataClientSyncIO(AudioALSACaptureDataProviderUsb::getInstance(), mStreamAttributeTarget);
    } else {
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
        mCaptureDataClient = new AudioALSACaptureDataClientAurisysNormal(AudioALSACaptureDataProviderUsb::getInstance(), mStreamAttributeTarget, NULL);
#else
        mCaptureDataClient = new AudioALSACaptureDataClient(AudioALSACaptureDataProviderUsb::getInstance(), mStreamAttributeTarget);
#endif
    }

    return NO_ERROR;
}


status_t AudioALSACaptureHandlerUsb::close() {
    ALOGD("%s(), input_device 0x%x, input_source 0x%x, sample_rate %d, num_channels %d, audio_format %d, flag 0x%x",
          __FUNCTION__,
          mStreamAttributeTarget->input_device,
          mStreamAttributeTarget->input_source,
          mStreamAttributeTarget->sample_rate,
          mStreamAttributeTarget->num_channels,
          mStreamAttributeTarget->audio_format,
          mStreamAttributeTarget->mAudioInputFlags);

    ASSERT(mCaptureDataClient != NULL);
    delete mCaptureDataClient;

    return NO_ERROR;
}


status_t AudioALSACaptureHandlerUsb::routing(const audio_devices_t input_device __unused) {
    WARNING("Not support!!");
    return NO_ERROR;
}


ssize_t AudioALSACaptureHandlerUsb::read(void *buffer, ssize_t bytes) {
    return mCaptureDataClient->read(buffer, bytes);
}


} // end of namespace android



