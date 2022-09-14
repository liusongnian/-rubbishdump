#ifndef ANDROID_AUDIO_ALSA_SPEECH_PHONE_CALL_CONTROLLER_H
#define ANDROID_AUDIO_ALSA_SPEECH_PHONE_CALL_CONTROLLER_H

#include <tinyalsa/asoundlib.h>
#include <media/AudioParameter.h>

#include <AudioType.h>
#include "SpeechType.h"

#include <AudioLock.h>
#include "AudioVolumeInterface.h"
#include "SpeechDriverInterface.h"
#include "AudioTypeExt.h"
#include "AudioParamParser.h"

namespace android {

class AudioALSAHardwareResourceManager;
class AudioALSAStreamManager;
class SpeechDriverFactory;
class AudioBTCVSDControl;
class AudioALSAVolumeController;



class AudioALSASpeechPhoneCallController {
public:
    virtual ~AudioALSASpeechPhoneCallController();

    static AudioALSASpeechPhoneCallController *getInstance();

    virtual audio_devices_t getInputDeviceForPhoneCall(const audio_devices_t output_devices);

    virtual status_t        open(const audio_mode_t audio_mode,
                                 const audio_devices_t output_devices,
                                 const audio_devices_t input_device);
    virtual status_t        close();
    virtual status_t        routing(const audio_devices_t new_output_devices, const audio_devices_t new_input_device);
    virtual audio_devices_t getAdjustedInputDevice();
    virtual audio_devices_t getAdjustedOutputDevice();
    virtual audio_devices_t getOutputDevice();

    virtual bool            checkSideToneFilterNeedOn(const audio_devices_t output_device) const;

    /**
     *  TTY & RTT
     */
    virtual bool            checkTtyNeedOn() const;
    inline TtyModeType      getTtyMode() const { return mTtyMode; }
    virtual status_t        setTtyMode(const TtyModeType ttyMode);
    virtual status_t        setTtyMode(const TtyModeType ttyMode, const int rttMode);
    virtual bool            isTtyDevice(audio_devices_t outputDevice) const;
    virtual int             setRttMode(const int rttMode);

    inline unsigned int     getSampleRate() const { return mConfig.rate; }

    virtual void            setVtNeedOn(const bool vt_on);
    virtual void            setMicMute(const bool mute_on);

    virtual void            setBTMode(const int mode);
    virtual void            setBtSpkDevice(const bool flag);
    virtual void            setDlMute(const bool mute_on);
    inline  bool            getDlMute() { return mDlMute; };
    virtual void            setDynamicDlMute(const bool mute_on);
    inline  bool            getDynamicDlMute() { return mDynamicDlMute; };
    virtual void            setUlMute(const bool mute_on);
    virtual void            getRFInfo();
    virtual char            *getSpeechNetworkName();
    virtual char            *getSpeechBandName();
    virtual status_t        setParam(const String8 &keyParamPairs);
    inline bool             isAudioTaste() { return bAudioTaste; };
    inline uint32_t         getSpeechDVT_SampleRate() { return mSpeechDVT_SampleRate; }
    inline uint32_t         getSpeechDVT_MD_IDX() { return mSpeechDVT_MD_IDX; }

    virtual void            muteDlForEnforcedAudibleSound(const bool mute_on);
    virtual void            updateVolume();
    virtual bool            checkReopen(const modem_index_t rilMappedMDIdx);
    virtual int             setPhoneId(const phone_id_t phoneId);

    inline phone_id_t       getPhoneId() { return mPhoneId; };
    inline modem_index_t    getIdxMDByPhoneId(uint8_t PhoneId) { return mIdxMDByPhoneId[PhoneId]; }
    /**
     * Speech Feature config
     */
    virtual int             updateSpeechFeature(const SpeechFeatureType speechFeatureType, bool enable);
    virtual bool            getSpeechFeatureStatus(const SpeechFeatureType speechFeatureType);
    virtual bool            getCallStatus();
    virtual void            setCustScene(const char* scene);

protected:
    AudioALSASpeechPhoneCallController();

    /**
     * init audio hardware
     */
    virtual status_t        init();

