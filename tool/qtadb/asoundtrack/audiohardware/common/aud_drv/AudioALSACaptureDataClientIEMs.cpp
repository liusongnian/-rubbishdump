#include <AudioALSACaptureDataClientIEMs.h>

#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <inttypes.h>
#include <pthread.h>

#include <log/log.h>

#include <AudioAssert.h>

#include <AudioType.h>

#include <audio_ringbuf.h>

#include <audio_fmt_conv_hal.h>

#include <AudioUtility.h>
#include <SpeechUtility.h>

#include <AudioALSADeviceParser.h>
#include <AudioCustParamClient.h>
#include <AudioVolumeInterface.h>
#include <AudioVolumeFactory.h>

#include <AudioALSAHardwareResourceManager.h>


#include <AudioALSACaptureDataProviderBase.h>

#include <WCNChipController.h>
#include <AudioALSACaptureDataProviderBTSCO.h>
#include <AudioALSACaptureDataProviderBTCVSD.h>

#if defined(PRIMARY_USB)
#include <AudioALSACaptureDataProviderUsb.h>
#endif

#include <AudioALSACaptureDataProviderNormal.h>


#include <aurisys_scenario.h>
#include <arsi_type.h>
#include <aurisys_config.h>

#include <audio_pool_buf_handler.h>

#include <aurisys_utility.h>
#include <aurisys_controller.h>
#include <aurisys_lib_manager.h>



#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSACaptureDataClientIEMs"


namespace android {


/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */

#ifndef UPLINK_DROP_POP_MS
#define DROP_POP_MS (50)
#else
#define DROP_POP_MS (UPLINK_DROP_POP_MS)
#endif

#ifndef UPLINK_DROP_POP_MS_FOR_UNPROCESSED
#define DROP_POP_MS_FOR_UNPROCESSED (80)
#else
#define DROP_POP_MS_FOR_UNPROCESSED (UPLINK_DROP_POP_MS_FOR_UNPROCESSED)
#endif

#define MAX_LOW_LATENCY_US (5333)
#define MAX_WAIT_UL_DATA_TO_DL_MS (200)

#define IEMS_EFFECT_FRAME_SIZE_MS (5)


//#define USE_SRC_TO_REPLACE_LIB



/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */


/*
 * =============================================================================
 *                     private global members
 * =============================================================================
 */



/*
 * =============================================================================
 *                     Implementation
 * =============================================================================
 */

static uint32_t getDropMs(const stream_attribute_t *attr) {
    if (audio_is_usb_in_device(attr->input_device)) {
        return 0;
    }

    if (attr->input_source == AUDIO_SOURCE_UNPROCESSED) {
        return DROP_POP_MS_FOR_UNPROCESSED;
    } else {
        return DROP_POP_MS;
    }
}


AudioALSACaptureDataClientIEMs *AudioALSACaptureDataClientIEMs::mAudioALSACaptureDataClientIEMs = NULL;

AudioALSACaptureDataClientIEMs *AudioALSACaptureDataClientIEMs::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSACaptureDataClientIEMs == NULL) {
        mAudioALSACaptureDataClientIEMs = new AudioALSACaptureDataClientIEMs();
    }
    ASSERT(mAudioALSACaptureDataClientIEMs != NULL);
    return mAudioALSACaptureDataClientIEMs;
}


AudioALSACaptureDataClientIEMs::AudioALSACaptureDataClientIEMs() :
    mAttrIEMsIn(NULL),
    mAttrDataProvider(NULL),
    mCaptureDataProvider(NULL),
    mCaptureDataProviderType(CAPTURE_PROVIDER_BASE),
    mRawStartFrameCount(0),
    mAudioALSAVolumeController(AudioVolumeFactory::CreateAudioVolumeController()),
    mEnable(false),
    hProcessThread(0),
    mProcessThreadLaunched(false),
    mProcessThreadWaitSync(false),
    mPeriodUs(0),
    mRawDataPeriodBufSize(0),
    mDropPopSize(0),
    mWriteCbk(NULL),
    mWriteArg(NULL),
    mReadCbk(NULL),
    mReadArg(NULL),
    mFmtConvHdl(NULL),
    mAurisysLibManager(NULL),
    mManagerConfig(NULL),
    mAurisysScenario(AURISYS_SCENARIO_INVALID),
    mAudioPoolBufUlIn(NULL),
    mAudioPoolBufUlOut(NULL),
    mLinearOut(NULL) {
    memset((void *)&mRawDataBuf, 0, sizeof(mRawDataBuf));
}


