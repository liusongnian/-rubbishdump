#include "SpeechDriverFactory.h"
#include "SpeechType.h"
#include "SpeechDriverInterface.h"
#include "SpeechDriverDummy.h"
#include <SpeechDriverNormal.h>
#include "SpeechDriverLAD.h"
#include "AudioUtility.h"
#include <SpeechUtility.h>

#include "AudioTypeExt.h"

#if defined(MTK_AUDIODSP_SUPPORT)
#include "SpeechDriverOpenDSP.h"
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SpeechDriverFactory"

// refer to /alps/vendor/mediatek/proprietary/hardware/ril/libratconfig/ratconfig.c
#define CDMA "C"


namespace android {

SpeechDriverFactory *SpeechDriverFactory::mSpeechDriverFactory = NULL;
uint32_t SpeechDriverInterface::mNetworkRate = NETWORK_GSM;
uint8_t SpeechDriverInterface::mIpcLpbkSwitch = 0;
uint8_t SpeechDriverInterface::mIpcPath = 0;

SpeechDriverFactory *SpeechDriverFactory::GetInstance() {
    static Mutex mGetInstanceLock;
    Mutex::Autolock _l(mGetInstanceLock);
    ALOGV("%s()", __FUNCTION__);

    if (mSpeechDriverFactory == NULL) {
        mSpeechDriverFactory = new SpeechDriverFactory();
    }
    ASSERT(mSpeechDriverFactory != NULL);
    return mSpeechDriverFactory;
}

SpeechDriverFactory::SpeechDriverFactory() {
    ALOGV("%s()", __FUNCTION__);

    mSpeechDriver1 = NULL;
    mSpeechDriver2 = NULL;
    mSpeechDriverExternal = NULL;
    mActiveModemIndex = MODEM_1; // default use modem 1

    CreateSpeechDriverInstances();

    ALOGD("-%s(), mActiveModemIndex = %d", __FUNCTION__, mActiveModemIndex);
}
status_t SpeechDriverFactory::CreateSpeechDriverInstances() {

#if defined(MTK_SPEECH_DUMMY)
    ALOGW("%s(), create SpeechDriverDummy for MODEM_1", __FUNCTION__);
    mSpeechDriver1 = new SpeechDriverDummy(MODEM_1);

    ALOGW("%s(), create SpeechDriverDummy for MODEM_2", __FUNCTION__);
    mSpeechDriver2 = new SpeechDriverDummy(MODEM_2);

    ALOGW("%s(), create SpeechDriverDummy for MODEM_EXTERNAL", __FUNCTION__);
    mSpeechDriverExternal = new SpeechDriverDummy(MODEM_EXTERNAL);
#else // no MTK_SPEECH_DUMMY
#if defined(MTK_COMBO_MODEM_SUPPORT)
    if (InFactoryMode()) {
        ALOGD("%s(), factory mode!! create dummy driver", __FUNCTION__);
        mSpeechDriver1 = new SpeechDriverDummy(MODEM_1);
        mSpeechDriver2 = new SpeechDriverDummy(MODEM_2);
        mSpeechDriverExternal = new SpeechDriverDummy(MODEM_EXTERNAL);
        return NO_ERROR;
    }


#if defined(MTK_AUDIODSP_SUPPORT)
    if (isAdspOptionEnable()) {
        if (SpeechDriverOpenDSP::GetInstance(MODEM_1)->isAdspPhoneCallEnhOn()) {
            ALOGD("%s(), create SpeechDriverOpenDSP for MODEM_1", __FUNCTION__);
            mSpeechDriver1 = SpeechDriverOpenDSP::GetInstance(MODEM_1);
        } else {
            ALOGD("%s(), create SpeechDriverNormal for MODEM_1", __FUNCTION__);
            mSpeechDriver1 = SpeechDriverNormal::GetInstance(MODEM_1);
        }
    } else
#endif
    {
        ALOGD("%s(), create SpeechDriverNormal for MODEM_1", __FUNCTION__);
        mSpeechDriver1 = SpeechDriverNormal::GetInstance(MODEM_1);
    }

    ALOGW("%s(), create SpeechDriverDummy for MODEM_2", __FUNCTION__);
    mSpeechDriver2 = new SpeechDriverDummy(MODEM_2);

    ALOGW("%s(), create SpeechDriverDummy for MODEM_EXTERNAL", __FUNCTION__);
    mSpeechDriverExternal = new SpeechDriverDummy(MODEM_EXTERNAL);
#else // no MTK_COMBO_MODEM_SUPPORT
    char md1Support[PROPERTY_VALUE_MAX] = {};
    get_string_from_property("ro.vendor.mtk_protocol1_rat_config", md1Support, PROPERTY_VALUE_MAX);
    uint32 isMd1Support = strlen(md1Support);

    ALOGD("%s(), isMd1Support = %d", __FUNCTION__, isMd1Support);
    if (isMd1Support > 0) {
        // for internal modem_1, always return LAD
        ALOGD("%s(), create SpeechDriverLAD for MODEM_1", __FUNCTION__);
        mSpeechDriver1 = SpeechDriverLAD::GetInstance(MODEM_1);
    } else {
        ALOGW("%s(), create SpeechDriverDummy for MODEM_1", __FUNCTION__);
        mSpeechDriver1 = new SpeechDriverDummy(MODEM_1);
    }
#endif // end of MTK_COMBO_MODEM_SUPPORT
#endif // end of MTK_SPEECH_DUMMY
    return NO_ERROR;
}

status_t SpeechDriverFactory::DestroySpeechDriverInstances() {
    if (mSpeechDriver1 != NULL) {
        delete mSpeechDriver1;
        mSpeechDriver1 = NULL;
    }

    if (mSpeechDriver2 != NULL) {
        delete mSpeechDriver2;
        mSpeechDriver2 = NULL;
    }

    if (mSpeechDriverExternal != NULL) {
        delete mSpeechDriverExternal;
        mSpeechDriverExternal = NULL;
    }
    return NO_ERROR;
}

SpeechDriverFactory::~SpeechDriverFactory() {
    DestroySpeechDriverInstances();
}

SpeechDriverInterface *SpeechDriverFactory::GetSpeechDriver() {
    SpeechDriverInterface *pSpeechDriver = NULL;
    ALOGV("%s(), mActiveModemIndex=%d", __FUNCTION__, mActiveModemIndex);

    switch (mActiveModemIndex) {
    case MODEM_1:
        pSpeechDriver = mSpeechDriver1;
        break;
    case MODEM_2:
        pSpeechDriver = mSpeechDriver2;
        break;
    case MODEM_EXTERNAL:
        pSpeechDriver = mSpeechDriverExternal;
        break;
    default:
        ALOGE("%s: no such modem index %d", __FUNCTION__, mActiveModemIndex);
        break;
    }

    ASSERT(pSpeechDriver != NULL);
    return pSpeechDriver;
}

/**
 * NO GUARANTEE that the returned pointer is not NULL!!
 * Be careful to use this function!!
 */
SpeechDriverInterface *SpeechDriverFactory::GetSpeechDriverByIndex(const modem_index_t modem_index) {
    SpeechDriverInterface *pSpeechDriver = NULL;
    ALOGV("%s(), modem_index=%d", __FUNCTION__, modem_index);

    switch (modem_index) {
    case MODEM_1:
        pSpeechDriver = mSpeechDriver1;
        break;
    case MODEM_2:
        pSpeechDriver = mSpeechDriver2;
        break;
    case MODEM_EXTERNAL:
        pSpeechDriver = mSpeechDriverExternal;
        break;
    default:
        ALOGE("%s: no such modem index %d", __FUNCTION__, modem_index);
        break;
    }

    return pSpeechDriver;
}


modem_index_t SpeechDriverFactory::GetActiveModemIndex() const {
    return mActiveModemIndex;
}

status_t SpeechDriverFactory::SetActiveModemIndex(const modem_index_t modem_index) {
#if !defined(MTK_COMBO_MODEM_SUPPORT)
    ALOGD("%s(), old modem index = %d, new modem index = %d", __FUNCTION__, mActiveModemIndex, modem_index);
#endif
    mActiveModemIndex = modem_index;
    return NO_ERROR;
}


status_t SpeechDriverFactory::SetActiveModemIndexByAudioMode(const audio_mode_t audio_mode) {
    status_t return_status = NO_ERROR;

#if !defined(MTK_COMBO_MODEM_SUPPORT)
    ALOGD("%s(), audio_mode = %d", __FUNCTION__, audio_mode);
#endif

    switch (audio_mode) {
    case AUDIO_MODE_IN_CALL:
    case AUDIO_MODE_CALL_SCREEN:
        return_status = SetActiveModemIndex(MODEM_1);
        break;
    default:
        ALOGE("%s() mode(%d) is neither MODE_IN_CALL nor MODE_IN_CALL_2!!", __FUNCTION__, audio_mode);
        return_status = INVALID_OPERATION;
        break;
    }
    return return_status;
}


} // end of namespace android

