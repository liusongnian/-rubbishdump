#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF__BTCVSD_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF__BTCVSD_H

#include "AudioALSACaptureDataProviderEchoRefBase.h"


namespace android {

class AudioALSACaptureDataProviderEchoRefBTCVSD : public AudioALSACaptureDataProviderEchoRefBase {
public:
    virtual ~AudioALSACaptureDataProviderEchoRefBTCVSD();

    static AudioALSACaptureDataProviderEchoRefBTCVSD *getInstance();

    /**
     * init
     */
    virtual void configDefaultAttribute(void);



protected:
    AudioALSACaptureDataProviderEchoRefBTCVSD();


private:
    /**
     * singleton pattern
     */
    static AudioALSACaptureDataProviderEchoRefBTCVSD *mAudioALSACaptureDataProviderEchoRefBTCVSD;
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF__BTCVSD_H
