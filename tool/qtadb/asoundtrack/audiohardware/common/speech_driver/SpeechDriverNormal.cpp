#include <SpeechDriverNormal.h>

#include <string.h>

#include <errno.h>

#include <pthread.h>

#include "AudioSystemLibCUtil.h"

#include <system/audio.h>
#include <media/AudioParameter.h>

#include <audio_time.h>

#include <AudioLock.h>
#include <AudioUtility.h>
#include <SpeechUtility.h>

#include <SpeechMessageID.h>


#include <SpeechMessageQueue.h>
#include <SpeechMessengerNormal.h>


#include <AudioVolumeFactory.h>
#include <SpeechVMRecorder.h>
#include <SpeechPcm2way.h>
#include <SpeechDataProcessingHandler.h>

#include <SpeechParserBase.h>
#include <SpeechConfig.h>

#include <SpeechEnhancementController.h>

#include <WCNChipController.h>

#include <AudioSmartPaController.h>

#include <AudioALSAHardwareResourceManager.h>

#include <AudioVIBSPKControl.h>

#include <AudioEventThreadManager.h>
#include <tinyalsa/asoundlib.h> // for mixctrl

#include <SpeechPcmMixerBGSPlayer.h>
#include <SpeechPcmMixerTelephonyTx.h>
#if defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
#include <SpeechPcmMixerVoipRx.h>
#endif
#include "LoopbackManager.h"
#if defined(MTK_SPEECH_ECALL_SUPPORT)
#include <SpeechEcallController.h>
#endif

#ifdef MTK_AUDIODSP_SUPPORT
#include "SpeechDriverOpenDSP.h"
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SpeechDriverNormal"

