#include "AudioUtility.h"

#include <AudioLock.h>
#include <sys/auxv.h>
#include <stdio.h>
#include <stdlib.h>
#include <queue>

#include <dlfcn.h>

#include <audio_ringbuf.h>

#include <audio_fmt_conv_hal.h>

#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
#include "AudioParamParser.h"
#endif

#if defined(MTK_POWERHAL_AUDIO_SUPPORT)
#include <vendor/mediatek/hardware/mtkpower/1.0/IMtkPower.h>
#include "mtkpower_hint.h"
using namespace vendor::mediatek::hardware::mtkpower::V1_0;
using android::hardware::Return;
using android::hardware::hidl_death_recipient;
using android::hidl::base::V1_0::IBase;
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioUtility"


#define MTK_STREAMIN_VOLUEM_MAX (0x1000)
#define MTK_STREAMIN_VOLUME_VALID_BIT (12)


#if defined(MTK_POWERHAL_AUDIO_SUPPORT)
static android::sp<IMtkPower> gPowerHal = NULL;
static AudioLock gPowerHalLock;

struct PowerDeathRecipient : virtual public hidl_death_recipient {
    virtual void serviceDied(uint64_t cookie __unused, const android::wp<IBase> &who __unused) override {
        ALOGW("%s(), power hal died, get power hal again", __FUNCTION__);
        AL_LOCK(gPowerHalLock);
        gPowerHal = NULL;
        android::getPowerHal();
        AL_UNLOCK(gPowerHalLock);
    }
};

static android::sp<PowerDeathRecipient> powerHalDeathRecipient = NULL;

#endif

namespace android {

/* Name for MicInfo */
#define MIC_INFO_AUDIO_TYPE_NAME        "MicInfo"
#define PROJECTS_CATEGORY_TYPE_NAME     "projects"
#define MICROPHONES_CATEGORY_TYPE_NAME  "microphones"
#define PROJECT_ID_PARAM_NAME           "device_id"
#define DEVICE_IN_TYPE_PARAM_NAME       "device_in_type"
#define ADDRESS_PARAM_NAME              "address"
#define MIC_LOCATION_PARAM_NAME         "mic_location"
#define DEVICE_GROUP_PARAM_NAME         "device_group"
#define INDEX_IN_THE_GROUP_PARAM_NAME   "index_in_the_group"
#define GEOMETRIC_LOCATION_PARAM_NAME   "geometric_location"
#define ORIENTATION_PARAM_NAME          "orientation"
#define FREQUENCY_RESPONSES_PARAM_NAME  "frequency_responses"
#define SENSITIVITY_PARAM_NAME          "sensitivity"
#define MAX_SPL_PARAM_NAME              "max_spl"
#define MIN_SPL_PARAM_NAME              "min_spl"
#define DIRECTIONALITY_PARAM_NAME       "directionality"

#define DEVICE_IN_TYPE_SEPERATOR "|"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#if defined(__LP64__)
const char *AUDIO_COMPENSATION_FILTER_LIB_VENDOR_PATH = "/vendor/lib64/libaudiocompensationfilter_vendor.so";
const char *AUDIO_COMPENSATION_FILTER_LIB_PATH = "/system_ext/lib64/libaudiocompensationfilter.so";
const char *AUDIO_COMPONENT_ENGINE_LIB_VENDOR_PATH = "/vendor/lib64/libaudiocomponentengine_vendor.so";
const char *AUDIO_COMPONENT_ENGINE_LIB_PATH = "/system/lib64/libaudiocomponentengine.so";

#else
const char *AUDIO_COMPENSATION_FILTER_LIB_VENDOR_PATH = "/vendor/lib/libaudiocompensationfilter_vendor.so";
const char *AUDIO_COMPENSATION_FILTER_LIB_PATH = "/system_ext/lib/libaudiocompensationfilter.so";
const char *AUDIO_COMPONENT_ENGINE_LIB_VENDOR_PATH = "/vendor/lib/libaudiocomponentengine_vendor.so";
const char *AUDIO_COMPONENT_ENGINE_LIB_PATH = "/system/lib/libaudiocomponentengine.so";

#endif
static void *g_AudioComponentEngineHandle = NULL;
static create_AudioBitConverter *g_CreateMtkAudioBitConverter = NULL;
static create_AudioSrc *g_CreateMtkAudioSrc = NULL;
static create_AudioLoud *g_CreateMtkAudioLoud = NULL;
static create_DcRemove *g_CreateMtkDcRemove = NULL;
static destroy_AudioBitConverter *g_DestroyMtkAudioBitConverter = NULL;
static destroy_AudioSrc *g_DestroyMtkAudioSrc = NULL;
static destroy_AudioLoud *g_DestroyMtkAudioLoud = NULL;
static destroy_DcRemove *g_DestroyMtkDcRemove = NULL;
static void *g_AudioCompensationFilterHandle = NULL;
static setFunAudioCompFltCustParam *g_setAudioCompFltCustParamFrom = NULL;
static getFunAudioCompFltCustParam *g_getAudioCompFltCustParamFrom = NULL;

const static char *ProcessBitsString[] = {"v7l", "v8l", "aarch64"};

const char *audio_dump_path = DUMP_PATH;
const char *streamout_ori = DUMP_PATH"streamoutori_dump.pcm";
const char *streamout_ori_propty = "streamout_ori.pcm.dump";
const char *streamout_dcr = DUMP_PATH"streamoutdcr_dump.pcm";
const char *streamout_dcr_propty = "streamout_dcr.pcm.dump";

const char *streamout_s2m = DUMP_PATH"streamouts2m_dump.pcm";
const char *streamout_s2m_propty = "streamout_s2m.pcm.dump";
const char *streamout_acf = DUMP_PATH"streamoutacf_dump.pcm";
const char *streamout_acf_propty = "streamout_acf.pcm.dump";
const char *streamout_hcf = DUMP_PATH"streamouthcf_dump.pcm";
const char *streamout_hcf_propty = "streamout_hcf.pcm.dump";

const char *streamout = DUMP_PATH"streamout.pcm";
const char *streamoutfinal = DUMP_PATH"streamoutfinal.pcm";
const char *streamout_propty = "vendor.streamout.pcm.dump";
const char *aud_dumpftrace_dbg_propty = "dumpftrace_dbg";
const char *streaminIVCPMIn = DUMP_PATH"StreamIVPCMIn_Dump.pcm";
const char *streaminIVIn = DUMP_PATH"StreamIVIn_Dump.pcm";
const char *streamout_vibsignal = DUMP_PATH"streamoutvib.pcm";
const char *streamout_notch = DUMP_PATH"streamoutnotch.pcm";
const char *streamoutdsp_propty = "vendor.streamout.dsp.dump";
const char *a2dpdsp_propty = "vendor.a2dp.streamout.pcm";

const char *streaminmanager = DUMP_PATH"StreamInManager_Dump.pcm";
const char *streamin = DUMP_PATH"StreamIn_Dump.pcm";
const char *streaminOri = DUMP_PATH"StreamInOri_Dump.pcm";
const char *streaminI2S = DUMP_PATH"StreamInI2S_Dump.pcm";
const char *streaminDAIBT = DUMP_PATH"StreamInDAIBT_Dump.pcm";
const char *streaminSpk = DUMP_PATH"streamin_spk.pcm";
const char *streaminSpk_propty = "streamin.spkpcm.dump";
const char *capture_data_provider = DUMP_PATH"CaptureDataProvider";

const char *streamin_propty = "vendor.streamin.pcm.dump";
const char *streamindsp_propty = "vendor.streamin.dsp.dump";

const char *streamin_epl_propty = "vendor.streamin.epl.dump";

const char *allow_low_latency_propty = "streamout.lowlatency.allow";
const char *streamhfp_propty = "streamhfp.pcm.dump";
const char *allow_offload_propty = "vendor.streamout.offload.allow";
const char *streamout_log_propty = "vendor.streamout.log";
const char *streamin_log_propty = "vendor.streamin.log";
const char *platform_arch = "aarch64";

#define EPL_PACKET_BYTE_SIZE 9600 //4800 * 2(short)

String8 keyCardName = String8("mtsndcard");

String8 keypcmDl1Meida = String8("MultiMedia1_PLayback");
String8 keypcmUl1Capture = String8("MultiMedia1_Capture");
String8 keypcmPcm2voice = String8("PCM2_PLayback");
String8 keypcmHDMI = String8("HMDI_PLayback");
String8 keypcmUlDlLoopback = String8("ULDL_Loopback");
String8 keypcmI2Splayback = String8("I2S0_PLayback");
String8 keypcmMRGrxPlayback = String8("MRGRX_PLayback");
String8 keypcmMRGrxCapture = String8("MRGRX_CAPTURE");
String8 keypcmFMI2SPlayback = String8("FM_I2S_Playback");
String8 keypcmFMI2SCapture = String8("FM_I2S_Capture");
String8 keypcmFMHostless = String8("Hostless_FM");
String8 keypcmI2S0Dl1Playback = String8("I2S0DL1_PLayback");
String8 keypcmDl1SpkPlayback = String8("DL1SCPSPK_PLayback");
String8 keypcmScpVoicePlayback = String8("SCPVoice_PLayback");
String8 keypcmCS43130 = String8("CS43130_Stream");
String8 keypcmCS35L35 = String8("CS35L35_Stream");
String8 keypcmDl1AwbCapture = String8("DL1_AWB_Record");
String8 keypcmVoiceCallBT = String8("Voice_Call_BT_Playback");
String8 keypcmVOIPCallBTPlayback = String8("VOIP_Call_BT_Playback");
String8 keypcmVOIPCallBTCapture = String8("VOIP_Call_BT_Capture");
String8 keypcmTDMLoopback = String8("TDM_Debug_Record");
String8 keypcmMRGTxPlayback = String8("FM_MRGTX_Playback");
String8 keypcmUl2Capture = String8("MultiMediaData2_Capture");
String8 keypcmI2SAwbCapture = String8("I2S0AWB_Capture");
String8 keypcmMODADCI2S = String8("ANC_Debug_Record_MOD");
String8 keypcmADC2AWB = String8("ANC_Debug_Record_ADC2");
String8 keypcmIO2DAI = String8("ANC_Debug_Record_IO2");
String8 keypcmHpimpedancePlayback = String8("HP_IMPEDANCE_Playback");
String8 keypcmModomDaiCapture = String8("Moddai_Capture");
String8 keypcmOffloadGdmaPlayback = String8("OFFLOAD_GDMA_Playback");
String8 keypcmDl2Meida = String8("MultiMedia2_PLayback");    //DL2 playback
String8 keypcmDl3Meida = String8("MultiMedia3_PLayback");        //DL3 playback
String8 keypcmBTCVSDCapture = String8("BTCVSD");
String8 keypcmBTCVSDPlayback = String8("BTCVSD");
String8 keypcmExtSpkMeida = String8("Speaker_PLayback");
String8 keypcmVoiceMD1 = String8("Voice_MD1_PLayback");
String8 keypcmVoiceMD2 = String8("Voice_MD2_PLayback");
String8 keypcmVoiceMD1BT = String8("Voice_MD1_BT_Playback");
String8 keypcmVoiceMD2BT = String8("Voice_MD2_BT_Playback");
String8 keypcmVoiceUltra = String8("Voice_ULTRA_PLayback");
String8 keypcmVoiceUSB = String8("Voice_USB_PLayback");
String8 keypcmVoiceUSBEchoRef = String8("Voice_USB_EchoRef");
String8 keypcmI2S2ADCCapture = String8("I2S2ADC2_Capture");
String8 keypcmVoiceDaiCapture = String8("Voice_Dai_Capture");
String8 keypcmOffloadPlayback = String8("Offload_Playback");
String8 keypcmExtHpMedia = String8("Headphone_PLayback");
String8 keypcmDL1DATA2PLayback = String8("Deep_Buffer_PLayback");
String8 keypcmPcmRxCapture= String8("VUL3_Capture");
String8 keypcmVUL2Capture = String8("VUL2_Capture");

#if defined(MTK_AUDIO_KS)
String8 keypcmPlayback1 = String8("Playback_1");
String8 keypcmPlayback2 = String8("Playback_2");
String8 keypcmPlayback3 = String8("Playback_3");
String8 keypcmPlayback4 = String8("Playback_4");
String8 keypcmPlayback5 = String8("Playback_5");
String8 keypcmPlayback6 = String8("Playback_6");
String8 keypcmPlayback8 = String8("Playback_8");
String8 keypcmPlayback12 = String8("Playback_12");
String8 keypcmPlaybackHDMI = String8("Playback_HDMI");

String8 keypcmCapture1 = String8("Capture_1");
String8 keypcmCapture2 = String8("Capture_2");
String8 keypcmCapture3 = String8("Capture_3");
String8 keypcmCapture4 = String8("Capture_4");
String8 keypcmCapture6 = String8("Capture_6");
String8 keypcmCapture7 = String8("Capture_7");
String8 keypcmCapture8 = String8("Capture_8");

String8 keypcmCaptureMono1 = String8("Capture_Mono_1");

String8 keypcmHostlessFm = String8("Hostless_FM");
String8 keypcmHostlessLpbk = String8("Hostless_LPBK");
String8 keypcmHostlessSpeech = String8("Hostless_Speech");
String8 keypcmHostlessSphEchoRef = String8("Hostless_Sph_Echo_Ref");
String8 keypcmHostlessSpkInit = String8("Hostless_Spk_Init");
String8 keypcmHostlessADDADLI2SOut = String8("Hostless_ADDA_DL_I2S_OUT");
String8 keypcmHostlessSRCBargein = String8("Hostless_SRC_Bargein");
String8 keypcmHostlessHwGainAAudio = String8("Hostless_HW_Gain_AAudio");
String8 keypcmHostlessSrcAAudio = String8("Hostless_SRC_AAudio");

#if defined(MTK_AUDIODSP_SUPPORT)
String8 keypcmPlaybackDspprimary = String8("DSP_Playback_Primary");
String8 keypcmPlaybackDspVoip    = String8("DSP_Playback_Voip");
String8 keypcmPlaybackDspDeepbuf = String8("DSP_Playback_DeepBuf");
String8 keypcmPlaybackDsp        = String8("DSP_Playback_Playback");
String8 keypcmPlaybackDspMixer1  = String8("DSP_Playback_Swmixer1");
String8 keypcmPlaybackDspMixer2  = String8("DSP_Playback_Swmixer2");
String8 keypcmCaptureDspUl1      = String8("DSP_Capture_Ul1");
String8 keypcmPlaybackDspA2DP    = String8("DSP_Playback_A2DP");
String8 keypcmPlaybackDspDataProvider    = String8("DSP_Playback_DataProvider");
String8 keypcmCallfinalDsp       = String8("DSP_Call_Final");
String8 keypcmPlaybackDspFast    = String8("DSP_Playback_Fast");
String8 keypcmPlaybackDspKtv    = String8("DSP_Playback_Ktv");
String8 keypcmCaptureDspRaw      = String8("DSP_Capture_Raw");
String8 keypcmPlaybackDspFm      = String8("DSP_Playback_Fm_Adsp");
#endif
#if defined(MTK_VOW_SUPPORT)
String8 keypcmVOWCapture = String8("VOW_Capture");
#endif
#endif
String8 keypcmVOWBargeInCapture = String8("VOW_Barge_In_Capture");

//ultra-sound
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
String8 keypcmUltra = String8("SCP_ULTRA_Playback");
String8 keypcmCapture5 = String8("Capture_5");
String8 keypcmPlayback7 = String8("Playback_7");
#endif

static bool bNeedAEETimeoutFlg;

/**
 * Get enum by string
 */
struct enum_to_str_table {
    const char* name;
    uint32_t value;
};


#define AUDIO_ENUM_TO_STR(X)   { #X, X }

static const struct enum_to_str_table micLoccationTable[] = {
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_LOCATION_UNKNOWN),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_LOCATION_MAINBODY),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_LOCATION_MAINBODY_MOVABLE),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_LOCATION_PERIPHERAL),
    {NULL, 0},
};

