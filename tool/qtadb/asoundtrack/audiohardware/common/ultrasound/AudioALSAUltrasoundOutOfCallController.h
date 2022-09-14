#ifndef ANDROID_AUDIO_ALSA_ULTRASOUND_OOC_CONTROLLER_H
#define ANDROID_AUDIO_ALSA_ULTRASOUND_OOC_CONTROLLER_H

#include "AudioType.h"
#include "AudioLock.h"
#include "AudioUtility.h"
#include <sys/prctl.h> /*  for prctl & PR_SET_NAME */
#include <sys/resource.h> /*  for PRIO_PROCESS */
#include "AudioALSAHardwareResourceManager.h"

namespace android
{

#define MAX_USECASE_NAME 256

extern const char* ultrasound_log_propty;

typedef enum {
    ULTRASOUND_OUT_CHANNEL_LEFT = 0,
    ULTRASOUND_OUT_CHANNEL_RIGHT,
    ULTRASOUND_OUT_CHANNEL_BOTH
} ultrasound_out_channel;

// enum for debug log control
enum {
    USND_CTL_FLOW_DEBUG = 0x1 << 0,
    USND_ROUTING_DEBUG  = 0x1 << 1
};

//no speaker_2in1  0
//speaker_2in1 and analog pa  1
//speaker_2in1 and smart pa 2
//speaker_2in1,smart pa and analog earpiece 3
enum {
    NO_SPEAKER_2IN1 = 0,
    SPEAKER_2IN1_ANALOG_PA = 1,
    SPEAKER_2IN1_SMART_PA = 2,
    SPEAKER_2IN1_SMART_PA_ANALOG_EARPIECE = 3
};

class AudioALSAHardwareResourceManager;

struct UltrasoundDeviceConfig {
    unsigned int msPerPeriod; // ms
    unsigned int rateIn; // hz
    unsigned int rateOut; // hz
    unsigned int channelsIn; // num
    unsigned int channelsOut; // num
    ultrasound_out_channel usndOutChannel; // only effective when channelsOut greater than 1
    unsigned int ramp_down_delay; //ms

    UltrasoundDeviceConfig() {
        msPerPeriod = 10;
        rateIn = 48000;
        rateOut = 48000;
        channelsIn = 2;
        channelsOut = 2;
        usndOutChannel = ULTRASOUND_OUT_CHANNEL_LEFT;
        ramp_down_delay = 25;
    }

    UltrasoundDeviceConfig(unsigned int period, unsigned int rate_in, unsigned int rate_out,
                           unsigned int channels_in, unsigned int channels_out,
                           ultrasound_out_channel out_channel, unsigned int rampdown_delay) {
        msPerPeriod = period;
        rateIn = rate_in;
        rateOut = rate_out;
        channelsIn = channels_in;
        channelsOut = channels_out;
        usndOutChannel = out_channel;
        ramp_down_delay = rampdown_delay;
    }

