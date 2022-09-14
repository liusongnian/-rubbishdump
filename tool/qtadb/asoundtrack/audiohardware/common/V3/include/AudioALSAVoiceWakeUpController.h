#ifndef ANDROID_ALSA_AUDIO_VOICE_WAKE_UP_CONTROLLER_H
#define ANDROID_ALSA_AUDIO_VOICE_WAKE_UP_CONTROLLER_H

#include <tinyalsa/asoundlib.h>

#include "AudioType.h"
#include "AudioParamParser.h"
#include <AudioLock.h>

#include "AudioALSACaptureHandlerBase.h"
#include "AudioALSACaptureHandlerVOW.h"

enum VOICE_COMMAND {
    VOICE_COMMAND_1ST,
    VOICE_COMMAND_2ND,
    VOICE_COMMAND_3RD,
};

enum SINGLE_MIC_INDEX {
	MIC_INDEX_IDLE = 0,
	MIC_INDEX_MAIN,
	MIC_INDEX_REF,
	MIC_INDEX_THIRD,
	MIC_INDEX_HEADSET,
	MIC_INDEX_NUM,
};

namespace android {
//class AudioALSACaptureDataProviderBase;

class AudioALSADeviceConfigManager;
class AudioALSACaptureDataProviderVOW;

class AudioALSAVoiceWakeUpController {
public:
    virtual ~AudioALSAVoiceWakeUpController();

    static AudioALSAVoiceWakeUpController *getInstance();

    virtual status_t setVoiceWakeUpEnable(const bool enable);
    virtual bool     getVoiceWakeUpEnable();

    virtual status_t updateDeviceInfoForVoiceWakeUp();

    virtual status_t updateVadParam();
    virtual status_t updateDspAecParam();
    virtual status_t updateVoiceCommandParam();
    virtual bool    getVoiceWakeUpStateFromKernel();
    virtual unsigned int getVOWMicType();
    virtual void setBargeInBypass(const bool enable);
    virtual void setBargeInForceOff(const bool enable);
    virtual void doBargeInRecovery(void);

    virtual status_t SeamlessRecordEnable();

    bool updateSpeakerPlaybackStatus(bool isSpeakerPlaying);
    bool setSpeakerSampleRate(uint32_t sampleRate);

    String8 getAlexaDspLibVersion(void);

    String8 getVoiceCommand(VOICE_COMMAND voiceCommand);

protected:
    AudioALSAVoiceWakeUpController();

    virtual status_t updateVadParamToKernel();
    virtual bool setBargeInEnable(const bool enable);

private:
    /**
     * singleton pattern
     */
    static AudioALSAVoiceWakeUpController *mAudioALSAVoiceWakeUpController;

    AudioALSACaptureDataProviderVOW *mVOWCaptureDataProvider;
    AudioALSACaptureHandlerBase *mCaptureHandler;
    stream_attribute_t *stream_attribute_target;
    status_t setVoiceWakeUpDebugDumpEnable(const bool enable);

    /**
     * For single mic mode, return selected single mic index.
     * @SINGLE_MIC_INDEX
     */
    int getSingleMicId();
    /**
     * For single mic mode, set single mode selected mic index.
     */
    int setMixerCtrlMicId(int micid);
    bool mDebug_Enable;

    struct mixer *mMixer; // TODO(Harvey): move it to AudioALSAHardwareResourceManager later
    struct pcm *mPcm;

    AudioLock mLock;

    bool mEnable;
    bool mWakeupEnableOngoing;

    bool mBargeInEnable;
    bool mBargeInEnableOngoing;
    bool mBargeInBypass;
    bool mBargeInForceOff;
    struct pcm *mBargeInPcm;
    struct pcm *mPcmHostlessUl;
    struct pcm *mPcmHostlessDl;
    String8 mBargeInTurnOnSequence;

    bool mIsUseHeadsetMic;
    AudioALSAHardwareResourceManager *mHardwareResourceManager;
    bool mIsNeedToUpdateVadParamToKernel;


    uint32_t mHandsetMicMode;
    uint32_t mHeadsetMicMode;
    AudioALSADeviceConfigManager *mDeviceConfigManager;
    AppHandle *mAppHandle;

    static void *dumyReadThread(void *arg);
    pthread_t hDumyReadThread;
    bool mDumpReadStart;
    AudioLock mDebugDumpLock;
    int mFd_dnn;
    AudioLock mSeamlessLock;
    int mFd_vow;
    bool mIsSpeakerPlaying;
    uint32_t mSpeakerSampleRate;

    struct pcm_config mSrcDlConfig;
    struct pcm_config mSrcUlConfig;
    unsigned int mVowChannel;

    String8 mVoiceCommand;
    String8 mVoiceCommand2nd;
    String8 mVoiceCommand3rd;
};

} // end namespace android

#endif // end of ANDROID_ALSA_AUDIO_VOICE_WAKE_UP_CONTROLLER_H