static const struct enum_to_str_table micDirectionalityTable[] = {
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_DIRECTIONALITY_UNKNOWN),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_DIRECTIONALITY_OMNI),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_DIRECTIONALITY_BI_DIRECTIONAL),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_DIRECTIONALITY_CARDIOID),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_DIRECTIONALITY_HYPER_CARDIOID),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_DIRECTIONALITY_SUPER_CARDIOID),
    {NULL, 0},
};

static const struct enum_to_str_table micChannelMappingTable[] = {
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_CHANNEL_MAPPING_UNUSED),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_CHANNEL_MAPPING_DIRECT),
    AUDIO_ENUM_TO_STR(AUDIO_MICROPHONE_CHANNEL_MAPPING_PROCESSED),
    {NULL, 0},
};

static const struct enum_to_str_table deviceInTypeTable[] = {
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_COMMUNICATION),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_AMBIENT),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_BUILTIN_MIC),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_WIRED_HEADSET),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_AUX_DIGITAL),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_HDMI),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_VOICE_CALL),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_TELEPHONY_RX),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_BACK_MIC),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_REMOTE_SUBMIX),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_ANLG_DOCK_HEADSET),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_DGTL_DOCK_HEADSET),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_USB_ACCESSORY),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_USB_DEVICE),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_FM_TUNER),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_TV_TUNER),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_LINE),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_SPDIF),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_BLUETOOTH_A2DP),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_LOOPBACK),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_IP),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_BUS),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_PROXY),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_USB_HEADSET),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_BLUETOOTH_BLE),
    AUDIO_ENUM_TO_STR(AUDIO_DEVICE_IN_DEFAULT),
    {NULL, 0},
};

#define WAVEFORMATEX_SIZE (44)

struct WAVEFORMATEX {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;

    WAVEFORMATEX() {
        wFormatTag      = 0;
        nChannels       = 0;
        nSamplesPerSec  = 0;
        nAvgBytesPerSec = 0;
        nBlockAlign     = 0;
        wBitsPerSample  = 0;
    }
};

struct WavFormatHeader
{
    char        ckID[5];           // 4 : Chunk ID: "RIFF"
    uint32_t    cksize;            // 4: Chunk size: File length - 8
    char        WAVEID[5];         // 4: WAVE ID: "WAVE"
    // Format Chunk
    char        FormatckID[5];     // 4: "fmt "
    uint32_t    Formatcksize;      // 4: Chunk size: 16 or 18 or 40 ( We will use 18, no extensiable format. )
    char        DataID[5];         // 4: "data"
    uint32_t    Datacksize;        // 4: Chunk size: Data Size
    WAVEFORMATEX WaveFormatEx;

    WavFormatHeader()
    :   cksize(0),
        Formatcksize(16),
        Datacksize(0)
    {
        strncpy(ckID, "RIFF", sizeof(ckID));
        strncpy(WAVEID, "WAVE", sizeof(WAVEID));
        strncpy(FormatckID, "fmt ", sizeof(FormatckID));
        strncpy(DataID, "data", sizeof(DataID));
    }
};

unsigned long long TimeDifference(struct timespec time1, struct timespec time2) {
    unsigned long long diffns = 0;
    struct timespec tstemp1 = time1;
    struct timespec tstemp2 = time2;

    //    ALOGD("TimeStampDiff time1 sec= %ld, nsec=%ld, time2 sec=%ld, nsec=%ld" ,tstemp1.tv_sec, tstemp1.tv_nsec, tstemp2.tv_sec, tstemp2.tv_nsec);

    if (tstemp1.tv_sec > tstemp2.tv_sec) {
        if (tstemp1.tv_nsec >= tstemp2.tv_nsec) {
            diffns = ((tstemp1.tv_sec - tstemp2.tv_sec) * (unsigned long long)1000000000) + tstemp1.tv_nsec - tstemp2.tv_nsec;
        } else {
            diffns = ((tstemp1.tv_sec - tstemp2.tv_sec - 1) * (unsigned long long)1000000000) + tstemp1.tv_nsec + 1000000000 - tstemp2.tv_nsec;
        }
    } else if (tstemp1.tv_sec == tstemp2.tv_sec) {
        if (tstemp1.tv_nsec >= tstemp2.tv_nsec) {
            diffns = tstemp1.tv_nsec - tstemp2.tv_nsec;
        } else {
            diffns = tstemp2.tv_nsec - tstemp1.tv_nsec;
        }
    } else {
        if (tstemp2.tv_nsec >= tstemp1.tv_nsec) {
            diffns = ((tstemp2.tv_sec - tstemp1.tv_sec) * (unsigned long long)1000000000) + tstemp2.tv_nsec - tstemp1.tv_nsec;
        } else {
            diffns = ((tstemp2.tv_sec - tstemp1.tv_sec - 1) * (unsigned long long)1000000000) + tstemp2.tv_nsec + 1000000000 - tstemp1.tv_nsec;
        }
    }
    //    ALOGD("TimeDifference time1 sec= %ld, nsec=%ld, time2 sec=%ld, nsec=%ld, diffns=%lld" ,tstemp1.tv_sec, tstemp1.tv_nsec, tstemp2.tv_sec,tstemp2.tv_nsec,diffns);
    return diffns;
}


//---------- implementation of ringbuffer--------------


// function for get how many data is available

/**
* function for get how many data is available
* @return how many data exist
*/

int RingBuf_getDataCount(const RingBuf *RingBuf1) {
    /*
    ALOGD("RingBuf1->pBase = 0x%x RingBuf1->pWrite = 0x%x  RingBuf1->bufLen = %d  RingBuf1->pRead = 0x%x",
        RingBuf1->pBufBase,RingBuf1->pWrite, RingBuf1->bufLen,RingBuf1->pRead);
        */
    int count = RingBuf1->pWrite - RingBuf1->pRead;
    if (count < 0) { count += RingBuf1->bufLen; }
    return count;
}

/**
*  function for get how free space available
* @return how free sapce
*/

int RingBuf_getFreeSpace(const RingBuf *RingBuf1) {
    /*
    ALOGD("RingBuf1->pBase = 0x%x RingBuf1->pWrite = 0x%x  RingBuf1->bufLen = %d  RingBuf1->pRead = 0x%x",
        RingBuf1->pBufBase,RingBuf1->pWrite, RingBuf1->bufLen,RingBuf1->pRead);*/
    int count = 0;

    if (RingBuf1->pRead > RingBuf1->pWrite) {
        count = RingBuf1->pRead - RingBuf1->pWrite - RING_BUF_SIZE_OFFSET;
    } else { // RingBuf1->pRead <= RingBuf1->pWrite
        count = RingBuf1->pRead - RingBuf1->pWrite + RingBuf1->bufLen - RING_BUF_SIZE_OFFSET;
    }

    return (count > 0) ? count : 0;
}

/**
* copy count number bytes from ring buffer to buf
* @param buf buffer copy from
* @param RingBuf1 buffer copy to
* @param count number of bytes need to copy
*/

void RingBuf_copyToLinear(char *buf, RingBuf *RingBuf1, int count) {
    /*
    ALOGD("RingBuf1->pBase = 0x%x RingBuf1->pWrite = 0x%x  RingBuf1->bufLen = %d  RingBuf1->pRead = 0x%x",
        RingBuf1->pBufBase,RingBuf1->pWrite, RingBuf1->bufLen,RingBuf1->pRead);*/
    if (RingBuf1->pRead <= RingBuf1->pWrite) {
        memcpy(buf, RingBuf1->pRead, count);
        RingBuf1->pRead += count;
    } else {
        char *end = RingBuf1->pBufBase + RingBuf1->bufLen;
        int r2e = end - RingBuf1->pRead;
        if (count <= r2e) {
            //ALOGD("2 RingBuf_copyToLinear r2e= %d count = %d",r2e,count);
            memcpy(buf, RingBuf1->pRead, count);
            RingBuf1->pRead += count;
            if (RingBuf1->pRead == end) {
                RingBuf1->pRead = RingBuf1->pBufBase;
            }
        } else {
            //ALOGD("3 RingBuf_copyToLinear r2e= %d count = %d",r2e,count);
            memcpy(buf, RingBuf1->pRead, r2e);
            memcpy(buf + r2e, RingBuf1->pBufBase, count - r2e);
            RingBuf1->pRead = RingBuf1->pBufBase + count - r2e;
        }
    }
}

/**
* copy count number bytes from buf to RingBuf1
* @param RingBuf1 ring buffer copy from
* @param buf copy to
* @param count number of bytes need to copy
*/
void RingBuf_copyFromLinear(RingBuf *RingBuf1, const char *buf, int count) {
    int spaceIHave;
    char *end = RingBuf1->pBufBase + RingBuf1->bufLen;

    // count buffer data I have
    spaceIHave = RingBuf1->bufLen - RingBuf_getDataCount(RingBuf1) - RING_BUF_SIZE_OFFSET;
    //spaceIHave = RingBuf_getDataCount(RingBuf1);

    // if not enough, assert
    ASSERT(spaceIHave >= count);

    if (RingBuf1->pRead <= RingBuf1->pWrite) {
        int w2e = end - RingBuf1->pWrite;
        if (count <= w2e) {
            memcpy(RingBuf1->pWrite, buf, count);
            RingBuf1->pWrite += count;
            if (RingBuf1->pWrite == end) {
                RingBuf1->pWrite = RingBuf1->pBufBase;
            }
        } else {
            memcpy(RingBuf1->pWrite, buf, w2e);
            memcpy(RingBuf1->pBufBase, buf + w2e, count - w2e);
            RingBuf1->pWrite = RingBuf1->pBufBase + count - w2e;
        }
    } else {
        memcpy(RingBuf1->pWrite, buf, count);
        RingBuf1->pWrite += count;
    }

}

/**
* fill count number zero bytes to RingBuf1
* @param RingBuf1 ring buffer fill from
* @param count number of bytes need to copy
*/
void RingBuf_fillZero(RingBuf *RingBuf1, int count) {
    int spaceIHave;
    char *end = RingBuf1->pBufBase + RingBuf1->bufLen;

    // count buffer data I have
    spaceIHave = RingBuf1->bufLen - RingBuf_getDataCount(RingBuf1) - RING_BUF_SIZE_OFFSET;
    //spaceIHave = RingBuf_getDataCount(RingBuf1);

    // if not enough, assert
    ASSERT(spaceIHave >= count);

    if (RingBuf1->pRead <= RingBuf1->pWrite) {
        int w2e = end - RingBuf1->pWrite;
        if (count <= w2e) {
            memset(RingBuf1->pWrite, 0, sizeof(char)*count);
            RingBuf1->pWrite += count;
            if (RingBuf1->pWrite == end) {
                RingBuf1->pWrite = RingBuf1->pBufBase;
            }
        } else {
            memset(RingBuf1->pWrite, 0, sizeof(char)*w2e);
            memset(RingBuf1->pBufBase, 0, sizeof(char) * (count - w2e));
            RingBuf1->pWrite = RingBuf1->pBufBase + count - w2e;
        }
    } else {
        memset(RingBuf1->pWrite, 0, sizeof(char)*count);
        RingBuf1->pWrite += count;
    }

}


/**
* copy ring buffer from RingBufs(source) to RingBuft(target)
* @param RingBuft ring buffer copy to
* @param RingBufs copy from copy from
*/

void RingBuf_copyEmpty(RingBuf *RingBuft, RingBuf *RingBufs) {
    if (RingBufs->pRead <= RingBufs->pWrite) {
        RingBuf_copyFromLinear(RingBuft, RingBufs->pRead, RingBufs->pWrite - RingBufs->pRead);
        //RingBufs->pRead = RingBufs->pWrite;
        // no need to update source read pointer, because it is read to empty
    } else {
        char *end = RingBufs->pBufBase + RingBufs->bufLen;
        RingBuf_copyFromLinear(RingBuft, RingBufs->pRead, end - RingBufs->pRead);
        RingBuf_copyFromLinear(RingBuft, RingBufs->pBufBase, RingBufs->pWrite - RingBufs->pBufBase);
    }
}


/**
* copy ring buffer from RingBufs(source) to RingBuft(target) with count
* @param RingBuft ring buffer copy to
* @param RingBufs copy from copy from
*/
int RingBuf_copyFromRingBuf(RingBuf *RingBuft, RingBuf *RingBufs, int count) {
    int cntInRingBufs = RingBuf_getDataCount(RingBufs);
    int freeSpaceInRingBuft = RingBuf_getFreeSpace(RingBuft);
    if (count > cntInRingBufs || count > freeSpaceInRingBuft) {
        ALOGE("%s(), src: b %p, r %p, w %p, e %p, sz %u. cnt %d, avail %d",
            __FUNCTION__,
            RingBufs->pBufBase,
            RingBufs->pRead,
            RingBufs->pWrite,
            RingBufs->pBufEnd,
            RingBufs->bufLen,
            count,
            cntInRingBufs);
        ALOGE("%s(), tar: b %p, r %p, w %p, e %p, sz %u. cnt %d, free %d",
            __FUNCTION__,
            RingBuft->pBufBase,
            RingBuft->pRead,
            RingBuft->pWrite,
            RingBuft->pBufEnd,
            RingBuft->bufLen,
            count,
            freeSpaceInRingBuft);
        ASSERT(count <= cntInRingBufs && count <= freeSpaceInRingBuft);
        return 0;
    }

    if (RingBufs->pRead <= RingBufs->pWrite) {
        RingBuf_copyFromLinear(RingBuft, RingBufs->pRead, count);
        RingBufs->pRead += count;
        // no need to update source read pointer, because it is read to empty
    } else {
        char *end = RingBufs->pBufBase + RingBufs->bufLen;
        int cnt2e = end - RingBufs->pRead;
        if (cnt2e >= count) {
            RingBuf_copyFromLinear(RingBuft, RingBufs->pRead, count);
            RingBufs->pRead += count;
            if (RingBufs->pRead == end) {
                RingBufs->pRead = RingBufs->pBufBase;
            }
        } else {
            RingBuf_copyFromLinear(RingBuft, RingBufs->pRead, cnt2e);
            RingBuf_copyFromLinear(RingBuft, RingBufs->pBufBase, count - cnt2e);
            RingBufs->pRead = RingBufs->pBufBase + count - cnt2e;
        }
    }
    return count;
}

/**
* write bytes size of count woith value
* @param RingBuf1 ring buffer copy to
* @value value put into buffer
* @count bytes ned to put.
*/
void RingBuf_writeDataValue(RingBuf *RingBuf1, const int value, const int count) {
    int spaceIHave;

    // count buffer data I have
    spaceIHave = RingBuf1->bufLen - RingBuf_getDataCount(RingBuf1) - RING_BUF_SIZE_OFFSET;

    // if not enough, assert
    ASSERT(spaceIHave >= count);

    if (RingBuf1->pRead <= RingBuf1->pWrite) {
        char *end = RingBuf1->pBufBase + RingBuf1->bufLen;
        int w2e = end - RingBuf1->pWrite;
        if (count <= w2e) {
            memset(RingBuf1->pWrite, value, count);
            RingBuf1->pWrite += count;
        } else {
            memset(RingBuf1->pWrite, value, w2e);
            memset(RingBuf1->pBufBase, value, count - w2e);
            RingBuf1->pWrite = RingBuf1->pBufBase + count - w2e;
        }
    } else {
        memset(RingBuf1->pWrite, value, count);
        RingBuf1->pWrite += count;
    }

}

/**
* discard count bytes data in ring buffer
* @param ringBuf ring buffer to discard data
* @param count bytes size to discard
* @return 0 if discard successfully, -EINVAL if count or RingBuf1 is invalid
*/
int RingBuf_discardData(RingBuf *ringBuf, int count) {
    if (!ringBuf) {
        ALOGE("%s(), ringBuf == NULL", __FUNCTION__);
        ASSERT(0);
        return -EINVAL;
    }

    if (count > RingBuf_getDataCount(ringBuf)) {
        ALOGE("%s(), count %d > remain data %d", __FUNCTION__, count, RingBuf_getDataCount(ringBuf));
        ASSERT(0);
        return -EINVAL;
    }

    if (ringBuf->pRead <= ringBuf->pWrite) {
        ringBuf->pRead += count;
    } else {
        char *end = ringBuf->pBufBase + ringBuf->bufLen;
        int r2e = end - ringBuf->pRead;
        if (count <= r2e) {
            ringBuf->pRead += count;
            if (ringBuf->pRead == end) {
                ringBuf->pRead = ringBuf->pBufBase;
            }
        } else {
            ringBuf->pRead = ringBuf->pBufBase + count - r2e;
        }
    }
    return 0;
}