AudioALSACaptureDataClientIEMs::~AudioALSACaptureDataClientIEMs() {

}


AudioALSACaptureDataProviderBase *AudioALSACaptureDataClientIEMs::getDataProvider(const audio_devices_t input_device) {
    AudioALSACaptureDataProviderBase *pCaptureDataProvider = NULL;

    /* get data provider singleton for mic source */
    if (audio_is_bluetooth_in_sco_device(input_device)) {
        // open BT  data provider
        if (WCNChipController::GetInstance()->IsBTMergeInterfaceSupported() == true) {
            pCaptureDataProvider = AudioALSACaptureDataProviderBTSCO::getInstance();
        } else {
            pCaptureDataProvider = AudioALSACaptureDataProviderBTCVSD::getInstance();
        }
#if defined(PRIMARY_USB)
    } else if (audio_is_usb_in_device(input_device)) {
        pCaptureDataProvider = AudioALSACaptureDataProviderUsb::getInstance();
#endif
    } else {
        /* if adsp enabled, just run IEMs on adsp.. :P */
        pCaptureDataProvider = AudioALSACaptureDataProviderNormal::getInstance();
    }


    return pCaptureDataProvider;
}


int AudioALSACaptureDataClientIEMs::open(stream_attribute_t *attrIEMsIn,
                                         IEMsCbk writeCbk,
                                         void *writeArg) {
    uint32_t drop_us = 0;
    uint32_t size_per_frame = 0;
    int ret = 0;

    if (!attrIEMsIn) {
        return -1;
    }

    AL_AUTOLOCK(mLock);

    if (mEnable) {
        return 0;
    }
    mEnable = true;

    ALOGD("%s(+), mAttrIEMsIn: in_dev 0x%x out_dev 0x%x rate %d fmt %d ch %d flags 0x%x",
          __FUNCTION__,
          attrIEMsIn->input_device,
          attrIEMsIn->output_devices,
          attrIEMsIn->sample_rate,
          attrIEMsIn->audio_format,
          attrIEMsIn->num_channels,
          attrIEMsIn->mAudioInputFlags);

    mAttrIEMsIn = attrIEMsIn;
    mWriteCbk = writeCbk;
    mWriteArg = writeArg;

    // open data provider
    mCaptureDataProvider = getDataProvider(mAttrIEMsIn->input_device);
    mCaptureDataProvider->configStreamAttribute(mAttrIEMsIn);
    mAttrDataProvider = mCaptureDataProvider->getStreamAttributeSource();

    mCaptureDataProvider->attach(this);
    if (audio_is_usb_in_device(mAttrDataProvider->input_device) &&
        mCaptureDataProvider->getPcmStatus() != NO_ERROR) {
        ALOGW("%s, PCM Open/Read Fail...USB Device is unplugged ?", __FUNCTION__);
        mCaptureDataProvider->detach(this);
        mEnable = false;
        return -1;
    }

    mPeriodUs = mCaptureDataProvider->getPeriodUs();
    mRawDataPeriodBufSize = getPeriodBufSizeByUs(mAttrDataProvider, mPeriodUs);


    // Gain control
    if (mAudioALSAVolumeController != NULL) {
        mAudioALSAVolumeController->SetCaptureGain(attrIEMsIn->audio_mode,
                                                   attrIEMsIn->input_source,
                                                   attrIEMsIn->input_device,
                                                   attrIEMsIn->output_devices);
    }

    // create lib manager
#if defined(USE_SRC_TO_REPLACE_LIB)
    mFmtConvHdl = createFmtConvHdlWrap(mAttrDataProvider, mAttrIEMsIn);
#else
    AUDIO_ALLOC_STRUCT(struct data_buf_t, mLinearOut);
    CreateAurisysLibManager();
#endif


    // depop
    drop_us = getDropMs(mAttrIEMsIn) * 1000;
    if (drop_us) {
        if ((drop_us % mPeriodUs) != 0) { // drop data size need to align interrupt rate
            drop_us = ((drop_us / mPeriodUs) + 1) * mPeriodUs; // cell()
        }
        mDropPopSize = getPeriodBufSizeByUs(mAttrIEMsIn, drop_us);
    }


    // processThread
    hProcessThread = 0;
    mProcessThreadWaitSync = true;
    ret = pthread_create(&hProcessThread, NULL,
                         AudioALSACaptureDataClientIEMs::processThread,
                         (void *)this);
    ASSERT(ret == 0);

    AL_LOCK(mProcessThreadSyncLock);
    ret = AL_WAIT_MS(mProcessThreadSyncLock, MAX_WAIT_UL_DATA_TO_DL_MS);
    if (ret != 0) {
        ALOGW("%s(), wait UL->DL write timeout", __FUNCTION__);
    }
    AL_UNLOCK(mProcessThreadSyncLock);

    ALOGD("%s(-)", __FUNCTION__);

    return ret;
}


