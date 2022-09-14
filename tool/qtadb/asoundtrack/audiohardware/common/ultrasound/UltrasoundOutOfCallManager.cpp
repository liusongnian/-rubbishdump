#include "UltrasoundOutOfCallManager.h"
#include <hardware_legacy/power.h>
#include "AudioALSAStreamManager.h"
#include "AudioALSAUltrasoundOutOfCallController.h"
#include "AudioALSAHardwareResourceManager.h"
#include "AudioVolumeFactory.h"

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#define LOG_TAG "UltrasoundOutOfCallManager"

namespace android
{
static const char ULTRASOUND_OUTOFCALL_WAKELOCK_NAME[] = "ULTRASOUND_OUTOFCALL_WAKELOCK_NAME";
const char* ultrasound_log_propty = "vendor.audio.ultrasound.log";

UltrasoundUsecaseConfig UltrasoundUsecaseTable[] = {
    {
        String8("ultrasound-proximity"),
        UltrasoundDeviceConfig(10, 48000, 48000, 2, 2, ULTRASOUND_OUT_CHANNEL_LEFT, 24),
    }
};

UltrasoundOutOfCallManager *UltrasoundOutOfCallManager::mUltrasoundOutOfCallManager = NULL;
UltrasoundOutOfCallManager *UltrasoundOutOfCallManager::GetInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);
    if (mUltrasoundOutOfCallManager == NULL) {
        mUltrasoundOutOfCallManager = new UltrasoundOutOfCallManager();
    }
    ASSERT(mUltrasoundOutOfCallManager != NULL);
    return mUltrasoundOutOfCallManager;
}

UltrasoundOutOfCallManager::UltrasoundOutOfCallManager() :
    mOutputDeviceCopy(AUDIO_DEVICE_OUT_EARPIECE),
    mActivUsecaseCount(0) {
    ALOGV("%s()", __FUNCTION__);
    for (int i = 0; i < MAX_ULTRASOUND_USECASES; i++) {
        mUsecaseActivated[i] = 0;
    }
    mLogFlag = property_get_int32(ultrasound_log_propty, 0);
}

UltrasoundOutOfCallManager::~UltrasoundOutOfCallManager() {
    ALOGV(" %s()", __FUNCTION__);
}

status_t UltrasoundOutOfCallManager::HandleUsecaseRequest(UltrasoundUsecaseInfo &usecase) {
    status_t status = NO_ERROR;
    Mutex::Autolock _l(mOutOfCallManagerLock);
    //Determine whether the kernel supports
    if (!(AudioALSAUltrasoundOutOfCallController::getInstance()->isUsndSupported())) {
        ALOGD("%s(), kernel not support", __FUNCTION__);
        return status;
    }
    if (usecase.Enable) {
        if (mActivUsecaseCount == 0) {
            status = EnableEngineAndUsecase(usecase);
            if (status == NO_ERROR) {
                mUsecaseActivated[usecase.Idx] = 1;
                mActivUsecaseCount++;
            }
        } else {
            if (mUsecaseActivated[usecase.Idx] == 0) {
                mUsecaseActivated[usecase.Idx] = 1;
                mActivUsecaseCount++;
            } else {
                ALOGE("%s(), Activation denied (Id:%d is active)", __FUNCTION__, usecase.Idx);
            }
        }
    } else {
        if (mActivUsecaseCount == 1 && mUsecaseActivated[usecase.Idx] == 1) {
            status = DisableEngineAndUsecase(usecase);
            if (status == NO_ERROR) {
                mUsecaseActivated[usecase.Idx] = 0;
                mActivUsecaseCount--;
            }
        } else {
            if (mUsecaseActivated[usecase.Idx] == 1) {
                mUsecaseActivated[usecase.Idx] = 0;
                mActivUsecaseCount--;
            } else {
                ALOGE("%s(), Dectivation denied (Id:%d is not active)", __FUNCTION__, usecase.Idx);
            }
        }
    }
    ALOGD("%s(), Id:%d, Enable:%d, status:%d", __FUNCTION__, usecase.Idx, usecase.Enable, status);
    return status;
}

