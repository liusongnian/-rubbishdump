#include <AudioMessengerIPI.h>

#include <system/audio.h>
#include <log/log.h>

#include <AudioAssert.h>

#include "AudioUtility.h"

#include <audio_dsp_service.h>


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioMessengerIPI"



unsigned int getDspFeatureID(const uint16_t flag) {
    if (!android::isAdspServiceEnable()) {
        ALOGD("%s() not support!! 0x%x", __FUNCTION__, flag);
        return 0;
    }

    switch (flag) {
    case (AUDIO_OUTPUT_FLAG_DEEP_BUFFER):
        return DEEPBUF_FEATURE_ID;
    case (AUDIO_OUTPUT_FLAG_PRIMARY | AUDIO_OUTPUT_FLAG_FAST):
    case (AUDIO_OUTPUT_FLAG_DEEP_BUFFER | AUDIO_OUTPUT_FLAG_PRIMARY):
    case (AUDIO_OUTPUT_FLAG_PRIMARY):
        return PRIMARY_FEATURE_ID;
    case (AUDIO_OUTPUT_FLAG_VOIP_RX):
        return VOIP_FEATURE_ID;
    case (AUDIO_OUTPUT_FLAG_FAST | AUDIO_OUTPUT_FLAG_RAW):
    case (AUDIO_OUTPUT_FLAG_RAW):
    case (AUDIO_OUTPUT_FLAG_FAST):
        return FAST_FEATURE_ID;
    case (AUDIO_OUTPUT_FLAG_COMPRESS_OFFLOAD):
        return OFFLOAD_FEATURE_ID;
    default:
        ALOGE("%s: no support flag %d, use PRIMARY_FEATURE_ID instead", __FUNCTION__, flag);
        return PRIMARY_FEATURE_ID;
    }
}

namespace android {

AudioMessengerIPI *AudioMessengerIPI::mAudioMessengerIPI = NULL;
AudioMessengerIPI *AudioMessengerIPI::getInstance() {
    if (!isAudioIpiEnable()) {
        return NULL;
    }

    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioMessengerIPI == NULL) {
        mAudioMessengerIPI = new AudioMessengerIPI();
    }
    ASSERT(mAudioMessengerIPI != NULL);
    return mAudioMessengerIPI;
}


AudioMessengerIPI::AudioMessengerIPI() {
    bool ipiDmaEn = isAudioIpiDmaEnable() ? true : false;
    bool adspRecoveryEn = isAdspRecoveryEnable() ? true : false;
    bool adspServiceEn = isAdspServiceEnable() ? true : false;

    audio_messenger_ipi_init(ipiDmaEn, adspRecoveryEn, adspServiceEn);
}


AudioMessengerIPI::~AudioMessengerIPI() {
    bool ipiDmaEn = isAudioIpiDmaEnable() ? true : false;
    bool adspRecoveryEn = isAdspRecoveryEnable() ? true : false;
    bool adspServiceEn = isAdspServiceEnable() ? true : false;

    audio_messenger_ipi_deinit(ipiDmaEn, adspRecoveryEn, adspServiceEn);
}


status_t AudioMessengerIPI::sendIpiMsg(
    struct ipi_msg_t *p_ipi_msg,
    uint8_t task_scene,
    uint8_t target_layer,
    uint8_t data_type,
    uint8_t ack_type,
    uint16_t msg_id,
    uint32_t param1,
    uint32_t param2,
    void    *data_buffer) {
    return audio_send_ipi_msg(
               p_ipi_msg,
               task_scene,
               target_layer,
               data_type,
               ack_type,
               msg_id,
               param1,
               param2,
               data_buffer);
}


void AudioMessengerIPI::registerDmaCbk(
    const uint8_t task_scene,
    const uint32_t a2dSize,
    const uint32_t d2aSize,
    audio_ipi_dma_cbk_t cbk,
    void *arg) {
    if (isAudioIpiDmaEnable()) {
        audio_ipi_dma_cbk_register(
            task_scene,
            a2dSize,
            d2aSize,
            cbk,
            arg);
    }
}


void AudioMessengerIPI::deregisterDmaCbk(const uint8_t task_scene) {
    if (isAudioIpiDmaEnable()) {
        audio_ipi_dma_cbk_deregister(task_scene);
    }
}


void AudioMessengerIPI::registerAdspFeature(const uint16_t feature_id) {
    if (isAdspServiceEnable()) {
        adsp_register_feature(feature_id);
    }
}


void AudioMessengerIPI::deregisterAdspFeature(const uint16_t feature_id) {
    if (isAdspServiceEnable()) {
        adsp_deregister_feature(feature_id);
    }
}


} // end of namespace android
