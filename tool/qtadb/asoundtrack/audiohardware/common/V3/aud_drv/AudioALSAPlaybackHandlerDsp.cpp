#include "AudioALSAPlaybackHandlerDsp.h"
#include "AudioALSAHardwareResourceManager.h"
#include "AudioALSASampleRateController.h"
#if defined(MTK_AUDIO_KS)
#include "AudioALSADeviceConfigManager.h"
#endif

#include "AudioMTKFilter.h"
#include "AudioALSADeviceParser.h"
#include "AudioALSADriverUtility.h"
#include "AudioALSAStreamManager.h"

#include "AudioSmartPaController.h"


#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
#include <aurisys_scenario_dsp.h>
#include <arsi_type.h>
#include <aurisys_config.h>
#include <audio_pool_buf_handler.h>
#include <aurisys_controller.h>
#include <aurisys_lib_manager.h>
#endif

#ifdef MTK_LATENCY_DETECT_PULSE
#include "AudioDetectPulse.h"
#endif

#include "AudioDspStreamManager.h"
#include <audio_task.h>
#include <AudioMessengerIPI.h>
#include "AudioUtility.h"


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSAPlaybackHandlerDsp"

#define calc_time_diff(x,y) ((x.tv_sec - y.tv_sec )+ (double)( x.tv_nsec - y.tv_nsec ) / (double)1000000000)
static const char* PROPERTY_KEY_EXTDAC = "vendor.audiohal.resource.extdac.support";
static const uint32_t kPcmDriverBufferSize = 0x20000; // 128k

namespace android {

AudioALSAPlaybackHandlerDsp::AudioALSAPlaybackHandlerDsp(const stream_attribute_t *stream_attribute_source) :
    AudioALSAPlaybackHandlerBase(stream_attribute_source),
    mDspHwPcm(NULL),
    mForceMute(false),
    mCurMuteBytes(0),
    mStartMuteBytes(0),
    mSupportNLE(false),
    mTaskScene(TASK_SCENE_INVALID),
    mAurisysDspConfig(NULL),
    mAurisysDspLibManager(NULL) {
    mPlaybackHandlerType = isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags) ?
                           PLAYBACK_HANDLER_DEEP_BUFFER : (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) ?
                           PLAYBACK_HANDLER_VOIP : isDspLowLatencyFlag(mStreamAttributeSource->mAudioOutputFlags) ?
                           PLAYBACK_HANDLER_FAST : PLAYBACK_HANDLER_NORMAL;

    ALOGD("%s() mPlaybackHandlerType = %d", __FUNCTION__, mPlaybackHandlerType);

    memset((void *)&mNewtime, 0, sizeof(mNewtime));
    memset((void *)&mOldtime, 0, sizeof(mOldtime));
    memset(&mDsphwConfig, 0, sizeof(mDsphwConfig));

    if (!(platformIsolatedDeepBuffer()) ||
        (platformIsolatedDeepBuffer() &&
         mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_DEEP_BUFFER)) {
        mSupportNLE = true;
    } else {
        mSupportNLE = false;
    }

    mPCMDumpFileDSP = NULL;
    mDspStreamManager = AudioDspStreamManager::getInstance();
    ASSERT(mDspStreamManager != NULL);
}

AudioALSAPlaybackHandlerDsp::~AudioALSAPlaybackHandlerDsp() {
    ALOGD("%s()", __FUNCTION__);
}

uint32_t AudioALSAPlaybackHandlerDsp::getLowJitterModeSampleRate() {
    return 48000;
}

int AudioALSAPlaybackHandlerDsp::setAfeDspShareMem(unsigned int flag, bool condition) {
    mDspStreamManager->setAfeOutDspShareMem(flag, condition);
    return 0;
}

int AudioALSAPlaybackHandlerDsp::setDspRuntimeEn(bool condition) {
    if (isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags)) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_deepbuf_runtime_en"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    } else if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_voipdl_runtime_en"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    } else if (isDspLowLatencyFlag(mStreamAttributeSource->mAudioOutputFlags)) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_fast_runtime_en"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    } else {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "dsp_primary_runtime_en"), 0, condition)) {
            ALOGW("%s(), enable fail", __FUNCTION__);
            return -1;
        }
    }
    return 0;
}

bool AudioALSAPlaybackHandlerDsp::deviceSupportHifi(audio_devices_t outputdevice) {
    // modify this to let output device support hifi audio
    if (outputdevice == AUDIO_DEVICE_OUT_WIRED_HEADSET || outputdevice == AUDIO_DEVICE_OUT_WIRED_HEADPHONE) {
        return true;
    } else {
        return false;
    }
}

uint32_t AudioALSAPlaybackHandlerDsp::chooseTargetSampleRate(uint32_t SampleRate, audio_devices_t outputdevice) {
    ALOGV("ChooseTargetSampleRate SampleRate = %d outputdevice = %d", SampleRate, outputdevice);
    uint32_t TargetSampleRate = 48000;
#if defined(MTK_HIFIAUDIO_SUPPORT)
    bool hifi_enable = mHardwareResourceManager->getHiFiStatus();
    bool device_support_hifi = deviceSupportHifi(outputdevice);
    ALOGD("%s() hifi_enable = %d device_support_hifi = %d, PrimarySampleRate = %d",
          __FUNCTION__, hifi_enable, device_support_hifi, SampleRate);

    if (hifi_enable && device_support_hifi) {
        TargetSampleRate = SampleRate;
    }
#else
    if (SampleRate <=  192000 && SampleRate > 96000 && deviceSupportHifi(outputdevice)) {
        TargetSampleRate = 192000;
    } else if (SampleRate <= 96000 && SampleRate > 48000 && deviceSupportHifi(outputdevice)) {
        TargetSampleRate = 96000;
    } else if (SampleRate <= 48000 && SampleRate >= 32000) {
        TargetSampleRate = SampleRate;
    }
#endif
    return TargetSampleRate;
}

