#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_CLIENT_IEMS_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_CLIENT_IEMS_H

#include <IAudioALSACaptureDataClient.h>

#include <pthread.h>
#include <system/audio.h>

#include <AudioType.h>
#include <AudioUtility.h>

#include <audio_ringbuf.h>

#include <AudioLock.h>


// Aurisys Framework
struct aurisys_lib_manager_t;
struct aurisys_lib_manager_config_t;
struct audio_pool_buf_t;
struct data_buf_t;


class AudioVolumeInterface;


namespace android {

typedef int (*IEMsCbk)(
    void *buffer,
    uint32_t bytes,
    void *arg);


class AudioALSACaptureDataProviderBase;

class AudioALSACaptureDataClientIEMs : public IAudioALSACaptureDataClient {
public:
    static AudioALSACaptureDataClientIEMs *getInstance();
    virtual ~AudioALSACaptureDataClientIEMs();

    int open(stream_attribute_t *attrMixerIn,
             IEMsCbk writeCbk,
             void *writeArg);
    int close(void);
    bool isEnabled(void) { return mEnable; }

    int hookCaptureHandler(IEMsCbk readCbk, void *readArg);
    int unhookCaptureHandler(void);

    AudioALSACaptureDataProviderBase *getCaptureDataProvider() { return mCaptureDataProvider; }
    capture_provider_t getDataProviderType() { return mCaptureDataProviderType; }

    /**
     * set client index
     */
    inline void        *getIdentity() const { return (void *)this; }

    /**
     * set/get raw frame count from hardware
     */
    inline void setRawStartFrameCount(int64_t frameCount);
    virtual inline int64_t getRawStartFrameCount() { return mRawStartFrameCount; }

    /**
     * let provider copy raw data to client
     */
    virtual uint32_t    copyCaptureDataToClient(RingBuf pcm_read_buf); // called by capture data provider


    /**
     * let handler read processed data from client
     */
    ssize_t read(void *buffer __unused, ssize_t bytes __unused) { return 0; }


    /**
     * check if the attached client has low latency requirement
     */
    bool    IsLowLatencyCapture(void);


    /**
     * Query captured frames & time stamp
     */
    int getCapturePosition(int64_t *frames, int64_t *time);


    /**
     * EchoRef
     */
    void AddEchoRefDataProvider(AudioALSACaptureDataProviderBase *pCaptureDataProvider __unused, stream_attribute_t *stream_attribute_target __unused) { }
    uint32_t copyEchoRefCaptureDataToClient(RingBuf pcm_read_buf __unused) { return 0; }


    /**
     * Update BesRecord Parameters
     */
    status_t UpdateBesRecParam() { return NO_ERROR; }


    /**
     * Attribute
     */
    const stream_attribute_t *getStreamAttributeSource() { return mAttrDataProvider; }
    const stream_attribute_t *getStreamAttributeTarget() { return mAttrIEMsIn; }
    const stream_attribute_t *getAttrIEMsIn() { return mAttrIEMsIn; }



private:
    static AudioALSACaptureDataClientIEMs *mAudioALSACaptureDataClientIEMs;

    AudioALSACaptureDataClientIEMs();

    AudioALSACaptureDataProviderBase *getDataProvider(const audio_devices_t input_device);



    /**
     * stream attribute
     */
    const stream_attribute_t *mAttrIEMsIn;
    const stream_attribute_t *mAttrDataProvider;

    /**
     * for data provider
     */
    AudioALSACaptureDataProviderBase *mCaptureDataProvider;
    capture_provider_t mCaptureDataProviderType;
    int64_t mRawStartFrameCount;

    /**
     * gain
     */
    AudioVolumeInterface *mAudioALSAVolumeController;

    /**
     * process raw data to processed data
     */
    bool            mEnable;
    AudioLock       mLock;

    static void    *processThread(void *arg);
    pthread_t       hProcessThread;
    bool            mProcessThreadLaunched;

    bool            mProcessThreadWaitSync;
    AudioLock       mProcessThreadSyncLock;

    uint32_t        mPeriodUs;

    audio_ringbuf_t mRawDataBuf;
    AudioLock       mRawDataBufLock;
    uint32_t        mRawDataPeriodBufSize;


    /**
     * Depop
     */
    uint32_t mDropPopSize;


    /**
     * Callback
     */
    IEMsCbk mWriteCbk;
    void *mWriteArg;

    IEMsCbk mReadCbk;
    void *mReadArg;
    AudioLock mhookCaptureHandlerLock;


    /**
     * format converter
     */
    void *mFmtConvHdl;


    /**
     * Aurisys Framework
     */
    void CreateAurisysLibManager();
    void InitArsiTaskConfig(struct aurisys_lib_manager_config_t *pManagerConfig);
    void InitBufferConfig(struct aurisys_lib_manager_t *manager);
    void DestroyAurisysLibManager();

    struct aurisys_lib_manager_t *mAurisysLibManager;
    struct aurisys_lib_manager_config_t *mManagerConfig;
    uint32_t mAurisysScenario;

    audio_pool_buf_t *mAudioPoolBufUlIn;
    audio_pool_buf_t *mAudioPoolBufUlOut;
    struct data_buf_t *mLinearOut;
};

} /* end namespace android */



#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_CLIENT_IEMS_H

