#ifndef ANDROID_AUDIO_ALSA_STREAM_MANAGER_H
#define ANDROID_AUDIO_ALSA_STREAM_MANAGER_H

#include "AudioSystemLibUtil.h"
#include "AudioTypeExt.h"

#include <hardware_legacy/AudioMTKHardwareInterface.h>


#include "AudioType.h"
#include <AudioLock.h>
#include "AudioMTKFilter.h"
#include "AudioPolicyParameters.h"
#include "AudioSpeechEnhanceInfo.h"
#include "AudioVolumeInterface.h"
#include <AudioCustParamClient.h>

/* Added for Ultrasound */
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
#include "UltrasoundOutOfCallManager.h"
#endif

#define KERNEL_BUFFER_FRAME_COUNT_REMAIN 1024

#define ULTRASOUND_RATE    (48000)

namespace android {

class AudioALSAStreamOut;
class AudioALSAStreamIn;

class AudioALSAPlaybackHandlerBase;
class AudioALSACaptureHandlerBase;

class AudioALSASpeechPhoneCallController;
class AudioSmartPaController;

class AudioALSAFMController;

class AudioALSAVolumeController;

class AudioALSAVoiceWakeUpController;

class SpeechDriverFactory;

class AudioALSAStreamManager {
public:
    virtual ~AudioALSAStreamManager();
    static AudioALSAStreamManager *getInstance();


    /**
     * open/close ALSA output stream
     */
    AudioMTKStreamOutInterface *openOutputStream(
        uint32_t devices,
        int *format,
        uint32_t *channels,
        uint32_t *sampleRate,
        status_t *status,
        uint32_t output_flag = 0);

    void closeOutputStream(AudioMTKStreamOutInterface *out);


    /**
     * open/close ALSA input stream
     */
    AudioMTKStreamInInterface *openInputStream(
        uint32_t devices,
        int *format,
        uint32_t *channels,
        uint32_t *sampleRate,
        status_t *status,
        audio_in_acoustics_t acoustics,
        uint32_t input_flag = 0);

    void closeInputStream(AudioMTKStreamInInterface *in);


    /**
     * create/destroy ALSA playback/capture handler
     */
    AudioALSAPlaybackHandlerBase *createPlaybackHandler(stream_attribute_t *stream_attribute_source);
    AudioALSACaptureHandlerBase  *createCaptureHandler(stream_attribute_t *stream_attribute_target);

    status_t destroyPlaybackHandler(AudioALSAPlaybackHandlerBase *pPlaybackHandler);
    status_t destroyCaptureHandler(AudioALSACaptureHandlerBase   *pCaptureHandler);


    /**
     * volume related functions
     */
    status_t setVoiceVolume(float volume);
    float getMasterVolume(void);
    status_t setMasterVolume(float volume, uint32_t iohandle = 0);
    status_t setHeadsetVolumeMax(void);
    status_t setFmVolume(float volume);

    status_t setMicMute(bool state);
    bool     getMicMute();
    uint32_t GetOffloadGain(float vol_f);
    void setAllInputStreamReopen(bool reopen);

#ifdef MTK_AUDIO_GAIN_TABLE
    status_t setAnalogVolume(int stream, int device, int index, bool force_incall);
    int SetCaptureGain(void);
#if defined(NON_SCENE_3GVT_VOWIFI_SUPPORT)
    void Set3GVTModeOn(bool enable);
    bool Get3GVTModeOn();
    void SetVxWifiOn(bool enable);
    bool GetVxWifiOn();
    void SetVxWifiWBMode(bool enable);
    bool GetVxWifiWBMode();
#endif
#endif

    /**
     * mode / routing related functions
     */
    status_t setMode(audio_mode_t new_mode);
    void updateAudioModePolicy(const audio_mode_t new_mode);