int AudioALSACaptureDataClientIEMs::close(void) {
    AL_AUTOLOCK(mLock);

    if (!mEnable) {
        return 0;
    }
    mEnable = false;

    // detach client to capture data provider
    mCaptureDataProvider->detach(this);
    mCaptureDataProvider = NULL;

    // terminate processThread thread
    AL_LOCK(mRawDataBufLock);
    AL_SIGNAL(mRawDataBufLock);
    AL_UNLOCK(mRawDataBufLock);

    pthread_join(hProcessThread, NULL);
    ALOGD("pthread_join hProcessThread done");

#if defined(USE_SRC_TO_REPLACE_LIB)
    if (mFmtConvHdl) {
        aud_fmt_conv_hal_destroy(mFmtConvHdl);
        mFmtConvHdl = NULL;
    }
#else
    DestroyAurisysLibManager();
    AUDIO_FREE_POINTER(mLinearOut);
#endif

    AUDIO_FREE_POINTER(mRawDataBuf.base);

    return 0;
}


uint32_t AudioALSACaptureDataClientIEMs::copyCaptureDataToClient(RingBuf pcm_read_buf) {
    audio_ringbuf_t pcm_read_buf_wrap;
    uint32_t data_count_from = 0;
    uint32_t data_count_to = 0;

    int ret = 0;

    if (mProcessThreadLaunched == false) {
        ALOGD("%s(), mProcessThreadLaunched == false. return", __FUNCTION__);
        return 0;
    }

    pcm_read_buf_wrap.base = pcm_read_buf.pBufBase;
    pcm_read_buf_wrap.read = pcm_read_buf.pRead;
    pcm_read_buf_wrap.write = pcm_read_buf.pWrite;
    pcm_read_buf_wrap.size = pcm_read_buf.bufLen;

#ifdef MTK_LATENCY_DETECT_PULSE
    AudioDetectPulse::doDetectPulse(
        TAG_CAPTURE_DATA_CLIENT5, PULSE_LEVEL, 0,
        (void *)pcm_read_buf_wrap.base,
        pcm_read_buf.bufLen - 1,
        mAttrIEMsIn->audio_format,
        mAttrIEMsIn->num_channels,
        mAttrIEMsIn->sample_rate);
#endif

    AL_LOCK(mRawDataBufLock);
    if (mEnable == true) {
        data_count_from = audio_ringbuf_count(&pcm_read_buf_wrap);
        data_count_to = audio_ringbuf_count(&mRawDataBuf);
        if (data_count_to > 8 * mRawDataPeriodBufSize) { /* too much data in ring buf... drop !! */
            ALOGW("%s(), data_count_to %u > 8 * mRawDataPeriodBufSize %u!! drop!!",
                  __FUNCTION__, data_count_to, mRawDataPeriodBufSize);
            audio_ringbuf_drop_data(&mRawDataBuf, data_count_from);
        }

        audio_ringbuf_copy_from_ringbuf_all(&mRawDataBuf, &pcm_read_buf_wrap);
        AL_SIGNAL(mRawDataBufLock);
    }
    AL_UNLOCK(mRawDataBufLock);

    return 0;
}