/**
* check if next count bytes data will cross boundary
* @param ringBuf ring buffer to check
* @param count bytes size to check
* @return 1 if cross boundary, 0 if not, < 0 if errno
*/
int RingBuf_checkDataCrossBoundary(const RingBuf *ringBuf, int count) {
    if (!ringBuf) {
        ALOGE("%s(), ringBuf == NULL", __FUNCTION__);
        ASSERT(0);
        return -EINVAL;
    }

    if (count > RingBuf_getDataCount(ringBuf)) {
        ALOGE("%s(), count %d > remain data %d", __FUNCTION__, count, RingBuf_getDataCount(ringBuf));
        ASSERT(0);
        return -EINVAL;
    }

    if (ringBuf->pRead <= ringBuf->pWrite) {
        return 0;
    } else {
        char *end = ringBuf->pBufBase + ringBuf->bufLen;
        int r2e = end - ringBuf->pRead;
        if (count <= r2e) {
            return 0;
        } else {
            return 1;
        }
    }

    return 0;
}


void RingBuf_dynamicChangeBufSize(struct RingBuf *ringBuf, uint32_t count) {
    uint32_t dataCount = 0;
    uint32_t freeSpace = 0;

    uint32_t newCount = 0;
    struct RingBuf newRingBuf;

    if (!ringBuf) {
        WARNING("null");
        return;
    }
    if (!count) {
        return;
    }

    memset(&newRingBuf, 0, sizeof(struct RingBuf));

    dataCount = RingBuf_getDataCount(ringBuf);
    freeSpace = RingBuf_getFreeSpace(ringBuf);

    if ((freeSpace  <                   count) ||
        (freeSpace  > (8 * (dataCount + count)))) {
        newCount  = (2 * (dataCount + count));
        newCount += RING_BUF_SIZE_OFFSET;

        if (newCount > MAX_RING_BUF_SIZE) {
            ALOGW("%s(), skip, keep ringBuf %u, count %u, freeSpace %u",__FUNCTION__, ringBuf->bufLen, count, freeSpace);
            return;
        }

        ALOGD("%s(), %p: %u -> %u, dataCount %u, count %u, freeSpace %u",
              __FUNCTION__,
              ringBuf->pBufBase,
              ringBuf->bufLen,
              newCount,
              dataCount,
              count,
              freeSpace);

        newRingBuf.bufLen = newCount;
        newRingBuf.pBufBase = new char[newRingBuf.bufLen];
        newRingBuf.pRead = newRingBuf.pBufBase;
        newRingBuf.pWrite = newRingBuf.pBufBase;
        newRingBuf.pBufEnd  = newRingBuf.pBufBase + newRingBuf.bufLen;

        memset(newRingBuf.pBufBase, 0, newCount);

        /* copy old data */
        RingBuf_copyFromRingBuf(
            &newRingBuf,
            ringBuf,
            dataCount);

        /* delete old ringbuf */
        delete[] ringBuf->pBufBase;

        /* update info */
        ringBuf->bufLen = newRingBuf.bufLen;
        ringBuf->pBufBase = newRingBuf.pBufBase;
        ringBuf->pRead = newRingBuf.pRead;
        ringBuf->pWrite = newRingBuf.pWrite;
        ringBuf->pBufEnd = newRingBuf.pBufEnd;
    }
}


//---------end of ringbuffer implemenation------------------------------------------------------

short clamp16(int sample) {
    short max = 0x7FFF;
    if (sample > 0 && sample > max) {
        sample = max;
    } else if (sample < 0 && sample < - max - 1) {
        sample = - max - 1;
    }
    return sample;
}


BCV_PCM_FORMAT get_bcv_pcm_format(audio_format_t source, audio_format_t target) {
    BCV_PCM_FORMAT bcv_pcm_format = BCV_SIMPLE_SHIFT_BIT_END;
    if (source == AUDIO_FORMAT_PCM_16_BIT) {
        if (target == AUDIO_FORMAT_PCM_8_24_BIT) {
            bcv_pcm_format = BCV_IN_Q1P15_OUT_Q9P23;
        } else if (target == AUDIO_FORMAT_PCM_32_BIT) {
            bcv_pcm_format = BCV_IN_Q1P15_OUT_Q1P31;
        }
    } else if (source == AUDIO_FORMAT_PCM_8_24_BIT) {
        if (target == AUDIO_FORMAT_PCM_16_BIT) {
            ALOGV("BCV_IN_Q9P23_OUT_Q1P15");
            bcv_pcm_format = BCV_IN_Q9P23_OUT_Q1P15;
        } else if (target == AUDIO_FORMAT_PCM_32_BIT) {
            bcv_pcm_format = BCV_IN_Q9P23_OUT_Q1P31;
        }
    } else if (source == AUDIO_FORMAT_PCM_32_BIT) {
        if (target == AUDIO_FORMAT_PCM_16_BIT) {
            bcv_pcm_format = BCV_IN_Q1P31_OUT_Q1P15;
        } else if (target == AUDIO_FORMAT_PCM_8_24_BIT) {
            bcv_pcm_format = BCV_IN_Q1P31_OUT_Q9P23;
        }
    }
    ALOGV("%s(), bcv_pcm_format %d", __FUNCTION__, bcv_pcm_format);
    return bcv_pcm_format;
}

size_t getSizePerFrame(audio_format_t fmt, unsigned int numChannel) {
    size_t sizePerChannel = audio_bytes_per_sample(fmt);
    if (sizePerChannel == 0) {
        ALOGW("%s(), sizePerChannel == 0!! fmt %d error!! use 4 instead!!", __FUNCTION__, fmt);
        sizePerChannel = 4; /* 8-24 & 32 bits */
    }
    return numChannel * sizePerChannel;
}

size_t getSizePerFrameByAttr(const stream_attribute_t *attr) {
    if (attr == NULL) {
        ALOGW("%s(), attr NULL!! use 8 instead!!", __FUNCTION__);
        return 8; /* 2ch, 8-24 & 32 bits */
    }
    return getSizePerFrame(attr->audio_format, attr->num_channels);
}

uint32_t getFramesByAttr(const stream_attribute_t *attr) {
    return attr->buffer_size / getSizePerFrameByAttr(attr);
}

uint32_t getPeriodBufSize(const stream_attribute_t *attribute, uint32_t period_time_ms) {
    uint32_t size_per_frame = getSizePerFrame(attribute->audio_format, attribute->num_channels);
    uint32_t size_per_period = (size_per_frame *
                                attribute->sample_rate *
                                period_time_ms) / 1000;

    return size_per_period;
}

uint32_t getPeriodBufSizeByUs(const stream_attribute_t *attribute, uint64_t period_time_us) {
    uint64_t size_per_frame = getSizePerFrame(attribute->audio_format, attribute->num_channels);
    uint64_t size_per_period = (size_per_frame *
                                attribute->sample_rate *
                                period_time_us) / 1000000;

    if ((size_per_period % size_per_frame) != 0) { // alignment
        size_per_period = ((size_per_period / size_per_frame) + 1) * size_per_frame;
    }

    return size_per_period;
}

uint64_t getBufferLatencyMs(const stream_attribute_t *attribute, uint64_t bytes) {
    if (attribute == NULL) {
        return 0;
    }

    uint64_t size_per_frame = getSizePerFrame(attribute->audio_format, attribute->num_channels);
    uint64_t size_per_second = attribute->sample_rate  * size_per_frame;

    if (size_per_second == 0) {
        return 0;
    }
    return (bytes * (uint64_t)1000) / (size_per_second);
}


uint64_t getBufferLatencyUs(const stream_attribute_t *attribute, uint64_t bytes) {
    if (attribute == NULL) {
        return 0;
    }

    uint64_t size_per_frame = getSizePerFrame(attribute->audio_format, attribute->num_channels);
    uint64_t size_per_second = attribute->sample_rate  * size_per_frame;

    if (size_per_second == 0) {
        return 0;
    }
    return (bytes * (uint64_t)1000000) / (size_per_second);
}


uint64_t getFmtConvBufferSize(const stream_attribute_t *attr_from,
                              const stream_attribute_t *attr_to,
                              const uint64_t bytes_from) {
    uint64_t bytes_to = 0;
    uint64_t size_per_frame_from = 0;
    uint64_t size_per_frame_to = 0;

    if (!attr_from || !attr_to || !bytes_from) {
        return bytes_from;
    }
    if (!attr_from->num_channels || !attr_to->num_channels) {
        return bytes_from;
    }
    if (!attr_from->sample_rate || !attr_to->sample_rate) {
        return bytes_from;
    }

    if (attr_from->audio_format == attr_to->audio_format &&
        attr_from->num_channels == attr_to->num_channels &&
        attr_from->sample_rate  == attr_to->sample_rate) {
        return bytes_from;
    }

    size_per_frame_from = getSizePerFrameByAttr(attr_from);
    size_per_frame_to = getSizePerFrameByAttr(attr_to);
    if (!size_per_frame_from || !size_per_frame_to) {
        return bytes_from;
    }

    bytes_to = (bytes_from * size_per_frame_to * (uint64_t)attr_to->sample_rate) /
                            (size_per_frame_from * (uint64_t)attr_from->sample_rate);

    if ((bytes_to % size_per_frame_to) != 0) { // alignment
        bytes_to = ((bytes_to / size_per_frame_to) + 1) * size_per_frame_to;
    }

    return bytes_to;
}


void transAudFmtCfgByAttr(struct aud_fmt_cfg_t *cfg, const stream_attribute_t *attr) {
    if (!cfg || !attr) {
        return;
    }
    cfg->audio_format = attr->audio_format;
    cfg->num_channels = attr->num_channels;
    cfg->sample_rate  = attr->sample_rate;
}


void *createFmtConvHdlWrap(const stream_attribute_t *attr_src, const stream_attribute_t *attr_tgt) {
    struct aud_fmt_cfg_t src_cfg;
    struct aud_fmt_cfg_t tgt_cfg;
    void *hdl = NULL;

    if (!attr_src || !attr_tgt) {
        return NULL;
    }

    transAudFmtCfgByAttr(&src_cfg, attr_src);
    transAudFmtCfgByAttr(&tgt_cfg, attr_tgt);

    if (aud_fmt_conv_hal_create(&src_cfg, &tgt_cfg, &hdl) != 0) {
        AUD_ASSERT(0);
        hdl = NULL;
    }

    if (hdl) {
        ALOGD("fmt_conv, sample_rate: %u => %u, num_channels: %d => %d, audio_format: 0x%x => 0x%x",
              src_cfg.sample_rate,  tgt_cfg.sample_rate,
              src_cfg.num_channels, tgt_cfg.num_channels,
              src_cfg.audio_format, tgt_cfg.audio_format);
    }

    return hdl;
}


bool isNeedApplyVolume(const capture_provider_t type) {
    bool ret = false;

    /* Only real input CaptureDataprovider need to apply volume for mic mute */
    switch (type) {
    case CAPTURE_PROVIDER_NORMAL:
    case CAPTURE_PROVIDER_BT_SCO:
    case CAPTURE_PROVIDER_BT_CVSD:
    case CAPTURE_PROVIDER_USB:
    case CAPTURE_PROVIDER_TDM_RECORD:
    case CAPTURE_PROVIDER_EXTERNAL:
        ret = true;
        break;
    default:
        ret = false;
    }

    return ret;
}


int applyVolume(bool *pMicMute,
                const bool bMicMuteNew,
                bool *pMuteTransition,
                void *buffer,
                uint32_t bytes) {
    if (!pMicMute || !pMuteTransition || !buffer || !bytes) {
        return -1;
    }

    // check if need apply mute
    if (*pMicMute != bMicMuteNew) {
        *pMicMute =  bMicMuteNew;
        *pMuteTransition = false;
    }

    if (*pMicMute) {
        // do ramp down
        if (*pMuteTransition == false) {
            uint32_t count = bytes >> 1;
            float Volume_inverse = ((float)MTK_STREAMIN_VOLUEM_MAX / (float)count) * -1;
            short *pPcm = (short *)buffer;
            int ConsumeSample = 0;
            int value = 0;
            while (count) {
                value = *pPcm * (MTK_STREAMIN_VOLUEM_MAX + (Volume_inverse * ConsumeSample));
                *pPcm = clamp16(value >> MTK_STREAMIN_VOLUME_VALID_BIT);
                pPcm++;
                count--;
                ConsumeSample ++;
                //ALOGD("ApplyVolume Volume_inverse = %f ConsumeSample = %d",Volume_inverse,ConsumeSample);
            }
            *pMuteTransition = true;
        } else {
            memset(buffer, 0, bytes);
        }
    } else {
        // do ramp up
        if (*pMuteTransition == false) {
            uint32_t count = bytes >> 1;
            float Volume_inverse = ((float)MTK_STREAMIN_VOLUEM_MAX / (float)count);
            short *pPcm = (short *)buffer;
            int ConsumeSample = 0;
            int value = 0;
            while (count) {
                value = *pPcm * (Volume_inverse * ConsumeSample);
                *pPcm = clamp16(value >> MTK_STREAMIN_VOLUME_VALID_BIT);
                pPcm++;
                count--;
                ConsumeSample ++;
                //ALOGD("ApplyVolume Volume_inverse = %f ConsumeSample = %d",Volume_inverse,ConsumeSample);
            }
            *pMuteTransition = true;
        }
    }

    return 0;
}

//--------pc dump operation

struct BufferDump {
    FILE *fp;
    bool fileClosed;
    void *pBufBase;
    int ssize_t;
};

#if defined(PC_EMULATION)
HANDLE hPCMDumpThread = NULL;
HANDLE PCMDataNotifyEvent = NULL;
#else
pthread_t hPCMDumpThread = 0;
pthread_cond_t  PCMDataNotifyEvent;
pthread_mutex_t PCMDataNotifyMutex;
#endif
bool pcmDumpThreadCreated = false;

AudioLock mPCMDumpMutex; // use for PCM buffer dump

Vector<FILE *> mDumpFileVector; // vector to save current recording files
std::queue<BufferDump *> mDumpBufferQueue; // vector to save current recording data

int mSleepTime = 2;

const char *transferAudioFormatToDumpString(const audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_PCM_8_BIT:
        return "8bit";
    case AUDIO_FORMAT_PCM_16_BIT:
        return "16bit";
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        return "24bit_pack";
    case AUDIO_FORMAT_PCM_8_24_BIT:
        return "8_24bit";
    case AUDIO_FORMAT_PCM_32_BIT:
        return "32bit";
    case AUDIO_FORMAT_PCM_FLOAT:
        return "float";
    default:
        ALOGE("%s: invalid audio format %#x", __FUNCTION__, format);
        return "unknown";
    }
}

void *PCMDumpThread(void *arg);

int AudiocheckAndCreateDirectory(const char *pC) {
    char tmp[PATH_MAX];
    unsigned int i = 0;
    while (*pC) {
        tmp[i] = *pC;
        if (*pC == '/' && i) {
            tmp[i] = '\0';
            if (access(tmp, F_OK) != 0) {
                if (mkdir(tmp, 0770) == -1) {
                    ALOGE("AudioDumpPCM: mkdir error! %s\n", (char *)strerror(errno));
                    return -1;
                }
            }
            tmp[i] = '/';
        }
        i++;
        pC++;
    }
    return 0;
}

bool bDumpStreamOutFlg = false;
bool bDumpStreamInFlg = false;

