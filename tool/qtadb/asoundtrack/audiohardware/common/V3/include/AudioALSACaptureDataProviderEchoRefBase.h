#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF_BASE_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF_BASE_H

#include "AudioALSACaptureDataProviderBase.h"

#include <AudioLock.h>
#include <pthread.h>

namespace android {

class AudioALSACaptureDataProviderEchoRefBase : public AudioALSACaptureDataProviderBase {
public:
    virtual ~AudioALSACaptureDataProviderEchoRefBase();

    /**
     * open/close pcm interface when 1st attach & the last detach
     */
    virtual void configDefaultAttribute(void) = 0;
    virtual status_t open(void);
    virtual status_t close(void);

    /**
     * PlaybackHandler use this API to provide echo ref data
     */
    status_t writeData(const char *echoRefData, uint32_t dataSize, struct timespec *timestamp);

    /**
     * PlaybackHandler use this API to inform DL status
     */
    void attachPlaybackHandler(const stream_attribute_t *attr);
    void detachPlaybackHandler(void);


protected:
    AudioALSACaptureDataProviderEchoRefBase();

    /**
     * For echo ref SW implementation
     */
    uint32_t         mReadBufferSize;

    RingBuf          mDataRingBuf;
    AudioLock        mDataBufLock;
    struct timespec  mCaptureStartTime;
    struct timespec  mEstimateTimeStamp;
    struct timespec  mOldEstimateTimeStamp;
    uint32_t         mTotalCaptureBufSize;
    bool             mIsLowLatencyPlayback;

    void             initDataRingBuf(uint32_t size);
    void             deinitDataRingBuf();
    status_t         readData(char *buffer, uint32_t size);
    void             signalDataWaiting();
    status_t         GetCaptureTimeStampByStartTime(time_info_struct_t *Time_Info);
    void             updateStartTimeStamp(struct timespec *timeStamp);
    void             updateTotalCaptureBufSize(uint32_t captureSize);
    void             resetTimeStampInfo();
    uint32_t         compensateSilenceData(uint32_t msec, RingBuf *ringBuf);

    /**
     * pcm read thread
     */
    static void *readThread(void *arg);
    pthread_t hReadThread;

    struct timespec mNewtime, mOldtime; //for calculate latency
    double timerec[3]; //0=>threadloop, 1=>kernel delay, 2=>process delay

    AudioThrottleTimeControl *mAudioThrottleTimeControlInstance;

    /**
     * Set the thread priority
     */
    void setThreadPriority(void);
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_PROVIDER_ECHOREF_BASE_H