namespace android {


/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */

#define USE_DEDICATED_LOOPBACK_DELAY_FRAMES (true)
#define MAX_LOOPBACK_DELAY_FRAMES (64)
#define DEFAULT_LOOPBACK_DELAY_FRAMES (12) /* 12 frames => 240 ms */

#define MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS (3000)

#define TEMP_CCCI_MD_PAYLOAD_SYNC (0x1234)


#define MAX_VM_RECORD_SIZE      (0x4000) // 7500 * 2 => 16K
#define MAX_RAW_RECORD_SIZE     (0x1000) // 1924 * 2 => 4K
#define MAX_PNW_UL_SIZE         (0x800)  //  960 * 2 => 2K
#define MAX_TTY_DEBUG_SIZE      (0x200)  //  160 * 2 => 512 bytes
#if defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
#define MAX_VOIP_RX_UL_SIZE    (0x800)  //  960 * 2 => 2K
#endif

// (20ms / 1000) * 48K(max sample rate) * 2byte(16bit data) = 1.92K
#define MAX_TELEPHONY_TX_SIZE   (0x800)


#define MAX_PARSED_RECORD_SIZE  (MAX_RAW_RECORD_SIZE)

#define MAX_MSG_PROCESS_TIME_MS (10)

#define SPH_DUMP_STR_SIZE (1024)

#define MAX_SPEECH_ECALL_BUF_LEN (1600)

/*
 * =============================================================================
 *                     global
 * =============================================================================
 */

/* keep modem status to recovery when audioserver die */
static const char *kPropertyKeyModemEPOF   = "vendor.audiohal.modem_1.epof";
static const char *kPropertyKeyModemStatus = "vendor.audiohal.modem_1.status";
static const char *kPropertyKeyModemHeadVersion   = "vendor.audiohal.modem_1.headversion";
static const char *kPropertyKeyModemVersion = "vendor.audiohal.modem_1.version";
static const char *kPropertyKeyWaitAckMsgId = "vendor.audiohal.wait.ack.msgid";

static char gKeyStringBuf[MAX_SPEECH_PARSER_KEY_LEN];
static uint16_t gLpbkRouting;
static uint16_t gIpcParamMsgId;

/*
 * =============================================================================
 *                     Callback
 * =============================================================================
 */

static void callbackSpeechParamChange(int audioEventType, void *caller, void *arg) {
    ALOGD("%s(), audioEventType = %d, caller(%p), arg(%p)",
          __FUNCTION__, audioEventType, caller, arg);

    SpeechDriverNormal *pSpeechDriver = NULL;
    pSpeechDriver = static_cast<SpeechDriverNormal *>(caller);
    if (pSpeechDriver == NULL) {
        ALOGE("%s(), pSpeechDriver is NULL!!", __FUNCTION__);
        return;
    }
    pSpeechDriver->updateSpeechParam(SPEECH_SCENARIO_PARAM_CHANGE);
}

/*
 * =============================================================================
 *                     Singleton Pattern
 * =============================================================================
 */

SpeechDriverNormal *SpeechDriverNormal::mSpeechDriver = NULL;

SpeechDriverNormal *SpeechDriverNormal::GetInstance(modem_index_t modem_index) {
    static AudioLock mGetInstanceLock;

    AL_AUTOLOCK(mGetInstanceLock);

    if (modem_index != MODEM_1) {
        ALOGE("%s(), modem_index %d not support!!", __FUNCTION__, modem_index);
        ASSERT(modem_index == MODEM_1);
        return NULL;
    }

    if (mSpeechDriver == NULL) {
        mSpeechDriver = new SpeechDriverNormal(modem_index);
    }
    return mSpeechDriver;
}



/*==============================================================================
 *                     Constructor / Destructor / Init / Deinit
 *============================================================================*/

SpeechDriverNormal::SpeechDriverNormal(modem_index_t modem_index) {
    mModemIndex = modem_index;

    // initialize buffer pointer
    mBgsBuf = NULL;
    mVmRecBuf = NULL;
    mRawRecBuf = NULL;
    mParsedRecBuf = NULL;
    mP2WUlBuf = NULL;
    mP2WDlBuf = NULL;
    mTtyDebugBuf = NULL;
    mTelephonyTxBuf = NULL;
    mVoipRxUlBuf = NULL;
    mVoipRxDlBuf = NULL;

    mSpeechMessenger = new SpeechMessengerNormal(mModemIndex);
    if (mSpeechMessenger == NULL) {
        ALOGE("%s(), mSpeechMessenger == NULL!!", __FUNCTION__);
    } else {
        if (get_uint32_from_mixctrl(kPropertyKeyModemEPOF) != 0) {
            if (mSpeechMessenger->checkModemAlive() == true) {
                ALOGD("%s(), md alive, reset EPOF", __FUNCTION__);
                set_uint32_to_mixctrl(kPropertyKeyModemEPOF, 0);
            }
        }

        kMaxApPayloadDataSize = mSpeechMessenger->getMaxApPayloadDataSize();
        kMaxMdPayloadDataSize = mSpeechMessenger->getMaxMdPayloadDataSize();

        AUDIO_ALLOC_BUFFER(mBgsBuf, kMaxApPayloadDataSize);
        AUDIO_ALLOC_BUFFER(mTelephonyTxBuf, kMaxApPayloadDataSize);
        AUDIO_ALLOC_BUFFER(mRawRecBuf, MAX_RAW_RECORD_SIZE);
        AUDIO_ALLOC_BUFFER(mParsedRecBuf, MAX_PARSED_RECORD_SIZE);
    }

    mSampleRateEnum = SPH_SAMPLE_RATE_32K;

    mApplication = SPH_APPLICATION_INVALID;
    mSpeechMode = SPEECH_MODE_NORMAL;
    mInputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
    mOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;

    mModemLoopbackDelayFrames = DEFAULT_LOOPBACK_DELAY_FRAMES;


    // Record capability
    mRecordSampleRateType = RECORD_SAMPLE_RATE_08K;
    mRecordChannelType    = RECORD_CHANNEL_MONO;
    mRecordType.direction = RECORD_TYPE_MIX;
#ifdef MTK_PHONE_CALL_RECORD_VOICE_ONLY
    mRecordType.dlPosition = RECORD_POS_DL_AFTER_ENH;
#else
    mRecordType.dlPosition = RECORD_POS_DL_END;
#endif
    mVolumeIndex = 0x3;

    mTtyDebugEnable = false;
    mApResetDuringSpeech = false;
    mModemResetDuringSpeech = false;
    mModemDead = false;
    mNeedWaitModemAckAfterApDie = false;
    mReadMsgThreadCreated = false;


    // init var
    mEnableThread = false;
    mEnableThreadDuringSpeech = false;

    hReadSpeechMessageThread = 0;
    hModemStatusMonitorThread = 0;

    isBtSpkDevice = false;
    mBTMode = 0;

    // BT Headset NREC
    mBtHeadsetNrecOn = SpeechEnhancementController::GetInstance()->GetBtHeadsetNrecOn();

    // RTT
    mRttMode = 0;

    memset(&mMdAliveInfo, 0, sizeof(MdAliveInfo));
    mMdAliveInfo.mdVersion = 0;
    mIsParseFail = false;
    mIsUsePreviousParam = false;
    mIsBTSwitchConfig = false;
    //Parser Attribute
    mSpeechParserAttribute.inputDevice = mInputDevice;
    mSpeechParserAttribute.outputDevice =  mOutputDevice;
    mSpeechParserAttribute.idxVolume = mVolumeIndex;
    mSpeechParserAttribute.driverScenario = SPEECH_SCENARIO_SPEECH_ON;
    mSpeechParserAttribute.ttyMode = mTtyMode;
    if (AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport() >= 2) {
        if (appIsFeatureOptionEnabled("MTK_INCALL_HANDSFREE_DMNR")) {
            mSpeechParserAttribute.speechFeatureOn = 6;
        } else {
            mSpeechParserAttribute.speechFeatureOn = 4;
        }
    } else {
        mSpeechParserAttribute.speechFeatureOn = 0;
    }
    mSpeechParam.dataSize = 0;
    mSpeechParam.memorySize = 0;
    mSpeechParam.bufferAddr = NULL;

    mIpcVolumeIndex = mVolumeIndex;
    mIpcLpbkSwitch = 0;
    mSpeechParserAttribute.custType = SPEECH_PARAM_CUST_TYPE_NONE;
    mSpeechParserAttribute.ipcPath = 0;
    mSpeechParserAttribute.extraMode = 0;
    mSpeechParserAttribute.memoryIdx = 0;
    mEcallRXCtrlData.size = 0;
    mEcallRXCtrlData.data = NULL;

    mSpeechMessageQueue = new SpeechMessageQueue(sendSpeechMessageToModemWrapper,
                                                 errorHandleSpeechMessageWrapper,
                                                 this);

    // initial modem side modem status
    mModemSideModemStatus = get_uint32_from_mixctrl(kPropertyKeyModemStatus);
    mVoipRxTypeDl = VOIP_RX_TYPE_MIX;
    mVoipRxTypeUl = VOIP_RX_TYPE_MIX;

    // check if any msg is waiting ack after audioserver crash
    mApWaitAckMsgID = get_uint32_from_mixctrl(kPropertyKeyWaitAckMsgId);

    if (mModemSideModemStatus || mApWaitAckMsgID) {
        mApResetDuringSpeech = true;
    }
    if (mApWaitAckMsgID) {
        mNeedWaitModemAckAfterApDie = true;
    }
    // ecall
#if defined(MTK_SPEECH_ECALL_SUPPORT)
    SpeechEcallController *ecallController = new SpeechEcallController();
    AUDIO_ALLOC_BUFFER(mEcallRXCtrlData.data, MAX_SPEECH_ECALL_BUF_LEN);
#endif

    createThreads();
    RecoverModemSideStatusToInitState();
    AudioEventThreadManager::getInstance()->registerCallback(AUDIO_EVENT_SPEECH_PARAM_CHANGE, callbackSpeechParamChange, this);
}


SpeechDriverNormal::~SpeechDriverNormal() {
    joinThreads();

    if (mSpeechMessageQueue) {
        delete mSpeechMessageQueue;
        mSpeechMessageQueue = NULL;
    }

    AUDIO_FREE_POINTER(mBgsBuf);
    AUDIO_FREE_POINTER(mTelephonyTxBuf);
    AUDIO_FREE_POINTER(mRawRecBuf);
    AUDIO_FREE_POINTER(mParsedRecBuf);

}


/*==============================================================================
 *                     modem status
 *============================================================================*/

bool SpeechDriverNormal::getModemSideModemStatus(
    const modem_status_mask_t modem_status_mask) const {
    return ((mModemSideModemStatus & modem_status_mask) > 0);
}


void SpeechDriverNormal::setModemSideModemStatus(const modem_status_mask_t modem_status_mask) {
    AL_AUTOLOCK(mModemSideModemStatusLock);

    if (getModemSideModemStatus(modem_status_mask) == true) {
        ALOGE("%s(), modem_status_mask: 0x%x already enabled!!", __FUNCTION__, modem_status_mask);
        return;
    }

    mModemSideModemStatus |= modem_status_mask;

    // save mModemSideModemStatus in kernel to avoid medieserver die
    set_uint32_to_mixctrl(kPropertyKeyModemStatus, mModemSideModemStatus);
}


void SpeechDriverNormal::resetModemSideModemStatus(const modem_status_mask_t modem_status_mask) {
    AL_AUTOLOCK(mModemSideModemStatusLock);

    if (getModemSideModemStatus(modem_status_mask) == false) {
        ALOGE("%s(), modem status:0x%x, modem_status_mask: 0x%x not enabled!!",
              __FUNCTION__, mModemSideModemStatus, modem_status_mask);
        return;
    }

    mModemSideModemStatus &= (~modem_status_mask);

    // save mModemSideModemStatus in kernel to avoid medieserver die
    set_uint32_to_mixctrl(kPropertyKeyModemStatus, mModemSideModemStatus);

}


void SpeechDriverNormal::cleanAllModemSideModemStatus() {
    AL_AUTOLOCK(mModemSideModemStatusLock);

    ALOGD("%s(), mModemSideModemStatus: 0x%x to be clean", __FUNCTION__, mModemSideModemStatus);
    mModemSideModemStatus = 0;

    set_uint32_to_mixctrl(kPropertyKeyModemStatus, mModemSideModemStatus);

}


/*==============================================================================
 *                     AP to MD control msg need ack
 *============================================================================*/

void SpeechDriverNormal::setApWaitAckMsgID(sph_msg_t *p_sph_msg) {
    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg1 = 0, time_diff_msg2 = 0;
    uint32_t currentApToMdNeedAckMsgId;

    audio_get_timespec_monotonic(&ts_start);

    // check if previous wait ack msg already reset
    currentApToMdNeedAckMsgId = get_uint32_from_mixctrl(kPropertyKeyWaitAckMsgId);

    audio_get_timespec_monotonic(&ts_stop);
    time_diff_msg1 = get_time_diff_ms(&ts_start, &ts_stop);
    if (currentApToMdNeedAckMsgId != 0) {
        ALOGW("%s(), previous wait ack msg:0x%x not reset! current msg:0x%x",
              __FUNCTION__, currentApToMdNeedAckMsgId, p_sph_msg->msg_id);
        WARNING("previous wait ack msg not reset");
    }
    mApWaitAckMsgID = p_sph_msg->msg_id;

    audio_get_timespec_monotonic(&ts_start);

    set_uint32_to_mixctrl(kPropertyKeyWaitAckMsgId, mApWaitAckMsgID);

    audio_get_timespec_monotonic(&ts_stop);
    time_diff_msg2 = get_time_diff_ms(&ts_start, &ts_stop);
    if ((time_diff_msg1 + time_diff_msg2) >= 1000) {
        ALOGE("%s(),msg_id:0x%x, mixer_ctl_get_value %ju ms, mixer_ctl_set_value %ju ms ",
              __FUNCTION__, p_sph_msg->msg_id, time_diff_msg1, time_diff_msg2);
    }
}


void SpeechDriverNormal::resetApWaitAckMsgID() {
    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg;
    // reset wait ack msg mictrl
    mApWaitAckMsgID = 0;
    audio_get_timespec_monotonic(&ts_start);

    set_uint32_to_mixctrl(kPropertyKeyWaitAckMsgId, 0);

    audio_get_timespec_monotonic(&ts_stop);
    time_diff_msg = get_time_diff_ms(&ts_start, &ts_stop);
    if (time_diff_msg >= 1000) {
        ALOGE("%s(),  mixer_ctl_set_value %ju ms ", __FUNCTION__, time_diff_msg);
    }
}


/*==============================================================================
 *                     msg
 *============================================================================*/

int SpeechDriverNormal::configSpeechInfo(sph_info_t *p_sph_info) {
    int retval = 0;
    uint32_t lenSphParam = 0, idxSphParam = 0;
    bool isLoopback = false;

    if (p_sph_info == NULL) {
        return -EFAULT;
    }

    ASSERT(sizeof(sph_info_t) == SIZE_OF_SPH_INFO);
    memset(p_sph_info, 0, sizeof(sph_info_t));

    /* application */
    p_sph_info->application = mApplication;
    isLoopback = mApplication == SPH_APPLICATION_LOOPBACK ? true : false;

    /* bt_info */
    const bool bt_device_on = audio_is_bluetooth_sco_device(mOutputDevice);

    if (bt_device_on == false) {
#if defined(MTK_BT_SPK_DUAL_PATH_SUPPORT)
        if (isBtSpkDevice) { /* BT Speaker dual path*/
            if (WCNChipController::GetInstance()->IsBTMergeInterfaceSupported() == true) {
                p_sph_info->bt_info = SPH_BT_OFF;
            } else {
                p_sph_info->bt_info = (mBTMode == 0) ? SPH_BT_CVSD : SPH_BT_MSBC;
            }
        } else {
            p_sph_info->bt_info = SPH_BT_OFF;
        }
#else
        p_sph_info->bt_info = SPH_BT_OFF;
#endif
    } else {
        if (WCNChipController::GetInstance()->IsBTMergeInterfaceSupported() == true) {
            p_sph_info->bt_info = SPH_BT_PCM;
        } else {
#if defined(MTK_BT_SPK_DUAL_PATH_SUPPORT)
            p_sph_info->bt_info = (mBTMode == 0) ? SPH_BT_CVSD : SPH_BT_MSBC;
#else
            p_sph_info->bt_info = SPH_BT_CVSD_MSBC;
#endif
        }
    }

    /* sample_rate_enum */
    p_sph_info->sample_rate_enum = mSampleRateEnum;

    /* param */
#ifdef MTK_AUDIODSP_SUPPORT
    p_sph_info->opendsp_flag = isAdspOptionEnable() ?
                               (SpeechDriverOpenDSP::GetInstance(mModemIndex)->isAdspPhoneCallEnhOn()): 0;
#else
    p_sph_info->opendsp_flag = 0;
#endif

    if (mIsBTSwitchConfig || mIsUsePreviousParam) {
        p_sph_info->sph_param_valid = SPH_PARAM_PREVIOUS_VALID;
    } else if (mIsParseFail) {
        p_sph_info->sph_param_valid = SPH_PARAM_INVALID; /* md use default data*/
    } else {
        if (p_sph_info->opendsp_flag) {
            p_sph_info->sph_param_path = SPH_PARAM_VIA_PAYLOAD;
            p_sph_info->sph_param_valid = SPH_PARAM_INVALID; /* bypass sph param for opendsp */
            p_sph_info->sph_param_length = 0;
            p_sph_info->sph_param_index = 0;
            p_sph_info->sph_param_usip_length = 0;
            p_sph_info->sph_param_usip_index = 0;
        } else {
            retval = writeAllSpeechParametersToModem(&lenSphParam, &idxSphParam);
            if (retval == 0) {
                p_sph_info->sph_param_path = mSpeechMessenger->getShareMemoryType();
#if defined(MTK_SPEECH_USIP_EMI_SUPPORT)
                p_sph_info->sph_param_usip_index = idxSphParam;
                p_sph_info->sph_param_usip_length = lenSphParam;
                p_sph_info->sph_param_length = 0;
                p_sph_info->sph_param_index = 0;
#else
                p_sph_info->sph_param_index = (uint16_t)idxSphParam;
                p_sph_info->sph_param_length = lenSphParam;
                p_sph_info->sph_param_usip_length = 0;
                p_sph_info->sph_param_usip_index = 0;
#endif

                if (lenSphParam == 0) {
#if defined(MTK_SPEECH_USIP_EMI_SUPPORT)
                    p_sph_info->sph_param_valid = SPH_PARAM_PREVIOUS_VALID; /* md use previous data */
#else
                    p_sph_info->sph_param_valid = SPH_PARAM_INVALID; /* md use default data*/
#endif
                } else {
                    p_sph_info->sph_param_valid = SPH_PARAM_VALID;
                }
            } else {
                p_sph_info->sph_param_path = SPH_PARAM_VIA_PAYLOAD;
                p_sph_info->sph_param_valid = SPH_PARAM_INVALID;
                p_sph_info->sph_param_length = 0;
                p_sph_info->sph_param_index = 0;
                p_sph_info->sph_param_usip_length = 0;
                p_sph_info->sph_param_usip_index = 0;
            }
        }
    }

    if (AudioSmartPaController::getInstance()->isDualSmartPA()) {
        p_sph_info->num_smart_pa = 2;
    } else {
        p_sph_info->num_smart_pa = 1;
    }
    /* ext_dev_info */
    switch (mOutputDevice) {
    case AUDIO_DEVICE_OUT_EARPIECE:
        if (AudioSmartPaController::getInstance()->isSmartPaSphEchoRefNeed(isLoopback, mOutputDevice)) {
            p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_SMARTPA_SPEAKER;
        } else {
#ifdef MTK_AUDIO_SPEAKER_PATH_3_IN_1
            p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_VIBRATION_RECEIVER;
#else
            p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_DEFULAT;
#endif
        }
        break;
    case AUDIO_DEVICE_OUT_SPEAKER:
        if (AudioSmartPaController::getInstance()->isSmartPaSphEchoRefNeed(isLoopback, mOutputDevice)) {
#if defined(MTK_AUDIO_SPEAKER_PATH_2_IN_1) || defined(MTK_AUDIO_SPEAKER_PATH_3_IN_1)
            p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_SMARTPA_VIBRATION_SPEAKER;
#else
            if (AudioSmartPaController::getInstance()->isDualSmartPA()) {
                p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_DUALSMARTPA_SPEAKER;
            } else {
                p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_SMARTPA_SPEAKER;
            }
#endif
        } else {
#if defined(MTK_AUDIO_SPEAKER_PATH_2_IN_1) || defined(MTK_AUDIO_SPEAKER_PATH_3_IN_1)
            p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_VIBRATION_SPEAKER;
#else
            p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_DEFULAT;
#endif /* end of MTK_AUDIO_SPEAKER_PATH_2_IN_1 || MTK_AUDIO_SPEAKER_PATH_3_IN_1 */
        }
        break;
    case AUDIO_DEVICE_OUT_USB_DEVICE:
    case AUDIO_DEVICE_OUT_USB_HEADSET:
        p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_USB_AUDIO;
        break;
    case AUDIO_DEVICE_OUT_WIRED_HEADSET:
    case AUDIO_DEVICE_OUT_WIRED_HEADPHONE:
        p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_EARPHONE;
        break;
    default:
        p_sph_info->ext_dev_info = SPH_EXT_DEV_INFO_DEFULAT;
        break;
    }


    /* loopback */
    if (p_sph_info->application != SPH_APPLICATION_LOOPBACK) {
        p_sph_info->loopback_flag  = 0;
        p_sph_info->loopback_delay = 0;
    } else {
        p_sph_info->loopback_flag = 0;
        /* bt codec */
        if (mUseBtCodec == false) {
            p_sph_info->loopback_flag |= SPH_LOOPBACK_INFO_FLAG_DISABLE_BT_CODEC;
        }
        /* delay ms */
        if (USE_DEDICATED_LOOPBACK_DELAY_FRAMES == true) {
            p_sph_info->loopback_flag |= SPH_LOOPBACK_INFO_FLAG_DELAY_SETTING;
            p_sph_info->loopback_delay = mModemLoopbackDelayFrames;
        } else {
            p_sph_info->loopback_delay = 0;
        }
    }


    /* echo_ref_delay_ms */
    if (p_sph_info->bt_info == SPH_BT_CVSD_MSBC ||
        p_sph_info->bt_info == SPH_BT_MSBC ||
        p_sph_info->bt_info == SPH_BT_CVSD) {
        if (mBtHeadsetNrecOn == false) {
            p_sph_info->echo_ref_delay_ms = 0;
        } else {
            getBtDelayTime(&p_sph_info->echo_ref_delay_ms);
        }
    }
    ASSERT(p_sph_info->echo_ref_delay_ms <= 256); /* modem limitation */

    /* mic_delay_ms */
    int delayTime = AudioSmartPaController::getInstance()->getSmartPaDelayUs(mOutputDevice);
    switch (p_sph_info->ext_dev_info) {
    case SPH_EXT_DEV_INFO_DUALSMARTPA_SPEAKER:
    case SPH_EXT_DEV_INFO_SMARTPA_SPEAKER:
    case SPH_EXT_DEV_INFO_SMARTPA_VIBRATION_SPEAKER:
        if (delayTime > 0) {
            p_sph_info->mic_delay_ms = delayTime / 1000;
        } else {
            p_sph_info->mic_delay_ms = 0;
        }
        break;
    case SPH_EXT_DEV_INFO_USB_AUDIO:
        getUsbDelayTime(&p_sph_info->mic_delay_ms);
        break;
    default:
        p_sph_info->mic_delay_ms = 0;
    }
    ASSERT(p_sph_info->mic_delay_ms <= 64); /* modem limitation */

    /* driver param */
#if defined(MTK_SPEECH_USIP_EMI_SUPPORT)
    getDriverParam(DRIVER_PARAM_COMMON_PAR, &p_sph_info->drv_common_param);
    getDriverParam(DRIVER_PARAM_DEBUG_INFO, &p_sph_info->drv_debug_info);
#endif

    /* speech enhancement function dynamic mask */
    sph_enh_mask_struct_t mask = SpeechEnhancementController::GetInstance()->GetSpeechEnhancementMask();
    p_sph_info->enh_dynamic_ctrl = speechEnhancementMaskWrapper(mask.dynamic_func);

    /* mute */
    p_sph_info->mute_info = mMuteInfo;

    /* dump info */
    ALOGD("%s(), app: %d, bt: %d, rate enum: %d, opendsp: %d, path: %d, param emi valid: %d, "
          "param size: 0x%x, param index: 0x%x, ext_dev_info: %d, loopback_flag: 0x%x, "
          "loopback_delay: %d, aec delay: %d, mic delay: %d, mute_info: 0x%x, "
          "enh_dynamic_ctrl: 0x%x, usip param size: 0x%x, usip param index: 0x%x, "
          "com par[0]: %d, debug info[0]: %d, smartPA onfig: %d",
          __FUNCTION__,
          p_sph_info->application,
          p_sph_info->bt_info,
          p_sph_info->sample_rate_enum,
          p_sph_info->opendsp_flag,
          p_sph_info->sph_param_path,
          p_sph_info->sph_param_valid,
          p_sph_info->sph_param_length,
          p_sph_info->sph_param_index,
          p_sph_info->ext_dev_info,
          p_sph_info->loopback_flag,
          p_sph_info->loopback_delay,
          p_sph_info->echo_ref_delay_ms,
          p_sph_info->mic_delay_ms,
          p_sph_info->mute_info,
          p_sph_info->enh_dynamic_ctrl,
          p_sph_info->sph_param_usip_length,
          p_sph_info->sph_param_usip_index,
          p_sph_info->drv_common_param[0],
          p_sph_info->drv_debug_info[0],
          p_sph_info->num_smart_pa);


    return 0;
}


int SpeechDriverNormal::configMailBox(
    sph_msg_t *p_sph_msg,
    uint16_t msg_id,
    uint16_t param_16bit,
    uint32_t param_32bit) {

    if (p_sph_msg == NULL) {
        return -EFAULT;
    }

    memset(p_sph_msg, 0, sizeof(sph_msg_t));

    p_sph_msg->buffer_type = SPH_MSG_BUFFER_TYPE_MAILBOX;
    p_sph_msg->msg_id = msg_id;
    p_sph_msg->param_16bit = param_16bit;
    p_sph_msg->param_32bit = param_32bit;

    return 0;
}


int SpeechDriverNormal::configPayload(
    sph_msg_t *p_sph_msg,
    uint16_t msg_id,
    uint16_t data_type,
    void    *data_addr,
    uint16_t data_size) {

    if (p_sph_msg == NULL) {
        return -EFAULT;
    }

    memset(p_sph_msg, 0, sizeof(sph_msg_t));

    p_sph_msg->buffer_type = SPH_MSG_BUFFER_TYPE_PAYLOAD;
    p_sph_msg->msg_id = msg_id;

    p_sph_msg->payload_data_type = data_type;
    p_sph_msg->payload_data_size = data_size;
    p_sph_msg->payload_data_addr = data_addr;

    return 0;
}


int SpeechDriverNormal::sendMailbox(sph_msg_t *p_sph_msg,
                                    uint16_t msg_id,
                                    uint16_t param_16bit,
                                    uint32_t param_32bit) {
    configMailBox(p_sph_msg, msg_id, param_16bit, param_32bit);
    if (isApMsgBypassQueue(p_sph_msg) == true) {
        return sendSpeechMessageToModem(p_sph_msg);
    } else {
        return sendSpeechMessageToQueue(p_sph_msg);
    }
}


int SpeechDriverNormal::sendPayload(sph_msg_t *p_sph_msg,
                                    uint16_t msg_id,
                                    uint16_t data_type,
                                    void    *data_buf,
                                    uint16_t data_size) {
    configPayload(p_sph_msg, msg_id, data_type, data_buf, data_size);
    if (isApMsgBypassQueue(p_sph_msg) == true) {
        return sendSpeechMessageToModem(p_sph_msg);
    } else {
        return sendSpeechMessageToQueue(p_sph_msg);
    }
}


/*==============================================================================
 *                     queue
 *============================================================================*/

int SpeechDriverNormal::sendSpeechMessageToQueue(sph_msg_t *p_sph_msg) {

    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    if (mSpeechMessageQueue == NULL) {
        ALOGW("%s(), mSpeechMessageQueue == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    uint32_t block_thread_ms = getBlockThreadTimeMsByID(p_sph_msg);
    return mSpeechMessageQueue->sendSpeechMessageToQueue(p_sph_msg, block_thread_ms);
}


int SpeechDriverNormal::sendSpeechMessageAckToQueue(sph_msg_t *p_sph_msg) {

    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    if (isMdAckBack(p_sph_msg) == false) {
        ALOGW("%s(), isMdAckBack(0x%x) failed!! return", __FUNCTION__, p_sph_msg->msg_id);
        return -EFAULT;
    }

    if (mSpeechMessageQueue == NULL) {
        ALOGW("%s(), mSpeechMessageQueue == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    return mSpeechMessageQueue->sendSpeechMessageAckToQueue(p_sph_msg);
}


int SpeechDriverNormal::sendSpeechMessageToModemWrapper(void *arg, sph_msg_t *p_sph_msg) {
    SpeechDriverNormal *pSpeechDriver = static_cast<SpeechDriverNormal *>(arg);

    if (pSpeechDriver == NULL) {
        ALOGE("%s(), static_cast failed!!", __FUNCTION__);
        return -EMEDIUMTYPE;
    }

    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    return pSpeechDriver->sendSpeechMessageToModem(p_sph_msg);
}


int SpeechDriverNormal::sendSpeechMessageToModem(sph_msg_t *p_sph_msg) {
    /* only config modem error state here to using lock to protect it */
    static AudioLock send_message_lock;
    static bool b_epof = (get_uint32_from_mixctrl(kPropertyKeyModemEPOF) != 0);
    static bool b_modem_crash_during_call = false;
    static bool b_during_call = false;

    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg = 0;

    int retval = 0;

    AL_AUTOLOCK_MS(send_message_lock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS);

    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    if (mSpeechMessenger == NULL) {
        ALOGW("%s(), mSpeechMessenger == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    AL_LOCK(mApWaitAckMsgIDLock);
    /* send message to modem */
    if ((b_epof == true || b_modem_crash_during_call == true || mModemResetDuringSpeech == true) &&
        p_sph_msg->msg_id != MSG_A2M_MD_ALIVE_ACK_BACK) {
        ALOGW("%s(), b_epof: %d, b_modem_crash_during_call: %d, mModemResetDuringSpeech: %d!! bypass msg 0x%x", __FUNCTION__,
              b_epof, b_modem_crash_during_call, mModemResetDuringSpeech, p_sph_msg->msg_id);
        retval = -EPIPE;
    } else {
        retval = mSpeechMessenger->sendSpeechMessage(p_sph_msg);
    }

    if (retval == 0) {
        if (isNeedDumpMsg(p_sph_msg) == true) {
            PRINT_SPH_MSG(ALOGD, "send msg done", p_sph_msg);
        } else {
            PRINT_SPH_MSG(SPH_LOG_D, "send msg done", p_sph_msg);
        }
    } else if (retval != 0) {
        PRINT_SPH_MSG(ALOGE, "send msg failed!!", p_sph_msg);
        /* notate whether modem crashed during phone call or not */
        /* cannot use GetApSideModemStatus because need lock protect it */
    }

    /* ctrl msg need ack, keep in property */
    if (retval == 0 && isApNeedAck(p_sph_msg) == true) {
        setApWaitAckMsgID(p_sph_msg);
    }
    AL_UNLOCK(mApWaitAckMsgIDLock);

    /* config modem state for error handling */
    switch (p_sph_msg->msg_id) {
    case MSG_A2M_SPH_ON:
        b_during_call = true;
        break;
    case MSG_A2M_SPH_OFF:
        /* this call is end, suppose modem will be recovered before next call */
        b_modem_crash_during_call = false;
        b_during_call = false;
        break;
    case MSG_A2M_EPOF_ACK:
        /* enable EPOF only after EPOF ack is sent to modem!! */
        b_epof = true;
        set_uint32_to_mixctrl(kPropertyKeyModemEPOF, b_epof);
        break;
    case MSG_A2M_MD_ALIVE_ACK_BACK:
        /* disable EPOF */
        b_epof = false;
        set_uint32_to_mixctrl(kPropertyKeyModemEPOF, b_epof);
        break;
    default:
        break;
    }

    if (retval != 0 && b_during_call == true) {
        b_modem_crash_during_call = true;
    }
    return retval;
}


int SpeechDriverNormal::errorHandleSpeechMessageWrapper(void *arg, sph_msg_t *p_sph_msg) {
    SpeechDriverNormal *pSpeechDriver = static_cast<SpeechDriverNormal *>(arg);

    if (pSpeechDriver == NULL) {
        ALOGE("%s(), static_cast failed!!", __FUNCTION__);
        return -EMEDIUMTYPE;
    }

    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    return pSpeechDriver->errorHandleSpeechMessage(p_sph_msg);
}


int SpeechDriverNormal::errorHandleSpeechMessage(sph_msg_t *p_sph_msg) {
    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    int retval = 0;

    if (getSyncType(p_sph_msg->msg_id) != SPH_MSG_HANDSHAKE_AP_CTRL_NEED_ACK) {
        PRINT_SPH_MSG(ALOGD, "no need ack. return", p_sph_msg);
        return 0;
    }

    retval = makeFakeMdAckMsgFromApMsg(p_sph_msg);
    if (retval != 0) {
        PRINT_SPH_MSG(ALOGW, "make fake modem ack error!! return", p_sph_msg);
        return retval;
    }

    PRINT_SPH_MSG(ALOGD, "make fake modem ack", p_sph_msg);
    retval = processModemAckMessage(p_sph_msg);

    return retval;
}


int SpeechDriverNormal::readSpeechMessageFromModem(sph_msg_t *p_sph_msg) {
    int retval = 0;

    AL_AUTOLOCK_MS(mReadMessageLock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS);

    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    if (mSpeechMessenger == NULL) {
        ALOGW("%s(), mSpeechMessenger == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    SPH_LOG_D("%s(+)", __FUNCTION__);
    retval = mSpeechMessenger->readSpeechMessage(p_sph_msg);
    SPH_LOG_D("%s(-), msg id 0x%x", __FUNCTION__, p_sph_msg->msg_id);

    return retval;
}


/*==============================================================================
 *                     thread
 *============================================================================*/

void SpeechDriverNormal::createThreads() {
    int ret = 0;

    mEnableThread = true;
    ret = pthread_create(&hReadSpeechMessageThread, NULL,
                         SpeechDriverNormal::readSpeechMessageThread,
                         (void *)this);
    ASSERT(ret == 0);
}


void SpeechDriverNormal::joinThreads() {
    mEnableThread = false;

    pthread_join(hReadSpeechMessageThread, NULL);
}


void *SpeechDriverNormal::readSpeechMessageThread(void *arg) {
    SpeechDriverNormal *pSpeechDriver = NULL;
    sph_msg_t sph_msg;
    int retval = 0;

    char thread_name[128] = {0};
    CONFIG_THREAD(thread_name, ANDROID_PRIORITY_AUDIO);

    pSpeechDriver = static_cast<SpeechDriverNormal *>(arg);
    if (pSpeechDriver == NULL) {
        ALOGE("%s(), NULL!! pSpeechDriver %p", __FUNCTION__, pSpeechDriver);
        goto READ_MSG_THREAD_DONE;
    }

    AL_LOCK(pSpeechDriver->mReadMsgThreadCreatedLock);
    pSpeechDriver->mReadMsgThreadCreated = true;
    // signal ap recovery process
    if (pSpeechDriver->mNeedWaitModemAckAfterApDie == true) {
        AL_SIGNAL(pSpeechDriver->mReadMsgThreadCreatedLock);
    }
    AL_UNLOCK(pSpeechDriver->mReadMsgThreadCreatedLock);

    while (pSpeechDriver->mEnableThread == true) {
        /* wait until modem message comes */
        memset(&sph_msg, 0, sizeof(sph_msg_t));
        retval = pSpeechDriver->readSpeechMessageFromModem(&sph_msg);
        if (retval != 0) {
            ALOGV("%s(), readSpeechMessageFromModem failed!!", __FUNCTION__);
            usleep(100 * 1000);
            continue;
        }
        pSpeechDriver->processModemMessage(&sph_msg);
    }


READ_MSG_THREAD_DONE:
    ALOGV("%s terminated", thread_name);
    pthread_exit(NULL);
    pSpeechDriver->mReadMsgThreadCreated = false;
    return NULL;
}


void SpeechDriverNormal::createThreadsDuringSpeech() {
    int ret = 0;

    mEnableThreadDuringSpeech = true;
    ret = pthread_create(&hModemStatusMonitorThread, NULL,
                         SpeechDriverNormal::modemStatusMonitorThread,
                         (void *)this);
    ASSERT(ret == 0);
}


void SpeechDriverNormal::joinThreadsDuringSpeech() {
    if (mEnableThreadDuringSpeech == true) {
        AL_LOCK_MS(mModemStatusMonitorThreadLock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS);
        mEnableThreadDuringSpeech = false;
        AL_SIGNAL(mModemStatusMonitorThreadLock);
        AL_UNLOCK(mModemStatusMonitorThreadLock);

        pthread_join(hModemStatusMonitorThread, NULL);
    }
}


void *SpeechDriverNormal::modemStatusMonitorThread(void *arg) {
    SpeechDriverNormal *pSpeechDriver = NULL;
    SpeechMessageQueue *pSpeechMessageQueue = NULL;

    int retval = 0;

    char thread_name[128] = {0};
    CONFIG_THREAD(thread_name, ANDROID_PRIORITY_AUDIO);

    pSpeechDriver = static_cast<SpeechDriverNormal *>(arg);

    if (pSpeechDriver == NULL) {
        ALOGE("%s(), NULL!! pSpeechDriver %p", __FUNCTION__, pSpeechDriver);
        goto MODEM_STATUS_MONITOR_THREAD_DONE;
    }

    pSpeechMessageQueue = pSpeechDriver->mSpeechMessageQueue;
    if (pSpeechMessageQueue == NULL) {
        ALOGE("%s(), NULL!! pSpeechMessageQueue %p", __FUNCTION__, pSpeechMessageQueue);
        goto MODEM_STATUS_MONITOR_THREAD_DONE;
    }

    while (pSpeechDriver->mEnableThreadDuringSpeech == true) {
        if (pSpeechDriver->CheckModemIsReady() == false) {
            ALOGW("%s(), modem status error!! notify queue", __FUNCTION__);

            AL_LOCK(pSpeechDriver->mModemDeadLock);
            pSpeechDriver->mModemDead = true;
            pSpeechDriver->mModemResetDuringSpeech = true;
            pSpeechMessageQueue->notifyQueueToStopWaitingAck();
            AL_UNLOCK(pSpeechDriver->mModemDeadLock);
            break;
        }

        AL_LOCK_MS(pSpeechDriver->mModemStatusMonitorThreadLock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS);
        AL_WAIT_MS(pSpeechDriver->mModemStatusMonitorThreadLock, 200); // check status each 200 ms
        AL_UNLOCK(pSpeechDriver->mModemStatusMonitorThreadLock);
    }


MODEM_STATUS_MONITOR_THREAD_DONE:
    ALOGV("%s terminated", thread_name);
    pthread_exit(NULL);
    return NULL;
}


/*==============================================================================
 *                     process msg
 *============================================================================*/

int SpeechDriverNormal::processModemMessage(sph_msg_t *p_sph_msg) {
    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg = 0;

    int retval = 0;

    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    /* get time for start */
    audio_get_timespec_monotonic(&ts_start);

    /* process modem message */
    switch (getSyncType(p_sph_msg->msg_id)) {
    case SPH_MSG_HANDSHAKE_MD_ACK_BACK_AP_CTRL:
        AL_LOCK(mModemDeadLock);
        if (mModemDead) {
            ALOGW("%s(), modem is dead, do not process msg:0x%x", __FUNCTION__, p_sph_msg->msg_id);
            AL_UNLOCK(mModemDeadLock);
            break;
        }
        retval = processModemAckMessage(p_sph_msg);
        /* check need wait ack after ap die */
        if (mNeedWaitModemAckAfterApDie) {
            mNeedWaitModemAckAfterApDie = false;
            /* notify waitModemAckAfterApDie */
            AL_LOCK(mWaitModemAckAfterApDieLock);
            AL_SIGNAL(mWaitModemAckAfterApDieLock);
            AL_UNLOCK(mWaitModemAckAfterApDieLock);
        } else { /* notify message queue */
            sendSpeechMessageAckToQueue(p_sph_msg);
        }
        AL_UNLOCK(mModemDeadLock);
        break;
    case SPH_MSG_HANDSHAKE_MD_CTRL_BYPASS_ACK:
    case SPH_MSG_HANDSHAKE_MD_CTRL_NEED_ACK:
        retval = processModemControlMessage(p_sph_msg);
        break;
    case SPH_MSG_HANDSHAKE_MD_REQUEST_DATA:
    case SPH_MSG_HANDSHAKE_MD_NOTIFY_DATA:
        retval = processModemDataMessage(p_sph_msg);
        break;
    default:
        ALOGW("%s(), p_sph_msg->msg_id 0x%x not support!!", __FUNCTION__, p_sph_msg->msg_id);
        retval = -EINVAL;
    }

    /* get time for stop */
    audio_get_timespec_monotonic(&ts_stop);
    time_diff_msg = get_time_diff_ms(&ts_start, &ts_stop);
    if (time_diff_msg >= MAX_MSG_PROCESS_TIME_MS) {
        ALOGW("%s(), msg 0x%x process time %ju ms is too long", __FUNCTION__,
              p_sph_msg->msg_id, time_diff_msg);
    }

    /* NOTICE: Must copy payload/modem data before return!! */
    return retval;
}


int SpeechDriverNormal::processModemAckMessage(sph_msg_t *p_sph_msg) {
    AL_LOCK(mApWaitAckMsgIDLock);
    if (mApWaitAckMsgID > 0) {
        // if ack match waitAckMsg, reset property
        if (isAckMessageInPairByID(mApWaitAckMsgID, p_sph_msg->msg_id)) {
            resetApWaitAckMsgID();
            ALOGV("%s(), reset property, ack:0x%x ", __FUNCTION__, p_sph_msg->msg_id);
        } else {
            ALOGW("%s(), ack:0x%x not in pair with msg:0x%x!", __FUNCTION__,
                  p_sph_msg->msg_id, mApWaitAckMsgID);
            WARNING("ack not in pair with msg!");
        }
    }
    AL_UNLOCK(mApWaitAckMsgIDLock);
    /* config modem status */
    switch (p_sph_msg->msg_id) {
    case MSG_M2A_MUTE_SPH_UL_ACK:
        break;
    case MSG_M2A_MUTE_SPH_DL_ACK:
        break;
    case MSG_M2A_MUTE_SPH_DYNAMIC_DL_ACK:
        break;
    case MSG_M2A_MUTE_SPH_UL_SOURCE_ACK:
        break;
    case MSG_M2A_SPH_ON_ACK:
        setModemSideModemStatus(SPEECH_STATUS_MASK);
        break;
    case MSG_M2A_SPH_OFF_ACK:
        if (mSpeechMessenger != NULL) { mSpeechMessenger->resetShareMemoryIndex(); }
        joinThreadsDuringSpeech();
        mSpeechMessageQueue->resetStopWaitAckFlag();
        resetModemSideModemStatus(SPEECH_STATUS_MASK);
        break;
    case MSG_M2A_SPH_DEV_CHANGE_ACK:
        break;
    case MSG_M2A_PNW_ON_ACK:
        setModemSideModemStatus(P2W_STATUS_MASK);
        break;
    case MSG_M2A_PNW_OFF_ACK:
        resetModemSideModemStatus(P2W_STATUS_MASK);
        break;
    case MSG_M2A_VM_REC_ON_ACK:
        setModemSideModemStatus(VM_RECORD_STATUS_MASK);
        break;
    case MSG_M2A_VM_REC_OFF_ACK:
        resetModemSideModemStatus(VM_RECORD_STATUS_MASK);
        break;
    case MSG_M2A_RECORD_RAW_PCM_ON_ACK:
        setModemSideModemStatus(RAW_RECORD_STATUS_MASK);
        break;
    case MSG_M2A_RECORD_RAW_PCM_OFF_ACK:
        resetModemSideModemStatus(RAW_RECORD_STATUS_MASK);
        break;
    case MSG_M2A_CTM_ON_ACK:
        setModemSideModemStatus(TTY_STATUS_MASK);
        break;
    case MSG_M2A_CTM_OFF_ACK:
        resetModemSideModemStatus(TTY_STATUS_MASK);
        break;
    case MSG_M2A_BGSND_ON_ACK:
        setModemSideModemStatus(BGS_STATUS_MASK);
        break;
    case MSG_M2A_BGSND_OFF_ACK:
        resetModemSideModemStatus(BGS_STATUS_MASK);
        break;
    case MSG_M2A_EM_DYNAMIC_SPH_ACK:
        break;
    case MSG_M2A_DYNAMIC_PAR_IN_STRUCT_SHM_ACK:
        break;
    case MSG_M2A_VIBSPK_PARAMETER_ACK:
        break;
    case MSG_M2A_SMARTPA_PARAMETER_ACK:
        break;
    case MSG_M2A_VOIP_RX_ON_ACK:
        setModemSideModemStatus(PCM_MIXER_STATUS_MASK);
        break;
    case MSG_M2A_VOIP_RX_OFF_ACK:
        resetModemSideModemStatus(PCM_MIXER_STATUS_MASK);
        break;
    case MSG_M2A_TELEPHONY_TX_ON_ACK:
        setModemSideModemStatus(TELEPHONY_TX_STATUS_MASK);
        break;
    case MSG_M2A_TELEPHONY_TX_OFF_ACK:
        resetModemSideModemStatus(TELEPHONY_TX_STATUS_MASK);
        break;
    case MSG_M2A_ECALL_CTL_SEQ_SWITCH_ACK:
    case MSG_M2A_IVS_SWITCH_ACK:
    case MSG_M2A_PSAP_SWITCH_ACK:
    case MSG_M2A_ECALL_MSD_ACK:
    case MSG_M2A_ECALL_TX_CTRL_PAR_ACK:
        break;

    default:
        ALOGE("%s(), not supported msg_id 0x%x", __FUNCTION__, p_sph_msg->msg_id);
    }

    return 0;
}


void SpeechDriverNormal::processModemEPOF() {

    ALOGD("%s(), mIpcLpbkSwitch 0x%x", __FUNCTION__, mIpcLpbkSwitch);
    if (mIpcLpbkSwitch != IPC_LOOPBACK_OFF) {
        mIpcLpbkSwitch = IPC_LOOPBACK_OFF;
        processIpcLoopback();
    }

    /* send EPOF ack to modem */
    sph_msg_t sph_msg;
    sendMailbox(&sph_msg, MSG_A2M_EPOF_ACK, 0, 0);

    /* notify queue */
    if (mSpeechMessageQueue != NULL) { mSpeechMessageQueue->notifyQueueToStopWaitingAck(); }
}


void SpeechDriverNormal::processModemAlive(sph_msg_t *sphMsg) {
#if defined(MTK_SPEECH_USIP_EMI_SUPPORT)
    MdAliveInfo mdAliveInfo;
    MdAliveInfo *pMdAliveInfo = NULL;

    uint16_t dataType = 0;
    uint16_t dataSize = 0;

    int retval = 0;

    /* error handling */
    if (sphMsg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return;
    }
    ALOGD("%s(), buffer_type=0x%x, length=0x%x, read_idx=0x%x",
          __FUNCTION__, sphMsg->buffer_type, sphMsg->length, sphMsg->rw_index);

    if (sphMsg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
        dataSize = sizeof(MdAliveInfo);
        retval = mSpeechMessenger->readMdDataFromShareMemory(
                     &mdAliveInfo,
                     &dataType,
                     &dataSize,
                     sphMsg->length,
                     sphMsg->rw_index);
        if (retval != 0) {
            PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", sphMsg);
            return;
        }
        pMdAliveInfo = &mdAliveInfo;
    } else if (sphMsg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
        ASSERT(sphMsg->payload_data_idx == sphMsg->payload_data_total_idx);

        pMdAliveInfo = (MdAliveInfo *)sphMsg->payload_data_addr;
        dataType = sphMsg->payload_data_type;
        dataSize = sphMsg->payload_data_size;
    } else {
        PRINT_SPH_MSG(ALOGW, "bad buffer_type!!", sphMsg);
        return;
    }

    /* check value */
    if (dataType != SHARE_BUFF_DATA_TYPE_CCCI_MD_ALIVE_INFO) {
        PRINT_SPH_MSG(ALOGE, "bad data_type!!", sphMsg);
        WARNING("bad data_type!!");
        return;
    }

    if (dataSize != sizeof(MdAliveInfo)) {
        PRINT_SPH_MSG(ALOGE, "bad data_size!!", sphMsg);
        WARNING("bad data_size!!");
        return;
    }
    memcpy(&mMdAliveInfo, pMdAliveInfo, sizeof(MdAliveInfo));
    set_uint32_to_mixctrl(kPropertyKeyModemHeadVersion, mMdAliveInfo.headerMdVersion);
    set_uint32_to_mixctrl(kPropertyKeyModemVersion, mMdAliveInfo.mdVersion);
    /* set network info in property */
    ALOGD("%s(), headerMdVersion: 0x%x, mdVersion: 0x%x",
          __FUNCTION__,
          mMdAliveInfo.headerMdVersion,
          mMdAliveInfo.mdVersion);
#else
    (void) sphMsg;
#endif
    ALOGD("%s(), mIpcLpbkSwitch 0x%x", __FUNCTION__, mIpcLpbkSwitch);
    if (mIpcLpbkSwitch != IPC_LOOPBACK_OFF) {
        mIpcLpbkSwitch = IPC_LOOPBACK_OFF ;
        processIpcLoopback();
    }

    /* set mStopWaitAck when EPOF and reset when MD alive */
    if (mSpeechMessageQueue != NULL) { mSpeechMessageQueue->resetStopWaitAckFlag(); }

    /* send alive ack to modem */
    sph_msg_t sph_msg;
    sendMailbox(&sph_msg, MSG_A2M_MD_ALIVE_ACK_BACK, 0, 0);
    //notify event to do reopen voice call
    if (GetApSideModemStatus(SPEECH_STATUS_MASK)) {
        AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_PHONECALL_REOPEN, this);
    }
}


void SpeechDriverNormal::processNetworkCodecInfo(sph_msg_t *p_sph_msg) {
    spcCodecInfoStruct codec_info;
    spcCodecInfoStruct *p_codec_info = NULL;

    uint16_t data_type = 0;
    uint16_t data_size = 0;

    int retval = 0;

    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return;
    }

    if (mApResetDuringSpeech == true) {
        PRINT_SPH_MSG(ALOGW, "mApResetDuringSpeech == true!! drop md data", p_sph_msg);
        return;
    }


    if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
        data_size = sizeof(spcCodecInfoStruct);
        retval = mSpeechMessenger->readMdDataFromShareMemory(
                     &codec_info,
                     &data_type,
                     &data_size,
                     p_sph_msg->length,
                     p_sph_msg->rw_index);
        if (retval != 0) {
            PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
            return;
        }

        p_codec_info = &codec_info;
    } else if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
        ASSERT(p_sph_msg->payload_data_idx == p_sph_msg->payload_data_total_idx);

        p_codec_info = (spcCodecInfoStruct *)p_sph_msg->payload_data_addr;
        data_type = p_sph_msg->payload_data_type;
        data_size = p_sph_msg->payload_data_size;
    } else {
        PRINT_SPH_MSG(ALOGW, "bad buffer_type!!", p_sph_msg);
        return;
    }

    /* check value */
    if (data_type != SHARE_BUFF_DATA_TYPE_CCCI_NW_CODEC_INFO) {
        PRINT_SPH_MSG(ALOGE, "bad data_type!!", p_sph_msg);
        WARNING("bad data_type!!");
        return;
    }

    if (data_size != sizeof(spcCodecInfoStruct)) {
        PRINT_SPH_MSG(ALOGE, "bad data_size!!", p_sph_msg);
        WARNING("bad data_size!!");
        return;
    }

    /* set network info in property */
    ALOGD("%s(), length: 0x%x, rw_index: 0x%x, RilSphCodecInfo: \"%s\", RilHdVoiceStatus: \"%s\"",
          __FUNCTION__,
          p_sph_msg->length,
          p_sph_msg->rw_index,
          p_codec_info->codecInfo,
          p_codec_info->codecOp);

    /* send read ack to modem */
    sph_msg_t sph_msg;
    sendMailbox(&sph_msg, MSG_A2M_NW_CODEC_INFO_READ_ACK, 0, 0);
}


int SpeechDriverNormal::processModemControlMessage(sph_msg_t *p_sph_msg) {
    switch (p_sph_msg->msg_id) {
    case MSG_M2A_EPOF_NOTIFY: /* need ack */
        PRINT_SPH_MSG(ALOGD, "EPOF!!", p_sph_msg);
        processModemEPOF();
        break;
    case MSG_M2A_MD_ALIVE: /* need ack */
        PRINT_SPH_MSG(ALOGD, "MD Alive", p_sph_msg);
        mModemDead = false;
        processModemAlive(p_sph_msg);
        break;
    case MSG_M2A_EM_DATA_REQUEST: /* bypass ack */
        break; /* lagecy control, do nothing after 93 modem */
    case MSG_M2A_NETWORK_STATUS_NOTIFY: { /* bypass ack */
        PRINT_SPH_MSG(ALOGD, "RAT INFO", p_sph_msg);
#ifdef MTK_AUDIO_GAIN_TABLE // speech network type change
        AudioVolumeFactory::CreateAudioVolumeController()->speechNetworkChange(p_sph_msg->param_16bit);
#endif
        bool isNetworkSupport = (p_sph_msg->param_16bit & (0x1 << 15)) != 0;
        if (isNetworkSupport) {
            mBand = (SPEECH_BAND)((p_sph_msg->param_16bit >> 4) & 0x3); //info[4:5]
        } else {
            mBand = (SPEECH_BAND)((p_sph_msg->param_16bit >> 3) & 0x7); //info[3:5]
        }

        if (((p_sph_msg->param_16bit) & 0xF) == mNetworkRate) {
            break;
        }
        mNetworkRate = ((p_sph_msg->param_16bit) & 0xF);
        AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_SPEECH_NETWORK_CHANGE, this);
        break;
    }
    case MSG_M2A_NW_CODEC_INFO_NOTIFY: /* need ack */
        processNetworkCodecInfo(p_sph_msg);
        break;
    case MSG_M2A_FROM_CUST_NOTIFY_LOOPBACK: /* bypass ack */
        mIpcLpbkSwitch = p_sph_msg->param_16bit & 0xff ;
        mSpeechParserAttribute.ipcPath = p_sph_msg->param_32bit & 0xff;
        mIpcVolumeIndex = (p_sph_msg->param_32bit & 0xff00) >> 8;//should be 5
        mSpeechParserAttribute.idxVolume = mIpcVolumeIndex;
        ALOGD("%s(), (0x%x) mIpcLpbkSwitch: 0x%x, ipcPath: 0x%x, idxVolume:0x%x",
              __FUNCTION__, p_sph_msg->msg_id, mIpcLpbkSwitch,  mSpeechParserAttribute.ipcPath,
              mSpeechParserAttribute.idxVolume);
        processIpcLoopback();
        break;
#if defined(MTK_SPEECH_ECALL_SUPPORT)
    case MSG_M2A_ECALL_HANDSHAKE_INFO:
        ALOGV("%s(), msg_id:0x%x ", __FUNCTION__, p_sph_msg->msg_id);
        mEcallIndication.header = p_sph_msg->param_16bit;
        mEcallIndication.data = p_sph_msg->param_32bit;
        AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_ECALL_INDICATION, this);
        break;
#endif

    default:
        ALOGE("%s(), not supported msg_id 0x%x", __FUNCTION__, p_sph_msg->msg_id);
    }

    return 0;
}


int SpeechDriverNormal::parseRawRecordPcmBuffer(void *raw_buf, void *parsed_buf, uint16_t *p_data_size) {
    spcRAWPCMBufInfo header_RawPcmBufInfo;
    spcApRAWPCMBufHdr header_ApRawPcmBuf;

    uint16_t BytesCopied = 0;
    uint16_t BytesToCopy = 0;

    char *PtrTarget = NULL;
    char *PtrSource = NULL;

    int retval = 0;

    // share buffer header
    memcpy(&header_RawPcmBufInfo, raw_buf, sizeof(spcRAWPCMBufInfo));
    PtrTarget = (char *)parsed_buf;

    AL_AUTOLOCK(mRecordTypeLock);
    switch (mRecordType.direction) {
    case RECORD_TYPE_UL:
        header_ApRawPcmBuf.u16SyncWord = TEMP_CCCI_MD_PAYLOAD_SYNC;
        header_ApRawPcmBuf.u16RawPcmDir = RECORD_TYPE_UL;
        header_ApRawPcmBuf.u16Freq = sph_sample_rate_enum_to_value(header_RawPcmBufInfo.u16ULFreq);
        header_ApRawPcmBuf.u16Length = header_RawPcmBufInfo.u16ULLength;
        header_ApRawPcmBuf.u16Channel = 1;
        header_ApRawPcmBuf.u16BitFormat = AUDIO_FORMAT_PCM_16_BIT;

        // uplink raw pcm header
        memcpy(PtrTarget, &header_ApRawPcmBuf, sizeof(spcApRAWPCMBufHdr));
        BytesCopied = sizeof(spcApRAWPCMBufHdr);

        //uplink raw pcm
        PtrTarget = (char *)parsed_buf + BytesCopied;
        PtrSource = (char *)raw_buf + sizeof(spcRAWPCMBufInfo);
        BytesToCopy = header_RawPcmBufInfo.u16ULLength;
        memcpy(PtrTarget, PtrSource, BytesToCopy);
        BytesCopied += BytesToCopy;
        break;
    case RECORD_TYPE_DL:
        header_ApRawPcmBuf.u16SyncWord = TEMP_CCCI_MD_PAYLOAD_SYNC;
        header_ApRawPcmBuf.u16RawPcmDir = RECORD_TYPE_DL;
        header_ApRawPcmBuf.u16Freq = sph_sample_rate_enum_to_value(header_RawPcmBufInfo.u16DLFreq);
        header_ApRawPcmBuf.u16Length = header_RawPcmBufInfo.u16DLLength;
        header_ApRawPcmBuf.u16Channel = 1;
        header_ApRawPcmBuf.u16BitFormat = AUDIO_FORMAT_PCM_16_BIT;

        // downlink raw pcm header
        memcpy(PtrTarget, &header_ApRawPcmBuf, sizeof(spcApRAWPCMBufHdr));
        BytesCopied = sizeof(spcApRAWPCMBufHdr);

        // downlink raw pcm
        PtrTarget = (char *)parsed_buf + BytesCopied;
        PtrSource = (char *)raw_buf + sizeof(spcRAWPCMBufInfo) + header_RawPcmBufInfo.u16ULLength;
        BytesToCopy = header_RawPcmBufInfo.u16DLLength;
        memcpy(PtrTarget, PtrSource, BytesToCopy);
        BytesCopied += BytesToCopy;
        break;
    case RECORD_TYPE_MIX:
        header_ApRawPcmBuf.u16SyncWord = TEMP_CCCI_MD_PAYLOAD_SYNC;
        header_ApRawPcmBuf.u16RawPcmDir = RECORD_TYPE_UL;
        header_ApRawPcmBuf.u16Freq = sph_sample_rate_enum_to_value(header_RawPcmBufInfo.u16ULFreq);
        header_ApRawPcmBuf.u16Length = header_RawPcmBufInfo.u16ULLength;
        header_ApRawPcmBuf.u16Channel = 1;
        header_ApRawPcmBuf.u16BitFormat = AUDIO_FORMAT_PCM_16_BIT;

        //uplink raw pcm header
        memcpy(PtrTarget, &header_ApRawPcmBuf, sizeof(spcApRAWPCMBufHdr));
        BytesCopied = sizeof(spcApRAWPCMBufHdr);

        //uplink raw pcm
        PtrTarget = (char *)parsed_buf + BytesCopied;
        PtrSource = (char *)raw_buf + sizeof(spcRAWPCMBufInfo);
        BytesToCopy = header_RawPcmBufInfo.u16ULLength;
        memcpy(PtrTarget, PtrSource, BytesToCopy);
        BytesCopied += BytesToCopy;

        PtrTarget = (char *)parsed_buf + BytesCopied;

        //downlink raw pcm header
        header_ApRawPcmBuf.u16RawPcmDir = RECORD_TYPE_DL;
        header_ApRawPcmBuf.u16Freq = sph_sample_rate_enum_to_value(header_RawPcmBufInfo.u16DLFreq);
        header_ApRawPcmBuf.u16Length = header_RawPcmBufInfo.u16DLLength;
        memcpy(PtrTarget, &header_ApRawPcmBuf, sizeof(spcApRAWPCMBufHdr));
        BytesCopied += sizeof(spcApRAWPCMBufHdr);

        //downlink raw pcm
        PtrTarget = (char *)parsed_buf + BytesCopied;
        PtrSource = (char *)raw_buf + sizeof(spcRAWPCMBufInfo) + header_RawPcmBufInfo.u16ULLength;
        BytesToCopy = header_RawPcmBufInfo.u16DLLength;
        memcpy(PtrTarget, PtrSource, BytesToCopy);
        BytesCopied += BytesToCopy;
        break;
    default:
        ALOGW("%s(), mRecordType direction %d error!!", __FUNCTION__, mRecordType.direction);
        retval = -EINVAL;
        BytesCopied = 0;
        break;
    }

    if (BytesCopied > *p_data_size) {
        ALOGW("%s(), BytesCopied %u > parsed_buf size %u!!", __FUNCTION__,
              BytesCopied, *p_data_size);
        *p_data_size = 0;
        WARNING("-EOVERFLOW");
        return -EOVERFLOW;
    }


    *p_data_size = BytesCopied;

    return retval;
}

static void dropMdDataInShareMemory(SpeechMessengerNormal *messenger, sph_msg_t *p_sph_msg) {
    uint8_t dummy_md_data[p_sph_msg->length];
    uint16_t data_type = 0;
    uint16_t data_size = 0;

    int retval = 0;

    if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
        data_size = p_sph_msg->length;
        retval = messenger->readMdDataFromShareMemory(
                     dummy_md_data,
                     &data_type,
                     &data_size,
                     p_sph_msg->length,
                     p_sph_msg->rw_index);
        if (retval != 0) {
            PRINT_SPH_MSG(ALOGW, "get share memory md data failed!!", p_sph_msg);
        }
    }
}

int SpeechDriverNormal::processModemDataMessage(sph_msg_t *p_sph_msg) {
    /* error handling */
    if (p_sph_msg == NULL) {
        ALOGW("%s(), p_sph_msg == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    if (mSpeechMessenger == NULL) {
        ALOGW("%s(), mSpeechMessenger == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }


    if (mApResetDuringSpeech == true) {
        PRINT_SPH_MSG(ALOGW, "mApResetDuringSpeech == true!! drop md data", p_sph_msg);
        return -ERESTART;
    }

    static SpeechPcmMixerBGSPlayer *pBGSPlayer = SpeechPcmMixerBGSPlayer::GetInstance();
    static SpeechVMRecorder *pSpeechVMRecorder = SpeechVMRecorder::getInstance();
    static SpeechDataProcessingHandler *pSpeechDataProcessingHandler = SpeechDataProcessingHandler::getInstance();
    static Record2Way *pRecord2Way = Record2Way::GetInstance();
    static Play2Way *pPlay2Way = Play2Way::GetInstance();

#if defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
    static SpeechPcmMixerVoipRx *pVoipRx = SpeechPcmMixerVoipRx::GetInstance();
#endif
    static SpeechPcmMixerTelephonyTx *pTelephonyTx = SpeechPcmMixerTelephonyTx::GetInstance();


    struct timespec ts_start;
    struct timespec ts_stop;

    uint64_t time_diff_shm = 0;
    uint64_t time_diff_vm = 0;

    sph_msg_t sph_msg;

    uint16_t num_data_request = 0;

    uint16_t data_type = 0;
    uint16_t data_size = 0;
    uint16_t payload_length = 0;
    uint32_t write_idx = 0;

    RingBuf ringbuf;

    int retval = 0;


    /* TODO: add class */
    switch (p_sph_msg->msg_id) {
    case MSG_M2A_BGSND_DATA_REQUEST: {
        // fill playback data
        if (getModemSideModemStatus(BGS_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md bgs off now!! break", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(BGS_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap bgs off now!! break", p_sph_msg);
            break;
        } else if (!mBgsBuf) {
            PRINT_SPH_MSG(ALOGW, "mBgsBuf NULL!! break", p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_D, "bgs data request", p_sph_msg);
            num_data_request = p_sph_msg->length;
            if (num_data_request > kMaxApPayloadDataSize) {
                num_data_request = kMaxApPayloadDataSize;
            }
            data_size = (uint16_t)pBGSPlayer->PutDataToSpeaker((char *)mBgsBuf, num_data_request);
            if (getPcmMixerLogEnableByLevel(PCM_MIXER_LOG_LEVEL_MODEM)) {
                ALOGD("%s(), bgs data request, id: 0x%x, data in md: %u, request: %d, bgs fill: %u",
                      __FUNCTION__, p_sph_msg->msg_id,
                      p_sph_msg->param_32bit, p_sph_msg->param_16bit, data_size);
            }
        }
        // share memory
        retval = mSpeechMessenger->writeApDataToShareMemory(mBgsBuf,
                                                            SHARE_BUFF_DATA_TYPE_CCCI_BGS_TYPE,
                                                            data_size,
                                                            &payload_length,
                                                            &write_idx);
        // send data notify to modem side
        if (retval == 0) { // via share memory
            retval = sendMailbox(&sph_msg, MSG_A2M_BGSND_DATA_NOTIFY, payload_length, write_idx);
        } else { // via payload
            retval = sendPayload(&sph_msg, MSG_A2M_BGSND_DATA_NOTIFY,
                                 SHARE_BUFF_DATA_TYPE_CCCI_BGS_TYPE,
                                 mBgsBuf, data_size);
        }

        break;
    }
    case MSG_M2A_VM_REC_DATA_NOTIFY: {
        if (getModemSideModemStatus(VM_RECORD_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md vm rec off now!! drop it", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(VM_RECORD_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap vm rec off now!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else if (!mVmRecBuf) {
            PRINT_SPH_MSG(ALOGW, "mVmRecBuf NULL!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_V, "vm rec data notify", p_sph_msg);
            time_diff_shm = 0;
            time_diff_vm = 0;

            /* get vm data */
            if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
                data_size = MAX_VM_RECORD_SIZE;

                audio_get_timespec_monotonic(&ts_start);
                retval = mSpeechMessenger->readMdDataFromShareMemory(
                             mVmRecBuf,
                             &data_type,
                             &data_size,
                             p_sph_msg->length,
                             p_sph_msg->rw_index);
                audio_get_timespec_monotonic(&ts_stop);
                time_diff_shm = get_time_diff_ms(&ts_start, &ts_stop);

                if (retval != 0) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                } else {
                    sendMailbox(&sph_msg, MSG_A2M_VM_REC_DATA_READ_ACK,
                                p_sph_msg->length, p_sph_msg->rw_index);
                }
            } else if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
                if (p_sph_msg->payload_data_size > kMaxMdPayloadDataSize) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                    retval = -ENOMEM;
                } else {
                    if (p_sph_msg->payload_data_size < MAX_VM_RECORD_SIZE) {
                        memcpy(mVmRecBuf,
                               p_sph_msg->payload_data_addr,
                               p_sph_msg->payload_data_size);
                    } else {
                        ALOGW("%s() no enough space, buffer:%d, payload_data:%d",
                              __FUNCTION__, MAX_VM_RECORD_SIZE, p_sph_msg->payload_data_size);
                        retval = -ENOMEM;
                    }
                    data_type = p_sph_msg->payload_data_type;
                    data_size = p_sph_msg->payload_data_size;
                    if (p_sph_msg->payload_data_idx == p_sph_msg->payload_data_total_idx) {
                        sendMailbox(&sph_msg, MSG_A2M_VM_REC_DATA_READ_ACK, 0, 0);
                    }
                }
            } else {
                PRINT_SPH_MSG(ALOGW, "bad buffer_type!!", p_sph_msg);
                retval = -EINVAL;
            }

            /* copy vm data */
            if (retval == 0) {
                if (data_type != SHARE_BUFF_DATA_TYPE_CCCI_VM_TYPE) {
                    PRINT_SPH_MSG(ALOGW, "wrong data_type. drop it", p_sph_msg);
                    retval = -EINVAL;
                } else if (data_size > 0) {
                    ringbuf.pBufBase = (char *)mVmRecBuf;
                    ringbuf.bufLen   = data_size + 1; // +1: avoid pRead == pWrite
                    ringbuf.pRead    = ringbuf.pBufBase;
                    ringbuf.pWrite   = ringbuf.pBufBase + data_size;
                    audio_get_timespec_monotonic(&ts_start);
                    pSpeechVMRecorder->getVmDataFromModem(ringbuf);
                    audio_get_timespec_monotonic(&ts_stop);
                    time_diff_vm = get_time_diff_ms(&ts_start, &ts_stop);
                }
            }
            if ((time_diff_shm + time_diff_vm) >= MAX_MSG_PROCESS_TIME_MS) {
                ALOGW("%s(), time_diff_shm %ju, time_diff_vm %ju", __FUNCTION__, time_diff_shm, time_diff_vm);
            }
        }
        break;
    }
    case MSG_M2A_RAW_PCM_REC_DATA_NOTIFY: {
        if (getModemSideModemStatus(RAW_RECORD_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md raw rec off now!! drop it", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(RAW_RECORD_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap raw rec off now!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else if (!mRawRecBuf || !mParsedRecBuf) {
            PRINT_SPH_MSG(ALOGW, "mRawRecBuf or mParsedRecBuf NULL!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_V, "raw rec data notify", p_sph_msg);

            /* get rec data */
            if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
                data_size = MAX_RAW_RECORD_SIZE;
                retval = mSpeechMessenger->readMdDataFromShareMemory(
                             mRawRecBuf,
                             &data_type,
                             &data_size,
                             p_sph_msg->length,
                             p_sph_msg->rw_index);
                if (retval != 0) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                } else {
                    sendMailbox(&sph_msg, MSG_A2M_RAW_PCM_REC_DATA_READ_ACK,
                                p_sph_msg->length, p_sph_msg->rw_index);
                }
            } else if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
                if (p_sph_msg->payload_data_size > kMaxMdPayloadDataSize) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                    retval = -ENOMEM;
                } else {
                    memcpy(mRawRecBuf,
                           p_sph_msg->payload_data_addr,
                           p_sph_msg->payload_data_size);
                    data_type = p_sph_msg->payload_data_type;
                    data_size = p_sph_msg->payload_data_size;
                    if (p_sph_msg->payload_data_idx == p_sph_msg->payload_data_total_idx) {
                        sendMailbox(&sph_msg, MSG_A2M_RAW_PCM_REC_DATA_READ_ACK, 0, 0);
                    }
                }
            } else {
                PRINT_SPH_MSG(ALOGW, "bad buffer_type!!", p_sph_msg);
                retval = -EINVAL;
            }

            /* copy raw rec data */
            if (retval == 0) {
#if 0 // check
                if (data_type != SHARE_BUFF_DATA_TYPE_CCCI_RAW_PCM_TYPE) {
                    PRINT_SPH_MSG(ALOGW, "wrong data_type. drop it", p_sph_msg);
                    retval = -EINVAL;
                } else
#endif
                    if (data_size > 0) {
                        data_size = MAX_PARSED_RECORD_SIZE;
                        retval = parseRawRecordPcmBuffer(mRawRecBuf, mParsedRecBuf, &data_size);
                        if (retval == 0) {
                            ringbuf.pBufBase = (char *)mParsedRecBuf;
                            ringbuf.bufLen   = data_size + 1; // +1: avoid pRead == pWrite
                            ringbuf.pRead    = ringbuf.pBufBase;
                            ringbuf.pWrite   = ringbuf.pBufBase + data_size;

                            pSpeechDataProcessingHandler->provideModemRecordDataToProvider(ringbuf);
                        }
                    }
            }
        }
        break;
    }
    case MSG_M2A_PNW_UL_DATA_NOTIFY: {
        if (getModemSideModemStatus(P2W_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md p2w off now!! drop it", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(P2W_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap p2w off now!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else if (!mP2WUlBuf) {
            PRINT_SPH_MSG(ALOGW, "mP2WUlBuf NULL!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_V, "p2w ul data notify", p_sph_msg);

            /* get p2w ul data */
            if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
                data_size = MAX_PNW_UL_SIZE;
                retval = mSpeechMessenger->readMdDataFromShareMemory(
                             mP2WUlBuf,
                             &data_type,
                             &data_size,
                             p_sph_msg->length,
                             p_sph_msg->rw_index);
                if (retval != 0) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                } else {
                    sendMailbox(&sph_msg, MSG_A2M_PNW_UL_DATA_READ_ACK,
                                p_sph_msg->length, p_sph_msg->rw_index);
                }
            } else if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
                if (p_sph_msg->payload_data_size > kMaxMdPayloadDataSize) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                    retval = -ENOMEM;
                } else {
                    memcpy(mP2WUlBuf,
                           p_sph_msg->payload_data_addr,
                           p_sph_msg->payload_data_size);
                    data_type = p_sph_msg->payload_data_type;
                    data_size = p_sph_msg->payload_data_size;
                    if (p_sph_msg->payload_data_idx == p_sph_msg->payload_data_total_idx) {
                        sendMailbox(&sph_msg, MSG_A2M_PNW_UL_DATA_READ_ACK, 0, 0);
                    }
                }
            } else {
                PRINT_SPH_MSG(ALOGW, "bad buffer_type!!", p_sph_msg);
                retval = -EINVAL;
            }

            /* copy p2w ul data */
            if (retval == 0) {
#if 0 // check
                if (data_type != SHARE_BUFF_DATA_TYPE_PCM_GetFromMic) {
                    PRINT_SPH_MSG(ALOGW, "wrong data_type. drop it", p_sph_msg);
                    retval = -EINVAL;
                } else
#endif
                    if (data_size > 0) {
                        ringbuf.pBufBase = (char *)mP2WUlBuf;
                        ringbuf.bufLen   = data_size + 1; // +1: avoid pRead == pWrite
                        ringbuf.pRead    = ringbuf.pBufBase;
                        ringbuf.pWrite   = ringbuf.pBufBase + data_size;
                        pRecord2Way->GetDataFromMicrophone(ringbuf);

#if 0 // PCM2WAY: UL -> DL Loopback
                        // Used for debug and Speech DVT
                        uint16_t size_bytes = 320;
                        char buffer[320];
                        pRecord2Way->Read(buffer, size_bytes);
                        pPlay2Way->Write(buffer, size_bytes);
#endif
                    }
            }
        }

        break;
    }
    case MSG_M2A_PNW_DL_DATA_REQUEST: {
        // fill p2w dl data
        if (getModemSideModemStatus(P2W_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md p2w off now!! break", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(P2W_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap p2w off now!! break", p_sph_msg);
            break;
        } else if (!mP2WDlBuf) {
            PRINT_SPH_MSG(ALOGW, "mP2WDlBuf NULL!! break", p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_D, "p2w dl data request", p_sph_msg);
            num_data_request = p_sph_msg->length;
            if (num_data_request > kMaxApPayloadDataSize) {
                num_data_request = kMaxApPayloadDataSize;
            }
            data_size = (uint16_t)pPlay2Way->PutDataToSpeaker((char *)mP2WDlBuf, num_data_request);
            if (data_size == 0) {
                PRINT_SPH_MSG(ALOGW, "data_size == 0", p_sph_msg);
#if 0
                break;
#endif
            }
        }
        // share memory
        retval = mSpeechMessenger->writeApDataToShareMemory(mP2WDlBuf,
                                                            SHARE_BUFF_DATA_TYPE_PCM_FillSpk,
                                                            data_size,
                                                            &payload_length,
                                                            &write_idx);
        // send data notify to modem side
        if (retval == 0) { // via share memory
            retval = sendMailbox(&sph_msg, MSG_A2M_PNW_DL_DATA_NOTIFY, payload_length, write_idx);
        } else { // via payload
            retval = sendPayload(&sph_msg, MSG_A2M_PNW_DL_DATA_NOTIFY,
                                 SHARE_BUFF_DATA_TYPE_PCM_FillSpk,
                                 mP2WDlBuf, data_size);
        }
        break;
    }
    case MSG_M2A_CTM_DEBUG_DATA_NOTIFY: {
        if (getModemSideModemStatus(TTY_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md tty off now!! drop it", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(TTY_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap tty off now!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else if (!mTtyDebugBuf) {
            PRINT_SPH_MSG(ALOGW, "mTtyDebugBuf NULL!! drop it", p_sph_msg);
            dropMdDataInShareMemory(mSpeechMessenger, p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_V, "tty debug data notify", p_sph_msg);

            /* get tty debug data */
            if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
                data_size = MAX_TTY_DEBUG_SIZE;
                retval = mSpeechMessenger->readMdDataFromShareMemory(
                             mTtyDebugBuf,
                             &data_type,
                             &data_size,
                             p_sph_msg->length,
                             p_sph_msg->rw_index);

                if (retval != 0) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                } else {
                    sendMailbox(&sph_msg, MSG_A2M_CTM_DEBUG_DATA_READ_ACK,
                                p_sph_msg->length, p_sph_msg->rw_index);
                }
            } else if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
                if (p_sph_msg->payload_data_size > kMaxMdPayloadDataSize) {
                    PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
                    retval = -ENOMEM;
                } else {
                    memcpy(mTtyDebugBuf,
                           p_sph_msg->payload_data_addr,
                           p_sph_msg->payload_data_size);
                    data_type = p_sph_msg->payload_data_type;
                    data_size = p_sph_msg->payload_data_size;
                    if (p_sph_msg->payload_data_idx == p_sph_msg->payload_data_total_idx) {
                        sendMailbox(&sph_msg, MSG_A2M_CTM_DEBUG_DATA_READ_ACK, 0, 0);
                    }
                }
            } else {
                PRINT_SPH_MSG(ALOGW, "bad buffer_type!!", p_sph_msg);
                retval = -EINVAL;
            }

