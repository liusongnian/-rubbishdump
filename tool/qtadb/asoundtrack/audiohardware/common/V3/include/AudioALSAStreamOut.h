#ifndef ANDROID_AUDIO_ALSA_STREAM_OUT_H
#define ANDROID_AUDIO_ALSA_STREAM_OUT_H

#include "AudioSystemLibUtil.h"

#include <hardware_legacy/AudioMTKHardwareInterface.h>
#include <hardware/audio.h>
#include <media/AudioParameter.h>

#include "AudioType.h"
#include <AudioLock.h>

#include "AudioALSAStreamManager.h"
#include "AudioALSAPlaybackHandlerBase.h"

namespace android {

class AudioALSAStreamManager;

enum stream_out_type {
    STREAM_OUT_PRIMARY = 0,
    STREAM_OUT_HDMI_STEREO = 1,
    STREAM_OUT_HDMI_MULTI_CHANNEL = 2,
    STREAM_OUT_VOICE_DL,
    NUM_STREAM_OUT_TYPE
};

class AudioALSAStreamOut : public AudioMTKStreamOutInterface {
public:
    AudioALSAStreamOut();
    virtual ~AudioALSAStreamOut();

    virtual status_t set(uint32_t devices,
                         int *format,
                         uint32_t *channels,
                         uint32_t *sampleRate,
                         status_t *status,
                         uint32_t flags);

    /** return audio sampling rate in hz - eg. 44100 */
    virtual uint32_t    sampleRate() const;

    /** returns size of output buffer - eg. 4800 */
    virtual size_t      bufferSize() const;

    /**
     * return the corresponding time of hal buffer in micro seconds
     */
    virtual uint32_t    bufferSizeTimeUs() const;

    /** returns the output channel mask */
    virtual uint32_t    channels() const;

    /**
     * return audio format in 8bit or 16bit PCM format -
     * eg. AUDIO_FORMAT_PCM_16_BIT
     */
    virtual int         format() const;

    /**
     * return the audio hardware driver latency in milli seconds.
     */
    virtual uint32_t    latency();

    /**
     * Use this method in situations where audio mixing is done in the
     * hardware. This method serves as a direct interface with hardware,
     * allowing you to directly set the volume as apposed to via the framework.
     * This method might produce multiple PCM outputs or hardware accelerated
     * codecs, such as MP3 or AAC.
     */
    virtual status_t    setVolume(float left, float right);

    /** write audio buffer to driver. Returns number of bytes written */
    virtual ssize_t     write(const void *buffer, size_t bytes);
    virtual int drain(audio_drain_type_t type);
    /**
     * Put the audio hardware output into standby mode. Returns
     * status based on include/utils/Errors.h
     */
    // "standby" is used by framework to close stream out.
    virtual status_t    standby(bool halRequest = true);
    // "standbyStreamOut" is used by HAL to close stream out.
    virtual status_t    standbyStreamOut(bool halRequest = true);

    /** dump the state of the audio output device */
    virtual status_t dump(int fd, const Vector<String16> &args);

    // set/get audio output parameters. The function accepts a list of parameters
    // key value pairs in the form: key1=value1;key2=value2;...
    // Some keys are reserved for standard parameters (See AudioParameter class).
    // If the implementation does not accept a parameter change while the output is
    // active but the parameter is acceptable otherwise, it must return INVALID_OPERATION.
    // The audio flinger will put the output in standby and then change the parameter value.
    virtual status_t    setParameters(const String8 &keyValuePairs);
    virtual String8     getParameters(const String8 &keys);

    // return the number of audio frames written by the audio dsp to DAC since
    // the output has exited standby
    virtual int    getRenderPosition(uint32_t *dspFrames);


    /**
     * set stream out index
     */
    inline void         setIdentity(const uint32_t identity) { mStreamAttributeSource.mStreamOutIndex = mIdentity = identity; }
    inline uint32_t     getIdentity() const { return mIdentity; }


    /**
     * open/close stream out related audio hardware
     */
    virtual status_t    open();
    virtual status_t    close();
    virtual int    pause();
    virtual int    resume();
    virtual int    flush();

    bool isOutPutStreamActive();


    /**
     * routing
     */
    status_t routing(audio_devices_t output_devices);
    void updatePolicyDevice(audio_devices_t outputDevice);


    /**
     * suspend/resume
     */
    status_t setSuspend(const bool suspend_on);
    static status_t setSuspendStreamOutHDMIStereo(const bool suspend_on);

    /**
     * set mute for routing, must be set after standby, it will reset to false in standby()
     */
    status_t setMuteForRouting(bool mute);

    /**
     * get stream attribute
     */
    virtual const stream_attribute_t *getStreamAttribute() const { return &mStreamAttributeSource; }


