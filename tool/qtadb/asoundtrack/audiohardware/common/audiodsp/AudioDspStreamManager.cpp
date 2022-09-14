#include "AudioDspStreamManager.h"
#include <tinyalsa/asoundlib.h>


#include "AudioALSAStreamOut.h"
#include "AudioALSAStreamIn.h"

#include "AudioALSAPlaybackHandlerBase.h"
#include "AudioALSACaptureDataProviderBase.h"
#include "AudioALSAHardwareResourceManager.h"

#include "AudioALSADeviceParser.h"
#include "AudioALSADriverUtility.h"
#include "AudioALSADeviceConfigManager.h"

#include "AudioParamParser.h"
#include "AudioSmartPaController.h"
#include <AudioMessengerIPI.h>
#include "audio_task.h"
#include "AudioUtility.h"
#include "audio_a2dp_msg_id.h"
#include <audio_dsp_service.h>
#include "AudioDspCallFinal.h"

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
#include <android/hardware/audio/common/7.0/types.h>
#endif


#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <arsi_type.h>
#include <aurisys_scenario_dsp.h>
#include <aurisys_config.h>
#include <aurisys_utility.h>
#include <aurisys_controller.h>
#include <aurisys_lib_manager.h>
#include <SpeechEnhancementController.h>
#endif

#include "AudioALSAHandlerKtv.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioDspStreamManager"

//#define DEBUG_VERBOSE
static AudioLock mAurisysLibManagerLock;

#define AUDPLAYBACK_DL_SAMPLES (1024 * 2)
#define AUDPLAYBACK_DL_TASK_PEROID_COUNT (4)

#define AUDPLAYBACK_UL_SAMPLES (1024 * 2)
#define AUDPLAYBACK_UL_TASK_PEROID_COUNT (4)
#define AUDIO_MULTIPLIER (96000)

#define A2DPPLAYBACK_DL_SAMPLES (2 * 512)
#define A2DPPLAYBACK_DL_TASK_PEROID_COUNT (2)

#define BTIF_ACK_TIMEOUT (2500)
#define SOURCEMETADATA_NUM (64)

static const unsigned int kMinimumDelayMs = 50;
static const unsigned int kMaximumDelayMs = 1000;
#define MAX_PARATERS_LENGTH (256)
#define DSP_PARAM_DEBUG_SET_STRING ("DEBUGFLAG")
#define DSP_PARAM_PULSE_SET_STRING ("PULSEFLAG")
#define DSP_PARAM_A2DP_SET_STRING ("A2DPSTREAMFLAG")

#define DSP_PARAM_PAYLOAD ("PAYLOAD")
namespace android {

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
using vendor::mediatek::hardware::bluetooth::audio::V2_1::IBluetoothAudioPort;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::AudioConfiguration;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::CodecConfiguration;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::SbcParameters;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::AacParameters;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::Status;
using vendor::mediatek::hardware::bluetooth::audio::V2_1::TimeSpec;

using ::android::hardware::audio::common::V5_0::AudioContentType;
using ::android::hardware::audio::common::V5_0::AudioUsage;
using ::android::hardware::audio::common::V5_0::PlaybackTrackMetadata;
using ::android::hardware::audio::common::V5_0::SourceMetadata;

#endif

enum {
    DSP_STREAM_NOTSUPPORT = -3,
    DSP_STREAM_VALID = -2,
    DSP_STREAM_NOSTREAM = -1,
    DSP_STREAM_CLOSE = 0,
    DSP_STREAM_OPEN,
    DSP_STREAM_START,
    DSP_STREAM_STOP,
    DSP_STREAM_NOCHANGE,
};

enum {
    A2DP_STREAM_DUMP_OPEN = 0,
    A2DP_STREAM_DUMP_START = 1,
};



/*==============================================================================
 *                     Singleton Pattern
 *============================================================================*/

AudioDspStreamManager *AudioDspStreamManager::mDspStreamManager = NULL;
AudioDspStreamManager *AudioDspStreamManager::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mDspStreamManager == NULL) {
        mDspStreamManager = new AudioDspStreamManager();
    }
    ASSERT(mDspStreamManager != NULL);
    return mDspStreamManager;
}

/*==============================================================================
 *                     Constructor / Destructor / Init / Deinit
 *============================================================================*/

AudioDspStreamManager::AudioDspStreamManager():
    mAudioMessengerIPI(AudioMessengerIPI::getInstance()),
    mDspA2DPStreamState(0),
    mDspA2DPIndex(0),
    mDspA2dpPcm(NULL),
    mDspTaskA2DPActive(0),
    mDspMusicStreamState(0),
    mPlaybackUlPcm(NULL),
    mPlaybackUlindex(0),
    mPlaybackDlPcm(NULL),
    mPlaybackDlindex(0),
    mPlaybackIVPcm(NULL),
    mPlaybackIVindex(0),
    mDspPcm(NULL),
    mCaptureDspPcm(NULL),
    mDspIndex(0),
    mDspTaskPlaybackActive(false),
    multiplier(0),
    mLowLatencyDspCapture(false),
    mDspStreamState(0),
    mStreamCardIndex(0),
    mOutputDeviceCount(0),
    mOutputDevice(AUDIO_DEVICE_NONE),
    mMixer(AudioALSADriverUtility::getInstance()->getMixer()),
    mPCMDumpFileDSP(NULL),
    mDumpFileNumDSP(0) {
    ALOGD("%s()", __FUNCTION__);

    mPlaybackHandlerVector.clear();
    mCaptureDataProviderVector.clear();

    memset((void *)&mPlaybackUlConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mPlaybackIVConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mPlaybackDlConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mDspConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mDspA2dpConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mCaptureDspConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mDspStreamAttribute, 0, sizeof(struct stream_attribute_t));

    mlog_flag = AudioALSADriverUtility::getInstance()->GetPropertyValue(streamout_log_propty);
    mA2dpOffloadDisabled = property_get_bool("persist.bluetooth.a2dp_offload.disabled", false);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    mAurisysLibManagerPlayback = NULL;
    mAurisysDspConfigPlayback = NULL;
    mAurisysLibManagerMusic = NULL;
    mAurisysDspConfigMusic = NULL;
#endif

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    mBluetoothAudioOffloadHostIf = NULL;
    mBluetoothAudioOffloadSession = 0;
    mA2dpStatus = BTAUDIO_DISABLED;
    mA2dpStatusAck = 0;
    mA2dpSuspend = false;
    mOffloadPaused = false;
    memset(&a2dp_codecinfo, 0, sizeof (A2DP_CODEC_INFO_T));
    metadata_all_tracks_count = SOURCEMETADATA_NUM;
    metadata_all.tracks = new playback_track_metadata[SOURCEMETADATA_NUM];
    mA2dpPcmDump = A2DP_STREAM_DUMP_OPEN;
    metadata_all.track_count = 0;
#endif

    doRecoveryState();
}


AudioDspStreamManager::~AudioDspStreamManager() {
    ALOGD("%s()", __FUNCTION__);

    /* clean vector*/
    mPlaybackHandlerVector.clear();
    mCaptureDataProviderVector.clear();

    /* clean pcm */
    if (mPlaybackUlPcm != NULL) {
        pcm_close(mPlaybackUlPcm);
        mPlaybackUlPcm = NULL;
    }

    if (mPlaybackDlPcm != NULL) {
        pcm_close(mPlaybackDlPcm);
        mPlaybackDlPcm = NULL;
    }

    if (mPlaybackIVPcm != NULL) {
        pcm_close(mPlaybackIVPcm);
        mPlaybackIVPcm = NULL;
    }

    if (mDspPcm != NULL) {
        pcm_close(mDspPcm);
        mDspPcm = NULL;
    }
#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    if (metadata_all.tracks) {
        delete[] metadata_all.tracks;
        metadata_all.tracks = NULL;
    }
#endif
}

int AudioDspStreamManager::setAfeDspShareMem(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_playback_sharemem_scenario"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    return 0;
}


int AudioDspStreamManager::setAfeInDspShareMem(bool condition, bool need_ref) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_capture_sharemem_scenario"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    if (need_ref == true) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_ref_sharemem_scenario"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}


int AudioDspStreamManager::setAfeOutDspShareMem(unsigned int flag, bool condition) {
    if (getDspVersion() == DSP_VER_SW_MIX) {
        return 0;
    }

    if (isIsolatedDeepBuffer((const audio_output_flags_t)flag)) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_deepbuffer_sharemem_scenario"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    } else if (flag & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_voip_sharemem_scenario"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    } else if (isDspLowLatencyFlag((audio_output_flags_t)flag)) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_fast_sharemem_scenario"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    } else {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_primary_sharemem_scenario"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