            /* copy tty debug data */
            if (retval == 0) {
                ringbuf.pBufBase = (char *)mTtyDebugBuf;
                ringbuf.bufLen   = data_size + 1; // +1: avoid pRead == pWrite
                ringbuf.pRead    = ringbuf.pBufBase;
                ringbuf.pWrite   = ringbuf.pBufBase + data_size;

                switch (data_type) {
                case SHARE_BUFF_DATA_TYPE_CCCI_CTM_UL_IN:
                    pSpeechVMRecorder->getCtmDebugDataFromModem(ringbuf, pSpeechVMRecorder->pCtmDumpFileUlIn);
                    break;
                case SHARE_BUFF_DATA_TYPE_CCCI_CTM_DL_IN:
                    pSpeechVMRecorder->getCtmDebugDataFromModem(ringbuf, pSpeechVMRecorder->pCtmDumpFileDlIn);
                    break;
                case SHARE_BUFF_DATA_TYPE_CCCI_CTM_UL_OUT:
                    pSpeechVMRecorder->getCtmDebugDataFromModem(ringbuf, pSpeechVMRecorder->pCtmDumpFileUlOut);
                    break;
                case SHARE_BUFF_DATA_TYPE_CCCI_CTM_DL_OUT:
                    pSpeechVMRecorder->getCtmDebugDataFromModem(ringbuf, pSpeechVMRecorder->pCtmDumpFileDlOut);
                    break;
                default:
                    PRINT_SPH_MSG(ALOGW, "wrong data_type. drop it", p_sph_msg);
                    retval = -EINVAL;
                    ASSERT(0);
                }
            }
        }
        break;
    }