FILE *AudioOpendumpPCMFile(const char *filepath, const char *propty) {
#ifdef MTK_AUDIO_HAL_DUMP_DISABLE
    UNUSED(filepath);
    UNUSED(propty);
#else
    char value[PROPERTY_VALUE_MAX] = {0};
    int ret;
    property_get(propty, value, "0");
    int bflag = atoi(value);

    if (!bflag) {
        if (!strcmp(propty, streamout_propty) && bDumpStreamOutFlg) {
            bflag = 1;
        } else if (!strcmp(propty, streamin_propty) && bDumpStreamInFlg) {
            bflag = 1;
        }
    }

    if (bflag) {
        ret = AudiocheckAndCreateDirectory(filepath);
        if (ret < 0) {
            ALOGE("%s(), dumpPCMData checkAndCreateDirectory() fail!!!", __FUNCTION__);
        } else {
            FILE *fp = fopen(filepath, "wb");
            if (fp != NULL) {

                AL_LOCK(mPCMDumpMutex);
                //ALOGD("AudioOpendumpPCMFile file=%p, pBD=%p",fp, pBD);
                mDumpFileVector.add(fp);
                /*for (size_t i = 0; i < mDumpFileVector.size() ; i++)
                {
                    ALOGD("AudioOpendumpPCMFile i=%zu, handle=%p",i,mDumpFileVector.itemAt(i));
                }*/

                if (!pcmDumpThreadCreated) {
#if defined(PC_EMULATION)
                    PCMDataNotifyEvent = CreateEvent(NULL, TRUE, FALSE, "PCMDataNotifyEvent");
                    hPCMDumpThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)PCMDumpThread, NULL, 0, 0);
                    if (hPCMDumpThread == 0) {
                        ALOGE("hPCMDumpThread create fail!!!");
                    } else {
                        ALOGD("hPCMDumpThread=%lu created", hPCMDumpThread);
                        pcmDumpThreadCreated = true;
                    }
#else
                    //create PCM data dump thread here
                    int ret;
                    ret = pthread_create(&hPCMDumpThread, NULL, PCMDumpThread, NULL);
                    if (ret != 0) {
                        ALOGE("hPCMDumpThread create fail!!!");
                    } else {
                        ALOGD("hPCMDumpThread created");
                        pcmDumpThreadCreated = true;
                    }
                    ret = pthread_cond_init(&PCMDataNotifyEvent, NULL);
                    if (ret != 0) {
                        ALOGE("PCMDataNotifyEvent create fail!!!");
                    }

                    ret = pthread_mutex_init(&PCMDataNotifyMutex, NULL);
                    if (ret != 0) {
                        ALOGE("PCMDataNotifyMutex create fail!!!");
                    }
#endif
                }
                AL_UNLOCK(mPCMDumpMutex);
                return fp;
            } else {
                ALOGE("%s(), file open %s fail", __FUNCTION__, filepath);
            }
        }
    }
#endif
    return NULL;
}

void AudioCloseDumpPCMFile(FILE  *file) {
#ifdef MTK_AUDIO_HAL_DUMP_DISABLE
    UNUSED(file);
#else
    if (file != NULL) {
        AL_LOCK(mPCMDumpMutex);
        //ALOGD("AudioCloseDumpPCMFile file=%p, HandleCount=%d",file,mDumpFileVector.size());
        if (mDumpFileVector.size()) {
            for (size_t i = 0; i < mDumpFileVector.size(); i++) {
                //ALOGD("AudioCloseDumpPCMFile i=%d, handle=%p",i,mDumpFileVector.itemAt(i));
                if (file == mDumpFileVector.itemAt(i)) {
                    // Add file closed notice to mDumpBufferQueue
                    BufferDump *newInBuffer = new BufferDump;
                    newInBuffer->fp = file;
                    newInBuffer->pBufBase = NULL;
                    newInBuffer->ssize_t = 0;
                    newInBuffer->fileClosed = true;
                    mDumpBufferQueue.push(newInBuffer);

                    mDumpFileVector.removeAt(i);
                }
            }
        }
        AL_UNLOCK(mPCMDumpMutex);

        if (!pcmDumpThreadCreated) {
            if (fclose(file)) {
                ALOGE("%s(), fclose file error", __FUNCTION__);
            }
        }
        file = NULL;
    } else {
        ALOGE("AudioCloseDumpPCMFile file== NULL");
    }
#endif
}

void UpdateWaveHeader(FILE *fp, uint32_t ck_size, audio_format_t format, uint16_t ch_count, uint32_t sample_rate)
{
    size_t write_cnt = 0;

    ALOGV("%s(), ck_size = %d, format = %d, channels = %d, sample_rate = %d",
          __FUNCTION__, ck_size, format, ch_count, sample_rate);

    WavFormatHeader wavHeader;
    void* tmpBuffer = malloc(WAVEFORMATEX_SIZE);
    if (!tmpBuffer) {
        ALOGE("%s(): malloc fail!", __FUNCTION__);
        return;
    }

    if (format == AUDIO_FORMAT_PCM_FLOAT) {
        wavHeader.WaveFormatEx.wFormatTag = 3; // IEEE Float
        wavHeader.WaveFormatEx.wBitsPerSample = 32;
    } else {
        wavHeader.WaveFormatEx.wFormatTag = 1; // PCM

        if (format == AUDIO_FORMAT_PCM_8_BIT) {
            wavHeader.WaveFormatEx.wBitsPerSample = 8;
        } else if (format == AUDIO_FORMAT_PCM_16_BIT) {
            wavHeader.WaveFormatEx.wBitsPerSample = 16;
        } else if (format == AUDIO_FORMAT_PCM_24_BIT_PACKED) {
            wavHeader.WaveFormatEx.wBitsPerSample = 24;
        } else if (format == AUDIO_FORMAT_PCM_8_24_BIT ||
                   format == AUDIO_FORMAT_PCM_32_BIT) {
            wavHeader.WaveFormatEx.wBitsPerSample = 32;
        }
    }

    wavHeader.cksize                       = ck_size + WAVEFORMATEX_SIZE - 8;
    wavHeader.Datacksize                   = ck_size;
    wavHeader.WaveFormatEx.nChannels       = ch_count;
    wavHeader.WaveFormatEx.nSamplesPerSec  = sample_rate;
    wavHeader.WaveFormatEx.nAvgBytesPerSec = wavHeader.WaveFormatEx.nSamplesPerSec * wavHeader.WaveFormatEx.nChannels
                                             * wavHeader.WaveFormatEx.wBitsPerSample / 8;
    wavHeader.WaveFormatEx.nBlockAlign     = wavHeader.WaveFormatEx.nChannels * wavHeader.WaveFormatEx.wBitsPerSample / 8;

    int pos = 0;
    memcpy((char*)tmpBuffer + pos, &wavHeader.ckID,            4); pos += 4;
    memcpy((char*)tmpBuffer + pos, &wavHeader.cksize,          4); pos += 4;
    memcpy((char*)tmpBuffer + pos, &wavHeader.WAVEID,          4); pos += 4;
    memcpy((char*)tmpBuffer + pos, &wavHeader.FormatckID,      4); pos += 4;
    memcpy((char*)tmpBuffer + pos, &wavHeader.Formatcksize,    4); pos += 4;
    memcpy((char*)tmpBuffer + pos, &wavHeader.WaveFormatEx,   16); pos += 16;
    memcpy((char*)tmpBuffer + pos, &wavHeader.DataID,          4); pos += 4;
    memcpy((char*)tmpBuffer + pos, &wavHeader.Datacksize,      4); pos += 4;

    AL_LOCK(mPCMDumpMutex);

    if (fseek(fp, 0, SEEK_SET) != 0) {
        ALOGE("%s(): fseek SEEK_SET fail!", __FUNCTION__);
    } else {
        write_cnt = fwrite(tmpBuffer, WAVEFORMATEX_SIZE, 1, fp);
        if (write_cnt != 1) {
            ALOGE("%s(), fwrite error, write size %zu", __FUNCTION__, write_cnt);
        }
        if (fseek(fp, 0, SEEK_END) != 0) {
            ALOGE("%s(): fseek SEEK_END fail!", __FUNCTION__);
        }
    }
    free(tmpBuffer);
    tmpBuffer = NULL;
    AL_UNLOCK(mPCMDumpMutex);

}

void AudioDumpPCMData(void *buffer, uint32_t bytes, FILE  *file) {
#ifdef MTK_AUDIO_HAL_DUMP_DISABLE
    UNUSED(buffer);
    UNUSED(bytes);
    UNUSED(file);
#else
    size_t write_cnt = 0;

    if (pcmDumpThreadCreated) {
        AL_LOCK(mPCMDumpMutex);
        if (mDumpFileVector.size()) {
            for (size_t i = 0; i < mDumpFileVector.size() ; i++) {
                if (file == mDumpFileVector.itemAt(i)) {
                    BufferDump *newInBuffer = new BufferDump;
                    newInBuffer->pBufBase = (short *) malloc(bytes);
                    ASSERT(newInBuffer->pBufBase != NULL);
                    memcpy(newInBuffer->pBufBase, buffer, bytes);
                    newInBuffer->ssize_t = bytes;
                    newInBuffer->fp = file;
                    newInBuffer->fileClosed = false;
                    mDumpBufferQueue.push(newInBuffer);

                    if (mSleepTime == -1) { //need to send event
#if defined(PC_EMULATION)
                        SetEvent(PCMDataNotifyEvent);
#else
                        pthread_mutex_lock(&PCMDataNotifyMutex);
                        pthread_cond_signal(&PCMDataNotifyEvent);
                        pthread_mutex_unlock(&PCMDataNotifyMutex);
#endif
                    }
                }
            }
        }
        AL_UNLOCK(mPCMDumpMutex);
    } else { //if no dump thread, just write the data
         write_cnt = fwrite((void *)buffer, sizeof(char), bytes, file);
        if (write_cnt != bytes) {
            ALOGE("%s(), fwrite error, write size %zu", __FUNCTION__, write_cnt);
        }
    }
#endif
}

void *PCMDumpThread(void *arg __unused) {
    bool bHasdata = false;
    int iNoDataCount = 0;
    BufferDump *bp = NULL;
    size_t write_cnt = 0;

    while (1) {
        bHasdata = false;
        bp = NULL;

        AL_LOCK(mPCMDumpMutex);
        if (mDumpBufferQueue.size()) {
            bp = mDumpBufferQueue.front();
            mDumpBufferQueue.pop();
            bHasdata = true;
        }
        AL_UNLOCK(mPCMDumpMutex);

        if (bp != NULL) {
            if (bp->pBufBase && bp->fp) {
                write_cnt = fwrite(bp->pBufBase, bp->ssize_t, 1, bp->fp);
                if (write_cnt != 1) {
                    AUD_LOG_E("%s(), fwrite error, write size %zu", __FUNCTION__, write_cnt);
                }
                free(bp->pBufBase);
            }
            if (bp->fileClosed && bp->fp) {
                if (fclose(bp->fp)) {
                    ALOGE("%s(), fclose bp->fp error", __FUNCTION__);
                }
            }
            delete bp;
        }

        if (!bHasdata) {
            iNoDataCount++;
            if (iNoDataCount >= 1000) {
                mSleepTime = -1;
                ALOGD("PCMDumpThread, wait for new data dump\n");
#if defined(PC_EMULATION)
                WaitForSingleObject(PCMDataNotifyEvent, INFINITE);
                ResetEvent(PCMDataNotifyEvent);
#else
                pthread_mutex_lock(&PCMDataNotifyMutex);
                pthread_cond_wait(&PCMDataNotifyEvent, &PCMDataNotifyMutex);
                pthread_mutex_unlock(&PCMDataNotifyMutex);
                ALOGD("PCMDumpThread, PCM data dump again\n");
#endif
            } else {
                mSleepTime = 10;
                usleep(mSleepTime * 1000);
            }
        } else {
            iNoDataCount = 0;
            //mSleepTime = 2;
            //usleep(mSleepTime * 1000);
        }
        /*
                if((mDumpFileVector.size() == 0) && (mDumpBufferQueue.size() == 0))
                {
                    ALOGD( "PCMDumpThread exit, no dump handle real");
                    hPCMDumpThread = NULL;
                    pthread_exit(NULL);
                    return 0;
                }*/

    }

    ALOGD("PCMDumpThread exit");

    pcmDumpThreadCreated = false;
    pthread_exit(NULL);
    return 0;
}

#define CVSD_LOOPBACK_BUFFER_SIZE (960 * 10)//BTSCO_CVSD_RX_FRAME*SCO_RX_PCM8K_BUF_SIZE * 10
#ifdef MTK_CVSD_LOOPBACK_SUPPORT
static uint8_t cvsd_temp_buffer[CVSD_LOOPBACK_BUFFER_SIZE]; //temp buf only for dump to file
#endif
static uint32_t cvsd_temp_w = 0;
static uint32_t cvsd_temp_r = 0;
const static uint32_t cvsd_temp_size = CVSD_LOOPBACK_BUFFER_SIZE;

void CVSDLoopbackGetWriteBuffer(uint8_t **buffer, uint32_t *buf_len) { // in bytes
#ifdef MTK_CVSD_LOOPBACK_SUPPORT
    int32_t count;

    if (cvsd_temp_r > cvsd_temp_w) {
        count = cvsd_temp_r - cvsd_temp_w - 8;
    } else {
        count = cvsd_temp_size - cvsd_temp_w;
    }

    *buffer = (uint8_t *)&cvsd_temp_buffer[cvsd_temp_w];
    *buf_len = (count > 0) ? count : 0;
    //    ALOGD("BT_SW_CVSD CODEC LOOPBACK record thread: CVSDLoopbackGetWriteBuffer: buf_len: %d, cvsd_temp_buffer %p", *buf_len, cvsd_temp_buffer);
    ALOGD("%s(), cvsd_temp_w %u, cvsd_temp_r %u, cvsd_temp_buffer %p, ret buffer %p, buf_len %u",
          __FUNCTION__, cvsd_temp_w, cvsd_temp_r, cvsd_temp_buffer, *buffer, *buf_len);
#else
    UNUSED(buffer);
    UNUSED(buf_len);
#endif
}

void CVSDLoopbackGetReadBuffer(uint8_t **buffer, uint32_t *buf_len) { // in bytes
#ifdef MTK_CVSD_LOOPBACK_SUPPORT
    int32_t count;

    if (cvsd_temp_w >= cvsd_temp_r) {
        count = cvsd_temp_w - cvsd_temp_r;
    } else {
        count = cvsd_temp_size - cvsd_temp_r;
    }

    *buffer = (uint8_t *)&cvsd_temp_buffer[cvsd_temp_r];
    *buf_len = count;
    ALOGD("%s(), cvsd_temp_w %u, cvsd_temp_r %u, cvsd_temp_buffer %p, ret buffer %p, buf_len %u",
          __FUNCTION__, cvsd_temp_w, cvsd_temp_r, cvsd_temp_buffer, *buffer, *buf_len);

    //    ALOGD("BT_SW_CVSD CODEC LOOPBACK record thread: CVSDLoopbackGetReadBuffer: buf_len: %d, cvsd_temp_buffer %p", count, cvsd_temp_buffer);
#else
    UNUSED(buffer);
    UNUSED(buf_len);
#endif
}

void CVSDLoopbackReadDataDone(uint32_t len) { // in bytes
#ifdef MTK_CVSD_LOOPBACK_SUPPORT
    cvsd_temp_r += len;
    if (cvsd_temp_r >= cvsd_temp_size) {
        cvsd_temp_r = 0;
    }
    //    ALOGD("BT_SW_CVSD CODEC LOOPBACK record thread: CVSDLoopbackReadDataDone: len: %d", len);
    ALOGD("%s(), cvsd_temp_w %u, cvsd_temp_r %u, cvsd_temp_buffer %p, len %u",
          __FUNCTION__, cvsd_temp_w, cvsd_temp_r, cvsd_temp_buffer, len);
#else
    UNUSED(len);
#endif
}

void CVSDLoopbackWriteDataDone(uint32_t len) { // in bytes
#ifdef MTK_CVSD_LOOPBACK_SUPPORT
    cvsd_temp_w += len;
    if (cvsd_temp_w >= cvsd_temp_size) {
        cvsd_temp_w = 0;
    }
    ALOGD("%s(), cvsd_temp_w %u, cvsd_temp_r %u, cvsd_temp_buffer %p, len %u",
          __FUNCTION__, cvsd_temp_w, cvsd_temp_r, cvsd_temp_buffer, len);
#else
    UNUSED(len);
#endif
}

void CVSDLoopbackResetBuffer(void) { // in bytes
#ifdef MTK_CVSD_LOOPBACK_SUPPORT
    memset(cvsd_temp_buffer, 0, CVSD_LOOPBACK_BUFFER_SIZE);
    cvsd_temp_w = CVSD_LOOPBACK_BUFFER_SIZE / 2; //if 0, deadlock
    cvsd_temp_r = 0;
    ALOGD("BT_SW_CVSD CODEC LOOPBACK record thread: CVSDLoopbackResetBuffer");
#endif
}