void *AudioALSACaptureDataClientIEMs::processThread(void *arg) {
    char thread_name[128] = {0};

    AudioALSACaptureDataClientIEMs *client = NULL;

    audio_ringbuf_t *raw_rb = NULL;
    uint32_t raw_rb_sz = 0;

    char *buf_raw = NULL;
    uint32_t buf_raw_sz = 0;

    uint32_t buf_lib_sz = 0;
    void *buf_lib = NULL;


    client = static_cast<AudioALSACaptureDataClientIEMs *>(arg);


    /* thread prior */
    CONFIG_THREAD(thread_name, ANDROID_PRIORITY_AUDIO);
    audio_sched_setschedule(0, SCHED_RR, sched_get_priority_min(SCHED_RR));


    /* process thread created */
    client->mProcessThreadLaunched = true;

    /* get buffer address */
    raw_rb = &client->mRawDataBuf;

    buf_raw = (char *)malloc(client->mRawDataPeriodBufSize);
    if (!buf_raw) {
        ALOGE("buf_raw malloc fail!!");
        goto THREAD_EXIT;
    }
    buf_raw_sz = client->mRawDataPeriodBufSize;

    while (client->mEnable == true) {
        // get data from raw_rb buffer
        AL_LOCK(client->mRawDataBufLock);

        raw_rb_sz = audio_ringbuf_count(raw_rb);
        if (raw_rb_sz < buf_raw_sz) {
            AL_WAIT_NO_TIMEOUT(client->mRawDataBufLock);
            if (client->mEnable == false) {
                AL_UNLOCK(client->mRawDataBufLock);
                break;
            }

            raw_rb_sz = audio_ringbuf_count(raw_rb);
            if (raw_rb_sz < buf_raw_sz) {
                AL_UNLOCK(client->mRawDataBufLock);
                ALOGW("raw_rb_sz %u < buf_raw_sz %u",
                      raw_rb_sz, buf_raw_sz);
                usleep(client->mPeriodUs);
                continue;
            }
        }

        audio_ringbuf_copy_to_linear(buf_raw, raw_rb, buf_raw_sz);

        AL_UNLOCK(client->mRawDataBufLock);

#ifdef MTK_LATENCY_DETECT_PULSE
        AudioDetectPulse::doDetectPulse(
            TAG_CAPTURE_DATA_CLIENT4, PULSE_LEVEL, 0,
            (void *)buf_raw,
            buf_raw_sz,
            client->mAttrDataProvider->audio_format,
            client->mAttrDataProvider->num_channels,
            client->mAttrDataProvider->sample_rate);
#endif


#if defined(USE_SRC_TO_REPLACE_LIB)
        // SRC
        aud_fmt_conv_hal_process(
            buf_raw, buf_raw_sz,
            &buf_lib, &buf_lib_sz,
            client->mFmtConvHdl);
#else
        // Lib
        audio_pool_buf_copy_from_linear(
            client->mAudioPoolBufUlIn,
            buf_raw,
            buf_raw_sz);

        aurisys_process_ul_only(
            client->mAurisysLibManager,
            client->mAudioPoolBufUlIn,
            client->mAudioPoolBufUlOut,
            NULL); // NO AEC for IEMs

        buf_lib_sz = audio_ringbuf_count(&client->mAudioPoolBufUlOut->ringbuf);
        audio_pool_buf_copy_to_linear(
            &client->mLinearOut->p_buffer,
            &client->mLinearOut->memory_size,
            client->mAudioPoolBufUlOut,
            buf_lib_sz);

        buf_lib = client->mLinearOut->p_buffer;
#endif

#ifdef MTK_LATENCY_DETECT_PULSE
        AudioDetectPulse::doDetectPulse(
            TAG_CAPTURE_DATA_CLIENT3, PULSE_LEVEL, 0,
            (void *)buf_lib,
            buf_lib_sz,
            client->mAttrIEMsIn->audio_format,
            client->mAttrIEMsIn->num_channels,
            client->mAttrIEMsIn->sample_rate);
#endif

        // depop
        if (client->mDropPopSize > 0) {
            if (buf_lib_sz >= client->mDropPopSize) {
                client->mDropPopSize = 0;
            } else {
                client->mDropPopSize -= buf_lib_sz;
            }
            continue;
        }

        // write to Rx
        if (client->mWriteCbk) {
            client->mWriteCbk(buf_lib, buf_lib_sz, client->mWriteArg);
        }
        if (client->mProcessThreadWaitSync) {
            client->mProcessThreadWaitSync = false;
            AL_LOCK(client->mProcessThreadSyncLock);
            AL_SIGNAL(client->mProcessThreadSyncLock);
            AL_UNLOCK(client->mProcessThreadSyncLock);
        }

        // read for Tx
        AL_LOCK(client->mhookCaptureHandlerLock);
        if (client->mReadCbk) {
            client->mReadCbk(buf_lib, buf_lib_sz, client->mReadArg);
        }
        AL_UNLOCK(client->mhookCaptureHandlerLock);

    }

    free(buf_raw);
    buf_raw = NULL;
    buf_raw_sz = 0;

THREAD_EXIT:
    client->mProcessThreadLaunched = false;
    ALOGD("%s terminated", thread_name);

    return NULL;
}


