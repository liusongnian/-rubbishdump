#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_HAP_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_HAP_H

#include "AudioALSACaptureDataProviderBase.h"

namespace android {

class AudioALSACaptureDataProviderHAP : public AudioALSACaptureDataProviderBase {
public:
    virtual ~AudioALSACaptureDataProviderHAP();

    static AudioALSACaptureDataProviderHAP *getInstance();

    /**
     * open/close pcm interface when 1st attach & the last detach
     */
    status_t open();
    status_t close();



protected:
    AudioALSACaptureDataProviderHAP();



private:
    /**
     * singleton pattern
     */
    static AudioALSACaptureDataProviderHAP *mAudioALSACaptureDataProviderHAP;


    /**
     * pcm read thread
     */
    static void *readThread(void *arg);
    pthread_t hReadThread;

};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_HAP_H