    void Dump();
};

struct ultra_audio_param
{
    int speaker_2in1;
    int usnd_rampdown_delay;
    int delay_start_duration;
    int mute_duration;
};

struct ultra_param_config {
    unsigned int rate_in;
    unsigned int rate_out;
    unsigned int channel_in;
    unsigned int channel_out;
    unsigned int format_in;
    unsigned int format_out;
    unsigned int period_in_size;
    unsigned int period_out_size;
    unsigned int target_out_channel;
};

struct ultra_gain_config {
    int mic_gain;
    int receiver_gain;
};

typedef enum {
	USND_STATE_IDLE,
	USND_STATE_ENABLED,
	USND_STATE_STARTED,
	USND_STATE_STOPPED,
} usnd_state_t;

#define CONFIG_USND_THREAD(thread_name, android_priority) \
    do { \
        snprintf(thread_name, sizeof(thread_name), "%s_%d_%d", __FUNCTION__, getpid(), gettid()); \
        prctl(PR_SET_NAME, (unsigned long)thread_name, 0, 0, 0); \
        int retval = setpriority(PRIO_PROCESS, 0, android_priority); \
        if (retval != 0) { \
            ALOGE("thread %s created. setpriority %s failed!! errno: %d, retval: %d", \
                  thread_name, #android_priority, errno, retval); \
        } else { \
            ALOGD("thread %s created. setpriority %s done", \
                     thread_name, #android_priority); \
        } \
    } while(0)


class AudioALSAUltrasoundOutOfCallController
{
    public:
        virtual ~AudioALSAUltrasoundOutOfCallController();
        static AudioALSAUltrasoundOutOfCallController *getInstance();
        virtual status_t open(const audio_devices_t output_devices,
                              UltrasoundDeviceConfig& ultrasound_pcm_cfg);
        virtual status_t close();
        status_t start(bool nolock = false);
        bool isEnabled();
        bool isStarted();
        status_t stop(bool nolock = false, bool rampDown = true, bool suspend = true);
        void SendMixerControl(const char *mixer_ctrl_name, int value);
        void setUsecaseName(const char *name);
        void updateAnalogGain(bool nolock = false);
        status_t openScpUltraPcmDriverWithFlag(const unsigned int device,unsigned int flag);
        bool calUsndOnOffState();
        bool calUsndOnOffStateWithoutRoutingCheck();
        bool isHeadPhoneConnected();
        void beforeInputDeviceRouting(const audio_devices_t input_device);
        void afterInputDeviceRouting(const audio_devices_t input_device);
        void beforeOutputDeviceRouting(const audio_devices_t current_output_devices,
                                       const audio_devices_t output_device);
        void afterOutputDeviceRouting(const audio_devices_t current_output_devices,
                                      const audio_devices_t output_device);
        usnd_state_t getUltrasoundState();
        void updateUltrasoundState(bool nolock = false);
        void updateDeviceConnectionState(audio_devices_t device, bool connect);
        bool isInCall();
        void ouputDeviceChanged(const audio_devices_t device, const DeviceStatus status);
        bool isCodecOutputDevices(audio_devices_t devices);
        bool isCodecInputDevices(audio_devices_t devices);
        void beforePhoneCallRouting(const audio_devices_t output_devices,
                                    const audio_devices_t input_device);
        void afterPhoneCallRouting(const audio_devices_t output_devices,
                                   const audio_devices_t input_device);
        void setEngineMode();
        status_t loadUltrasoundParam();
        void updateXmlParam(const char *_audioTypeName);
        void delayStartUsndAsyncRequest(bool start);
        void muteUsndAsyncRequest(bool start);
        bool isUsndSupported();

    protected:
        AudioALSAUltrasoundOutOfCallController();

        AudioALSAHardwareResourceManager *mHardwareResourceManager;
        AudioLock mUltraSndLock;
        struct pcm_config mConfig;
        struct pcm_config mOutConfig;
        struct pcm *mPcmDL;
        struct pcm *mPcmUL;
        struct pcm *mPcmUltra;
        struct mixer *mMixer;
        struct UltrasoundDeviceConfig mUltrasoundDeviceConfig;
        struct ultra_param_config mParamConfigToSCP;
        struct ultra_gain_config mGainConfigToSCP;
        audio_devices_t mDefaultOutputDevice;
        volatile usnd_state_t mUsndState;
        bool mInputDeviceRouting;
        bool mDeviceOpened;
        audio_devices_t mAvailableOutputDevices;
        audio_devices_t mAvailableInputDevices;
        bool mPhoneCallRouting;
        bool mOutputDeviceRouting;
        bool mWaitingForTargetOutputOn;
        audio_devices_t mWaitingOutputDevice;
        bool mEngineSuspended;
        bool mUsndSupported;

        // Usecase mixer control
        char mUsecaseName[MAX_USECASE_NAME];
        char mUsecaseSpkrName[MAX_USECASE_NAME];

        static void *muteDlForRoutingThread(void *arg);
        AudioLock mMuteDlForRoutingLock;
        pthread_t mMuteDlForRoutingThread;
        bool mMuteDlForRoutingThreadEnable;
        int mMuteDlForRoutingState;
        int mMuteDlForRoutingCtrl;

        static void *delayStartUsndThread(void *arg);
        AudioLock mDelayStartUsndLock;
        pthread_t mDelayStartUsndThread;
        bool mDelayStartUsndThreadEnable;
        int mDelayStartUsndState;
        int mDelayStartUsndCtrl;

        audio_devices_t mTargetOutputDevice;
        audio_devices_t mOpenedOutputDevice;
        audio_devices_t mTargetInputDevice;
        audio_devices_t mOpenedInputDevice;

        String8 mTurnOnSeqCustDev1;
        String8 mTurnOnSeqCustDev2;

        int mLogFlag;
        struct ultra_audio_param mParam;

    private:
        /**
         * singleton pattern
         */
        static AudioALSAUltrasoundOutOfCallController *mInstance;

};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_ULTRASOUND_OOC_CONTROLLER_H
