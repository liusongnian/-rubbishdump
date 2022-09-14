#include <AudioIEMsController.h>

#include <stdint.h>
#include <stdlib.h>

#include <errno.h>
#include <inttypes.h>

#include <log/log.h>
#include <system/audio.h>

#include <AudioType.h>

#include <AudioAssert.h>

#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif


#include <AudioALSAStreamManager.h>

#include <AudioALSAPlaybackHandlerBase.h>
#include <AudioALSACaptureDataClientIEMs.h>

#if defined(PRIMARY_USB)
#include <AudioUSBCenter.h>
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioIEMsController"


namespace android {


/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */

//#define ENABLE_BT_IEMS


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

AudioIEMsController *AudioIEMsController::mAudioIEMsController = NULL;
AudioIEMsController *AudioIEMsController::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioIEMsController == NULL) {
        mAudioIEMsController = new AudioIEMsController();
    }
    ASSERT(mAudioIEMsController != NULL);
    return mAudioIEMsController;
}

AudioIEMsController::AudioIEMsController() :
    mEnable(false),
    mSuspendMask(0),
    mOpen(false),
    mHasWiredHeadsetOut(false),
    mHasWiredHeadsetIn(false),
    mHasWiredHeadset(false),
    mHasBtHeadsetOut(false),
    mHasBtHeadsetIn(false),
    mHasBtHeadset(false),
    mHasUsbHeadsetOut(false),
    mHasUsbHeadsetIn(false),
    mHasUsbHeadset(false),
    mSuspendWhenAllDevDisconn(false),
    mOutputDevice(AUDIO_DEVICE_NONE),
    mInputDevice(AUDIO_DEVICE_NONE),
    mIsDeviceValidForIEMs(false),
    mIsWaitConnDone(false),
    mSampleRate(0),
    mPeriodUs(0),
    mStreamManager(AudioALSAStreamManager::getInstance()),
    mPlaybackHandler(NULL),
    mCaptureDataClient(NULL) {

    memset(&mAttrOut, 0, sizeof(mAttrOut));
    memset(&mAttrIn, 0, sizeof(mAttrIn));

    /* default suspend when init, and wait device connected */
    mSuspendWhenAllDevDisconn = true;
    suspend(IEMS_SUSPEND_USER_DEV_CONNECT);
}


AudioIEMsController::~AudioIEMsController() {

}


int AudioIEMsController::wrapWriteCbk(
    void *buffer, uint32_t bytes, void *arg) {
    AudioIEMsController *pAudioIEMsController;
    int ret = 0;

    pAudioIEMsController = static_cast<AudioIEMsController *>(arg);
    if (!pAudioIEMsController) {
        ALOGE("%s(), pAudioIEMsController NULL!!", __FUNCTION__);
        return 0;
    }
    if (!pAudioIEMsController->isIEMsOn()) {
        return 0;
    }

    AL_LOCK(pAudioIEMsController->mPlaybackHandlerLock);
    if (pAudioIEMsController->mPlaybackHandler) {
        ret = pAudioIEMsController->mPlaybackHandler->write(buffer, bytes);
    }
    AL_UNLOCK(pAudioIEMsController->mPlaybackHandlerLock);

    return ret;
}


void AudioIEMsController::createPlaybackHandler(void) {
    /* init attr */
    mAttrOut.audio_format = AUDIO_FORMAT_PCM_32_BIT;
    mAttrOut.audio_channel_mask = AUDIO_CHANNEL_OUT_STEREO;
    mAttrOut.mAudioOutputFlags = AUDIO_OUTPUT_FLAG_FAST;
    mAttrOut.input_device = mInputDevice;
    mAttrOut.output_devices = mOutputDevice;
    mAttrOut.num_channels = popcount(mAttrOut.audio_channel_mask);
    mAttrOut.sample_rate = mSampleRate;
    mAttrOut.audio_mode = AUDIO_MODE_NORMAL;
    mAttrOut.buffer_size = getPeriodBufSizeByUs(&mAttrOut, mPeriodUs);
    mAttrOut.isIEMsSource = true;
    mAttrOut.periodUs = mPeriodUs;
    mAttrOut.periodCnt = 2;

    AL_LOCK(mPlaybackHandlerLock);
    mPlaybackHandler = mStreamManager->createPlaybackHandler(&mAttrOut);
    mPlaybackHandler->open();
    mPlaybackHandler->setFirstDataWriteFlag(true);
    AL_UNLOCK(mPlaybackHandlerLock);
}


