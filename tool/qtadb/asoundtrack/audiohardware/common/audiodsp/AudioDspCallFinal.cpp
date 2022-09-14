#include <AudioDspCallFinal.h>
#include <string.h>
#include <errno.h>
#include <system/audio.h>
#include <audio_time.h>
#include <AudioLock.h>
#include <AudioSmartPaController.h>
#include <AudioALSAHardwareResourceManager.h>
#include "AudioALSADeviceConfigManager.h"
#include <AudioALSADeviceParser.h>
#include <tinyalsa/asoundlib.h> // for mixctrl
#include <arsi_type.h>
#include <aurisys_lib_manager.h>
#include <audio_task.h>
#include <aurisys_scenario_dsp.h>
#include <aurisys_config.h>
#include <aurisys_utility.h>
#include <aurisys_controller.h>
#include <AudioDspType.h>
#include <AudioMessengerIPI.h>
#include "audio_dsp_service.h"
#include <SpeechUtility.h>
#include "AudioUtility.h"
static const char *PROPERTY_KEY_PCM_DUMP_ON = "persist.vendor.audiohal.aurisys.pcm_dump_on";
#define DUMP_DSP_PCM_DATA_PATH "/data/vendor/audiohal/aurisys_dump/"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioDspCallFinal"

namespace android {


/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */
static FILE     *fp_pcm_dump_final_in;
static FILE     *fp_pcm_dump_final_out;
static FILE     *fp_pcm_dump_final_iv;

/*
 * =============================================================================
 *                     global
 * =============================================================================
 */


/*
 * =============================================================================
 *                     Callback
 * =============================================================================
 */


/*
 * =============================================================================
 *                     Singleton Pattern
 * =============================================================================
 */

AudioDspCallFinal *AudioDspCallFinal::mAudioDspCallFinal = NULL;

AudioDspCallFinal *AudioDspCallFinal::getInstance(void) {
    static AudioLock mGetInstanceLock;

    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioDspCallFinal == NULL) {
        mAudioDspCallFinal = new AudioDspCallFinal();
    }
    return mAudioDspCallFinal;
}



/*==============================================================================
 *                     Constructor / Destructor / Init / Deinit
 *============================================================================*/
static struct mixer *mMixer;
AudioDspCallFinal::AudioDspCallFinal() {
    mInputDevice = AUDIO_DEVICE_IN_BUILTIN_MIC;
    mOutputDevice = AUDIO_DEVICE_OUT_EARPIECE;
    mSampleRate = 48000;
    mPcmDlIn = NULL;
    mPcmDlOut = NULL;
    mPcmIv = NULL;
    mPcmDsp = NULL;

    fp_pcm_dump_final_in = NULL;
    fp_pcm_dump_final_out = NULL;
    fp_pcm_dump_final_iv = NULL;

    mAurisysDspConfigCallFinal = NULL;
    mAurisysLibManagerCallFinal = NULL;

    mMixer = AudioALSADriverUtility::getInstance()->getMixer();
    ASSERT(mMixer != NULL);
    mHardwareResourceManager = AudioALSAHardwareResourceManager::getInstance();

    // initialize mConfig
    memset((void *)&mConfig, 0, sizeof(pcm_config));
    memset((void *)&mSpkIvConfig, 0, sizeof(pcm_config));
}


AudioDspCallFinal::~AudioDspCallFinal() {

}