String8 AudioALSAPlaybackHandlerDsp::getPlaybackTurnOnSequence(unsigned int turnOnSeqType,
                                                               const char *playbackSeq) {
    String8 sequence = String8();
    bool isSpk = (mStreamAttributeSource->output_devices & AUDIO_DEVICE_OUT_SPEAKER);
    bool isSmartPA = AudioSmartPaController::getInstance()->isSmartPAUsed();
    bool isADSPPlayback = AudioDspStreamManager::getInstance()->isAdspPlayback(mStreamAttributeSource->mAudioOutputFlags,
                                                                               mStreamAttributeSource->output_devices);
    bool isADSPA2dpUsed = (AudioDspStreamManager::getInstance()->getDspA2DPEnable() == true) &&
                          (mStreamAttributeSource->output_devices & AUDIO_DEVICE_OUT_ALL_A2DP);
    if (playbackSeq == NULL){
        ASSERT(playbackSeq != NULL);
        return sequence;
    }

    switch (turnOnSeqType) {
    case TURN_ON_SEQUENCE_1:
        if (isADSPPlayback) {
            sequence = String8(playbackSeq) + AUDIO_CTL_ADSP_UL;
        } else {
            sequence = mHardwareResourceManager->getOutputTurnOnSeq(mStreamAttributeSource->output_devices,
                                                                    false, playbackSeq);
        }
        break;
    case TURN_ON_SEQUENCE_2:
        if (popcount(mStreamAttributeSource->output_devices) > 1) {
            if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
                if (isSpk && isSmartPA && !isADSPPlayback) {
                    sequence = mHardwareResourceManager->getOutputTurnOnSeq(mStreamAttributeSource->output_devices,
                                                                           true, playbackSeq);
                }
            } else {
                if ((isSpk && isSmartPA) || isADSPPlayback) {
                    sequence = mHardwareResourceManager->getOutputTurnOnSeq(mStreamAttributeSource->output_devices,
                                                                            true, playbackSeq);
                }
            }
        }
        break;
    case TURN_ON_SEQUENCE_3:
        if (((mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) == 0) &&
            !isADSPPlayback && isADSPA2dpUsed) {
            sequence = String8(playbackSeq) + AUDIO_CTL_ADSP_UL;
        }
        break;
    case TURN_ON_SEQUENCE_DSP:
        if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
            if (isSpk && isADSPPlayback) {
                sequence = String8(playbackSeq) + AUDIO_CTL_DSPDL;
            }
        } else {
            if (isADSPPlayback) {
                sequence = String8(playbackSeq) + AUDIO_CTL_DSPDL;
            } else {
                if (!isSpk && !isSmartPA && strcmp(playbackSeq, AUDIO_CTL_PLAYBACK3) == 0) {
                    sequence = String8(playbackSeq) + AUDIO_CTL_DSPDL;
                }

                if (isADSPA2dpUsed) {
                    sequence = String8(playbackSeq) + AUDIO_CTL_DSPDL;
                }
            }
        }
        break;
    default:
        ASSERT(0);
        break;
    }

    return sequence;
}

status_t AudioALSAPlaybackHandlerDsp::openDspHwPcm() {
    int pcmindex = -1, cardindex = 0, ret = 0;
    unsigned int pcmmaxsize, pcmconfigsize;
    struct pcm_params *params = NULL;

    ALOGV("+%s(),", __FUNCTION__);

#if defined(MTK_AUDIO_KS)
    String8 playbackSeq = String8();
    if (isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags)) {
        pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback3);
        if (pcmindex < 0) {
            // use playback 2 if this platform does not have playback 3
            pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback2);
            cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback2);
            playbackSeq = String8(AUDIO_CTL_PLAYBACK2);
        } else {
            cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback3);
            playbackSeq = String8(AUDIO_CTL_PLAYBACK3);
        }

        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "deep_buffer_scenario"), 0, 1)) {
            ALOGW("%s(), deep_buffer_scenario enable fail", __FUNCTION__);
        }
    } else if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        // voip dl using DL12 , if not support using DL3
        pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback12);
        if(pcmindex < 0) {
            pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback3);
            cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback3);
            playbackSeq = String8(AUDIO_CTL_PLAYBACK3);
        } else {
            cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback12);
            playbackSeq = String8(AUDIO_CTL_PLAYBACK12);
        }

    } else if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST) {
        pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback2);
        cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback2);
        playbackSeq = String8(AUDIO_CTL_PLAYBACK2);
    }else {
        pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlayback1);
        cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlayback1);
        playbackSeq = String8(AUDIO_CTL_PLAYBACK1);
    }

    mApTurnOnSequence = getPlaybackTurnOnSequence(TURN_ON_SEQUENCE_1, playbackSeq);
    mApTurnOnSequence2 = getPlaybackTurnOnSequence(TURN_ON_SEQUENCE_2, playbackSeq);
    if ((mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) == 0) {
        mApTurnOnSequence3 = getPlaybackTurnOnSequence(TURN_ON_SEQUENCE_3, playbackSeq);
    }
    mApTurnOnSequenceDsp = getPlaybackTurnOnSequence(TURN_ON_SEQUENCE_DSP, playbackSeq);
    mHardwareResourceManager->setCustOutputDevTurnOnSeq(mStreamAttributeSource->output_devices,
                                                        mTurnOnSeqCustDev1, mTurnOnSeqCustDev2);

    mHardwareResourceManager->enableTurnOnSequence(mApTurnOnSequence);
    mHardwareResourceManager->enableTurnOnSequence(mApTurnOnSequence2);
    mHardwareResourceManager->enableTurnOnSequence(mApTurnOnSequenceDsp);
    mHardwareResourceManager->enableTurnOnSequence(mApTurnOnSequence3);
    mHardwareResourceManager->enableTurnOnSequence(mTurnOnSeqCustDev1);
    mHardwareResourceManager->enableTurnOnSequence(mTurnOnSeqCustDev2);
