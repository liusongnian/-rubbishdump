#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF__USB_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF__USB_H

#include "AudioALSACaptureDataProviderEchoRefBase.h"


namespace android {

class AudioALSACaptureDataProviderEchoRefUsb : public AudioALSACaptureDataProviderEchoRefBase {
public:
    virtual ~AudioALSACaptureDataProviderEchoRefUsb();

    static AudioALSACaptureDataProviderEchoRefUsb *getInstance();

    /**
     * init
     */
    virtual void configDefaultAttribute(void);



protected:
    AudioALSACaptureDataProviderEchoRefUsb();


private:
    /**
     * singleton pattern
     */
    static AudioALSACaptureDataProviderEchoRefUsb *mAudioALSACaptureDataProviderEchoRefUsb;
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF__BTCVSD_H