void AudioDspCallFinal::processDmaMsgCallFinal(struct ipi_msg_t *msg, void *buf, uint32_t size, void *arg) {

    ALOGV("%s() msg_id=0x%x, task_scene=%d, param2=0x%x, size=%d arg=%p\n",
          __FUNCTION__, msg->msg_id, msg->task_scene, msg->param2, size, arg);

    switch (msg->msg_id) {
    case AUDIO_DSP_TASK_PCMDUMP_DATA:
        if (DEBUG_PCMDUMP_IN == msg->param2) {
            if (fp_pcm_dump_final_in) {
                AudioDumpPCMData(buf, size, fp_pcm_dump_final_in);
            }
        } else if (DEBUG_PCMDUMP_OUT == msg->param2) {
            if (fp_pcm_dump_final_out) {
                AudioDumpPCMData(buf, size, fp_pcm_dump_final_out);
            }
        } else if (DEBUG_PCMDUMP_IV == msg->param2) {
            if (fp_pcm_dump_final_iv) {
                AudioDumpPCMData(buf, size, fp_pcm_dump_final_iv);
            }
        }
        break;
    default:
        break;
    }
}

int AudioDspCallFinal::getDspCallFinalRuntimeEnable(void) {
    struct mixer_ctl *ctl = NULL;

    if (!isAdspOptionEnable())
        return false;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_call_final_runtime_en");
    if (ctl == NULL)
        return 0;

    return mixer_ctl_get_value(ctl, 0);
}

int AudioDspCallFinal::setCallFinalruntime(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_call_final_runtime_en"), 0, condition)) {
        ALOGW("%s(), set fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

int AudioDspCallFinal::setCallFinalRefruntime(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_call_final_ref_runtime_en"), 0, condition)) {
        ALOGW("%s(), set fail", __FUNCTION__);
        return -1;
    }
    return 0;
}


int AudioDspCallFinal::setCallFinalDspShareMem(bool condition) {
    if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "adsp_call_final_sharemem_scenario"), 0, condition)) {
        ALOGW("%s(), set fail", __FUNCTION__);
        return -1;
    }
    return 0;
}

void AudioDspCallFinal::SetArsiTaskConfigCallFinal(struct arsi_task_config_t *pTaskConfig) {
    if (!pTaskConfig) {
        return;
    }

    pTaskConfig->input_device_info.devices = mInputDevice;
    pTaskConfig->input_device_info.audio_format = AUDIO_FORMAT_PCM_16_BIT;
    pTaskConfig->input_device_info.sample_rate = 48000;
    pTaskConfig->input_device_info.num_channels = 2;
    pTaskConfig->input_device_info.channel_mask = AUDIO_CHANNEL_IN_STEREO;
    pTaskConfig->input_device_info.hw_info_mask = 0;

    pTaskConfig->output_device_info.devices = mOutputDevice;
    pTaskConfig->output_device_info.audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
    pTaskConfig->output_device_info.sample_rate = 48000;
    pTaskConfig->output_device_info.num_channels = 2;
    pTaskConfig->output_device_info.channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    pTaskConfig->output_device_info.hw_info_mask = 0;

    /* Speaker */
    if (mOutputDevice == AUDIO_DEVICE_OUT_SPEAKER) {
        if (AudioSmartPaController::getInstance()->isSmartPAUsed()) {
            pTaskConfig->output_device_info.hw_info_mask |= OUTPUT_DEVICE_HW_INFO_SMARTPA_SPEAKER;
        }
        if (AudioALSAHardwareResourceManager::getInstance()->getSpkNum() == 2) {
            pTaskConfig->output_device_info.hw_info_mask |= OUTPUT_DEVICE_HW_INFO_STEREO_SPEAKER;
        }
    }


    pTaskConfig->task_scene = TASK_SCENE_CALL_FINAL;
    pTaskConfig->audio_mode = AUDIO_MODE_IN_CALL;

    pTaskConfig->max_input_device_sample_rate  = 48000;
    pTaskConfig->max_output_device_sample_rate = 48000;
    pTaskConfig->max_input_device_num_channels = 2;
    pTaskConfig->max_output_device_num_channels = 2;

    pTaskConfig->output_flags = 0;
    pTaskConfig->input_source = 0;
    pTaskConfig->input_flags  = 0;


    pTaskConfig->enhancement_feature_mask = 0;

    dump_task_config(pTaskConfig);
}


