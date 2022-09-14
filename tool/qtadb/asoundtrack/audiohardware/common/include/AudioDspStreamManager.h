#ifndef ANDROID_AUDIO_DSP_STREAM_MANAGER_H
#define ANDROID_AUDIO_DSP_STREAM_MANAGER_H

#include "AudioSystemLibCUtil.h"

#include <hardware_legacy/AudioMTKHardwareInterface.h>

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
#include <vendor/mediatek/hardware/bluetooth/audio/2.1/IBluetoothAudioProvider.h>
using vendor::mediatek::hardware::bluetooth::audio::V2_1::IBluetoothAudioPort;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::AudioConfiguration;
#endif

#include <tinyalsa/asoundlib.h>
#include "AudioType.h"
#include "AudioLock.h"
#include "AudioPolicyParameters.h"
#include "AudioDspType.h"


#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
struct aurisys_lib_manager_t;
struct aurisys_dsp_config_t;
struct aurisys_gain_config_t;
struct aurisys_lib_manager_config_t;
#endif

enum {
    DSP_VER_HARDWARE_MIX,
    DSP_VER_SW_MIX,
};

enum AudioBluetoothStreamState {
    BTAUDIO_DISABLED = 0, // This stream is closing or set param "suspend=true"
    BTAUDIO_STANDBY,
    BTAUDIO_STARTING,
    BTAUDIO_STARTED,
    BTAUDIO_SUSPENDING,   // wait for suspend complete.
    BTAUDIO_UNKNOWN,
};

enum AudioBTIFState{
    BTIF_OFF = 0,
    BTIF_ON = 1,
    BTIF_DISABLE = 2, // call when set a2dp suspend
};

enum {
    ADSP_DUMP_TASK_DISABLE = 0,
    ADSP_DUMP_TASK_DEFAULT_ENABLE = 0x1 << 0,
    ADSP_DUMP_TASK_PRIMARY_ENABLE = 0x1 << 1,
    ADSP_DUMP_TASK_DEEPBUFFER_ENABLE = 0x1 << 2,
    ADSP_DUMP_TASK_FAST_ENABLE = 0x1 << 3,
    ADSP_DUMP_TASK_MUSIC_ENABLE = 0x1 << 4,
    ADSP_DUMP_TASK_PLAYBACK_ENABLE = 0x1 << 5,
    ADSP_DUMP_TASK_VOIP_DL_ENABLE = 0x1 << 6,
    ADSP_DUMP_TASK_A2DP_ENABLE = 0x1 << 7,
    ADSP_DUMP_TASK_CAPTURE_UL1_ENABLE = 0x1 << 8,
    ADSP_DUMP_TASK_CAPTURE_RAW_ENABLE = 0x1 << 9,
};

typedef struct {
    uint32_t codec_type; // codec types ex: SBC/AAC/LDAC/APTx
    uint32_t sample_rate;    // sample rate, ex: 44.1/48.88.2/96 KHz
    uint32_t encoded_audio_bitrate;  // encoder audio bitrates
    uint16_t max_latency;    // maximum latency
    uint16_t scms_t_enable;  // content protection enable
    uint16_t acl_hdl;    // connection handle
    uint16_t l2c_rcid;   // l2cap channel id
    uint16_t mtu;        // mtu size
    unsigned char bits_per_sample; // bits per sample, ex: 16/24/32
    unsigned char ch_mode;         // None:0, Left:1, Right:2
    unsigned char codec_info[32];  //codec specific information
} A2DP_CODEC_INFO_T;

namespace android {

class AudioALSAPlaybackHandlerBase;
class AudioALSACaptureDataProviderBase;
class AudioMessengerIPI;
class AudioALSAStreamOut;

class AudioDspStreamManager {
public:
    virtual ~AudioDspStreamManager();
    static AudioDspStreamManager *getInstance();

    int addPlaybackHandler(AudioALSAPlaybackHandlerBase *playbackHandler);
    int removePlaybackHandler(AudioALSAPlaybackHandlerBase *playbackHandler);