void AudioIEMsController::destoryPlaybackHandler(void) {
    AL_LOCK(mPlaybackHandlerLock);
    if (mPlaybackHandler) {
        mPlaybackHandler->close();
        mStreamManager->destroyPlaybackHandler(mPlaybackHandler);
        mPlaybackHandler = NULL;
    }
    AL_UNLOCK(mPlaybackHandlerLock);
}


void AudioIEMsController::createCaptureDataClient(void) {
    /* init attr */
    mAttrIn.audio_format = AUDIO_FORMAT_PCM_32_BIT;
    mAttrIn.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
    mAttrIn.mAudioInputFlags = AUDIO_INPUT_FLAG_FAST;
    mAttrIn.input_device = mInputDevice;
    mAttrIn.output_devices = mOutputDevice;
    mAttrIn.input_source = AUDIO_SOURCE_DEFAULT;
    mAttrIn.num_channels = popcount(mAttrIn.audio_channel_mask);
    mAttrIn.sample_rate = mSampleRate;
    mAttrIn.audio_mode = AUDIO_MODE_NORMAL;
    mAttrIn.buffer_size = getPeriodBufSizeByUs(&mAttrIn, mPeriodUs);
    mAttrIn.isIEMsSource = true;
    mAttrIn.periodUs = mPeriodUs;
    mAttrIn.periodCnt = 2;


    /* create data client */
    mCaptureDataClient = AudioALSACaptureDataClientIEMs::getInstance();
    mCaptureDataClient->open(&mAttrIn,
                             wrapWriteCbk,
                             this);
}


void AudioIEMsController::destroyCaptureDataClient(void) {
    if (mCaptureDataClient) {
        mCaptureDataClient->close();
        mCaptureDataClient = NULL;
    }
}


bool AudioIEMsController::isAbleToRoutingByIEMs(void) {
    return (mStreamManager->getActiveStreamOutSize() == 0) ? true : false;
}


int AudioIEMsController::connectDevice(const audio_devices_t device) {
    audio_devices_t connOutputDevice = AUDIO_DEVICE_NONE;
    audio_devices_t connInputDevice = AUDIO_DEVICE_NONE;

    if (device == AUDIO_DEVICE_NONE) {
        return -EINVAL;
    }

    AL_AUTOLOCK(mDevConnLock);

    ALOGD("%s(+), cur out 0x%x in 0x%x, hs %d/%d/%d, bt %d/%d/%d, usb %d/%d/%d, device 0x%x",
          __FUNCTION__,
          mOutputDevice,
          mInputDevice,
          mHasWiredHeadsetOut,
          mHasWiredHeadsetIn,
          mHasWiredHeadset,
          mHasBtHeadsetOut,
          mHasBtHeadsetIn,
          mHasBtHeadset,
          mHasUsbHeadsetOut,
          mHasUsbHeadsetIn,
          mHasUsbHeadset,
          device);

    /* only support headset for IEMs */
    if (audio_is_output_device(device)) {
        if (device == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
            mHasWiredHeadsetOut = true;
            if (mHasWiredHeadsetIn) {
                mHasWiredHeadset = true;
                connOutputDevice = AUDIO_DEVICE_OUT_WIRED_HEADSET;
                connInputDevice = AUDIO_DEVICE_IN_WIRED_HEADSET;
            }
#if defined(ENABLE_BT_IEMS)
        } else if (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) {
            mHasBtHeadsetOut = true;
            if (mHasBtHeadsetIn) {
                mHasBtHeadset = true;
                connOutputDevice = AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
                connInputDevice = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
            }
#endif
#if defined(PRIMARY_USB)
        } else if (device == AUDIO_DEVICE_OUT_USB_HEADSET) {
            mHasUsbHeadsetOut = true;
            if (mHasUsbHeadsetIn) {
                mHasUsbHeadset = true;
                connOutputDevice = AUDIO_DEVICE_OUT_USB_HEADSET;
                connInputDevice = AUDIO_DEVICE_IN_USB_HEADSET;
            }
#endif
        }
    } else if (audio_is_input_device(device)) {
        if (device == AUDIO_DEVICE_IN_WIRED_HEADSET) {
            mHasWiredHeadsetIn = true;
            if (mHasWiredHeadsetOut) {
                mHasWiredHeadset = true;
                connOutputDevice = AUDIO_DEVICE_OUT_WIRED_HEADSET;
                connInputDevice = AUDIO_DEVICE_IN_WIRED_HEADSET;
            }
#if defined(ENABLE_BT_IEMS)
        } else if (device == AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            mHasBtHeadsetIn = true;
            if (mHasBtHeadsetOut) {
                mHasBtHeadset = true;
                connOutputDevice = AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
                connInputDevice = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
            }
#endif
#if defined(PRIMARY_USB)
        } else if (device == AUDIO_DEVICE_IN_USB_HEADSET) {
            mHasUsbHeadsetIn = true;
            if (mHasUsbHeadsetOut) {
                mHasUsbHeadset = true;
                connOutputDevice = AUDIO_DEVICE_OUT_USB_HEADSET;
                connInputDevice = AUDIO_DEVICE_IN_USB_HEADSET;
            }
#endif
        }
    } else {
        return -EINVAL;
    }


    if (mHasWiredHeadset || mHasBtHeadset || mHasUsbHeadset) {
        if (mSuspendWhenAllDevDisconn == true) {
            mSuspendWhenAllDevDisconn = false;
            resume(IEMS_SUSPEND_USER_DEV_CONNECT);
        }
    }

    if (connOutputDevice != AUDIO_DEVICE_NONE && connInputDevice != AUDIO_DEVICE_NONE) {
        if (isAbleToRoutingByIEMs() || /* able to sing along even when no music playing */
            mIsWaitConnDone) {          /* routing but not connect both in/out yet */
            routing(connOutputDevice);
        }
    }


    ALOGD("%s(-), cur out 0x%x in 0x%x, hs %d/%d/%d, bt %d/%d/%d, usb %d/%d/%d, device 0x%x",
          __FUNCTION__,
          mOutputDevice,
          mInputDevice,
          mHasWiredHeadsetOut,
          mHasWiredHeadsetIn,
          mHasWiredHeadset,
          mHasBtHeadsetOut,
          mHasBtHeadsetIn,
          mHasBtHeadset,
          mHasUsbHeadsetOut,
          mHasUsbHeadsetIn,
          mHasUsbHeadset,
          device);

    return 0;
}


