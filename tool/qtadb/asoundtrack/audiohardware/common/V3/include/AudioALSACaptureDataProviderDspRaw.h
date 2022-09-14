#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_DSP_RAW_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_DSP_RAW_H

#include "AudioALSACaptureDataProviderBase.h"
#include "AudioDspType.h"

struct aurisys_dsp_config_t;
struct aurisys_lib_manager_t;
class AudioVolumeInterface;

namespace android {

class AudioALSACaptureDataProviderDspRaw : public AudioALSACaptureDataProviderBase {
public:
    virtual ~AudioALSACaptureDataProviderDspRaw();

    static AudioALSACaptureDataProviderDspRaw *getInstance();

    /**
     * open/close pcm interface when 1st attach & the last detach
     */
    status_t open();
    status_t close();
    virtual void updateInputSource() { mUpdateInputSource = true; };



protected:
    AudioALSACaptureDataProviderDspRaw();


private:
    /**
     * singleton pattern
     */
    static AudioALSACaptureDataProviderDspRaw *mAudioALSACaptureDataProviderDspRaw;
    struct mixer *mMixer;

    int setDspRuntimeEn(bool condition);

    void setApHwPcm();
    status_t openApHwPcm();


    /**
     * pcm read thread
     */
    static void *readThread(void *arg);
    pthread_t hReadThread;

    uint32_t mCaptureDropSize;

    struct timespec mNewtime, mOldtime; //for calculate latency
    double timerec[3]; //0=>threadloop, 1=>kernel delay, 2=>process delay
    void  adjustSpike();

    struct pcm_config mDsphwConfig;

    String8 mDspRefTurnOnSequence;
    bool mUpdateInputSource;

    /* Volume Controller */
    AudioVolumeInterface *mAudioALSAVolumeController;
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_DSP_RAW_H