int AudioALSACaptureDataClientIEMs::hookCaptureHandler(IEMsCbk readCbk, void *readArg) {
    AL_AUTOLOCK(mhookCaptureHandlerLock);

    mReadCbk = readCbk;
    mReadArg = readArg;

    return 0;
}


int AudioALSACaptureDataClientIEMs::unhookCaptureHandler(void) {
    AL_AUTOLOCK(mhookCaptureHandlerLock);

    mReadCbk = NULL;
    mReadArg = NULL;

    return 0;
}


bool AudioALSACaptureDataClientIEMs::IsLowLatencyCapture(void) {
    bool low_latency_on = false;

#ifdef UPLINK_LOW_LATENCY
    if (mAttrIEMsIn->mAudioInputFlags & AUDIO_INPUT_FLAG_FAST ||
        mPeriodUs <= MAX_LOW_LATENCY_US) {
        low_latency_on = true;
    }
#endif

    return low_latency_on;
}


/*
 * =============================================================================
 *                     Aurisys Framework 2.0
 * =============================================================================
 */

void AudioALSACaptureDataClientIEMs::CreateAurisysLibManager() {
    uint8_t frame_size_ms = 0;

    /* scenario & sample rate */
    mAurisysScenario = AURISYS_SCENARIO_RECORD_IEM;

    /* manager config */
    AUDIO_ALLOC_STRUCT(struct aurisys_lib_manager_config_t, mManagerConfig);
    if (mManagerConfig == NULL) {
        ALOGE("%s(), alloc mManagerConfig fail!", __FUNCTION__);
        return;
    }

    mManagerConfig->aurisys_scenario = mAurisysScenario;
    mManagerConfig->arsi_process_type = ARSI_PROCESS_TYPE_UL_ONLY;
    mManagerConfig->audio_format = mAttrDataProvider->audio_format;
    mManagerConfig->sample_rate = mAttrDataProvider->sample_rate;
    frame_size_ms = mAttrDataProvider->periodUs / 1000;
    if ((frame_size_ms % IEMS_EFFECT_FRAME_SIZE_MS) == 0) {
        mManagerConfig->frame_size_ms = IEMS_EFFECT_FRAME_SIZE_MS;
    } else {
        mManagerConfig->frame_size_ms = frame_size_ms;
    }
    mManagerConfig->num_channels_ul = mAttrDataProvider->num_channels;
    mManagerConfig->num_channels_dl = 2;
    mManagerConfig->core_type = AURISYS_CORE_HAL;


    ALOGD("%s(), input_source: %d, flag: 0x%x => mAurisysScenario: %u",
          __FUNCTION__,
          mAttrIEMsIn->input_source,
          mAttrIEMsIn->mAudioInputFlags,
          mAurisysScenario);

    /* task config */
    InitArsiTaskConfig(mManagerConfig);

    /* create manager */
    mAurisysLibManager = create_aurisys_lib_manager(mManagerConfig);
    InitBufferConfig(mAurisysLibManager);

    aurisys_parsing_param_file(mAurisysLibManager);
    aurisys_create_arsi_handlers(mAurisysLibManager); /* should init task/buf configs first */
    aurisys_pool_buf_formatter_init(mAurisysLibManager); /* should init task/buf configs first */

    if (mAudioALSAVolumeController != NULL) {
        int16_t ulDigitalGain = mAudioALSAVolumeController->GetSWMICGain() << 2; // (unit: 0.25 db)
        int16_t ulAnalogGain = ((mAudioALSAVolumeController->GetULTotalGain() - 192) / 4 + 34 - mAudioALSAVolumeController->GetSWMICGain()) << 2; // (unit: 0.25 db)
        if (audio_is_bluetooth_in_sco_device(mAttrIEMsIn->input_device)) {
            ulDigitalGain = 0;
            ulAnalogGain = 0;
            ALOGD("BT path set Digital MIC gain = 0");
        } else if (audio_is_usb_in_device(mAttrIEMsIn->input_device)) {
            ulAnalogGain = 0;
            ALOGD("USB path set analog MIC gain = 0");
        }
        aurisys_set_ul_digital_gain(mAurisysLibManager, ulAnalogGain, ulDigitalGain);
        //aurisys_set_dl_digital_gain(mAurisysLibManager, 0, 0);
    }
}