    int addCaptureDataProvider(AudioALSACaptureDataProviderBase *dataProvider);
    int removeCaptureDataProvider(AudioALSACaptureDataProviderBase *dataProvider);

    int dumpPlaybackHandler(void);
    int dumpCaptureDataProvider(void);

    unsigned int getUlLatency(void);
    unsigned int getDlLatency(void);
    unsigned int getDspSample(void);
    unsigned int getA2dpPcmLatency(void);
    bool getDspTaskPlaybackStatus(void);
    bool getDspTaskA2DPStatus(void);
    int setAfeInDspShareMem(bool condition, bool need_ref);
    int setAfeOutDspShareMem(unsigned int flag, bool condition);

    /* get audio dsp support stream */
    unsigned int getDspOutHandlerEnable(unsigned int flag, unsigned int device);
    int getDspInHandlerEnable(unsigned int flag);
    int getDspRawInHandlerEnable(unsigned int flag);
    int getDspPlaybackEnable(void);
    int getDspA2DPEnable(void);
    int getDspFmEnable(void);
    void updateMode(audio_mode_t audioMode);
    int doRecoveryState();
    int getDspVersion(void);
    int setDspVersion(int version);
    int openCaptureDspHwPcm(AudioALSACaptureDataProviderBase *dataProvider);
    int closeCaptureDspHwPcm(AudioALSACaptureDataProviderBase *dataProvider);
    bool HasLowLatencyCaptureDsp(void);
    int UpdateCaptureDspLatency(void);

    int setParameters(String8 param);
    struct pcm_config *getCaptureDspConfig(void) { return &mCaptureDspConfig;}

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    void CreateAurisysLibManager(
        struct aurisys_lib_manager_t **manager,
        struct aurisys_dsp_config_t **config,
        const uint8_t task_scene,
        const uint32_t aurisys_scenario,
        const uint8_t arsi_process_type,
        const uint32_t audio_mode,
        const struct stream_attribute_t *attribute_in,
        const struct stream_attribute_t *attribute_out,
        const struct stream_attribute_t *attribute_ref,
        const struct aurisys_gain_config_t *gain_config);
    void DestroyAurisysLibManager(
        struct aurisys_lib_manager_t **manager,
        struct aurisys_dsp_config_t **config,
        const uint8_t task_scene);
    void UpdateAurisysConfig(
        struct aurisys_lib_manager_t *pAurisysLibManager,
        struct aurisys_dsp_config_t *pAurisysDspConfig,
        const uint32_t audio_mode,
        const struct stream_attribute_t *attribute_in,
        const struct stream_attribute_t *attribute_out);
    void SetArsiTaskConfig(
        struct aurisys_lib_manager_config_t *pManagerConfig,
        const uint8_t task_scene,
        const uint32_t aurisys_scenario,
        const uint32_t audio_mode,
        const struct stream_attribute_t *attribute_in,
        const struct stream_attribute_t *attribute_out);
#endif

    int triggerDsp(unsigned int task_scene, int data_type);
    int getDspRuntimeEn(uint8_t task_scene);
    int setDspRuntimeEn(uint8_t task_scene, bool condition);
    int setDspRefRuntimeEn(uint8_t task_scene, bool condition);
    int setAfeDspShareMem(bool condition);

    bool isAdspPlayback(const int mAudioOutputFlags,
                        const audio_devices_t outputDevices);

    /* set offload task state */
    void setOffloadPaused(bool status, audio_output_flags_t flag, float gain);
    bool getOffloadPaused(void);

    /*
     * start/stop device call when device change
     * resourcemanager call with stat/stop device.
     */
    int startDevice(audio_devices_t devices);
    int stopDevice(audio_devices_t devices);