    status_t routingOutputDevicePhoneCall(const audio_devices_t output_devices);
    status_t routingOutputDevice(AudioALSAStreamOut *pAudioALSAStreamOut, const audio_devices_t current_output_devices, audio_devices_t output_devices);
    status_t routingInputDevice(AudioALSAStreamIn *pAudioALSAStreamIn, const audio_devices_t current_input_device, audio_devices_t input_device);
    audio_devices_t CheckInputDevicePriority(audio_devices_t input_device);
    uint32_t setUsedDevice(const audio_devices_t used_device);
    bool CheckStreaminPhonecallRouting(audio_devices_t new_phonecall_device, bool checkrouting = false);
    audio_mode_t getMode();
    status_t syncSharedOutDevice(audio_devices_t routingSharedDevice, AudioALSAStreamOut *currentStreamOut);
    bool isOutputNeedRouting(AudioALSAStreamOut *eachStreamOut, AudioALSAStreamOut *currentStreamOut,
                             audio_devices_t routingSharedOutDevice);

    // check if headset has changed
    bool CheckHeadsetChange(const audio_devices_t current_input_device, audio_devices_t input_device);


    status_t DeviceNoneUpdate(void);

    /**
     * FM radio related opeation // TODO(Harvey): move to FM Controller later
     */
    status_t setFmEnable(const bool enable, bool bForceControl = false, bool bForce2DirectConn = false, audio_devices_t output_device = AUDIO_DEVICE_NONE);
    bool     getFmEnable();

    // TODO(Harvey): move to Loopback Controller later
    status_t setLoopbackEnable(const bool enable);

    status_t setHdmiEnable(const bool enable);

    /**
     * suspend/resume all input/output stream
     */
    status_t setAllOutputStreamsSuspend(const bool suspend_on, const bool setModeRequest = false);
    status_t setAllInputStreamsSuspend(const bool suspend_on, const bool setModeRequest = false, const capture_handler_t caphandler = CAPTURE_HANDLER_ALL);
    status_t setAllStreamsSuspend(const bool suspend_on, const bool setModeRequest = false);


    /**
     * standby all input/output stream
     */
    status_t standbyAllOutputStreams(const bool setModeRequest = false);
    status_t standbyAllInputStreams(const bool setModeRequest = false, const capture_handler_t caphandler = CAPTURE_HANDLER_ALL);
    status_t standbyAllStreams(const bool setModeRequest = false);


    /**
     * audio mode status
     */
    bool isPhoneCallOpen();
    inline bool isModeInPhoneCall() { return isModeInPhoneCall(mAudioMode); }
    inline bool isModeInVoipCall()  { return isModeInVoipCall(mAudioMode); }
    inline bool isModeInRingtone()  { return isModeInRingtone(mAudioMode); }
    inline audio_mode_t getAudioMode() { return mAudioMode; }
    inline audio_mode_t getModeForGain() { return isPhoneCallOpen() ? AUDIO_MODE_IN_CALL : mAudioMode; }

    // TODO(Harvey): test code, remove it later
    inline uint32_t getStreamOutVectorSize()  { return mStreamOutVector.size(); }
    uint32_t getActiveStreamOutSize();
    inline AudioALSAStreamOut *getStreamOut(const size_t i)  { return mStreamOutVector[i]; }
    inline AudioALSAStreamIn *getStreamIn(const size_t i)  { return mStreamInVector[i]; }

#if defined(MTK_HIFIAUDIO_SUPPORT)
    /**
     * Hi-Fi Mode
     */
    int setAllStreamHiFi(AudioALSAStreamOut *pAudioALSAStreamOut, uint32_t sampleRate);
#endif

    void dynamicSetAudioDump(AUDIO_DUMP_TYPE dump_type);

    /**
     * stream in related
     */
    virtual size_t getInputBufferSize(uint32_t sampleRate, audio_format_t format, uint32_t channelCount);
    status_t updateOutputDeviceForAllStreamIn(audio_devices_t outputDevices);
    status_t updateOutputDeviceForAllStreamIn_l(audio_devices_t outputDevices);