/* TODO: move to aurisys framework?? add a new struct to keep hal arributes */
void AudioALSACaptureDataClientIEMs::InitArsiTaskConfig(
    struct aurisys_lib_manager_config_t *pManagerConfig) {
    struct arsi_task_config_t *pTaskConfig = &pManagerConfig->task_config;

    /* input device */
    pTaskConfig->input_device_info.devices = mAttrIEMsIn->input_device; /* TODO: HAL capture */
    pTaskConfig->input_device_info.audio_format = mAttrDataProvider->audio_format;
    pTaskConfig->input_device_info.sample_rate = mAttrDataProvider->sample_rate;
    pTaskConfig->input_device_info.channel_mask = mAttrDataProvider->audio_channel_mask; /* TODO */
    pTaskConfig->input_device_info.num_channels = mAttrDataProvider->num_channels;
    pTaskConfig->input_device_info.hw_info_mask = 0; /* TODO */

    /* output device (no DL path for record) */
    pTaskConfig->output_device_info.devices = mAttrIEMsIn->output_devices;
    pTaskConfig->output_device_info.audio_format = AUDIO_FORMAT_DEFAULT;
    pTaskConfig->output_device_info.sample_rate = 0;
    pTaskConfig->output_device_info.channel_mask = AUDIO_CHANNEL_NONE;
    pTaskConfig->output_device_info.num_channels = 0;
    pTaskConfig->output_device_info.hw_info_mask = 0;

    /* task scene */
    pTaskConfig->task_scene = map_aurisys_scenario_to_task_scene(
                                  pManagerConfig->core_type,
                                  pManagerConfig->aurisys_scenario);

    /* audio mode */
    pTaskConfig->audio_mode = mAttrIEMsIn->audio_mode;

    /* max device capability for allocating memory */
    pTaskConfig->max_input_device_sample_rate  = 48000; /* TODO */
    pTaskConfig->max_output_device_sample_rate = 48000; /* TODO */

    pTaskConfig->max_input_device_num_channels =
        AudioALSAHardwareResourceManager::getInstance()->getNumPhoneMicSupport();
    pTaskConfig->max_output_device_num_channels = 2; /* TODO */

    /* flag & source */
    pTaskConfig->output_flags = 0;
    pTaskConfig->input_source = mAttrIEMsIn->input_source;
    pTaskConfig->input_flags  = mAttrIEMsIn->mAudioInputFlags;

    dump_task_config(pTaskConfig);
}