void AudioDspCallFinal::SetArsiAttributeCallFinal() {
    struct stream_attribute_dsp *attribute = NULL;
    struct aurisys_lib_manager_config_t *pManagerConfig = NULL;

    pManagerConfig = &mAurisysDspConfigCallFinal->manager_config;

    if (!mAurisysDspConfigCallFinal) {
        return;
    }

    /* DL in attribute */
    attribute = &mAurisysDspConfigCallFinal->attribute[DATA_BUF_DOWNLINK_IN];
    attribute->num_channels = 2;
    attribute->sample_rate  = mSampleRate;
    attribute->audio_format = AUDIO_FORMAT_PCM_8_24_BIT;

    /* DL out attribute */
    attribute = &mAurisysDspConfigCallFinal->attribute[DATA_BUF_DOWNLINK_OUT];
    attribute->num_channels = 2;
    attribute->sample_rate  = mSampleRate;
    attribute->audio_format = AUDIO_FORMAT_PCM_8_24_BIT;

    /* DL iv attribute */
    attribute = &mAurisysDspConfigCallFinal->attribute[DATA_BUF_IV_BUFFER];
    attribute->num_channels = 2;
    attribute->sample_rate  = mSampleRate;
    attribute->audio_format = pManagerConfig->iv_format;
}


