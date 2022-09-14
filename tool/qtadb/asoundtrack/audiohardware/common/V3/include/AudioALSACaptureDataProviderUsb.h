#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_USB_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_USB_H

#include "AudioALSACaptureDataProviderBase.h"

extern "C" {
    //#include <tinyalsa/asoundlib.h>
#include "alsa_device_profile.h"
#include "alsa_device_proxy.h"
#include "alsa_logging.h"
#include <audio_utils/channels.h>
}


namespace android {
class IAudioALSACaptureDataClient;

class AudioALSACaptureDataProviderUsb : public AudioALSACaptureDataProviderBase {
public:
    virtual ~AudioALSACaptureDataProviderUsb();

    static AudioALSACaptureDataProviderUsb *getInstance();

    /**
     * open/close pcm interface when 1st attach & the last detach
     */

    virtual status_t open();
    virtual status_t close();
    virtual void initUsbInfo(stream_attribute_t stream_attribute_source_usb, alsa_device_proxy *proxy, size_t buffer_size, bool dl_on);
    virtual bool isNeedEchoRefData();

protected:
    AudioALSACaptureDataProviderUsb();

    status_t updateStartTimeStamp(struct timespec timeStamp);

    status_t updateCaptureTimeStampByStartTime(uint32_t bufferSize);

    struct timespec  mCaptureStartTime;

    struct timespec mEstimatedBufferTimeStamp;



private:
    /**
     * singleton pattern
     */
    static AudioALSACaptureDataProviderUsb *mAudioALSACaptureDataProviderUsb;
    int prepareUsb(void);

    /**
     * pcm read thread
     */
    static void *readThread(void *arg);
    pthread_t hReadThread;

    struct timespec mNewtime, mOldtime; //for calculate latency
    double timerec[3]; //0=>threadloop, 1=>kernel delay, 2=>process delay

    alsa_device_proxy mProxy;
    alsa_device_proxy *usbProxy;

    bool usbVoipMode;
    bool mIsUsbHAL;
};

}

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_NORMAL_H