int32_t CVSDLoopbackGetFreeSpace(void) {
    int32_t count;

    if (cvsd_temp_r > cvsd_temp_w) {
        count = cvsd_temp_r - cvsd_temp_w - 8;
    } else {
        count = cvsd_temp_size + cvsd_temp_r - cvsd_temp_w - 8;
    }

    return (count > 0) ? count : 0; // free size in byte
}

int32_t CVSDLoopbackGetDataCount(void) {
    return (cvsd_temp_size - CVSDLoopbackGetFreeSpace() - 8);
}

const char* PROPERTY_KEY_2IN1SPK_ON = "vendor.persist.af.feature.2in1spk";
const char* PROPERTY_KEY_VIBSPK_ON = "vendor.persist.af.feature.vibspk";
static const char *g_phone_mic_propty = "persist.vendor.rm.debug.phonemic";
static const char *g_headset_mic_propty = "persist.vendor.rm.debug.headsetmic";

bool IsAudioSupportFeature(int dFeatureOption) {
    bool bSupportFlg = false;
    char stForFeatureUsage[PROPERTY_VALUE_MAX] = {0};
    bool dmic_usage = false;
    bool property_set = false;

    switch (dFeatureOption) {
    case AUDIO_SUPPORT_DMIC: {
        // PHONE_MIC_MODE/HEADSET_MIC_MODE defined in audio_custom_exp.h
#ifdef PHONE_MIC_MODE
        bSupportFlg = ((PHONE_MIC_MODE == AUDIO_MIC_MODE_DMIC) || (PHONE_MIC_MODE == AUDIO_MIC_MODE_DMIC_LP) ||
                       (HEADSET_MIC_MODE == AUDIO_MIC_MODE_DMIC) || (HEADSET_MIC_MODE == AUDIO_MIC_MODE_DMIC_LP)) ?
                       true : false;
#else
        bSupportFlg = false;
#endif
        property_get(g_phone_mic_propty, stForFeatureUsage, "0");
        if (atoi(stForFeatureUsage) != 0) {
            property_set = true;
            dmic_usage = (atoi(stForFeatureUsage) == AUDIO_MIC_MODE_DMIC) ||
                         (atoi(stForFeatureUsage) == AUDIO_MIC_MODE_DMIC_LP) ? true : false;
        }
        property_get(g_headset_mic_propty, stForFeatureUsage, "0");
        if (atoi(stForFeatureUsage) != 0) {
            property_set = true;
            dmic_usage |= (atoi(stForFeatureUsage) == AUDIO_MIC_MODE_DMIC) ||
                          (atoi(stForFeatureUsage) == AUDIO_MIC_MODE_DMIC_LP) ? true : false;
        }

        if (property_set) {
            bSupportFlg = dmic_usage;
        }
        ALOGV("%s AUDIO_SUPPORT_DMIC bSupportFlg[%d]", __FUNCTION__, bSupportFlg);

        break;
    }
    case AUDIO_SUPPORT_2IN1_SPEAKER: {
#ifdef USING_2IN1_SPEAKER
        property_get(PROPERTY_KEY_2IN1SPK_ON, stForFeatureUsage, "1"); //"1": default on
#else
        property_get(PROPERTY_KEY_2IN1SPK_ON, stForFeatureUsage, "0"); //"0": default off
#endif
        bSupportFlg = (stForFeatureUsage[0] == '0') ? false : true;
        //ALOGD("IsAudioSupportFeature AUDIO_SUPPORT_2IN1_SPEAKER [%d]\n",bSupportFlg);

        break;
    }
    case AUDIO_SUPPORT_VIBRATION_SPEAKER: {
#ifdef MTK_VIBSPK_SUPPORT
        property_get(PROPERTY_KEY_VIBSPK_ON, stForFeatureUsage, "1"); //"1": default on
#else
        property_get(PROPERTY_KEY_VIBSPK_ON, stForFeatureUsage, "0"); //"0": default off
#endif
        bSupportFlg = (stForFeatureUsage[0] == '0') ? false : true;
        //ALOGD("IsAudioSupportFeature AUDIO_SUPPORT_VIBRATION_SPEAKER [%d]\n",bSupportFlg);

        break;
    }
    case AUDIO_SUPPORT_EXTERNAL_BUILTIN_MIC: {
#ifdef MTK_EXTERNAL_BUILTIN_MIC_SUPPORT
        bSupportFlg = true;
#else
        bSupportFlg = false;
#endif
        break;
    }
    case AUDIO_SUPPORT_EXTERNAL_ECHO_REFERENCE: {
#ifdef MTK_EXTERNAL_SPEAKER_DAC_SUPPORT
        bSupportFlg = true;
#else
        bSupportFlg = false;
#endif
        break;
    }
    case AUDIO_SUPPORT_VOW_DUAL_MIC: {
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
        if (appIsFeatureOptionEnabled("MTK_VOW_DUAL_MIC_SUPPORT")) {
            bSupportFlg = true;
        } else
#endif
        {
            bSupportFlg = false;
        }
        break;
    }
    default:
        break;
    }

    return bSupportFlg;
}

bool isAdspOptionEnable(void)
{
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    if (appIsFeatureOptionEnabled("MTK_AUDIODSP_SUPPORT")) {
          return true;
    }
#endif

    return false;
}

bool isAudioIpiEnable(void)
{
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    if (appIsFeatureOptionEnabled("MTK_AUDIODSP_SUPPORT")) {
          return true;
    }
#endif

    return false;
}

bool isAudioIpiDmaEnable(void)
{
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    if (appIsFeatureOptionEnabled("MTK_AUDIODSP_SUPPORT")) {
          return true;
    }
#endif

    return false;
}

bool isAdspServiceEnable(void)
{
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    if (appIsFeatureOptionEnabled("MTK_AUDIODSP_SUPPORT")) {
          return true;
    }
#endif

    return false;
}

bool isAdspRecoveryEnable(void)
{
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    if (appIsFeatureOptionEnabled("MTK_AUDIODSP_SUPPORT")) {
          return true;
    }
#endif

    return false;
}

bool isBtSpkDevice(audio_devices_t devices) {
    return (devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & AUDIO_DEVICE_OUT_ALL_SCO);
}

bool isa2dpSpkDevice(audio_devices_t devices) {
    return (devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & AUDIO_DEVICE_OUT_ALL_A2DP);
}

bool isUsbSpkDevice(audio_devices_t devices) {
    return (devices & AUDIO_DEVICE_OUT_SPEAKER) && (devices & AUDIO_DEVICE_OUT_ALL_USB);
}

bool isEarphoneSpkDevice(audio_devices_t devices) {
    return (devices & AUDIO_DEVICE_OUT_SPEAKER) &&
           (devices & AUDIO_DEVICE_OUT_WIRED_HEADSET || devices & AUDIO_DEVICE_OUT_WIRED_HEADPHONE);
}

bool isPmicInputDevice(const audio_devices_t device) {
    return (device & ~AUDIO_DEVICE_BIT_IN) &
           (AUDIO_DEVICE_IN_BUILTIN_MIC | AUDIO_DEVICE_IN_BACK_MIC | AUDIO_DEVICE_IN_WIRED_HEADSET);
}

timespec GetSystemTime(bool print) {
    struct timespec systemtime;
    int rc;
    rc = clock_gettime(CLOCK_MONOTONIC, &systemtime);
    if (rc != 0) {
        systemtime.tv_sec  = 0;
        systemtime.tv_nsec = 0;
        ALOGD("%s() clock_gettime error", __FUNCTION__);
    }
    if (print == true) {
        ALOGD("%s(), sec %ld nsec %ld", __FUNCTION__, systemtime.tv_sec, systemtime.tv_nsec);
    }

    return systemtime;
}

uint32_t GetMicDeviceMode(uint32_t mic_category) { //0: phonemic, 1: headsetmic
    char value[PROPERTY_VALUE_MAX] = {0};
    int ret, bflag;
    uint32_t mPhoneMicMode, mHeadsetMicMode;

    if (mic_category == 0) {
#ifdef PHONE_MIC_MODE //defined in audio_custom_exp.h
        mPhoneMicMode = PHONE_MIC_MODE;
        ALOGD("PHONE_MIC_MODE defined!, mPhoneMicMode = %d", mPhoneMicMode);
#else
        mPhoneMicMode = AUDIO_MIC_MODE_DCC;
#endif
        // control by setprop
        property_get(g_phone_mic_propty, value, "0");
        bflag = atoi(value);
        if (bflag != 0) {
            mPhoneMicMode = bflag;
            ALOGD("mPhoneMicMode getprop, mPhoneMicMode = %d", mPhoneMicMode);
        }
        return mPhoneMicMode;
    } else if (mic_category == 1) {
#ifdef HEADSET_MIC_MODE //defined in audio_custom_exp.h
        mHeadsetMicMode = HEADSET_MIC_MODE;
        ALOGD("HEADSET_MIC_MODE defined!, mHeadsetMicMode = %d", mHeadsetMicMode);
#else
        mHeadsetMicMode = AUDIO_MIC_MODE_DCC;
#endif
        // control by setprop
        property_get(g_headset_mic_propty, value, "0");
        bflag = atoi(value);
        if (bflag != 0) {
            mHeadsetMicMode = bflag;
            ALOGD("mHeadsetMicMode getprop, mHeadsetMicMode = %d", mHeadsetMicMode);
        }
        return mHeadsetMicMode;
    } else {
        ALOGE("%s() wrong mic_category!!!", __FUNCTION__);
        return 0;
    }
}

unsigned int FormatTransfer(int SourceFormat, int TargetFormat, void *Buffer, unsigned int mReadBufferSize) {
    unsigned mReformatSize = 0;
    int *srcbuffer = (int *)Buffer;
    short *dstbuffer = (short *)Buffer;
    if (SourceFormat == PCM_FORMAT_S32_LE && TargetFormat == PCM_FORMAT_S16_LE) {
        short temp = 0;
        while (mReadBufferSize) {
            temp = (short)((*srcbuffer) / 256);
            *dstbuffer = temp;
            srcbuffer++;
            dstbuffer++;
            mReadBufferSize -= sizeof(int);
            mReformatSize += sizeof(short);
        }
    } else {
        mReformatSize = mReadBufferSize;
    }
    return mReformatSize;
}

#define FACTORY_BOOT 4
#define ATE_FACTORY_BOOT 6
#define BOOTMODE_PATH "/proc/device-tree/chosen/atag,boot"

int readSys_int(char const *path) {
    int fd;

    if (path == NULL) {
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd >= 0) {
        uint32_t buffer[8] = {0};
        int amt = read(fd, buffer, sizeof(uint32_t)*8);
        if (amt > 0) {
            ALOGD("boot mode struct:size=%d,tag=%d,mode=%d\n", buffer[0],buffer[1],buffer[2]);
            close(fd);
            return buffer[2];
        } else {
            ALOGD("read boot mode struct:size <=0");
            close(fd);
        }
    }

    ALOGE("Fail to open boot mode file  %s\n", path);
    return -errno;
}

int InFactoryMode() {
    int bootMode;
    int ret = false;

    bootMode = readSys_int(BOOTMODE_PATH);
    ALOGD("bootMode = %d", bootMode);
    if (FACTORY_BOOT == bootMode) {
        ALOGD("Factory mode boot!\n");
        ret = true;
    } else if (ATE_FACTORY_BOOT == bootMode) {
        ALOGD("ATE Factory mode boot!\n");
        ret = true;
    } else {
        ret = false;
        ALOGD("Unsupported factory mode!\n");
    }
    return ret;
}

int In64bitsProcess() {
    char *platform = (char *)getauxval(AT_PLATFORM);
    if (strcmp(platform, platform_arch) == 0) {
        return true;
    }
    return false;
}

inline void *openAudioRelatedLib(const char *filepath) {
    if (filepath == NULL) {
        ALOGE("%s null input parameter", __FUNCTION__);
        return NULL;
    } else {
        if (access(filepath, R_OK) == 0) {
            return dlopen(filepath, RTLD_NOW);
        } else {
            ALOGE("%s filepath %s doesn't exist", __FUNCTION__, filepath);
            return NULL;
        }
    }
}
inline bool openAudioComponentEngine(void) {
    if (g_AudioComponentEngineHandle == NULL) {
        g_CreateMtkAudioBitConverter = NULL;
        g_CreateMtkAudioSrc = NULL;
        g_CreateMtkAudioLoud = NULL;
        g_DestroyMtkAudioBitConverter = NULL;
        g_DestroyMtkAudioSrc = NULL;
        g_DestroyMtkAudioLoud = NULL;
        g_AudioComponentEngineHandle = openAudioRelatedLib(AUDIO_COMPONENT_ENGINE_LIB_VENDOR_PATH);
        if (g_AudioComponentEngineHandle == NULL) {
            g_AudioComponentEngineHandle  = openAudioRelatedLib(AUDIO_COMPONENT_ENGINE_LIB_PATH);
            return (g_AudioComponentEngineHandle == NULL) ? false : true;
        }
    }
    return true;
}

inline void closeAudioComponentEngine(void) {
    if (g_AudioComponentEngineHandle != NULL) {
        dlclose(g_AudioComponentEngineHandle);
        g_AudioComponentEngineHandle = NULL;
        g_CreateMtkAudioBitConverter = NULL;
        g_CreateMtkAudioSrc = NULL;
        g_CreateMtkAudioLoud = NULL;
        g_DestroyMtkAudioBitConverter = NULL;
        g_DestroyMtkAudioSrc = NULL;
        g_DestroyMtkAudioLoud = NULL;
    }
}

MtkAudioBitConverterBase *newMtkAudioBitConverter(uint32_t sampling_rate, uint32_t channel_num, BCV_PCM_FORMAT format) {
    if (!openAudioComponentEngine()) {
        return NULL;
    }

    if (g_CreateMtkAudioBitConverter == NULL) {
        g_CreateMtkAudioBitConverter = (create_AudioBitConverter *)dlsym(g_AudioComponentEngineHandle, "createMtkAudioBitConverter");
        const char *dlsym_error1 = dlerror();
        if (g_CreateMtkAudioBitConverter == NULL) {
            ALOGE("Error -dlsym createMtkAudioBitConverter fail");
            closeAudioComponentEngine();
            return NULL;
        }
    }
    ALOGV("%p g_CreateMtkAudioBitConverter %p", g_AudioComponentEngineHandle, g_CreateMtkAudioBitConverter);
    return g_CreateMtkAudioBitConverter(sampling_rate, channel_num, format);
}

MtkAudioSrcBase *newMtkAudioSrc(uint32_t input_SR, uint32_t input_channel_num, uint32_t output_SR, uint32_t output_channel_num, SRC_PCM_FORMAT format) {
    if (!openAudioComponentEngine()) {
        return NULL;
    }

    if (g_CreateMtkAudioSrc == NULL) {
        g_CreateMtkAudioSrc = (create_AudioSrc *)dlsym(g_AudioComponentEngineHandle, "createMtkAudioSrc");
        const char *dlsym_error1 = dlerror();
        if (g_CreateMtkAudioSrc == NULL) {
            ALOGE("Error -dlsym createMtkAudioSrc fail");
            closeAudioComponentEngine();
            return NULL;
        }
    }
    ALOGV("%p g_CreateMtkAudioSrc %p", g_AudioComponentEngineHandle, g_CreateMtkAudioSrc);
    return g_CreateMtkAudioSrc(input_SR, input_channel_num, output_SR, output_channel_num, format);
}

MtkAudioLoudBase *newMtkAudioLoud(uint32_t eFLTtype, bool bFastTrack) {
    if (!openAudioComponentEngine()) {
        return NULL;
    }

    if (g_CreateMtkAudioLoud == NULL) {
        g_CreateMtkAudioLoud = (create_AudioLoud *)dlsym(g_AudioComponentEngineHandle, "createMtkAudioLoud");
        const char *dlsym_error1 = dlerror();
        if (g_CreateMtkAudioLoud == NULL) {
            ALOGE("Error -dlsym createMtkAudioLoud fail");
            closeAudioComponentEngine();
            return NULL;
        }
    }
    ALOGV("%p g_CreateMtkAudioLoud %p", g_AudioComponentEngineHandle, g_CreateMtkAudioLoud);
    return g_CreateMtkAudioLoud(eFLTtype, bFastTrack);
}