#else
    String8 pcmPath = isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags) ?
                      keypcmDL1DATA2PLayback : keypcmI2S0Dl1Playback;

    pcmindex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(pcmPath);
    cardindex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(pcmPath);
#endif

    ALOGD("%s(), pcmindex = %d cardindex = %d ", __FUNCTION__, pcmindex, cardindex);

    /* allocate the same with dsp platform drver */
    mDsphwConfig.period_size = mConfig.period_size;
    mDsphwConfig.period_count = mConfig.period_count;
    mDsphwConfig.channels = mStreamAttributeTarget.num_channels;
    mDsphwConfig.rate = mStreamAttributeTarget.sample_rate;
    mDsphwConfig.format = transferAudioFormatToPcmFormat(mStreamAttributeTarget.audio_format);

    pcmconfigsize = mDsphwConfig.period_count * mDsphwConfig.period_size * mDsphwConfig.channels * (pcm_format_to_bits(mDsphwConfig.format) / 8);

    ALOGD("%s(), flag 0x%x, mDevice = 0x%x, mDsphwConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d",
          __FUNCTION__, mStreamAttributeSource->mAudioOutputFlags, mStreamAttributeSource->output_devices,
          mDsphwConfig.channels, mDsphwConfig.rate, mDsphwConfig.period_size, mDsphwConfig.period_count, mDsphwConfig.format);

    mDsphwConfig.start_threshold = (mDsphwConfig.period_count * mDsphwConfig.period_size);
    if (mStreamAttributeSource->mAudioInputFlags & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        mDsphwConfig.start_threshold = ((mDsphwConfig.period_count - 1) * mDsphwConfig.period_size);
    }
    mDsphwConfig.stop_threshold = ~(0U);
    mDsphwConfig.silence_threshold = 0;
    opeDspPcmDriver(pcmindex);
    if (pcm_start(mDspHwPcm) != 0) {
        ALOGE("%s(), pcm_start(%p) == false due to %s", __FUNCTION__, mDspHwPcm, pcm_get_error(mDspHwPcm));
    }

    ALOGV("-%s(),", __FUNCTION__);
    return NO_ERROR;
}

uint32_t AudioALSAPlaybackHandlerDsp::updateKernelBufferSize(audio_devices_t outputdevice) {
    uint32_t target_size = AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_DSPBUFFER_SIZE_DEEP);
#if defined(MTK_HIFIAUDIO_SUPPORT)
    bool hifi_enable = mHardwareResourceManager->getHiFiStatus();
    bool device_support_hifi = deviceSupportHifi(outputdevice);
    uint32_t flag_support_hifi = isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags) ||
                                 (mStreamAttributeSource->mAudioOutputFlags == AUDIO_OUTPUT_FLAG_PRIMARY);

    ALOGD("%s(), hifi_enable = %d device_support_hifi(device:%d) = %d, flag_support_hifi(flag:0x%x) = 0x%x",
          __FUNCTION__, hifi_enable, outputdevice, device_support_hifi,
          mStreamAttributeSource->mAudioOutputFlags, flag_support_hifi);

    if (hifi_enable && device_support_hifi && flag_support_hifi) {
        if (mStreamAttributeTarget.sample_rate > 48000 && mStreamAttributeTarget.sample_rate <= 96000) {
            target_size = AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_DSPBUFFER_SIZE_HIFI_96K);
        } else {
            target_size = AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_DSPBUFFER_SIZE_HIFI_192K);
        }
    }
#endif
    ALOGV("%s(), outputdevice = %d, update kernel BufferSize = %d", __FUNCTION__, outputdevice, target_size);
    return target_size;
}

