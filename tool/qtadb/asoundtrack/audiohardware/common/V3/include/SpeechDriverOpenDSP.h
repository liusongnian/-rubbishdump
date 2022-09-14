#ifndef ANDROID_SPEECH_DRIVER_OPEN_DSP_H
#define ANDROID_SPEECH_DRIVER_OPEN_DSP_H

#include "SpeechDriverInterface.h"
#include <string.h>

#include <AudioLock.h>

struct ipi_msg_t;

struct aurisys_dsp_config_t;
struct aurisys_lib_manager_t;
struct arsi_task_config_t;

#define SCENEKEYWORDLLENGTH (64)

namespace android {

class AudioMessengerIPI;

class SpeechDriverOpenDSP : public SpeechDriverInterface {
public:
    virtual         ~SpeechDriverOpenDSP();


    /**
     * get instance's pointer
     */
    static SpeechDriverOpenDSP *GetInstance(modem_index_t modem_index);


    /**
     * speech control
     */
    virtual status_t SetSpeechMode(const audio_devices_t input_device, const audio_devices_t output_devices);
    virtual status_t SpeechOn();
    virtual status_t SpeechOff();
    virtual status_t VideoTelephonyOn();
    virtual status_t VideoTelephonyOff();
    virtual status_t SpeechRouterOn();
    virtual status_t SpeechRouterOff();

    virtual status_t setMDVolumeIndex(int stream, int device, int index);

    /**
     * record control
     */
    virtual status_t recordOn();
    virtual status_t recordOff();
    virtual status_t recordOn(SpcRecordTypeStruct typeRecord);
    virtual status_t recordOff(SpcRecordTypeStruct typeRecord);
    virtual status_t setPcmRecordType(SpcRecordTypeStruct typeRecord);

    virtual status_t VoiceMemoRecordOn();
    virtual status_t VoiceMemoRecordOff();

    virtual uint16_t GetRecordSampleRate() const;
    virtual uint16_t GetRecordChannelNumber() const;


    /**
     * background sound control
     */
    virtual status_t BGSoundOn();
    virtual status_t BGSoundConfig(uint8_t ul_gain, uint8_t dl_gain);
    virtual status_t BGSoundOff();


    /**
     * voip rx on/off
     */
    virtual status_t VoipRxOn();
    virtual status_t VoipRxOff();
    virtual status_t VoipRxConfig(const uint8_t direction, const uint8_t mixType);


    /**
     * telephony tx
     */
    virtual status_t TelephonyTxOn(uint16_t pcmTelephonyTxType);
    virtual status_t TelephonyTxConfig(uint16_t pcmTelephonyTxType);
    virtual status_t TelephonyTxOff();


    /**
     * pcm 2 way
     */
    virtual status_t PCM2WayOn(const bool wideband_on);
    virtual status_t PCM2WayOff();


    /**
     * tty ctm control
     */
    virtual status_t TtyCtmOn();
    virtual status_t TtyCtmOff();
    virtual status_t TtyCtmDebugOn(bool tty_debug_flag);
    virtual int setTtyMode(const TtyModeType ttyMode);

    /**
     * rtt control
     */
    virtual int RttConfig(int rttMode);

    /**
     * acoustic loopback
     */
    virtual status_t SetAcousticLoopback(bool loopback_on);
    virtual status_t SetAcousticLoopbackBtCodec(bool enable_codec);

    virtual status_t SetAcousticLoopbackDelayFrames(int32_t delay_frames);
    virtual status_t setLpbkFlag(bool enableLpbk);

    /**
     * Modem Audio DVT and Debug
     */
    virtual status_t SetModemLoopbackPoint(uint16_t loopback_point);

    /**
     * encryption
     */
    virtual status_t SetEncryption(bool encryption_on);

    /**
     * volume control
     */
    virtual status_t SetDownlinkGain(int16_t gain);
    virtual status_t SetEnh1DownlinkGain(int16_t gain);
    virtual status_t SetUplinkGain(int16_t gain);
    virtual status_t SetDownlinkMute(bool mute_on);
    virtual status_t SetDynamicDownlinkMute(bool mute_on);
    virtual status_t SetUplinkMute(bool mute_on);
    virtual status_t SetUplinkSourceMute(bool mute_on);
    virtual status_t SetSidetoneGain(int16_t gain);
    virtual status_t SetDownlinkMuteCodec(bool mute_on);
    virtual void     setMuteInfo(uint32_t muteInfo);