    status_t SetMusicPlusStatus(bool bEnable);
    bool GetMusicPlusStatus();
    status_t UpdateACFHCF(int value);
    status_t SetACFPreviewParameter(void *ptr, int len);
    status_t SetHCFPreviewParameter(void *ptr, int len);
    status_t SetBesLoudnessStatus(bool bEnable);
    bool GetBesLoudnessStatus();
    status_t SetBesLoudnessControlCallback(const BESLOUDNESS_CONTROL_CALLBACK_STRUCT *callback_data);

    status_t SetEMParameter(AUDIO_CUSTOM_PARAM_STRUCT *pSphParamNB);
    status_t updateSpeechNVRAMParam(const int speech_band);
    status_t UpdateDualMicParams();
    status_t UpdateMagiConParams();
    status_t UpdateHACParams();
    status_t UpdateSpeechMode();
    status_t UpdateSpeechVolume();
    status_t SetVCEEnable(bool bEnable);
    status_t UpdateSpeechLpbkParams();

    status_t Enable_DualMicSettng(sph_enh_dynamic_mask_t sphMask, bool bEnable);
    status_t Set_LSPK_DlMNR_Enable(sph_enh_dynamic_mask_t sphMask, bool bEnable);
    status_t setSpkOutputGain(int32_t gain, uint32_t ramp_sample_cnt);
    status_t setSpkFilterParam(uint32_t fc, uint32_t bw, int32_t th);
    status_t setVtNeedOn(const bool vt_on);
    bool EnableBesRecord(void);
    bool getPhoncallOutputDevice();

    /**
     * Magic Conference Call
     */
    status_t SetMagiConCallEnable(bool bEnable);
    bool GetMagiConCallEnable(void);

    /**
     * HAC
     */
    status_t SetHACEnable(bool bEnable); // HAC: for general device phonecall with Hearing Aid Compatible
    bool GetHACEnable(void);
    status_t setSoftwareBridgeEnable(bool bEnable); // HAP: for Hearing Aid device phonecall
    bool getSoftwareBridgeEnable() { return mSwBridgeEnable; } // HAP

    /**
     * volume index
     */
    int setVolumeIndex(int stream, int device, int index);
    int getVolumeIndex() { return mVolumeIndex; }
    int getVoiceVolumeIndex() { return mVoiceVolumeIndex; };
    void volumeChangedCallback();

    /**
     * stream type
     */
    void setStreamType(int stream);

    /**
     * reopen Phone Call audio path according to RIL mapped modem notify
     */
    int phoneCallRefreshModem(const char *rilMappedMDName);

    /**
     * reopen Phone Call audio path
     */
    int phoneCallReopen();

    /**
     * update Phone Call phone id
     */
    int phoneCallUpdatePhoneId(const phone_id_t phoneId);

    /**
     * BT headset name
     */
    status_t SetBtHeadsetName(const char *btHeadsetName);
    const char *GetBtHeadsetName();

    /**
     * BT NREC
     */
    status_t SetBtHeadsetNrec(bool bEnable);
    bool GetBtHeadsetNrecStatus(void);

    /**
     * BT Codec
     */
    status_t SetBtCodec(int btCodec);
    int GetBtCodec(void);

    /**
     * voice wake up
     */
    status_t setVoiceWakeUpEnable_l(const bool enable);  // Locked with mStreamVectorLock
    status_t setVoiceWakeUpEnable(const bool enable);
    status_t setVoiceWakeUpNeedOn(const bool enable);
    bool     getVoiceWakeUpNeedOn();

    /**
     * VoIP dynamic function
     */
    void UpdateDynamicFunctionMask(void);


    /**
     * low latency
     */
    status_t setScreenState(bool mode);

    /**
     * Bypass DL Post Process Flag
     */
    status_t setBypassDLProcess(bool flag);


    /**
     * [TMP] stream out routing related // TODO(Harvey)
     */
    virtual status_t setParametersToStreamOut(const String8 &keyValuePairs);
    virtual status_t setParameters(const String8 &keyValuePairs, int IOport);