void UltrasoundOutOfCallManager::GetUsecaseInfo(AudioParameter &param,
                                                UltrasoundUsecaseInfo &usecase) {
    int i = 0;
    int value = -1;
    int num_usecases = (sizeof(UltrasoundUsecaseTable) / sizeof(UltrasoundUsecaseTable[0]));

    while (i < num_usecases) {
        String8 us_string = GetUsecaseString(i);
        ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,
                "%s(): us_string : %s", __FUNCTION__, us_string.string());
        if (param.getInt(us_string, value) == NO_ERROR) {
            break;
        }
        i++;
    }

    if (i < num_usecases) {
        if (value == 1 || value == 0) {
            usecase.Enable = (value==1);
            usecase.Idx = i;
        }
    }
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "%s(): i, value: %d, %d  Selected Usecase: %s",
            __FUNCTION__, i, value, (i<num_usecases) ? GetUsecaseString(i).string() : "N/A");
}

status_t UltrasoundOutOfCallManager::EnableEngineAndUsecase(const UltrasoundUsecaseInfo &usecase) {
    status_t status = NO_ERROR;

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, ">>> %s()", __FUNCTION__);

    // get devices
    audio_devices_t output_device = mOutputDeviceCopy;

    AudioALSAUltrasoundOutOfCallController::getInstance()->setUsecaseName(
            GetUltrasoundUsecaseName(usecase.Idx));

    UltrasoundDeviceConfig ultrasound_devcfg = GetUltrasoundDeviceConfig(usecase.Idx);
    status = AudioALSAUltrasoundOutOfCallController::getInstance()->open(output_device,
                                                                         ultrasound_devcfg);
    if (status == NO_ERROR) {
        // acquire wake lock
        int ret = acquire_wake_lock(PARTIAL_WAKE_LOCK, ULTRASOUND_OUTOFCALL_WAKELOCK_NAME);
        if (ret != 0) {
            ALOGE("%s(), acquire_wake_lock fail, %s return %d.",
                    __FUNCTION__, ULTRASOUND_OUTOFCALL_WAKELOCK_NAME, ret);
        }
    } else {
        ALOGE("%s(), open ultrasound failed", __FUNCTION__);
    }

    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, "<<< %s()", __FUNCTION__);
    return status;
}

status_t UltrasoundOutOfCallManager::DisableEngineAndUsecase(const UltrasoundUsecaseInfo &usecase) {
    status_t status = NO_ERROR;
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG, ">>> %s(), usecase.Enable=%d",
             __FUNCTION__, usecase.Enable);

    status = AudioALSAUltrasoundOutOfCallController::getInstance()->close();

    // release wake lock
    int ret = release_wake_lock(ULTRASOUND_OUTOFCALL_WAKELOCK_NAME);
    if (ret != 0) {
        ALOGE("%s(), release_wake_lock fail, %s return %d.",
                __FUNCTION__, ULTRASOUND_OUTOFCALL_WAKELOCK_NAME, ret);
        status = ret;
    }
    ALOGD_IF(mLogFlag & USND_CTL_FLOW_DEBUG,"<<< %s()", __FUNCTION__);
    return status;
}

UltrasoundDeviceConfig UltrasoundOutOfCallManager::GetUltrasoundDeviceConfig(int i) {
    return UltrasoundUsecaseTable[i].UltrasoundDevCfg;
}

String8 UltrasoundOutOfCallManager::GetUsecaseString(int i) {
    return UltrasoundUsecaseTable[i].UsecaseName;
}

const char* UltrasoundOutOfCallManager::GetUltrasoundUsecaseName(int i) {
    return UltrasoundUsecaseTable[i].UsecaseName.string();
}

} // end of namespace android