status_t AudioALSAPlaybackHandlerDsp::open() {
    struct pcm_params *params = NULL;
    int dspPcmIndex, dspCardIndex = 0;
    audio_output_flags_t outputFlag = mStreamAttributeSource->mAudioOutputFlags;
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    uint32_t aurisys_scenario = 0xFFFFFFFF;
    uint8_t arsi_process_type = ARSI_PROCESS_TYPE_DL_ONLY;
    const struct stream_attribute_t *attrIn = NULL;
    const struct stream_attribute_t *attrOut = NULL;
#endif
    unsigned int feature_id = getDspFeatureID(outputFlag);
    uint32_t d2a_size = 0x10000;

    /* Init log when open */
    initLogFlag();

    setAfeDspShareMem(outputFlag, true);
    setDspRuntimeEn(true);
    mAudioMessengerIPI->registerAdspFeature(feature_id);

    /* get task scene */
    if (isIsolatedDeepBuffer(outputFlag)) {
        mTaskScene = TASK_SCENE_DEEPBUFFER;
        aurisys_scenario = AURISYS_SCENARIO_DSP_DEEP_BUF;
    } else if (outputFlag & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        mTaskScene = TASK_SCENE_VOIP;
        aurisys_scenario = AURISYS_SCENARIO_DSP_VOIP;
        arsi_process_type = ARSI_PROCESS_TYPE_DL_ONLY; //ARSI_PROCESS_TYPE_UL_AND_DL;
    } else if (isDspLowLatencyFlag(outputFlag)) {
        mTaskScene = TASK_SCENE_FAST;
        /* fast don't need aurisys */
    } else {
        mTaskScene = TASK_SCENE_PRIMARY;
#if defined(MTK_TC10_FEATURE) && defined(MTK_TC10_IN_HOUSE)
        if (mStreamAttributeSource->audio_mode == AUDIO_MODE_IN_COMMUNICATION) {
            aurisys_scenario = AURISYS_SCENARIO_DSP_VOIP; //for non voip_rx platform
        } else {
            aurisys_scenario = AURISYS_SCENARIO_DSP_PRIMARY;
        }
#else
        aurisys_scenario = AURISYS_SCENARIO_DSP_PRIMARY;
#endif
    }

    /* choose adsp task pcm */
    if (isIsolatedDeepBuffer(outputFlag)) {
        dspPcmIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDspDeepbuf);
        dspCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlaybackDspDeepbuf);
        ALOGD_IF((mlog_flag & AUD_OUT_OP_LOG), "%s(), dspPcmIndex = %d dspCardIndex = %d deep buffer",
                 __FUNCTION__, dspPcmIndex, dspCardIndex);
    } else if (outputFlag & AUDIO_OUTPUT_FLAG_VOIP_RX) {
        dspPcmIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDspVoip);
        dspCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlaybackDspVoip);
        ALOGD_IF((mlog_flag & AUD_OUT_OP_LOG), "%s(), dspPcmIndex = %d dspCardIndex = %d voip dl",
                 __FUNCTION__, dspPcmIndex, dspCardIndex);
    } else if (isDspLowLatencyFlag(outputFlag)) {
        dspPcmIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDspFast);
        dspCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlaybackDspFast);
        ALOGD_IF((mlog_flag & AUD_OUT_OP_LOG), "%s(), dspPcmIndex = %d dspCardIndex = %d fast dl",
                 __FUNCTION__, dspPcmIndex, dspCardIndex);
    } else {
        dspPcmIndex = AudioALSADeviceParser::getInstance()->GetPcmIndexByString(keypcmPlaybackDspprimary);
        dspCardIndex = AudioALSADeviceParser::getInstance()->GetCardIndexByString(keypcmPlaybackDspprimary);
        ALOGD_IF((mlog_flag & AUD_OUT_OP_LOG), "%s(), dspPcmIndex = %d dspCardIndex = %d primary dl",
                 __FUNCTION__, dspPcmIndex, dspCardIndex);
    }

    /* set HW related attribute config */
#ifdef PLAYBACK_USE_24BITS_ONLY
    mStreamAttributeTarget.audio_format = AUDIO_FORMAT_PCM_8_24_BIT;
#else
    mStreamAttributeTarget.audio_format = (mStreamAttributeSource->audio_format == AUDIO_FORMAT_PCM_32_BIT) ? AUDIO_FORMAT_PCM_8_24_BIT : AUDIO_FORMAT_PCM_16_BIT;
#endif

    mStreamAttributeTarget.audio_channel_mask = AUDIO_CHANNEL_IN_STEREO;
    mStreamAttributeTarget.num_channels = popcount(mStreamAttributeTarget.audio_channel_mask);

    mStreamAttributeTarget.sample_rate = chooseTargetSampleRate(AudioALSASampleRateController::getInstance()->getPrimaryStreamOutSampleRate(),
                                                                mStreamAttributeSource->output_devices);

    mStreamAttributeTarget.output_devices = mStreamAttributeSource->output_devices;
    mStreamAttributeTarget.mAudioOutputFlags = outputFlag;

    /* HW pcm config initialize */
    memset(&mConfig, 0, sizeof(mConfig));

    /*
      kernel buffer size setting (period_size * period_count)
      deep buffer: 16K * 2 = 32k bytes
      primary: 4k * 4 = 16K bytes
      fast: 2k * 4 = 8k bytes
      raw: 768 * 11 = 8448 bytes (at least bigger than fast kernel buffer to avoid underflow)
    */
    mConfig.period_count = isIsolatedDeepBuffer(outputFlag) ? 2 :
                           ((outputFlag & AUDIO_OUTPUT_FLAG_RAW) ? 11 : 4);

    /* set target buffer size */
#if defined(MTK_HIFIAUDIO_SUPPORT)
    if (mStreamAttributeTarget.sample_rate > 48000) {
        mStreamAttributeTarget.buffer_size = updateKernelBufferSize(mStreamAttributeSource->output_devices);
        d2a_size *= 2;
    } else