    /**
     * Enable/Disable speech Strm
     */
    status_t DisableSphStrm(const audio_mode_t new_mode);
    status_t EnableSphStrm(const audio_mode_t new_mode);
    status_t DisableSphStrmByDevice(audio_devices_t output_devices);
    status_t EnableSphStrmByDevice(audio_devices_t output_devices);
    bool isModeInPhoneCallSupportEchoRef(const audio_mode_t audio_mode);
    bool IsSphStrmSupport(void);
    void updateDeviceConnectionState(audio_devices_t device, bool connect);
    bool getDeviceConnectionState(audio_devices_t device);

    /**
     * Check Normal Record for tuning tool
     */
    bool     isNormalRecordUsing();
    /**
     * Check for VOIP and FM not concurrent
     */
    bool     isEchoRefUsing();
    status_t updateInputSource(int IOport);
    /**
     * Scene APIs
     */
    void setCustScene(const String8 scene);
    String8 getCustScene() { return mCustScene; }

    /*
     * VoIP independent path
     */
    bool needEnableVoip(const stream_attribute_t *streamAttribute);

    /**
     * TDM DPTX
     */
    String8 getTVOutCapability(const char *capabilityType);

    /**
     * Call Fwd
     */

    void forceTelephonyTX(bool enable);
    /**
     * Call Memo
     */
    void callMemo(int state);

    void logStreamDumpSize();

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    void setForceDisableVoiceWakeUpForUsnd(bool disable);
#endif

    /**
     * HDR Record
     */
    void setHDRRecord(bool enable);
    bool getHDRRecord(void) { return mHDRRecordOn; }

protected:
    AudioALSAStreamManager();

    inline bool isModeInPhoneCall(const audio_mode_t audio_mode) {
        return (audio_mode == AUDIO_MODE_IN_CALL || audio_mode == AUDIO_MODE_CALL_SCREEN);
    }

    inline bool isModeInVoipCall(const audio_mode_t audio_mode) {
        return (audio_mode == AUDIO_MODE_IN_COMMUNICATION);
    }

    inline bool isModeInRingtone(const audio_mode_t audio_mode) {
        return (audio_mode == AUDIO_MODE_RINGTONE);
    }

    inline bool isModeInNormal(const audio_mode_t audio_mode) {
        return (audio_mode == AUDIO_MODE_NORMAL);
    }

    void SetInputMute(bool bEnable);
    bool enableHifi(uint32_t type, uint32_t typeValue);

private:
    /**
     * singleton pattern
     */
    static AudioALSAStreamManager *mStreamManager;


    /**
     * stream manager lock
     */
    AudioLock mStreamVectorLock; // used in setMode & open/close input/output stream
    AudioLock mPlaybackHandlerVectorLock;
    AudioLock mCaptureHandlerVectorLock;
    AudioLock mLock;
    AudioLock mAudioModeLock;
    AudioLock mVowLock;

    /**
     * stream in/out vector
     */
    KeyedVector<uint32_t, AudioALSAStreamOut *> mStreamOutVector;
    KeyedVector<uint32_t, AudioALSAStreamIn *>  mStreamInVector;
    uint32_t mStreamOutIndex;
    uint32_t mStreamInIndex;


    /**
     * stream playback/capture handler vector
     */
    KeyedVector<uint32_t, AudioALSAPlaybackHandlerBase *> mPlaybackHandlerVector;
    KeyedVector<uint32_t, AudioALSACaptureHandlerBase *>  mCaptureHandlerVector;
    uint32_t mPlaybackHandlerIndex;
    uint32_t mCaptureHandlerIndex;


    /**
     * speech phone call controller
     */
    AudioALSASpeechPhoneCallController *mSpeechPhoneCallController;

    /**
     * SmartPA controller
     */

    AudioSmartPaController *mSmartPaController;

    /**
     * FM radio
     */
    AudioALSAFMController *mFMController;


    /**
     * volume controller
     */
    AudioVolumeInterface *mAudioALSAVolumeController;
    SpeechDriverFactory *mSpeechDriverFactory;


    /**
     * volume related variables
     */
    bool mMicMute;


