#include "AudioALSACaptureHandlerSyncIO.h"
#include "AudioALSAHardwareResourceManager.h"
#include "AudioALSACaptureDataClientSyncIO.h"
#include "AudioALSACaptureDataProviderNormal.h"

#include "AudioSmartPaController.h"
#include "WCNChipController.h"
#include "AudioALSACaptureDataProviderEchoRefExt.h"
#include "AudioALSACaptureDataProviderEchoRef.h"
#include "AudioALSACaptureDataProviderEchoRefBTCVSD.h"
#include "AudioALSACaptureDataProviderEchoRefBTSCO.h"
#if defined(PRIMARY_USB)
#include "AudioALSACaptureDataProviderEchoRefUsb.h"
#endif

#include "AudioParamParser.h"
#include <SpeechUtility.h>
#ifdef MTK_BT_HEARING_AID_SUPPORT
#include "AudioALSACaptureDataProviderHAP.h"
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSACaptureHandlerSyncIO"

namespace android {

//static FILE *pOutFile = NULL;

AudioALSACaptureHandlerSyncIO::AudioALSACaptureHandlerSyncIO(stream_attribute_t *stream_attribute_target) :
    AudioALSACaptureHandlerBase(stream_attribute_target) {
    ALOGD("%s()", __FUNCTION__);

    init();
}


AudioALSACaptureHandlerSyncIO::~AudioALSACaptureHandlerSyncIO() {
    ALOGD("+%s()", __FUNCTION__);

    ALOGD("%-s()", __FUNCTION__);
}


status_t AudioALSACaptureHandlerSyncIO::init() {
    ALOGD("%s()", __FUNCTION__);
    mCaptureHandlerType = CAPTURE_HANDLER_SYNCIO;
    return NO_ERROR;
}


status_t AudioALSACaptureHandlerSyncIO::open() {
    ALOGD("+%s(), input_device = 0x%x, input_source = 0x%x, sample_rate=%d, num_channels=%d",
          __FUNCTION__, mStreamAttributeTarget->input_device, mStreamAttributeTarget->input_source, mStreamAttributeTarget->sample_rate,
          mStreamAttributeTarget->num_channels);

    ASSERT(mCaptureDataClient == NULL);

    if (mStreamAttributeTarget->input_source == AUDIO_SOURCE_ECHO_REFERENCE) {
        AudioALSACaptureDataProviderBase *pDataProviderEchoRef = NULL;

        if (AudioSmartPaController::getInstance()->isSmartPAUsed() &&
            (mStreamAttributeTarget->output_devices & AUDIO_DEVICE_OUT_SPEAKER) &&
            !AudioSmartPaController::getInstance()->isApSideSpkProtect()) {
            pDataProviderEchoRef = AudioALSACaptureDataProviderEchoRefExt::getInstance();
        } else if (audio_is_bluetooth_out_sco_device(mStreamAttributeTarget->output_devices)) {
            // open BT echoref data provider
            if (WCNChipController::GetInstance()->IsBTMergeInterfaceSupported() == true) {
                pDataProviderEchoRef = AudioALSACaptureDataProviderEchoRefBTSCO::getInstance();
            } else {
                pDataProviderEchoRef = AudioALSACaptureDataProviderEchoRefBTCVSD::getInstance();
            }
#if defined(PRIMARY_USB)
        } else if (audio_is_usb_out_device(mStreamAttributeTarget->output_devices)) {
            pDataProviderEchoRef = AudioALSACaptureDataProviderEchoRefUsb::getInstance();
#endif
        } else {
            pDataProviderEchoRef = AudioALSACaptureDataProviderEchoRef::getInstance();
        }

        ASSERT(pDataProviderEchoRef != NULL);
        mCaptureDataClient = new AudioALSACaptureDataClientSyncIO(pDataProviderEchoRef, mStreamAttributeTarget);
#ifdef MTK_BT_HEARING_AID_SUPPORT
    } else if ((appIsFeatureOptionEnabled("MTK_BT_HEARING_AID_SUPPORT")) &&
               (get_uint32_from_property("persist.vendor.audiohal.asha_test")||
               (mStreamAttributeTarget->input_source == AUDIO_SOURCE_MIC &&
                mStreamAttributeTarget->input_device == AUDIO_DEVICE_IN_TELEPHONY_RX))) {
        mCaptureDataClient = new AudioALSACaptureDataClientSyncIO(AudioALSACaptureDataProviderHAP::getInstance(), mStreamAttributeTarget);
#endif
    } else {
        mCaptureDataClient = new AudioALSACaptureDataClientSyncIO(AudioALSACaptureDataProviderNormal::getInstance(), mStreamAttributeTarget);
    }

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSACaptureHandlerSyncIO::close() {
    ALOGD("+%s()", __FUNCTION__);

    ASSERT(mCaptureDataClient != NULL);
    delete mCaptureDataClient;

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}


status_t AudioALSACaptureHandlerSyncIO::routing(const audio_devices_t input_device) {
    mHardwareResourceManager->changeInputDevice(input_device);
    return NO_ERROR;
}


ssize_t AudioALSACaptureHandlerSyncIO::read(void *buffer, ssize_t bytes) {
    ALOGV("%s()", __FUNCTION__);

    mCaptureDataClient->read(buffer, bytes);
#if 0   //remove dump here which might cause process too long due to SD performance
    if (pOutFile != NULL) {
        fwrite(buffer, sizeof(char), bytes, pOutFile);
    }
#endif

    return bytes;
}

} // end of namespace android
