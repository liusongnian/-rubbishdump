#include "AudioALSACaptureDataProviderEchoRefBTCVSD.h"

#include "AudioType.h"

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include "WCNChipController.h"
#else
#include "AudioALSASampleRateController.h"
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSACaptureDataProviderEchoRefBTCVSD"


namespace android {


/*==============================================================================
 *                     Implementation
 *============================================================================*/

AudioALSACaptureDataProviderEchoRefBTCVSD *AudioALSACaptureDataProviderEchoRefBTCVSD::mAudioALSACaptureDataProviderEchoRefBTCVSD = NULL;
AudioALSACaptureDataProviderEchoRefBTCVSD *AudioALSACaptureDataProviderEchoRefBTCVSD::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSACaptureDataProviderEchoRefBTCVSD == NULL) {
        mAudioALSACaptureDataProviderEchoRefBTCVSD = new AudioALSACaptureDataProviderEchoRefBTCVSD();
    }
    ASSERT(mAudioALSACaptureDataProviderEchoRefBTCVSD != NULL);
    return mAudioALSACaptureDataProviderEchoRefBTCVSD;
}

AudioALSACaptureDataProviderEchoRefBTCVSD::AudioALSACaptureDataProviderEchoRefBTCVSD() {
    mCaptureDataProviderType = CAPTURE_PROVIDER_ECHOREF_BTCVSD;
}

AudioALSACaptureDataProviderEchoRefBTCVSD::~AudioALSACaptureDataProviderEchoRefBTCVSD() {

}


void AudioALSACaptureDataProviderEchoRefBTCVSD::configDefaultAttribute(void) {
    // already get attr from playback handler
    if (getPlaybackEnabled()) {
        return;
    }

    // use default attr
    mStreamAttributeSource.audio_format = AUDIO_FORMAT_PCM_16_BIT;
    mStreamAttributeSource.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
    mStreamAttributeSource.num_channels = popcount(mStreamAttributeSource.audio_channel_mask);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    WCNChipController *pWCNChipController = WCNChipController::GetInstance();
    if (!pWCNChipController) {
        WARNING("pWCNChipController NULL!!");
        mStreamAttributeSource.sample_rate = 8000;
    } else {
        mStreamAttributeSource.sample_rate = pWCNChipController->GetBTCurrentSamplingRateNumber();
    }
#else
    AudioALSASampleRateController *pAudioALSASampleRateController = AudioALSASampleRateController::getInstance();
    if (!pAudioALSASampleRateController) {
        WARNING("pAudioALSASampleRateController NULL!!");
        mStreamAttributeSource.sample_rate = 8000;
    } else {
        mStreamAttributeSource.sample_rate = pAudioALSASampleRateController->getPrimaryStreamOutSampleRate();
    }
#endif

    ALOGW("%s(), type %d not attach!! default attr: fmt %d, ch %d, rate %u",
          __FUNCTION__,
          mCaptureDataProviderType,
          mStreamAttributeSource.audio_format,
          mStreamAttributeSource.num_channels,
          mStreamAttributeSource.sample_rate);
}


} // end of namespace android