int AudioIEMsController::disconnectDevice(const audio_devices_t device) {
    bool reRouting = false;

    if (device == AUDIO_DEVICE_NONE) {
        return -EINVAL;
    }

    AL_AUTOLOCK(mDevConnLock);

    ALOGD("%s(+), cur out 0x%x in 0x%x, hs %d/%d/%d, bt %d/%d/%d, usb %d/%d/%d, device 0x%x",
          __FUNCTION__,
          mOutputDevice,
          mInputDevice,
          mHasWiredHeadsetOut,
          mHasWiredHeadsetIn,
          mHasWiredHeadset,
          mHasBtHeadsetOut,
          mHasBtHeadsetIn,
          mHasBtHeadset,
          mHasUsbHeadsetOut,
          mHasUsbHeadsetIn,
          mHasUsbHeadset,
          device);

    /* only support headset for IEMs */
    if (audio_is_output_device(device)) {
        if (device == mOutputDevice) {
            reRouting = true;
        }

        if (device == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
            mHasWiredHeadsetOut = false;
            mHasWiredHeadset = false;
#if defined(ENABLE_BT_IEMS)
        } else if (device == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) {
            mHasBtHeadsetOut = false;
            mHasBtHeadset = false;
#endif
#if defined(PRIMARY_USB)
        } else if (device == AUDIO_DEVICE_OUT_USB_HEADSET) {
            mHasUsbHeadsetOut = false;
            mHasUsbHeadset = false;
#endif
        }
    } else if (audio_is_input_device(device)) {
        if (device == mInputDevice) {
            reRouting = true;
        }

        if (device == AUDIO_DEVICE_IN_WIRED_HEADSET) {
            mHasWiredHeadsetIn = false;
            mHasWiredHeadset = false;
#if defined(ENABLE_BT_IEMS)
        } else if (device == AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET) {
            mHasBtHeadsetIn = false;
            mHasBtHeadset = false;
#endif
#if defined(PRIMARY_USB)
        } else if (device == AUDIO_DEVICE_IN_USB_HEADSET) {
            mHasUsbHeadsetIn = false;
            mHasUsbHeadset = false;
#endif
        }
    } else {
        return -EINVAL;
    }


    if (!mHasWiredHeadset && !mHasBtHeadset && !mHasUsbHeadset) {
        if (mSuspendWhenAllDevDisconn == false) {
            mSuspendWhenAllDevDisconn = true;
            suspend(IEMS_SUSPEND_USER_DEV_CONNECT);
        }
    }

    /* able to sing along even when no music playing */
    if (reRouting && isAbleToRoutingByIEMs()) {
        if (mHasWiredHeadset) {
            routing(AUDIO_DEVICE_OUT_WIRED_HEADSET);
#if defined(ENABLE_BT_IEMS)
        } else if (mHasBtHeadset) {
            routing(AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET);
#endif
#if defined(PRIMARY_USB)
        } else if (mHasUsbHeadset) {
            routing(AUDIO_DEVICE_OUT_USB_HEADSET);
#endif
        }
    }

    ALOGD("%s(-), cur out 0x%x in 0x%x, hs %d/%d/%d, bt %d/%d/%d, usb %d/%d/%d, device 0x%x",
          __FUNCTION__,
          mOutputDevice,
          mInputDevice,
          mHasWiredHeadsetOut,
          mHasWiredHeadsetIn,
          mHasWiredHeadset,
          mHasBtHeadsetOut,
          mHasBtHeadsetIn,
          mHasBtHeadset,
          mHasUsbHeadsetOut,
          mHasUsbHeadsetIn,
          mHasUsbHeadset,
          device);

    return 0;
}