    /*
     * need to do lock with mTaskLock,
     * when playbackhandlerdsp addplaybackhandler,
     * also trigger dspstreammanager start/stop task when start/stop output device
     * resourcemanager may not call start output device due to output device is already open with
     * other playbackhandler.
     */
    int startTasks_l();
    int stopTasks_l(bool force);

    /* upadate source_meta*/
    void updateSourceMetadata(const struct source_metadata* source_metadata, audio_output_flags_t flag);
    unsigned long long getA2dpRemoteDelayus();

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    void setBluetoothAudioOffloadParam(const sp<IBluetoothAudioPort>& hostIf,
                                       const AudioConfiguration& codecConfig,
                                       bool on);
    /* set a2dp suspend/resume */
    void setA2dpSuspend(bool on);
    /* set a2dp status from bt */
    void setA2dpStatus(int status);
    /* get a2dp status fomr status set by bt*/
    int getA2dpStatus(void);

    sp<IBluetoothAudioPort> getBluetoothAudioHostIf();
    int getBluetoothAudioSession();
    void *getBluetoothAudioCodecInfo(void);
    uint32_t getBluetoothAudioCodecType(void);

    int WaitHostIfState(void);
    int setBtHostIfState(int state);
    int btOutWriteAction(audio_devices_t devices);

    int setA2dpStreamStatusToDsp(int status);
    int setA2dpReconfig(bool on);
#endif

private:
    /**
     * singleton pattern
     */
    AudioDspStreamManager();
    static AudioDspStreamManager *mDspStreamManager;
    AudioMessengerIPI *mAudioMessengerIPI;

    /*
     * condition check if need open playback task,
     */
    int needOpenPlaybackTask(void);
    int needStartPlaybackTask(audio_devices_t outputdevice);
    int needStopPlaybackTask(audio_devices_t outputdevice, bool force);

    /*
     * start and stop playback task
     * open source and then open target with swmixer
     * close source and then close target with swmixer
     * need to open ==> start ==> stop ==> close playback task
     */
    int openPlaybackTask(AudioALSAPlaybackHandlerBase *playbackHandler);
    int closePlaybackTask(AudioALSAPlaybackHandlerBase *playbackHandler);
    int startPlaybackTask();
    int stopPlaybackTask();

    void dumpSourceMetadata(const struct source_metadata *source_metadata);


#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    /*
     * condition check if need open a2dp task,
     */
    int needOpenA2dpTask(void);
    int needStartA2dpTask(audio_devices_t outputdevice);
    int needStopA2dpTask(audio_devices_t outputdevice, bool force);

    /*
     * start and stop a2dp task
     * open source and then open target with swmixer
     * close source and then close target with swmixer
     * need to open ==> start ==> stop ==> close playback task
     */
    int openA2dpTask(AudioALSAPlaybackHandlerBase *playbackHandler);
    int closeA2dpTask(AudioALSAPlaybackHandlerBase *playbackHandler);
    int startA2dpTask();
    int startA2dpTask_l();
    int stopA2dpTask_l();
    int stopA2dpTask();
#endif

    bool dataPasstoDspAfe(const int mAudioOutputFlags,
                        const audio_devices_t outputDevices);
    bool dataPassToCodec(const int mAudioOutputFlags,
                        const audio_devices_t outputDevices);

    int setA2dpDspShareMem(bool condition);

    bool dataPasstoA2DPTask(AudioALSAPlaybackHandlerBase *Base);

    int checkMusicTaskStatus(void);
    bool dataPasstoMusicTask(AudioALSAPlaybackHandlerBase *Base);
    int startMusicTask(AudioALSAPlaybackHandlerBase *Base);
    int stopMusicTask(AudioALSAPlaybackHandlerBase *Base);

    void openPCMDumpA2DP(AudioALSAPlaybackHandlerBase *playbackHandler);
    void closePCMDumpA2DP(AudioALSAPlaybackHandlerBase *playbackHandler);
    int parseParameter(char* strbuf, int *taskScene, int* param);