void AudioALSACaptureDataClientIEMs::InitBufferConfig(struct aurisys_lib_manager_t *manager) {
    /* UL in */
    mAudioPoolBufUlIn = create_audio_pool_buf(manager, DATA_BUF_UPLINK_IN, 0);

    if (mAudioPoolBufUlIn != NULL) {
        mAudioPoolBufUlIn->buf->b_interleave = 1; /* LRLRLRLR*/
        mAudioPoolBufUlIn->buf->frame_size_ms = 0;
        mAudioPoolBufUlIn->buf->num_channels = mAttrDataProvider->num_channels;
        mAudioPoolBufUlIn->buf->sample_rate_buffer = mAttrDataProvider->sample_rate;
        mAudioPoolBufUlIn->buf->sample_rate_content = mAttrDataProvider->sample_rate;
        mAudioPoolBufUlIn->buf->audio_format = mAttrDataProvider->audio_format;
    } else {
        ALOGE("%s(), create pool buf in fail!", __FUNCTION__);
    }

    /* UL out */
    mAudioPoolBufUlOut = create_audio_pool_buf(manager, DATA_BUF_UPLINK_OUT, 0);

    if (mAudioPoolBufUlOut != NULL) {
        mAudioPoolBufUlOut->buf->b_interleave = 1; /* LRLRLRLR*/
        mAudioPoolBufUlOut->buf->frame_size_ms = 0;
        mAudioPoolBufUlOut->buf->num_channels = mAttrIEMsIn->num_channels;
        mAudioPoolBufUlOut->buf->sample_rate_buffer = mAttrIEMsIn->sample_rate;
        mAudioPoolBufUlOut->buf->sample_rate_content = mAttrIEMsIn->sample_rate;
        mAudioPoolBufUlOut->buf->audio_format = mAttrIEMsIn->audio_format;
    } else {
        ALOGE("%s(), create pool buf out fail!", __FUNCTION__);
    }
}


void AudioALSACaptureDataClientIEMs::DestroyAurisysLibManager() {
    ALOGD("%s()", __FUNCTION__);

    if (mAurisysLibManager) {
        aurisys_destroy_arsi_handlers(mAurisysLibManager);
        aurisys_pool_buf_formatter_deinit(mAurisysLibManager);
        destroy_aurisys_lib_manager(mAurisysLibManager);
        mAurisysLibManager = NULL;

        /* NOTE: auto destroy audio_pool_buf when destroy_aurisys_lib_manager() */
        mAudioPoolBufUlIn = NULL;
        mAudioPoolBufUlOut = NULL;
    }

    if (mLinearOut) {
        AUDIO_FREE_POINTER(mLinearOut->p_buffer);
        memset(mLinearOut, 0, sizeof(data_buf_t));
    }

    if (mManagerConfig) {
        AUDIO_FREE_POINTER(mManagerConfig);
    }
}


void AudioALSACaptureDataClientIEMs::setRawStartFrameCount(int64_t frameCount) {
    if (!mAttrIEMsIn || !mAttrDataProvider) {
        return;
    }
    mRawStartFrameCount = frameCount * mAttrIEMsIn->sample_rate / mAttrDataProvider->sample_rate;
}


int AudioALSACaptureDataClientIEMs::getCapturePosition(int64_t *frames, int64_t *time) {
    int ret = 0;

    if (!frames || !time) {
        return -EINVAL;
    }
    if (!mCaptureDataProvider || !mAttrIEMsIn || !mAttrDataProvider) {
        return -ENODEV;
    }

    ret = mCaptureDataProvider->getCapturePosition(frames, time);
    if (ret == 0) {
        *frames = (*frames) * mAttrIEMsIn->sample_rate / mAttrDataProvider->sample_rate;
    }

    ALOGV("%s(), frames = %u", __FUNCTION__, (uint32_t)(*frames & 0xFFFFFFFF));
    return ret;
}



} /* end of namespace android */

