#ifndef ANDROID_AUDIO_IEMS_CONTROLLER_H
#define ANDROID_AUDIO_IEMS_CONTROLLER_H

#include <AudioType.h>

#include <AudioLock.h>


namespace android {

enum {
    IEMS_SUSPEND_USER_DEV_CONNECT       = (1 << 0),
    IEMS_SUSPEND_USER_SET_MODE          = (1 << 1),
    IEMS_SUSPEND_USER_SET_MODE_ROUTING  = (1 << 2),
    IEMS_SUSPEND_USER_CALL_OPEN         = (1 << 3),
    IEMS_SUSPEND_USER_VOIP_RX           = (1 << 4),
    IEMS_SUSPEND_USER_VOIP_TX           = (1 << 5)
};

class AudioALSAStreamManager;
class AudioALSAPlaybackHandlerBase;
class AudioALSACaptureDataClientIEMs;

/* In-Ear Monitors */

class AudioIEMsController {
public:
    static AudioIEMsController *getInstance();
    virtual ~AudioIEMsController();

    /* set param conn/disconn */
    int connectDevice(const audio_devices_t device);
    int disconnectDevice(const audio_devices_t device);

    /* policy routing */
    int routing(const audio_devices_t output_devices);

    /* set param user turn on/off */
    int enable();
    int disable();

    /* avoid to be with ringtone/phone/voip/... */
    void suspend(const uint32_t mask);
    void resume(const uint32_t mask);

    /* report IEMs status */
    bool isIEMsEnable(void) { return mEnable; }
    bool isIEMsOn(void) { return mOpen; }

    /* get device */
    audio_devices_t getOutputDevice(void) { return mOutputDevice; }
    audio_devices_t getInputDevice(void) { return mInputDevice; }


protected:
    AudioIEMsController();


private:
    static AudioIEMsController *mAudioIEMsController;

    static int wrapWriteCbk(void *buffer, uint32_t bytes, void *arg);

    int open();
    int close();

    void createPlaybackHandler(void);
    void destoryPlaybackHandler(void);

    void createCaptureDataClient(void);
    void destroyCaptureDataClient(void);

    bool isAbleToRoutingByIEMs(void);

    bool mEnable;
    uint32 mSuspendMask;
    bool mOpen;

    AudioLock mLock;
    AudioLock mPlaybackHandlerLock;

    AudioLock mDevConnLock;
    bool mHasWiredHeadsetOut;
    bool mHasWiredHeadsetIn;
    bool mHasWiredHeadset;
    bool mHasBtHeadsetOut;
    bool mHasBtHeadsetIn;
    bool mHasBtHeadset;
    bool mHasUsbHeadsetOut;
    bool mHasUsbHeadsetIn;
    bool mHasUsbHeadset;
    bool mSuspendWhenAllDevDisconn;

    audio_devices_t mOutputDevice;
    audio_devices_t mInputDevice;
    bool mIsDeviceValidForIEMs;
    bool mIsWaitConnDone;

    uint32_t mSampleRate;
    uint32_t mPeriodUs;

    stream_attribute_t mAttrOut;
    stream_attribute_t mAttrIn;

    AudioALSAStreamManager *mStreamManager;
    AudioALSAPlaybackHandlerBase *mPlaybackHandler;
    AudioALSACaptureDataClientIEMs *mCaptureDataClient;
};

} /* end namespace android */



#endif // end of ANDROID_AUDIO_IEMS_CONTROLLER_H

