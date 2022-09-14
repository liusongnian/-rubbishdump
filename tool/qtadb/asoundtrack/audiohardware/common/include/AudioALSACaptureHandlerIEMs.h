#ifndef ANDROID_AUDIO_ALSA_CAPTURE_HANDLER_IEMS_H
#define ANDROID_AUDIO_ALSA_CAPTURE_HANDLER_IEMS_H

#include <AudioALSACaptureHandlerBase.h>

#include <AudioLock.h>

#include <audio_ringbuf.h>



namespace android {

class AudioALSACaptureDataClientIEMs;

class AudioALSACaptureHandlerIEMs : public AudioALSACaptureHandlerBase {
public:
    AudioALSACaptureHandlerIEMs(stream_attribute_t *stream_attribute_target);
    virtual ~AudioALSACaptureHandlerIEMs();

    /**
     * open/close audio hardware
     */
    virtual status_t open();
    virtual status_t close();
    virtual status_t routing(const audio_devices_t input_device);


    /**
     * read data from audio hardware
     */
    virtual ssize_t  read(void *buffer, ssize_t bytes);
    virtual int64_t getRawStartFrameCount();
    virtual int getCapturePosition(int64_t *frames, int64_t *time);


private:
    static int wrapReadCbk(void *buffer,
                           uint32_t bytes,
                           void *arg);

    uint32_t copyCaptureDataToHandler(void *buffer,
                                      uint32_t bytes);

    bool mEnable;
    AudioLock mLock;

    bool mMicMute;
    bool mMuteTransition;

    AudioALSACaptureDataClientIEMs *mCaptureDataClientIEMs;
    const stream_attribute_t *mStreamAttributeSource;

    audio_ringbuf_t mIEMsDataBufTmp;
    audio_ringbuf_t mIEMsDataBuf;
    AudioLock       mIEMsDataBufLock;

    void *mBufIEMs;
    uint32_t mBufIEMsSz;

    void *mFmtConvHdl;
    audio_ringbuf_t mSrcDataBuf;
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_CAPTURE_HANDLER_IEMS_H
