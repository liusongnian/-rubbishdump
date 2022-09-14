#ifndef ANDROID_AUDIO_ALSA_CAPTURE_DATA_CLIENT_DSP_H
#define ANDROID_AUDIO_ALSA_CAPTURE_DATA_CLIENT_DSP_H

#include "IAudioALSACaptureDataClient.h"

#include <pthread.h>


#include "AudioType.h"
#include "AudioUtility.h"

#include <audio_ringbuf.h>

#include <AudioLock.h>



namespace android {

class AudioALSACaptureDataProviderBase;
class MtkAudioSrcBase;
class MtkAudioBitConverterBase;

/* TODO: base class */
class AudioALSACaptureDataClientDsp : public IAudioALSACaptureDataClient {
public:
    AudioALSACaptureDataClientDsp(
        AudioALSACaptureDataProviderBase *pCaptureDataProvider,
        stream_attribute_t *stream_attribute_target);

    virtual ~AudioALSACaptureDataClientDsp();


    /**
     * set client index
     */
    inline void        *getIdentity() const { return (void *)this; }

    /**
     * set/get raw frame count from hardware
     */
    inline void setRawStartFrameCount(int64_t frameCount) { mRawStartFrameCount = frameCount * mStreamAttributeTarget->sample_rate / mStreamAttributeTargetDSP.sample_rate; }
    virtual inline int64_t getRawStartFrameCount() { return mRawStartFrameCount; }

    /**
     * let provider copy raw data to client
     */
    virtual uint32_t    copyCaptureDataToClient(RingBuf pcm_read_buf); // called by capture data provider


    /**
     * let handler read processed data from client
     */
    virtual ssize_t     read(void *buffer, ssize_t bytes); // called by capture handler


    /**
     * check if the attached client has low latency requirement
     */
    bool    IsLowLatencyCapture(void);

    /**
     * Update volume index to SWIP
     */
    status_t updateVolumeIndex(int stream, int device, int index);

    /**
     * Query captured frames & time stamp
     */
    int getCapturePosition(int64_t *frames, int64_t *time);


    /**
     * EchoRef
     */
    void AddEchoRefDataProvider(AudioALSACaptureDataProviderBase *pCaptureDataProvider, stream_attribute_t *stream_attribute_target);
    uint32_t copyEchoRefCaptureDataToClient(RingBuf pcm_read_buf);


    /**
     * Update BesRecord Parameters
     */
    status_t UpdateBesRecParam() { return INVALID_OPERATION; } /* TODO: remove it */


    /**
     * Attribute
     */
    const stream_attribute_t *getStreamAttributeSource() { return mStreamAttributeSource; }
    const stream_attribute_t *getStreamAttributeTarget() { return mStreamAttributeTarget; }
    void setStreamAttributeTargetDSP(stream_attribute_t attrDsp) { mStreamAttributeTargetDSP = attrDsp; }

    virtual AudioALSACaptureDataProviderBase *getCaptureDataProvider() { return mCaptureDataProvider; }

    /**
     * StreamIn reopen
     */
    virtual bool getStreamInReopen() { return mStreamInReopen; }
    virtual void setStreamInReopen(bool state) { mStreamInReopen = state; }

private:
    AudioALSACaptureDataClientDsp() {}


    /**
     * stream attribute
     */
    const stream_attribute_t *mStreamAttributeSource; // from audio hw
    stream_attribute_t       *mStreamAttributeTarget; // to stream in
    stream_attribute_t       mStreamAttributeTargetDSP; // to stream DSP

    bool isVoIPEnable(void) { return mStreamAttributeTarget->mVoIPEnable; }

    /**
     * for data provider
     */
    AudioALSACaptureDataProviderBase *mCaptureDataProvider;
    int64_t mRawStartFrameCount;

    /**
     * gain control
     */
    bool IsNeedApplyVolume();
    status_t ApplyVolume(void *Buffer, uint32_t BufferSize);
    bool mMicMute;
    bool mMuteTransition;

    void configDSPAttribute();

    /**
     * process raw data to processed data
     */
    bool            mEnable;

    bool            mStreamInReopen;

    static void    *processThread(void *arg);
    pthread_t       hProcessThread;

    audio_ringbuf_t mRawDataBuf;
    char           *mRawDataBufLinear;
    AudioLock       mRawDataBufLock;

    audio_ringbuf_t mProcessedDataBuf;
    AudioLock       mProcessedDataBufLock;


    /**
     * Bli SRC + Bit Converter
     */
    void *mFmtConvHdl;


    /**
     * Bit Converter
     */
    status_t         initBitConverter();
    status_t         deinitBitConverter();
    status_t         doBitConversion(void *pInBuffer, uint32_t inBytes, void **ppOutBuffer, uint32_t *pOutBytes);

    MtkAudioBitConverterBase *mBitConverter;
    char                     *mBitConverterOutputBuffer;

    /**
     * Depop
     */
    uint32_t        mDropPopSize;

};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_DATA_CLIENT_SYNC_IO_H