int AudioIEMsController::routing(const audio_devices_t output_devices) {
    AL_AUTOLOCK(mLock);

    if (output_devices == AUDIO_DEVICE_NONE) {
        return -EINVAL;
    }
    if (output_devices == mOutputDevice && isIEMsOn()) {
        ALOGV("%s(), same device running, do nothing", __FUNCTION__);
        return 0;
    }

    ALOGD("%s(+), cur out 0x%x in 0x%x, hs %d, bt %d, usb %d, device 0x%x, mask 0x%x, on %d",
          __FUNCTION__,
          mOutputDevice,
          mInputDevice,
          mHasWiredHeadset,
          mHasBtHeadset,
          mHasUsbHeadset,
          output_devices,
          mSuspendMask,
          isIEMsOn());

    /* do routing */
    if (isIEMsOn()) {
        close();
    }

    if (output_devices == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
        if (mHasWiredHeadset) {
            mOutputDevice = AUDIO_DEVICE_OUT_WIRED_HEADSET;
            mInputDevice = AUDIO_DEVICE_IN_WIRED_HEADSET;
            mIsDeviceValidForIEMs = true;
            mIsWaitConnDone = false;
        } else {
            mOutputDevice = AUDIO_DEVICE_NONE;
            mInputDevice = AUDIO_DEVICE_NONE;
            mIsDeviceValidForIEMs = false;
            mIsWaitConnDone = true;
        }
#if defined(ENABLE_BT_IEMS)
    } else if (output_devices == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) {
        if (mHasBtHeadset) {
            mOutputDevice = AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET;
            mInputDevice = AUDIO_DEVICE_IN_BLUETOOTH_SCO_HEADSET;
            mIsDeviceValidForIEMs = true;
            mIsWaitConnDone = false;
        } else {
            mOutputDevice = AUDIO_DEVICE_NONE;
            mInputDevice = AUDIO_DEVICE_NONE;
            mIsDeviceValidForIEMs = false;
            mIsWaitConnDone = true;
        }
#endif
#if defined(PRIMARY_USB)
    } else if (output_devices == AUDIO_DEVICE_OUT_USB_HEADSET) {
        if (mHasUsbHeadset) {
            mOutputDevice = AUDIO_DEVICE_OUT_USB_HEADSET;
            mInputDevice = AUDIO_DEVICE_IN_USB_HEADSET;
            mIsDeviceValidForIEMs = true;
            mIsWaitConnDone = false;
        } else {
            mOutputDevice = AUDIO_DEVICE_NONE;
            mInputDevice = AUDIO_DEVICE_NONE;
            mIsDeviceValidForIEMs = false;
            mIsWaitConnDone = true;
        }
#endif
    } else {
        mOutputDevice = AUDIO_DEVICE_NONE;
        mInputDevice = AUDIO_DEVICE_NONE;
        mIsDeviceValidForIEMs = false;
        mIsWaitConnDone = false;
    }

    if (mEnable && !mSuspendMask && mIsDeviceValidForIEMs) {
        open();
    }

    ALOGD("%s(-), cur out 0x%x in 0x%x", __FUNCTION__, mOutputDevice, mInputDevice);

    return 0;
}


int AudioIEMsController::enable() {
    AL_AUTOLOCK(mLock);

    ALOGD("%s(), mask 0x%x", __FUNCTION__, mSuspendMask);

    if (mEnable == true) {
        ALOGE("%s(), already enabled!!", __FUNCTION__);
        return -EEXIST;
    }
    mEnable = true;

    if (!mSuspendMask && mIsDeviceValidForIEMs) {
        open();
    }

    return 0;
}


