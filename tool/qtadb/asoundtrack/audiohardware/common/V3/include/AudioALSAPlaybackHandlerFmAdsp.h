#ifndef ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_FM_ADSP_H
#define ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_FM_ADSP_H

#include "AudioALSAPlaybackHandlerBase.h"
#include "AudioDspType.h"

namespace android {

class WCNChipController;

class AudioDspStreamManager;

class AudioALSAPlaybackHandlerFmAdsp : public AudioALSAPlaybackHandlerBase {
public:
    AudioALSAPlaybackHandlerFmAdsp(const stream_attribute_t *stream_attribute_source);
    virtual ~AudioALSAPlaybackHandlerFmAdsp();
    static AudioALSAPlaybackHandlerFmAdsp *getInstance(const stream_attribute_t *stream_attribute_source);

    /**
     * open/close audio hardware
     */
    virtual status_t open();
    virtual status_t close();
    virtual status_t routing(const audio_devices_t output_devices);

    /**
     * write data to audio hardware
     */
    virtual ssize_t  write(const void *buffer, size_t bytes);

    virtual void startFmAdspTask(const stream_attribute_t *attribute);
    virtual void stopFmAdspTask(void);
    void setFmDspShareMem(bool enable);
    virtual status_t openDspHwPcm();
    virtual status_t closeDspHwPcm();
    int setDspRuntimeEn(bool condition);

private:
    WCNChipController *mWCNChipController;
    static AudioALSAPlaybackHandlerFmAdsp *mPlaybackHandlerFmAdsp;

    uint8_t mTaskScene;

    struct pcm *mFmAdspUlPcm;
    struct pcm *mFmAdspDlPcm;
    struct pcm *mFmAdspPcm;
    struct pcm *mDspHwPcm;
    int mFmAdspUlIndex;
    int mFmAdspDlIndex;
    int mFmAdspIndex;
    int mStreamCardIndex;
    struct mixer *mMixer;
    struct pcm_config mFmAdspUlConfig;
    struct pcm_config mFmAdspDlConfig;
    struct pcm_config mFmAdspConfig;

};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_FM_ADSP_H
