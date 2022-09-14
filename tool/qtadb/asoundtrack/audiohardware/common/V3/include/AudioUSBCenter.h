#ifndef ANDROID_AUDIO_USB_CENTER_H
#define ANDROID_AUDIO_USB_CENTER_H

#include <system/audio.h>
extern "C" {
#include <alsa_device_profile.h>
#include <alsa_device_proxy.h>
}

#include <unordered_map>

#include "AudioLock.h"


#define USB_GET_PROP_TIMEOUT_US (2000) /* 2ms */



struct stream_attribute_t;

namespace android {

class AudioUSBCenter {
public:
    static AudioUSBCenter *getInstance();
    ~AudioUSBCenter();

    int setUSBOutConnectionState(audio_devices_t devices, bool connect, int card, int device);
    int setUSBInConnectionState(audio_devices_t devices, bool connect, int card, int device);
    bool getUSBConnectionState(audio_devices_t devices);

    int setParameter(alsa_device_profile *profile, const char *kvpairs);
    int outSetParameter(const char *kvpairs);
    char *outGetParameter(const char *keys);
    char *inGetParameter(const char *keys);

    int inSetParameter(const char *kvpairs);

    alsa_device_profile *getProfile(const int direction);

    audio_format_t getHighestAudioFmt(const int direction);
    uint32_t getHighestSampleRate(const int direction);
    uint32_t getIEMsPeriodUs();
    uint32_t getOutPeriodUs();
    uint32_t getInPeriodUs(const bool isFast);

    int prepareUsb(alsa_device_proxy *proxy,
                   stream_attribute_t *attr,
                   const int direction,
                   const uint32_t period_us,
                   const uint32_t period_count);



private:
    static AudioUSBCenter *mUSBCenter;
    AudioUSBCenter();

private:
    AudioLock mLock;

    alsa_device_profile mOutProfile;
    alsa_device_profile mInProfile;

    std::unordered_map<audio_devices_t, bool> mIsDevConn;
};

}
#endif
