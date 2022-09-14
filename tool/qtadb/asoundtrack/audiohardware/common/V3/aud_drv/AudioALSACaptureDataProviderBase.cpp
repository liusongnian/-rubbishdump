#include "AudioALSACaptureDataProviderBase.h"

#include <inttypes.h>

#include "AudioType.h"
#include <AudioLock.h>

#include <audio_lock.h>

#include "IAudioALSACaptureDataClient.h"
#include "AudioALSAHardwareResourceManager.h"

#ifdef MTK_AUDIO_IPI_SUPPORT
#include <AudioMessengerIPI.h>
#endif

#ifdef MTK_AUDIODSP_SUPPORT
#include <audio_task.h>
#include "AudioDspType.h"
#include "AudioDspStreamManager.h"
#endif

#include "AudioSmartPaController.h"
#include "AudioSpeechEnhanceInfo.h"
#include "AudioALSAStreamManager.h"
#include "AudioALSASpeechPhoneCallController.h"
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSACaptureDataProviderBase"


namespace android {
#ifdef MTK_AUDIODSP_SUPPORT
uint32_t AudioALSACaptureDataProviderBase::mDumpFileNumDsp = 0;
#endif

int AudioALSACaptureDataProviderBase::mDumpFileNum = 0;
AudioALSACaptureDataProviderBase::AudioALSACaptureDataProviderBase() :
#ifdef MTK_AUDIO_IPI_SUPPORT
    mAudioMessengerIPI(AudioMessengerIPI::getInstance()),
#else
    mAudioMessengerIPI(NULL),
#endif
    mHardwareResourceManager(AudioALSAHardwareResourceManager::getInstance()),
    mCaptureFramesReaded(0),
    mTotalReadBytes(0),
    mEnable(false),
    mPlaybackEnabled(false),
    mOpenIndex(0),
    mCurTopSource(AUDIO_SOURCE_DEFAULT),
    mPcm(NULL),
    mStart(true),
    mReadThreadReady(true),
    mCaptureDataProviderType(CAPTURE_PROVIDER_BASE),
    mPcmflag(0),
    audio_pcm_read_wrapper_fp(NULL),
    mBytesWavDumpWritten(0),
    mBytesWavDumpWritten4ch(0){
    ALOGD("%s(), %p", __FUNCTION__, this);

    mCaptureDataClientVector.clear();
    memset((void *)&mCaptureStartTime, 0, sizeof(mCaptureStartTime));

    memset((void *)&mPcmReadBuf, 0, sizeof(mPcmReadBuf));

    memset((void *)&mConfig, 0, sizeof(mConfig));

    memset((void *)&mStreamAttributeSource, 0, sizeof(mStreamAttributeSource));

    memset((void *)&mStreamAttributeTargetDSP, 0, sizeof(mStreamAttributeTargetDSP));

    memset((void *)&mCaptureTimeStamp, 0, sizeof(timespec));

    mPCMDumpFile = NULL;
    mPCMDumpFile4ch = NULL;

    mlatency = UPLINK_NORMAL_LATENCY_MS;
    mPeriodUs = 0;
    mNewBufSize = 0;

    audio_pcm_read_wrapper_fp = pcm_read;

    mPcmStatus = NO_ERROR;

    mlog_flag = AudioALSADriverUtility::getInstance()->GetPropertyValue(streamin_log_propty);

#ifdef MTK_AUDIODSP_SUPPORT
    mPCMDumpFileDsp = NULL;
    if (isAdspOptionEnable()) {
        for (int i = 0; i < TASK_SCENE_SIZE; i++) {
            for (int j = 0; j < DEBUG_PCMDUMP_NUM; j++) {
                pcmin_dump_array[i][j] = NULL;
            }
        }
    }
#endif

    mUseWavDump = false;
}

AudioALSACaptureDataProviderBase::~AudioALSACaptureDataProviderBase() {
    ALOGD("%s(), %p", __FUNCTION__, this);
}

status_t AudioALSACaptureDataProviderBase::preparePcmDriver(struct pcm **mPcmIn) {
    int prepare_error;

    if (*mPcmIn == NULL || pcm_is_ready(*mPcmIn) != true) {
        ASSERT(0);
        return UNKNOWN_ERROR;
    }
    prepare_error = pcm_prepare(*mPcmIn);
    if (prepare_error != 0) {
        ASSERT(0);
        pcm_close(*mPcmIn);
        *mPcmIn = NULL;
        return UNKNOWN_ERROR;
    }

    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderBase::openPcmDriver(const unsigned int device) {
    return openPcmDriverWithFlag(device, PCM_IN);
}

status_t AudioALSACaptureDataProviderBase::openPcmDriverWithFlag(const unsigned int device, unsigned int flag) {
    ALOGD("+%s(), pcm device = %d", __FUNCTION__, device);

    ASSERT(mPcm == NULL);
    mPcmflag = flag;
    mPcm = pcm_open(AudioALSADeviceParser::getInstance()->GetCardIndex(), device, flag, &mConfig);
    if (mPcm == NULL) {
        ALOGE("%s(), mPcm == NULL!!", __FUNCTION__);
    } else if (pcm_is_ready(mPcm) == false) {
        ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mPcm, pcm_get_error(mPcm));
        pcm_close(mPcm);
        mPcm = NULL;
    } else {
        if (!(mStreamAttributeSource.mAudioInputFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ)) {
            pcm_start(mPcm);
        }
    }

    if (flag & PCM_MMAP) {
        audio_pcm_read_wrapper_fp = pcm_mmap_read;
    } else {
        audio_pcm_read_wrapper_fp = pcm_read;
    }

    ALOGD("-%s(), mPcm = %p", __FUNCTION__, mPcm);
    ASSERT(mPcm != NULL);
    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderBase::closePcmDriver() {
    ALOGD("+%s(), mPcm = %p", __FUNCTION__, mPcm);

    if (mPcm != NULL) {
        pcm_stop(mPcm);
        pcm_close(mPcm);
        mPcm = NULL;
    }

    ALOGD("-%s(), mPcm = %p", __FUNCTION__, mPcm);
    return NO_ERROR;
}

void AudioALSACaptureDataProviderBase::enablePmicInputDevice(bool enable) {
    if (mCaptureDataProviderType == CAPTURE_PROVIDER_NORMAL || mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
        ALOGV("%s(+), enable=%d", __FUNCTION__, enable);
        if (enable == true) {
            if ((mHardwareResourceManager->getInputDevice() != AUDIO_DEVICE_NONE) &&
                ((mHardwareResourceManager->getInputDevice() & mStreamAttributeSource.input_device) & ~AUDIO_DEVICE_BIT_IN) == 0) {
                    mHardwareResourceManager->changeInputDevice(mStreamAttributeSource.input_device);
                } else {
                    mHardwareResourceManager->startInputDevice(mStreamAttributeSource.input_device);
                }
        } else {
            if (((mHardwareResourceManager->getInputDevice() & mStreamAttributeSource.input_device) & ~AUDIO_DEVICE_BIT_IN) != 0) {
                mHardwareResourceManager->stopInputDevice(mStreamAttributeSource.input_device);
            }
        }
    }
}

void AudioALSACaptureDataProviderBase::attach(IAudioALSACaptureDataClient *pCaptureDataClient) {
    uint32_t size = 0;
    uint32_t lowLatencyMs = 0;
    stream_attribute_t *pStreamAttr = NULL;
    audio_source_t tmpInputSource = AUDIO_SOURCE_DEFAULT;
    uint32_t i = 0;
    bool hasLowLatencyCapture = HasLowLatencyCapture();

    AL_LOCK(mEnableLock);

    // add client
    AL_LOCK(mClientLock);
    ALOGD("%s(), %p, mCaptureDataClientVector.size()=%u, Identity=%p, mCaptureDataProviderType = %d", __FUNCTION__, this,
          (uint32_t)mCaptureDataClientVector.size(),
          pCaptureDataClient->getIdentity(),
          mCaptureDataProviderType);
    mCaptureDataClientVector.add(pCaptureDataClient->getIdentity(), pCaptureDataClient);
    size = (uint32_t)mCaptureDataClientVector.size();

    // For concurrent case, we have to calculate their frames by getRawStartFrameCount()
    if (size > 1 && mCaptureDataProviderType != CAPTURE_PROVIDER_ECHOREF &&
        mCaptureDataProviderType != CAPTURE_PROVIDER_ECHOREF_BTSCO &&
        mCaptureDataProviderType != CAPTURE_PROVIDER_ECHOREF_BTCVSD &&
        mCaptureDataProviderType != CAPTURE_PROVIDER_ECHOREF_EXT) {
        int64_t time = 0;
        int64_t frameCount = 0;
        getCapturePosition(&frameCount, &time);
        pCaptureDataClient->setStreamAttributeTargetDSP(mStreamAttributeTargetDSP);
        pCaptureDataClient->setRawStartFrameCount(frameCount);
    }
    AL_UNLOCK(mClientLock);

    // open pcm interface when 1st attach
    if (size == 1) {
        mOpenIndex++;
        open();
        mCurTopSource = mStreamAttributeSource.input_source;
    } else {
        if (mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
            lowLatencyMs = UPLINK_HIFI3_LOW_LATENCY_MS;
            pStreamAttr = &mStreamAttributeTargetDSP; // for after dsp process read size change
        } else {
            lowLatencyMs = UPLINK_LOW_LATENCY_MS;
            pStreamAttr = &mStreamAttributeSource;
        }
        if (!hasLowLatencyCapture && pCaptureDataClient->IsLowLatencyCapture()) {
            // update HW interrupt rate by HW sample rate
            updateReadSize(getPeriodBufSize(pStreamAttr, UPLINK_NORMAL_LATENCY_MS) *
                           lowLatencyMs / UPLINK_NORMAL_LATENCY_MS);
            if (mCaptureDataProviderType != CAPTURE_PROVIDER_DSP) {
                mHardwareResourceManager->setULInterruptRate(mStreamAttributeSource.sample_rate *
                                                             lowLatencyMs / 1000);
            }
#ifdef MTK_AUDIODSP_SUPPORT
            else if (isAdspOptionEnable()) {
                AudioDspStreamManager::getInstance()->UpdateCaptureDspLatency();
            }
#endif

            ALOGD("%s(), setULInterruptRate = %d", __FUNCTION__, mStreamAttributeSource.sample_rate * lowLatencyMs / 1000);
        }
        enablePmicInputDevice(true);

        if ((mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) && (pCaptureDataClient->getStreamAttributeTarget() != NULL)) {
            tmpInputSource = pCaptureDataClient->getStreamAttributeTarget()->input_source;
            if ((tmpInputSource == AUDIO_SOURCE_VOICE_COMMUNICATION) &&
                (mCurTopSource != AUDIO_SOURCE_VOICE_COMMUNICATION)) {
                pCaptureDataClient->setStreamInReopen(true);
                ALOGE("%s(), should be first dataprovider, re-open! input_source: %d, vector size:%d",
                      __FUNCTION__, tmpInputSource, size);
            } else if (source_priority(mCurTopSource) < source_priority(tmpInputSource)) {
                ALOGD("%s(), update input source %d -> %d", __FUNCTION__, mCurTopSource, tmpInputSource);
                mCurTopSource = tmpInputSource;
                mStreamAttributeSource.input_source = mCurTopSource;
                updateInputSource();
            }
        }
    }

    AL_UNLOCK(mEnableLock);
    ALOGV("%s(-), size=%u", __FUNCTION__, size);
}


void AudioALSACaptureDataProviderBase::detach(IAudioALSACaptureDataClient *pCaptureDataClient) {
    uint32_t size = 0;
    uint32_t lowLatencyMs = 0;
    audio_source_t tmpTopSource = AUDIO_SOURCE_DEFAULT, tmpInputSource = AUDIO_SOURCE_DEFAULT;
    stream_attribute_t *pStreamAttr = NULL;
    uint32_t i = 0;
    bool hasLowLatencyCaptureOld = false;
    bool hasLowLatencyCaptureNow = false;

    AL_LOCK(mEnableLock);

    hasLowLatencyCaptureOld = HasLowLatencyCapture();

    // remove client
    AL_LOCK(mClientLock);
    ALOGD("%s(), %p, mCaptureDataClientVector.size()=%u, Identity=%p", __FUNCTION__, this,
          (uint32_t)mCaptureDataClientVector.size(),
          pCaptureDataClient->getIdentity());

    mCaptureDataClientVector.removeItem(pCaptureDataClient->getIdentity());
    size = (uint32_t)mCaptureDataClientVector.size();
    AL_UNLOCK(mClientLock);

    // close pcm interface when there is no client attached
    if (size == 0) {
        close();
        mCurTopSource = AUDIO_SOURCE_DEFAULT;
        AL_LOCK(mTimeStampLock);
        mCaptureFramesReaded = 0; // clear timestamp when all detatch
        AL_UNLOCK(mTimeStampLock);
    } else {
        hasLowLatencyCaptureNow = HasLowLatencyCapture();
        if (mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
            lowLatencyMs = UPLINK_HIFI3_LOW_LATENCY_MS;
            pStreamAttr = &mStreamAttributeTargetDSP; // for after dsp process read size change
        } else {
            lowLatencyMs = UPLINK_LOW_LATENCY_MS;
            pStreamAttr = &mStreamAttributeSource;
        }
        if ((hasLowLatencyCaptureOld == true) && (hasLowLatencyCaptureNow == false)) {
            // update HW interrupt rate by HW sample rate
            updateReadSize(getPeriodBufSize(pStreamAttr, lowLatencyMs) *
                           UPLINK_NORMAL_LATENCY_MS / lowLatencyMs);
            if (mCaptureDataProviderType != CAPTURE_PROVIDER_DSP) {
                mHardwareResourceManager->setULInterruptRate(mStreamAttributeSource.sample_rate *
                                                             UPLINK_NORMAL_LATENCY_MS / 1000);
            }
#ifdef MTK_AUDIODSP_SUPPORT
            else if (isAdspOptionEnable()) {
                AudioDspStreamManager::getInstance()->UpdateCaptureDspLatency();
            }
#endif
            ALOGD("%s(), setULInterruptRate = %d", __FUNCTION__,
                  mStreamAttributeSource.sample_rate * UPLINK_NORMAL_LATENCY_MS / 1000);
        }
        if (mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
            for (i = 0; i < mCaptureDataClientVector.size(); i++) {
                if (mCaptureDataClientVector[i] == NULL ||
                    mCaptureDataClientVector[i]->getStreamAttributeTarget() == NULL) {
                    ALOGE("%s(), ptr is NULL!!", __FUNCTION__);
                    continue;
                }
                tmpInputSource = mCaptureDataClientVector[i]->getStreamAttributeTarget()->input_source;
                if (source_priority(tmpTopSource) < source_priority(tmpInputSource)) {
                    tmpTopSource = tmpInputSource;
                }
            }
            if (tmpTopSource != mCurTopSource) {
                ALOGD("%s(), update input source %d -> %d", __FUNCTION__, mCurTopSource, tmpTopSource);
                mCurTopSource = tmpTopSource;
                mStreamAttributeSource.input_source = mCurTopSource;
                updateInputSource();
            }
        }
    }

    enablePmicInputDevice(false);

    AL_UNLOCK(mEnableLock);
    ALOGV("%s(-), size=%u", __FUNCTION__, size);
}


void AudioALSACaptureDataProviderBase::provideCaptureDataToAllClients(const uint32_t open_index) {
    ALOGV("+%s()", __FUNCTION__);

    if (open_index != mOpenIndex) {
        ALOGD("%s(), open_index(%d) != mOpenIndex(%d), return", __FUNCTION__, open_index, mOpenIndex);
        return;
    }

    IAudioALSACaptureDataClient *pCaptureDataClient = NULL;

    if (mUseWavDump) {
        writeWavDumpData();
    } else {
        WritePcmDumpData();
    }

    AL_LOCK(mClientLock);
    for (size_t i = 0; i < mCaptureDataClientVector.size(); i++) {
        pCaptureDataClient = mCaptureDataClientVector[i];
        pCaptureDataClient->copyCaptureDataToClient(mPcmReadBuf);
    }
    AL_UNLOCK(mClientLock);

    ALOGV("-%s()", __FUNCTION__);
}


bool AudioALSACaptureDataProviderBase::isNeedSyncPcmStart() {
    bool retval = false;

    AL_LOCK(mClientLock);
    retval = (mCaptureDataClientVector.size() == 0)
             ? false
             : mCaptureDataClientVector[0]->isNeedSyncPcmStart();
    AL_UNLOCK(mClientLock);

    return retval;
}


void AudioALSACaptureDataProviderBase::signalPcmStart() { /* for client */
    AL_LOCK(mStartLock);

    if (mStart == true || mPcm == NULL || isNeedSyncPcmStart() == false) {
        AL_UNLOCK(mStartLock);
        return;
    }

    AL_SIGNAL(mStartLock);
    AL_UNLOCK(mStartLock);
}

int AudioALSACaptureDataProviderBase::pcmRead(struct pcm *mpcm, void *data, unsigned int count) {
    return audio_pcm_read_wrapper_fp(mpcm, data, count);
}

void AudioALSACaptureDataProviderBase::waitPcmStart() { /* for read thread */
    int wait_result = 0;

    AL_LOCK(mStartLock);

    mReadThreadReady = true;

    if (mStart == true || mPcm == NULL) {
        AL_UNLOCK(mStartLock);
        return;
    }

    if (isNeedSyncPcmStart() == true) {
        wait_result = AL_WAIT_MS(mStartLock, 100);
        if (wait_result != 0) {
            ALOGW("%s() wait fail", __FUNCTION__);
        }
    }

    ALOGD("pcm_start");
    pcm_start(mPcm);
    mStart = true;
    AL_UNLOCK(mStartLock);
}


bool AudioALSACaptureDataProviderBase::HasLowLatencyCapture(void) {
    bool bRet = false;
    IAudioALSACaptureDataClient *pCaptureDataClient = NULL;

    AL_LOCK(mClientLock);
    for (size_t i = 0; i < mCaptureDataClientVector.size(); i++) {
        pCaptureDataClient = mCaptureDataClientVector[i];
        if (pCaptureDataClient->IsLowLatencyCapture()) {
            bRet = true;
            break;
        }
    }
    AL_UNLOCK(mClientLock);

    ALOGV("%s(), bRet=%d", __FUNCTION__, bRet);
    return bRet;
}

void AudioALSACaptureDataProviderBase::setThreadPriority(void) {
#ifdef UPLINK_LOW_LATENCY
    if (HasLowLatencyCapture()) {
        audio_sched_setschedule(0, SCHED_RR, sched_get_priority_min(SCHED_RR));
    } else
#endif
    {
        audio_sched_setschedule(0, SCHED_NORMAL, sched_get_priority_max(SCHED_NORMAL));
    }
}

void AudioALSACaptureDataProviderBase::OpenPCMDump(const char *class_name) {
    ALOGV("%s(), mCaptureDataProviderType=%d", __FUNCTION__, mCaptureDataProviderType);
    char mDumpFileName[128];
    int ret = 0;

    if (!AudioSmartPaController::getInstance()->isInCalibration()) {
        ret = snprintf(mDumpFileName, sizeof(mDumpFileName) - 1, "%s%d.%s.%d.%s.%dch.pcm",
                       streamin, mDumpFileNum, class_name,
                       mStreamAttributeSource.sample_rate,
                       transferAudioFormatToDumpString(mStreamAttributeSource.audio_format),
                       mStreamAttributeSource.num_channels);
        if (ret < 0) {
            ALOGE("%s(), snprintf mDumpFileName fail!!", __FUNCTION__);
        }

    } else {
        ret = snprintf(mDumpFileName, sizeof(mDumpFileName) - 1, "%s.%s.pcm",
                       streamin, class_name);
        if (ret < 0) {
            ALOGE("%s(), snprintf mDumpFileName fail!!", __FUNCTION__);
        }
    }

    mPCMDumpFile = NULL;
    mPCMDumpFile = AudioOpendumpPCMFile(mDumpFileName, streamin_propty);


    char mDumpFileName4ch[128];
    if (mConfig.channels == 4) {
        ret = snprintf(mDumpFileName4ch, sizeof(mDumpFileName4ch) - 1, "%s%d.%s_4ch.%d.%s.%dch.pcm",
                       streamin, mDumpFileNum, class_name,
                       mStreamAttributeSource.sample_rate,
                       transferAudioFormatToDumpString(mStreamAttributeSource.audio_format),
                       mStreamAttributeSource.num_channels);
        if (ret < 0) {
            ALOGE("%s(), snprintf mDumpFileName4ch fail!!", __FUNCTION__);
        }

        mPCMDumpFile4ch = AudioOpendumpPCMFile(mDumpFileName4ch, streamin_propty);
        if (mPCMDumpFile4ch != NULL) {
            ALOGD("%s mDumpFileName4ch = %s", __FUNCTION__, mDumpFileName4ch);
        }
    }

    if (mPCMDumpFile != NULL) {
        ALOGD("%s DumpFileName = %s", __FUNCTION__, mDumpFileName);

        mDumpFileNum++;
        mDumpFileNum %= MAX_DUMP_NUM;
    }
}

void AudioALSACaptureDataProviderBase::ClosePCMDump() {
    if (mPCMDumpFile) {
        AudioCloseDumpPCMFile(mPCMDumpFile);
        ALOGD("%s(), mCaptureDataProviderType=%d", __FUNCTION__, mCaptureDataProviderType);
        mPCMDumpFile = NULL;
    }

    if (mPCMDumpFile4ch) {
        AudioCloseDumpPCMFile(mPCMDumpFile4ch);
        mPCMDumpFile4ch = NULL;
    }
}

void  AudioALSACaptureDataProviderBase::WritePcmDumpData(void) {

    if (mPCMDumpFile) {
        //ALOGD("%s()", __FUNCTION__);
        if (mPcmReadBuf.pWrite >= mPcmReadBuf.pRead) {
            AudioDumpPCMData((void *)mPcmReadBuf.pRead, mPcmReadBuf.pWrite - mPcmReadBuf.pRead, mPCMDumpFile);
        } else {
            AudioDumpPCMData((void *)mPcmReadBuf.pRead, mPcmReadBuf.pBufEnd - mPcmReadBuf.pRead, mPCMDumpFile);
            AudioDumpPCMData((void *)mPcmReadBuf.pBufBase, mPcmReadBuf.pWrite - mPcmReadBuf.pBufBase, mPCMDumpFile);
        }
    }
}

void AudioALSACaptureDataProviderBase::openWavDump(const char *class_name) {
    ALOGV("%s(), mCaptureDataProviderType = %d", __FUNCTION__, mCaptureDataProviderType);
    char mDumpFileName[256];
    int ret = 0;

    char timep_str[32];
    getCurrentTimestamp(timep_str, sizeof(timep_str));


    uint32_t num_channels;
    uint32_t sample_rate;
    audio_format_t audio_format;

    if (mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
        audio_format = mStreamAttributeTargetDSP.audio_format,
        num_channels = mStreamAttributeTargetDSP.num_channels,
        sample_rate = mStreamAttributeTargetDSP.sample_rate;
    } else {
        audio_format = mStreamAttributeSource.audio_format;
        num_channels = mStreamAttributeSource.num_channels;
        sample_rate = mStreamAttributeSource.sample_rate;
    }

    if (!AudioSmartPaController::getInstance()->isInCalibration()) {
        ret = snprintf(mDumpFileName, sizeof(mDumpFileName) - 1, "%s%d.%s.%d.%s.%dch_%s.wav",
                       streamin, mDumpFileNum, class_name,
                       sample_rate,
                       transferAudioFormatToDumpString(audio_format),
                       num_channels, timep_str);
        if (ret < 0) {
            ALOGE("%s(), snprintf mDumpFileName fail!!", __FUNCTION__);
        }

    } else {
        ret = snprintf(mDumpFileName, sizeof(mDumpFileName) - 1, "%s.%s_%s.wav",
                       streamin, class_name, timep_str);
        if (ret < 0) {
            ALOGE("%s(), snprintf mDumpFileName fail!!", __FUNCTION__);
        }
    }

    AL_LOCK(mAudioDumpLock);
    mPCMDumpFile = NULL;
    mPCMDumpFile = AudioOpendumpPCMFile(mDumpFileName, streamin_propty);
    mBytesWavDumpWritten = 0;

    if (mPCMDumpFile != NULL) {
        ALOGD("%s(), DumpFileName = %s, mCaptureDataProviderType = %d",
              __FUNCTION__, mDumpFileName, mCaptureDataProviderType);
        updateWavDumpHeader(false);
        mDumpFileNum++;
        mDumpFileNum %= MAX_DUMP_NUM;
    }

    char mDumpFileName4ch[256];
    mBytesWavDumpWritten4ch = 0;

    if (mConfig.channels == 4) {
        ret = snprintf(mDumpFileName4ch, sizeof(mDumpFileName4ch) - 1, "%s%d.%s_4ch.%d.%s.%dch_%s.wav",
                       streamin, mDumpFileNum, class_name,
                       sample_rate,
                       transferAudioFormatToDumpString(audio_format),
                       num_channels, timep_str);
        if (ret < 0) {
            ALOGE("%s(), snprintf mDumpFileName4ch fail!!", __FUNCTION__);
        }

        mPCMDumpFile4ch = AudioOpendumpPCMFile(mDumpFileName4ch, streamin_propty);
        if (mPCMDumpFile4ch != NULL) {
            ALOGD("%s(), mDumpFileName4ch = %s, mCaptureDataProviderType = %d",
                  __FUNCTION__, mDumpFileName4ch, mCaptureDataProviderType);
            updateWavDumpHeader(true);
        }
    }
    AL_UNLOCK(mAudioDumpLock);

}

void AudioALSACaptureDataProviderBase::closeWavDump() {

    AL_LOCK(mAudioDumpLock);
    if (mPCMDumpFile4ch) {
        updateWavDumpHeader(true);
        AudioCloseDumpPCMFile(mPCMDumpFile4ch);
        mBytesWavDumpWritten4ch = 0;
        mPCMDumpFile4ch = NULL;
    }

    if (mPCMDumpFile) {
        updateWavDumpHeader(false);
        AudioCloseDumpPCMFile(mPCMDumpFile);

        if (mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
            ALOGD("%s(), mCaptureDataProviderType = %d, mBytesWavDumpWritten = %d, format = %d, channels = %d, sample_rate = %d",
                  __FUNCTION__, mCaptureDataProviderType,
                  mBytesWavDumpWritten,
                  mStreamAttributeTargetDSP.audio_format,
                  mStreamAttributeTargetDSP.num_channels,
                  mStreamAttributeTargetDSP.sample_rate);
        } else {
            ALOGD("%s(), mCaptureDataProviderType = %d, mBytesWavDumpWritten = %d, format = %d, channels = %d, sample_rate = %d",
                  __FUNCTION__, mCaptureDataProviderType,
                  mBytesWavDumpWritten,
                  mStreamAttributeSource.audio_format,
                  mStreamAttributeSource.num_channels,
                  mStreamAttributeSource.sample_rate);
        }
        mBytesWavDumpWritten = 0;
        mPCMDumpFile = NULL;
    }
    AL_UNLOCK(mAudioDumpLock);
}

void  AudioALSACaptureDataProviderBase::writeWavDumpData(void) {

    if (mPCMDumpFile) {
        //ALOGD("%s()", __FUNCTION__);
        if (mPcmReadBuf.pWrite >= mPcmReadBuf.pRead) {
            AudioDumpPCMData((void *)mPcmReadBuf.pRead, mPcmReadBuf.pWrite - mPcmReadBuf.pRead, mPCMDumpFile);
            mBytesWavDumpWritten += (mPcmReadBuf.pWrite - mPcmReadBuf.pRead);
        } else {
            AudioDumpPCMData((void *)mPcmReadBuf.pRead, mPcmReadBuf.pBufEnd - mPcmReadBuf.pRead, mPCMDumpFile);
            mBytesWavDumpWritten += (mPcmReadBuf.pBufEnd - mPcmReadBuf.pRead);
            AudioDumpPCMData((void *)mPcmReadBuf.pBufBase, mPcmReadBuf.pWrite - mPcmReadBuf.pBufBase, mPCMDumpFile);
            mBytesWavDumpWritten += (mPcmReadBuf.pWrite - mPcmReadBuf.pBufBase);
        }
        updateWavDumpHeader(false);
    }
}

bool AudioALSACaptureDataProviderBase::isWavDumpEnabled() {
    AL_AUTOLOCK(mAudioDumpLock);
    return (mPCMDumpFile || mPCMDumpFile4ch) ? true : false;
}

void AudioALSACaptureDataProviderBase::updateWavDumpHeader(bool is4ch) {

    if (mCaptureDataProviderType == CAPTURE_PROVIDER_DSP) {
        UpdateWaveHeader((is4ch) ? mPCMDumpFile4ch : mPCMDumpFile,
                         (is4ch) ? mBytesWavDumpWritten4ch : mBytesWavDumpWritten,
                         mStreamAttributeTargetDSP.audio_format,
                         mStreamAttributeTargetDSP.num_channels,
                         mStreamAttributeTargetDSP.sample_rate);
    } else {
        UpdateWaveHeader((is4ch) ? mPCMDumpFile4ch : mPCMDumpFile,
                         (is4ch) ? mBytesWavDumpWritten4ch : mBytesWavDumpWritten,
                         mStreamAttributeSource.audio_format,
                         mStreamAttributeSource.num_channels,
                         mStreamAttributeSource.sample_rate);
    }
}

void AudioALSACaptureDataProviderBase::dynamicSetCaptureDataProviderAudioDump() {

    bool isWavDumping = isWavDumpEnabled();

    if (isWavDumping == false) {
        ALOGD("%s(), isWavDumping = %d", __FUNCTION__, isWavDumping);
        openWavDump(LOG_TAG);
    } else {
        ALOGD("%s(), mPCMDumpFile already exist, BYPASS!!!", __FUNCTION__);
    }
}

#ifdef MTK_AUDIODSP_SUPPORT
void  AudioALSACaptureDataProviderBase::OpenPCMDumpDsp(const char *className, uint8_t task_scene) {
#define MAX_TASKNAME_LEN (128)

    const char *audio_dump = "/data/vendor/audiohal/audio_dump";

    char mDumpFileName[128];
    char task_name[MAX_TASKNAME_LEN];
    char value[PROPERTY_VALUE_MAX] = {0};
    uint8_t pcmdump_task_id = task_scene;
    int i, dsp_taskdump_property = 0, ret = 0;
    struct ipi_msg_t ipi_msg;
    FILE *pcm_dump = NULL;
    property_get(streamindsp_propty, value, "0");
    dsp_taskdump_property = atoi(value);

    ALOGD("dsp_taskdump_property = %d", dsp_taskdump_property);

    switch (task_scene) {
        case TASK_SCENE_CAPTURE_UL1:
            ret = snprintf(task_name, MAX_TASKNAME_LEN, "%s", "capture_ul1");
            break;
        case TASK_SCENE_CAPTURE_RAW:
            ret = snprintf(task_name, MAX_TASKNAME_LEN, "%s", "capture_raw");
            break;
        default:
            ret = snprintf(task_name, MAX_TASKNAME_LEN, "%s", "TaskUnKnown");
            break;
    }
    if (ret < 0) {
         ALOGE("%s(), snprintf task_name fail!!", __FUNCTION__);
    }

    if (isAdspTaskDumpEnable(dsp_taskdump_property, task_scene)) {
        for (i = 0; i < DEBUG_PCMDUMP_NUM; i++) {
            ret = snprintf(mDumpFileName, sizeof(mDumpFileName), "%s/%s.%d.%d.%d.%s_point%d.pcm",
                           audio_dump, className, mDumpFileNumDsp, getpid(), gettid(), task_name, i);
            if (ret < 0) {
                ALOGE("%s(), snprintf mDumpFileName fail!!", __FUNCTION__);
            }
            mPCMDumpFileDsp = AudioOpendumpPCMFile(mDumpFileName, streamindsp_propty);
            if (mPCMDumpFileDsp != NULL) {
                ALOGD("%s DumpFileName = %s", __FUNCTION__, mDumpFileName);
            }
            set_task_pcmdump_info(pcmdump_task_id, i, (void *)mPCMDumpFileDsp);
        }

        // send PCM_DUMP_ENABLE ipi to DSP
        mAudioMessengerIPI->sendIpiMsg(
            &ipi_msg,
            pcmdump_task_id, AUDIO_IPI_LAYER_TO_DSP,
            AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
            AUDIO_DSP_TASK_PCMDUMP_ON, 1, 0,
            NULL);


        for (i = 0; i < DEBUG_PCMDUMP_NUM; i++) {
            get_task_pcmdump_info(pcmdump_task_id, i, (void **)&pcm_dump);
            if (pcm_dump == NULL) {
                int ret = 0;
                ret = snprintf(mDumpFileName, sizeof(mDumpFileName), "%s/%s.%d.%d.%d.%s_point%d.pcm",
                               audio_dump, className, mDumpFileNumDsp, getpid(), gettid(), task_name, i);
                if (ret < 0) {
                    ALOGE("%s(), snprintf mDumpFileName fail!!", __FUNCTION__);
                }
                mPCMDumpFileDsp = AudioOpendumpPCMFile(mDumpFileName, streamindsp_propty);
                if (mPCMDumpFileDsp != NULL) {
                    ALOGD("%s DumpFileName = %s", __FUNCTION__, mDumpFileName);
                }
                set_task_pcmdump_info(pcmdump_task_id, i, (void *)mPCMDumpFileDsp);
            }
        }

        mDumpFileNumDsp++;
        mDumpFileNumDsp %= MAX_DUMP_NUM;
    } else {
        mAudioMessengerIPI->sendIpiMsg(
            &ipi_msg,
            pcmdump_task_id, AUDIO_IPI_LAYER_TO_DSP,
            AUDIO_IPI_MSG_ONLY, AUDIO_IPI_MSG_BYPASS_ACK,
            AUDIO_DSP_TASK_PCMDUMP_ON, 0, 0,
            NULL);
    }

}

void  AudioALSACaptureDataProviderBase::ClosePCMDumpDsp(uint8_t task_scene) {
    char value[PROPERTY_VALUE_MAX];
    uint8_t pcmdump_task_id = task_scene;
    int dsp_taskdump_property;
    FILE *pcm_dump = NULL;
    int i;

    ALOGD("%s", __FUNCTION__);


    for (i = 0; i < DEBUG_PCMDUMP_NUM; i++) {
        get_task_pcmdump_info(pcmdump_task_id, i, (void **)&pcm_dump);
        if (pcm_dump != NULL) {
            ALOGD("%s AudioCloseDumpPCMFile", __FUNCTION__);
            AudioCloseDumpPCMFile(pcm_dump);
            set_task_pcmdump_info(pcmdump_task_id, i, NULL);
        }
    }
}

bool AudioALSACaptureDataProviderBase::isAdspTaskDumpEnable(int property, uint8_t taskScene) {
    int ret = false;

    if (!property) {
        return ret;
    }

    /* adsp dumps are too many. Default not to dump
     * capture raw in adsp to avode ipc busy.
     */
    if ((property & ADSP_DUMP_TASK_DEFAULT_ENABLE) &&
         taskScene != TASK_SCENE_CAPTURE_RAW) {
         return true;
    }

    switch (taskScene) {
        case TASK_SCENE_CAPTURE_RAW:
            if (property & ADSP_DUMP_TASK_CAPTURE_RAW_ENABLE) {
                ret = true;
            }
            break;
        default:
            ALOGW("%s(), taskScene %d not supported", __FUNCTION__, taskScene);
            ret = false;
            break;
    }

    return ret;
}


void AudioALSACaptureDataProviderBase::get_task_pcmdump_info(uint32_t task_id, uint32_t param, void **pcm_dump) {
    *pcm_dump = pcmin_dump_array[task_id][param];
    ALOGV("%s() %p %d %d\n", __FUNCTION__, *((FILE **)pcm_dump), task_id, param);
}

void AudioALSACaptureDataProviderBase::set_task_pcmdump_info(uint32_t task_id, uint32_t param, void *pcm_dump) {
    pcmin_dump_array[task_id][param] = (FILE *)pcm_dump;
    ALOGD("%s() %p %d %d\n", __FUNCTION__, pcmin_dump_array[param], task_id, param);
}

void AudioALSACaptureDataProviderBase::processDmaMsg(
    struct ipi_msg_t *msg,
    void *buf,
    uint32_t size) {
    FILE *pcm_dump = NULL;

    ALOGV("%s() msg_id=0x%x, task_scene=%d, param2=0x%x, size=%d\n",
          __FUNCTION__, msg->msg_id, msg->task_scene, msg->param2, size);

    switch (msg->msg_id) {
    case AUDIO_DSP_TASK_PCMDUMP_DATA:
        get_task_pcmdump_info(msg->task_scene, msg->param2, (void **)&pcm_dump);
        if (pcm_dump != NULL) {
            AudioDumpPCMData(buf, size, pcm_dump);
        }
        break;
    default:
        break;
    }
}

void AudioALSACaptureDataProviderBase::processDmaMsgWrapper(
    struct ipi_msg_t *msg,
    void *buf,
    uint32_t size,
    void *arg) {

    AudioALSACaptureDataProviderBase *pAudioALSACaptureDataProviderBase =
        static_cast<AudioALSACaptureDataProviderBase *>(arg);

    if (pAudioALSACaptureDataProviderBase != NULL) {
        pAudioALSACaptureDataProviderBase->processDmaMsg(msg, buf, size);
    }
}
#endif /* MTK_AUDIODSP_SUPPORT */

//echoref+++
void AudioALSACaptureDataProviderBase::provideEchoRefCaptureDataToAllClients(const uint32_t open_index) {
    ALOGV("+%s()", __FUNCTION__);

    if (open_index != mOpenIndex) {
        ALOGD("%s(), open_index(%d) != mOpenIndex(%d), return", __FUNCTION__, open_index, mOpenIndex);
        return;
    }

    IAudioALSACaptureDataClient *pCaptureDataClient = NULL;

    WritePcmDumpData();

    AL_LOCK(mClientLock);
    for (size_t i = 0; i < mCaptureDataClientVector.size(); i++) {
        pCaptureDataClient = mCaptureDataClientVector[i];
        pCaptureDataClient->copyEchoRefCaptureDataToClient(mPcmReadBuf);
    }
    AL_UNLOCK(mClientLock);


    ALOGV("-%s()", __FUNCTION__);
}
//echoref---


status_t AudioALSACaptureDataProviderBase::GetCaptureTimeStamp(time_info_struct_t *Time_Info, unsigned int read_size) {
    status_t retval = NO_ERROR;

    ALOGV("%s()", __FUNCTION__);
    if (mPcm == NULL) {
        ASSERT(mPcm != NULL);
        retval = INVALID_OPERATION;
        return retval;
    }

    long ret_ns;
    size_t avail;
    Time_Info->timestamp_get.tv_sec  = 0;
    Time_Info->timestamp_get.tv_nsec = 0;
    Time_Info->frameInfo_get = 0;
    Time_Info->buffer_per_time = 0;
    Time_Info->kernelbuffer_ns = 0;

    //ALOGD("%s(), Going to check pcm_get_htimestamp", __FUNCTION__);
    int ret = pcm_get_htimestamp(mPcm, &Time_Info->frameInfo_get, &Time_Info->timestamp_get);
    if (ret == 0) {
        Time_Info->buffer_per_time = pcm_bytes_to_frames(mPcm, read_size);
        Time_Info->kernelbuffer_ns = 1000000000 / mStreamAttributeSource.sample_rate * (Time_Info->buffer_per_time + Time_Info->frameInfo_get);
        Time_Info->total_frames_readed += Time_Info->buffer_per_time;
        ALOGV("%s(), pcm_get_htimestamp sec= %ld, nsec=%ld, frameInfo_get = %u, buffer_per_time=%u, ret_ns = %lu, read_size = %u\n",
              __FUNCTION__, Time_Info->timestamp_get.tv_sec, Time_Info->timestamp_get.tv_nsec, Time_Info->frameInfo_get,
              Time_Info->buffer_per_time, Time_Info->kernelbuffer_ns, read_size);

        // Write time stamp to cache to avoid getCapturePosition performance issue
        AL_LOCK(mTimeStampLock);
        mCaptureFramesReaded = Time_Info->total_frames_readed;
        mCaptureTimeStamp = Time_Info->timestamp_get;
        AL_UNLOCK(mTimeStampLock);
#if 0
        if ((TimeStamp->tv_nsec - ret_ns) >= 0) {
            TimeStamp->tv_nsec -= ret_ns;
        } else {
            TimeStamp->tv_sec -= 1;
            TimeStamp->tv_nsec = 1000000000 + TimeStamp->tv_nsec - ret_ns;
        }

        ALOGD("%s calculate pcm_get_htimestamp sec= %ld, nsec=%ld, avail = %d, ret_ns = %ld\n",
              __FUNCTION__, TimeStamp->tv_sec, TimeStamp->tv_nsec, avail, ret_ns);
#endif
    } else {
        ALOGE("%s pcm_get_htimestamp fail, ret: %d, pcm_get_error: %s, time: %lld.%.9ld, frameInfo_get = %u",
              __FUNCTION__, ret, pcm_get_error(mPcm),
              (long long)Time_Info->timestamp_get.tv_sec,
              Time_Info->timestamp_get.tv_nsec,
              Time_Info->frameInfo_get);
        retval = UNKNOWN_ERROR;
    }
    return retval;
}


status_t AudioALSACaptureDataProviderBase::calculateCaptureTimeStamp(time_info_struct_t *Time_Info, unsigned int read_size) {
    ALOGV("%s()", __FUNCTION__);

    Time_Info->timestamp_get.tv_sec  = 0;
    Time_Info->timestamp_get.tv_nsec = 0;
    Time_Info->frameInfo_get = 0;
    Time_Info->buffer_per_time = 0;
    Time_Info->kernelbuffer_ns = 0;

    calculateTimeStampByBytes(mCaptureStartTime, mTotalReadBytes, mStreamAttributeSource, &Time_Info->timestamp_get);
    Time_Info->total_frames_readed = mTotalReadBytes / audio_bytes_per_frame(mStreamAttributeSource.num_channels, mStreamAttributeSource.audio_format);
    ALOGV("%s(), pcm_get_htimestamp sec= %ld, nsec=%ld, frameInfo_get = %u, buffer_per_time=%u, ret_ns = %lu, read_size = %u\n",
          __FUNCTION__, Time_Info->timestamp_get.tv_sec, Time_Info->timestamp_get.tv_nsec, Time_Info->frameInfo_get,
          Time_Info->buffer_per_time, Time_Info->kernelbuffer_ns, read_size);

    // Write time stamp to cache to avoid getCapturePosition performance issue
    AL_LOCK(mTimeStampLock);
    mCaptureFramesReaded = Time_Info->total_frames_readed;
    mCaptureTimeStamp = Time_Info->timestamp_get;
    AL_UNLOCK(mTimeStampLock);

    ALOGV("%s(), mCaptureStartTime %ld.%09ld, mTotalReadBytes %d, timestamp_get %ld.%09ld",
          __FUNCTION__, mCaptureStartTime.tv_sec, mCaptureStartTime.tv_nsec, mTotalReadBytes, Time_Info->timestamp_get.tv_sec, Time_Info->timestamp_get.tv_nsec);

    return NO_ERROR;
}


status_t AudioALSACaptureDataProviderBase::updateStartTimeStamp() {
    if (mCaptureStartTime.tv_sec == 0 && mCaptureStartTime.tv_nsec == 0) {
        if (clock_gettime(CLOCK_MONOTONIC, &mCaptureStartTime) == 0) {
            ALOGD("%s(), Set start timestamp (%ld.%09ld), mTotalReadBytes = %d",
                  __FUNCTION__,
                  mCaptureStartTime.tv_sec,
                  mCaptureStartTime.tv_nsec,
                  mTotalReadBytes);
        } else {
            ALOGW("Cannot get system time\n");
        }
    } else {
        ALOGV("%s(), Set start timestamp (%ld.%09ld), mTotalReadBytes = %d",
              __FUNCTION__,
              mCaptureStartTime.tv_sec,
              mCaptureStartTime.tv_nsec,
              mTotalReadBytes);
    }

    return NO_ERROR;
}


void AudioALSACaptureDataProviderBase::configStreamAttribute(const stream_attribute_t *attribute) {
    AL_LOCK(mEnableLock);

    ALOGD("%s(), audio_mode: %d => %d, input_device: 0x%x => 0x%x, flag: 0x%x => 0x%x, "
          "input_source: %d->%d, output_device: 0x%x => 0x%x, sample_rate: %d => %d, period_us: %u => %u, "
          "DSP out sample_rate: %d => %d",
          __FUNCTION__,
          mStreamAttributeSource.audio_mode, attribute->audio_mode,
          mStreamAttributeSource.input_device, attribute->input_device,
          mStreamAttributeSource.mAudioInputFlags, attribute->mAudioInputFlags,
          mStreamAttributeSource.input_source, attribute->input_source,
          mStreamAttributeSource.output_devices, attribute->output_devices,
          mStreamAttributeSource.sample_rate, attribute->sample_rate,
          mStreamAttributeSource.periodUs, attribute->periodUs,
          mStreamAttributeTargetDSP.sample_rate, attribute->sample_rate);

    if (mEnable == false) {
        mStreamAttributeSource.audio_mode = attribute->audio_mode;
        mStreamAttributeSource.input_device = attribute->input_device;
        mStreamAttributeSource.mAudioInputFlags = attribute->mAudioInputFlags;
        mStreamAttributeSource.input_source = attribute->input_source;
        mStreamAttributeSource.output_devices = attribute->output_devices;
        mStreamAttributeSource.mVoIPEnable = attribute->mVoIPEnable;

        mStreamAttributeSource.isIEMsSource = attribute->isIEMsSource;
        mStreamAttributeSource.periodUs = attribute->periodUs;
        mStreamAttributeSource.periodCnt = attribute->periodCnt;

        mStreamAttributeTargetDSP.audio_mode = attribute->audio_mode;
        mStreamAttributeTargetDSP.input_device = attribute->input_device;
        mStreamAttributeTargetDSP.mAudioInputFlags = attribute->mAudioInputFlags;
        mStreamAttributeTargetDSP.input_source = attribute->input_source;
        mStreamAttributeTargetDSP.output_devices = attribute->output_devices;
        mStreamAttributeTargetDSP.mVoIPEnable = attribute->mVoIPEnable;
        mStreamAttributeTargetDSP.sample_rate = attribute->sample_rate;
        mStreamAttributeTargetDSP.num_channels = attribute->num_channels;
        mStreamAttributeTargetDSP.NativePreprocess_Info = attribute->NativePreprocess_Info;

        if (mStreamAttributeSource.mAudioInputFlags & AUDIO_INPUT_FLAG_MMAP_NOIRQ) {
            mStreamAttributeSource.audio_format = (attribute->audio_format == AUDIO_FORMAT_PCM_32_BIT) ?
                                                  AUDIO_FORMAT_PCM_8_24_BIT : AUDIO_FORMAT_PCM_16_BIT;
            mStreamAttributeSource.audio_channel_mask = attribute->audio_channel_mask;
            mStreamAttributeSource.num_channels = attribute->num_channels;
            mStreamAttributeSource.sample_rate = attribute->sample_rate;
        }
    } else {
        ALOGW("%s(), already enabled!!", __FUNCTION__);
    }

    AL_UNLOCK(mEnableLock);
}


int AudioALSACaptureDataProviderBase::getCapturePosition(int64_t *frames, int64_t *time) {
    AL_LOCK(mTimeStampLock);
    *frames = mCaptureFramesReaded;
    *time = mCaptureTimeStamp.tv_sec * 1000000000LL + mCaptureTimeStamp.tv_nsec;
    ALOGD_IF((mlog_flag&AUD_LOG_DEBUG_EN), "%s(), return frames = %" PRIu64 ", time = %" PRIu64 "", __FUNCTION__, *frames, *time);
    AL_UNLOCK(mTimeStampLock);

    return 0;
}

status_t AudioALSACaptureDataProviderBase::getPcmStatus() {
    //ALOGD("%s()", __FUNCTION__);
    return mPcmStatus;
}

status_t AudioALSACaptureDataProviderBase::start() {
    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderBase::stop() {
    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderBase::createMmapBuffer(int32_t min_size_frames __unused,
                                                               struct audio_mmap_buffer_info *info __unused) {
    return NO_ERROR;
}

status_t AudioALSACaptureDataProviderBase::getMmapPosition(struct audio_mmap_position *position __unused) {
    return NO_ERROR;
}

uint32_t AudioALSACaptureDataProviderBase::getInputSampleRate(audio_devices_t inputDevice, audio_devices_t outputDevice) {
    bool bHifiRecord = AudioSpeechEnhanceInfo::getInstance()->GetHifiRecord();
    bool bPhoneCallOpen = AudioALSAStreamManager::getInstance()->isPhoneCallOpen();
    uint32_t sampleRate = 48000;

    ALOGD("%s(), input_device: 0x%x, output_device 0x%x, hifi_record = %d, phone call open = %d",
          __FUNCTION__, inputDevice, outputDevice, bHifiRecord, bPhoneCallOpen);

    if (bHifiRecord == true) {
        sampleRate = 96000;
    }
#ifdef MTK_DMIC_SR_LIMIT
    if (IsAudioSupportFeature(AUDIO_SUPPORT_DMIC)) {
        if (inputDevice != AUDIO_DEVICE_IN_WIRED_HEADSET) {
            sampleRate = 32000;
        }
    }
#endif
    if (bPhoneCallOpen == true) {
        //if not BT phonecall
        if (!(audio_is_bluetooth_sco_device(outputDevice))) {
            uint32_t sampleRateTmp = AudioALSASpeechPhoneCallController::getInstance()->getSampleRate();
            if (sampleRateTmp != 0) {
                sampleRate = sampleRateTmp;
                ALOGD("%s(), Phone call mode active, change sample rate: %d", __FUNCTION__, sampleRate);
            }
        }
    }
    return sampleRate;
}

} // end of namespace android