MtkAudioDcRemoveBase *newMtkDcRemove() {
    if (!openAudioComponentEngine()) {
        ALOGD("openAudioComponentEngine fail");
        return NULL;
    }

    if (g_CreateMtkDcRemove == NULL) {
        g_CreateMtkDcRemove = (create_DcRemove *)dlsym(g_AudioComponentEngineHandle, "createMtkDcRemove");
        const char *dlsym_error1 = dlerror();
        if (g_CreateMtkDcRemove == NULL) {
            ALOGE("Error -dlsym createMtkDcRemove fail");
            closeAudioComponentEngine();
            return NULL;
        }
    }
    ALOGV("%p g_CreateMtkDcRemove %p", g_AudioComponentEngineHandle, g_CreateMtkDcRemove);
    return g_CreateMtkDcRemove();
}

void deleteMtkAudioBitConverter(MtkAudioBitConverterBase *pObject) {
    if (!openAudioComponentEngine()) {
        return;
    }

    if (g_DestroyMtkAudioBitConverter == NULL) {
        g_DestroyMtkAudioBitConverter = (destroy_AudioBitConverter *)dlsym(g_AudioComponentEngineHandle, "destroyMtkAudioBitConverter");
        const char *dlsym_error1 = dlerror();
        if (g_DestroyMtkAudioBitConverter == NULL) {
            ALOGE("Error -dlsym destroyMtkAudioBitConverter fail");
            closeAudioComponentEngine();
            return;
        }
    }
    ALOGV("%p g_DestroyMtkAudioBitConverter %p", g_AudioComponentEngineHandle, g_DestroyMtkAudioBitConverter);
    g_DestroyMtkAudioBitConverter(pObject);
    return;
}

void deleteMtkAudioSrc(MtkAudioSrcBase *pObject) {
    if (!openAudioComponentEngine()) {
        return;
    }

    if (g_DestroyMtkAudioSrc == NULL) {
        g_DestroyMtkAudioSrc = (destroy_AudioSrc *)dlsym(g_AudioComponentEngineHandle, "destroyMtkAudioSrc");
        const char *dlsym_error1 = dlerror();
        if (g_DestroyMtkAudioSrc == NULL) {
            ALOGE("Error -dlsym destroyMtkAudioSrc fail");
            closeAudioComponentEngine();
            return;
        }
    }
    ALOGV("%p g_DestroyMtkAudioSrc %p", g_AudioComponentEngineHandle, g_DestroyMtkAudioSrc);
    g_DestroyMtkAudioSrc(pObject);
}

void deleteMtkAudioLoud(MtkAudioLoudBase *pObject) {
    if (!openAudioComponentEngine()) {
        return;
    }

    if (g_DestroyMtkAudioLoud == NULL) {
        g_DestroyMtkAudioLoud = (destroy_AudioLoud *)dlsym(g_AudioComponentEngineHandle, "destroyMtkAudioLoud");
        const char *dlsym_error1 = dlerror();
        if (g_DestroyMtkAudioLoud == NULL) {
            ALOGE("Error -dlsym destroyMtkAudioLoud fail");
            closeAudioComponentEngine();
            return;
        }
    }
    ALOGV("%p g_DestroyMtkAudioLoud %p", g_AudioComponentEngineHandle, g_DestroyMtkAudioLoud);
    g_DestroyMtkAudioLoud(pObject);
    return;
}

void deleteMtkDcRemove(MtkAudioDcRemoveBase *pObject) {
    if (!openAudioComponentEngine()) {
        return;
    }

    if (g_DestroyMtkDcRemove == NULL) {
        g_DestroyMtkDcRemove = (destroy_DcRemove *)dlsym(g_AudioComponentEngineHandle, "destroyMtkAudioDcRemove");
        const char *dlsym_error1 = dlerror();
        if (g_DestroyMtkDcRemove == NULL) {
            ALOGE("Error -dlsym destroyMtkDcRemove fail");
            closeAudioComponentEngine();
            return;
        }
    }
    ALOGV("%p g_DestroyMtkAudioLoud %p", g_AudioComponentEngineHandle, g_DestroyMtkDcRemove);
    g_DestroyMtkDcRemove(pObject);
    return;
}

inline bool openAudioCompensationFilter(void) {
    if (g_AudioCompensationFilterHandle == NULL) {
        g_setAudioCompFltCustParamFrom = NULL;
        g_getAudioCompFltCustParamFrom = NULL;
        g_AudioCompensationFilterHandle = openAudioRelatedLib(AUDIO_COMPENSATION_FILTER_LIB_VENDOR_PATH);
        if (g_AudioCompensationFilterHandle == NULL) {
            g_AudioCompensationFilterHandle  = openAudioRelatedLib(AUDIO_COMPENSATION_FILTER_LIB_PATH);
            return (g_AudioCompensationFilterHandle == NULL) ? false : true;
        }
    }
    return true;
}

inline void closeAudioCompensationFilter(void) {
    if (g_AudioCompensationFilterHandle != NULL) {
        dlclose(g_AudioCompensationFilterHandle);
        g_AudioCompensationFilterHandle = NULL;
        g_setAudioCompFltCustParamFrom = NULL;
        g_getAudioCompFltCustParamFrom = NULL;
    }
}

int setAudioCompFltCustParam(AudioCompFltType_t eFLTtype, AUDIO_ACF_CUSTOM_PARAM_STRUCT *audioParam) {
    if (!openAudioCompensationFilter()) {
        return 0;
    } else {
        if (g_setAudioCompFltCustParamFrom == NULL) {
            g_setAudioCompFltCustParamFrom = (setFunAudioCompFltCustParam *)dlsym(g_AudioCompensationFilterHandle, "setAudioCompFltCustParamToStorage");
            const char *dlsym_error1 = dlerror();
            if (g_setAudioCompFltCustParamFrom == NULL) {
                closeAudioCompensationFilter();
                ALOGE("Error -dlsym setAudioCompFltCustParam fail");
                return 0;
            }
        }
    }
    return g_setAudioCompFltCustParamFrom(eFLTtype, audioParam);
}

int getAudioCompFltCustParam(AudioCompFltType_t eFLTtype, AUDIO_ACF_CUSTOM_PARAM_STRUCT *audioParam, const char *custScene) {
    if (!openAudioCompensationFilter()) {
        return 0;
    } else {
        if (g_getAudioCompFltCustParamFrom == NULL) {
            g_getAudioCompFltCustParamFrom = (getFunAudioCompFltCustParam *)dlsym(g_AudioCompensationFilterHandle, "getAudioCompFltCustParamFromStorage");
            const char *dlsym_error1 = dlerror();
            if (g_getAudioCompFltCustParamFrom == NULL) {
                closeAudioCompensationFilter();
                ALOGE("Error -dlsym getAudioCompFltCustParam fail");
                return 0;
            }
        }
    }
    return g_getAudioCompFltCustParamFrom(eFLTtype, audioParam, custScene);
}


bool generateVmDumpByEpl(const char *eplPath, const char *vmPath) {
    bool ret = true;

    FILE *eplFp = fopen(eplPath, "rb");
    if (eplFp == NULL) {
        ALOGE("[%s] eplFp failed, errno: %d", __FUNCTION__, errno);
    }

    FILE *vmFp = fopen(vmPath, "wb");
    if (vmFp == NULL) {
        ALOGE("[%s] vmFp failed, errno: %d", __FUNCTION__, errno);
    }

    if (eplFp && vmFp) {
        uint16_t sampleRate = 0;

        fseek(eplFp, 0, SEEK_END);
        size_t totalSize = ftell(eplFp);
        rewind(eplFp);

        size_t size = totalSize;
        while (size >= EPL_PACKET_BYTE_SIZE) {
            char rawBuffer[EPL_PACKET_BYTE_SIZE];
            if (fread(rawBuffer, 1, EPL_PACKET_BYTE_SIZE, eplFp) != EPL_PACKET_BYTE_SIZE) {
                ALOGW("%s(), Cannot read %d bytes from EPL file!", __FUNCTION__, EPL_PACKET_BYTE_SIZE);
                break;
            }

            uint16_t *buffer = (uint16_t *)rawBuffer;

            sampleRate = buffer[3843];

            if (sampleRate == 48000) {
                fwrite(buffer, 2, 1920, vmFp);      // VM short [0~1919]: EPL short [0~1919]
                fwrite(&buffer[3847], 2, 1, vmFp);  // VM short [1920]: EPL short [3847]
                fwrite(&buffer[3848], 2, 1, vmFp);  // VM short [1921]: EPL short [3848]
            } else if (sampleRate == 16000) {
                fwrite(&buffer[640], 2, 640, vmFp); // VM short [0~639]: EPL[640+i]
                fwrite(&buffer[3847], 2, 1, vmFp);  // VM short [640]: EPL[3847]
                fwrite(&buffer[3848], 2, 1, vmFp);  // VM short [641]: EPL[3848]
            } else {
                ALOGE("%s(), unsupport sample rate(%hu, remain size 0x%zx)! cannot convert EPL to vm", __FUNCTION__, sampleRate, size);
                ret = false;
                break;
            }

            size -= EPL_PACKET_BYTE_SIZE;

            ALOGV("%s(), sample rate = %d (0x%4x), remaining size = %zu", __FUNCTION__, sampleRate, sampleRate, size);
        }

        if (ret) {
            ALOGD("%s(), %s(size = %zu) -> %s , sample rate = %d succefully", __FUNCTION__, eplPath, totalSize, vmPath, sampleRate);
        }
    } else {
        if (eplFp == NULL) {
            ALOGE("%s(), fp == NULL (eplPath = %s)", __FUNCTION__, eplPath);
            ret = false;
        }

        if (vmFp == NULL) {
            ALOGE("%s(), fp == NULL (vmPath = %s)", __FUNCTION__, vmPath);
            ret = false;
        }
    }

    if (eplFp) {
        if (fclose(eplFp)) {
            ALOGE("%s(), fclose eplFp error", __FUNCTION__);
        }
        eplFp = NULL;
    }

    if (vmFp) {
        if (fclose(vmFp)) {
            ALOGE("%s(), fclose vmFp error", __FUNCTION__);
        }
        vmFp = NULL;
    }

    /* Backup the tmp EPL dump for debugging */
    if (rename(eplPath, "/data/vendor/audiohal/SPE_EPL.bak") != 0) {
        ALOGW("%s(), Cannot rename %s EPL succefully!", __FUNCTION__, eplPath);
    }

    return ret;
}
void SpeechMemCpy(void *dest, void *src, size_t n) {
    char *c_src = (char *)src;
    char *c_dest = (char *)dest;
    char tmp;
    size_t i;

    for (i = 0; i < n; i++) {
        c_dest[i] = c_src[i];
        asm("" ::: "memory");
    }
    asm volatile("dsb ish": : : "memory");
}

void adjustTimeStamp(struct timespec *startTime, int delayMs) {
    /* Adjust delay time */
    if (delayMs > 0) {
        long delayNs = (long)delayMs * 1000000;
        startTime->tv_nsec += delayNs;
        if (startTime->tv_nsec >= 1000000000) {
            startTime->tv_nsec -= 1000000000;
            startTime->tv_sec += 1;
        }
    } else if (delayMs < 0) {
        long delayNs = -(long)delayMs * 1000000;
        if (startTime->tv_nsec >= delayNs) {
            startTime->tv_nsec -= delayNs;
        } else {
            startTime->tv_nsec = 1000000000 - (delayNs - startTime->tv_nsec);
            startTime->tv_sec -= 1;
        }
    }
}

void calculateTimeStampByFrames(struct timespec startTime, uint32_t mTotalCaptureFrameSize, stream_attribute_t streamAttribute, struct timespec *newTimeStamp) {
    uint32_t framesPerSec = streamAttribute.sample_rate;
    unsigned long sec = mTotalCaptureFrameSize / framesPerSec;
    unsigned long ns = (mTotalCaptureFrameSize % framesPerSec) / (float)framesPerSec * 1000000000;

    newTimeStamp->tv_sec = startTime.tv_sec + sec;
    newTimeStamp->tv_nsec = startTime.tv_nsec + ns;

    if (newTimeStamp->tv_nsec >= 1000000000) {
        newTimeStamp->tv_nsec -= 1000000000;
        newTimeStamp->tv_sec += 1;
    }

    ALOGV("%s(), Start time = %ld.%09ld, framesPerSec = %d = %zu(format) * %d(ch) * %d(sr), New time = %ld.%09ld",
          __FUNCTION__,
          startTime.tv_sec, startTime.tv_nsec,
          framesPerSec,
          audio_bytes_per_sample(streamAttribute.audio_format),
          streamAttribute.num_channels,
          streamAttribute.sample_rate,
          newTimeStamp->tv_sec, newTimeStamp->tv_nsec);
}

void calculateTimeStampByBytes(struct timespec startTime, uint32_t totalBufferSize, stream_attribute_t streamAttribute, struct timespec *newTimeStamp) {
    uint32_t bytesPerSec = audio_bytes_per_sample(streamAttribute.audio_format) * streamAttribute.num_channels * streamAttribute.sample_rate;
    unsigned long sec = totalBufferSize / bytesPerSec;
    unsigned long ns = (totalBufferSize % bytesPerSec) / (float)bytesPerSec * 1000000000;

    newTimeStamp->tv_sec = startTime.tv_sec + sec;
    newTimeStamp->tv_nsec = startTime.tv_nsec + ns;

    if (newTimeStamp->tv_nsec >= 1000000000) {
        newTimeStamp->tv_nsec -= 1000000000;
        newTimeStamp->tv_sec += 1;
    }

    ALOGV("%s(), Start time = %ld.%09ld, bytesPerSec = %d = %zu(format) * %d(ch) * %d(sr), New time = %ld.%09ld",
          __FUNCTION__,
          startTime.tv_sec, startTime.tv_nsec,
          bytesPerSec,
          audio_bytes_per_sample(streamAttribute.audio_format),
          streamAttribute.num_channels,
          streamAttribute.sample_rate,
          newTimeStamp->tv_sec, newTimeStamp->tv_nsec);
}

uint32_t convertMsToBytes(const uint32_t ms, const stream_attribute_t *streamAttribute) {
    return ms * streamAttribute->num_channels * audio_bytes_per_sample(streamAttribute->audio_format) * streamAttribute->sample_rate / 1000;
}

static bool isolatedDeepBuffer = false;

bool isIsolatedDeepBuffer(const audio_output_flags_t flag) {
    return ((flag & AUDIO_OUTPUT_FLAG_DEEP_BUFFER) && !(flag & AUDIO_OUTPUT_FLAG_PRIMARY)) ? true : false;
}

void collectPlatformOutputFlags(audio_output_flags_t flag) {
    if (isIsolatedDeepBuffer(flag)) {
        isolatedDeepBuffer = true;
    }
}

bool platformIsolatedDeepBuffer() {
    return isolatedDeepBuffer;
}

unsigned int wordSizeAlign(unsigned int inSize)
{
	unsigned int alignSize;

	/* sram is device memory, need word size align, 8 byte for 64 bit platform */
	/* [3:0] = 4'h0 for the convenience of the hardware implementation */
	alignSize = inSize & 0xFFFFFFF0;
	return alignSize;
}