#if defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
    case MSG_M2A_VOIP_RX_DL_DATA_REQUEST: {
        // fill p2w dl data
        if (getModemSideModemStatus(PCM_MIXER_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md pcm mixer off now!! break", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(PCM_MIXER_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap pcm mixer off now!! break", p_sph_msg);
            break;
        } else if (!mVoipRxDlBuf) {
            PRINT_SPH_MSG(ALOGW, "mVoipRxDlBuf NULL!! break", p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_D, "voip rx dl data request", p_sph_msg);
            num_data_request = p_sph_msg->length;
            if (num_data_request > kMaxApPayloadDataSize) {
                num_data_request = kMaxApPayloadDataSize;
            }
            data_size = (uint16_t)pVoipRx->PutDataToSpeaker((char *)mVoipRxDlBuf, num_data_request);
            if (data_size == 0) {
                PRINT_SPH_MSG(ALOGW, "data_size == 0", p_sph_msg);
            }
        }
        // share memory
        retval = mSpeechMessenger->writeApDataToShareMemory(mVoipRxDlBuf,
                                                            SHARE_BUFF_DATA_TYPE_CCCI_VOIP_RX,
                                                            data_size,
                                                            &payload_length,
                                                            &write_idx);
        // send data notify to modem side
        if (retval == 0) { // via share memory
            retval = sendMailbox(&sph_msg, MSG_A2M_VOIP_RX_DL_DATA_NOTIFY, payload_length, write_idx);
        } else { // via payload
            retval = sendPayload(&sph_msg, MSG_A2M_VOIP_RX_DL_DATA_NOTIFY,
                                 SHARE_BUFF_DATA_TYPE_PCM_FillSpk,
                                 mVoipRxDlBuf, data_size);
        }
        break;
    }