    /**
     * audio mode
     */
    audio_mode_t mAudioMode;
    audio_mode_t mAudioModePolicy;
    bool mEnterPhoneCallMode;
    bool mPhoneCallControllerStatusPolicy;
    bool mResumeAllStreamsAtRouting;
    bool mIsNeedResumeStreamOut;

    /**
     * Loopback related
     */
    bool mLoopbackEnable; // TODO(Harvey): move to Loopback Controller later

    bool mHdmiEnable; // TODO(Harvey): move to Loopback Controller later
    /**
     * stream in/out vector
     */
    KeyedVector<uint32_t, AudioMTKFilterManager *> mFilterManagerVector;

    bool mBesLoudnessStatus;

    void (*mBesLoudnessControlCallback)(void *data);

    /**
     * Speech EnhanceInfo Instance
     */
    AudioSpeechEnhanceInfo *mAudioSpeechEnhanceInfoInstance;

    /**
     * headphone change flag
     */
    bool mHeadsetChange;

    /**
     * voice wake up
     */
    AudioALSAVoiceWakeUpController *mAudioALSAVoiceWakeUpController;
    bool mVoiceWakeUpNeedOn;
    bool mForceDisableVoiceWakeUpForPhoneCall;

    /**
    * Bypass DL Post Process Flag
    */
    bool mBypassPostProcessDL;

    /**
    * Bypass UL DMNR Pre Process Flag
    */
    bool mBypassDualMICProcessUL;

    /**
     * BT device info
     */
    const char *mBtHeadsetName;

    /**
     * BT device info
     */
    int mBtCodec;

//#if defined(NON_SCENE_3GVT_VOWIFI_SUPPORT)
    /**
     * VoWifi info
     */
    bool mIsVoWifi;
    bool mIsVoWifiWbAmr;

    /**
     * 3GVT info
     */
    bool mIs3GVT;
//#endif

    /**
     * AudioCustParamClient
     */
    AudioCustParamClient *mAudioCustParamClient;

    /*
     * Device Available flag
     */
    uint32_t mAvailableOutputDevices;

    /*
     * flag of dynamic enable verbose/debug log
     */
    int mLogEnable;

    /**
     * Customized scenario information
     */
    String8 mCustScene;

    /**
     * Prevent setMode during destructing
     */
    bool mStreamManagerDestructing;

    /**
     * volume index
     */
    int mVolumeIndex;
    int mVoiceVolumeIndex;
    int mVoiceStream;
    int mVoiceDevice;

    /**
     * stream type
     */
    audio_stream_type_t mStreamType;

    /**
     * ADSP
     */
    static void audioDspStopWrap(void *arg);
    static void audioDspReadyWrap(void *arg);
    void audioDspStatusUpdate(const bool adspReady);

    AudioALSAStreamOut *mOutputStreamForCall;
    audio_devices_t mCurrentOutputDevicesForCall;
    audio_devices_t mOutputDevicesForCall;

    /*
     * In-Earphone Monitors
     */
    bool checkHalIEMsInOn(const stream_attribute_t *attr);

    /*
     * Output available flags
     */
    uint32_t mAvailableOutputFlags;

    /*
     * Record offload query
     */
    bool isCaptureOffload(stream_attribute_t *stream_attribute_target);

    /*
     * Hearing Aid Device Phonecall (HAP)
     */
    bool mSwBridgeEnable;

    /*
     * Update and Inquire BesLoudness status on XML
     */
    status_t setBesLoudnessStateToXML(bool value);
    int getBesLoudnessStateFromXML();

    /**
     * Call Fwd
     */
    bool mIsMicMuteBeforeCallFwd;
    bool mIsDlMuteBeforeCallFwd;
    bool mIsCallFwdEnabled;

    /**
     * Call Memo
     */
    bool mIsMicMuteBeforeCallMemo;
    bool mIsDlMuteBeforeCallMemo;
    int mCallMemoState;

    /**
     * HDR Record
     */
    bool mHDRRecordOn;

#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
    UltrasoundOutOfCallManager *mUltrasoundOutOfCallManager;
    bool mForceDisableVoiceWakeUpForUsnd;
#endif
};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_STREAM_MANAGER_H