void setupCustomInfoStr(char* customInfoStr, size_t customInfoStrLength, const char* sceneName, int volLevel, int btCodec) {
    /* custom info */
    *customInfoStr = '\0';

#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    AppOps *appOps = appOpsGetInstance();
    if (appOps && appOps->appHandleIsFeatureOptionEnabled(appOps->appHandleGetInstance(), "VIR_SCENE_CUSTOMIZATION_SUPPORT")) {
        snprintf(customInfoStr, customInfoStrLength, "SetAudioCustomScene=%s;", sceneName ? sceneName : "");
    }
#endif

#if !defined(MTK_SPEECH_PARAM_VALID_VOLUME_0)
    /* Mapping FW volume level 1~8 to SWIP volume level 0~7 */
    if (volLevel > 0) {
        volLevel--;
    }
#endif

    if (volLevel >= 0) {
        snprintf(customInfoStr, customInfoStrLength, "%svol_level=%d;", customInfoStr, volLevel);
    }

    switch (btCodec) {
        case 0:
            snprintf(customInfoStr, customInfoStrLength, "%sbt_codec=%s;", customInfoStr, "cvsd");
            break;
        case 1:
            snprintf(customInfoStr, customInfoStrLength, "%sbt_codec=%s;", customInfoStr, "msbc");
            break;
        default:
            break;
    }

    ALOGD("%s(), custom_info = \"%s\", (scene = %s, vol_level = %d, bt_codec = %d)", __FUNCTION__, customInfoStr, sceneName, volLevel, btCodec);
}

char *audio_strncpy(char *target, const char *source, size_t target_size) {
    char *retval = NULL;

    if (target != NULL && source != NULL && target_size > 0) {
        retval = strncpy(target, source, target_size);
        target[target_size - 1] = '\0';
    } else {
        retval = target;
    }

    return retval;
}


char *audio_strncat(char *target, const char *source, size_t target_size) {
    char *retval = NULL;

    if (target != NULL && source != NULL && target_size > (strlen(target) + 1)) {
        retval = strncat(target, source, target_size - strlen(target) - 1);
    } else {
        retval = target;
    }

    return retval;
}

void initPowerHal() {
#if defined(MTK_POWERHAL_AUDIO_SUPPORT)
    AL_LOCK(gPowerHalLock);
    android::getPowerHal();
    AL_UNLOCK(gPowerHalLock);
#endif
}

bool getPowerHal() {
#if defined(MTK_POWERHAL_AUDIO_SUPPORT)
    if (gPowerHal == NULL) {
        ALOGD("%s(), get PowerHal Service", __FUNCTION__);
        gPowerHal = IMtkPower::tryGetService();
        if (gPowerHal != NULL) {
            powerHalDeathRecipient = new PowerDeathRecipient();
            hardware::Return<bool> linked = gPowerHal->linkToDeath(powerHalDeathRecipient, 0);
            if (!linked.isOk()) {
                ALOGE("%s(), Transaction error in linking to PowerHal death: %s", __FUNCTION__,
                      linked.description().c_str());
            } else if (!linked) {
                ALOGW("%s(), Unable to link to PowerHal death notifications", __FUNCTION__);
            } else {
                ALOGD("%s(), Link to death notification successfully", __FUNCTION__);
            }
        } else {
            ALOGD("%s(), Cound not get PowerHal Service", __FUNCTION__);
        }
    }
    return gPowerHal != NULL;
#else
    return false;
#endif
}

void power_hal_hint(PowerHalHint hint, bool enable) {
#if defined(MTK_POWERHAL_AUDIO_SUPPORT)
    AL_LOCK(gPowerHalLock);
    if (getPowerHal() == false) {
        ALOGE("IPower error!!");
        AL_UNLOCK(gPowerHalLock);
        return;
    }

    int32_t custPowerHint;
    switch (hint) {
    case POWERHAL_LATENCY_DL:
        custPowerHint = MTKPOWER_HINT_AUDIO_LATENCY_DL;
        break;
    case POWERHAL_LATENCY_UL:
        custPowerHint = MTKPOWER_HINT_AUDIO_LATENCY_UL;
        break;
    case POWERHAL_POWER_DL:
        custPowerHint = MTKPOWER_HINT_AUDIO_POWER_DL;
        break;
    case POWERHAL_DISABLE_WIFI_POWER_SAVE:
        custPowerHint = MTKPOWER_HINT_AUDIO_DISABLE_WIFI_POWER_SAVE;
        break;
    default:
        ALOGE("%s - no support hint %d", __FUNCTION__, hint);
        AL_UNLOCK(gPowerHalLock);
        return;
    }

    int data = enable ? MTKPOWER_HINT_ALWAYS_ENABLE : 0;
    gPowerHal->mtkCusPowerHint((int32_t)custPowerHint, data);
    ALOGD("%s - custPowerHint %d, data %d", __FUNCTION__, custPowerHint, data);
    AL_UNLOCK(gPowerHalLock);
#else
    (void) hint;
    (void) enable;
#endif
}

int audio_sched_setschedule(pid_t pid, int policy, int sched_priority) {
    int ret;
    struct sched_param sched_p;

    ret = sched_getparam(pid, &sched_p);
    if (ret)
    {
        ALOGE("%s(), sched_getparam failed, errno: %d, ret %d", __FUNCTION__, errno, ret);
    }

    sched_p.sched_priority = sched_priority;

    ret = sched_setscheduler(pid, policy, &sched_p);
    if (ret)
    {
        ALOGE("%s(), sched_setscheduler failed, errno: %d, ret %d", __FUNCTION__, errno, ret);
    }

    return ret;
}

void setNeedAEETimeoutFlg(bool state) { bNeedAEETimeoutFlg = state; }
bool getNeedAEETimeoutFlg() { return bNeedAEETimeoutFlg; }

bool findEnumByString(const struct enum_to_str_table* table, const char *str, uint32_t *enumVal)
{
    if (table == NULL) {
        ALOGW("%s(), table is NULL", __FUNCTION__);
        return false;
    }

    if (str == NULL) {
        ALOGW("%s(), str is NULL", __FUNCTION__);
        return false;
    }

    for(size_t i = 0; table[i].name != NULL; i++) {
        if (strcmp(table[i].name, str) == 0) {
            *enumVal = table[i].value;
            return true;
        }
    }
    return false;
}

uint32_t deviceInTypeStrToEnum(const char* deviceInType) {
    uint32_t retEnumVal = 0;
    char *restOfStr = NULL;
    char* str = NULL;

    if (deviceInType == NULL) {
        return 0;
    }

    str = strdup(deviceInType);
    char *deviceInTypeStr = strtok_r(str, DEVICE_IN_TYPE_SEPERATOR, &restOfStr);
    while (deviceInTypeStr) {
        uint32_t enumVal = 0;
        if (findEnumByString(deviceInTypeTable, deviceInTypeStr, &enumVal)) {
            ALOGV("%s(), deviceInTypeStr = %s => enum 0x%x", __FUNCTION__, deviceInTypeStr, enumVal);
            retEnumVal |= enumVal;
        } else {
            ALOGW("%s(), no match device in type string (%s)", __FUNCTION__, deviceInTypeStr);
            continue;
        }
        deviceInTypeStr = strtok_r(NULL, DEVICE_IN_TYPE_SEPERATOR, &restOfStr);
    }
    ALOGV("%s(), deviceInTypeStr = %s, enum = 0x%x", __FUNCTION__, deviceInType, retEnumVal);
    free(str);

    return retEnumVal;
}

#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
bool getMicInfoFromXml(const char* projectName, audio_microphone_characteristic_t* micArray, size_t *micCount) {
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGE("%s(), Error: AppOps == NULL", __FUNCTION__);
        ASSERT(0);
        return false;
    }

    AppHandle *appHandle = appOps->appHandleGetInstance();
    AudioType* micInfoAudioType = appOps->appHandleGetAudioTypeByName(appHandle, MIC_INFO_AUDIO_TYPE_NAME);
    if (micInfoAudioType == NULL) {
        ALOGE("%s(), Error: micInfoAudioType == NULL", __FUNCTION__);
        ASSERT(0);
        return false;
    }

    // get number of mics
    CategoryType* categoryType = appOps->audioTypeGetCategoryTypeByName(micInfoAudioType, MICROPHONES_CATEGORY_TYPE_NAME);
    if (categoryType == NULL) {
        ALOGE("%s(), Error: categoryType == NULL", __FUNCTION__);
        ASSERT(0);
        return false;
    }

    size_t micArrayIndex = 0;
    size_t numOfCategory = appOps->categoryTypeGetNumOfCategory(categoryType);
    for (size_t i = 0; i < numOfCategory; i++) {
        audio_microphone_characteristic_t micInfo;
        Param *param = NULL;
        size_t size = 0;
        uint32_t enumVal = 0;

        memset(&micInfo, 0, sizeof(micInfo));

        Category *category = appOps->categoryTypeGetCategoryByIndex(categoryType, i);
        std::string categoryPath = std::string(PROJECTS_CATEGORY_TYPE_NAME) + "," +
                                   (projectName ? projectName : "") + "," +
                                   (categoryType->name ? categoryType->name : "") + "," +
                                   (category->name ? category->name : "");
        ALOGD("Mic[%zu] %s\n", i, categoryPath.c_str());

        ParamUnit *paramUnit = appOps->audioTypeGetParamUnit(micInfoAudioType, categoryPath.c_str());
        if (paramUnit == NULL) {
            ALOGW("Mic[%zu] %s is NULL.\n", i, categoryPath.c_str());
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, PROJECT_ID_PARAM_NAME);
        if (param != NULL) {
            char* deviceId = (char*)param->data;
            ASSERT(strlen(deviceId) < AUDIO_MICROPHONE_ID_MAX_LEN);
            strncpy(micInfo.device_id, deviceId, AUDIO_MICROPHONE_ID_MAX_LEN);
            micInfo.device_id[AUDIO_MICROPHONE_ID_MAX_LEN - 1] = '\0';
            ALOGV("device_id = %s", deviceId);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, DEVICE_IN_TYPE_PARAM_NAME);
        if (param != NULL) {
            char* deviceInType = (char *)param->data;
            enumVal = deviceInTypeStrToEnum(deviceInType);
            micInfo.device = (audio_devices_t)enumVal;
            ALOGV("deviceInType = %s, enum = 0x%x", deviceInType, enumVal);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, ADDRESS_PARAM_NAME);
        if (param != NULL) {
            char *address = (char *)param->data;
            ASSERT(strlen(address) < AUDIO_DEVICE_MAX_ADDRESS_LEN);
            strncpy(micInfo.address, address, AUDIO_DEVICE_MAX_ADDRESS_LEN);
            micInfo.address[AUDIO_DEVICE_MAX_ADDRESS_LEN - 1] = '\0';
            ALOGV("address = %s", address);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, MIC_LOCATION_PARAM_NAME);
        if (param != NULL) {
            char* micLocation = (char *)param->data;
            ASSERT(findEnumByString(micLoccationTable, micLocation, &enumVal));
            micInfo.location= (audio_microphone_location_t)enumVal;
            ALOGV("mic_location = %s, enum = %d", micLocation, enumVal);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, DEVICE_GROUP_PARAM_NAME);
        if (param != NULL) {
            int deviceGroup = *(int *)param->data;
            micInfo.group = deviceGroup;
            ALOGV("device_group = %d", deviceGroup);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, INDEX_IN_THE_GROUP_PARAM_NAME);
        if (param != NULL) {
            int indexInTheGroup = *(int *)param->data;
            micInfo.index_in_the_group = indexInTheGroup;
            ALOGV("index_in_the_group = %d", indexInTheGroup);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, GEOMETRIC_LOCATION_PARAM_NAME);
        if (param) {
            double *geometric_location = (double *)param->data;
            size = param->arraySize;
            if (size == 3) {
                micInfo.geometric_location.x = (float)geometric_location[0];
                micInfo.geometric_location.y = (float)geometric_location[1];
                micInfo.geometric_location.z = (float)geometric_location[2];
                ALOGV("geometric_location = (%f, %f, %f), size = %zu", geometric_location[0], geometric_location[1], geometric_location[2], size);
            } else {
                ALOGW("%s(), size is invalid (%zu)", __FUNCTION__, size);
                ASSERT(size == 3);
                continue;
            }
        } else {
            micInfo.geometric_location.x = 0;
            micInfo.geometric_location.y = 0;
            micInfo.geometric_location.z = 0;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, ORIENTATION_PARAM_NAME);
        if (param) {
            double *orientation = (double *)param->data;
            size = param->arraySize;
            if (size == 3) {
                micInfo.orientation.x = (float)orientation[0];
                micInfo.orientation.y = (float)orientation[1];
                micInfo.orientation.z = (float)orientation[2];
                ALOGV("orientation = (%f,%f,%f), size = %zu", orientation[0], orientation[1], orientation[2], size);
            } else {
                ALOGW("%s(), size is invalid (%zu)", __FUNCTION__, size);
                ASSERT(size == 3);
                continue;
            }
        } else {
            micInfo.orientation.x = 0;
            micInfo.orientation.y = 0;
            micInfo.orientation.z = 0;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, FREQUENCY_RESPONSES_PARAM_NAME);
        if (param) {
            double *frequency_responses = (double *)param->data;
            size = param->arraySize;
            if ((size > 0) && (size % 2 == 0)) {
                for (size_t i = 0; i < size; i+=2) {
                    micInfo.frequency_responses[0][i/2] = (float)frequency_responses[i];
                    micInfo.frequency_responses[1][i/2] = (float)frequency_responses[i+1];
                    ALOGV("frequency_responses freq: %f, amp:%f", frequency_responses[i], frequency_responses[i+1]);
                }
                micInfo.num_frequency_responses = size / 2;
            } else {
                ALOGW("%s(), the freq response size is invalid (%zu)", __FUNCTION__, size);
                ASSERT(0);
                continue;
            }
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, SENSITIVITY_PARAM_NAME);
        if (param) {
            float sensitivity = *(float *)param->data;
            micInfo.sensitivity = sensitivity;
            ALOGV("sensitivity = %f", sensitivity);
        } else {
            micInfo.sensitivity = AUDIO_MICROPHONE_SENSITIVITY_UNKNOWN;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, MAX_SPL_PARAM_NAME);
        if (param) {
            float max_spl = *(float *)param->data;
            micInfo.max_spl = max_spl;
            ALOGV("max_spl = %f", max_spl);
        } else {
            micInfo.max_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, MIN_SPL_PARAM_NAME);
        if (param) {
            float min_spl = *(float *)param->data;
            micInfo.min_spl = min_spl;
            ALOGV("min_spl = %f", min_spl);
        } else {
            micInfo.min_spl = AUDIO_MICROPHONE_SPL_UNKNOWN;
        }

        param = appOps->paramUnitGetParamByName(paramUnit, DIRECTIONALITY_PARAM_NAME);
        if (param) {
            char* directionality = (char *)param->data;
            ASSERT(findEnumByString(micDirectionalityTable, directionality, &enumVal));
            micInfo.directionality = (audio_microphone_directionality_t)enumVal;
            ALOGV("directionality = %s, enum = %d", directionality, enumVal);
        } else {
            ALOGW("%s(), param is NULL", __FUNCTION__);
            ASSERT(param != NULL);
            continue;
        }

        micArray[micArrayIndex++] = micInfo;
    }

    *micCount = micArrayIndex;

    return true;
}
#endif