#endif

    case MSG_M2A_TELEPHONY_TX_UL_DATA_REQUEST: {
        // fill p2w dl data
        if (getModemSideModemStatus(TELEPHONY_TX_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "md telephony tx off now!! break", p_sph_msg);
            break;
        } else if (GetApSideModemStatus(TELEPHONY_TX_STATUS_MASK) == false) {
            PRINT_SPH_MSG(ALOGW, "ap telephony tx off now!! break", p_sph_msg);
            break;
        } else if (!mTelephonyTxBuf) {
            PRINT_SPH_MSG(ALOGW, "mTelephonyTxBuf NULL!! break", p_sph_msg);
            break;
        } else {
            PRINT_SPH_MSG(SPH_LOG_D, "telephonytx data request", p_sph_msg);
            num_data_request = p_sph_msg->length;
            if (num_data_request > kMaxApPayloadDataSize) {
                num_data_request = kMaxApPayloadDataSize;
            }
            data_size = (uint16_t)pTelephonyTx->PutDataToSpeaker((char *)mTelephonyTxBuf, num_data_request);
            if (data_size == 0) {
                PRINT_SPH_MSG(ALOGW, "data_size == 0", p_sph_msg);
            }
        }
        // share memory
        retval = mSpeechMessenger->writeApDataToShareMemory(mTelephonyTxBuf,
                                                            SHARE_BUFF_DATA_TYPE_CCCI_TELEPHONY_TX,
                                                            data_size,
                                                            &payload_length,
                                                            &write_idx);
        // send data notify to modem side
        if (retval == 0) { // via share memory
            retval = sendMailbox(&sph_msg, MSG_A2M_TELEPHONY_TX_UL_DATA_NOTIFY, payload_length, write_idx);
        } else { // via payload
            retval = sendPayload(&sph_msg, MSG_A2M_TELEPHONY_TX_UL_DATA_NOTIFY,
                                 SHARE_BUFF_DATA_TYPE_CCCI_TELEPHONY_TX,
                                 mTelephonyTxBuf, data_size);
        }
        break;
    }
#if defined(MTK_SPEECH_ECALL_SUPPORT)
    case MSG_M2A_ECALL_RX_CTRL_PAR_NOTIFY: {
        ALOGV("%s(), msg_id:0x%x ", __FUNCTION__, p_sph_msg->msg_id);

        /* get ecall rx data */
        if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { // via share memory
            data_size = MAX_SPEECH_ECALL_BUF_LEN;
            retval = mSpeechMessenger->readMdDataFromShareMemory(
                         mEcallRXCtrlData.data,
                         &data_type,
                         &data_size,
                         p_sph_msg->length,
                         p_sph_msg->rw_index);
            if (retval != 0) {
                PRINT_SPH_MSG(ALOGW, "get share memory md data failed!! drop it", p_sph_msg);
            } else {
                mEcallRXCtrlData.size = data_size;
                AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_ECALL_RX, this);
            }
        } else if (p_sph_msg->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { // via payload
            ASSERT(p_sph_msg->payload_data_idx == p_sph_msg->payload_data_total_idx);
            if (p_sph_msg->payload_data_size <= MAX_SPEECH_ECALL_BUF_LEN) {
                memcpy(mEcallRXCtrlData.data, p_sph_msg->payload_data_addr, p_sph_msg->payload_data_size);
                AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_ECALL_RX, this);
            } else {
                ALOGE("%s(), buffer size(%d) > MAX SIZE(%d)", __FUNCTION__,
                      p_sph_msg->payload_data_size, MAX_SPEECH_ECALL_BUF_LEN);
            }
        }
        break;
    }
#endif

    default:
        ALOGE("%s(), not supported msg_id 0x%x", __FUNCTION__, p_sph_msg->msg_id);
    }


    return retval;
}



/*==============================================================================
 *                     Speech Control
 *============================================================================*/

status_t SpeechDriverNormal::SetSpeechMode(const audio_devices_t input_device, const audio_devices_t output_device) {
    sph_msg_t sph_msg;
    sph_info_t sph_info;

    int retval = 0;

    SLOG_ENG("%s(), input_device: 0x%x, output_device: 0x%x",
             __FUNCTION__, input_device, output_device);

    mInputDevice = input_device;
    mOutputDevice = output_device;

    // set a unreasonable gain value s.t. the reasonable gain can be set to modem next time
    mDownlinkGain   = kUnreasonableGainValue;
    mDownlinkenh1Gain = kUnreasonableGainValue;
    mUplinkGain     = kUnreasonableGainValue;
    mSideToneGain   = kUnreasonableGainValue;

    if (isSpeechApplicationOn()) {
        AL_AUTOLOCK_MS(mSpeechParamLock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS); // atomic: write shm & send msg
        parseSpeechParam(SPEECH_SCENARIO_DEVICE_CHANGE);
        configSpeechInfo(&sph_info);
        retval = sendPayload(&sph_msg, MSG_A2M_SPH_DEV_CHANGE,
                             SHARE_BUFF_DATA_TYPE_CCCI_SPH_INFO,
                             &sph_info, sizeof(sph_info_t));
    }

    return 0;
}


status_t SpeechDriverNormal::setMDVolumeIndex(int stream, int device, int index) {
    int param_arg[4];
    int indexShift = 1;
    //Android M Voice volume index: available index 1~7, 0 for mute
    //Android L Voice volume index: available index 0~6

#if defined(MTK_SPEECH_PARAM_VALID_VOLUME_0)
    indexShift = 0;
#endif
    if (index <= 0) {
        return 0;
    } else {
        mVolumeIndex = index - indexShift;
    }

    if (isSpeechApplicationOn() == false) {
        ALOGD("%s(), stream: %d, device: 0x%x, index: %d, sph off, return",
              __FUNCTION__, stream, device, index);
    } else {
        updateSpeechParam(SPEECH_SCENARIO_VOLUME_CHANGE);
    }

    return 0;
}


int SpeechDriverNormal::SpeechOnByApplication(const uint8_t application) {
    sph_msg_t sph_msg;
    sph_info_t sph_info;

    SLOG_ENG("SpeechOn(), application: %d", application);

    // reset modem status
    mModemResetDuringSpeech = false;
    if (mModemDead == true) {
        ALOGW("%s(), mModemDead not clear!! reset it!!", __FUNCTION__);
        mModemDead = false;
    }


    // update phone call status to parser
    SpeechParserBase::getInstance()->updatePhoneCallStatus(true);

    // speech param
    AL_AUTOLOCK_MS(mSpeechParamLock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS); // atomic: write shm & send msg

    if (application == SPH_APPLICATION_LOOPBACK) {
        updateFeatureMask(SPEECH_FEATURE_LOOPBACK, true);
    }
    parseSpeechParam(SPEECH_SCENARIO_SPEECH_ON);
    mApplication = application;
    configSpeechInfo(&sph_info);

    int retval = sendPayload(&sph_msg, MSG_A2M_SPH_ON,
                             SHARE_BUFF_DATA_TYPE_CCCI_SPH_INFO,
                             &sph_info, sizeof(sph_info_t));

    createThreadsDuringSpeech();

    return retval;
}

int SpeechDriverNormal::SpeechOffByApplication(const uint8_t application) {
    sph_msg_t sph_msg;

    SLOG_ENG("SpeechOff(), application: %d, mApplication: %d", application, mApplication);
    if (application != mApplication) {
        WARNING("speech off not in pair!!");
    }
    if (application == SPH_APPLICATION_LOOPBACK) {
        updateFeatureMask(SPEECH_FEATURE_LOOPBACK, false);
    }

    int retval = sendMailbox(&sph_msg, MSG_A2M_SPH_OFF, 0, 0);

    CleanGainValueAndMuteStatus();

    mApplication = SPH_APPLICATION_INVALID;

    mModemResetDuringSpeech = false;

    // update phone call status to parser
    SpeechParserBase::getInstance()->updatePhoneCallStatus(false);

    return retval;
}