#endif
    if (!platformIsolatedDeepBuffer() &&
        mStreamAttributeSource->audio_mode == AUDIO_MODE_IN_COMMUNICATION) {
        mStreamAttributeTarget.buffer_size = 2 * mStreamAttributeSource->buffer_size /
                                             ((mStreamAttributeSource->audio_format == AUDIO_FORMAT_PCM_16_BIT) ? 2 : 4) *
                                             ((mStreamAttributeTarget.audio_format == AUDIO_FORMAT_PCM_16_BIT) ? 2 : 4);
    } else if (outputFlag & AUDIO_OUTPUT_FLAG_FAST) {
        mStreamAttributeTarget.buffer_size = mStreamAttributeSource->buffer_size * mConfig.period_count;
    } else {
        mStreamAttributeTarget.buffer_size = isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags) ?
                                             AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_DSPBUFFER_SIZE_DEEP):
                                             AudioPlatformInfo::getInstance()->getPlatformInfoValue(KERNEL_BUFFER_SIZE_NORMAL);
    }

    if (outputFlag & AUDIO_OUTPUT_FLAG_FAST) {
        // audio low latency param - playback - interrupt rate
        mConfig.period_size = bytesToFrames(mStreamAttributeSource->buffer_size,
                                            mStreamAttributeSource->num_channels,
                                            mStreamAttributeTarget.audio_format);
    } else {
        mConfig.period_size = bytesToFrames(mStreamAttributeTarget.buffer_size,
                                            mStreamAttributeSource->num_channels,
                                            mStreamAttributeTarget.audio_format) / mConfig.period_count;
    }

    mConfig.channels = mStreamAttributeSource->num_channels;
    mConfig.rate = mStreamAttributeSource->sample_rate;
    mConfig.format = transferAudioFormatToPcmFormat(mStreamAttributeSource->audio_format);
    mConfig.start_threshold = mConfig.period_count * mConfig.period_size;
    mConfig.stop_threshold = 0;
    mConfig.silence_threshold = 0;

    ALOGD("%s(), flag 0x%x, mDevice = 0x%x, mConfig: channels = %d, rate = %d, period_size = %d, period_count = %d, format = %d avail_min = %d",
          __FUNCTION__, outputFlag, mStreamAttributeSource->output_devices,
          mConfig.channels, mConfig.rate, mConfig.period_size, mConfig.period_count, mConfig.format, mConfig.avail_min);

    ALOGD_IF(mlog_flag & AUD_OUT_OP_LOG,
             "%s(), flag %d, mDevice = 0x%x, d2a_size = 0x%x, buffer_size(s/t) = %d/%d, sample_rate(s/t) = %d/%d, channels(s/t) = %d/%d, format(s/t) = %d/%d",
             __FUNCTION__, outputFlag, mStreamAttributeSource->output_devices, d2a_size,
             mStreamAttributeSource->buffer_size, mStreamAttributeTarget.buffer_size,
             mStreamAttributeSource->sample_rate, mStreamAttributeTarget.sample_rate,
             mStreamAttributeSource->num_channels, mStreamAttributeTarget.num_channels,
             mStreamAttributeSource->audio_format, mStreamAttributeTarget.audio_format);

    mStreamAttributeTarget.stream_type = mStreamAttributeSource->stream_type;

    mAudioMessengerIPI->registerDmaCbk(
        mTaskScene,
        0x08000,
        d2a_size,
        processDmaMsgWrapper,
        this);

    openWavDump(LOG_TAG);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (!(outputFlag & AUDIO_OUTPUT_FLAG_FAST) &&
        !mStreamAttributeSource->isBypassAurisys) {
        if (get_aurisys_on() && needAPAurisysProcess(aurisys_scenario)) {
            attrIn = &mStreamAttributeTarget;
            attrOut = &mStreamAttributeTarget;
        } else {
            attrIn = mStreamAttributeSource;
            attrOut = (mDspStreamManager->getDspVersion() == DSP_VER_HARDWARE_MIX) ?
                      &mStreamAttributeTarget : mStreamAttributeSource;
        }
        mDspStreamManager->CreateAurisysLibManager(
            &mAurisysDspLibManager,
            &mAurisysDspConfig,
            mTaskScene,
            aurisys_scenario,
            arsi_process_type,
            mStreamAttributeSource->audio_mode,
            attrIn,
            attrOut,
            NULL,
            NULL);
    }
#endif

    // open pcm driver
    openPcmDriver(dspPcmIndex);

    if(mDspStreamManager->getDspVersion() == DSP_VER_HARDWARE_MIX)
        openDspHwPcm();

    mDspStreamManager->addPlaybackHandler(this);
    OpenPCMDumpDSP(LOG_TAG, mTaskScene);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (get_aurisys_on() && needAPAurisysProcess(aurisys_scenario)) {
        CreateAurisysLibManager();
    }
#endif

#if defined(MTK_HYBRID_NLE_SUPPORT) // must be after pcm open
    mStreamAttributeTarget.output_devices = mStreamAttributeSource->output_devices;
    initNLEProcessing();
#endif

    // open codec driver
    if (mStreamAttributeSource->dualDevice != AUDIO_DEVICE_NONE) {
        mHardwareResourceManager->startOutputDevice(mStreamAttributeSource->dualDevice, mStreamAttributeTarget.sample_rate);
    } else {
        mHardwareResourceManager->startOutputDevice(mStreamAttributeSource->output_devices, mStreamAttributeTarget.sample_rate);
    }

    //===========================================

    /* init time nomitor */
    initOutMonitor();
    calThresTime(mPcm);

    mTimeStampValid = false;
    mBytesWriteKernel = 0;
    ALOGV("-%s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerDsp::start() {
    int retval = NO_ERROR;

    AL_LOCK(mLock);
    if (mPcm)
        retval = pcm_start(mPcm);
    AL_UNLOCK(mLock);
    ALOGD("+%s(),flag = 0x%x retval = %d", __FUNCTION__,
          mStreamAttributeSource->mAudioOutputFlags, retval);
    return retval;
}


status_t AudioALSAPlaybackHandlerDsp::stop() {
    int retval = NO_ERROR;

    /* lock to avoid close done */
    AL_LOCK(mLock);
    if (mPcm)
        retval = pcm_stop(mPcm);
    AL_UNLOCK(mLock);
    ALOGD("+%s(),flag = 0x%x retval = %d", __FUNCTION__,
          mStreamAttributeSource->mAudioOutputFlags, retval);
    return retval;
}

