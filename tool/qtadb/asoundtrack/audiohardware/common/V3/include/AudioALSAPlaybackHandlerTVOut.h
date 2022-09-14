#ifndef ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_TVOUT_H
#define ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_TVOUT_H

#include "AudioALSAPlaybackHandlerBase.h"


namespace android {

class AudioALSAPlaybackHandlerTVOut : public AudioALSAPlaybackHandlerBase {
public:
    AudioALSAPlaybackHandlerTVOut(const stream_attribute_t *mAttributeSource);
    virtual ~AudioALSAPlaybackHandlerTVOut();

    /*
     * open/close audio hardware
     */
    virtual status_t open();
    virtual status_t close();

    /*
     * write data to audio hardware
     */
    virtual ssize_t  write(const void *buffer, size_t bytes);

    virtual status_t setFilterMng(AudioMTKFilterManager *pFilterMng);
    virtual status_t routing(const audio_devices_t output_devices);

private:
    status_t setTVOutPlaybackInfo(int channels, int format, int sampleRate);
    status_t setTVOutEnable(int enable);
    String8 getPlaybackSequence(unsigned int turnOnSeqType,
                                const char *playbackSeq);

    int mTvOutFd;
    struct mixer *mMixer;
    struct timespec mNewtime, mOldtime;
    double latencyTime[3];
};

} // end namespace android

#endif
