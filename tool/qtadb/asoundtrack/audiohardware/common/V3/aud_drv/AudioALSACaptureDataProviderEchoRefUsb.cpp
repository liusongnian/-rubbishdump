#include "AudioALSACaptureDataProviderEchoRefUsb.h"

#include "AudioType.h"


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSACaptureDataProviderEchoRefUsb"


namespace android {


/*==============================================================================
 *                     Implementation
 *============================================================================*/

AudioALSACaptureDataProviderEchoRefUsb *AudioALSACaptureDataProviderEchoRefUsb::mAudioALSACaptureDataProviderEchoRefUsb = NULL;
AudioALSACaptureDataProviderEchoRefUsb *AudioALSACaptureDataProviderEchoRefUsb::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSACaptureDataProviderEchoRefUsb == NULL) {
        mAudioALSACaptureDataProviderEchoRefUsb = new AudioALSACaptureDataProviderEchoRefUsb();
    }
    ASSERT(mAudioALSACaptureDataProviderEchoRefUsb != NULL);
    return mAudioALSACaptureDataProviderEchoRefUsb;
}

AudioALSACaptureDataProviderEchoRefUsb::AudioALSACaptureDataProviderEchoRefUsb() {
    mCaptureDataProviderType = CAPTURE_PROVIDER_ECHOREF_USB;
}

AudioALSACaptureDataProviderEchoRefUsb::~AudioALSACaptureDataProviderEchoRefUsb() {

}


void AudioALSACaptureDataProviderEchoRefUsb::configDefaultAttribute(void) {
    // already get attr from playback handler
    if (getPlaybackEnabled()) {
        return;
    }

    // use default attr
    if (mStreamAttributeSource.audio_format == AUDIO_FORMAT_DEFAULT) {
        mStreamAttributeSource.audio_format = AUDIO_FORMAT_PCM_24_BIT_PACKED;
    }

    if (mStreamAttributeSource.audio_channel_mask == 0) {
        mStreamAttributeSource.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
    }
    mStreamAttributeSource.num_channels = popcount(mStreamAttributeSource.audio_channel_mask);

    if (mStreamAttributeSource.sample_rate == 0) {
        mStreamAttributeSource.sample_rate = 48000;
    }

    ALOGW("%s(), type %d not attach!! default attr: fmt %d, ch %d, rate %u",
          __FUNCTION__,
          mCaptureDataProviderType,
          mStreamAttributeSource.audio_format,
          mStreamAttributeSource.num_channels,
          mStreamAttributeSource.sample_rate);
}


} // end of namespace android