void AudioDspCallFinal::CreateAurisysLibManagerCallFinal() {
    struct aurisys_lib_manager_config_t *pManagerConfig = NULL;

    struct data_buf_t paramList;
    uint8_t *configAndParam = NULL;

    struct ipi_msg_t msg;
    int retval = 0;
    char file_path_final_in[100];
    char file_path_final_out[100];
    char file_path_final_iv[100];
    char string_time[100] = "unknown_time_";
    time_t rawtime;

    if (time(&rawtime) == ((time_t)-1)) {
        ALOGE("%s(), unknown rawtime", __FUNCTION__);
    } else {
        struct tm *timeinfo = localtime(&rawtime);
        if (timeinfo == NULL) {
            ALOGE("%s(), unknown time info", __FUNCTION__);
        } else {
            retval = strftime(string_time, 100, "%Y_%m_%d_%H%M%S_", timeinfo);
            if (retval == 0) {
                ALOGE("%s(), strftime error", __FUNCTION__);
            }
        }
    }

    paramList.data_size = 0;
    paramList.memory_size = 0;
    paramList.p_buffer = NULL;

    ALOGD("%s(+)", __FUNCTION__);

    if (mAurisysLibManagerCallFinal != NULL || mAurisysDspConfigCallFinal != NULL) {
        ALOGE("%p %p already init!!", mAurisysLibManagerCallFinal, mAurisysDspConfigCallFinal);
        WARNING("already init!!");
        return;
    }

    AudioMessengerIPI::getInstance()->registerDmaCbk(
        TASK_SCENE_CALL_FINAL,
        0x10000,
        0x40000,
        processDmaMsgCallFinal,
        this);

    AUDIO_ALLOC_STRUCT(struct aurisys_dsp_config_t, mAurisysDspConfigCallFinal);
    if (mAurisysDspConfigCallFinal == NULL) {
        ALOGE("mAurisysDspConfigCallFinal alloc Fail!!");
        return;
    }
    mAurisysDspConfigCallFinal->guard_head = AURISYS_GUARD_HEAD_VALUE;
    mAurisysDspConfigCallFinal->guard_tail = AURISYS_GUARD_TAIL_VALUE;

    /* manager config */
    pManagerConfig = &mAurisysDspConfigCallFinal->manager_config;

    pManagerConfig->aurisys_scenario = AURISYS_SCENARIO_DSP_CALL_FINAL;
    pManagerConfig->core_type = AURISYS_CORE_HIFI3;
    pManagerConfig->arsi_process_type = ARSI_PROCESS_TYPE_DL_ONLY;
    pManagerConfig->frame_size_ms = 20;
    pManagerConfig->num_channels_ul = 2;
    pManagerConfig->num_channels_dl = 2;
    pManagerConfig->audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
    pManagerConfig->sample_rate = mSampleRate;
    pManagerConfig->dsp_task_scene = TASK_SCENE_CALL_FINAL;

    if (AudioALSAHardwareResourceManager::getInstance()->is32BitI2sSupport()) {
        pManagerConfig->iv_format = AUDIO_FORMAT_PCM_32_BIT;
    } else {
        pManagerConfig->iv_format = AUDIO_FORMAT_PCM_8_24_BIT;
    }

    ALOGD("manager config: aurisys_scenario %u, core_type %d, " \
          "arsi_process_type %d, frame_size_ms %d, " \
          "num_channels_ul %d, num_channels_dl %d, " \
          "audio_format %u, sample_rate %u, iv_format %u",
          pManagerConfig->aurisys_scenario,
          pManagerConfig->core_type,
          pManagerConfig->arsi_process_type,
          pManagerConfig->frame_size_ms,
          pManagerConfig->num_channels_ul,
          pManagerConfig->num_channels_dl,
          pManagerConfig->audio_format,
          pManagerConfig->sample_rate,
          pManagerConfig->iv_format);

    /* task config */
    SetArsiTaskConfigCallFinal(&pManagerConfig->task_config);


    /* attribute */
    SetArsiAttributeCallFinal();

    /* gain */
    mAurisysDspConfigCallFinal->gain_config.ul_digital_gain = 0;
    mAurisysDspConfigCallFinal->gain_config.ul_analog_gain  = 0;
    mAurisysDspConfigCallFinal->gain_config.dl_digital_gain = 0;
    mAurisysDspConfigCallFinal->gain_config.dl_analog_gain  = 0;

    /* func */
    mAurisysDspConfigCallFinal->voip_on = false;
    mAurisysDspConfigCallFinal->aec_on = false;
    if (AudioSmartPaController::getInstance()->isSmartPAUsed() &&
        AudioSmartPaController::getInstance()->isSwDspSpkProtect(mOutputDevice)) {
        mAurisysDspConfigCallFinal->iv_on = true;
    }

    /* create manager */
    mAurisysLibManagerCallFinal = create_aurisys_lib_manager(pManagerConfig);
    if (mAurisysLibManagerCallFinal == NULL) {
        ALOGE("mAurisysLibManagerCallFinal create Fail!!");
        return;
    }

    /* parsing param */
    aurisys_parsing_param_file(mAurisysLibManagerCallFinal);

    paramList.data_size = 0;
    paramList.memory_size = 32768; /* TODO: refine it */
    AUDIO_ALLOC_BUFFER(paramList.p_buffer, paramList.memory_size);
    do {
        retval = aurisys_get_param_list(mAurisysLibManagerCallFinal, &paramList);
        if (retval == 0) {
            break;
        }
        ALOGE("%s(), paramList.memory_size %u not enough!!",
              __FUNCTION__, paramList.memory_size);

        AUDIO_FREE_POINTER(paramList.p_buffer);
        paramList.data_size = 0;
        paramList.memory_size *= 2;
        AUDIO_ALLOC_BUFFER(paramList.p_buffer, paramList.memory_size);
    } while (1);
    if (paramList.p_buffer == NULL) {
        ALOGE("paramList.p_buffer alloc Fail!!");
        return;
    }

    /* send config */
    configAndParam = (uint8_t *)AUDIO_MALLOC(sizeof(struct aurisys_dsp_config_t) + paramList.data_size);
    if (configAndParam == NULL) {
        ALOGE("configAndParam alloc Fail!!");
        return;
    }
    memcpy(configAndParam, mAurisysDspConfigCallFinal, sizeof(struct aurisys_dsp_config_t));
    memcpy(configAndParam + sizeof(struct aurisys_dsp_config_t), paramList.p_buffer, paramList.data_size);

    retval = AudioMessengerIPI::getInstance()->sendIpiMsg(
                 &msg,
                 TASK_SCENE_CALL_FINAL, AUDIO_IPI_LAYER_TO_DSP,
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

    snprintf(file_path_final_in, 100, "%s%s%s", DUMP_DSP_PCM_DATA_PATH, string_time, "final_in.pcm");
    snprintf(file_path_final_out, 100, "%s%s%s", DUMP_DSP_PCM_DATA_PATH, string_time, "final_out.pcm");
    snprintf(file_path_final_iv, 100, "%s%s%s", DUMP_DSP_PCM_DATA_PATH, string_time, "ivdump.pcm");
    fp_pcm_dump_final_in = AudioOpendumpPCMFile(file_path_final_in, PROPERTY_KEY_PCM_DUMP_ON);
    fp_pcm_dump_final_out = AudioOpendumpPCMFile(file_path_final_out, PROPERTY_KEY_PCM_DUMP_ON);
    fp_pcm_dump_final_iv = AudioOpendumpPCMFile(file_path_final_iv, PROPERTY_KEY_PCM_DUMP_ON);
    retval = AudioMessengerIPI::getInstance()->sendIpiMsg(
                 &msg,
                 TASK_SCENE_CALL_FINAL, AUDIO_IPI_LAYER_TO_DSP,
                 AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
                 AUDIO_DSP_TASK_PCMDUMP_ON, (get_uint32_from_property(PROPERTY_KEY_PCM_DUMP_ON) != 0), 0,
                 NULL);

    ALOGD("%s(-)", __FUNCTION__);
}

void AudioDspCallFinal::DestroyAurisysLibManagerCallFinal() {
    struct ipi_msg_t ipi_msg;
    int retval = 0;

    if (!mAurisysLibManagerCallFinal) {
        ALOGD("%s mAurisysLibManagerCallFinal is NULL, no need destroy", __FUNCTION__);
        return;
    }

    ALOGD("%s(+)", __FUNCTION__);

    AudioCloseDumpPCMFile(fp_pcm_dump_final_in);
    AudioCloseDumpPCMFile(fp_pcm_dump_final_out);
    AudioCloseDumpPCMFile(fp_pcm_dump_final_iv);

    retval = AudioMessengerIPI::getInstance()->sendIpiMsg(
                 &ipi_msg,
                 TASK_SCENE_CALL_FINAL,
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

    if (mAurisysLibManagerCallFinal) {
        destroy_aurisys_lib_manager(mAurisysLibManagerCallFinal);
        mAurisysLibManagerCallFinal = NULL;
    }

    AUDIO_FREE_POINTER(mAurisysDspConfigCallFinal);

    AudioMessengerIPI::getInstance()->deregisterDmaCbk(TASK_SCENE_CALL_FINAL);
    ALOGD("%s(-)", __FUNCTION__);
}

void AudioDspCallFinal::initCallFinalTask(const audio_devices_t input_device,
                                          const audio_devices_t output_device,
                                          uint16_t sample_rate, int mdIdx) {
    int PcmDlInIdx = 0;
    int PcmDlOutIdx = 0;
    int PcmIvIdx = 0;
    int PcmDspIdx = 0;
    int CardIndex = 0;

    ALOGD("%s(+), mOutputDevice: %d", __FUNCTION__, mOutputDevice);

    mOutputDevice = output_device;
    mInputDevice = input_device;
    mSampleRate = sample_rate;

    AudioMessengerIPI::getInstance()->registerAdspFeature(CALL_FINAL_FEATURE_ID);
    CreateAurisysLibManagerCallFinal();

    PcmDlOutIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback1);
    PcmDlInIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture6);
    PcmIvIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCapture4);
    PcmDspIdx = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmCallfinalDsp);
    CardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback1);

    mUlTurnOnSeq = (mdIdx == MODEM_1) ? AUDIO_CTL_MD1_TO_CAPTURE6 : AUDIO_CTL_MD2_TO_CAPTURE6;
    mEchoRefTurnOnSeq = (mdIdx == MODEM_1) ? AUDIO_CTL_DL1_TO_MD1_ECHO_REF : AUDIO_CTL_DL1_TO_MD2_ECHO_REF;
    mDlTurnOnSeq1 = mHardwareResourceManager->getOutputTurnOnSeq(output_device, false, AUDIO_CTL_PLAYBACK1);
    if ((output_device & AUDIO_DEVICE_OUT_SPEAKER) &&
         AudioSmartPaController::getInstance()->isSmartPAUsed() &&
         popcount(output_device) > 1) {
        mDlTurnOnSeq2 = mHardwareResourceManager->getOutputTurnOnSeq(output_device, true, AUDIO_CTL_PLAYBACK1);
    }

    memset(&mConfig, 0, sizeof(mConfig));
    mConfig.channels = 2;
    mConfig.rate = sample_rate;
    mConfig.start_threshold = 0;
    mConfig.stop_threshold = ~(0U);
    mConfig.period_size = 256;
    mConfig.period_count = 4;
    mConfig.start_threshold = mConfig.period_size * mConfig.period_count;
    mConfig.format = PCM_FORMAT_S24_LE;

    memcpy(&mSpkIvConfig, &mConfig, sizeof(struct pcm_config));
    mSpkIvConfig.channels = 2;
    if (mHardwareResourceManager->is32BitI2sSupport()) {
        mSpkIvConfig.format = PCM_FORMAT_S32_LE;
    } else {
        mSpkIvConfig.format = PCM_FORMAT_S24_LE;
    }

    setCallFinalruntime(true);
    setCallFinalRefruntime(true);
    setCallFinalDspShareMem(true);

    mHardwareResourceManager->enableTurnOnSequence(mUlTurnOnSeq);
    mHardwareResourceManager->enableTurnOnSequence(mEchoRefTurnOnSeq);
    mHardwareResourceManager->enableTurnOnSequence(mDlTurnOnSeq1);
    mHardwareResourceManager->enableTurnOnSequence(mDlTurnOnSeq2);

    mPcmDlOut = pcm_open(CardIndex, PcmDlOutIdx, PCM_OUT, &mConfig);
    if (!pcm_is_ready(mPcmDlOut) || (pcm_prepare(mPcmDlOut) != 0)) {
        ALOGE("%s(), dlout pcm(%p) err: %s, close pcm.", __FUNCTION__, mPcmDlOut, pcm_get_error(mPcmDlOut));
        pcm_close(mPcmDlOut);
        mPcmDlOut = NULL;
        ASSERT(0);
    }
    mPcmDlIn = pcm_open(CardIndex, PcmDlInIdx, PCM_IN, &mConfig);
    if (!pcm_is_ready(mPcmDlIn) || (pcm_prepare(mPcmDlIn) != 0)) {
        ALOGE("%s(), dlin pcm(%p) err: %s, close pcm.", __FUNCTION__, mPcmDlIn, pcm_get_error(mPcmDlIn));
        pcm_close(mPcmDlIn);
        mPcmDlIn = NULL;
        ASSERT(0);
    }
    mPcmIv = pcm_open(CardIndex, PcmIvIdx, PCM_IN, &mSpkIvConfig);
    if (!pcm_is_ready(mPcmIv) || (pcm_prepare(mPcmIv) != 0)) {
        ALOGE("%s(), iv pcm(%p) err: %s, close pcm.", __FUNCTION__, mPcmIv, pcm_get_error(mPcmIv));
        pcm_close(mPcmIv);
        mPcmIv = NULL;
        ASSERT(0);
    }
    mPcmDsp = pcm_open(CardIndex, PcmDspIdx, PCM_OUT, &mConfig);
    if (!pcm_is_ready(mPcmDsp) || (pcm_prepare(mPcmDsp) != 0)) {
        ALOGE("%s(), dsp pcm(%p) err: %s, close pcm.", __FUNCTION__, mPcmDsp, pcm_get_error(mPcmDsp));
        pcm_close(mPcmDsp);
        mPcmDsp = NULL;
        ASSERT(0);
    }
}