bool getMicInfo(audio_microphone_characteristic_t* micArray, size_t *micCount) {
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    /* Query projects' mic info */
    const char* project = NULL;
    if (IsAudioSupportFeature(AUDIO_SUPPORT_DMIC)) {
        project = "dmic_proj";
    } else {
        project = "amic_proj";
    }

    return getMicInfoFromXml(project, micArray, micCount);
#else
    *micCount = 2;

    /* Setup main mic */
    memset(&micArray[0], 0, sizeof(micArray[0]));
    strncpy(micArray[0].device_id, "SPH1642HT5H_REV_B", AUDIO_MICROPHONE_ID_MAX_LEN);
    micArray[0].device_id[AUDIO_MICROPHONE_ID_MAX_LEN - 1] = '\0';

    micArray[0].device = AUDIO_DEVICE_IN_BUILTIN_MIC;

    strncpy(micArray[0].address, "bottom", AUDIO_DEVICE_MAX_ADDRESS_LEN);
    micArray[0].address[AUDIO_DEVICE_MAX_ADDRESS_LEN - 1] = '\0';

    micArray[0].location= AUDIO_MICROPHONE_LOCATION_MAINBODY;

    micArray[0].group = 0;
    micArray[0].index_in_the_group = 0;

    micArray[0].geometric_location.x = 0.5f;
    micArray[0].geometric_location.y = 0.0f;
    micArray[0].geometric_location.z = 0.5f;

    micArray[0].orientation.x = 0.0f;
    micArray[0].orientation.y = -1.0f;
    micArray[0].orientation.z = 0.0f;

    /* Setup freq response array */
    micArray[0].frequency_responses[0][0] = 80;
    micArray[0].frequency_responses[0][1] = 100;
    micArray[0].frequency_responses[0][2] = 500;
    micArray[0].frequency_responses[0][3] = 5000;
    micArray[0].frequency_responses[0][4] = 10000;

    micArray[0].frequency_responses[1][0] = -2.0f;
    micArray[0].frequency_responses[1][1] = -1.25f;
    micArray[0].frequency_responses[1][2] = 0.0f;
    micArray[0].frequency_responses[1][3] = 0.0f;
    micArray[0].frequency_responses[1][4] = 1.75f;
    micArray[0].num_frequency_responses = 5;

    micArray[0].sensitivity = -41;
    micArray[0].max_spl = 124;
    micArray[0].min_spl = 124;

    micArray[0].directionality = AUDIO_MICROPHONE_DIRECTIONALITY_OMNI;

    /* Setup sub mic */
    memset(&micArray[1], 0, sizeof(micArray[1]));

    strncpy(micArray[1].device_id, "SPH1642HT5H_REV_B", AUDIO_MICROPHONE_ID_MAX_LEN);
    micArray[1].device_id[AUDIO_MICROPHONE_ID_MAX_LEN - 1] = '\0';

    micArray[1].device = static_cast<audio_devices_t>(AUDIO_DEVICE_IN_BUILTIN_MIC|AUDIO_DEVICE_IN_BACK_MIC);

    strncpy(micArray[1].address, "back", AUDIO_DEVICE_MAX_ADDRESS_LEN);
    micArray[1].address[AUDIO_DEVICE_MAX_ADDRESS_LEN - 1] = '\0';

    micArray[1].location= AUDIO_MICROPHONE_LOCATION_MAINBODY;

    micArray[1].group = 1;
    micArray[1].index_in_the_group = 0;

    micArray[1].geometric_location.x = 0.5f;
    micArray[1].geometric_location.y = 0.7f;
    micArray[1].geometric_location.z = 0.0f;

    micArray[1].orientation.x = 0.0f;
    micArray[1].orientation.y = 0.0f;
    micArray[1].orientation.z = -1.0f;

    /* Setup freq response array */
    micArray[1].frequency_responses[0][0] = 80;
    micArray[1].frequency_responses[0][1] = 100;
    micArray[1].frequency_responses[0][2] = 500;
    micArray[1].frequency_responses[0][3] = 5000;
    micArray[1].frequency_responses[0][4] = 10000;
    micArray[1].frequency_responses[1][0] = -2.0f;
    micArray[1].frequency_responses[1][1] = -1.25f;
    micArray[1].frequency_responses[1][2] = 0.0f;
    micArray[1].frequency_responses[1][3] = 0.0f;
    micArray[1].frequency_responses[1][4] = 1.75f;
    micArray[1].num_frequency_responses = 5;

    micArray[1].sensitivity = -41;
    micArray[1].max_spl = 124;
    micArray[1].min_spl = 124;

    micArray[1].directionality = AUDIO_MICROPHONE_DIRECTIONALITY_OMNI;

    return true;
#endif
}

void dumpMicInfo(struct audio_microphone_characteristic_t *micArray, size_t micCount) {
    ALOGD("%s(), ======= micCount = %zu =======", __FUNCTION__, micCount);
    for(size_t i = 0; i< micCount; i++) {
        ALOGD("micArray[%zu].address = %s", i, micArray[i].address);
        ALOGD("micArray[%zu].device = 0x%x", i, micArray[i].device);
        ALOGD("micArray[%zu].device_id = %s", i, micArray[i].device_id);
        ALOGD("micArray[%zu].directionality = %d", i, micArray[i].directionality);
        ALOGD("micArray[%zu].num_frequency_responses = %d", i, micArray[i].num_frequency_responses);
        for (size_t j = 0; j < micArray[i].num_frequency_responses; j++) {
            ALOGD("micArray[%zu].frequency_responses[%zu] freq: %f, amp:%f",i, j,  micArray[i].frequency_responses[0][j], micArray[i].frequency_responses[1][j]);
        }
        ALOGD("micArray[%zu].geometric_location = %f, %f, %f", i, micArray[i].geometric_location.x, micArray[i].geometric_location.y, micArray[i].geometric_location.z);
        ALOGD("micArray[%zu].group = %d", i, micArray[i].group);
        ALOGD("micArray[%zu].index_in_the_group = %d", i, micArray[i].index_in_the_group);
        ALOGD("micArray[%zu].location = %d", i, micArray[i].location);
        ALOGD("micArray[%zu].max_spl = %f", i, micArray[i].max_spl);
        ALOGD("micArray[%zu].min_spl = %f", i, micArray[i].min_spl);
        ALOGD("micArray[%zu].orientation = %f, %f, %f", i, micArray[i].orientation.x, micArray[i].orientation.y, micArray[i].orientation.z);
        ALOGD("micArray[%zu].sensitivity = %f", i, micArray[i].sensitivity);
    }
    ALOGD("%s(), =====================", __FUNCTION__);
}


uint32_t iter_div_u64_rem(uint64_t dividend, uint32_t divisor, uint64_t *remainder)
{
    uint32_t ret = 0;
    while (dividend >= divisor) {
        dividend -= divisor;
        ret++;
    }
    *remainder = dividend;
    return ret;
}

void timespec_add_ns(struct timespec *a, uint64_t ns)
{
    a->tv_sec += iter_div_u64_rem(a->tv_nsec + ns, NSEC_PER_SEC, &ns);
    a->tv_nsec = ns;
}

uint32_t source_priority(audio_source_t inputSource) {
    switch (inputSource) {
    case AUDIO_SOURCE_VOICE_COMMUNICATION:
        return 9;
    case AUDIO_SOURCE_CAMCORDER:
        return 8;
    case AUDIO_SOURCE_VOICE_PERFORMANCE:
        return 7;
    case AUDIO_SOURCE_UNPROCESSED:
        return 6;
    case AUDIO_SOURCE_MIC:
        return 5;
    case AUDIO_SOURCE_ECHO_REFERENCE:
        return 4;
    case AUDIO_SOURCE_FM_TUNER:
        return 3;
    case AUDIO_SOURCE_VOICE_RECOGNITION:
        return 2;
    case AUDIO_SOURCE_HOTWORD:
        return 1;
    default:
    break;
    }
    return 0;
}


AudioThrottleTimeControl::AudioThrottleTimeControl(
    bool isOutput,
    uint32_t unitSleepUs,
    uint32_t errorThdSleepUs):
    mBytesSum(0),
    mThrottleControlStartTime(0),
    mThrottleControlUnitSleepUs(unitSleepUs),
    mThrottleControlErrorThdSleepUs(errorThdSleepUs),
    mIsOutput(isOutput){
    ALOGD("%s(), mIsOutput = %d, mBytesSum = %llu, mThrottleControlStartTime = %lld",
          __FUNCTION__, mIsOutput,  (unsigned long long)mBytesSum,  (unsigned long long)mThrottleControlStartTime);
}

AudioThrottleTimeControl::~AudioThrottleTimeControl() {
    ALOGD("%s()", __FUNCTION__);
}

int64_t AudioThrottleTimeControl::calculateSleepUs(uint64_t totalbytes, stream_attribute_t streamAttribute) {
    int64_t targetTimeUs = (totalbytes * 1000 * 1000) /
                            getSizePerFrame(streamAttribute.audio_format, streamAttribute.num_channels) / streamAttribute.sample_rate;
    int64_t procTimeUs = (systemTime(CLOCK_MONOTONIC) - mThrottleControlStartTime) / 1000;
    int64_t sleepTimeUs = targetTimeUs - procTimeUs;
    ALOGV("%s(), mIsOutput(%d), %lld, %lld, %lld",
          __FUNCTION__, mIsOutput, (long long)sleepTimeUs, (long long)procTimeUs, (long long)targetTimeUs);
    return sleepTimeUs;
}

int AudioThrottleTimeControl::adaptiveSleepUs(ssize_t readsize, stream_attribute_t streamAttribute) {
    int64_t procTimeUs = 0;
    int64_t startTime = systemTime(CLOCK_MONOTONIC);
    int64_t sleepUs = 0;

    if (mIsOutput == true) {
        if (mBytesSum == 0) {
            mThrottleControlStartTime = systemTime(CLOCK_MONOTONIC);
            ALOGV("%s(), mIsOutput(%d), mBytesSum = %llu, readsize = %zd, sleepUs =%lld",
                  __FUNCTION__, mIsOutput, (unsigned long long)mBytesSum, readsize, (long long)sleepUs);
        }
        mBytesSum += readsize;
        sleepUs = calculateSleepUs(mBytesSum, streamAttribute);
    } else {
        mBytesSum += readsize;
        if (mBytesSum == readsize) {
            mThrottleControlStartTime = systemTime(CLOCK_MONOTONIC);
        } else {
            sleepUs = calculateSleepUs(mBytesSum - readsize, streamAttribute);
        }
    }
    ALOGV("%s(), mIsOutput(%d), mBytesSum = %llu, readsize = %zd, sleepUs =%lld",
          __FUNCTION__, mIsOutput, (unsigned long long)mBytesSum, readsize, (long long)sleepUs);
    if (sleepUs > 0) {
        while ((procTimeUs + mThrottleControlUnitSleepUs) < sleepUs) {
            usleep(mThrottleControlUnitSleepUs);
            procTimeUs = ((systemTime(CLOCK_MONOTONIC) - startTime)) / 1000;
        }
        if (procTimeUs < sleepUs) {
            usleep(sleepUs - procTimeUs);
        }
        procTimeUs = ((systemTime(CLOCK_MONOTONIC) - startTime)) / 1000;
        if ((procTimeUs - sleepUs) > mThrottleControlErrorThdSleepUs) {
            ALOGW("%s(), mIsOutput(%d), %lld, %lld, %lld, %d", __FUNCTION__, mIsOutput, (long long)sleepUs,
                  (long long)procTimeUs, (long long)(procTimeUs - sleepUs), mThrottleControlErrorThdSleepUs);
        }
    }
    return 0;
}

void AudioThrottleTimeControl::resetTimeStampInfo() {
    // Reset total data read counter
    mBytesSum = 0;
}

void AudioThrottleTimeControl::updateTimeStamp(ssize_t readsize) {
    // Update total data read counter
    mBytesSum += readsize;
    ALOGV("%s(), mBytesSum = %llu, readsize = %zd ", __FUNCTION__, (unsigned long long)mBytesSum, readsize);
}

static bool gPlaybackTimestampLogOn;
void setPlaybackTimestampLogOn(const bool enable)
{
    gPlaybackTimestampLogOn = enable;
}


bool getPlaybackTimestampLogOn(void)
{
    return gPlaybackTimestampLogOn;
}

bool isHwSrcSupport(void) {
#if defined(HW_SRC_SUPPORT)
    return true;
#else
    return false;
#endif
}

void getCurrentTimestamp(char *timep_str, uint32_t timep_str_size)
{
    time_t rawtime;
    struct tm *time_info;

    if (time(&rawtime) == ((time_t)-1)) {
        ALOGE("%s(), get timep failed\n", __FUNCTION__);
    } else {
        time_info = localtime(&rawtime);

        if (time_info != NULL) {
            if (!strftime(timep_str, timep_str_size, "%Y%m%d_%H%M%S", time_info)) {
                ALOGE("%s(), strftime failed, timep_str : %s\n", __FUNCTION__, timep_str);
            }
        }
    }
}

/*==============================================================================
 *                     Singleton Pattern
 *============================================================================*/
AudioPlatformInfo *AudioPlatformInfo::mAudioPlatformInfo = NULL;
AudioPlatformInfo *AudioPlatformInfo::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioPlatformInfo == NULL) {
        mAudioPlatformInfo = new AudioPlatformInfo();
    }
    ASSERT(mAudioPlatformInfo != NULL);
    return mAudioPlatformInfo;
}

AudioPlatformInfo::AudioPlatformInfo() {
    ALOGD("%s()", __FUNCTION__);

    for (int i = 0 ; i < NUM_PLATFORM_FO_TYPE_STR; i++) {
        mPlatformInfoStr[i] = "";
    }

#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    //init mPlatformFOXXXs for different type
    AppOps *appOps = appOpsGetInstance();
    if (appOps != NULL) {
        AppHandle *appHandle = appOps->appHandleGetInstance();
        if (appHandle != NULL) {
            for (int i = 0 ; i < NUM_PLATFORM_FO_TYPE_INT; i++) {
                mPlatformInfoValue[i] = appOps->appHandleGetFeatureOptionIntValue(appHandle, platformInfoIntTypeStr[i], 0);
                ALOGD("%s(), mPlatformInfoValue(%s) = %d", __FUNCTION__, platformInfoIntTypeStr[i], mPlatformInfoValue[i]);
            }

            for (int i = 0 ; i < NUM_PLATFORM_FO_TYPE_STR; i++) {
                mPlatformInfoStr[i] = appOps->appHandleGetFeatureOptionValue(appHandle, platformInfoStringTypeStr[i]);
                ALOGD("%s(), mPlatformInfoStr(%s) = %s", __FUNCTION__, platformInfoStringTypeStr[i], mPlatformInfoStr[i]);
            }
        } else {
            ALOGE("%s(), appHandle is NULL, not get platform info!!!", __FUNCTION__);
        }
    } else {
        ALOGE("%s(), AppOps is NULL, not get platform info!!!",__FUNCTION__);
    }
#else //init for rainer(k80)
    mPlatformInfoValue[KERNEL_BUFFER_SIZE_NORMAL] = 16384;
    mPlatformInfoValue[KERNEL_BUFFER_SIZE_DEEP] = 16384;
    mPlatformInfoValue[KERNEL_DSPBUFFER_SIZE_DEEP] = 0;
    mPlatformInfoValue[KERNEL_DSPBUFFER_SIZE_HIFI_96K] = 0;
    mPlatformInfoValue[KERNEL_DSPBUFFER_SIZE_HIFI_192K] = 0;
#endif
}

AudioPlatformInfo::~AudioPlatformInfo() {
    ALOGD("%s()", __FUNCTION__);
}

int AudioPlatformInfo::getPlatformInfoValue(audio_platform_fo_int_type_t type) {
    if (type >= 0 && type < NUM_PLATFORM_FO_TYPE_INT) {
        return mPlatformInfoValue[type];
    }

    ALOGE("%s(), invalid platform fo type %d", __FUNCTION__, type);
    ASSERT(false);
    return 0;
}

const char* AudioPlatformInfo::getPlatformInfoStr(audio_platform_fo_str_type_t type) {
    if (type >= 0 && type < NUM_PLATFORM_FO_TYPE_STR) {
        return mPlatformInfoStr[type];
    }

    ALOGE("%s(), invalid platform fo type %d", __FUNCTION__, type);
    ASSERT(false);
    return NULL;
}

unsigned int formatToBytes(const audio_format_t format) {
    switch (format) {
    case AUDIO_FORMAT_PCM_8_BIT:
        return 1;
    case AUDIO_FORMAT_PCM_16_BIT:
        return 2;
    case AUDIO_FORMAT_PCM_24_BIT_PACKED:
        return 3;
    case AUDIO_FORMAT_PCM_8_24_BIT:
    case AUDIO_FORMAT_PCM_32_BIT:
        return 4;
    default:
        ALOGE("%s: invalid audio format %#x", __FUNCTION__, format);
        return 0;
    }
}

unsigned int bytesToFrames(unsigned int bytes, unsigned int channels, audio_format_t format)
{
    if (channels * formatToBytes(format) == 0) {
        ASSERT(0);
        return 0;
    }

    return (bytes) / (channels * formatToBytes(format));
}

int isDspLowLatencyFlag(audio_output_flags_t flag)
{
#if (defined(MTK_AUDIO_HIERARCHICAL_PARAM_SUPPORT) && (MTK_AUDIO_TUNING_TOOL_V2_PHASE >= 2))
    if (appIsFeatureOptionEnabled("MTK_AUDIO_FAST_RAW_SUPPORT")) {
        return flag & AUDIO_OUTPUT_FLAG_RAW;
    }
#endif
    return flag & AUDIO_OUTPUT_FLAG_FAST;
}

}