    /**
     * low latency
     */
    status_t setScreenState(bool mode);

    /**
     * update mode
     */
    int updateAudioMode(audio_mode_t mode);

    /**
     * update volume index
     */
    status_t updateVolumeIndex(int stream, int device, int index);

    /**
     *
     */
    status_t UpdateSampleRate(int samplerate);

    /**
     *
     */
    bool  SampleRateSupport(uint32_t samplerate);

    virtual int getPresentationPosition(uint64_t *frames, struct timespec *timestamp);//Add for KK

    /**
     * Called when the metadata of the stream's source has been changed.
     * @param source_metadata Description of the audio that is played by the clients.
     */
    virtual void updateSourceMetadata(const struct source_metadata* source_metadata);

    virtual status_t    setCallBack(stream_callback_t callback, void *cookie);
    //Force below two functions to pure virtual
    virtual status_t    getNextWriteTimestamp(int64_t *timestamp);

    /**
     * set streamout samplerate by sampleRate
     */
    int setStreamOutSampleRate(const uint32_t sampleRate);

    /**
     * set streamout flags
     */
    int setStreamOutOutputFlags(const audio_output_flags_t flag, const bool enable);

    status_t syncPolicyDevice();

    /**
     * AAudio MMAP
     */
    virtual status_t    start();
    virtual status_t    stop();
    virtual status_t    createMmapBuffer(int32_t min_size_frames,
                                  struct audio_mmap_buffer_info *info);
    virtual status_t    getMmapPosition(struct audio_mmap_position *position);


    status_t dynamicSetStreamOutAudioDump();

    status_t logDumpSize();

protected:
    AudioALSAStreamManager         *mStreamManager;
    AudioALSAPlaybackHandlerBase   *mPlaybackHandler;
    /**
     * inner standby function which need locking first
     */
    status_t standby_l(bool halRequest);

    /**
     * for debug PCM dump
     */
    void  OpenPCMDump(const char *class_name);
    void  ClosePCMDump(void);
    void  WritePcmDumpData(const void *buffer, ssize_t bytes);
    FILE *mPCMDumpFile;
    static uint32_t mDumpFileNum;

    /**
     * for debug WAV dump
     */
    void openWavDump(const char *className);
    void closeWavDump();
    void writeWavDumpData(const void *buffer, uint32_t bytes);
    bool isWavDumpEnabled();

    FILE *mDumpFile;
    uint32_t mBytesWavDumpWritten;
    AudioLock mAudioDumpLock;

    /**
     * low latency
     */
    status_t setScreenState_l() {
        status_t ret = NO_ERROR;
        if (NULL != mPlaybackHandler) {
#if defined(MTK_HYBRID_NLE_SUPPORT)
            ret = mPlaybackHandler->setScreenState(mLowLatencyMode != 0, mStreamAttributeSource.buffer_size,
                                                   (KERNEL_BUFFER_FRAME_COUNT_REMAIN << 1));
#else
            ret = mPlaybackHandler->setScreenState(mLowLatencyMode != 0, mStreamAttributeSource.buffer_size,
                                                   KERNEL_BUFFER_FRAME_COUNT_REMAIN);
#endif
        }
        return ret;
    }

    /**
     * mute/unmute process
     */
    status_t dataProcessForMute(const void *buffer, size_t bytes);

private:
    AudioLock           mLock;
    volatile int32_t    mLockCount;
    AudioLock           mSuspendLock;

    uint32_t            mIdentity; // key for mStreamOutVector

    uint32_t     mSuspendCount;
    static uint32_t     mSuspendStreamOutHDMIStereoCount;
    bool                mSuspendIEMsForVoIP;

    bool                mMuteForRouting;
    struct timespec     mMuteTime;
    struct timespec     mMuteCurTime;

    bool                mStandby;
    stream_attribute_t  mStreamAttributeSource;

    stream_out_type     mStreamOutType;
    static AudioALSAStreamOut *mStreamOutHDMIStereo;
    uint64_t mPresentedBytes;
    timespec mPresentedTime;
    uint64_t mPresentFrames;
    uint64_t mPreviousFrames;
    struct timespec mPretimestamp;

    bool                mLowLatencyMode;
    bool                mOffload;
    bool                mPaused;
    stream_callback_t   mStreamCbk;
    void                *mCbkCookie;
    uint32_t            mOffloadVol;
    uint32_t            mLatency;

    void setBufferSize();
    void updateLatency_l();

    /*
     * flag of dynamic enable verbose/debug log
     */
    int mLogEnable;

    /**
     * AAudio MMAP
     */
    bool                mStart;
    bool                mDestroy;

    /**
     * ADSP recovery
     */
    void *mAdspRecoveryLock;
};

} // end namespace android

#endif // end of ANDROID_SPEECH_DRIVER_INTERFACE_H