status_t SpeechDriverNormal::SpeechOn() {
    CheckApSideModemStatusAllOffOrDie();
    SetApSideModemStatus(SPEECH_STATUS_MASK);

    return SpeechOnByApplication(SPH_APPLICATION_NORMAL);
}


status_t SpeechDriverNormal::SpeechOff() {
    /* should send sph off first and then clean state */
    int retval = SpeechOffByApplication(SPH_APPLICATION_NORMAL);

    ResetApSideModemStatus(SPEECH_STATUS_MASK);
    CheckApSideModemStatusAllOffOrDie();

    return retval;
}


status_t SpeechDriverNormal::VideoTelephonyOn() {
    CheckApSideModemStatusAllOffOrDie();
    SetApSideModemStatus(VT_STATUS_MASK);

    return SpeechOnByApplication(SPH_APPLICATION_VT_CALL);
}


status_t SpeechDriverNormal::VideoTelephonyOff() {
    /* should send sph off first and then clean state */
    int retval = SpeechOffByApplication(SPH_APPLICATION_VT_CALL);

    ResetApSideModemStatus(VT_STATUS_MASK);
    CheckApSideModemStatusAllOffOrDie();

    return retval;
}


status_t SpeechDriverNormal::SpeechRouterOn() {
    CheckApSideModemStatusAllOffOrDie();
    SetApSideModemStatus(SPEECH_ROUTER_STATUS_MASK);

    return SpeechOnByApplication(SPH_APPLICATION_ROUTER);
}


status_t SpeechDriverNormal::SpeechRouterOff() {
    /* should send sph off first and then clean state */
    int retval = SpeechOffByApplication(SPH_APPLICATION_ROUTER);

    ResetApSideModemStatus(SPEECH_ROUTER_STATUS_MASK);
    CheckApSideModemStatusAllOffOrDie();

    return retval;
}



/*==============================================================================
 *                     Recording Control
 *============================================================================*/

status_t SpeechDriverNormal::recordOn(SpcRecordTypeStruct typeRecord) {
    AL_AUTOLOCK(mRecordTypeLock);
    SLOG_ENG("%s(), Record direction: %d => %d, dlPosition: %d", __FUNCTION__,
             mRecordType.direction, typeRecord.direction, typeRecord.dlPosition);

    if (typeRecord.direction >= RECORD_TYPE_MAX ||
        typeRecord.dlPosition >= RECORD_POS_DL_MAX) {
        ALOGE("%s(), Wrong record type!! direction:%d, dlPosition:%d",
              __FUNCTION__, typeRecord.direction, typeRecord.dlPosition);
    }

    SetApSideModemStatus(RAW_RECORD_STATUS_MASK);

    mRecordType = typeRecord;
    sph_msg_t sph_msg;
    uint16_t param_16bit = typeRecord.dlPosition;
    return sendMailbox(&sph_msg, MSG_A2M_RECORD_RAW_PCM_ON, param_16bit, 0);
}


status_t SpeechDriverNormal::recordOff(SpcRecordTypeStruct typeRecord) {
    AL_AUTOLOCK(mRecordTypeLock);
    SLOG_ENG("%s(), Record direction: %d => %d, dlPosition: %d", __FUNCTION__,
             mRecordType.direction, typeRecord.direction, typeRecord.dlPosition);

    if (typeRecord.direction >= RECORD_TYPE_MAX ||
        typeRecord.dlPosition >= RECORD_POS_DL_MAX) {
        ALOGE("%s(), Wrong record type!! direction:%d, dlPosition:%d",
              __FUNCTION__, typeRecord.direction, typeRecord.dlPosition);
    }

    sph_msg_t sph_msg;
    int retval = 0;

    retval = sendMailbox(&sph_msg, MSG_A2M_RECORD_RAW_PCM_OFF, 0, 0);

    ResetApSideModemStatus(RAW_RECORD_STATUS_MASK);
    mRecordType = typeRecord;
    return retval;
}


status_t SpeechDriverNormal::setPcmRecordType(SpcRecordTypeStruct typeRecord) {
    AL_AUTOLOCK(mRecordTypeLock);
    ALOGD("%s(), Record direction: %d => %d", __FUNCTION__,
          mRecordType.direction, typeRecord.direction);
    mRecordType = typeRecord;
    return 0;
}


status_t SpeechDriverNormal::VoiceMemoRecordOn() {
    // Dynamic allocate VM buffer
    if (mVmRecBuf == NULL) {
        AUDIO_ALLOC_BUFFER(mVmRecBuf, MAX_VM_RECORD_SIZE);
    }
    SetApSideModemStatus(VM_RECORD_STATUS_MASK);
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_VM_REC_ON, 0, 0);
}


status_t SpeechDriverNormal::VoiceMemoRecordOff() {
    sph_msg_t sph_msg;
    int retval = 0;
    retval = sendMailbox(&sph_msg, MSG_A2M_VM_REC_OFF, 0, 0);

    ResetApSideModemStatus(VM_RECORD_STATUS_MASK);
    // Dynamic free VM buffer
    AUDIO_FREE_POINTER(mVmRecBuf);
    return retval;
}


uint16_t SpeechDriverNormal::GetRecordSampleRate() const {
    uint16_t num_sample_rate = 0;

    switch (mRecordSampleRateType) {
    case RECORD_SAMPLE_RATE_08K:
        num_sample_rate = 8000;
        break;
    case RECORD_SAMPLE_RATE_16K:
        num_sample_rate = 16000;
        break;
    case RECORD_SAMPLE_RATE_32K:
        num_sample_rate = 32000;
        break;
    case RECORD_SAMPLE_RATE_48K:
        num_sample_rate = 48000;
        break;
    default:
        num_sample_rate = 8000;
        break;
    }

    return num_sample_rate;
}


uint16_t SpeechDriverNormal::GetRecordChannelNumber() const {
    uint16_t num_channel = 0;

    switch (mRecordChannelType) {
    case RECORD_CHANNEL_MONO:
        num_channel = 1;
        break;
    case RECORD_CHANNEL_STEREO:
        num_channel = 2;
        break;
    default:
        num_channel = 1;
        break;
    }

    return num_channel;
}

/*==============================================================================
 *                     Background Sound
 *============================================================================*/

status_t SpeechDriverNormal::BGSoundOn() {
    SetApSideModemStatus(BGS_STATUS_MASK);
    bool useModemNewBgsPatch = true;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_BGSND_ON, useModemNewBgsPatch, 0);
}


status_t SpeechDriverNormal::BGSoundConfig(uint8_t ul_gain, uint8_t dl_gain) {
    sph_msg_t sph_msg;
    uint16_t param_16bit = (ul_gain << 8) | dl_gain;
    return sendMailbox(&sph_msg, MSG_A2M_BGSND_CONFIG, param_16bit, 0);
}


status_t SpeechDriverNormal::BGSoundOff() {
    sph_msg_t sph_msg;
    int retval = 0;
    retval = sendMailbox(&sph_msg, MSG_A2M_BGSND_OFF, 0, 0);

    ResetApSideModemStatus(BGS_STATUS_MASK);
    return retval;
}


/*==============================================================================
 *                     VOIP Rx
 *============================================================================*/
status_t SpeechDriverNormal::VoipRxOn() {
#if !defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
    ALOGW("%s() not support!!!", __FUNCTION__);
    return 0;
#else
    sph_msg_t sph_msg;

    // Dynamic allocate Voip Rx buffer
    if (mP2WUlBuf == NULL) {
        AUDIO_ALLOC_BUFFER(mVoipRxUlBuf, MAX_VOIP_RX_UL_SIZE);
    }
    if (mP2WDlBuf == NULL) {
        AUDIO_ALLOC_BUFFER(mVoipRxDlBuf, kMaxApPayloadDataSize);
    }
    SetApSideModemStatus(PCM_MIXER_STATUS_MASK);
    uint16_t param16bit = (mVoipRxTypeUl << 2) | mVoipRxTypeDl;
    ALOGD("%s(), mVoipRxTypeDl: %u, mVoipRxTypeUl: %u, param16bit: %u",
          __FUNCTION__, mVoipRxTypeDl, mVoipRxTypeUl, param16bit);
    return sendMailbox(&sph_msg, MSG_A2M_VOIP_RX_ON, param16bit, 0);
#endif
}

status_t SpeechDriverNormal::VoipRxOff() {
#if !defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
    ALOGW("%s() not support!!!", __FUNCTION__);
    return 0;
#else
    sph_msg_t sph_msg;
    int retval = 0;
    ALOGD("%s()", __FUNCTION__);

    retval = sendMailbox(&sph_msg, MSG_A2M_VOIP_RX_OFF, 0, 0);
    ResetApSideModemStatus(PCM_MIXER_STATUS_MASK);
    // Dynamic free P2W buffer
    AUDIO_FREE_POINTER(mVoipRxUlBuf);
    AUDIO_FREE_POINTER(mVoipRxDlBuf);
    return retval;
#endif
}

status_t SpeechDriverNormal::VoipRxConfig(const uint8_t direction, const uint8_t mixType) {
#if !defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
    ALOGW("%s() not support!!!direction: %u, mixType: %u",
          __FUNCTION__, direction, mixType);
    return 0;
#else
    sph_msg_t sph_msg;
    if (direction == PCM_DIRECTION_UPLINK) {
        mVoipRxTypeUl = mixType;
    } else {
        mVoipRxTypeDl = mixType;
    }
    uint16_t param16bit = (mVoipRxTypeUl << 2) | mVoipRxTypeDl;
    ALOGD("%s(), direction: %u, mixType: %u, param16bit: %u",
          __FUNCTION__, direction, mixType, param16bit);
    return sendMailbox(&sph_msg, MSG_A2M_VOIP_RX_CONFIG, param16bit, 0);
#endif
}


/*==============================================================================
 *                     Telephony Tx
 *============================================================================*/

status_t SpeechDriverNormal::TelephonyTxOn(uint16_t pcmTelephonyTxType) {
    SetApSideModemStatus(TELEPHONY_TX_STATUS_MASK);
    sph_msg_t sph_msg;
    // 16bit: telephon tx type, 0:replace UL, 1:mix UL
    ALOGD("%s(), pcmTelephonyTxType: %d", __FUNCTION__, pcmTelephonyTxType);
    return sendMailbox(&sph_msg, MSG_A2M_TELEPHONY_TX_ON, pcmTelephonyTxType, 0);
}


status_t SpeechDriverNormal::TelephonyTxConfig(uint16_t pcmTelephonyTxType) {
    sph_msg_t sph_msg;
    // 16bit: telephon tx type, 0:replace UL, 1:mix UL
    ALOGD("%s(), pcmTelephonyTxType: %d", __FUNCTION__, pcmTelephonyTxType);
    return sendMailbox(&sph_msg, MSG_A2M_TELEPHONY_TX_CONFIG, pcmTelephonyTxType, 0);
}


status_t SpeechDriverNormal::TelephonyTxOff() {
    sph_msg_t sph_msg;
    int retval = 0;
    ALOGD("%s()", __FUNCTION__);
    retval = sendMailbox(&sph_msg, MSG_A2M_TELEPHONY_TX_OFF, 0, 0);

    ResetApSideModemStatus(TELEPHONY_TX_STATUS_MASK);
    return retval;
}


/*==============================================================================
 *                     PCM 2 Way
 *============================================================================*/

status_t SpeechDriverNormal::PCM2WayOn(const bool wideband_on) {
    // Dynamic allocate P2W buffer
    if (mP2WUlBuf == NULL) {
        AUDIO_ALLOC_BUFFER(mP2WUlBuf, MAX_PNW_UL_SIZE);
    }
    if (mP2WDlBuf == NULL) {
        if (kMaxApPayloadDataSize > 0) {
            AUDIO_ALLOC_BUFFER(mP2WDlBuf, kMaxApPayloadDataSize);
        } else {
            ASSERT(kMaxApPayloadDataSize > 0);
        }
    }
    SetApSideModemStatus(P2W_STATUS_MASK);

    mPCM2WayState = (SPC_PNW_MSG_BUFFER_SPK | SPC_PNW_MSG_BUFFER_MIC | (wideband_on << 4));
    ALOGD("%s(), wideband_on: %d, mPCM2WayState: 0x%x", __FUNCTION__, wideband_on, mPCM2WayState);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_PNW_ON, mPCM2WayState, 0);
}


status_t SpeechDriverNormal::PCM2WayOff() {
    ALOGD("%s(), mPCM2WayState: 0x%x => 0", __FUNCTION__, mPCM2WayState);
    mPCM2WayState = 0;

    sph_msg_t sph_msg;
    int retval = 0;
    retval = sendMailbox(&sph_msg, MSG_A2M_PNW_OFF, 0, 0);

    ResetApSideModemStatus(P2W_STATUS_MASK);
    // Dynamic free P2W buffer
    AUDIO_FREE_POINTER(mP2WUlBuf);
    AUDIO_FREE_POINTER(mP2WDlBuf);
    return retval;
}


/*==============================================================================
 *                     TTY-CTM Control
 *============================================================================*/

status_t SpeechDriverNormal::TtyCtmOn() {
    SpeechVMRecorder *pSpeechVMRecorder = SpeechVMRecorder::getInstance();
    const bool uplink_mute_on_copy = mUplinkMuteOn;

    ALOGD("%s(), mTtyMode: %d", __FUNCTION__, mTtyMode);

    // Dynamic allocate tty debug buffer
    if (mTtyDebugBuf == NULL) {
        AUDIO_ALLOC_BUFFER(mTtyDebugBuf, MAX_TTY_DEBUG_SIZE);
    }
    SetApSideModemStatus(TTY_STATUS_MASK);

    SetUplinkMute(true);
    TtyCtmDebugOn(pSpeechVMRecorder->getVmConfig() == SPEECH_VM_CTM4WAY);

    sph_msg_t sph_msg;
    int retval = sendMailbox(&sph_msg, MSG_A2M_CTM_ON, mTtyMode, 0);

    SetUplinkMute(uplink_mute_on_copy);

    return retval;
}

status_t SpeechDriverNormal::TtyCtmOff() {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    int retval = 0;
    mTtyMode = AUD_TTY_OFF;

    if (mTtyDebugEnable == true) {
        TtyCtmDebugOn(false);
    }
    retval = sendMailbox(&sph_msg, MSG_A2M_CTM_OFF, 0, 0);

    ResetApSideModemStatus(TTY_STATUS_MASK);

    // Dynamic free tty debug buffer
    AUDIO_FREE_POINTER(mTtyDebugBuf);
    return retval;
}


status_t SpeechDriverNormal::TtyCtmDebugOn(bool tty_debug_flag) {
    SpeechVMRecorder *pSpeechVMRecorder = SpeechVMRecorder::getInstance();

    ALOGD("%s(), tty_debug_flag: %d", __FUNCTION__, tty_debug_flag);

    if (tty_debug_flag == true) {
        mTtyDebugEnable = true;
        pSpeechVMRecorder->startCtmDebug();
    } else {
        pSpeechVMRecorder->stopCtmDebug();
        mTtyDebugEnable = false;
    }

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_CTM_DUMP_DEBUG_FILE, tty_debug_flag, 0);
}

/*==============================================================================
 *                     RTT
 *============================================================================*/

int SpeechDriverNormal::RttConfig(int rttMode) {
    ALOGD("%s(), rttMode = %d, old mRttMode = %d", __FUNCTION__, rttMode, mRttMode);

    if (rttMode == mRttMode) { return NO_ERROR; }
    mRttMode = rttMode;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_RTT_CONFIG, (uint16_t)mRttMode, 0);
}


/*==============================================================================
 *                     Modem Audio DVT and Debug
 *============================================================================*/

status_t SpeechDriverNormal::SetModemLoopbackPoint(uint16_t loopback_point) {
    ALOGD("%s(), loopback_point: %d", __FUNCTION__, loopback_point);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_SET_LPBK_POINT_DVT, loopback_point, 0);
}
/*==============================================================================
 *                     ECALL
 *============================================================================*/

status_t SpeechDriverNormal::eCallIvsSwitch(bool enable) {
    ALOGD("%s(), enable: %d", __FUNCTION__, enable);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_IVS_SWITCH, enable, 0);
}

status_t SpeechDriverNormal::eCallIvsSend() {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_IVS_SEND, 0, 0);
}

status_t SpeechDriverNormal::eCallPsapSwitch(bool enable) {
    ALOGD("%s(), enable: %d", __FUNCTION__, enable);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_PSAP_SWITCH, enable, 0);
}

status_t SpeechDriverNormal::eCallPsapSend() {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_PSAP_SEND, 0, 0);
}

status_t SpeechDriverNormal::eCallCtrlSeqSwitch(bool enable) {
    ALOGD("%s(), enable: %d", __FUNCTION__, enable);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_ECALL_CTL_SEQ_SWITCH, enable, 0);
}

status_t SpeechDriverNormal::eCallMsd(void *data, uint16_t len) {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    return sendPayload(&sph_msg, MSG_A2M_ECALL_MSD, SHARE_BUFF_DATA_TYPE_CCCI_ECALL_MSD_TYPE, data, len);
}

status_t SpeechDriverNormal::eCallTxCtrlParam(void *data, uint16_t len) {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    return sendPayload(&sph_msg, MSG_A2M_ECALL_TX_CTRL_PAR, SHARE_BUFF_DATA_TYPE_CCCI_ECALL_MSD_TYPE, data, len);
}

status_t SpeechDriverNormal::eCallInfo() {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_ECALL_HANDSHAKE_INFO_READ_ACK, 0, 0);
}

status_t SpeechDriverNormal::eCallRxCtrl() {
    ALOGD("%s()", __FUNCTION__);

    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_ECALL_RX_CTRL_READ_ACK, 0, 0);
}


/*==============================================================================
 *                     Acoustic Loopback
 *============================================================================*/

status_t SpeechDriverNormal::SetAcousticLoopback(bool loopback_on) {
    ALOGD("%s(), loopback_on: %d, mModemLoopbackDelayFrames: %d, mUseBtCodec: %d",
          __FUNCTION__, loopback_on, mModemLoopbackDelayFrames, mUseBtCodec);

    int retval = 0;

    if (loopback_on == true) {
        CheckApSideModemStatusAllOffOrDie();
        SetApSideModemStatus(LOOPBACK_STATUS_MASK);

        retval = SpeechOnByApplication(SPH_APPLICATION_LOOPBACK);
    } else {
        mUseBtCodec = true;

        /* should send sph off first and then clean state */
        retval = SpeechOffByApplication(SPH_APPLICATION_LOOPBACK);

        ResetApSideModemStatus(LOOPBACK_STATUS_MASK);
        CheckApSideModemStatusAllOffOrDie();
    }

    return retval;
}