    /* a2dp related */
    int mDspA2DPStreamState;
    int mDspA2DPIndex;
    struct pcm_config mDspA2dpConfig;
    struct pcm *mDspA2dpPcm;
    bool mDspTaskA2DPActive;

    int mDspMusicStreamState;
    /**
     * stream manager lock
     */
    AudioLock mLock;
    AudioLock mDeviceLock;
    AudioLock mTaskLock;
    AudioLock mOffloadPausedLock;
    AudioLock mCaptureDspLock;
    AudioLock mCaptureDspVectorLock;

    /**
     * stream playback/capture handler vector
     */
    KeyedVector<unsigned long long, AudioALSAPlaybackHandlerBase *> mPlaybackHandlerVector;
    KeyedVector<unsigned long long, AudioALSACaptureDataProviderBase *>  mCaptureDataProviderVector;
    KeyedVector<unsigned long long, AudioALSAPlaybackHandlerBase *> mMusicHandlerVector;
    KeyedVector<unsigned long, struct source_metadata*> mMetadataVector;
    /*
     * pcm for capture pcm and playback pcmDumpThreadCreated
     */
    struct pcm_config mPlaybackUlConfig;
    struct pcm *mPlaybackUlPcm;
    int mPlaybackUlindex;
    struct pcm_config mPlaybackDlConfig;
    struct pcm *mPlaybackDlPcm;
    int mPlaybackDlindex;
    struct pcm_config mPlaybackIVConfig;
    struct pcm *mPlaybackIVPcm;
    int mPlaybackIVindex;
    struct pcm_config mDspConfig;
    struct pcm *mDspPcm;
    struct pcm_config mCaptureDspConfig;
    struct pcm *mCaptureDspPcm;
    int mDspIndex;
    bool mDspTaskPlaybackActive;
    unsigned int multiplier;
    bool mLowLatencyDspCapture;
    bool mA2dpOffloadDisabled;

    /* dsp stream state */
    int mDspStreamState;
    int mStreamCardIndex;
    struct stream_attribute_t mDspStreamAttribute;

    /* log flag */
    uint32_t mlog_flag;

    /* device status*/
    int mOutputDeviceCount;
    audio_devices_t mOutputDevice;

    /* dsp ap interconnection to device */
    String8 mApTurnOnSequence;
    String8 mApTurnOnSequence2;
    String8 mApTurnOnSequenceIV;
    String8 mTurnOnSeqCustDev1;
    String8 mTurnOnSeqCustDev2;

    /* Mixer*/
    struct mixer *mMixer;
    AudioLock mMetadataLock;

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    sp<IBluetoothAudioPort> mBluetoothAudioOffloadHostIf;
    int mBluetoothAudioOffloadSession;
    int mA2dpStatus; /* this is status report from bt */
    bool mA2dpSuspend;
    int mA2dpStatusAck; /* ack flag from bt */
    AudioLock mA2dpSessionLock;
    AudioLock mA2dpStatusLock;
    AudioLock mA2dpStreamLock;
    bool mOffloadPaused;
    A2DP_CODEC_INFO_T a2dp_codecinfo;

    /* merge all sourcemetadata to this struct*/
    struct source_metadata metadata_all;
    unsigned int metadata_all_tracks_count;
    /*
     * 0: dump in open close, 1: dump in start/stop,
     * start/stop may cause in short time, may occur performance problem.
     * defualt using 0.
     */
    int mA2dpPcmDump;
#endif

    // a2dp pcm dump
    FILE *mPCMDumpFileDSP;
    uint32_t mDumpFileNumDSP;

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    struct aurisys_lib_manager_t *mAurisysLibManagerPlayback;
    struct aurisys_dsp_config_t *mAurisysDspConfigPlayback;
    struct aurisys_lib_manager_t *mAurisysLibManagerMusic;
    struct aurisys_dsp_config_t *mAurisysDspConfigMusic;
#endif
};

} // end namespace android

#endif // end of ANDROID_AUDIO_DSP_STREAM_MANAGER_H