    inline uint32_t         calculateSampleRate(const bool bt_device_on) {
#if defined(MTK_COMBO_MODEM_SUPPORT)
    if (appIsFeatureOptionEnabled("MTK_PHONE_CALL_FIXED_48K_SAMPLERATE")) {
        return (bt_device_on == false) ? 48000 : (mBTMode == 0) ? 8000 : 16000;
    } else {
        return (bt_device_on == false) ? 32000 : (mBTMode == 0) ? 8000 : 16000;
    }
#else
    return (bt_device_on == false) ? 16000 : (mBTMode == 0) ? 8000 : 16000;
#endif

    }
    AudioALSAHardwareResourceManager *mHardwareResourceManager;
    AudioALSAStreamManager  *mStreamManager;
    AudioVolumeInterface    *mAudioALSAVolumeController;

    SpeechDriverFactory     *mSpeechDriverFactory;
    AudioBTCVSDControl      *mAudioBTCVSDControl;

    AudioLock               mLock;
    AudioLock               mCheckOpenLock;
    audio_mode_t            mAudioMode;
    bool                    mMicMute;
    bool                    mDlMute;
    bool                    mDynamicDlMute;
    bool                    mUlMute;
    bool                    mVtNeedOn;
    bool                    bAudioTaste;
    TtyModeType             mTtyMode;
    audio_devices_t         mInputDevice;//original device
    audio_devices_t         mOutputDevice;//original device
    audio_devices_t         mAdjustedInDev;
    audio_devices_t         mAdjustedOutDev;
    bool                    mIsBtSpkDevice;
    int                     mBTMode; // BT mode, 0:NB, 1:WB
    modem_index_t           mIdxMD; // Modem Index, 0:MD1, 1:MD2, 2: MD3
    struct pcm_config       mConfig;
    struct pcm              *mPcmIn;
    struct pcm              *mPcmOut;
    String8                 mApTurnOnSequenceIv;
    String8                 mApTurnOnSequence;
    String8                 mApTurnOnSequence2;
    String8                 mTurnOnSeqCustDev1;
    String8                 mTurnOnSeqCustDev2;
    uint16_t                mRfInfo, mRfMode, mASRCNeedOn;
    uint32_t                mSpeechDVT_SampleRate;
    uint32_t                mSpeechDVT_MD_IDX;
    char                    *mSpeechBandName;

private:
    static AudioALSASpeechPhoneCallController *mSpeechPhoneCallController; // singleton

    modem_index_t           updatePhysicalModemIdx(const audio_mode_t audio_mode);
    void                    muteDlUlForRouting(const int muteCtrl);
    virtual int             setTtyInOutDevice();
    virtual int             adjustTtyInOutDevice();
    void                    setMuteInfo();

    bool                    mIsSidetoneEnable;
    phone_id_t              mPhoneId;
    modem_index_t           mIdxMDByPhoneId[NUM_PHONE_ID];
    bool                    mCallStatus;

    /*
     * flag of dynamic enable verbose/debug log
     */
    int                     mLogEnable;

    /**
    *  pmic reset(debug for bring up)
    */
    static void             *pmicResetThread(void *arg);
    pthread_t               hPmicResetThread;
    bool                    mIsPmicResetThreadEnable;
    AudioLock               mPmicResetLock;

    /**
    *  Dynamic DL mute while enforced audible sound playback
    */
    static void             *muteDlForEnforcedAudibleSoundThread(void *arg);
    pthread_t               hMuteDlForEnforcedAudibleSoundThread;
    bool                    mMuteDlForEnforcedAudibleSoundThreadEnable;

    AudioLock               mMuteDlForEnforcedAudibleSoundLock;
    AudioLock               mMuteDlForEnforcedAudibleSoundCtrlLock;
    uint32_t                mMuteDlForEnforcedAudibleSoundCount;
    int                    mMuteDlForEnforcedAudibleSoundState;
    int                    mMuteDlForEnforcedAudibleSoundCtrl;

    /**
     *  DL/UL mute for routing
     */
    static void             *muteDlUlForRoutingThread(void *arg);
    pthread_t               mMuteDlUlForRoutingThread;
    bool                    mMuteDlUlForRoutingThreadEnable;
    AudioLock               mMuteDlUlForRoutingLock;
    int                     mMuteDlUlForRoutingState;
    int                     mMuteDlUlForRoutingCtrl;

    /**
     *  RTT
     */
    int                     mRttMode;

    /**
     *  Super Volume
     */
    bool                    mSuperVolumeEnable;

    /**
     *  Customized Scene Name
     */
    char mCustScene[MAX_SPEECH_FEATURE_CUST_SCENE_LEN];

};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_SPEECH_PHONE_CALL_CONTROLLER_H
