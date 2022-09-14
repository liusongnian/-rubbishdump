#include <SpeechPcmMixerTelephonyTx.h>
#include <SpeechDriverInterface.h>
#include "SpeechUtility.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SpeechPcmMixerTelephonyTx"

namespace android {

static const char *PROPERTY_KEY_TELEPHOPNY_TX_TYPE = "vendor.audiohal.telephonytx.type"; // 0 for property disable, so need to set enum value +1

SpeechPcmMixerTelephonyTx *SpeechPcmMixerTelephonyTx::mTelephonyTx = NULL;
SpeechPcmMixerTelephonyTx *SpeechPcmMixerTelephonyTx::GetInstance() {
    static Mutex mGetInstanceLock;
    Mutex::Autolock _l(mGetInstanceLock);

    if (mTelephonyTx == NULL) {
        mTelephonyTx = new SpeechPcmMixerTelephonyTx();
    }
    ASSERT(mTelephonyTx != NULL);
    return mTelephonyTx;
}

SpeechPcmMixerTelephonyTx::SpeechPcmMixerTelephonyTx() {
    mPcmTelephonyTxType = PCM_TELEPHONY_TX_TYPE_REPLACE;

    uint16_t pcmTelephonyTxType = get_uint32_from_property(PROPERTY_KEY_TELEPHOPNY_TX_TYPE);
    if (pcmTelephonyTxType > 0) {
        mPcmTelephonyTxType = pcmTelephonyTxType - 1;
    }
}

status_t SpeechPcmMixerTelephonyTx::pcmMixerOn(SpeechDriverInterface *pSpeechDriver) {
    if (mPcmTelephonyTxType >= PCM_TELEPHONY_TX_TYPE_MAX) {
        ALOGD("%s(),  Wrong mPcmTelephonyTxType: %d, use default : %d\n",
              __FUNCTION__, mPcmTelephonyTxType, PCM_TELEPHONY_TX_TYPE_REPLACE);
        mPcmTelephonyTxType = PCM_TELEPHONY_TX_TYPE_REPLACE;
    }
    ALOGD("%s(), mPcmTelephonyTxType: %d\n", __FUNCTION__, mPcmTelephonyTxType);
    pSpeechDriver->TelephonyTxOn(mPcmTelephonyTxType);
    return NO_ERROR;
}

status_t SpeechPcmMixerTelephonyTx::pcmMixerOff(SpeechDriverInterface *pSpeechDriver) {
    ALOGD("%s(),", __FUNCTION__);
    pSpeechDriver->TelephonyTxOff();
    return NO_ERROR;
}

void SpeechPcmMixerTelephonyTx::setTelephonyTxType(uint16_t pcmTelephonyTxType) {
    ALOGD("%s(), pcmTelephonyTxType: %d -> %d\n", __FUNCTION__, mPcmTelephonyTxType, pcmTelephonyTxType);
    mPcmTelephonyTxType = pcmTelephonyTxType;

    // 0 for property disable, so need to set enum value +1
    set_uint32_to_property(PROPERTY_KEY_TELEPHOPNY_TX_TYPE, pcmTelephonyTxType + 1);
}

}; // namespace android