int AudioDspStreamManager::setA2dpDspShareMem(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_a2dp_sharemem_scenario"), 0, condition)) {
        ALOGW("%s(), enable fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

int AudioDspStreamManager::getDspRuntimeEn(uint8_t task_scene) {
#define MAX_TASKNAME_LEN (128)
    int ret = 0;
    struct mixer_ctl *ctl = NULL;
    char runtime_en[MAX_TASKNAME_LEN] ={0};

    switch (task_scene) {
        case TASK_SCENE_PRIMARY:
            strncpy(runtime_en, "dsp_primary_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_DEEPBUFFER:
            strncpy(runtime_en, "dsp_deepbuf_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_VOIP:
            strncpy(runtime_en, "dsp_voipdl_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_PLAYBACK_MP3:
            strncpy(runtime_en, "dsp_offload_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_AUDPLAYBACK:
            strncpy(runtime_en, "dsp_playback_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_CAPTURE_UL1:
            strncpy(runtime_en, "dsp_captureul1_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_A2DP:
            strncpy(runtime_en, "dsp_a2dp_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_DATAPROVIDER:
            strncpy(runtime_en, "dsp_dataprovider_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_MUSIC:
            strncpy(runtime_en, "dsp_music_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_CALL_FINAL:
            strncpy(runtime_en, "dsp_call_final_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_FAST:
            strncpy(runtime_en, "dsp_fast_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_KTV:
            strncpy(runtime_en, "dsp_ktv_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_FM_ADSP:
            strncpy(runtime_en, "dsp_fm_runtime_en", MAX_TASKNAME_LEN);
            break;
        default:
            ALOGE("-%s task_scene = %d", __FUNCTION__, task_scene);
            break;
    }

    ctl = mixer_get_ctl_by_name(mMixer, runtime_en);
    if (ctl == NULL) {
        ALOGE("-%s Mixer of %s = NULL!!", __FUNCTION__, runtime_en);
        return 0;
    }
    ret = mixer_ctl_get_value(ctl, 0);
    return ret;
}

int AudioDspStreamManager::setDspRuntimeEn(uint8_t task_scene, bool condition) {
#define MAX_TASKNAME_LEN (128)
    struct mixer_ctl *ctl = NULL;
    char runtime_en[MAX_TASKNAME_LEN] ={0};

    ALOGD("%s(), task_scene = %d, condition = %d", __FUNCTION__, task_scene, condition);
    switch (task_scene) {
        case TASK_SCENE_PRIMARY:
            strncpy(runtime_en, "dsp_primary_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_DEEPBUFFER:
            strncpy(runtime_en, "dsp_deepbuf_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_VOIP:
            strncpy(runtime_en, "dsp_voipdl_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_PLAYBACK_MP3:
            strncpy(runtime_en, "dsp_offload_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_AUDPLAYBACK:
            strncpy(runtime_en, "dsp_playback_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_CAPTURE_UL1:
            strncpy(runtime_en, "dsp_captureul1_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_A2DP:
            strncpy(runtime_en, "dsp_a2dp_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_DATAPROVIDER:
            strncpy(runtime_en, "dsp_dataprovider_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_MUSIC:
            strncpy(runtime_en, "dsp_music_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_CALL_FINAL:
            strncpy(runtime_en, "dsp_call_final_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_FAST:
            strncpy(runtime_en, "dsp_fast_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_KTV:
            strncpy(runtime_en, "dsp_ktv_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_FM_ADSP:
            strncpy(runtime_en, "dsp_fm_runtime_en", MAX_TASKNAME_LEN);
            break;
        default:
            ALOGE("-%s task_scene = %d", __FUNCTION__, task_scene);
            break;
    }

    ctl = mixer_get_ctl_by_name(mMixer, runtime_en);
    if (ctl == NULL) {
        ALOGE("-%s Mixer of %s = NULL!!", __FUNCTION__, runtime_en);
        return -1;
    }

    if (mixer_ctl_set_value(ctl, 0, condition)) {
        ALOGW("%s(), mixer_ctl_set_value %s fail", __FUNCTION__, runtime_en);
        return -1;
    }
    return 0;
}

int AudioDspStreamManager::setDspRefRuntimeEn(uint8_t task_scene, bool condition) {
#define MAX_TASKNAME_LEN (128)
    struct mixer_ctl *ctl = NULL;
    char runtime_en[MAX_TASKNAME_LEN] ={0};

    ALOGD("%s(), task_scene = %d, condition = %d", __FUNCTION__, task_scene, condition);
    switch (task_scene) {
        case TASK_SCENE_AUDPLAYBACK:
            strncpy(runtime_en, "dsp_playback_ref_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_CAPTURE_UL1:
            strncpy(runtime_en, "dsp_captureul1_ref_runtime_en", MAX_TASKNAME_LEN);
            break;
        case TASK_SCENE_CALL_FINAL:
            strncpy(runtime_en, "dsp_call_final_ref_runtime_en", MAX_TASKNAME_LEN);
            break;
        default:
            ALOGE("-%s task_scene = %d, not support ref runtime enable!", __FUNCTION__, task_scene);
            break;
    }

    ctl = mixer_get_ctl_by_name(mMixer, runtime_en);
    if (ctl == NULL) {
        ALOGE("-%s Mixer of %s = NULL!!", __FUNCTION__, runtime_en);
        return -1;
    }

    if (mixer_ctl_set_value(ctl, 0, condition)) {
        ALOGW("%s(), mixer_ctl_set_value %s fail", __FUNCTION__, runtime_en);
        return -1;
    }
    return 0;
}

bool AudioDspStreamManager::dataPassToCodec(const int mAudioOutputFlags,
                        const audio_devices_t outputDevices) {
    unsigned int devicecount = popcount(outputDevices);
    bool ret;

    /* a2dp device only , return false */
    if ((outputDevices & AUDIO_DEVICE_OUT_ALL_A2DP) &&
        (popcount(outputDevices) == 1)) {
        ret = false;
    } else {
        ret = true;
    }
    ALOGD("%s output_devices = 0x%x mAudioOutputFlags = %d devicecount = %u ret = %d",
          __FUNCTION__, outputDevices, mAudioOutputFlags, devicecount, ret);
    return ret;
}

/* which data need to pass to dsp stream manager*/
bool AudioDspStreamManager::dataPasstoDspAfe(const int mAudioOutputFlags,
                                             const audio_devices_t outputDevices) {
    bool isUseAdspPlaybackTask = false;
    bool ret;
    if (getDspVersion() == DSP_VER_HARDWARE_MIX){
        //For hw mix platform, we only enable ADSP playback task for spk protection case only in default.
        isUseAdspPlaybackTask = AudioSmartPaController::getInstance()->isSwDspSpkProtect(outputDevices);
    } else {
        //For sw mix platform, we need to enable ADSP playback task in 2 case: 1 spk protection, 2 by output handler.
        isUseAdspPlaybackTask = AudioSmartPaController::getInstance()->isSwDspSpkProtect(outputDevices) ||
                                getDspOutHandlerEnable(mAudioOutputFlags, outputDevices);
    }
    ret = isUseAdspPlaybackTask && dataPassToCodec(mAudioOutputFlags, outputDevices);

    ALOGD("%s output_devices = 0x%x Flags = %d isUseAdspPlaybackTask = %d ret = %d",
          __FUNCTION__, outputDevices, mAudioOutputFlags, isUseAdspPlaybackTask, ret);
    return ret;
}

/* check if data pass to afe codec.*/
int AudioDspStreamManager::needOpenPlaybackTask() {
    size_t i = 0;
    int ret = 0;
    bool dspstartflag = false;
    audio_devices_t outputdevice = AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();

    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
        if (!outputdevice){
            outputdevice = attribute->output_devices;
        }
        dspstartflag |= dataPasstoDspAfe(attribute->mAudioOutputFlags, attribute->output_devices);
        if (dspstartflag == true) {
            break;
        }
    }

    /* data need to pass to afe codec */
    if (dspstartflag == true) {
        switch (mDspStreamState){
        case DSP_STREAM_CLOSE:
            ret = DSP_STREAM_OPEN;
            break;
        case DSP_STREAM_OPEN:
        case DSP_STREAM_START:
        case DSP_STREAM_STOP:
            ret = DSP_STREAM_NOCHANGE;
            break;
        default:
            break;
        }
    } else if (dspstartflag == false) {
        switch (mDspStreamState){
        case DSP_STREAM_CLOSE:
        case DSP_STREAM_OPEN:
        case DSP_STREAM_START:
            ret = DSP_STREAM_NOCHANGE;
            break;
        case DSP_STREAM_STOP:
            ret = DSP_STREAM_CLOSE;
            break;
        default:
            break;
        }
    }

    ALOGD("%s ret = %d mDspStreamState = %d dspstartflag = %d", __FUNCTION__, ret, mDspStreamState, dspstartflag);
    return ret;
}

int AudioDspStreamManager::needStopPlaybackTask(audio_devices_t outputdevice, bool force) {
    size_t i = 0;
    bool dspstartflag = false;
    int ret = 0;

    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
        if (!outputdevice){
            outputdevice = attribute->output_devices;
        }
        dspstartflag |= dataPasstoDspAfe(attribute->mAudioOutputFlags, attribute->output_devices);
        if (dspstartflag == true) {
            break;
        }
    }

    if (dspstartflag == false && mDspTaskPlaybackActive == true) {
        ret = true;
    } else if (mDspTaskPlaybackActive == true && force == true) {
        ret = true;
    } else {
        ret = false;
    }
    ALOGD("%s ret = %d mDspTaskPlaybackActive = %d mDspTaskPlaybackActive[%d] dspstartflag[%d]",
           __FUNCTION__, ret, mDspTaskPlaybackActive, mDspTaskPlaybackActive, dspstartflag);
    return ret;
}

int AudioDspStreamManager::needStartPlaybackTask(audio_devices_t outputdevice) {
    size_t i = 0;
    bool dspstartflag = false;

    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
        if (!outputdevice){
            outputdevice = attribute->output_devices;
        }
        dspstartflag |= dataPasstoDspAfe(attribute->mAudioOutputFlags, attribute->output_devices);
        if (dspstartflag == true) {
            break;
        }
    }

    ALOGD("%s mDspTaskPlaybackActive = %d dspstartflag = %d", __FUNCTION__, mDspTaskPlaybackActive, dspstartflag);
    if (dspstartflag == true && mDspTaskPlaybackActive == false){
        return true;
    } else {
        return false;
    }
}

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
/* check if data pass to afe codec.*/
int AudioDspStreamManager::needOpenA2dpTask() {
    size_t i = 0;
    int ret = 0;
    bool a2dpstartflag = false;
    audio_devices_t outputdevice = AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();

    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
        if (!outputdevice){
            outputdevice = attribute->output_devices;
        }
        a2dpstartflag |= dataPasstoA2DPTask(Base);
        if (a2dpstartflag == true) {
            break;
        }
    }

    /* data need to pass to afe codec */
    if (a2dpstartflag == true) {
        switch (mDspA2DPStreamState){
        case DSP_STREAM_CLOSE:
            ret = DSP_STREAM_OPEN;
            break;
        case DSP_STREAM_OPEN:
        case DSP_STREAM_START:
        case DSP_STREAM_STOP:
            ret = DSP_STREAM_NOCHANGE;
            break;
        default:
            break;
        }
    } else if (a2dpstartflag == false) {
        switch (mDspA2DPStreamState){
        case DSP_STREAM_CLOSE:
            ret = DSP_STREAM_NOCHANGE;
            break;
        case DSP_STREAM_OPEN:
        case DSP_STREAM_START:
        case DSP_STREAM_STOP:
            ret = DSP_STREAM_CLOSE;
            break;
        default:
            break;
        }
    }

    ALOGD("%s ret = %d mDspA2DPStreamState = %d a2dpstartflag = %d", __FUNCTION__,
          ret, mDspA2DPStreamState, a2dpstartflag);
    return ret;
}

int AudioDspStreamManager::needStopA2dpTask(audio_devices_t outputdevice, bool force) {
    size_t i = 0;
    bool dspstartflag = false;
    int ret = 0;
    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
        if (!outputdevice){
            outputdevice = attribute->output_devices;
        }
        dspstartflag |= dataPasstoA2DPTask(Base);
        if (dspstartflag == true) {
            break;
        }
    }

    if (dspstartflag == false && mDspTaskA2DPActive == true) {
        ret = true;
    } else if (mDspTaskA2DPActive == true && force == true) {
        ret = true;
    } else {
        ret = false;
    }
    ALOGD("%s ret = %d mDspA2DPStreamState = %d mDspTaskA2DPActive[%d] dspstartflag[%d]",
           __FUNCTION__, ret, mDspA2DPStreamState, mDspTaskA2DPActive, dspstartflag);
    return ret;
}

int AudioDspStreamManager::needStartA2dpTask(audio_devices_t outputdevice) {
    size_t i = 0;
    bool dspstartflag = false;

    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
        if (!outputdevice){
            outputdevice = attribute->output_devices;
        }
        dspstartflag |= dataPasstoA2DPTask(Base);
        if (dspstartflag == true) {
            break;
        }
    }

    ALOGD("%s mDspA2DPStreamState = %d mDspTaskA2DPActive[%d] dspstartflag[%d]",
           __FUNCTION__, mDspA2DPStreamState, mDspTaskA2DPActive, dspstartflag);

    if (dspstartflag == true && mDspTaskA2DPActive == false && mBluetoothAudioOffloadSession > 0){
        return true;
    } else {
        return false;
    }
}

bool AudioDspStreamManager::dataPasstoA2DPTask(AudioALSAPlaybackHandlerBase *Base) {
    const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
    int playbackType = Base->getPlaybackHandlerType();
    audio_devices_t outputdevice = attribute->output_devices;
    if (!outputdevice){
        outputdevice = attribute->output_devices;
    }

    if ((outputdevice & AUDIO_DEVICE_OUT_ALL_A2DP) != 0) {
        ALOGD("%s return true outputdevice = 0x%x playbackType = %d",
              __FUNCTION__, outputdevice, playbackType);
        return true;
    }
    ALOGD("%s return false outputdevice = 0x%x playbckType = %d",
          __FUNCTION__, outputdevice, playbackType);
    return false;
}

int AudioDspStreamManager::startA2dpTask()
{
    AL_LOCK(mA2dpStreamLock);
    startA2dpTask_l();
    AL_UNLOCK(mA2dpStreamLock);
    return 0;
}

int AudioDspStreamManager::startA2dpTask_l()
{
    if (mA2dpPcmDump == A2DP_STREAM_DUMP_START) {
        for (int i = 0; i < mPlaybackHandlerVector.size(); i++) {
            AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
            if (Base){
                openPCMDumpA2DP(Base);
                break;
            }
        }
    }

    /* when start a2dp task , nned to tell codec info */
    struct ipi_msg_t ipi_msg;
    int retval = audio_send_ipi_msg(&ipi_msg,
                                TASK_SCENE_A2DP, AUDIO_IPI_LAYER_TO_DSP,
                                AUDIO_IPI_PAYLOAD, AUDIO_IPI_MSG_NEED_ACK,
                                AUDIO_DSP_TASK_A2DP_CODECINFO, sizeof(A2DP_CODEC_INFO_T), 0,
                                (char *)getBluetoothAudioCodecInfo());
    ALOGD("+%s(), ", __FUNCTION__);
    if (retval != NO_ERROR) {
        ALOGW("AUDIO_DSP_TASK_A2DP_CODECINFO fail");
    }
    if (mA2dpStatus == BTAUDIO_STANDBY) {
        setBtHostIfState(BTIF_ON);
    }

    if (mDspA2dpPcm != NULL) {
        if (pcm_start(mDspA2dpPcm) != 0) {
            ALOGE("%s(), pcm_start(%p) fail due to %s", __FUNCTION__, mDspA2dpPcm, pcm_get_error(mDspA2dpPcm));
            triggerDsp(TASK_SCENE_A2DP, AUDIO_DSP_TASK_START);
        }
    } else {
        ALOGE("%s() mDspA2dpPcm == NULL", __FUNCTION__);
        ASSERT(0);
    }

    if (mDspTaskA2DPActive == true) {
        ALOGE("%s(), mDspTaskA2DPActive(%d)", __FUNCTION__, mDspTaskA2DPActive);
        ASSERT(0);
        return -1;
    }

    mDspTaskA2DPActive = true;
    ALOGD("-%s(), ", __FUNCTION__);
    return 0;
}

int AudioDspStreamManager::stopA2dpTask()
{
    AL_LOCK(mA2dpStreamLock);
    stopA2dpTask_l();
    AL_UNLOCK(mA2dpStreamLock);
    return 0;
}


int AudioDspStreamManager::stopA2dpTask_l()
{
    int i = 0;
    if (mDspA2dpPcm != NULL) {
        if (pcm_stop(mDspA2dpPcm) != 0) {
            ALOGE("%s(), pcm_stop(%p) fail due to %s", __FUNCTION__, mDspA2dpPcm, pcm_get_error(mDspA2dpPcm));
            triggerDsp(TASK_SCENE_A2DP, AUDIO_DSP_TASK_STOP);
        }
    }

    if (mA2dpStatus != BTAUDIO_DISABLED) {
        setBtHostIfState(BTIF_OFF);
    }

    if (mDspTaskA2DPActive == false) {
        ALOGE("%s(), mDspTaskA2DPActive(%d)", __FUNCTION__, mDspTaskA2DPActive);
        ASSERT(0);
        return -1;
    }

    mDspTaskA2DPActive = false;
    mDspA2DPStreamState = DSP_STREAM_STOP;
    ALOGD("-%s(), ", __FUNCTION__);

    if (mA2dpPcmDump == A2DP_STREAM_DUMP_START) {
        for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
            AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
            if (Base){
                closePCMDumpA2DP(Base);
                break;
            }
        }
    }

    return 0;
}

#endif

void AudioDspStreamManager::dumpSourceMetadata(const struct source_metadata *source_metadata)
{
    ssize_t track_count;
    playback_track_metadata* tracks;

    if(source_metadata == NULL) {
        ALOGW("%s source_metadata = %p", __func__, source_metadata);
        return;
    }

    ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s track_count[%zu] source_metadata = %p", __func__, source_metadata->track_count, source_metadata);
    tracks = source_metadata->tracks;
    for (track_count = 0; track_count < source_metadata->track_count; track_count++) {
        ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s usage[%d] content_type[%d] gain[%f]\n", __func__, tracks->usage, tracks->content_type, tracks->gain);
        tracks++;
    }
    return;
}

bool AudioDspStreamManager::getOffloadPaused(void) {
    AL_AUTOLOCK(mOffloadPausedLock);

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    return mOffloadPaused;
#else
    return false;
#endif
}

void AudioDspStreamManager::setOffloadPaused(bool status, audio_output_flags_t flag, float gain) {
    AL_AUTOLOCK(mOffloadPausedLock);;

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    int index = 0, count =0;
    struct source_metadata* metadata = NULL;

    ALOGD("%s() status=%d mOffloadPaused=%d flag=%x",
          __FUNCTION__, status, mOffloadPaused, flag);

    /* status change */
    if (mOffloadPaused != status) {
        mOffloadPaused = status;
        AL_LOCK(mMetadataLock);
        int index = mMetadataVector.indexOfKey(flag);
        if (index >= 0) {
            metadata = mMetadataVector.valueAt(index);
            if (metadata && metadata->track_count) {
                ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s track_count[%zd] metadata[%p]", __func__, metadata->track_count, metadata);
                for (count = 0; count < metadata->track_count; count++) {
                    if (status) {
                        metadata->tracks[count].gain = 0.0;  //pause set to gain 0.0
                    } else {
                        metadata->tracks[count].gain = gain; //restore gain
                    }
                }
                dumpSourceMetadata(metadata);
            } else {
                ALOGW("%s metadata = NULL", __func__);
            }
        }
        AL_UNLOCK(mMetadataLock);
        if (metadata)
            updateSourceMetadata(metadata, flag);
    }
#else
    UNUSED(status);
    UNUSED(flag);
    UNUSED(gain);
#endif
}

/* upadate source_meta*/
void AudioDspStreamManager::updateSourceMetadata(const struct source_metadata* source_metadata, audio_output_flags_t flag)
{
    int index = 0;
    struct source_metadata* metadata = NULL;
    ssize_t track_totalcount = 0;

    /* if non-offload, return */
    if (mA2dpOffloadDisabled) {
        ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s mA2dpOffloadDisabled %d return", __func__, mA2dpOffloadDisabled);
        return;
    }

    if (!source_metadata) {
        ALOGD("%s source_metadata = %p", __func__, source_metadata);
        return;
    }
    AL_LOCK(mMetadataLock);

    index = mMetadataVector.indexOfKey(flag);
    if (index >= 0) {
        /* replace metadata with this streamout vector*/
        metadata = mMetadataVector.valueAt(index);
        ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s index[%d] flag[%u] metadata[%p]", __func__, index, flag, metadata);
        if (metadata) {
            ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s replace index[%d] flag[%u] track_count[%zd] metadata[%p] ",
                  __func__, index, flag, metadata->track_count, metadata);
            /* delete previous data */
            if (metadata->tracks) {
                delete[] metadata->tracks;
                metadata->tracks = NULL;
            }

            /* allocate for new metadata */
             metadata->tracks = new playback_track_metadata[source_metadata->track_count];
             metadata->track_count = source_metadata->track_count;
             memcpy(metadata->tracks, source_metadata->tracks, sizeof(struct playback_track_metadata) * source_metadata->track_count);
             dumpSourceMetadata(source_metadata);
        } else {
            ALOGW("%s flag[%u] index[%d] metadata = NULL", __func__, flag, index);
        }
    } else {
        /* add streamout metadata to vector */
        metadata = new (struct source_metadata);
        metadata->tracks = new playback_track_metadata[source_metadata->track_count];
        metadata->track_count = source_metadata->track_count;
        memcpy(metadata->tracks, source_metadata->tracks, sizeof(struct playback_track_metadata) * source_metadata->track_count);
        mMetadataVector.add(flag, metadata);
        ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s add index[%d] flag[%u] track_count[%zd] metadata[%p]",
              __func__, index, flag, source_metadata->track_count, metadata);
    }

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    AL_AUTOLOCK(mA2dpSessionLock);
    if (mBluetoothAudioOffloadHostIf == NULL) {
        AL_UNLOCK(mMetadataLock);
        return;
    }

    // get total count of track.
    for(index = 0; index < mMetadataVector.size(); index++) {
        struct source_metadata* temp = mMetadataVector.valueAt(index);
        if (temp) {
            track_totalcount += temp->track_count;
        }
    }

    if (track_totalcount == 0) {
        ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "%s track_totalcount = %zu ", __FUNCTION__, track_totalcount);
        AL_UNLOCK(mMetadataLock);
        return;
    }

    // double size.
    if (track_totalcount >= metadata_all_tracks_count) {
        if (metadata_all.tracks) {
            delete[] metadata_all.tracks;
            metadata_all.tracks = NULL;
        }
        metadata_all_tracks_count = metadata_all_tracks_count * 2;
        metadata_all.tracks = new playback_track_metadata[metadata_all_tracks_count];
    }

    // merge all metadata
    metadata_all.track_count = track_totalcount;
    struct playback_track_metadata* tracks_all = metadata_all.tracks;
    for(index = 0; index < mMetadataVector.size(); index++) {
        struct source_metadata* temp = mMetadataVector.valueAt(index);
        if (temp->track_count) {
            memcpy(tracks_all, temp->tracks, sizeof(playback_track_metadata) * temp->track_count);
            tracks_all += temp->track_count;
        }
    }

    if (metadata_all.track_count > 0) {
        dumpSourceMetadata(&metadata_all);
    }

    /* send merged metadata to bt*/
    SourceMetadata sourceMetadata;
    PlaybackTrackMetadata* halMetadata;

    sourceMetadata.tracks.resize(metadata_all.track_count);
    halMetadata = sourceMetadata.tracks.data();

    ssize_t track_count = metadata_all.track_count;
    playback_track_metadata* track = metadata_all.tracks;
    while (track_count && track) {
        halMetadata->usage = (AudioUsage)(track->usage);
        halMetadata->contentType = (AudioContentType)(track->content_type);
        halMetadata->gain = track->gain;
        --track_count;
        ++track;
        ++halMetadata;
    }

    AL_UNLOCK(mMetadataLock);

    mBluetoothAudioOffloadHostIf->updateMetadata(sourceMetadata);
#else
    AL_UNLOCK(mMetadataLock);
    ALOGV("%s source_metadata track count = %zu ", __FUNCTION__, source_metadata->track_count);
#endif
    return;
}


bool AudioDspStreamManager::dataPasstoMusicTask(AudioALSAPlaybackHandlerBase *Base) {
    const stream_attribute_t *attribute = Base->getStreamAttributeTarget();
    int playbackType = Base->getPlaybackHandlerType();

    if (playbackType == PLAYBACK_HANDLER_DEEP_BUFFER) {
        return true;
    }
    if (playbackType == PLAYBACK_HANDLER_NORMAL) {
        return true;
    }
    if (playbackType == PLAYBACK_HANDLER_OFFLOAD) {
        return true;
    }
    if (playbackType == PLAYBACK_HANDLER_FAST) {
        return false;
    }

    ALOGD("%s return false attribute->output_devices = 0x%x playbackType = %d",
          __FUNCTION__, attribute->output_devices, playbackType);
    return false;
}

int AudioDspStreamManager::checkMusicTaskStatus() {
    size_t i = 0;
    int ret = 0;
    bool dspstartflag = false;

    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        dspstartflag |= dataPasstoMusicTask(Base);
        if (dspstartflag == true) {
            break;
        }
    }

    if (dspstartflag == true && mDspMusicStreamState != DSP_STREAM_START) {
        ret = DSP_STREAM_START;
        ALOGD("%s ret = DSP_STREAM_START", __FUNCTION__);
    } else if (dspstartflag == false && mDspMusicStreamState != DSP_STREAM_STOP) {
        ret = DSP_STREAM_STOP;
        ALOGD("%s ret = DSP_STREAM_STOP", __FUNCTION__);
    } else {
        ret = DSP_STREAM_NOCHANGE;
        ALOGV("%s ret = DSP_STREAM_NOCHANGE, mDspMusicStreamState = %d dspstartflag = %d", __FUNCTION__, mDspMusicStreamState, dspstartflag);
    }

    return ret;
}

int AudioDspStreamManager::startPlaybackTask()
{
    ALOGD("+%s", __FUNCTION__);

    if (mPlaybackDlPcm != NULL) {
        if (pcm_start(mPlaybackDlPcm) != 0) {
            ALOGE("%s(), DL pcm_start(%p) fail due to %s", __FUNCTION__, mPlaybackDlPcm, pcm_get_error(mPlaybackDlPcm));
        }
    }

    if (mPlaybackIVPcm != NULL) {
        if (pcm_start(mPlaybackIVPcm) != 0) {
            ALOGE("%s(), IV pcm_start(%p) fail due to %s", __FUNCTION__, mPlaybackIVPcm, pcm_get_error(mPlaybackIVPcm));
        }
    }

    if (mPlaybackUlPcm != NULL) {
        if (pcm_start(mPlaybackUlPcm) != 0) {
            ALOGE("%s(), UL pcm_start(%p) fail due to %s", __FUNCTION__, mPlaybackIVPcm, pcm_get_error(mPlaybackIVPcm));
        }
    }
    if (mDspPcm != NULL) {
        if (pcm_start(mDspPcm) != 0) {
            ALOGE("%s(), dsp pcm_start(%p) fail due to %s", __FUNCTION__, mDspPcm, pcm_get_error(mDspPcm));
            triggerDsp(TASK_SCENE_AUDPLAYBACK, AUDIO_DSP_TASK_START);
        }
    }

    if (mDspTaskPlaybackActive == true) {
        ALOGE("%s(), mDspTaskPlaybackActive(%d)", __FUNCTION__, mDspTaskPlaybackActive);
        ASSERT(0);
        return -1;
    }

    mDspTaskPlaybackActive = true;
    return 0;
}

int AudioDspStreamManager::stopPlaybackTask()
{
    ALOGD("+%s", __FUNCTION__);

    int i = 0;
    /* stop all pcm */
    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        Base->stop();
    }

    if (mDspPcm != NULL) {
        if (pcm_stop(mDspPcm) != 0) {
            ALOGE("%s(), dsp pcm_stop(%p) fail due to %s", __FUNCTION__, mDspPcm, pcm_get_error(mDspPcm));
        }
        triggerDsp(TASK_SCENE_AUDPLAYBACK, AUDIO_DSP_TASK_STOP);
    }

    if (mPlaybackDlPcm != NULL) {
        if (pcm_stop(mPlaybackDlPcm) != 0) {
            ALOGE("%s(), DL pcm_stop(%p) fail due to %s", __FUNCTION__, mPlaybackDlPcm, pcm_get_error(mPlaybackDlPcm));
        }
    }
    if (mPlaybackIVPcm != NULL) {
        if (pcm_stop(mPlaybackIVPcm) != 0) {
            ALOGE("%s(), IV pcm_stop(%p) fail due to %s", __FUNCTION__, mPlaybackIVPcm, pcm_get_error(mPlaybackIVPcm));
        }
    }
    if (mPlaybackUlPcm != NULL) {
        if (pcm_stop(mPlaybackUlPcm) != 0) {
            ALOGE("%s(), UL pcm_stop(%p) fail due to %s", __FUNCTION__, mPlaybackIVPcm, pcm_get_error(mPlaybackIVPcm));
        }
    }

    if (mDspTaskPlaybackActive == false) {
        ALOGE("%s(), mDspTaskPlaybackActive(%d)", __FUNCTION__, mDspTaskPlaybackActive);
        ASSERT(0);
        return -1;
    }

    mDspTaskPlaybackActive = false;
    return 0;
}


int AudioDspStreamManager::openPlaybackTask(AudioALSAPlaybackHandlerBase *playbackHandler) {
    if (mPlaybackHandlerVector.size() <= 0) {
        return DSP_STREAM_NOSTREAM;
    }

    if (getDspPlaybackEnable() == false) {
        return DSP_STREAM_NOTSUPPORT;
    }

    /* get first handlerbase*/
    AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(0);
    memcpy(&mDspStreamAttribute, Base->getStreamAttributeTarget(), sizeof(stream_attribute_t));
    mDspStreamAttribute.mAudioOutputFlags = AUDIO_OUTPUT_FLAG_PLAYBACK;
    mDspStreamAttribute.audio_mode = AudioALSAStreamManager::getInstance()->getAudioMode();
    const stream_attribute_t *pAttributeul = Base->getStreamAttributeTarget();

    setAfeDspShareMem(true);
    setDspRuntimeEn(TASK_SCENE_AUDPLAYBACK, true);
    if(AudioSmartPaController::getInstance()->isSwDspSpkProtect(pAttributeul->output_devices)) {
        setDspRefRuntimeEn(TASK_SCENE_AUDPLAYBACK, true);
    }
    mAudioMessengerIPI->registerAdspFeature(AUDIO_PLAYBACK_FEATURE_ID);
    mAudioMessengerIPI->registerDmaCbk(
        TASK_SCENE_AUDPLAYBACK,
        0x08000,
        0x10000,
        AudioALSAPlaybackHandlerBase::processDmaMsgWrapper,
        playbackHandler);

    playbackHandler->OpenPCMDumpDSP(LOG_TAG, TASK_SCENE_AUDPLAYBACK);

    /* clean config*/
    memset((void *)&mPlaybackUlConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mPlaybackIVConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mPlaybackDlConfig, 0, sizeof(struct pcm_config));
    memset((void *)&mDspConfig, 0, sizeof(struct pcm_config));

    AudioALSAHardwareResourceManager *pHwResourceManager = AudioALSAHardwareResourceManager::getInstance();
    multiplier = (pAttributeul->sample_rate / AUDIO_MULTIPLIER) + 1;

    mPlaybackUlConfig.rate = pAttributeul->sample_rate;
    mPlaybackUlConfig.channels = pAttributeul->num_channels;
    mPlaybackUlConfig.format = PCM_FORMAT_S24_LE;
    mPlaybackUlConfig.period_count = AUDPLAYBACK_UL_TASK_PEROID_COUNT;
    mPlaybackUlConfig.period_size = (AUDPLAYBACK_UL_SAMPLES * multiplier) / AUDPLAYBACK_UL_TASK_PEROID_COUNT;
    mPlaybackUlConfig.stop_threshold = ~(0U);
    mPlaybackUlConfig.silence_threshold = 0;

    /* todo : can change real dl sample rate ??*/
    mPlaybackDlConfig.rate = pAttributeul->sample_rate;
    mPlaybackDlConfig.channels = pAttributeul->num_channels;
    mPlaybackDlConfig.format = PCM_FORMAT_S24_LE;
    mPlaybackDlConfig.period_count = AUDPLAYBACK_DL_TASK_PEROID_COUNT;
    mPlaybackDlConfig.period_size = (AUDPLAYBACK_DL_SAMPLES * multiplier) / AUDPLAYBACK_UL_TASK_PEROID_COUNT;
    mPlaybackDlConfig.start_threshold = (mPlaybackDlConfig.period_count * mPlaybackDlConfig.period_size);
    mPlaybackDlConfig.stop_threshold = ~(0U);
    mPlaybackDlConfig.silence_threshold = 0;

    mPlaybackIVConfig.rate = pAttributeul->sample_rate;
    mPlaybackIVConfig.channels = pAttributeul->num_channels;
    if (pHwResourceManager->is32BitI2sSupport()) {
        mPlaybackIVConfig.format = PCM_FORMAT_S32_LE;
    } else {
        mPlaybackIVConfig.format = PCM_FORMAT_S24_LE;
    }
    mPlaybackIVConfig.period_count = AUDPLAYBACK_UL_TASK_PEROID_COUNT;
    mPlaybackIVConfig.period_size = (AUDPLAYBACK_UL_SAMPLES * multiplier) / AUDPLAYBACK_UL_TASK_PEROID_COUNT;
    mPlaybackIVConfig.stop_threshold = ~(0U);
    mPlaybackIVConfig.silence_threshold = 0;

    /* todo : can change real dl sample rate ??*/
    mDspConfig.rate = pAttributeul->sample_rate;
    mDspConfig.channels = pAttributeul->num_channels;
    mDspConfig.format = PCM_FORMAT_S24_LE;
    mDspConfig.period_count = AUDPLAYBACK_DL_TASK_PEROID_COUNT;
    mDspConfig.period_size = (AUDPLAYBACK_DL_SAMPLES * multiplier) / AUDPLAYBACK_UL_TASK_PEROID_COUNT;
    mDspConfig.start_threshold = (mDspConfig.period_count * mDspConfig.period_size);
    mDspConfig.stop_threshold = ~(0U);
    mDspConfig.silence_threshold = 0;

    /* get card index and pcm index*/
    /* todo : pcm should move to platform */
    mStreamCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback4);
    mDspIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDsp);

    mPlaybackUlindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture6);
    mPlaybackIVindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture4);
    mPlaybackDlindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback4);

    /* here task audplayback source == target*/
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    uint32_t aurisys_scenario = AURISYS_SCENARIO_DSP_PLAYBACK;

    if (AudioSmartPaController::getInstance()->isSwDspSpkProtect(pAttributeul->output_devices)) {
        aurisys_scenario = AURISYS_SCENARIO_DSP_PLAYBACK_SMARTPA;
    }

    CreateAurisysLibManager(
        &mAurisysLibManagerPlayback,
        &mAurisysDspConfigPlayback,
        TASK_SCENE_AUDPLAYBACK,
        aurisys_scenario,
        ARSI_PROCESS_TYPE_DL_ONLY,
        mDspStreamAttribute.audio_mode,
        &mDspStreamAttribute,
        &mDspStreamAttribute,
        NULL,
        NULL);

#endif

    mApTurnOnSequence = pHwResourceManager->getOutputTurnOnSeq(pAttributeul->output_devices,
                                                               false, AUDIO_CTL_PLAYBACK4);
    if (AudioSmartPaController::getInstance()->isSmartPAUsed() &&
        (pAttributeul->output_devices & AUDIO_DEVICE_OUT_SPEAKER) &&
        popcount(pAttributeul->output_devices) > 1) {
        mApTurnOnSequence2 = pHwResourceManager->getOutputTurnOnSeq(pAttributeul->output_devices,
                                                                    true, AUDIO_CTL_PLAYBACK4);
    }
    if(AudioSmartPaController::getInstance()->isSwDspSpkProtect(pAttributeul->output_devices)) {
        mApTurnOnSequenceIV = AudioSmartPaController::getInstance()->getI2sSequence(AUDIO_CTL_I2S_TO_CAPTURE4, true);
        AudioSmartPaController::getInstance()->setI2sInHD(true);
        pHwResourceManager->enableTurnOnSequence(mApTurnOnSequenceIV);
    }
    pHwResourceManager->setCustOutputDevTurnOnSeq(pAttributeul->output_devices,
                                                  mTurnOnSeqCustDev1, mTurnOnSeqCustDev2);

    pHwResourceManager->enableTurnOnSequence(mApTurnOnSequence);
    pHwResourceManager->enableTurnOnSequence(mApTurnOnSequence2);
    pHwResourceManager->enableTurnOnSequence(mTurnOnSeqCustDev1);
    pHwResourceManager->enableTurnOnSequence(mTurnOnSeqCustDev2);

    ALOGD("%s(), mPlaybackDlConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d, "
          "mPlaybackIVConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d, "
          "mPlaybackUlConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d, "
          "mDspConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d",
          __FUNCTION__,
          mPlaybackDlConfig.channels, mPlaybackDlConfig.rate, mPlaybackDlConfig.period_size, mPlaybackDlConfig.period_count, mPlaybackDlConfig.format,
          mPlaybackIVConfig.channels, mPlaybackIVConfig.rate, mPlaybackIVConfig.period_size, mPlaybackIVConfig.period_count, mPlaybackIVConfig.format,
          mPlaybackUlConfig.channels, mPlaybackUlConfig.rate, mPlaybackUlConfig.period_size, mPlaybackUlConfig.period_count, mPlaybackUlConfig.format,
          mDspConfig.channels, mDspConfig.rate, mDspConfig.period_size, mDspConfig.period_count, mDspConfig.format);

    ASSERT(mPlaybackDlPcm == NULL);
    mPlaybackDlPcm = pcm_open(mStreamCardIndex,
                              mPlaybackDlindex, PCM_OUT | PCM_MONOTONIC, &mPlaybackDlConfig);

    if (mPlaybackDlPcm == NULL) {
        ALOGE("%s(), mPlaybackDlPcm == NULL!!", __FUNCTION__);
        ASSERT(mPlaybackDlPcm != NULL);
        return -1;
    } else if (pcm_is_ready(mPlaybackDlPcm) == false) {
        ALOGE("%s(), DL pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mPlaybackDlPcm, pcm_get_error(mPlaybackDlPcm));
        pcm_close(mPlaybackDlPcm);
        mPlaybackDlPcm = NULL;
        return -1;
    } else if (pcm_prepare(mPlaybackDlPcm) != 0) {
        ALOGE("%s(), DL pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mPlaybackDlPcm, pcm_get_error(mPlaybackDlPcm));
        pcm_close(mPlaybackDlPcm);
        mPlaybackDlPcm = NULL;
        return -1;
    }

    if (AudioSmartPaController::getInstance()->isSwDspSpkProtect(pAttributeul->output_devices)) {
        ASSERT(mPlaybackIVPcm == NULL);

        mPlaybackIVPcm = pcm_open(mStreamCardIndex,
                                  mPlaybackIVindex, PCM_IN | PCM_MONOTONIC, &mPlaybackIVConfig);

        if (mPlaybackIVPcm == NULL) {
            ALOGE("%s(), mPlaybackIVPcm == NULL!!", __FUNCTION__);
            ASSERT(mPlaybackIVPcm != NULL);
            return -1;
        } else if (pcm_is_ready(mPlaybackIVPcm) == false) {
            ALOGE("%s(), IV pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mPlaybackIVPcm, pcm_get_error(mPlaybackIVPcm));
            pcm_close(mPlaybackIVPcm);
            mPlaybackIVPcm = NULL;
            return -1;
        } else if (pcm_prepare(mPlaybackIVPcm) != 0) {
            ALOGE("%s(), IV pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mPlaybackIVPcm, pcm_get_error(mPlaybackIVPcm));
            pcm_close(mPlaybackIVPcm);
            mPlaybackIVPcm = NULL;
            return -1;
        }
     }

    ASSERT(mPlaybackUlPcm == NULL);
    if (this->getDspVersion() == DSP_VER_HARDWARE_MIX) {
        ALOGD("%s(), pcm_open mPlaybackUlPcm", __FUNCTION__);
        mPlaybackUlPcm = pcm_open(mStreamCardIndex,
                                  mPlaybackUlindex, PCM_IN | PCM_MONOTONIC, &mPlaybackUlConfig);

        if (mPlaybackUlPcm == NULL) {
            ALOGE("%s(), mPlaybackUlPcm mPlaybackUlPcm == NULL!!", __FUNCTION__);
            ASSERT(mPlaybackUlPcm != NULL);
            return -1;
        } else if (pcm_is_ready(mPlaybackUlPcm) == false) {
            ALOGE("%s(), mPlaybackUlPcm pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mPlaybackUlPcm, pcm_get_error(mPlaybackUlPcm));
            pcm_close(mPlaybackUlPcm);
            mPlaybackUlPcm = NULL;
            return -1;
        } else if (pcm_prepare(mPlaybackUlPcm) != 0) {
            ALOGE("%s(), mPlaybackUlPcm pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mPlaybackUlPcm, pcm_get_error(mPlaybackUlPcm));
            pcm_close(mPlaybackUlPcm);
            mPlaybackUlPcm = NULL;
            return -1;
        }
    } else
        ALOGD("%s(), getDspVersion[%d] not open aud_playbnack UL!!",
              __FUNCTION__, this->getDspVersion());

    ASSERT(mDspPcm == NULL);
    mDspPcm = pcm_open(mStreamCardIndex,
                       mDspIndex, PCM_OUT | PCM_MONOTONIC, &mDspConfig);

    if (mDspPcm == NULL) {
        ALOGE("%s(), mDspPcm == NULL!!", __FUNCTION__);
        ASSERT(mDspPcm != NULL);
        return -1;
    } else if (pcm_is_ready(mDspPcm) == false) {
        ALOGE("%s(), dsp pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mDspPcm, pcm_get_error(mDspPcm));
        pcm_close(mDspPcm);
        mDspPcm = NULL;
        return -1;
    } else if (pcm_prepare(mDspPcm) != 0) {
        ALOGE("%s(), dps pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mDspPcm, pcm_get_error(mDspPcm));
        pcm_close(mDspPcm);
        mDspPcm = NULL;
        return -1;
    }

    return NO_ERROR;
}

/* clsoe playbacktask , also stop before close */
int AudioDspStreamManager::closePlaybackTask(AudioALSAPlaybackHandlerBase *playbackHandler) {
    int ret = 0;
    AudioALSAHardwareResourceManager *pHwResourceManager = AudioALSAHardwareResourceManager::getInstance();

    if (mPlaybackHandlerVector.size() != 0) {
        ALOGE("-%s(), mPlaybackHandlerVector.size = %zd", __FUNCTION__, mPlaybackHandlerVector.size());
    }

    if (getDspPlaybackEnable() == false) {
        ALOGW("%s(),DSP_STREAM_NOTSUPPORT", __FUNCTION__);
        return DSP_STREAM_NOTSUPPORT;
    }

    pHwResourceManager->disableTurnOnSequence(mTurnOnSeqCustDev1);
    pHwResourceManager->disableTurnOnSequence(mTurnOnSeqCustDev2);

    if (mDspPcm != NULL) {
        ret = pcm_stop(mDspPcm);
        if (ret != 0) {
            ALOGE("%s(), pcm_stop(%p) fail due to %s", __FUNCTION__, mDspPcm, pcm_get_error(mDspPcm));
            triggerDsp(TASK_SCENE_AUDPLAYBACK, AUDIO_DSP_TASK_STOP);
        }
        pcm_close(mDspPcm);
        mDspPcm = NULL;
    }

    if (mPlaybackUlPcm != NULL) {
        pcm_stop(mPlaybackUlPcm);
        pcm_close(mPlaybackUlPcm);
        mPlaybackUlPcm = NULL;
    }
    if (mPlaybackIVPcm != NULL) {
        pcm_stop(mPlaybackIVPcm);
        pcm_close(mPlaybackIVPcm);
        mPlaybackIVPcm = NULL;
    }
    if (mPlaybackDlPcm != NULL) {
        pcm_stop(mPlaybackDlPcm);
        pcm_close(mPlaybackDlPcm);
        mPlaybackDlPcm = NULL;
    }

    pHwResourceManager->disableTurnOnSequence(mApTurnOnSequence);
    pHwResourceManager->disableTurnOnSequence(mApTurnOnSequence2);
    if (!mApTurnOnSequenceIV.isEmpty()) {
        pHwResourceManager->disableTurnOnSequence(mApTurnOnSequenceIV);
        AudioSmartPaController::getInstance()->setI2sInHD(false);
    }
    setDspRefRuntimeEn(TASK_SCENE_AUDPLAYBACK, false);
    setAfeDspShareMem(false);
    setDspRuntimeEn(TASK_SCENE_AUDPLAYBACK, false);
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    DestroyAurisysLibManager(&mAurisysLibManagerPlayback, &mAurisysDspConfigPlayback, TASK_SCENE_AUDPLAYBACK);
#endif
    mAudioMessengerIPI->deregisterDmaCbk(TASK_SCENE_AUDPLAYBACK);
    mAudioMessengerIPI->deregisterAdspFeature(AUDIO_PLAYBACK_FEATURE_ID);

    playbackHandler->ClosePCMDumpDSP(TASK_SCENE_AUDPLAYBACK);

    ALOGD("-%s(), mDspTaskPlaybackActive = %d, mPlaybackUlPcm = %p mPlaybackDlPcm = %p, mDspPcm = %p",
          __FUNCTION__, mDspTaskPlaybackActive, mPlaybackUlPcm, mPlaybackDlPcm, mDspPcm);

    return 0;
}

int AudioDspStreamManager::startDevice(audio_devices_t devices)
{
    ALOGD("+%s() Devices 0x%x mOutputDeviceCount 0x%x", __FUNCTION__, devices, mOutputDeviceCount);
    AL_LOCK(mDeviceLock);
    mOutputDevice = devices;
    if (mOutputDeviceCount == 0) {
        startTasks_l();
    }
    mOutputDeviceCount++;
    AL_UNLOCK(mDeviceLock);
    return 0;
}

/*
 * need to do lock with mDeviceLock avoid change output device.
 * please call startTasks_l with mDeviceLock hold.
 * startTasks_l may has no mPlaybackHandler and do nothing
 * because of non-dsp playbackhandler may open before.
 */
int AudioDspStreamManager::startTasks_l()
{
    if (mPlaybackHandlerVector.size() == 0) {
        ALOGE("%s() but size 0", __FUNCTION__);
        return -1;
    }
    AL_LOCK(mTaskLock);

    if (needStartPlaybackTask(mOutputDevice)) {
        startPlaybackTask();
        mDspStreamState = DSP_STREAM_START;
        ALOGD("%s() mDspStreamState = DSP_STREAM_START", __FUNCTION__);
    }
#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    if (needStartA2dpTask(mOutputDevice)) {
        startA2dpTask();
        mDspA2DPStreamState = DSP_STREAM_START;
        ALOGD("%s() mDspA2DPStreamState = DSP_STREAM_START", __FUNCTION__);
    }
#endif

    AL_UNLOCK(mTaskLock);
    return 0;
}

/* need to do lock with mDeviceLock , avoid change with output device */
int AudioDspStreamManager::stopTasks_l(bool force)
{
    ALOGD("+%s()", __FUNCTION__);
    AL_LOCK(mTaskLock);

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    if (needStopA2dpTask(mOutputDevice, force)) {
        stopA2dpTask();
        ALOGD("%s() mDspA2DPStreamState = DSP_STREAM_STOP", __FUNCTION__);
    }
#endif
    if (needStopPlaybackTask(mOutputDevice, force)) {
        stopPlaybackTask();
        mDspStreamState = DSP_STREAM_STOP;
        ALOGD("%s() mDspStreamState = DSP_STREAM_STOP", __FUNCTION__);
    }
    AL_UNLOCK(mTaskLock);
    return 0;
}

int AudioDspStreamManager::stopDevice(audio_devices_t devices)
{
    ALOGD("+%s() Devices 0x%x mOutputDeviceCount 0x%x", __FUNCTION__, devices, mOutputDeviceCount);
    AL_LOCK(mDeviceLock);
    mOutputDeviceCount--;
    mOutputDevice = devices;
    if (mOutputDeviceCount == 0) {
        stopTasks_l(true);
    }

    if (mOutputDeviceCount < 0) {
        ALOGW("%s() mOutputDeviceCount 0x%x", __FUNCTION__, mOutputDeviceCount);
        mOutputDeviceCount = 0;
        ASSERT(0);
    }

    AL_UNLOCK(mDeviceLock);

    ALOGD("-%s() Devices 0x%x", __FUNCTION__, devices);
    return 0;
}

int AudioDspStreamManager::addPlaybackHandler(AudioALSAPlaybackHandlerBase *playbackHandler) {
    if (getDspPlaybackEnable() == false && getDspA2DPEnable() == false) {
        ALOGW("%s() and not support", __FUNCTION__);
        return -1;
    }

    AL_AUTOLOCK(mLock);
    AL_LOCK(mDeviceLock);

    mPlaybackHandlerVector.add((unsigned long long)playbackHandler, playbackHandler);
    const stream_attribute_t *attribute = playbackHandler->getStreamAttributeTarget();
    dumpPlaybackHandler();

    if (needOpenPlaybackTask() == DSP_STREAM_OPEN) {
        openPlaybackTask(playbackHandler);
        mDspStreamState = DSP_STREAM_OPEN;
        ALOGD("%s() mDspStreamState = %d", __FUNCTION__, mDspStreamState);
    }

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    if (needOpenA2dpTask() == DSP_STREAM_OPEN) {
        openA2dpTask(playbackHandler);
        mDspA2DPStreamState = DSP_STREAM_OPEN;
        ALOGD("%s() mDspA2DPStreamState = %d", __FUNCTION__, mDspA2DPStreamState);
    }
#endif

    /* if devicecount is not 0, means device is started */
    if (mOutputDeviceCount > 0) {
        ALOGD("%s(), startTasks_l mOutputDeviceCount[%d]", __FUNCTION__, mOutputDeviceCount);
        startTasks_l();
    }
    AL_UNLOCK(mDeviceLock);

    if (getDspVersion() == DSP_VER_SW_MIX) {
        if (checkMusicTaskStatus() == DSP_STREAM_START) {
            startMusicTask(playbackHandler);
            mDspMusicStreamState = DSP_STREAM_START;
            ALOGD("%s() mDspMusicStreamState = DSP_STREAM_START", __FUNCTION__);
        }
    }

    ALOGD("-%s(), playbackHandler = %llu %p", __FUNCTION__, (unsigned long long)playbackHandler, playbackHandler);

    return 0;
}

int AudioDspStreamManager::removePlaybackHandler(AudioALSAPlaybackHandlerBase *playbackHandler) {
    AL_AUTOLOCK(mLock);
    ALOGD("+%s(), playbackHandler = %llu %p", __FUNCTION__,
         (unsigned long long)playbackHandler, playbackHandler);

    if (getDspPlaybackEnable() == false && getDspA2DPEnable() == false) {
        ALOGW("%s() and not support", __FUNCTION__);
        return -1;
    }

    mPlaybackHandlerVector.removeItem((unsigned long long)playbackHandler);

    AL_LOCK(mDeviceLock);

    if (getDspVersion() == DSP_VER_SW_MIX) {
        if (checkMusicTaskStatus() == DSP_STREAM_STOP) {
            stopMusicTask(playbackHandler);
            mDspMusicStreamState = DSP_STREAM_STOP;
            ALOGD("%s() mDspMusicStreamState = DSP_STREAM_STOP", __FUNCTION__);
        }
    }

    /* when  no dsp handler */
    if (mPlaybackHandlerVector.size() == 0){
        ALOGD("%s() mOutputDeviceCount 0x%x", __FUNCTION__, mOutputDeviceCount);
        stopTasks_l(false);
    }

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    if (needOpenA2dpTask() == DSP_STREAM_CLOSE) {
        AL_LOCK(mTaskLock);
        if (needStopA2dpTask(mOutputDevice, false)) {
            stopA2dpTask();
        }
        AL_UNLOCK(mTaskLock);
        closeA2dpTask(playbackHandler);
        mDspA2DPStreamState = DSP_STREAM_CLOSE;
        ALOGD("%s() mDspA2DPStreamState = DSP_STREAM_STOP", __FUNCTION__);
    }
#endif

    if (needOpenPlaybackTask() == DSP_STREAM_CLOSE) {
        closePlaybackTask(playbackHandler);
        mDspStreamState = DSP_STREAM_CLOSE;
    }
    AL_UNLOCK(mDeviceLock);

    ALOGV("-%s(), playbackHandler = %llu %p mDspStreamState = %d",
          __FUNCTION__, (unsigned long long)playbackHandler, playbackHandler, mDspStreamState);
    dumpPlaybackHandler();
    return 0;
}

int AudioDspStreamManager::addCaptureDataProvider(AudioALSACaptureDataProviderBase *dataProvider) {
    uint32_t size = 0;
    ALOGD("%s() CaptureDataProvider = %llu %p", __FUNCTION__, (unsigned long long)dataProvider, dataProvider);

    AL_AUTOLOCK(mCaptureDspLock);
    AL_LOCK(mCaptureDspVectorLock);
    mCaptureDataProviderVector.add((unsigned long long)dataProvider, dataProvider);
    size = mCaptureDataProviderVector.size();
    dumpCaptureDataProvider();
    AL_UNLOCK(mCaptureDspVectorLock);

    if (size == 1) {
        openCaptureDspHwPcm(dataProvider);
    } else {
        UpdateCaptureDspLatency();
    }

    return 0;
}

int AudioDspStreamManager::removeCaptureDataProvider(AudioALSACaptureDataProviderBase *dataProvider) {
    uint32_t size = 0;
    ALOGD("%s() CaptureDataProvider = %llu %p, size = %zu",
          __FUNCTION__, (unsigned long long)dataProvider, dataProvider, mCaptureDataProviderVector.size());

    AL_AUTOLOCK(mCaptureDspLock);
    AL_LOCK(mCaptureDspVectorLock);
    mCaptureDataProviderVector.removeItem((unsigned long long)dataProvider);
    size = mCaptureDataProviderVector.size();
    dumpCaptureDataProvider();
    AL_UNLOCK(mCaptureDspVectorLock);

    if (size == 0) {
        closeCaptureDspHwPcm(dataProvider);
        mLowLatencyDspCapture = false;
    } else {
        UpdateCaptureDspLatency();
    }
    dumpCaptureDataProvider();
    return 0;
}

int AudioDspStreamManager::openCaptureDspHwPcm(AudioALSACaptureDataProviderBase *dataProvider) {
    int pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture1);
    int cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmCapture1);

    struct pcm_params *params;
    const stream_attribute_t *pAttrSource = dataProvider->getStreamAttributeSource();
    bool bHDRRecord = false;

    /* clean config */
    memset((void *)&mCaptureDspConfig, 0, sizeof(struct pcm_config));

    setAfeInDspShareMem(true, false);
    setDspRuntimeEn(TASK_SCENE_CAPTURE_UL1, true);

    params = pcm_params_get(cardindex, pcmindex, PCM_IN);
    if (params == NULL) {
        ALOGD("Device does not exist.\n");
    }

    unsigned int buffersizemax = pcm_params_get_max(params, PCM_PARAM_BUFFER_BYTES);
    pcm_params_free(params);

#ifdef RECORD_INPUT_24BITS
    mCaptureDspConfig.format = PCM_FORMAT_S24_LE;
#else
    mCaptureDspConfig.format = PCM_FORMAT_S16_LE;
#endif
    mCaptureDspConfig.period_count = 6;

    if (pAttrSource->latency == UPLINK_HIFI3_LOW_LATENCY_MS) {
        mCaptureDspConfig.period_count = mCaptureDspConfig.period_count * UPLINK_NORMAL_LATENCY_MS / UPLINK_HIFI3_LOW_LATENCY_MS;
        mLowLatencyDspCapture = true;
    }

    bHDRRecord = AudioALSAHardwareResourceManager::getInstance()->getHDRRecord();
    mCaptureDspConfig.channels = AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport();
    if (pAttrSource->input_device == AUDIO_DEVICE_IN_WIRED_HEADSET ||
        pAttrSource->input_source == AUDIO_SOURCE_UNPROCESSED){
        mCaptureDspConfig.channels = 1;
    } else if (mCaptureDspConfig.channels > 2) {
        mCaptureDspConfig.channels = 4;
    } else {
        mCaptureDspConfig.channels = 2;
    }

    if (bHDRRecord) {
        if (pAttrSource->input_device == AUDIO_DEVICE_IN_WIRED_HEADSET) {
            mCaptureDspConfig.channels = 2;
        } else {
            mCaptureDspConfig.channels = 4;
        }
    }

#ifdef UPLINK_DSP_RAW_FORCE_4CH
    mCaptureDspConfig.channels = 4;
#endif
    mCaptureDspConfig.rate = pAttrSource->sample_rate;

#ifdef UPLINK_LOW_LATENCY
    mCaptureDspConfig.period_size =
        ((audio_bytes_per_sample(pAttrSource->audio_format) * mCaptureDspConfig.channels * mCaptureDspConfig.rate * pAttrSource->latency) / 1000) / mCaptureDspConfig.channels / (pcm_format_to_bits(mCaptureDspConfig.format) / 8);  //period size will impact the interrupt interval
#else
    mCaptureDspConfig.period_size =
        (buffersizemax / mCaptureDspConfig.channels / (pcm_format_to_bits(mCaptureDspConfig.format) / 8) / mCaptureDspConfig.period_count;
#endif
    mCaptureDspConfig.start_threshold = 0;
    mCaptureDspConfig.stop_threshold = mCaptureDspConfig.period_size * mCaptureDspConfig.period_count;
    mCaptureDspConfig.silence_threshold = 0;

    /* allocate the same with dsp platform drver */
    mCaptureDspConfig.stop_threshold = ~(0U);
    ALOGD("capture mCaptureDspConfig format: %d, channels: %d, rate: %d, period_size: %d, period_count: %d",
          mCaptureDspConfig.format, mCaptureDspConfig.channels, mCaptureDspConfig.rate, mCaptureDspConfig.period_size, mCaptureDspConfig.period_count);

    mCaptureDspPcm = pcm_open(cardindex, pcmindex, PCM_IN | PCM_MONOTONIC, &mCaptureDspConfig);
    ASSERT(mCaptureDspPcm != NULL);
    ALOGD("%s(), mDspHwPcm = %p", __FUNCTION__, mCaptureDspPcm);

    if (pcm_prepare(mCaptureDspPcm) != 0) {
        ALOGE("%s(), pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mCaptureDspPcm, pcm_get_error(mCaptureDspPcm));
        ASSERT(0);
        pcm_close(mCaptureDspPcm);
        mCaptureDspPcm = NULL;
        return UNKNOWN_ERROR;
    }

    if (pcm_start(mCaptureDspPcm) != 0) {
        ASSERT(0);
        pcm_close(mCaptureDspPcm);
        mCaptureDspPcm = NULL;
        return UNKNOWN_ERROR;
    }
    if (pAttrSource->input_device == AUDIO_DEVICE_IN_WIRED_HEADSET &&
        AudioALSAHandlerKtv::getInstance()->getTaskKtvStatus() &&
        (pAttrSource->mVoIPEnable != true)) {
        AudioALSAHandlerKtv::getInstance()->startKtvTask(pAttrSource);
    }
    return 0;
}

int AudioDspStreamManager::closeCaptureDspHwPcm(AudioALSACaptureDataProviderBase *dataProvider) {
    const stream_attribute_t *pAttrSource = dataProvider->getStreamAttributeSource();

    if (pAttrSource->input_device == AUDIO_DEVICE_IN_WIRED_HEADSET && getDspRuntimeEn(TASK_SCENE_KTV)) {
        AudioALSAHandlerKtv::getInstance()->stopKtvTask();
    }

    if (pcm_stop(mCaptureDspPcm) != 0) {
        ALOGE("%s() pcm_stop hReadThread fail!!", __FUNCTION__);
    }
    pcm_close(mCaptureDspPcm);
    setAfeInDspShareMem(false, false);
    setDspRuntimeEn(TASK_SCENE_CAPTURE_UL1, false);
    return 0;
}

int AudioDspStreamManager::UpdateCaptureDspLatency() {
    struct ipi_msg_t ipi_msg;
    bool newLowLatencyFlag = false;
    uint32_t latency = 0;
    static AudioLock updateLock;

    AL_AUTOLOCK(updateLock);

    newLowLatencyFlag = HasLowLatencyCaptureDsp();
    if (newLowLatencyFlag != mLowLatencyDspCapture) {
        ALOGD("%s(), hasLowLatency %d => %d", __FUNCTION__, mLowLatencyDspCapture, newLowLatencyFlag);
        if (newLowLatencyFlag == true) {
            latency = UPLINK_HIFI3_LOW_LATENCY_MS;
        } else {
            latency = UPLINK_NORMAL_LATENCY_MS;
        }

        mAudioMessengerIPI->sendIpiMsg(
                        &ipi_msg,
                        TASK_SCENE_CAPTURE_UL1,
                        AUDIO_IPI_LAYER_TO_DSP,
                        AUDIO_IPI_MSG_ONLY,
                        AUDIO_IPI_MSG_NEED_ACK,
                        AUDIO_DSP_TASK_UPDATE_UL_PIPE_LATENCY,
                        latency,
                        0,
                        NULL);
        mLowLatencyDspCapture = newLowLatencyFlag;
        AudioALSAHardwareResourceManager::getInstance()->setULInterruptRate(mCaptureDspConfig.rate *
                                                                            latency / 1000);
    }
    return 0;
}

bool AudioDspStreamManager::HasLowLatencyCaptureDsp() {
    size_t i = 0;
    AL_AUTOLOCK(mCaptureDspVectorLock);
    AudioALSACaptureDataProviderBase *Base = NULL;
    for (i = 0; i < mCaptureDataProviderVector.size(); i++) {
        Base = mCaptureDataProviderVector.valueAt(i);
        if (Base != NULL && Base->HasLowLatencyCapture()) {
            return true;
        }
    }
    return false;
}

int AudioDspStreamManager::dumpPlaybackHandler() {
#ifdef DEBUG_VERBOSE
    size_t i = 0;
    for (i = 0; i < mPlaybackHandlerVector.size(); i++) {
        AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(i);
        ALOGD("%s()playbackHandler = %llu", __FUNCTION__, (unsigned long long)Base);
    }
#endif
    return 0;
}

int AudioDspStreamManager::dumpCaptureDataProvider() {
#ifdef DEBUG_VERBOSE
    size_t i = 0;
    for (i = 0; i < mCaptureDataProviderVector.size(); i++) {
        AudioALSACaptureDataProviderBase *Base = mCaptureDataProviderVector.valueAt(i);
        ALOGD("%s()captureDataProvider = %llu", __FUNCTION__, (unsigned long long)Base);
    }
#endif
    return 0;
}

unsigned int AudioDspStreamManager::getUlLatency(void) {
    if (getDspPlaybackEnable() == true)
        return AUDPLAYBACK_UL_SAMPLES * 1000 * multiplier / mPlaybackDlConfig.rate;
    else
        return 0;
}
unsigned int AudioDspStreamManager::getDlLatency(void) {
    unsigned int dllatency = 0;
    if (getDspPlaybackEnable() == true)
        dllatency = AUDPLAYBACK_DL_SAMPLES * 1000 * multiplier/ mPlaybackDlConfig.rate;
    else
        dllatency =  0;
    return dllatency;
}

unsigned int AudioDspStreamManager::getDspSample(void) {
    unsigned int dspSample = 0;
    if (getDspPlaybackEnable() == true) {
         /* dsp playback DL sample + swmixer buffer */
        dspSample = AUDPLAYBACK_DL_SAMPLES;
    } else {
        dspSample =  0;
    }
    return dspSample;
}

unsigned int AudioDspStreamManager::getA2dpPcmLatency(void) {
    unsigned int a2dplatency = A2DPPLAYBACK_DL_SAMPLES * 1000 * multiplier/ mDspConfig.rate;
    return A2DPPLAYBACK_DL_SAMPLES * 1000 * multiplier/ mDspConfig.rate;
}

bool AudioDspStreamManager::getDspTaskPlaybackStatus(void) {
    return mDspTaskPlaybackActive;
}
bool AudioDspStreamManager::getDspTaskA2DPStatus(void) {
    return mDspTaskA2DPActive;
}

/* get audio dsp support stream */
unsigned int AudioDspStreamManager::getDspOutHandlerEnable(unsigned int flag, unsigned int device) {
    unsigned int ret = 0, device_support = 0;
    int retval = 0;
    struct mixer_ctl *ctl = NULL;

    /* deep buffer */
    if ((flag & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) && !(flag & AUDIO_OUTPUT_FLAG_PRIMARY)){
        ctl = mixer_get_ctl_by_name(mMixer, "dsp_deepbuf_default_en");
        if (ctl == NULL)
            return 0;
        ret = mixer_ctl_get_value(ctl, 0);
    } else if (flag & AUDIO_OUTPUT_FLAG_VOIP_RX) { /* void dl */
        ctl = mixer_get_ctl_by_name(mMixer, "dsp_voipdl_default_en");
        if (ctl == NULL)
            return 0;
        ret = mixer_ctl_get_value(ctl, 0);

    } else if (flag & AUDIO_OUTPUT_FLAG_PRIMARY) { /* primary dl */
        ctl = mixer_get_ctl_by_name(mMixer, "dsp_primary_default_en");
        if (ctl == NULL)
            return 0;
        ret = mixer_ctl_get_value(ctl, 0);
    } else if (isDspLowLatencyFlag((audio_output_flags_t)flag)) { /* fast dl */
        ctl = mixer_get_ctl_by_name(mMixer, "dsp_fast_default_en");
        if (ctl == NULL)
            return 0;
        ret = mixer_ctl_get_value(ctl, 0);
    } else if (flag & AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD) { /* compress */
        /* for compress , compress only can run with dsp */
        ctl = mixer_get_ctl_by_name(mMixer, "dsp_offload_default_en");
        if (ctl == NULL) {
            return 0;
        }
        ret = mixer_ctl_get_value(ctl, 0);
        return ret;
    } else if (flag & AUDIO_OUTPUT_FLAG_FM_ADSP) { /* fm adsp dl */
        ctl = mixer_get_ctl_by_name(mMixer, "dsp_fm_default_en");
        if (ctl == NULL)
            return 0;
        ret = mixer_ctl_get_value(ctl, 0);
    } else {
        ALOGD("%s flag = %x not support", __func__, flag);
        ret = 0;
    }

    /* hw mix just enable and disable . no device support.*/
    if (getDspVersion() == 0)
        return ret;

    device_support = ret >> 1;
    /* device support and enable*/
    if (device & (AUDIO_DEVICE_OUT_ALL_A2DP)) {
        retval = true;
    } else if ((device & device_support) && (ret & AUD_TASK_DEFAULT_ENABLE)) {
        retval = true;
    } else {
        retval = false;
    }
    ALOGD("%s flag = %x ret = %d device = %u device_support = %u retval = %d",
          __func__, flag, ret, device, device_support, retval);
    return retval;
}

/* todo : judge input flag */
int AudioDspStreamManager::getDspInHandlerEnable(unsigned int flag) {
    int ret = 0;
    struct mixer_ctl *ctl = NULL;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_captureul1_default_en");
    if (ctl == NULL)
        return 0;
    ret = mixer_ctl_get_value(ctl, 0);
    ALOGD("%s flag = %x ret = %d", __func__, flag, ret);
    return ret;
}

int AudioDspStreamManager::getDspRawInHandlerEnable(unsigned int flag) {
    int ret = 0;
    struct mixer_ctl *ctl = NULL;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_captureraw_default_en");
    if (ctl == NULL)
        return 0;
    ret = mixer_ctl_get_value(ctl, 0);
    ALOGD("%s flag = %x ret = %d", __func__, flag, ret);
    return ret;
}

int AudioDspStreamManager::getDspPlaybackEnable() {
    int ret = 0;
    struct mixer_ctl *ctl = NULL;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_playback_default_en");
    if (ctl == NULL)
        return 0;
    ret = mixer_ctl_get_value(ctl, 0);
    return ret;
}

/* set adsp version*/
int AudioDspStreamManager::setDspVersion(int version) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "audio_dsp_version"), 0, version)) {
        ALOGW("%s(), setDspVersion fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

/* get adsp version*/
int AudioDspStreamManager::getDspVersion(void) {
    int ret = 0;
    struct mixer_ctl *ctl = NULL;

    ctl = mixer_get_ctl_by_name(mMixer, "audio_dsp_version");
    if (ctl == NULL) {
        return 0;
    }
    ret = mixer_ctl_get_value(ctl, 0);
    return ret;
}

int AudioDspStreamManager::getDspA2DPEnable() {
    int ret = 0;
    struct mixer_ctl *ctl = NULL;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_a2dp_default_en");
    if (ctl == NULL) {
        ALOGE("-%s Mixer of dsp_a2dp_default_en = NULL!!", __FUNCTION__);
        return 0;
    }
    ret = mixer_ctl_get_value(ctl, 0);
    return ret;
}

int AudioDspStreamManager::getDspFmEnable() {
    int ret = 0;
    struct mixer_ctl *ctl = NULL;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_fm_default_en");
    if (ctl == NULL) {
        ALOGE("-%s Mixer of dsp_fm_default_en = NULL!!", __FUNCTION__);
        return 0;
    }
    ret = mixer_ctl_get_value(ctl, 0);
    return ret;
}

int AudioDspStreamManager::setParameters(String8 param)
{
    ALOGD("%s param = %s",  __FUNCTION__, param.string());
    int retval = 0, param2 = 0, task_scene = TASK_SCENE_INVALID;;

    char strbuf[MAX_PARATERS_LENGTH];
    char *temp = NULL;
    struct ipi_msg_t ipi_msg;

    /* check for length*/
    if (param.length() >= MAX_PARATERS_LENGTH - 1) {
        ALOGW("%s param %s too long", __FUNCTION__, param.string());
        WARNING("AudioDspStreamManager setParameters param too long");
        return INVALID_OPERATION;
    }

    mAudioMessengerIPI->registerAdspFeature(SYSTEM_FEATURE_ID);
    strncpy(strbuf, param.c_str(), sizeof(strbuf) - 1);
    strbuf[sizeof(strbuf) - 1] = '\0';

    /* parse param*/
    if (param.contains (DSP_PARAM_DEBUG_SET_STRING)){
        /* DSPPARAM=DEBUGFLAG,8,4  task_sceneid = 8 , debuglevel = 4*/
        retval = parseParameter(strbuf, &task_scene, &param2);
        if (retval != NO_ERROR)
            return retval;
        retval = mAudioMessengerIPI->sendIpiMsg(
                 &ipi_msg,
                 task_scene, AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
                 AUDIO_DSP_TASK_SETPRAM, DSP_PARAM_DEBUGLEVELSET, param2, NULL);
    } else if (param.contains (DSP_PARAM_PULSE_SET_STRING)){
        /* DSPPARAM=PULSEFLAG,8,1 task_sceneid = 8, PULSE_SET = 1*/
        retval = parseParameter(strbuf, &task_scene, &param2);
        if (retval != NO_ERROR)
            return retval;
        retval = mAudioMessengerIPI->sendIpiMsg(
                 &ipi_msg,
                 task_scene, AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
                 AUDIO_DSP_TASK_SETPRAM, DSP_PARAM_PULSEDETECTSET, param2, NULL);
    } else if (param.contains (DSP_PARAM_PAYLOAD)){
        ALOGD("%s DSP_PARAM_PAYLOAD strbuf %s", __FUNCTION__, strbuf);
        retval = mAudioMessengerIPI->sendIpiMsg(
                 &ipi_msg,
                 task_scene, AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_PAYLOAD, AUDIO_IPI_MSG_BYPASS_ACK,
                 AUDIO_DSP_TASK_SETPRAM, MAX_PARATERS_LENGTH, 0, strbuf);
    } else {
        ALOGE("%s(), not support command", __FUNCTION__);
    }

    /* check return value */
    if (retval != 0) {
        ALOGE("%s(), fail!! retval = %d", __FUNCTION__, retval);
    }

    mAudioMessengerIPI->deregisterAdspFeature(SYSTEM_FEATURE_ID);

    return 0;
}
int AudioDspStreamManager::triggerDsp(unsigned int task_scene, int data_type) {
    int retval;
    struct ipi_msg_t ipi_msg;

    if (data_type != AUDIO_DSP_TASK_START && data_type != AUDIO_DSP_TASK_STOP) {
        ALOGW("%s error task_scene = %u data_type = %u", __FUNCTION__, task_scene, data_type);
        return -1;
    }

    retval = mAudioMessengerIPI->sendIpiMsg(
                 &ipi_msg,
                 task_scene, AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
                 data_type, 0, 0, NULL);
    if (retval != 0) {
        ALOGE("%s(), fail!! retval = %d", __FUNCTION__, retval);
    }

    ALOGD("-%s task_scene = %d data_type = %d", __FUNCTION__, task_scene, data_type);
    return 0;
}

void AudioDspStreamManager::updateMode(audio_mode_t audioMode) {
    ALOGD("%s(), audioMode = %d", __FUNCTION__, audioMode);
    AL_AUTOLOCK(mLock);
    mDspStreamAttribute.audio_mode = audioMode;
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (mAurisysLibManagerPlayback != NULL && mAurisysDspConfigPlayback != NULL) {
        UpdateAurisysConfig(
            mAurisysLibManagerPlayback,
            mAurisysDspConfigPlayback,
            mDspStreamAttribute.audio_mode,
            &mDspStreamAttribute,
            &mDspStreamAttribute);
    }
#endif
}


#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
static void convertAttrFromHalToDsp(const struct stream_attribute_t *hal,
                                    struct stream_attribute_dsp *dsp) {
    if (!hal || !dsp) {
        return;
    }

    dsp->audio_format =  hal->audio_format;
    dsp->audio_offload_format = hal->audio_offload_format;
    dsp->audio_channel_mask = hal->audio_channel_mask;
    dsp->mAudioOutputFlags = hal->mAudioOutputFlags;
    dsp->mAudioInputFlags = hal->mAudioInputFlags;
    dsp->output_devices = hal->output_devices;
    dsp->input_device = hal->input_device;
    dsp->input_source = hal->input_source;
    dsp->num_channels = hal->num_channels;
    dsp->sample_rate = hal->sample_rate;
    dsp->acoustics_mask = hal->acoustics_mask;
    dsp->latency_ms = hal->latency;
}


void AudioDspStreamManager::CreateAurisysLibManager(
    struct aurisys_lib_manager_t **manager,
    struct aurisys_dsp_config_t **config,
    const uint8_t task_scene,
    const uint32_t aurisys_scenario,
    const uint8_t arsi_process_type,
    const uint32_t audio_mode,
    const struct stream_attribute_t *attribute_in,
    const struct stream_attribute_t *attribute_out,
    const struct stream_attribute_t *attribute_ref,
    const struct aurisys_gain_config_t *gain_config) {
    uint8_t *configAndParam = NULL;

    struct aurisys_lib_manager_t *newManager = NULL;
    struct aurisys_dsp_config_t *newConfig = NULL;

    struct aurisys_lib_manager_config_t *pManagerConfig = NULL;
    struct arsi_task_config_t *pTaskConfig = NULL;

    const char *custom_scene = NULL;
    struct ipi_msg_t msg;
    int retval = 0;

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    mOffloadPaused = false;
#endif

    struct data_buf_t paramList;

    paramList.data_size = 0;
    paramList.memory_size = 0;
    paramList.p_buffer = NULL;


    if (!manager) {
        WARNING("manager NULL!!");
        return;
    }
    if (!config) {
        WARNING("config NULL!!");
        return;
    }
    if ((*manager) != NULL || (*config) != NULL) {
        WARNING("already init!!");
        return;
    }

    AUDIO_ALLOC_STRUCT(struct aurisys_dsp_config_t, newConfig);
    if (newConfig == NULL) {
        ALOGE("%s(), newConfig is NULL!!", __FUNCTION__);
        return;
    }
    newConfig->guard_head = AURISYS_GUARD_HEAD_VALUE;
    newConfig->guard_tail = AURISYS_GUARD_TAIL_VALUE;

    /* manager config */
    pManagerConfig = &newConfig->manager_config;

    pManagerConfig->aurisys_scenario = aurisys_scenario;
    pManagerConfig->arsi_process_type = arsi_process_type;
    pManagerConfig->audio_format = attribute_in->audio_format;
    pManagerConfig->sample_rate = attribute_in->sample_rate;
    if (aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP) {
        //Voip can do 16k processinbg
        pManagerConfig->sample_rate = attribute_out->sample_rate;
    }
    pManagerConfig->frame_size_ms = 20;
    pManagerConfig->num_channels_ul = attribute_in->num_channels;
    pManagerConfig->num_channels_dl = attribute_in->num_channels;
    pManagerConfig->core_type = AURISYS_CORE_HIFI3;
    pManagerConfig->dsp_task_scene = task_scene;

    if (AudioALSAHardwareResourceManager::getInstance()->is32BitI2sSupport()) {
        pManagerConfig->iv_format = AUDIO_FORMAT_PCM_32_BIT;
    } else {
        pManagerConfig->iv_format = AUDIO_FORMAT_PCM_8_24_BIT;
    }

    pTaskConfig = &pManagerConfig->task_config;

    /* task config */
    SetArsiTaskConfig(
        pManagerConfig,
        task_scene,
        aurisys_scenario,
        audio_mode,
        attribute_in,
        attribute_out);

    /* attribute */
    if (arsi_process_type == ARSI_PROCESS_TYPE_DL_ONLY) {
        convertAttrFromHalToDsp(attribute_in, &newConfig->attribute[DATA_BUF_DOWNLINK_IN]);
        convertAttrFromHalToDsp(attribute_out, &newConfig->attribute[DATA_BUF_DOWNLINK_OUT]);
    }
    if (AudioSmartPaController::getInstance()->isSwDspSpkProtect(pTaskConfig->output_device_info.devices) &&
        AudioSmartPaController::getInstance()->isSmartPAUsed()) {
        convertAttrFromHalToDsp(attribute_out, &newConfig->attribute[DATA_BUF_IV_BUFFER]);
        newConfig->attribute[DATA_BUF_IV_BUFFER].audio_format = pManagerConfig->iv_format;
        newConfig->iv_on = true;
        ALOGV("%s() iv is on. ", __FUNCTION__);
    } else {
        newConfig->iv_on = false;
    }
    if (arsi_process_type == ARSI_PROCESS_TYPE_UL_ONLY) {
        convertAttrFromHalToDsp(attribute_in, &newConfig->attribute[DATA_BUF_UPLINK_IN]);
        convertAttrFromHalToDsp(attribute_out, &newConfig->attribute[DATA_BUF_UPLINK_OUT]);
        if (attribute_ref != NULL) {
            convertAttrFromHalToDsp(attribute_ref, &newConfig->attribute[DATA_BUF_ECHO_REF]);
            newConfig->aec_on = true;
        } else {
            newConfig->aec_on = false;
        }
    }

    /* custom info */
    AppOps *appOps = appOpsGetInstance();
    if (appOps && appOps->appHandleIsFeatureOptionEnabled(appOps->appHandleGetInstance(), "VIR_SCENE_CUSTOMIZATION_SUPPORT")) {
        custom_scene = AudioALSAStreamManager::getInstance()->getCustScene().string();
        if (aurisys_scenario == AURISYS_SCENARIO_DSP_RECORD) {
            if (!strcmp(custom_scene, "rec_interview") || !strcmp(custom_scene, "rec_voice")) {
                pManagerConfig->num_channels_ul = 1;
                ALOGD("%s(), custom_scene = %s, ul ch = %d\n",
                      __FUNCTION__, custom_scene, pManagerConfig->num_channels_ul);
            }
        } else if (aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP) {
            if (!strcmp(custom_scene, "app_gaming_16k")) {
                pManagerConfig->sample_rate = 16000;
                ALOGD("%s(), custom_scene = %s, force using 16k sample rate", __FUNCTION__, custom_scene);
            } else if (!strcmp(custom_scene, "VxWifi_NB") || !strcmp(custom_scene, "3GVT")) {
                pManagerConfig->sample_rate = 8000;
                ALOGD("%s(), custom_scene = %s, force using 8k sample rate", __FUNCTION__, custom_scene);
            } else if (!strcmp(custom_scene, "VxWifi_WB") || !strcmp(custom_scene, "Default")) {
                pManagerConfig->sample_rate = 16000;
                ALOGD("%s(), custom_scene = %s, force using 16k sample rate", __FUNCTION__, custom_scene);
            } else if (!strcmp(custom_scene, "VxWifi_SWB")) {
                pManagerConfig->sample_rate = 32000;
                ALOGD("%s(), custom_scene = %s, force using 32k sample rate", __FUNCTION__, custom_scene);
            }
        }
    }

    /* Setup custom info */
    setupCustomInfoStr(pManagerConfig->custom_info, MAX_CUSTOM_INFO_LEN, custom_scene,
        aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP ? AudioALSAStreamManager::getInstance()->getVoiceVolumeIndex() : -1,
        audio_is_bluetooth_sco_device((audio_devices_t)(pManagerConfig->task_config.output_device_info.devices)) ? AudioALSAStreamManager::getInstance()->GetBtCodec() : -1);

    ALOGD("%s(), custom_info = %s, voip = %d", __FUNCTION__, pManagerConfig->custom_info, aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP);

    AL_LOCK(mAurisysLibManagerLock);
    /* create manager */
    newManager = create_aurisys_lib_manager(pManagerConfig);

    /* parsing param */
    aurisys_parsing_param_file(newManager);

    /* parsing param */
    paramList.data_size = 0;
    paramList.memory_size = 0x48000; /* TODO: refine it */
    AUDIO_ALLOC_BUFFER(paramList.p_buffer, paramList.memory_size);
    do {
        retval = aurisys_get_param_list(newManager, &paramList);
        if (retval == 0) {
            break;
        }
        ALOGE("%s(), paramList.memory_size %u not enough!!",
              __FUNCTION__, paramList.memory_size);
        AUD_WARNING("mem not enough!!");

        AUDIO_FREE_POINTER(paramList.p_buffer);
        paramList.data_size = 0;
        paramList.memory_size *= 2;
        AUDIO_ALLOC_BUFFER(paramList.p_buffer, paramList.memory_size);
    } while (paramList.p_buffer != NULL);

    if (!paramList.p_buffer) {
        AUDIO_FREE_POINTER(newConfig);
        ALOGE("%s(), paramList.p_buffer is NULL!!", __FUNCTION__);
        AL_UNLOCK(mAurisysLibManagerLock);
        return;
    }

    /* set UL digital gain */
    if (gain_config != NULL) {
        newConfig->gain_config = *gain_config;
    }
    /* send config */
    configAndParam = (uint8_t *)AUDIO_MALLOC(sizeof(struct aurisys_dsp_config_t) + paramList.data_size);
    if (!configAndParam) {
        AUDIO_FREE_POINTER(newConfig);
        AUDIO_FREE_POINTER(paramList.p_buffer);
        ALOGE("%s(), configAndParam is NULL!!", __FUNCTION__);
        AL_UNLOCK(mAurisysLibManagerLock);
        return;
    }
    memcpy(configAndParam, newConfig, sizeof(struct aurisys_dsp_config_t));
    memcpy(configAndParam + sizeof(struct aurisys_dsp_config_t), paramList.p_buffer, paramList.data_size);

    retval = mAudioMessengerIPI->sendIpiMsg(
                 &msg,
                 task_scene, AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_DMA, AUDIO_IPI_MSG_NEED_ACK,
                 AUDIO_DSP_TASK_AURISYS_INIT,
                 sizeof(struct aurisys_dsp_config_t) + paramList.data_size,
                 0,
                 configAndParam);
    if (retval != 0) {
        ALOGE("%s(), fail!! retval = %d", __FUNCTION__, retval);
    }
    AUDIO_FREE_POINTER(configAndParam);
    AUDIO_FREE_POINTER(paramList.p_buffer);

    /* update ptr */
    *manager = newManager;
    *config = newConfig;
    AL_UNLOCK(mAurisysLibManagerLock);
}


void AudioDspStreamManager::UpdateAurisysConfig(
    struct aurisys_lib_manager_t *pAurisysLibManager,
    struct aurisys_dsp_config_t *pAurisysDspConfig,
    const uint32_t audio_mode,
    const struct stream_attribute_t *attribute_in,
    const struct stream_attribute_t *attribute_out) {
    struct aurisys_lib_manager_config_t *pManagerConfig = NULL;

    struct data_buf_t paramList;

    struct ipi_msg_t msg;
    int retval = 0;

    paramList.data_size = 0;
    paramList.memory_size = 0;
    paramList.p_buffer = NULL;

    AL_LOCK(mAurisysLibManagerLock);
    if (pAurisysLibManager == NULL || pAurisysDspConfig == NULL) {
        ALOGE("%s(), not init!!", __FUNCTION__);
        AL_UNLOCK(mAurisysLibManagerLock);
        return;
    }

    pManagerConfig = &pAurisysDspConfig->manager_config;

    /* task config */
    SetArsiTaskConfig(
        pManagerConfig,
        pManagerConfig->dsp_task_scene,
        pManagerConfig->aurisys_scenario,
        audio_mode,
        attribute_in,
        attribute_out);

    /* parsing param */
    aurisys_parsing_param_file(pAurisysLibManager);

    paramList.data_size = 0;
    paramList.memory_size = 0x48000; /* TODO: refine it */
    AUDIO_ALLOC_BUFFER(paramList.p_buffer, paramList.memory_size);
    do {
        retval = aurisys_get_param_list(pAurisysLibManager, &paramList);
        if (retval == 0) {
            break;
        }
        ALOGE("%s(), paramList.memory_size %u not enough!!",
              __FUNCTION__, paramList.memory_size);

        AUDIO_FREE_POINTER(paramList.p_buffer);
        paramList.data_size = 0;
        paramList.memory_size *= 2;
        AUDIO_ALLOC_BUFFER(paramList.p_buffer, paramList.memory_size);
    } while (paramList.p_buffer != NULL);

    if (!paramList.p_buffer) {
        ALOGE("%s(), paramList.p_buffer is NULL!!", __FUNCTION__);
        AL_UNLOCK(mAurisysLibManagerLock);
        return;
    }

    retval = mAudioMessengerIPI->sendIpiMsg(
             &msg,
             pManagerConfig->dsp_task_scene, AUDIO_IPI_LAYER_TO_DSP,
             AUDIO_IPI_DMA, AUDIO_IPI_MSG_NEED_ACK,
             AUDIO_DSP_TASK_AURISYS_PARAM_LIST,
             paramList.data_size,
             0,
             paramList.p_buffer);

    if (retval != 0) {
        ALOGE("%s(), fail!! retval = %d", __FUNCTION__, retval);
    }
    AUDIO_FREE_POINTER(paramList.p_buffer);
    AL_UNLOCK(mAurisysLibManagerLock);
}


void AudioDspStreamManager::SetArsiTaskConfig(
    struct aurisys_lib_manager_config_t *pManagerConfig,
    const uint8_t task_scene,
    const uint32_t aurisys_scenario,
    const uint32_t audio_mode,
    const struct stream_attribute_t *attribute_in,
    const struct stream_attribute_t *attribute_out) {
    struct arsi_task_config_t *pTaskConfig = &pManagerConfig->task_config;

    pTaskConfig->input_device_info.devices = attribute_in->input_device;
    pTaskConfig->input_device_info.audio_format = attribute_in->audio_format;
    pTaskConfig->input_device_info.sample_rate = attribute_in->sample_rate;
    pTaskConfig->input_device_info.channel_mask = attribute_in->audio_channel_mask;
    pTaskConfig->input_device_info.num_channels = attribute_in->num_channels;
    pTaskConfig->input_device_info.hw_info_mask = 0;

    pTaskConfig->output_device_info.devices = attribute_in->output_devices;
    pTaskConfig->output_device_info.audio_format = attribute_in->audio_format;
    pTaskConfig->output_device_info.sample_rate = attribute_in->sample_rate;
    pTaskConfig->output_device_info.channel_mask = attribute_in->audio_channel_mask;
    pTaskConfig->output_device_info.num_channels = attribute_in->num_channels;
    if (AudioSmartPaController::getInstance()->isSmartPAUsed()) {
        pTaskConfig->output_device_info.hw_info_mask |= OUTPUT_DEVICE_HW_INFO_SMARTPA_SPEAKER;
    }

    if (AudioALSAHardwareResourceManager::getInstance()->getSpkNum() == 2) {
        pTaskConfig->output_device_info.hw_info_mask |= OUTPUT_DEVICE_HW_INFO_STEREO_SPEAKER;
    }

    pTaskConfig->task_scene = map_aurisys_scenario_to_task_scene(
                                  pManagerConfig->core_type,
                                  pManagerConfig->aurisys_scenario);

    /* TODO: decouple */
    if (pTaskConfig->task_scene != task_scene) {
        ALOGW("%s(), %d != %d", __FUNCTION__, task_scene, pTaskConfig->task_scene);
    }

    pTaskConfig->audio_mode = audio_mode;
    pTaskConfig->stream_type = attribute_in->stream_type;

    pTaskConfig->max_input_device_sample_rate  = 48000; /* TODO */
    pTaskConfig->max_output_device_sample_rate = 48000; /* TODO */
    pTaskConfig->max_input_device_num_channels  = 3; /* TODO */
    pTaskConfig->max_output_device_num_channels = 2; /* TODO */

    pTaskConfig->output_flags = attribute_in->mAudioOutputFlags;
    pTaskConfig->input_source = attribute_in->input_source; /* TODO: UL*/
    pTaskConfig->input_flags  = attribute_in->mAudioInputFlags; /* TODO: UL*/

    if (pTaskConfig->output_device_info.devices == AUDIO_DEVICE_OUT_EARPIECE &&
        SpeechEnhancementController::GetInstance()->GetHACOn()) {
        pTaskConfig->enhancement_feature_mask |= ENHANCEMENT_FEATURE_EARPIECE_HAC;
    }

    if ((pTaskConfig->input_device_info.devices & AUDIO_DEVICE_IN_ALL_SCO)
        && (pTaskConfig->output_device_info.devices & AUDIO_DEVICE_OUT_ALL_SCO)
        && SpeechEnhancementController::GetInstance()->GetBtHeadsetNrecOn()) {
        pTaskConfig->enhancement_feature_mask |= ENHANCEMENT_FEATURE_BT_NREC;
    }

    if ((aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP)
        && (attribute_out->NativePreprocess_Info.PreProcessEffect_AECOn == true)) {
        pTaskConfig->enhancement_feature_mask |= ENHANCEMENT_FEATURE_EC;
    }

    if ((aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP)
        && (attribute_out->NativePreprocess_Info.PreProcessEffect_NSOn == true)) {
        pTaskConfig->enhancement_feature_mask |= ENHANCEMENT_FEATURE_NS;
    }

    if ((aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP)
        && (attribute_out->NativePreprocess_Info.PreProcessEffect_AGCOn == true)) {
        pTaskConfig->enhancement_feature_mask |= ENHANCEMENT_FEATURE_AGC;
    }

    dump_task_config(pTaskConfig);
}


int AudioDspStreamManager::doRecoveryState(){
    int value = 0;

    value = getDspRuntimeEn(TASK_SCENE_AUDPLAYBACK);
    if (value == true) {
         setAfeDspShareMem(false);
         mAudioMessengerIPI->deregisterAdspFeature(AUDIO_PLAYBACK_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_AUDPLAYBACK, false);
         setDspRefRuntimeEn(TASK_SCENE_AUDPLAYBACK, false);
         ALOGD("%s() AUDIO_PLAYBACK_FEATURE_ID", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_DEEPBUFFER);
    if (value == true) {
         setAfeOutDspShareMem(AUDIO_OUTPUT_FLAG_DEEP_BUFFER, false);
         mAudioMessengerIPI->deregisterAdspFeature(getDspFeatureID(AUDIO_OUTPUT_FLAG_DEEP_BUFFER));
         setDspRuntimeEn(TASK_SCENE_DEEPBUFFER, false);
         ALOGD("%s() AUDIO_OUTPUT_FLAG_DEEP_BUFFER", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_VOIP);
    if (value == true) {
         setAfeOutDspShareMem(AUDIO_OUTPUT_FLAG_VOIP_RX, false);
         mAudioMessengerIPI->deregisterAdspFeature(getDspFeatureID(AUDIO_OUTPUT_FLAG_VOIP_RX));
         setDspRuntimeEn(TASK_SCENE_VOIP, false);
         ALOGD("%s() AUDIO_OUTPUT_FLAG_VOIP_RX", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_PRIMARY);
    if (value == true) {
         setAfeOutDspShareMem(AUDIO_OUTPUT_FLAG_PRIMARY, false);
         mAudioMessengerIPI->deregisterAdspFeature(getDspFeatureID(AUDIO_OUTPUT_FLAG_PRIMARY));
         setDspRuntimeEn(TASK_SCENE_PRIMARY, false);
         ALOGD("%s() AUDIO_OUTPUT_FLAG_PRIMARY", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_PLAYBACK_MP3);
    if (value == true) {
         setAfeOutDspShareMem(AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD, false);
         mAudioMessengerIPI->deregisterAdspFeature(getDspFeatureID(AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD));
         setDspRuntimeEn(TASK_SCENE_PLAYBACK_MP3, false);
         ALOGD("%s() AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_CAPTURE_UL1);
    if (value == true) {
         setAfeInDspShareMem(false, true);
         mAudioMessengerIPI->deregisterAdspFeature(CAPTURE_UL1_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_CAPTURE_UL1, false);
         setDspRefRuntimeEn(TASK_SCENE_CAPTURE_UL1, false);
         ALOGD("%s() CAPTURE_UL1_FEATURE_ID", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_CAPTURE_RAW);
    if (value == true) {
         setAfeInDspShareMem(false, false);
         mAudioMessengerIPI->deregisterAdspFeature(CAPTURE_RAW_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_CAPTURE_RAW, false);
         ALOGD("%s() CAPTURE_RAW_FEATURE_ID", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_A2DP);
    if (value == true) {
         setA2dpDspShareMem(false);
         mAudioMessengerIPI->deregisterAdspFeature(A2DP_PLAYBACK_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_A2DP, false);
         ALOGD("%s() A2DP_PLAYBACK_FEATURE_ID", __FUNCTION__);
    }
    value = getDspRuntimeEn(TASK_SCENE_MUSIC);
    if (value == true) {
         mAudioMessengerIPI->deregisterAdspFeature(AUDIO_MUSIC_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_MUSIC, false);
         ALOGD("%s() TASK_SCENE_MUSIC", __FUNCTION__);
    }
    value = getDspRuntimeEn(TASK_SCENE_FAST);
    if (value == true) {
        bool inputFlag = appIsFeatureOptionEnabled("MTK_AUDIO_FAST_RAW_SUPPORT") ?
                         AUDIO_OUTPUT_FLAG_RAW : AUDIO_OUTPUT_FLAG_FAST;
         setAfeOutDspShareMem(inputFlag, false);
         mAudioMessengerIPI->deregisterAdspFeature(FAST_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_FAST, false);
         ALOGD("%s() TASK_SCENE_FAST", __FUNCTION__);
    }

    AudioALSAHandlerKtv::getInstance()->recoveryKtvStatus();
    value = getDspRuntimeEn(TASK_SCENE_KTV);
    if (value == true) {
         AudioALSAHandlerKtv::getInstance()->setktvDspShareMem(false);
         mAudioMessengerIPI->deregisterAdspFeature(KTV_FEATURE_ID);
         setDspRuntimeEn(TASK_SCENE_KTV, false);
         ALOGD("%s() KTV_FEATURE_ID", __FUNCTION__);
    }

    value = getDspRuntimeEn(TASK_SCENE_CALL_FINAL);
    if (value == true) {
        AudioDspCallFinal::getInstance()->setCallFinalDspShareMem(false);
        mAudioMessengerIPI->deregisterAdspFeature(CALL_FINAL_FEATURE_ID);
        setDspRuntimeEn(TASK_SCENE_CALL_FINAL, false);
        ALOGD("%s() CALL_FINAL_FEATURE_ID", __FUNCTION__);
    }

    return 0;
}

void AudioDspStreamManager::DestroyAurisysLibManager(
    struct aurisys_lib_manager_t **manager,
    struct aurisys_dsp_config_t **config,
    const uint8_t task_scene) {
    struct ipi_msg_t ipi_msg;
    int retval = 0;

    if (!manager) {
        WARNING("manager NULL!!");
        return;
    }
    if (!config) {
        WARNING("config NULL!!");
        return;
    }
    if ((*manager) == NULL || (*config) == NULL) {
        return;
    }

    AL_AUTOLOCK(mAurisysLibManagerLock);
    retval = mAudioMessengerIPI->sendIpiMsg(
                 &ipi_msg,
                 task_scene,
                 AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_MSG_ONLY,
                 AUDIO_IPI_MSG_NEED_ACK,
                 AUDIO_DSP_TASK_AURISYS_DEINIT,
                 0,
                 0,
                 NULL);
    if (retval != 0) {
        ALOGE("%s(), fail!! retval = %d", __FUNCTION__, retval);
    }

    destroy_aurisys_lib_manager(*manager);
    *manager = NULL;

    AUDIO_FREE_POINTER(*config);
    *config = NULL;
}
#endif /* end of MTK_AURISYS_FRAMEWORK_SUPPORT */

void AudioDspStreamManager::openPCMDumpA2DP(AudioALSAPlaybackHandlerBase *playbackHandler) {
    /* dump data for 0x10000 64k */

    mAudioMessengerIPI->registerDmaCbk(TASK_SCENE_A2DP,
                                       0x10000,0x10000,
                                       AudioALSAPlaybackHandlerBase::processDmaMsgWrapper, playbackHandler);
    playbackHandler->OpenPCMDumpDSP(LOG_TAG, TASK_SCENE_A2DP);
    ALOGD("-%s(),", __FUNCTION__);
}

void AudioDspStreamManager::closePCMDumpA2DP(AudioALSAPlaybackHandlerBase *playbackHandler) {
    mAudioMessengerIPI->deregisterDmaCbk(TASK_SCENE_A2DP);
    playbackHandler->ClosePCMDumpDSP(TASK_SCENE_A2DP);
    ALOGD("-%s(),", __FUNCTION__);
}

int AudioDspStreamManager::parseParameter(char* strbuf, int *taskScene, int* param)
{
    char *temp = NULL;
    ALOGD("%s strbuf %s", __FUNCTION__, strbuf);

    temp = strtok (strbuf, ",");
    if (!temp)
        return INVALID_OPERATION;
    /* parse task scene */
    temp = strtok (NULL, ",");
    if (!temp)
        return INVALID_OPERATION;
    *taskScene = atoi(temp);
    ALOGD("%s temp %s task_scene %d", __FUNCTION__, temp, *taskScene);

    /* parse task param */
    temp = strtok (NULL, ",");
    if (!temp)
        return INVALID_OPERATION;
    *param = atoi(temp);

    ALOGD("%s temp %s task_scene %d param %d", __FUNCTION__, temp, *taskScene, *param);
    return NO_ERROR;
}

int AudioDspStreamManager::startMusicTask(AudioALSAPlaybackHandlerBase *playbackHandler) {
    stream_attribute_t attributeMusic;

    if (mPlaybackHandlerVector.size() <= 0) {
        return DSP_STREAM_NOSTREAM;
    }

    if (getDspPlaybackEnable() == false) {
        return DSP_STREAM_NOTSUPPORT;
    }

    if (!playbackHandler) {
        WARNING("playbackHandler NULL");
        return DSP_STREAM_NOSTREAM;
    }

    AudioALSAPlaybackHandlerBase *Base = playbackHandler;

    if (Base == NULL || !dataPasstoMusicTask(Base)) {
        WARNING("No music playback handler!!");
    }

    setDspRuntimeEn(TASK_SCENE_MUSIC, true);
    mAudioMessengerIPI->registerAdspFeature(AUDIO_MUSIC_FEATURE_ID);

    mAudioMessengerIPI->registerDmaCbk(
        TASK_SCENE_MUSIC,
        0x10000,
        0x10000,
        AudioALSAPlaybackHandlerBase::processDmaMsgWrapper,
        playbackHandler);
    playbackHandler->OpenPCMDumpDSP(LOG_TAG, TASK_SCENE_MUSIC);

    memcpy(&attributeMusic, Base->getStreamAttributeTarget(), sizeof(stream_attribute_t));

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    CreateAurisysLibManager(
        &mAurisysLibManagerMusic,
        &mAurisysDspConfigMusic,
        TASK_SCENE_MUSIC,
        AURISYS_SCENARIO_DSP_MUSIC,
        ARSI_PROCESS_TYPE_DL_ONLY,
        AUDIO_MODE_NORMAL,
        &attributeMusic,
        &attributeMusic,
        NULL,
        NULL);
#endif

    return 0;
}

int AudioDspStreamManager::stopMusicTask(AudioALSAPlaybackHandlerBase *playbackHandler) {
    if (getDspPlaybackEnable() == false) {
        return DSP_STREAM_NOTSUPPORT;
    }

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    DestroyAurisysLibManager(&mAurisysLibManagerMusic, &mAurisysDspConfigMusic, TASK_SCENE_MUSIC);
#endif

    playbackHandler->ClosePCMDumpDSP(TASK_SCENE_MUSIC);
    mAudioMessengerIPI->deregisterDmaCbk(TASK_SCENE_MUSIC);
    mAudioMessengerIPI->deregisterAdspFeature(AUDIO_MUSIC_FEATURE_ID);
    setDspRuntimeEn(TASK_SCENE_MUSIC, false);
    return 0;
}

bool AudioDspStreamManager::isAdspPlayback(const int mAudioOutputFlags,
                                           const audio_devices_t outputDevices) {
    return getDspPlaybackEnable() && dataPasstoDspAfe(mAudioOutputFlags, outputDevices);
}

#ifdef MTK_A2DP_OFFLOAD_SUPPORT
int AudioDspStreamManager::openA2dpTask(AudioALSAPlaybackHandlerBase *playbackHandler) {
    if (mPlaybackHandlerVector.size() <= 0) {
        return DSP_STREAM_NOSTREAM;
    }

    /* get first handlerbase*/
    AudioALSAPlaybackHandlerBase *Base = mPlaybackHandlerVector.valueAt(0);
    const stream_attribute_t *attribute = Base->getStreamAttributeTarget();

    // confition with a2dp is not in suspend and btif is not NULL
    if (mA2dpSuspend == false && mBluetoothAudioOffloadHostIf != NULL) {
        ALOGD("%s() mA2dpStatus = BTAUDIO_STANDBY", __func__);
        mA2dpStatus = BTAUDIO_STANDBY;
    }

    setDspRuntimeEn(TASK_SCENE_A2DP, true);
    adsp_register_feature(A2DP_PLAYBACK_FEATURE_ID);

    const stream_attribute_t *pAttributeul = Base->getStreamAttributeTarget();

    mDspA2dpConfig.rate = pAttributeul->sample_rate;
    mDspA2dpConfig.channels = pAttributeul->num_channels;
    mDspA2dpConfig.format = PCM_FORMAT_S24_LE;
    mDspA2dpConfig.period_count = A2DPPLAYBACK_DL_TASK_PEROID_COUNT;
    mDspA2dpConfig.period_size = A2DPPLAYBACK_DL_SAMPLES / A2DPPLAYBACK_DL_TASK_PEROID_COUNT;
    mDspA2dpConfig.start_threshold = 0;
    mDspA2dpConfig.stop_threshold = ~(0U);
    mDspA2dpConfig.silence_threshold = 0;

    /* get card index and pcm index*/
    mStreamCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlaybackDspA2DP);
    mDspA2DPIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDspA2DP);
    ALOGD("%s(), mStreamCardIndex: %d, mDspA2DPIndex = %d",
          __FUNCTION__, mStreamCardIndex, mDspA2DPIndex);

    if(mA2dpPcmDump == A2DP_STREAM_DUMP_OPEN) {
        openPCMDumpA2DP(playbackHandler);
    }

    ASSERT(mDspA2dpPcm == NULL);
    mDspA2dpPcm = pcm_open(mStreamCardIndex,
                       mDspA2DPIndex, PCM_OUT | PCM_MONOTONIC, &mDspA2dpConfig);
    ALOGD("%s(), mDspA2dpPcm = %p channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d",
          __FUNCTION__, mDspA2dpPcm, mDspA2dpConfig.channels, mDspA2dpConfig.rate, mDspA2dpConfig.period_size,
          mDspA2dpConfig.period_count, mDspA2dpConfig.format);

    if (mDspA2dpPcm == NULL) {
        ALOGD("%s pcm_open error = %s", __func__, pcm_get_error(mDspA2dpPcm));
        ASSERT(mDspA2dpPcm != NULL);
    }

    ALOGD("%s() pcm_prepare", __FUNCTION__);
    if (pcm_prepare(mDspA2dpPcm) != 0) {
        ALOGE("%s(), pcm_prepare(%p) fail due to %s", __FUNCTION__, mDspA2dpPcm, pcm_get_error(mDspA2dpPcm));
    }

    ALOGD("-%s(), ", __FUNCTION__);
    return NO_ERROR;
}

int AudioDspStreamManager::closeA2dpTask(AudioALSAPlaybackHandlerBase *playbackHandler) {
    int retval;

    if (mDspA2dpPcm != NULL) {
        if (pcm_stop(mDspA2dpPcm) != 0) {
            ALOGE("%s(), pcm_stop(%p) fail due to %s", __FUNCTION__, mDspA2dpPcm, pcm_get_error(mDspA2dpPcm));
            triggerDsp(TASK_SCENE_A2DP, AUDIO_DSP_TASK_STOP);
        }
        ALOGD("+%s() pcm_close ", __FUNCTION__);
        pcm_close(mDspA2dpPcm);
        mDspA2dpPcm = NULL;
    }

    setDspRuntimeEn(TASK_SCENE_A2DP, false);
    adsp_deregister_feature(A2DP_PLAYBACK_FEATURE_ID);
    mA2dpStatus = BTAUDIO_DISABLED;
    if(mA2dpPcmDump == A2DP_STREAM_DUMP_OPEN) {
        closePCMDumpA2DP(playbackHandler);
    }
    ALOGD("-%s(), ", __FUNCTION__);

    return 0;
}

void AudioDspStreamManager::setBluetoothAudioOffloadParam(const sp<IBluetoothAudioPort> &hostIf,
                                                           const AudioConfiguration &audioConfig,
                                                           bool on)
{
    ALOGD("+%s()", __FUNCTION__);
    AL_LOCK(mTaskLock);
    AL_LOCK(mA2dpStreamLock);
    AL_LOCK(mA2dpSessionLock); //hold for update meta data void access
    if (on == true) {
        mBluetoothAudioOffloadHostIf = hostIf;
        ALOGD("&audioConfigr=%p", (void *)&audioConfig);
        if (audioConfig.getDiscriminator() !=
            AudioConfiguration::hidl_discriminator::codecConfig) {
            ALOGW(" %s() Invalid safe_union AudioConfiguration!!!",__func__);
            AL_UNLOCK(mA2dpSessionLock);
            AL_UNLOCK(mA2dpStreamLock);
            AL_UNLOCK(mTaskLock);
            return;
        }
        const CodecConfiguration codec_config= audioConfig.codecConfig();
        if (a2dp_codecinfo.codec_type != (uint32_t)codec_config.codecType) {
            ALOGW("%s() codec change!",__func__);
            setA2dpReconfig(true);
        }
        a2dp_codecinfo.codec_type = (uint32_t)codec_config.codecType;
        a2dp_codecinfo.encoded_audio_bitrate = codec_config.encodedAudioBitrate;
        a2dp_codecinfo.mtu = codec_config.peerMtu;
        if (a2dp_codecinfo.codec_type == 0x1) { // sbc
            if (codec_config.config.getDiscriminator() !=
                CodecConfiguration::CodecSpecific::hidl_discriminator::sbcConfig) {
                ALOGW(" %s() Invalid safe_union sbcConfig!!!",__func__);
                AL_UNLOCK(mA2dpSessionLock);
                AL_UNLOCK(mA2dpStreamLock);
                AL_UNLOCK(mTaskLock);
                return;
            }

            const SbcParameters sbc_config= codec_config.config.sbcConfig();
            a2dp_codecinfo.sample_rate = (uint32_t)sbc_config.sampleRate;
            a2dp_codecinfo.ch_mode = (uint8_t)sbc_config.channelMode; // 1: JOINT_STEREO, 8: MONO. In IsOffloadSbcConfigurationValid(), only 1 or 8 are valid.
            a2dp_codecinfo.bits_per_sample = (uint8_t)sbc_config.bitsPerSample;
            a2dp_codecinfo.codec_info[0] = ((uint8_t)sbc_config.blockLength & 0xf0) | ((uint8_t)sbc_config.numSubbands & 0x0c) | ((uint8_t)sbc_config.allocMethod & 0x03);
            a2dp_codecinfo.codec_info[1] = sbc_config.minBitpool;
            a2dp_codecinfo.codec_info[2] = sbc_config.maxBitpool;
        } else if (a2dp_codecinfo.codec_type == 0x2) { // aac
            if (codec_config.config.getDiscriminator() !=
                CodecConfiguration::CodecSpecific::hidl_discriminator::aacConfig) {
                ALOGW(" %s() Invalid safe_union aacConfig!!!",__func__);
                return;
            }
            const AacParameters aac_config= codec_config.config.aacConfig();
            a2dp_codecinfo.sample_rate = (uint32_t)aac_config.sampleRate;
            a2dp_codecinfo.ch_mode = (uint8_t)aac_config.channelMode;
            a2dp_codecinfo.bits_per_sample = (uint8_t)aac_config.bitsPerSample;
            a2dp_codecinfo.codec_info[0] = (uint8_t)aac_config.objectType;
            a2dp_codecinfo.codec_info[1] = (uint8_t)aac_config.variableBitRateEnabled;
        }

        mBluetoothAudioOffloadSession++;
        ALOGD("codecType=%u, sampleRate=%u, bitsPerSample=%u, channelMode=%u encodedAudioBitrate=%u peerMtu=%u",
              (uint32_t)a2dp_codecinfo.codec_type,
              (uint32_t)a2dp_codecinfo.sample_rate,
              (unsigned char)a2dp_codecinfo.bits_per_sample,
              (unsigned char)a2dp_codecinfo.ch_mode,
              (uint32_t)a2dp_codecinfo.encoded_audio_bitrate,
              (uint16_t)a2dp_codecinfo.mtu);
        ALOGD("codecParameters=%u, minBitpool=%u, maxBitpool=%u",
              (unsigned char)a2dp_codecinfo.codec_info[0],
              (unsigned char)a2dp_codecinfo.codec_info[1],
              (unsigned char)a2dp_codecinfo.codec_info[2]);
        AL_LOCK(mA2dpStatusLock);
        ALOGW("%s() on mDspA2DPStreamState[%d] mA2dpStatus[%d]",
              __FUNCTION__, mDspA2DPStreamState, mA2dpStatus);
        if (mDspA2DPStreamState == DSP_STREAM_OPEN || mDspA2DPStreamState == DSP_STREAM_STOP
            || mDspA2DPStreamState == DSP_STREAM_START) {
            mA2dpStatus = BTAUDIO_STANDBY;
            AL_UNLOCK(mA2dpStatusLock);
            /* start a2dp task due to device is connect*/
            if (needStartA2dpTask(mOutputDevice)) {
                startA2dpTask_l();
                mDspA2DPStreamState = DSP_STREAM_START;
                ALOGD("%s() on mDspA2DPStreamState = %d", __FUNCTION__, mDspA2DPStreamState);
            }
        } else {
            mA2dpStatus = BTAUDIO_DISABLED;
            AL_UNLOCK(mA2dpStatusLock);
        }
    } else {
        mBluetoothAudioOffloadSession--;
        if (mBluetoothAudioOffloadSession < 0) {
            ALOGW("%s() off Error: mBluetoothAudioOffloadSession < 0, reset to 0!", __FUNCTION__);
            mBluetoothAudioOffloadSession = 0;
        }
        if (mBluetoothAudioOffloadSession == 0) {
            mBluetoothAudioOffloadHostIf = NULL;
            /* stop a2dp task due to device is disconnect*/
            if (needStopA2dpTask(mOutputDevice, true)) {
                stopA2dpTask_l();
                ALOGD("%s() off mDspA2DPStreamState = DSP_STREAM_STOP", __FUNCTION__);
            }
            AL_LOCK(mA2dpStatusLock);
            mA2dpStatus = BTAUDIO_DISABLED;
            setA2dpStreamStatusToDsp(false);
            AL_UNLOCK(mA2dpStatusLock);
        }
    }
    AL_UNLOCK(mA2dpSessionLock);
    AL_UNLOCK(mA2dpStreamLock);
    AL_UNLOCK(mTaskLock);
    ALOGD("-%s() mBluetoothAudioOffloadSession=%d", __FUNCTION__, mBluetoothAudioOffloadSession);
}

void *AudioDspStreamManager::getBluetoothAudioCodecInfo(void) {
    return (void *)&a2dp_codecinfo;
}

uint32_t AudioDspStreamManager::getBluetoothAudioCodecType(void) {
    if (mBluetoothAudioOffloadSession > 0) {
        ALOGD("%s(), codec_type = %d", __FUNCTION__, a2dp_codecinfo.codec_type);
        return a2dp_codecinfo.codec_type;
    } else {
        ALOGD("%s(), mBluetoothAudioOffloadSession = %d, set codec_type unknown", __FUNCTION__,
              mBluetoothAudioOffloadSession);
        return 0; // unknown
    }
}

int AudioDspStreamManager::WaitHostIfState(void)
{
    int ret = NO_ERROR;
    AL_LOCK_MS(mA2dpStatusLock, BTIF_ACK_TIMEOUT);
    ALOGD("+%s() mA2dpStatusAck = %d", __FUNCTION__, mA2dpStatusAck);
    /* bt not ack, wait for bt ack */
    if (mA2dpStatusAck <= 0){
        ret = AL_WAIT_MS(mA2dpStatusLock, BTIF_ACK_TIMEOUT);
        if (ret) {
            AL_UNLOCK(mA2dpStatusLock);
            ALOGW("%s() enter wait mA2dpStatusLock error mA2dpStatus = %d ret = %d",
                  __FUNCTION__, mA2dpStatus, ret);
            ret = INVALID_OPERATION;
        }
        mA2dpStatusAck--;
    } else {
        mA2dpStatusAck--;
    }
    AL_UNLOCK(mA2dpStatusLock);
    return ret;
}

/* setBThost may be call with */
int AudioDspStreamManager::setBtHostIfState(int state) {
    int ret = NO_ERROR;

    if ((mBluetoothAudioOffloadHostIf == NULL) || (mBluetoothAudioOffloadSession == 0)) {
         ALOGW("%s() mBluetoothAudioOffloadSession = 0x%x mA2dpStatus = %d",
               __FUNCTION__, mBluetoothAudioOffloadSession, mA2dpStatus);
         ret = INVALID_OPERATION;
         return ret;
    }
    ALOGD("+%s() mA2dpStatus %d state[%d]", __FUNCTION__, mA2dpStatus, state);
    ret = AL_LOCK_MS_NO_ASSERT(mA2dpStatusLock, BTIF_ACK_TIMEOUT);
    if (ret) {
        ALOGD("%s() AL_LOCK_MS mA2dpStatusLock fail ret = %d", __FUNCTION__, ret);
        return ret;
    }

    switch(state) {
    case BTIF_OFF: {
        // stop btif if status is started.
        if (mA2dpStatus == BTAUDIO_STARTED) {
            mA2dpStatus = BTAUDIO_SUSPENDING;;
            AL_UNLOCK(mA2dpStatusLock);
            setA2dpStreamStatusToDsp(false);
            mBluetoothAudioOffloadHostIf->suspendStream();
            WaitHostIfState();
            ALOGD("-%s() suspendStream mA2dpStatus %d", __FUNCTION__, mA2dpStatus);
        } else if (mA2dpStatus == BTAUDIO_STARTING || mA2dpStatus == BTAUDIO_SUSPENDING) {
            ALOGW("%s() NOT ready to be standby ret = %d", __FUNCTION__, ret);
            ret = -EBUSY;
            AL_UNLOCK(mA2dpStatusLock);
        } else {
            ALOGD("%s() BTIF_OFF mA2dpStatus = %d ret = %d", __FUNCTION__, mA2dpStatus, ret);
            AL_UNLOCK(mA2dpStatusLock);
        }
        break;
    }
    case BTIF_ON: {
        mA2dpStatus = BTAUDIO_STARTING;
        AL_UNLOCK(mA2dpStatusLock);
        mBluetoothAudioOffloadHostIf->startStream();
        WaitHostIfState();
        setA2dpStreamStatusToDsp(true);
        if (mA2dpStatus != BTAUDIO_STARTED)
            ret = INVALID_OPERATION;
        ALOGD("-%s() BTIF_ON startStream mA2dpStatus %d ret = %d", __FUNCTION__, mA2dpStatus, ret);
        break;
    }
    case BTIF_DISABLE:{
        if (mA2dpStatus != BTAUDIO_DISABLED) {
            mA2dpStatus = BTAUDIO_DISABLED;
            AL_UNLOCK(mA2dpStatusLock);
            setA2dpStreamStatusToDsp(false);
            mBluetoothAudioOffloadHostIf->suspendStream();
            WaitHostIfState();
        }
        ALOGD("-%s() BTIF_DISABLE mA2dpStatus %d", __FUNCTION__, mA2dpStatus);
        break;
    }
    default:
        ALOGD("%s() state[%d] 2dpStatus[%d]", __FUNCTION__, state, mA2dpStatus);
    }

    return ret;
}

void AudioDspStreamManager::setA2dpSuspend(bool on) {
    if (!isAdspOptionEnable()) {
        return;
    }

    int32_t outputDevice = (audio_devices_t)AudioALSAHardwareResourceManager::getInstance()->getOutputDevice();
    mA2dpSuspend = on;
    AL_AUTOLOCK(mA2dpStreamLock);
    ALOGD("+%s()outputDevice = 0x%x mA2dpStatus = %d mA2dpSuspend %d", __FUNCTION__, outputDevice, mA2dpStatus, mA2dpSuspend);

    if (on) {
        // suspend stream
        if (mA2dpStatus != BTAUDIO_DISABLED) {
            setBtHostIfState(BTIF_DISABLE);
        }
    } else {
        // change a2dp status
        AL_AUTOLOCK(mA2dpStatusLock);
        if (mA2dpStatus == BTAUDIO_DISABLED){
            mA2dpStatus = BTAUDIO_STANDBY;
        }
    }
    ALOGD("-%s(), bool %doutputDevice = 0x%x", __FUNCTION__, on, outputDevice);
}

/*
 * bt if will call setA2dpStatus and wtih state
 * may be the same thread with mBluetoothAudioOffloadHostIf->startStream()
 * may be different thread wuth mBluetoothAudioOffloadHostIf->startStream()
 */
void AudioDspStreamManager::setA2dpStatus(int status) {
    if (!isAdspOptionEnable()) {
        return;
    }

    // keep pre_status.
    AL_LOCK(mA2dpStatusLock);
    int pre_status = mA2dpStatus;
    ALOGD("+%s mA2dpStatus[%d] status[%d]", __func__, mA2dpStatus, status);

    mA2dpStatusAck++;
    ALOGD("%s mA2dpStatusAck %d", __func__, mA2dpStatusAck);

    switch (pre_status){
        case BTAUDIO_STARTING: {
            if (status == A2DP_START_SUCCESS) {
                mA2dpStatus = BTAUDIO_STARTED;
            } else {
                ALOGW("%s staring fail mA2dpStatus[%d] status[%d]", __func__, mA2dpStatus, status);
                mA2dpStatus = BTAUDIO_STANDBY;
            }
            break;
        }
        case BTAUDIO_SUSPENDING:{
            if (status == A2DP_SUSPEND_SUCCESS) {
                mA2dpStatus = BTAUDIO_STANDBY;
            } else {
                mA2dpStatus = BTAUDIO_DISABLED;
                ALOGW("%s suspend fail mA2dpStatus[%d] status[%d]", __func__, mA2dpStatus, status);
            }
            break;
        }
        case BTAUDIO_STARTED: {
            /* back to standby */
            if (status == A2DP_SUSPEND_SUCCESS) {
                mA2dpStatus = BTAUDIO_STANDBY;
            } else {
                ALOGW("%s BTAUDIO_STARTED mA2dpStatus[%d] status[%d]", __func__, mA2dpStatus, status);
            }
            break;
        }
        default:
            ALOGD("%s mA2dpStatus[%d]", __func__, mA2dpStatus);
    }
    AL_SIGNAL(mA2dpStatusLock);
    AL_UNLOCK(mA2dpStatusLock);
    ALOGD("-%s mA2dpStatus[%d] status[%d]", __func__, mA2dpStatus, status);

    return;
}

int AudioDspStreamManager::getA2dpStatus(void) {
    AL_AUTOLOCK(mA2dpStatusLock);

    if (isAdspOptionEnable()) {
        return mA2dpStatus;
    }

    return false;
}

sp<IBluetoothAudioPort> AudioDspStreamManager::getBluetoothAudioHostIf() {
    return mBluetoothAudioOffloadHostIf;
}

int AudioDspStreamManager::getBluetoothAudioSession() {
    return mBluetoothAudioOffloadSession;
}

/*
 * when playbackhandler with a2dp device output , need to call this function
 * because of a2dp start/stop may spent a long time, in this state transition state,
 * audio should wait for bt status ack to do next action.
 */
int AudioDspStreamManager::btOutWriteAction(audio_devices_t devices) {
    int ret = 0;
    if ((devices & AUDIO_DEVICE_OUT_ALL_A2DP) == 0)
        return 0;

    AL_AUTOLOCK(mA2dpStreamLock);
    /*
     * if a2dp status is stared , can do write.
     * else should resume bt if , and check status.
     */
    if (mA2dpStatus != BTAUDIO_STARTED) {
        // do resume bt if state is standby
        if (mA2dpStatus == BTAUDIO_STANDBY) {
            setBtHostIfState(BTIF_ON);
        } else if (mA2dpStatus == BTAUDIO_STARTED || mA2dpStatus == BTAUDIO_SUSPENDING) {
            ALOGW("%s mA2dpStatus[%d] not allow resume", __func__, mA2dpStatus);
            ret = -EBUSY;
        } else {
            ALOGW("%s mA2dpStatus[%d] resume", __func__, mA2dpStatus);
            ret = -EINVAL;
        }
    }
    return ret;
}

int AudioDspStreamManager::setA2dpStreamStatusToDsp(int status)
{
    int retval = NO_ERROR;
    struct ipi_msg_t ipi_msg;
    retval = mAudioMessengerIPI->sendIpiMsg(&ipi_msg,
                                            TASK_SCENE_A2DP, AUDIO_IPI_LAYER_TO_DSP,
                                            AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_NEED_ACK,
                                            AUDIO_DSP_TASK_SETPRAM, DSP_PARAM_A2DPSTREAMSET, status, NULL);
    return retval;
}

int AudioDspStreamManager::setA2dpReconfig(bool on)
{
    int retval = NO_ERROR;
    struct ipi_msg_t ipi_msg;
    retval = mAudioMessengerIPI->sendIpiMsg(&ipi_msg,
                                            TASK_SCENE_A2DP, AUDIO_IPI_LAYER_TO_DSP,
                                            AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
                                            AUDIO_DSP_TASK_SETPRAM, DSP_PARAM_A2DPRECONFIG, on, NULL);
    return retval;
}
#endif /* end of #ifdef MTK_A2DP_OFFLOAD_SUPPORT */

unsigned long long AudioDspStreamManager::getA2dpRemoteDelayus()
{
    unsigned long delayReportms = 0;
#ifdef MTK_A2DP_OFFLOAD_SUPPORT
    unsigned long remoteDeviceAudioDelayNanos, transmittedOctets, monoTime;
    struct timespec transmittedOctetsTimeStamp;
    Status status;

    if (AL_TRYLOCK(mTaskLock)!= 0) {
        ALOGW("%s() mTaskLock fail using default", __FUNCTION__);
        return 0;
    }
    if (!mBluetoothAudioOffloadHostIf) {
        ALOGW("%s() btIf NULL", __FUNCTION__);
        AL_UNLOCK(mTaskLock);
        return -1;
    }

   mBluetoothAudioOffloadHostIf->getPresentationPosition(
              [&](Status status1, uint64_t remoteDeviceAudioDelayNanos1, uint64_t transmittedOctets1, TimeSpec transmittedOctetsTimeStamp1){
              status = status1;
              transmittedOctetsTimeStamp.tv_sec = transmittedOctetsTimeStamp1.tvSec;
              transmittedOctetsTimeStamp.tv_nsec = transmittedOctetsTimeStamp1.tvNSec;
              remoteDeviceAudioDelayNanos = remoteDeviceAudioDelayNanos1;
              transmittedOctets = transmittedOctets1;
              });

    monoTime = transmittedOctetsTimeStamp.tv_sec * 1000000000LL + transmittedOctetsTimeStamp.tv_nsec;
    delayReportms = remoteDeviceAudioDelayNanos / 1000000;
    ALOGD_IF((mlog_flag&AUD_DSP_STREAM), "-%s() status[%hhu] remoteDeviceAudioDelayNanos[%lu] transmittedOctets[%lu] monoTime[%lu] delayReportms[%lu]",
          __FUNCTION__, status, remoteDeviceAudioDelayNanos, transmittedOctets, monoTime, delayReportms);

    // out of range, reset to default value.
    if (delayReportms < kMinimumDelayMs || delayReportms > kMaximumDelayMs) {
        AudioALSAHardwareResourceManager::getInstance()->resetA2dpDeviceLatency();
    } else {
        AudioALSAHardwareResourceManager::getInstance()->setA2dpDeviceLatency(delayReportms);
    }

    AL_UNLOCK(mTaskLock);
#endif
    return delayReportms;
}

} // end of namespace android