int AudioIEMsController::disable() {
    AL_AUTOLOCK(mLock);

    ALOGD("%s(), mask 0x%x", __FUNCTION__, mSuspendMask);

    if (mEnable == false) {
        ALOGW("%s(), already disabled!!", __FUNCTION__);
        return 0;
    }
    mEnable = false;

    if (isIEMsOn()) {
        close();
    }

    return 0;
}


void AudioIEMsController::suspend(const uint32_t mask) {
    AL_AUTOLOCK(mLock);

    if (mask & mSuspendMask) { /* already suspend */
        return;
    }

    ALOGD("%s(), mask 0x%x, mSuspendMask 0x%x => 0x%x", __FUNCTION__, mask, mSuspendMask, (mSuspendMask | mask));
    mSuspendMask |= mask;

    if (isIEMsOn()) {
        close();
    }

    return;
}


void AudioIEMsController::resume(const uint32_t mask) {
    AL_AUTOLOCK(mLock);

    if ((mask & mSuspendMask) == 0) { /* already resume */
        return;
    }

    ALOGD("%s(), mask 0x%x, mSuspendMask 0x%x => 0x%x", __FUNCTION__, mask, mSuspendMask, (mSuspendMask & (~mask)));
    mSuspendMask &= (~mask);

    if (mEnable && !mSuspendMask && mIsDeviceValidForIEMs && !isIEMsOn()) {
        open();
    }

    return;
}


int AudioIEMsController::open() {
    if (!mEnable || mSuspendMask || !mIsDeviceValidForIEMs) {
        ALOGE("%s(), mEnable %d mSuspendMask 0x%x mIsDeviceValidForIEMs %d!! return",
              __FUNCTION__, mEnable, mSuspendMask, mIsDeviceValidForIEMs);
        return -EINVAL;
    }

    if (mOutputDevice == AUDIO_DEVICE_OUT_WIRED_HEADSET) {
        mSampleRate = 48000; // TODO: HiFi
        mPeriodUs = 5000;
#if defined(ENABLE_BT_IEMS)
    } else if (mOutputDevice == AUDIO_DEVICE_OUT_BLUETOOTH_SCO_HEADSET) {
        mSampleRate = 48000; // TODO: no SRC?
        mPeriodUs = 20500;   // TODO: no throttle?
#endif
#if defined(PRIMARY_USB)
    } else if (mOutputDevice == AUDIO_DEVICE_OUT_USB_HEADSET) {
        mSampleRate = 48000; // TODO: HiFi
        mPeriodUs = AudioUSBCenter::getInstance()->getIEMsPeriodUs();
#endif
    }
    if (!mSampleRate || !mPeriodUs) {
        ALOGE("%s(), sample_rate %u period_us %u error!!", __FUNCTION__, mSampleRate, mPeriodUs);
        return -EINVAL;
    }
    ALOGD("%s(), out 0x%x in 0x%x rate %u us %u", __FUNCTION__,
          mOutputDevice,
          mInputDevice,
          mSampleRate,
          mPeriodUs);

    mOpen = true;

    mStreamManager->setAllStreamsSuspend(true, false);
    mStreamManager->standbyAllStreams(true);

    createPlaybackHandler();
    createCaptureDataClient();

    mStreamManager->setAllStreamsSuspend(false, false);

    return 0;
}


int AudioIEMsController::close() {
    bool doOutSuspendStandby = false;

    ALOGD("%s()", __FUNCTION__);

    if (mOpen == false) {
        ALOGW("%s(), already close!!", __FUNCTION__);
        return 0;
    }
    mOpen = false;

    /* if playback go via mixer, others can still go via mixer */
    if (!audio_is_bluetooth_out_sco_device(mOutputDevice) &&
        !audio_is_usb_out_device(mOutputDevice)) {
        mStreamManager->setAllOutputStreamsSuspend(true, false);
        mStreamManager->standbyAllOutputStreams(true);
        doOutSuspendStandby = true;
    }
    /* suspend all stream in when close IEMs */
    mStreamManager->setAllInputStreamsSuspend(true, false);
    mStreamManager->standbyAllInputStreams(true);

    destoryPlaybackHandler();
    destroyCaptureDataClient();

    if (doOutSuspendStandby) {
        mStreamManager->setAllOutputStreamsSuspend(false, false);
    }
    mStreamManager->setAllInputStreamsSuspend(false, false);

    return 0;
}


} /* end of namespace android */