status_t SpeechDriverNormal::SetAcousticLoopbackBtCodec(bool enable_codec) {
    ALOGD("%s(), mUseBtCodec: %d => %d", __FUNCTION__, mUseBtCodec, enable_codec);
    mUseBtCodec = enable_codec;
    return 0;
}


status_t SpeechDriverNormal::SetAcousticLoopbackDelayFrames(int32_t delay_frames) {
    ALOGD("%s(), mModemLoopbackDelayFrames: %d => %d", __FUNCTION__,
          mModemLoopbackDelayFrames, delay_frames);

    if (delay_frames < 0) {
        ALOGE("%s(), delay_frames(%d) < 0!! set 0 instead", __FUNCTION__, delay_frames);
        delay_frames = 0;
    }

    mModemLoopbackDelayFrames = (uint8_t)delay_frames;
    if (mModemLoopbackDelayFrames > MAX_LOOPBACK_DELAY_FRAMES) {
        ALOGE("%s(), delay_frames(%d) > %d!! set %d instead.", __FUNCTION__,
              mModemLoopbackDelayFrames, MAX_LOOPBACK_DELAY_FRAMES, MAX_LOOPBACK_DELAY_FRAMES);
        mModemLoopbackDelayFrames = MAX_LOOPBACK_DELAY_FRAMES;
    }

    if (mApplication == SPH_APPLICATION_LOOPBACK) {
        ALOGW("Loopback is enabled now! The new delay_frames will be applied next time");
    }

    return 0;
}



/*==============================================================================
 *                     Volume Control
 *============================================================================*/

status_t SpeechDriverNormal::SetDownlinkGain(int16_t gain) {
    static AudioLock gainLock;
    AL_AUTOLOCK(gainLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    if (gain != mDownlinkGain) {
        ALOGD("%s(), mDownlinkGain: 0x%x => 0x%x", __FUNCTION__, mDownlinkGain, gain);
    }
    mDownlinkGain = gain;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_SPH_DL_DIGIT_VOLUME, gain, 0);
}


status_t SpeechDriverNormal::SetEnh1DownlinkGain(int16_t gain) {
    static AudioLock gainLock;
    AL_AUTOLOCK(gainLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    if (gain != mDownlinkenh1Gain) {
        ALOGD("%s(), mDownlinkenh1Gain: 0x%x => 0x%x", __FUNCTION__, mDownlinkenh1Gain, gain);
    }
    mDownlinkenh1Gain = gain;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_SPH_DL_ENH_REF_DIGIT_VOLUME, gain, 0);
}


status_t SpeechDriverNormal::SetUplinkGain(int16_t gain) {
    static AudioLock gainLock;
    AL_AUTOLOCK(gainLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    if (gain != mUplinkGain) {
        ALOGD("%s(), mUplinkGain: 0x%x => 0x%x", __FUNCTION__, mUplinkGain, gain);
    }
    mUplinkGain = gain;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_SPH_UL_DIGIT_VOLUME, gain, 0);
}


status_t SpeechDriverNormal::SetDownlinkMute(bool mute_on) {
    static AudioLock muteLock;
    AL_AUTOLOCK(muteLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    ALOGD("%s(), mDownlinkMuteOn: %d => %d", __FUNCTION__, mDownlinkMuteOn, mute_on);
    mDownlinkMuteOn = mute_on;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_MUTE_SPH_DL, mute_on, 0);
}


status_t SpeechDriverNormal::SetDynamicDownlinkMute(bool mute_on) {
    static AudioLock muteLock;
    AL_AUTOLOCK(muteLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    ALOGD("%s(), mDynamicDownlinkMuteOn: %d => %d", __FUNCTION__, mDynamicDownlinkMuteOn, mute_on);
    mDynamicDownlinkMuteOn = mute_on;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_MUTE_SPH_DYNAMIC_DL, mute_on, 0);
}


status_t SpeechDriverNormal::SetDownlinkMuteCodec(bool mute_on) {
    static AudioLock muteLock;
    AL_AUTOLOCK(muteLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    ALOGD("%s(), mute_on: %d", __FUNCTION__, mute_on);
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_MUTE_SPH_DL_CODEC, mute_on, 0);
}


status_t SpeechDriverNormal::SetUplinkMute(bool mute_on) {
    static AudioLock muteLock;
    AL_AUTOLOCK(muteLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    ALOGD("%s(), mUplinkMuteOn: %d => %d", __FUNCTION__, mUplinkMuteOn, mute_on);
    mUplinkMuteOn = mute_on;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_MUTE_SPH_UL, mute_on, 0);
}


status_t SpeechDriverNormal::SetUplinkSourceMute(bool mute_on) {
    static AudioLock muteLock;
    AL_AUTOLOCK(muteLock);

    if (isSpeechApplicationOn() == false) {
        return 0;
    }
    ALOGD("%s(), mUplinkSourceMuteOn: %d => %d", __FUNCTION__, mUplinkSourceMuteOn, mute_on);
    mUplinkSourceMuteOn = mute_on;
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_MUTE_SPH_UL_SOURCE, mute_on, 0);
}

void SpeechDriverNormal::setMuteInfo(uint32_t muteInfo) {
    static AudioLock muteLock;
    AL_AUTOLOCK(muteLock);

    ALOGD("%s(), mMuteInfo: %d => %d", __FUNCTION__, mMuteInfo, muteInfo);

    mMuteInfo = muteInfo;
    mDownlinkMuteOn = muteInfo & SPH_ON_MUTE_BIT_DL;
    mUplinkMuteOn = muteInfo & SPH_ON_MUTE_BIT_UL;
    mUplinkSourceMuteOn = muteInfo & SPH_ON_MUTE_BIT_UL_SOURCE;
    mDynamicDownlinkMuteOn = muteInfo & SPH_ON_MUTE_BIT_DL_DYNAMIC;
}




/*==============================================================================
 *                     Device related Config
 *============================================================================*/

status_t SpeechDriverNormal::SetModemSideSamplingRate(uint16_t sample_rate) {
    mSampleRateEnum = sph_sample_rate_value_to_enum(sample_rate);
    return 0;
}

status_t SpeechDriverNormal::switchBTMode(uint32_t sample_rate) {
    sph_msg_t sph_msg;
    sph_info_t sph_info;

    ALOGD("%s(), sample_rate: %u", __FUNCTION__, sample_rate);

    // Set MD side sampling rate
    SetModemSideSamplingRate(sample_rate);

    // set bt mode no need to parse parameter
    mIsBTSwitchConfig = true;
    configSpeechInfo(&sph_info);
    mIsBTSwitchConfig = false;

    int retval = sendPayload(&sph_msg, MSG_A2M_SPH_DEV_CHANGE,
                             SHARE_BUFF_DATA_TYPE_CCCI_SPH_INFO,
                             &sph_info, sizeof(sph_info_t));

    return retval;
}

void SpeechDriverNormal::setBTMode(const int mode) {
    ALOGD("%s(), mBTMode: %d => %d", __FUNCTION__, mBTMode, mode);
    mBTMode = mode;
}

void SpeechDriverNormal::setBtSpkDevice(const bool flag) {
    ALOGD("%s(), set BtSpkDevice : %d => %d", __FUNCTION__, isBtSpkDevice, flag);
    isBtSpkDevice = flag;
}


/*==============================================================================
 *                     Speech Enhancement Control
 *============================================================================*/

status_t SpeechDriverNormal::SetSpeechEnhancement(bool enhance_on) {
    ALOGD("%s(), enhance_on = %d ", __FUNCTION__, enhance_on);
    sph_msg_t sph_msg;
    return sendMailbox(&sph_msg, MSG_A2M_CTRL_SPH_ENH, enhance_on, 0);
}


status_t SpeechDriverNormal::SetSpeechEnhancementMask(const sph_enh_mask_struct_t &mask) {
    sph_msg_t sph_msg;

    uint16_t enh_dynamic_ctrl = speechEnhancementMaskWrapper(mask.dynamic_func);

    ALOGD("%s(), enh_dynamic_ctrl mask 0x%x", __FUNCTION__, enh_dynamic_ctrl);
    return sendMailbox(&sph_msg, MSG_A2M_ENH_CTRL_SUPPORT, enh_dynamic_ctrl, 0);
}

/*
 * from: sph_enh_dynamic_mask_t
 * to:   sph_enh_dynamic_ctrl_t
 */
uint16_t SpeechDriverNormal::speechEnhancementMaskWrapper(const uint32_t enh_dynamic_mask) {
    uint16_t enh_dynamic_ctrl = 0;

    /* DMNR */
    if (enh_dynamic_mask & SPH_ENH_DYNAMIC_MASK_DMNR) {
        enh_dynamic_ctrl |= SPH_ENH_DYNAMIC_CTRL_MASK_DMNR;
    }

    /* TDNC */
    enh_dynamic_ctrl |= SPH_ENH_DYNAMIC_CTRL_MASK_TDNC; /* always on */

    /* MAGIC CONFERENCE */
    if (enh_dynamic_mask & SPH_ENH_DYNAMIC_MASK_LSPK_DMNR) {
        enh_dynamic_ctrl |= SPH_ENH_DYNAMIC_CTRL_MASK_MAGIC_CONFERENCE;
    }

#if defined(MTK_SPEECH_DE_REVERB)
    /* DE REVERB */
    if (enh_dynamic_mask & SPH_ENH_DYNAMIC_MASK_DE_REVERB) {
        enh_dynamic_ctrl |= SPH_ENH_DYNAMIC_CTRL_MASK_DE_REVERB;
        enableDeReverb(true);
    }
#endif

    return enh_dynamic_ctrl;
}


status_t SpeechDriverNormal::SetBtHeadsetNrecOn(const bool bt_headset_nrec_on) {
    ALOGD("%s(), mBtHeadsetNrecOn: %d => %d", __FUNCTION__, mBtHeadsetNrecOn, bt_headset_nrec_on);
    mBtHeadsetNrecOn = bt_headset_nrec_on; /* will be applied later in SetSpeechMode() */
    return 0;
}

void SpeechDriverNormal::setBtHeadsetName(const char *bt_headset_name) {
    ALOGD("%s(), Kyle mBtHeadsetName: %s => %s", __FUNCTION__, mBtHeadsetName, bt_headset_name);
    if (bt_headset_name != NULL && strlen(bt_headset_name) > 0) {
        ALOGD("%s(), mBtHeadsetName: %s => %s", __FUNCTION__, mBtHeadsetName, bt_headset_name);
        strncpy(mBtHeadsetName, bt_headset_name, sizeof(mBtHeadsetName) - 1);
        mBtHeadsetName[sizeof(mBtHeadsetName) - 1] = '\0'; // for coverity check null-terminated string
        ALOGD("%s(),cov  mBtHeadsetName: %s => %s, sizeof(mBtHeadsetName): %d", __FUNCTION__, mBtHeadsetName, bt_headset_name, (int)(sizeof(mBtHeadsetName)));
    }
}

/*==============================================================================
 *                     Speech Enhancement Parameters
 *============================================================================*/

int SpeechDriverNormal::parseSpeechParam(const SpeechScenario scenario) {
    int retval = 0;

    static AudioLock parserAttrLock;
    AL_AUTOLOCK(parserAttrLock);
    mSpeechParserAttribute.inputDevice = mInputDevice;
    mSpeechParserAttribute.outputDevice =  mOutputDevice;
    mSpeechParserAttribute.idxVolume = mVolumeIndex;
    mSpeechParserAttribute.driverScenario = (SpeechScenario)scenario;
    mSpeechParserAttribute.ttyMode = mTtyMode;
    bool isHacOn = (SpeechEnhancementController::GetInstance()->GetHACOn()) ? true : false; // for coverity
    updateFeatureMask(SPEECH_FEATURE_HAC, isHacOn);
    updateFeatureMask(SPEECH_FEATURE_BTNREC, mBtHeadsetNrecOn);

    uint32_t parsed_size = 0;
    mIsUsePreviousParam = false;
    mIsParseFail = false;
#if defined(MTK_SPEECH_USIP_EMI_SUPPORT)
    char keyString[MAX_SPEECH_PARSER_KEY_LEN];
    memset((void *)keyString, 0, MAX_SPEECH_PARSER_KEY_LEN);

    uint16_t mdVersion = get_uint32_from_mixctrl(kPropertyKeyModemVersion);

    if (mSpeechMessenger != NULL) {
        uint16_t uMdVersion = mSpeechMessenger->getMdVersion();
        if (uMdVersion != 0) {
            mdVersion = uMdVersion;
        }
    }

    //"SPEECH_PARSER_SET_PARAM,MDVERSION="
    int ret = snprintf(keyString, MAX_SPEECH_PARSER_KEY_LEN, "%s,%s=%d", SPEECH_PARSER_SET_KEY_PREFIX, SPEECH_PARSER_MD_VERSION, mdVersion);
    if (ret < 0 || ret >= MAX_SPEECH_PARSER_KEY_LEN) {
        ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, keyString, MAX_SPEECH_PARSER_KEY_LEN, ret);
    }
    int retVal = setParameter(keyString);
#endif

    SpeechDataBufType outBuf;
    memset(&outBuf, 0, sizeof(outBuf));

    retval = SpeechParserBase::getInstance()->getParamBuffer(mSpeechParserAttribute, &outBuf);
    if (retval != 0) {
        mIsParseFail = true;
        ALOGE("%s(), parameter parse fail (retval = %d), return. md use default parameter.",
              __FUNCTION__, -EFAULT);
        return -EFAULT;
    } else if (outBuf.dataSize == 0) {
        mIsUsePreviousParam = true;
        ALOGW("%s(), parsed_size 0, return. md use previous parameter.",
              __FUNCTION__);
        return -EFAULT;
    } else if (outBuf.dataSize > outBuf.memorySize) {
        mIsParseFail = true;
        ALOGW("%s(), parsed_size %u > memorySize %u",
              __FUNCTION__, outBuf.dataSize, outBuf.memorySize);
        WARNING("overflow!!");
        return -ENOMEM;
    }
    if (mSpeechParam.bufferAddr == NULL || mSpeechParam.dataSize < outBuf.dataSize) {
        if (mSpeechParam.dataSize < outBuf.dataSize) {
            AUDIO_FREE_POINTER(mSpeechParam.bufferAddr);
        }
        if (outBuf.dataSize > 0) {
            AUDIO_ALLOC_BUFFER(mSpeechParam.bufferAddr, outBuf.dataSize);
        } else {
            ASSERT(outBuf.dataSize > 0);
        }
    }
    mSpeechParam.dataSize = outBuf.dataSize;
    mSpeechParam.memorySize = outBuf.memorySize;
    memcpy(mSpeechParam.bufferAddr, outBuf.bufferAddr, outBuf.dataSize);

    ALOGD("%s(), parsed_size=%d, mIsParseFail=%d", __FUNCTION__, outBuf.dataSize, mIsParseFail);
    return retval;

}

int SpeechDriverNormal::writeAllSpeechParametersToModem(uint32_t *p_length, uint32_t *p_index) {
    sph_msg_t sph_msg;

    int retval = 0;
    // write to share memory
    uint32_t write_idx = 0;
    retval = mSpeechMessenger->writeSphParamToShareMemory(mSpeechParam.bufferAddr,
                                                          mSpeechParam.dataSize,
                                                          &write_idx);
    if (retval != 0) { // shm fail =>
        ALOGE("%s(), dataSize %u, writeSphParamToShareMemory FAIIL!!",
              __FUNCTION__, mSpeechParam.dataSize);
    }
    // update length & index
    if (retval == 0) {
        *p_length = mSpeechParam.dataSize;
        *p_index  = write_idx;
    }
    AUDIO_FREE_POINTER(mSpeechParam.bufferAddr);
    mSpeechParam.memorySize = 0;
    mSpeechParam.dataSize   = 0;

    return retval;
}

int SpeechDriverNormal::updateSpeechParam(const SpeechScenario scenario) {
    AL_AUTOLOCK_MS(mSpeechParamLock, MAX_SPEECH_AUTO_LOCK_TIMEOUT_MS); // atomic: write shm & send msg

    sph_msg_t sph_msg;
    int retval = 0;

    // parse
    retval = parseSpeechParam(scenario);

    // share memory
    uint32_t write_idx = 0;
    if (retval == 0) { //only transfer data if parse successfully
#if defined(MTK_SPEECH_USIP_EMI_SUPPORT)
        sph_info_t sph_info;
        configSpeechInfo(&sph_info);
        // send sph param to modem side
        retval = sendPayload(&sph_msg, MSG_A2M_DYNAMIC_PAR_IN_STRUCT_SHM,
                             SHARE_BUFF_DATA_TYPE_CCCI_SPH_INFO,
                             &sph_info, sizeof(sph_info_t));
#else
        retval = mSpeechMessenger->writeSphParamToShareMemory(mSpeechParam.bufferAddr,
                                                              mSpeechParam.dataSize,
                                                              &write_idx);
        // send sph param to modem side
        if (retval == 0) { // via share memory
            retval = sendMailbox(&sph_msg, MSG_A2M_DYNAMIC_PAR_IN_STRUCT_SHM,
                                 mSpeechParam.dataSize, write_idx);
        } else {
            ALOGE("%s(), dataSize %u, writeSphParamToShareMemory Fail!!",
                  __FUNCTION__, mSpeechParam.dataSize);
        }
#endif
    }
    ALOGD("%s(), dataSize: %d", __FUNCTION__, mSpeechParam.dataSize);
    AUDIO_FREE_POINTER(mSpeechParam.bufferAddr);
    mSpeechParam.memorySize = 0;
    mSpeechParam.dataSize   = 0;
    return retval;
}