    /**
     * device related config
     */
    virtual status_t SetModemSideSamplingRate(uint16_t sample_rate);
    virtual status_t switchBTMode(uint32_t sample_rate);
    virtual void     setBTMode(const int mode);
    virtual void     setBtSpkDevice(const bool flag);



    /**
     * speech enhancement control
     */
    virtual status_t SetSpeechEnhancement(bool enhance_on);
    virtual status_t SetSpeechEnhancementMask(const sph_enh_mask_struct_t &mask);

    virtual status_t SetBtHeadsetNrecOn(const bool bt_headset_nrec_on);
    virtual void setBtHeadsetName(const char *bt_headset_name);


    /**
     * speech enhancement parameters setting
     */
    virtual int updateSpeechFeature(const SpeechFeatureType featureType, const bool flagOn);
    virtual status_t GetVibSpkParam(void *eVibSpkParam);
    virtual status_t SetVibSpkParam(void *eVibSpkParam);

    virtual status_t SetDynamicSpeechParameters(const int type, const void *param_arg);
    virtual int setParameter(const char *keyParameter);
    virtual const char *getParameter(const char *keyParameter);
    virtual int updateCustScene(const char *custScene);
    virtual int updateCustInfo(const char *custInfo);

    /**
     * check whether modem is ready.
     */
    virtual bool     CheckModemIsReady();

    /**
     * ipc control
     */
    virtual int      sendIpcCmd(int cmd);
    void UpdateAurisysConfig(const bool is_enh_need_reset);

    virtual bool     isAdspPhoneCallEnhOn();
    virtual bool     isAdspPhoneCallCoreSeparate();
    virtual void     setAdspPhoneCallEnh(int value);


protected:
    SpeechDriverOpenDSP() {}
    SpeechDriverOpenDSP(modem_index_t modem_index);

    /**
     * scp phone call on/off
     */
    virtual status_t ScpSpeechOn();
    virtual status_t ScpSpeechOff();
    virtual status_t ScpMaskMDIrq();

    /**
     * recover status (speech/record/bgs/vt/p2w/tty)
     */
    virtual bool     GetScpSideSpeechStatus(const modem_status_mask_t modem_status_mask);
    virtual void     SetScpSideSpeechStatus(const modem_status_mask_t modem_status_mask);
    virtual void     ResetScpSideSpeechStatus(const modem_status_mask_t modem_status_mask);
    virtual void     RecoverModemSideStatusToInitState();


    AudioMessengerIPI *mAudioMessengerIPI;
    SpeechDriverInterface *pSpeechDriverInternal; // adaptor



    uint32_t         mScpSideSpeechStatus; // value |= modem_status_mask_t
    AudioLock        mScpSideSpeechStatusLock;

    uint32_t         mModemPcmSampleRate; // AFE pcm config
    uint32_t         mModemDspMaxSampleRate; // FD216 working sample rate



    /**
     * lib related
     */
    void             setDspDumpWakelock(bool condition);
    void             createDumpFiles();
    static void      processDmaMsg(struct ipi_msg_t *msg, void *buf, uint32_t size, void *arg);
    bool             mEnableDump;
#ifdef MTK_AUDIODSP_SUPPORT
    bool             mEnableEPLDump;
#endif
    bool             mSuperVolumeEnable;
    char             mScenekeyword[SCENEKEYWORDLLENGTH];
    char             mBtcodec[SCENEKEYWORDLLENGTH];
    bool             mIsLoopBack; // on or off

    unsigned long long mAdspPhoneCallConfig;

    /**
     * aurisys
     */
    struct aurisys_dsp_config_t *mAurisysDspConfig;
    audio_devices_t mInputDevice;
    audio_devices_t mOutputDevices;

    struct aurisys_lib_manager_t *mAurisysLibManager;

    void SetArsiTaskConfig(struct arsi_task_config_t *pTaskConfig);
    void SetArsiAttribute();
    void SetArsiCustInfo();
    void CreateAurisysLibManager();
    void DestroyAurisysLibManager() ;
#ifdef MTK_AUDIODSP_SUPPORT
    void EnableEPLDUmp();
#endif


private:
    /**
     * singleton pattern
     */
    static SpeechDriverOpenDSP *mSpeechDriverOpenDSP;
    static void callbackSpeechParamChange(int audioEventType, void *caller, void *arg);
    AudioLock mAurisysDspConfigLock;
};

} // end namespace android

#endif // end of ANDROID_SPEECH_DRIVER_OPEN_DSP_H
