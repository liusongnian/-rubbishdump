#ifndef ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_MIXER_H
#define ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_MIXER_H

#include <unordered_map>

#include "AudioALSAPlaybackHandlerBase.h"

namespace android {

struct MixerTarget;

class AudioALSAPlaybackHandlerMixer : public AudioALSAPlaybackHandlerBase {
public:
    AudioALSAPlaybackHandlerMixer(const stream_attribute_t *stream_attribute_source);
    virtual ~AudioALSAPlaybackHandlerMixer();

    /**
     * open/close audio hardware
     */
    virtual status_t open();
    virtual status_t close();
    virtual status_t routing(const audio_devices_t output_devices);

    virtual int getLatency();

    virtual int setSuspend(bool suspend);

    /**
     * write data to audio hardware
     */
    virtual ssize_t  write(const void *buffer, size_t bytes);
    virtual int preWriteOperation(const void *buffer __unused, size_t bytes __unused) { return 0; }
    virtual int updateAudioMode(audio_mode_t mode __unused) { return 0; }

    /**
     * get hardware buffer info (framecount)
     */
    virtual status_t getHardwareBufferInfo(time_info_struct_t *HWBuffer_Time_Info);
    virtual int getPresentationPosition(uint64_t totalWriteFrames, uint64_t *presentedFrames, struct timespec *timestamp);

    /**
     * low latency
     */
    virtual status_t setScreenState(bool mode, size_t buffer_size, size_t reduceInterruptSize, bool bforce = false);

private:
    void openMixerSourceHandler(void);
    void closeMixerSourceHandler(void);

    void configPrimaryAttribute(stream_attribute_t *attr);
    void configEarphoneRingtoneAttribute(stream_attribute_t *attr);
    void lunchMixerTargetHandler(
        AudioALSAPlaybackHandlerBase **playbackHdl,
        void **targetHdl,
        stream_attribute_t *attr,
        const uint32_t mixer_id,
        const uint32_t host_order);
    void openMixerTargetHandler(void);
    void closeMixerTargetHandler(void);


    void *mSourceHdl;
    bool mIsFirstSource;
    uint64_t mTotalWriteFrames;

    struct MixerTarget *mMixerTarget;
    uint64_t mTargetPresentedFramesQueued;
    uint64_t mTargetPresentedFramesOffset;
    bool mIsTargetPresentedFramesOffsetValid;

    bool mIsPreviousFramesValid;

    static std::unordered_map<audio_devices_t, struct MixerTarget *> mMixerTargetList;
    static AudioLock mMixerTargetListLock;

    static std::unordered_map<audio_output_flags_t, stream_attribute_t> mOutputBufferAttr;


    void updateOutputBufferAttr(const stream_attribute_t *attr);
    uint32_t getOutputBufferSize(const stream_attribute_t *attr_src,
                                 const stream_attribute_t *attr_tgt);


    bool deviceSupportHifi(audio_devices_t outputdevice);
    uint32_t chooseTargetSampleRate(uint32_t SampleRate, audio_devices_t outputdevice);
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_MIXER_H