status_t SpeechDriverNormal::GetSmartpaParam(void *eParamSmartpa) {
    /* error handling */
    if (eParamSmartpa == NULL) {
        ALOGW("%s(), eParamSmartpa == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    return 0;
}


status_t SpeechDriverNormal::SetSmartpaParam(void *eParamSmartpa) {
    /* error handling */
    if (eParamSmartpa == NULL) {
        ALOGW("%s(), eParamSmartpa == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    return 0;
}



/*==============================================================================
 *                     Recover State
 *============================================================================*/

void SpeechDriverNormal::waitModemAckAfterApDie() {
    AL_LOCK(mReadMsgThreadCreatedLock);
    // wait until readSpeechMessageThread created
    if (mReadMsgThreadCreated == false) {
        AL_WAIT_NO_TIMEOUT(mReadMsgThreadCreatedLock);
    }
    AL_UNLOCK(mReadMsgThreadCreatedLock);

    AL_LOCK(mWaitModemAckAfterApDieLock);
    ALOGV("%s(), wait 1s for readthread bypass ack", __FUNCTION__);
    if (AL_WAIT_MS(mWaitModemAckAfterApDieLock, 1000) != 0) {
        ALOGW("wait time out, ack missed, make fake ack!");
        sph_msg_t sph_msg;
        configMailBox(&sph_msg, mApWaitAckMsgID | 0x8000, 0, 0);
        // update mModemSideModemStatus
        processModemAckMessage(&sph_msg);
        mNeedWaitModemAckAfterApDie = false;
    }
    AL_UNLOCK(mWaitModemAckAfterApDieLock);
}


void SpeechDriverNormal::RecoverModemSideStatusToInitState() {
    // send msg to MD but not get ack yet
    if (mNeedWaitModemAckAfterApDie == true) {
        ALOGD("%s(), mModemSideModemStatus: 0x%x, waitAckMsgId:0x%x, mReadMsgThreadCreated:0x%x",
              __FUNCTION__, mModemSideModemStatus, mApWaitAckMsgID, mReadMsgThreadCreated);
        waitModemAckAfterApDie();
    }

    if (mModemSideModemStatus != 0) {
        ALOGD("%s(), mModemIndex: %d, mModemSideModemStatus: 0x%x", __FUNCTION__,
              mModemIndex, mModemSideModemStatus);
    }

    // Raw Record
    if (getModemSideModemStatus(RAW_RECORD_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, raw_record_on = true",  __FUNCTION__, mModemIndex);
        SetApSideModemStatus(RAW_RECORD_STATUS_MASK);
        recordOff(mRecordType);
    }

    // VM Record
    if (getModemSideModemStatus(VM_RECORD_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, vm_on = true",  __FUNCTION__, mModemIndex);
        SetApSideModemStatus(VM_RECORD_STATUS_MASK);
        VoiceMemoRecordOff();
    }

    // BGS
    if (getModemSideModemStatus(BGS_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, bgs_on = true", __FUNCTION__, mModemIndex);
        SetApSideModemStatus(BGS_STATUS_MASK);
        BGSoundOff();
    }

#if defined(MTK_SPEECH_VOICE_MIXER_SUPPORT)
    // Voip Rx
    if (getModemSideModemStatus(PCM_MIXER_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, voip_rx_on = true", __FUNCTION__, mModemIndex);
        SetApSideModemStatus(PCM_MIXER_STATUS_MASK);
        VoipRxOff();
    }
#endif

    // Telephony Tx
    if (getModemSideModemStatus(TELEPHONY_TX_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, telephony_tx_on = true", __FUNCTION__, mModemIndex);
        SetApSideModemStatus(TELEPHONY_TX_STATUS_MASK);
        TelephonyTxOff();
    }

    // TTY
    if (getModemSideModemStatus(TTY_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, tty_on = true", __FUNCTION__, mModemIndex);
        SetApSideModemStatus(TTY_STATUS_MASK);
        TtyCtmOff();
    }

    // P2W
    if (getModemSideModemStatus(P2W_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, p2w_on = true", __FUNCTION__, mModemIndex);
        SetApSideModemStatus(P2W_STATUS_MASK);
        PCM2WayOff();
    }

    // SPH (Phone Call / VT / Loopback / ...)
    if (getModemSideModemStatus(SPEECH_STATUS_MASK) == true) {
        ALOGD("%s(), mModemIndex = %d, speech_on = true", __FUNCTION__, mModemIndex);
        SetApSideModemStatus(SPEECH_STATUS_MASK);
        mApplication = SPH_APPLICATION_NORMAL;
        SpeechOff();
    }
    mApResetDuringSpeech = false;
}



/*==============================================================================
 *                     Check Modem Status
 *============================================================================*/

bool SpeechDriverNormal::CheckModemIsReady() {
    if (mSpeechMessenger == NULL) {
        return false;
    }

    return (mSpeechMessenger->checkModemReady() == true &&
            mModemResetDuringSpeech == false);
}

/*==============================================================================
 *                     Delay sync
 *============================================================================*/

int SpeechDriverNormal::getBtDelayTime(uint16_t *p_bt_delay_ms) {
    if (p_bt_delay_ms == NULL) {
        ALOGW("%s(), p_bt_delay_ms == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    if (strlen(mBtHeadsetName) == 0) {
        ALOGW("%s(), mBtHeadsetName invalid!!", __FUNCTION__);
        *p_bt_delay_ms = 0;
        return -ENODEV;
    }

    *p_bt_delay_ms = SpeechConfig::getInstance()->getBtDelayTime(mBtHeadsetName);
    return 0;
}

int SpeechDriverNormal::getUsbDelayTime(uint8_t *usbDelayMs) {
    int retVal = 0;
    if (usbDelayMs == NULL) {
        ALOGW("%s(), p_usb_delay_ms == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }

    uint8_t delayMs = 0;

    /* SpeechEchoRef_AudioParam.xml */
    retVal = SpeechConfig::getInstance()->getEchoRefParam(&delayMs);
    if (retVal == 0) {
        usbDelayMs = &delayMs;
    } else {
        *usbDelayMs = 0;
    }
    return retVal;
}

int SpeechDriverNormal::getDriverParam(uint8_t paramType, void *paramBuf) {
    int retVal = 0;
    if (paramBuf == NULL) {
        ALOGW("%s(), paramBuf == NULL!! return", __FUNCTION__);
        return -EFAULT;
    }
    if (paramType >= NUM_DRIVER_PARAM) {
        ALOGW("%s(), paramType(%d) Invalid!! return", __FUNCTION__, paramType);
        return -EFAULT;
    }

    /* SpeechGeneral_AudioParam.xml */
    retVal = SpeechConfig::getInstance()->getDriverParam(paramType, paramBuf);
    return retVal;
}

int SpeechDriverNormal::updateFeatureMask(const SpeechFeatureType featureType, const bool flagOn) {
    AL_AUTOLOCK(mFeatureMaskLock);
    uint16_t featureMaskType = 1 << featureType;

    const bool currentFlagOn = ((mSpeechParserAttribute.speechFeatureOn & featureMaskType) > 0);
    if (flagOn == currentFlagOn) {
        ALOGV("%s(), featureMaskType(0x%x), flagOn(%d) == currentFeature(0x%x), return",
              __FUNCTION__, featureMaskType, flagOn, mSpeechParserAttribute.speechFeatureOn);
        return -ENOSYS;
    }
    if (flagOn == false) {
        mSpeechParserAttribute.speechFeatureOn &= (~featureMaskType);
    } else {
        mSpeechParserAttribute.speechFeatureOn |= featureMaskType;
    }
    ALOGD("%s() featureType:%d, flagon:%d, speechFeatureOn:%d",
          __FUNCTION__, featureType, flagOn, mSpeechParserAttribute.speechFeatureOn);
    return 0;
}

int SpeechDriverNormal::updateSpeechFeature(const SpeechFeatureType featureType, const bool flagOn) {
    int retVal = 0;
    retVal = updateFeatureMask(featureType, flagOn);
    if (retVal == 0) {
        updateSpeechParam(SPEECH_SCENARIO_FEATURE_CHANGE);
    }
    return retVal;
}

int SpeechDriverNormal::updateCustScene(const char *custScene) {
    int retVal = 0;
    char keyString[MAX_SPEECH_PARSER_KEY_LEN];
    memset((void *)keyString, 0, MAX_SPEECH_PARSER_KEY_LEN);

    //"SPEECH_PARSER_SET_PARAM,CUSTOM_SCENE="
    int ret = snprintf(keyString, MAX_SPEECH_PARSER_KEY_LEN,
                       "SPEECH_PARSER_SET_PARAM,CUSTOM_SCENE=%s",
                       custScene);
    if (ret < 0 || ret >= MAX_SPEECH_PARSER_KEY_LEN) {
        ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, keyString, MAX_SPEECH_PARSER_KEY_LEN, ret);
    }
    retVal = setParameter(keyString);
    if (retVal == 0) {
        updateSpeechFeature(SPEECH_FEATURE_SCENE, true);
    }
    updateFeatureMask(SPEECH_FEATURE_SCENE, false);
    return retVal;
}

int SpeechDriverNormal::enableDeReverb(const bool enable) {
    int retVal = 0;
    char keyString[MAX_SPEECH_PARSER_KEY_LEN];
    memset((void *)keyString, 0, MAX_SPEECH_PARSER_KEY_LEN);

    //"SPEECH_PARSER_SET_PARAM,PHONE_CALL_DE_REVERB="
    int ret = snprintf(keyString, MAX_SPEECH_PARSER_KEY_LEN,
                       "SPEECH_PARSER_SET_PARAM,PHONE_CALL_DE_REVERB=%d",
                       enable);
    if (ret < 0 || ret >= MAX_SPEECH_PARSER_KEY_LEN) {
        ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, keyString, MAX_SPEECH_PARSER_KEY_LEN, ret);
    }
    retVal = setParameter(keyString);
    return retVal;
}

int SpeechDriverNormal::updateCustInfo(const char *custInfo) {
    int retVal = 0;
    char keyString[MAX_SPEECH_PARSER_KEY_LEN];
    memset((void *)keyString, 0, MAX_SPEECH_PARSER_KEY_LEN);

    //"SPEECH_PARSER_SET_PARAM,CUSTOM_INFO="
    int ret = snprintf(keyString, MAX_SPEECH_PARSER_KEY_LEN,
                       "SPEECH_PARSER_SET_PARAM,CUSTOM_INFO=%s",
                       custInfo);
    if (ret < 0 || ret >= MAX_SPEECH_PARSER_KEY_LEN) {
        ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, keyString, MAX_SPEECH_PARSER_KEY_LEN, ret);
    }
    retVal = setParameter(keyString);
    if (retVal == 0) {
        updateSpeechFeature(SPEECH_FEATURE_CUSTOM_INFO, true);
    }
    updateFeatureMask(SPEECH_FEATURE_CUSTOM_INFO, false);
    return retVal;
}

int SpeechDriverNormal::setParameter(const char *keyParameter) {
    char keySpeechParserSetParam[] = "SPEECH_PARSER_SET_PARAM";

    if (keyParameter == NULL) {
        return 0;
    } else {
        ALOGD("%s(), %s", __FUNCTION__, keyParameter);
        char keyString[MAX_SPEECH_PARSER_KEY_LEN];
        if (strncmp(keySpeechParserSetParam, keyParameter, strlen(keySpeechParserSetParam)) == 0) {
            SpeechStringBufType keyValuePair;
            memset(&keyValuePair, 0, sizeof(SpeechStringBufType));
            strncpy(keyString, keyParameter, MAX_SPEECH_PARSER_KEY_LEN - 1);
            keyString[MAX_SPEECH_PARSER_KEY_LEN - 1] = '\0';  // for coverity check null-terminated string
            keyValuePair.stringAddr = keyString;
            keyValuePair.memorySize = strlen(keyString) + 1;
            keyValuePair.stringSize = strlen(keyString);
            return SpeechParserBase::getInstance()->setKeyValuePair(&keyValuePair);
        } else {
            ALOGD("%s(), string != %s. return.", __FUNCTION__, keySpeechParserSetParam);
            return 0;
        }
    }
}

const char *SpeechDriverNormal::getParameter(const char *keyParameter) {
    char keySpeechParserGetParam[] = "SPEECH_PARSER_GET_PARAM";

    if (keyParameter == NULL) {
        return NULL;
    } else {
        ALOGD("+%s(), %s", __FUNCTION__, keyParameter);
        if (strncmp(keySpeechParserGetParam, keyParameter, strlen(keySpeechParserGetParam)) == 0) {
            SpeechStringBufType keyValuePair;
            memset(&keyValuePair, 0, sizeof(SpeechStringBufType));
            strncpy(gKeyStringBuf, keyParameter, MAX_SPEECH_PARSER_KEY_LEN - 1);
            gKeyStringBuf[MAX_SPEECH_PARSER_KEY_LEN - 1] = '\0';  // for coverity check null-terminated string
            keyValuePair.stringAddr = gKeyStringBuf;
            keyValuePair.memorySize = strlen(gKeyStringBuf) + 1;
            keyValuePair.stringSize = strlen(gKeyStringBuf);
            SpeechParserBase::getInstance()->getKeyValuePair(&keyValuePair);
            ALOGD("-%s(), %s", __FUNCTION__, keyValuePair.stringAddr);
            return keyValuePair.stringAddr;
        } else {
            return NULL;
        }
    }
}


/*==============================================================================
 *                     IPC Control
 *============================================================================*/
void SpeechDriverNormal::processIpcLoopback() {
    bool isSpeechOn = GetApSideModemStatus(SPEECH_STATUS_MASK);
    if (isSpeechOn) {
        ALOGW("%s(), isSpeechOn: %d, return.", __FUNCTION__, isSpeechOn);
        LoopbackManager::GetInstance()->setIpcLpbkStatus(LOOPBACK_STATE_READY);
    } else {
        ALOGD("%s(), isSpeechOn: %d, mIpcLpbkSwitch=%d, ipcPath=0x%x",
              __FUNCTION__, isSpeechOn, mIpcLpbkSwitch, mSpeechParserAttribute.ipcPath);

        switch (mIpcLpbkSwitch) {
        case IPC_LOOPBACK_OFF:
            gLpbkRouting = IPC_LOOPBACK_CASE_INVALID;
            break;
        case IPC_LOOPBACK_ON_PCM:
            if (mSpeechParserAttribute.ipcPath == 2) {
                gLpbkRouting = IPC_LOOPBACK_PCM_CASE_HEADSET;//RX: Headset, TX: Headset Mic
            } else if (mSpeechParserAttribute.ipcPath == 6) {
                gLpbkRouting = IPC_LOOPBACK_PCM_CASE_SPK;//RX: Speaker, TX: Main Mic
            } else if (mSpeechParserAttribute.ipcPath == 7) {
                gLpbkRouting = IPC_LOOPBACK_PCM_CASE_HEADPHONE_MAINMIC;//RX: Headphone, TX: Main Mic
            } else {//default ipcPath == 1
                gLpbkRouting = IPC_LOOPBACK_PCM_CASE_HANDSET;//RX: Handset, TX: Main Mic
            }
            break;
        case IPC_LOOPBACK_ON:
            SetAcousticLoopbackDelayFrames(25);//delay 500ms
            if (mSpeechParserAttribute.ipcPath == 2) {
                gLpbkRouting = IPC_LOOPBACK_CASE_HEADSET;//RX: Headset, TX: Headset Mic
            } else if (mSpeechParserAttribute.ipcPath == 6) {
                gLpbkRouting = IPC_LOOPBACK_CASE_SPK;//RX: Speaker, TX: Main Mic
            } else if (mSpeechParserAttribute.ipcPath == 7) {
                gLpbkRouting = IPC_LOOPBACK_CASE_HEADPHONE_MAINMIC;//RX: Headphone, TX: Main Mic
            } else {//default ipcPath == 1
                gLpbkRouting = IPC_LOOPBACK_CASE_HANDSET;//RX: Handset, TX: Main Mic
            }
            break;
        case IPC_LOOPBACK_NODELAY:
            SetAcousticLoopbackDelayFrames(3);//delay 60ms
            if (mSpeechParserAttribute.ipcPath == 0x34) {
                gLpbkRouting = IPC_LOOPBACK_CASE_HEADPHONE_REFMIC;//RX: Headphone, TX: Ref Mic
            } else if (mSpeechParserAttribute.ipcPath == 0x35) {
                gLpbkRouting = IPC_LOOPBACK_CASE_HEADPHONE_3RDMIC;//RX: Headphone, TX: 3rd Mic
            } else {//default ipcPath == 0x33
                gLpbkRouting = IPC_LOOPBACK_CASE_HEADPHONE_MAINMIC;//RX: Headphone, TX: Main Mic
            }
            break;
        default:
            gLpbkRouting = IPC_LOOPBACK_CASE_INVALID;
            break;
        }
        SpeechDriverInterface::mIpcLpbkSwitch = mIpcLpbkSwitch;
        SpeechDriverInterface::mIpcPath = mSpeechParserAttribute.ipcPath;
        AudioEventThreadManager::getInstance()->notifyCallback(AUDIO_EVENT_IPC_LOOPBACK, &gLpbkRouting);
    }
    return;
}

int SpeechDriverNormal::sendIpcCmd(int cmd) {
    uint16_t param16bit = 0;
    uint32_t param32bit = 0;
    sph_msg_t sphMsg;
    int retval = NO_ERROR, indexDv = 0;
    switch (cmd) {
    default:
        unsigned char lpbkSwitch = 0;
        unsigned char lpbkAudioPath = 0;
        switch (cmd) {
        case 35:
            lpbkSwitch = IPC_LOOPBACK_OFF;
            break;
        case 36:
            lpbkSwitch = IPC_LOOPBACK_ON;
            lpbkAudioPath = 1;
            break;
        case 37:
            lpbkSwitch = IPC_LOOPBACK_ON;
            lpbkAudioPath = 2;
            break;
        case 38:
            lpbkSwitch = IPC_LOOPBACK_ON;
            lpbkAudioPath = 6;
            break;
        case 39:
            lpbkSwitch = IPC_LOOPBACK_ON;
            lpbkAudioPath = 7;
            break;
        case 40:
            lpbkSwitch = IPC_LOOPBACK_NODELAY;
            lpbkAudioPath = 0x33;
            break;
        case 41:
            lpbkSwitch = IPC_LOOPBACK_NODELAY;
            lpbkAudioPath = 0x34;
            break;
        case 42:
            lpbkSwitch = IPC_LOOPBACK_NODELAY;
            lpbkAudioPath = 0x35;
            break;
        default:
            break;
        }
        mIpcLpbkSwitch = lpbkSwitch;
        mSpeechParserAttribute.ipcPath = lpbkAudioPath;
        ALOGD("%s(), IPC Loopback test cmd: 0x%x, mIpcLpbkSwitch: 0x%x, ipcPath: 0x%x",
              __FUNCTION__, cmd, mIpcLpbkSwitch,  mSpeechParserAttribute.ipcPath);
        processIpcLoopback();
    }
    return retval;
}

} /* end of namespace android */