void AudioDspCallFinal::startCallFinalTask() {

    ALOGD("%s(+) mOutputDevice: %d", __FUNCTION__, mOutputDevice);
    if (mPcmIv != NULL) {
        if (pcm_start(mPcmIv)) {
            ALOGE("%s(), pcm_start mPcmIv %p fail due to %s", __FUNCTION__, mPcmIv, pcm_get_error(mPcmIv));
        }
    }
    if (mPcmDlIn != NULL) {
        if (pcm_start(mPcmDlIn)) {
            ALOGE("%s(), pcm_start mPcmDlIn %p fail due to %s", __FUNCTION__, mPcmDlIn, pcm_get_error(mPcmDlIn));
        }
    }
    if (mPcmDlOut != NULL) {
        if (pcm_start(mPcmDlOut)) {
            ALOGE("%s(), pcm_start mPcmDlOut %p fail due to %s", __FUNCTION__, mPcmDlOut, pcm_get_error(mPcmDlOut));
        }
    }
    if (mPcmDsp != NULL) {
        if (pcm_start(mPcmDsp)) {
            ALOGE("%s(), pcm_start mPcmDsp %p fail due to %s", __FUNCTION__, mPcmDsp, pcm_get_error(mPcmDsp));
        }
    }
}

void AudioDspCallFinal::stopCallFinalTask() {
    ALOGD("%s(+)", __FUNCTION__);

    if (mPcmDsp != NULL) {
        pcm_stop(mPcmDsp);
        pcm_close(mPcmDsp);
        mPcmDsp = NULL;
    }
    if (mPcmDlOut != NULL) {
        pcm_stop(mPcmDlOut);
        pcm_close(mPcmDlOut);
        mPcmDlOut = NULL;
    }
    if (mPcmDlIn != NULL) {
        pcm_stop(mPcmDlIn);
        pcm_close(mPcmDlIn);
        mPcmDlIn = NULL;
    }
    if (mPcmIv != NULL) {
        pcm_stop(mPcmIv);
        pcm_close(mPcmIv);
        mPcmIv = NULL;
    }

    mHardwareResourceManager->disableTurnOnSequence(mUlTurnOnSeq);
    mHardwareResourceManager->disableTurnOnSequence(mEchoRefTurnOnSeq);
    mHardwareResourceManager->disableTurnOnSequence(mDlTurnOnSeq1);
    mHardwareResourceManager->disableTurnOnSequence(mDlTurnOnSeq2);

    ALOGV("%s(-)", __FUNCTION__);
}

void AudioDspCallFinal::deinitCallFinalTask() {
    setCallFinalRefruntime(false);
    setCallFinalruntime(false);
    setCallFinalDspShareMem(false);

    DestroyAurisysLibManagerCallFinal();
    AudioMessengerIPI::getInstance()->deregisterAdspFeature(CALL_FINAL_FEATURE_ID);
}

bool AudioDspCallFinal::isOutputDevCallFinalSupport(int outputDevice) {
    struct mixer_ctl *ctl = NULL;
    unsigned int ret = 0, supportDevice;
    int retValue = false;

    if (!isAdspOptionEnable())
        return false;

    ctl = mixer_get_ctl_by_name(mMixer, "dsp_call_final_default_en");
    if (ctl == NULL) {
        return false;
    }

    ret = mixer_ctl_get_value(ctl, 0);

    supportDevice = ret >> 1;
    if ((ret & AUD_TASK_DEFAULT_ENABLE) && (supportDevice & outputDevice)) {
        retValue = true;
    }

    return retValue;
}

} /* end of namespace android */
