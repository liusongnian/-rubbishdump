#ifndef ANDROID_AUDIO_ALSA_DRIVER_UTILITY_H
#define ANDROID_AUDIO_ALSA_DRIVER_UTILITY_H

#include <tinyalsa/asoundlib.h>
#include <AudioLock.h>

namespace android {

class AudioALSADriverUtility {
public:
    virtual ~AudioALSADriverUtility();

    static AudioALSADriverUtility *getInstance();

    struct mixer *getMixer() const { return mMixer; }

    int GetPropertyValue(const char *ProPerty_Key);

    int setPropertyValue(const char *ProPerty_Key, int value);

    inline AudioLock *getStreamSramDramLock() { return &mStreamSramDramLock; }

    int mixerCtrlGetValueByName(const char *name, unsigned int id);
    int mixerCtrlSetValueByName(const char *name, unsigned int id, int value);
    int mixerCtrlSetEnumByName(const char *name, unsigned int id, const char *string);

    struct mixer_ctl *getMixerCtrlByName(struct mixer *mixer, const char *name);
    int mixerCtrlGetValue(struct mixer_ctl *ctl, unsigned int id);
    int mixerCtrlSetValue(struct mixer_ctl *ctl, unsigned int id, int value);

private:
    AudioALSADriverUtility();


    /**
     * singleton pattern
     */
    static AudioALSADriverUtility *mAudioALSADriverUtility;


    /**
     * singleton pattern
     */
    struct mixer *mMixer;

    /**
     * Lock for pcm open & close
     */
    AudioLock mStreamSramDramLock; // protect stream in/out sram/dram allocation mechanism
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_DRIVER_UTILITY_H