status_t AudioALSAPlaybackHandlerDsp::closePcmDriver() {
    ALOGV("+%s(), mPcm = %p", __FUNCTION__, mPcm);

    AL_LOCK(mLock);
    if (mPcm != NULL) {
        pcm_stop(mPcm);
        pcm_close(mPcm);
        mPcm = NULL;
    }
    AL_UNLOCK(mLock);

    ALOGD("-%s(), mPcm = %p", __FUNCTION__, mPcm);
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerDsp::close() {
    ALOGD("+%s(), flag 0x%x, mDevice = 0x%x", __FUNCTION__, mStreamAttributeSource->mAudioOutputFlags,
          mStreamAttributeSource->output_devices);
    unsigned int feature_id = getDspFeatureID(mStreamAttributeSource->mAudioOutputFlags);

#if defined(MTK_HYBRID_NLE_SUPPORT)
    // Must do this before close analog path
    deinitNLEProcessing();
#endif

    // close codec driver
    // For DAPM sequence when barge-in and playback use the same front-end
    mHardwareResourceManager->stopOutputDeviceWithoutNotify();
    mHardwareResourceManager->disableTurnOnSequence(mTurnOnSeqCustDev1);
    mHardwareResourceManager->disableTurnOnSequence(mTurnOnSeqCustDev2);

    if(mDspStreamManager->getDspVersion() == DSP_VER_HARDWARE_MIX)
        closeDspPcmDriver();

    // close pcm driver
    closePcmDriver();

    setAfeDspShareMem(mStreamAttributeSource->mAudioOutputFlags, false);
    setDspRuntimeEn(false);

    mDspStreamManager->removePlaybackHandler(this);

    // For DAPM sequence when barge-in and playback use the same front-end
    mHardwareResourceManager->notifyOutputDeviceStatusChange(mStreamAttributeSource->output_devices, DEVICE_STATUS_OFF, 0);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (!(mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_FAST)) {
        mDspStreamManager->DestroyAurisysLibManager(
            &mAurisysDspLibManager, &mAurisysDspConfig, mTaskScene);
    }
#endif
    mAudioMessengerIPI->deregisterDmaCbk(mTaskScene);
    mAudioMessengerIPI->deregisterAdspFeature(feature_id);

#if defined(MTK_AUDIO_KS)
    mHardwareResourceManager->disableTurnOnSequence(mApTurnOnSequence);
    mHardwareResourceManager->disableTurnOnSequence(mApTurnOnSequenceDsp);
    mHardwareResourceManager->disableTurnOnSequence(mApTurnOnSequence2);
    mHardwareResourceManager->disableTurnOnSequence(mApTurnOnSequence3);

    if (isIsolatedDeepBuffer(mStreamAttributeSource->mAudioOutputFlags)) {
        if (mixer_ctl_set_value(mixer_get_ctl_by_name(mMixer, "deep_buffer_scenario"), 0, 0)) {
            ALOGW("%s(), deep_buffer_scenario disable fail", __FUNCTION__);
        }
    }
#endif

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (get_aurisys_on() && (mAurisysLibManager != NULL)) {
        DestroyAurisysLibManager();
    }
#endif

    /* update wav header */
    closeWavDump();

    ClosePCMDumpDSP(mTaskScene);

    ALOGD("-%s()", __FUNCTION__);
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerDsp::openDspPcmDriverWithFlag(const unsigned int device, unsigned int flag) {
    ALOGD("+%s(), mDspHwPcm device = %d, flag = 0x%x", __FUNCTION__, device, flag);

    ASSERT(mDspHwPcm == NULL);
    mDspHwPcm = pcm_open(AudioALSADeviceParser::getInstance()->GetCardIndex(),
                         device, flag, &mDsphwConfig);
    if (mDspHwPcm == NULL) {
        ALOGE("%s(), mDspHwPcm == NULL!!", __FUNCTION__);
    } else if (pcm_is_ready(mDspHwPcm) == false) {
        ALOGE("%s(), pcm_is_ready(%p) == false due to %s, close pcm.", __FUNCTION__, mDspHwPcm, pcm_get_error(mDspHwPcm));
        pcm_close(mDspHwPcm);
        mDspHwPcm = NULL;
    } else if (pcm_prepare(mDspHwPcm) != 0) {
        ALOGE("%s(), pcm_prepare(%p) == false due to %s, close pcm.", __FUNCTION__, mDspHwPcm, pcm_get_error(mDspHwPcm));
        pcm_close(mDspHwPcm);
        mDspHwPcm = NULL;
    }

    ASSERT(mDspHwPcm != NULL);
    return NO_ERROR;
}


status_t AudioALSAPlaybackHandlerDsp::opeDspPcmDriver(const unsigned int device) {
    return openDspPcmDriverWithFlag(device, PCM_OUT | PCM_MONOTONIC);
}

status_t AudioALSAPlaybackHandlerDsp::closeDspPcmDriver() {

    if (mDspHwPcm != NULL) {
        pcm_stop(mDspHwPcm);
        pcm_close(mDspHwPcm);
        mDspHwPcm = NULL;
    }

    ALOGV("-%s(), mDspHwPcm = %p", __FUNCTION__, mDspHwPcm);
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerDsp::routing(const audio_devices_t output_devices) {
    mHardwareResourceManager->changeOutputDevice(output_devices);
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    if (mAudioFilterManagerHandler) { mAudioFilterManagerHandler->setDevice(output_devices); }
#endif
    return NO_ERROR;
}

status_t AudioALSAPlaybackHandlerDsp::setScreenState(bool mode, size_t buffer_size, size_t reduceInterruptSize, bool bforce __unused) {
    // don't increase irq period when play hifi
    if (mode == 0 && mStreamAttributeSource->sample_rate > 48000) {
        return NO_ERROR;
    }

    ALOGD("%s, flag 0x%x, %f, mode = %d , buffer_size = %zu, channel %d, format%d reduceInterruptSize = %zu",
          __FUNCTION__, mStreamAttributeSource->mAudioOutputFlags,
          mStreamAttributeTarget.mInterrupt, mode, buffer_size, mConfig.channels,
          mStreamAttributeTarget.audio_format, reduceInterruptSize);

    return NO_ERROR;
}

ssize_t AudioALSAPlaybackHandlerDsp::write(const void *buffer, size_t bytes) {
    ALOGD_IF((mlog_flag & AUD_OUT_WRITE_LOG), "+%s(), buffer = %p, bytes = %zu flag = 0x%x",
             __FUNCTION__, buffer, bytes, mStreamAttributeSource->mAudioOutputFlags);

    if (mPcm == NULL) {
        ALOGE("+%s(), buffer = %p, bytes = %zu flag = 0x%x",
              __FUNCTION__, buffer, bytes, mStreamAttributeSource->mAudioOutputFlags);
        return bytes;
    }

    // const -> to non const
    void *pBufferAfterPending = NULL;
    uint32_t bytesAfterpending = 0;
    void *pBuffer = const_cast<void *>(buffer);

    if (pBuffer == NULL) {
        ALOGE("%s(), pBuffer == NULL, return", __FUNCTION__);
        ASSERT(0);
        return bytes;
    }

#ifdef MTK_AUDIO_DYNAMIC_LOG
    calHoldTime(AUD_AF_THRES);
#endif

    // stereo to mono for speaker
    doStereoToMonoConversionIfNeed(pBuffer, bytes);

#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
    if (get_aurisys_on() && (mAurisysLibManager != NULL)) {
        // expect library output amount smoothly
        mTransferredBufferSize = GetTransferredBufferSize(
            bytes,
            mStreamAttributeSource,
            &mStreamAttributeTarget);

        audio_pool_buf_copy_from_linear(
            mAudioPoolBufDlIn,
            pBuffer,
            bytes);

        // post processing + SRC + Bit conversion
        aurisys_process_dl_only(mAurisysLibManager, mAudioPoolBufDlIn, mAudioPoolBufDlOut);

        // data pending: sram is device memory, need word size align 64 byte for 64 bit platform
        uint32_t data_size = audio_ringbuf_count(&mAudioPoolBufDlOut->ringbuf);
        if (data_size > mTransferredBufferSize) {
            data_size = mTransferredBufferSize;
        }
        data_size &= 0xFFFFFFC0;

        audio_pool_buf_copy_to_linear(
            &mLinearOut->p_buffer,
            &mLinearOut->memory_size,
            mAudioPoolBufDlOut,
            data_size);
        //ALOGD("aurisys process data_size: %u", data_size);

        // wrap to original playback handler
        pBufferAfterPending = (void *)mLinearOut->p_buffer;
        bytesAfterpending = data_size;
    } else
#endif
    {
        pBufferAfterPending = pBuffer;
        bytesAfterpending = bytes;
    }

    writeWavDumpData(pBufferAfterPending, bytesAfterpending);

#ifdef MTK_AUDIO_DYNAMIC_LOG
    calHoldTime(AUD_HAL_THRES);
#endif

#ifdef MTK_LATENCY_DETECT_PULSE
    AudioDetectPulse::doDetectPulse(TAG_PLAYERBACK_HANDLER, PULSE_LEVEL, 0, (void *)pBufferAfterPending,
                                    bytes, mStreamAttributeTarget.audio_format,
                                    mStreamAttributeTarget.num_channels, mStreamAttributeTarget.sample_rate);
#endif

#if defined(MTK_A2DP_OFFLOAD_SUPPORT)
    if (AudioDspStreamManager::getInstance()->btOutWriteAction(mStreamAttributeSource->output_devices)){
        ALOGW("%s device 0x%x flag[0x%x]", __FUNCTION__,
              mStreamAttributeSource->output_devices, mStreamAttributeSource->mAudioOutputFlags);
        // if task playback still working, need to write ,otherewise return.
        if (isa2dpSpkDevice (mStreamAttributeSource->output_devices) == false) {
            usleep(bufferSizeTimeUs());
            return bytes;
        }
    }
#endif

    // write data to pcm driver
    int retval = pcm_write(mPcm, pBufferAfterPending, bytesAfterpending);

    mBytesWriteKernel = mBytesWriteKernel + bytesAfterpending;
    if (mTimeStampValid == false) {
        if (mBytesWriteKernel >= (mStreamAttributeTarget.buffer_size >> 1)) {
            mTimeStampValid = true;
        }
    }
#if defined(MTK_HYBRID_NLE_SUPPORT)
    if (mSupportNLE) {
        doNLEProcessing(pBufferAfterPending, bytesAfterpending);
    }
#endif

    updateHardwareBufferInfo(bytesAfterpending, bytesAfterpending);

#ifdef MTK_AUDIO_DYNAMIC_LOG
    calHoldTime(AUD_KERNEL_THRES);
#endif

    if (retval != 0) {
        ALOGE("+%s(),pcm_write fail pcm_stop flag: 0x%x", __FUNCTION__, mStreamAttributeSource->mAudioOutputFlags);
        retval = pcm_stop(mPcm);
    }

#ifdef MTK_AUDIO_DYNAMIC_LOG
    checkThresTime(mStreamAttributeSource->mAudioOutputFlags);
#endif
    ALOGD_IF((mlog_flag & AUD_OUT_WRITE_LOG), "-%s(), buffer = %p, bytes = %zu flag = 0x%x",
              __FUNCTION__, buffer, bytes, mStreamAttributeSource->mAudioOutputFlags);

    return bytes;
}

status_t AudioALSAPlaybackHandlerDsp::setFilterMng(AudioMTKFilterManager *pFilterMng) {
#if !defined(MTK_AURISYS_FRAMEWORK_SUPPORT)
    ALOGD("+%s() mAudioFilterManagerHandler [%p]", __FUNCTION__, pFilterMng);
    mAudioFilterManagerHandler = pFilterMng;
#else
    (void *)pFilterMng;
#endif
    return NO_ERROR;
}

int AudioALSAPlaybackHandlerDsp::updateAudioMode(audio_mode_t mode) {
    ALOGD("%s(), mode %d", __FUNCTION__, mode);
#if defined(MTK_AURISYS_FRAMEWORK_SUPPORT) && defined(MTK_TC10_FEATURE) && defined(MTK_TC10_IN_HOUSE)
    uint32_t aurisys_scenario = 0xFFFFFFFF;
    if (mStreamAttributeSource->mAudioOutputFlags & AUDIO_OUTPUT_FLAG_PRIMARY) {
        if (mode == AUDIO_MODE_IN_COMMUNICATION) {
            aurisys_scenario = AURISYS_SCENARIO_DSP_VOIP;
        } else {
            aurisys_scenario = AURISYS_SCENARIO_DSP_PRIMARY;
        }

        mDspStreamManager->DestroyAurisysLibManager(
            &mAurisysLibManager, &mAurisysDspConfig, mTaskScene);
        mDspStreamManager->CreateAurisysLibManager(
            &mAurisysLibManager,
            &mAurisysDspConfig,
            mTaskScene,
            AURISYS_SCENARIO_DSP_VOIP,
            ARSI_PROCESS_TYPE_DL_ONLY,
            mode,
            mStreamAttributeSource,
            (mDspStreamManager->getDspVersion() == DSP_VER_HARDWARE_MIX) ?
            &mStreamAttributeTarget : mStreamAttributeSource,
            NULL,
            NULL);
        }
    }
#endif
    return 0;
}

unsigned int AudioALSAPlaybackHandlerDsp::getPlaygbackExBuffer(){
    unsigned int sampleNum;
    unsigned long long a2dpremotedelayms, a2dpdelay;

    // a2dp device
    if (mStreamAttributeSource->output_devices & AUDIO_DEVICE_OUT_ALL_A2DP) {
        a2dpremotedelayms = AudioDspStreamManager::getInstance()->getA2dpRemoteDelayus();
        a2dpdelay = AudioALSAHardwareResourceManager::getInstance()->getA2dpLatency();
        sampleNum = (a2dpdelay * mStreamAttributeSource->sample_rate) / 1000;
        ALOGD_IF((mlog_flag&AUD_OUT_WRITE_LOG), "%s a2dpremotedelayms[%llu] a2dpdelay[%llu] sampleNum[%u] mStreamAttributeSource->sample_rate[%u] mStreamAttributeTarget.sample_rate[%u]",
              __func__, a2dpremotedelayms, a2dpdelay, sampleNum, mStreamAttributeSource->sample_rate, mStreamAttributeTarget.sample_rate);
    } else {
        sampleNum = AudioDspStreamManager::getInstance()->getDspSample();
    }

    ALOGD_IF((mlog_flag&AUD_OUT_WRITE_LOG), "%s()return sampleNum %u", __FUNCTION__, sampleNum);
    return sampleNum;
}

bool AudioALSAPlaybackHandlerDsp::needAPAurisysProcess(const uint32_t aurisys_scenario) {
    return ((mStreamAttributeSource->mAudioOutputFlags == AUDIO_OUTPUT_FLAG_VOIP_RX) &&
            is_bypass_aurisys_lib(AURISYS_CORE_HIFI3, aurisys_scenario, ARSI_PROCESS_TYPE_DL_ONLY));
}

status_t AudioALSAPlaybackHandlerDsp::updateVolumeIndex(int stream, int device, int index) {
    if (!mAurisysDspConfig) {
        ALOGD("%s(), mAurisysDspConfig is NULL, ignore", __FUNCTION__);
        return NO_ERROR;
    }

    ALOGD("%s(), stream = %d, device = %d, index = %d, current scenario = %d (voip: 8)", __FUNCTION__, stream, device, index, mAurisysDspConfig->manager_config.aurisys_scenario);

    if (mAurisysDspConfig->manager_config.aurisys_scenario == AURISYS_SCENARIO_DSP_VOIP && stream == AUDIO_STREAM_VOICE_CALL) {
        ALOGD("%s(), mAurisysDspConfig = %p, mStreamAttributeSource = %p", __FUNCTION__, mAurisysDspConfig, mStreamAttributeSource);
        /* Setup custom info */
        setupCustomInfoStr(mAurisysDspConfig->manager_config.custom_info, MAX_CUSTOM_INFO_LEN, mStreamAttributeSource->mCustScene, index,
            audio_is_bluetooth_sco_device((audio_devices_t)(mManagerConfig->task_config.output_device_info.devices)) ? AudioALSAStreamManager::getInstance()->GetBtCodec() : -1);

        AudioDspStreamManager::getInstance()->UpdateAurisysConfig(
            mAurisysLibManager,
            mAurisysDspConfig,
            mStreamAttributeSource->audio_mode,
            mStreamAttributeSource,
            (mDspStreamManager->getDspVersion() == DSP_VER_HARDWARE_MIX) ?
            &mStreamAttributeTarget : mStreamAttributeSource);
    }

    return NO_ERROR;
}

} // end of namespace android
