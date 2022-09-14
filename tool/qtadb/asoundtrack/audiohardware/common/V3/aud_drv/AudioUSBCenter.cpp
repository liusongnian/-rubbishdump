#include <AudioUSBCenter.h>

#include <cutils/str_parms.h>
#include "AudioSystemLibCUtil.h"

#include <AudioType.h>
#include <AudioUtility.h>

#include "AudioAssert.h"

#ifdef HAVE_AEE_FEATURE
#include <aee.h>
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioUSBCenter"
#include <fstream>
#include <stdint.h>
#include <stdlib.h>

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifdef USB_DEFAULT_IEMS_PERIOD_US
#undef USB_DEFAULT_IEMS_PERIOD_US
#endif
#ifdef USB_FIXED_IEMS_PERIOD_US
#define USB_DEFAULT_IEMS_PERIOD_US (USB_FIXED_IEMS_PERIOD_US)
#else
#define USB_DEFAULT_IEMS_PERIOD_US (10000)
#endif

#ifdef USB_DEFAULT_OUT_PERIOD_US
#undef USB_DEFAULT_OUT_PERIOD_US
#endif
#ifdef USB_FIXED_OUT_PERIOD_US
#define USB_DEFAULT_OUT_PERIOD_US (USB_FIXED_OUT_PERIOD_US)
#else
#define USB_DEFAULT_OUT_PERIOD_US (5333)
#endif

#ifdef USB_DEFAULT_FAST_IN_PERIOD_US
#undef USB_DEFAULT_FAST_IN_PERIOD_US
#endif
#ifdef USB_FIXED_FAST_IN_PERIOD_US
#define USB_DEFAULT_FAST_IN_PERIOD_US (USB_FIXED_FAST_IN_PERIOD_US)
#else
#define USB_DEFAULT_FAST_IN_PERIOD_US (5000)
#endif


#if defined(MTK_AUDIO_SUPER_HIFI_USB)
static unsigned support_sample_rates[] = {96000, 88200, 192000, 176400, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 0};
#else
static unsigned support_sample_rates[] = {96000, 88200, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 0};
#endif


static char *support_channel[] = {
    /* 2 */(char *)"AUDIO_CHANNEL_IN_STEREO",
    /* 1 */(char *)"AUDIO_CHANNEL_IN_MONO",
    /* channel counts greater than this not considered */
};

static char *index_chans_strs[] = {
    /* 2 */(char *)"AUDIO_CHANNEL_INDEX_MASK_2",
    /* 1 */(char *)"AUDIO_CHANNEL_INDEX_MASK_1",
    /* 0 */(char *)"AUDIO_CHANNEL_NONE", /* will never be taken as this is a terminator */
    /* 3 */(char *)"AUDIO_CHANNEL_INDEX_MASK_3",
    /* 4 */(char *)"AUDIO_CHANNEL_INDEX_MASK_4",
    /* 5 */(char *)"AUDIO_CHANNEL_INDEX_MASK_5",
    /* 6 */(char *)"AUDIO_CHANNEL_INDEX_MASK_6",
    /* 7 */(char *)"AUDIO_CHANNEL_INDEX_MASK_7",
    /* 8 */(char *)"AUDIO_CHANNEL_INDEX_MASK_8",
};

static const char *format_string_map[] = {
    "AUDIO_FORMAT_PCM_16_BIT",      /* "PCM_FORMAT_S16_LE", */
    "AUDIO_FORMAT_PCM_32_BIT",      /* "PCM_FORMAT_S32_LE", */
    "AUDIO_FORMAT_PCM_8_BIT",       /* "PCM_FORMAT_S8", */
    "AUDIO_FORMAT_PCM_8_24_BIT",    /* "PCM_FORMAT_S24_LE", */
    "AUDIO_FORMAT_PCM_24_BIT_PACKED"/* "PCM_FORMAT_S24_3LE" */
};


static const char *get_format_string(const enum pcm_format fmt) {
    const char *string = NULL;

    switch (fmt) {
    case PCM_FORMAT_S16_LE:
        string = "AUDIO_FORMAT_PCM_16_BIT";
        break;
    case PCM_FORMAT_S32_LE:
        string = "AUDIO_FORMAT_PCM_32_BIT";
        break;
    case PCM_FORMAT_S8:
        string = "AUDIO_FORMAT_PCM_8_BIT";
        break;
    case PCM_FORMAT_S24_LE:
        string = "AUDIO_FORMAT_PCM_8_24_BIT";
        break;
    case PCM_FORMAT_S24_3LE:
        string = "AUDIO_FORMAT_PCM_24_BIT_PACKED";
        break;
    default:
        ALOGW("pcm_format %d error!!", fmt);
        string = "AUDIO_FORMAT_INVALID";
    }

    return string;
}


static unsigned int proxy_get_period_count(const alsa_device_proxy *proxy) {
    return (proxy) ? proxy->alsa_config.period_count : 0;
}


static enum pcm_format get_highest_pcm_fmt(const alsa_device_profile *profile) {
    enum pcm_format pcm_fmt = PCM_FORMAT_INVALID;

    if (!profile) {
        return PCM_FORMAT_S16_LE;
    }

    pcm_fmt = profile->default_config.format; /* save hifi cfg in default_config */
    if (!profile_is_format_valid(profile, pcm_fmt)) {
        ALOGW("%s(), pcm_fmt %d not support", __FUNCTION__, pcm_fmt);
        pcm_fmt = profile_get_default_format(profile);
    }

    return pcm_fmt;
}


static uint32_t get_highest_sample_rate(const alsa_device_profile *profile) {
    uint32_t sample_rate = 0;

    if (!profile) {
        return 48000;
    }

    sample_rate = profile->default_config.rate; /* save hifi cfg in default_config */
    if (!profile_is_sample_rate_valid(profile, sample_rate)) {
        ALOGW("%s(), sample_rate %u not support", __FUNCTION__, sample_rate);
        sample_rate = profile_get_default_sample_rate(profile);
    }

    return sample_rate;
}


static void dump_profile(const alsa_device_profile *profile) {
    char *rate_list = NULL;
    char *fmt_list = NULL;
    char *ch_list = NULL;

    if (!profile) {
        return;
    }

    rate_list = profile_get_sample_rate_strs(profile);
    fmt_list  = profile_get_format_strs(profile);
    ch_list   = profile_get_channel_count_strs(profile);

    ALOGD("card %d device %d direction 0x%x, " \
          "rate %s(=>%u), " \
          "fmt %s(=>%s), " \
          "ch %s(=>%u (min %u/max %u)), " \
          "period size (min %d/max %d)",
          profile->card,
          profile->device,
          profile->direction,
          rate_list,
          profile->default_config.rate,
          fmt_list,
          get_format_string(profile->default_config.format),
          ch_list,
          profile->default_config.channels,
          profile->min_channel_count,
          profile->max_channel_count,
          profile->min_period_size,
          profile->max_period_size);

    free(rate_list);
    free(fmt_list);
    free(ch_list);
}


static bool check_profile_valid(const alsa_device_profile *profile) {
    if (!profile) {
        ALOGE("profile NULL");
        return false;
    }
    if (!profile_is_initialized(profile)) {
        ALOGE("profile card %d device %d not valid", profile->card, profile->device);
        return false;
    }
    if (!profile_is_valid(profile)) {
        ALOGE("profile not valid");
        return false;
    }
    if (profile_get_default_sample_rate(profile) == 0) {
        ALOGE("rate is 0");
        return false;
    }
    if (profile_get_default_channel_count(profile) == 0) {
        ALOGE("ch is 0");
        return false;
    }
    if (profile->max_channel_count == 0) {
        ALOGE("max_channel_count is 0");
        return false;
    }
    if (profile->max_period_size == 0) {
        ALOGE("max_period_size is 0");
        return false;
    }

    return true;
}


static int descend_cmp(const void *a, const void *b) {
    unsigned _a = *(unsigned *)a;
    unsigned _b = *(unsigned *)b;
    int ret = 0;

    if (_a < _b) {
        ret = 1;
    } else if (_a == _b) {
        ret = 0;
    } else {
        ret = -1;
    }

    return ret;
}


static void set_hifi_config(alsa_device_profile *profile) {
    enum pcm_format pcm_fmt_array[] = {PCM_FORMAT_S32_LE,
                                       PCM_FORMAT_S24_3LE,
                                       PCM_FORMAT_S24_LE,
                                       PCM_FORMAT_S16_LE
                                      }; /* must be in descending order */
    struct pcm_config *config = NULL;

    unsigned rate_idx = 0;
    unsigned fmt_idx = 0;
    unsigned ch_idx = 0;


    struct pcm *pcm = NULL;
    bool valid = false;

    if (!profile) {
        return;
    }
    config = &profile->default_config; /* save hifi cfg in default_config */

    for (rate_idx = 0; rate_idx < ARRAY_SIZE(profile->sample_rates); rate_idx++) {
        if (valid || profile->sample_rates[rate_idx] == 0) {
            break;
        }
#if !defined(MTK_AUDIO_SUPER_HIFI_USB)
        if (profile->sample_rates[rate_idx] > 96000) {
            continue;
        }
#endif
        config->rate = profile->sample_rates[rate_idx];
        config->period_size = profile_get_period_size(profile, config->rate);

        for (fmt_idx = 0; fmt_idx < ARRAY_SIZE(pcm_fmt_array); fmt_idx++) {
            if (valid) {
                break;
            }
            if (!profile_is_format_valid(profile, pcm_fmt_array[fmt_idx])) {
                continue;
            }
            config->format = pcm_fmt_array[fmt_idx];

            for (ch_idx = 0; ch_idx < ARRAY_SIZE(profile->channel_counts); ch_idx++) {
                if (profile->channel_counts[ch_idx] == 0) {
                    break;
                }
                config->channels = profile->channel_counts[ch_idx];

                pcm = pcm_open(profile->card, profile->device,
                               profile->direction | PCM_MONOTONIC, config);
                if (pcm != NULL) {
                    if (pcm_is_ready(pcm)) {
                        pcm_close(pcm);
                        pcm = NULL;
                        valid = true;
                        ALOGD("%s(), hifi rate %u fmt %s ch %u", __FUNCTION__,
                              config->rate,
                              get_format_string(config->format),
                              config->channels);
                        break;
                    }
                    pcm_close(pcm);
                    pcm = NULL;
                }
            }
        }
    }

    if (valid == false) {
        config->rate = profile_get_default_sample_rate(profile);
        config->format = profile_get_default_format(profile);
        config->channels = profile_get_default_channel_count(profile);
    }
}


static int open_profile(alsa_device_profile *profile) {
    uint8_t try_cnt = 0;
    const uint8_t k_max_try_cnt = 10;
    const uint8_t k_sleep_ms = 100;

    int ret = -1;

    if (!profile) {
        AUD_WARNING("profile NULL");
        return false;
    }
    if (!profile_is_initialized(profile)) {
        ALOGE("%s(), profile card %d device %d not valid", __FUNCTION__, profile->card, profile->device);
        return false;
    }

    for (try_cnt = 0; try_cnt < k_max_try_cnt; try_cnt++) {
        profile_read_device_info(profile);
        if (check_profile_valid(profile)) {
            /* fix std_sample_rates[] not sorted in alsa_device_profile.c */
            qsort(profile->sample_rates, ARRAY_SIZE(profile->sample_rates), sizeof(unsigned), descend_cmp);
            ret = 0;
            break;
        }
        usleep(k_sleep_ms * 1000);
    }

    if (ret != 0) {
        ALOGW("open profile card %d device %d fail", profile->card, profile->device);
    } else {
        set_hifi_config(profile);
        dump_profile(profile);
    }

    return ret;
}



namespace android {

AudioUSBCenter *AudioUSBCenter::mUSBCenter = NULL;

AudioUSBCenter *AudioUSBCenter::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (!mUSBCenter) {
        mUSBCenter = new AudioUSBCenter();
    }

    return mUSBCenter;
}

AudioUSBCenter::AudioUSBCenter() {
    memset(&mOutProfile, 0, sizeof(mOutProfile));
    memset(&mInProfile, 0, sizeof(mInProfile));

    profile_init(&mOutProfile, PCM_OUT);
    profile_init(&mInProfile, PCM_IN);
}

AudioUSBCenter::~AudioUSBCenter() {
}

int AudioUSBCenter::setUSBOutConnectionState(audio_devices_t devices, bool connect, int card, int device) {
    ALOGD("%s(), devices 0x%x, connect %d, card %d, device %d",
          __FUNCTION__, devices, connect, card, device);

    if (audio_is_usb_out_device(devices)) {
        if (mIsDevConn.find(devices) == mIsDevConn.end()) {
            mIsDevConn.insert({devices, connect});
        } else {
            mIsDevConn[devices] = connect;
        }
        if (connect) {
            if (profile_is_cached_for(&mOutProfile, card, device) && check_profile_valid(&mOutProfile)) {
                ALOGD("%s(), card %d, device %d already opened", __FUNCTION__, card, device);
            } else {
                profile_init(&mOutProfile, PCM_OUT);

                ASSERT(card >= 0 && device >= 0);
                mOutProfile.card = card;
                mOutProfile.device = device;

                open_profile(&mOutProfile);
            }
        } else {
            profile_decache(&mOutProfile);
        }
    }

    return 0;
}

int AudioUSBCenter::setUSBInConnectionState(audio_devices_t devices, bool connect, int card, int device) {
    ALOGD("%s(), devices 0x%x, connect %d, card %d, device %d",
          __FUNCTION__, devices, connect, card, device);

    if (audio_is_usb_in_device(devices)) {
        if (mIsDevConn.find(devices) == mIsDevConn.end()) {
            mIsDevConn.insert({devices, connect});
        } else {
            mIsDevConn[devices] = connect;
        }
        if (connect) {
            if (profile_is_cached_for(&mInProfile, card, device) && check_profile_valid(&mInProfile)) {
                ALOGD("%s(), card %d, device %d already opened", __FUNCTION__, card, device);
            } else {
                profile_init(&mInProfile, PCM_IN);

                ASSERT(card >= 0 && device >= 0);
                mInProfile.card = card;
                mInProfile.device = device;

                open_profile(&mInProfile);
            }
        } else {
            profile_decache(&mInProfile);
        }
    }

    return 0;
}

bool AudioUSBCenter::getUSBConnectionState(audio_devices_t devices) {
    if (mIsDevConn.find(devices) == mIsDevConn.end()) {
        return false;
    }
    return mIsDevConn[devices];
}

// parse_card_device_params from usb audio_hal
static bool parse_card_device_params(const char *kvpairs, int *card, int *device) {
    struct str_parms *parms = NULL;
    char value[32] = {0};
    int param_val = -1;

    // initialize to "undefined" state.
    *card = -1;
    *device = -1;

    parms = str_parms_create_str(kvpairs);
    if (!parms) {
        ALOGE("%s(), str_parms_create_str(%s) NULL!!", __FUNCTION__, kvpairs);
        return false;
    }

    param_val = str_parms_get_str(parms, "card", value, sizeof(value));
    if (param_val >= 0) {
        *card = atoi(value);
    }

    param_val = str_parms_get_str(parms, "device", value, sizeof(value));
    if (param_val >= 0) {
        *device = atoi(value);
    }

    str_parms_destroy(parms);

    return *card >= 0 && *device >= 0;
}

int AudioUSBCenter::setParameter(alsa_device_profile *profile, const char *kvpairs) {
    int retValue = 0;
    int card = -1;
    int device = -1;

    if (!parse_card_device_params(kvpairs, &card, &device)) {
        // nothing to do
        return retValue;
    }

    if (!profile_is_cached_for(profile, card, device)) {
        int savedCard = profile->card;
        int savedDevice = profile->device;
        profile->card = card;
        profile->device = device;
        /*retValue = profile_read_device_info(profile) ? 0 : -EINVAL;
        if (retValue != 0) {
            profile->card = savedCard;
            profile->device = savedDevice;
        }*/
    }

    // TODO: profile_read_device_info in playback handler, or put here is better?

    // TODO: usb phone call card/device, change to use these info? instead of from connect/disconnect?

    ALOGD("%s(), direction 0x%x, card %d, device %d",
          __FUNCTION__, profile->direction, profile->card, profile->device);

    return retValue;
}

static char *device_get_parameters(alsa_device_profile *profile, const char *keys) {
    ALOGD("usb:audio_hal::device_get_parameters() keys:%s", keys);

    if (profile->card < 0 || profile->device < 0) {
        ALOGE("%s(), card %d device %d error!!", __FUNCTION__, profile->card, profile->device);
        return strdup("");
    }
    bool isCTSDevice = false;

    std::string deviceUSBName;
    std::string deviceCTSName;
    std::string usbidPath = "/proc/asound/cards";
    std::string pattern = "USB";
    std::string pattern2 = "HEADSET";
    std::string patternCTS = "AudioBox USB";
    std::string patternCTS2 = "AudioBox 44 VSL";
    std::string patternCTS3 = "AudioBox 22 VSL";
    std::string patternCTS4 = "Scarlett 2i4";
    std::string patternCTS5 = "UMC204HD";
    std::string patternCTS6 = "Rubix24";
    std::string patternCTS7 = "JBL Reflect Aware";
    std::string patternCTS8 = "USB-Audio - Libratone_INEAR";
    std::string patternCTS9 = "USB-Audio - HTC Type-C to 3.5mm Headphone J";
    std::string patternCTS10 = "Pixel USB-C Dongle";
    std::ifstream is("/proc/asound/cards");
    std::string buffer;
    while (std::getline(is, buffer)) {
        if (buffer.find(pattern) != std::string::npos || buffer.find(pattern2) != std::string::npos) {
            deviceUSBName = buffer;
        }
        if (buffer.find(patternCTS) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS));
        } else if (buffer.find(patternCTS2) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS2));
        } else if (buffer.find(patternCTS3) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS3));
        } else if (buffer.find(patternCTS4) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS4));
        } else if (buffer.find(patternCTS5) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS5));
        } else if (buffer.find(patternCTS6) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS6));
        } else if (buffer.find(patternCTS7) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS7));
        } else if (buffer.find(patternCTS8) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS8));
        } else if (buffer.find(patternCTS9) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS9));
        } else if (buffer.find(patternCTS10) != std::string::npos) {
            deviceCTSName = buffer.substr(buffer.find(patternCTS10));
        }
        if (!deviceCTSName.empty()) {
            isCTSDevice = true;
        }
    }
    ALOGD("USB Name: %s, open path %s , isCTSDevice : %d, CTSkeyword: %s", deviceUSBName.c_str(), usbidPath.c_str(), isCTSDevice, deviceCTSName.c_str());
    if (is) {
        is.close();
    }

    struct str_parms *query = str_parms_create_str(keys);
    if (!query) {
        ALOGE("%s(), str_parms_create_str(%s) fail!!", __FUNCTION__, keys);
        return strdup("");
    }

    struct str_parms *result = str_parms_create();
    if (!result) {
        ALOGE("%s(), str_parms_create() result fail!!", __FUNCTION__);
        str_parms_destroy(query);
        query = NULL;
        return strdup("");
    }

    /* These keys are from hardware/libhardware/include/audio.h */
    /* supported sample rates */
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES)) {
        char *rates_list_device = profile_get_sample_rate_strs(profile);
        if (rates_list_device) {
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
            bool isOutProfile = profile->direction == PCM_OUT;
            if (isOutProfile == false && isCTSDevice == false) {
                char buffer[128];
                buffer[0] = '\0';
                size_t buffSize = ARRAY_SIZE(buffer);
                size_t curStrLen = 0;
                char numBuffer[32];
                size_t numEntries = 0;
                size_t index;
                for (index = 0; support_sample_rates[index] != 0; index++) {
                    int ret = snprintf(numBuffer, sizeof(numBuffer), "%u", support_sample_rates[index]);
                    if (ret < 0 || ret >= sizeof(numBuffer)) {
                        ALOGE("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
                              numBuffer, sizeof(numBuffer), ret);
                        continue;
                    }

                    if (buffSize - curStrLen < strlen(numBuffer) + (numEntries != 0 ? 2 : 1)) {
                        break;
                    }
                    if (numEntries++ != 0) {
                        strlcat(buffer, "|", buffSize);
                    }
                    curStrLen = strlcat(buffer, numBuffer, buffSize);
                }
                char *rates_list = strdup(buffer);
                if (rates_list) {
                    ALOGD("rates_list: %s, rates_list_device: %s", rates_list, rates_list_device);
                    str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, rates_list);
                    free(rates_list);
                }
                free(rates_list_device);
            } else
#endif
            {
                ALOGD("rates_list_device: %s", rates_list_device);
                str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_SAMPLING_RATES, rates_list_device);
                free(rates_list_device);
            }
        }
    }

    /* supported channel counts */
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_CHANNELS)) {
        char *channels_list_device = profile_get_channel_count_strs(profile);
        if (channels_list_device) {
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
            bool isOutProfile = profile->direction == PCM_OUT;
            if (isOutProfile == false && isCTSDevice == false) {
                const size_t chans_strs_size = ARRAY_SIZE(support_channel);
                char buffer[27 * 16 + 1]; /* caution, may need to be expanded */
                buffer[0] = '\0';
                size_t buffSize = ARRAY_SIZE(buffer);
                size_t curStrLen = 0;
                size_t index;
                for (index = 0; index < chans_strs_size; index++) {
                    curStrLen = strlcat(buffer, support_channel[index], buffSize);
                    strlcat(buffer, "|", buffSize);
                    curStrLen = strlcat(buffer, index_chans_strs[index], buffSize);
                    if (index != chans_strs_size - 1) {
                        strlcat(buffer, "|", buffSize);
                    }
                }
                char *channel_list = strdup(buffer);
                if (channel_list) {
                    ALOGD("Input: channel_list: %s, channels_list_device: %s", channel_list, channels_list_device);
                    str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, channel_list);
                    free(channel_list);
                }
                free(channels_list_device);
            } else
#endif
            {
                ALOGD("channels_list_device: %s", channels_list_device);
                str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_CHANNELS, channels_list_device);
                free(channels_list_device);
            }
        }
    }

    /* supported sample formats */
    if (str_parms_has_key(query, AUDIO_PARAMETER_STREAM_SUP_FORMATS)) {
        char *format_params_device = profile_get_format_strs(profile);
        if (format_params_device) {
#ifdef MTK_AURISYS_FRAMEWORK_SUPPORT
            //bool isOutProfile = profile->direction == PCM_OUT;
            if (isCTSDevice == false) {
                char buffer[256];
                buffer[0] = '\0';
                size_t buffSize = ARRAY_SIZE(buffer);
                const size_t format_strs_size = ARRAY_SIZE(format_string_map);
                size_t curStrLen = 0;
                size_t numEntries = 0;
                size_t index = 0;
                for (index = 0; index < format_strs_size; index++) {
                    curStrLen = strlcat(buffer, format_string_map[index], buffSize);
                    if (index != format_strs_size - 1) {
                        strlcat(buffer, "|", buffSize);
                    }
                }
                char *format_params = strdup(buffer);
                if (format_params) {
                    ALOGD("format_params: %s, format_params_device: %s", format_params, format_params_device);
                    str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_FORMATS, format_params);
                    free(format_params);
                }
                free(format_params_device);
            } else
#endif
            {
                ALOGD("format_params_device:%s", format_params_device);
                str_parms_add_str(result, AUDIO_PARAMETER_STREAM_SUP_FORMATS, format_params_device);
                free(format_params_device);
            }
        }
    }
    str_parms_destroy(query);
    query = NULL;

    char *result_str = str_parms_to_str(result);
    str_parms_destroy(result);
    result = NULL;

    ALOGV("device_get_parameters = %s", result_str);
    return result_str;
}

int AudioUSBCenter::outSetParameter(const char *kvpairs) {
    return setParameter(&mOutProfile, kvpairs);
}

char *AudioUSBCenter::outGetParameter(const char *keys) {
    ALOGD("usb:audio_hal::getParameter() keys:%s", keys);
    return device_get_parameters(&mOutProfile, keys);
}

int AudioUSBCenter::inSetParameter(const char *kvpairs) {
    return setParameter(&mInProfile, kvpairs);
}

char *AudioUSBCenter::inGetParameter(const char *keys) {
    ALOGD("usb:audio_hal::getParameter() keys:%s", keys);
    return device_get_parameters(&mInProfile, keys);
}


alsa_device_profile *AudioUSBCenter::getProfile(const int direction) {
    alsa_device_profile *profile = NULL;
    int ret = 0;

    if (direction == PCM_OUT) {
        profile = &mOutProfile;
    } else if (direction == PCM_IN) {
        profile = &mInProfile;
    } else {
        ALOGW("%s(), direction 0x%x not support", __FUNCTION__, direction);
        return NULL;
    }

    if (!profile_is_initialized(profile)) {
        ALOGE("%s(), profile card %d device %d not valid", __FUNCTION__, profile->card, profile->device);
        return NULL;
    }

    if (!check_profile_valid(profile)) {
        ret = open_profile(profile);
    }

    return (ret == 0) ? profile : NULL;
}


audio_format_t AudioUSBCenter::getHighestAudioFmt(const int direction) {
    return audio_format_from_pcm_format(get_highest_pcm_fmt(getProfile(direction)));
}


uint32_t AudioUSBCenter::getHighestSampleRate(const int direction) {
    return get_highest_sample_rate(getProfile(direction));
}


uint32_t AudioUSBCenter::getIEMsPeriodUs() {
    return property_get_int32("vendor.audio.usb.iems.period_us", USB_DEFAULT_IEMS_PERIOD_US);
}


uint32_t AudioUSBCenter::getOutPeriodUs() {
    return property_get_int32("vendor.audio.usb.out.period_us", USB_DEFAULT_OUT_PERIOD_US);
}


uint32_t AudioUSBCenter::getInPeriodUs(const bool isFast) {
    if (isFast) {
        return property_get_int32("vendor.audio.usb.in.fast.period_us", USB_DEFAULT_FAST_IN_PERIOD_US);
    }

    return UPLINK_NORMAL_LATENCY_MS * 1000;
}


int AudioUSBCenter::prepareUsb(alsa_device_proxy *proxy,
                               stream_attribute_t *attr,
                               const int direction,
                               const uint32_t period_us,
                               const uint32_t period_count) {
    alsa_device_profile *profile = getProfile(direction);
    struct pcm_config config;

    enum pcm_format fmt = PCM_FORMAT_INVALID;
    unsigned period_size = 0;


    if (!proxy) {
        ASSERT(0);
        return -EINVAL;
    }
    if (!attr) {
        ASSERT(0);
        return -EINVAL;
    }
    if (!profile) {
        return -EINVAL;
    }

    dump_profile(profile);

    memset(&config, 0, sizeof(config));

    /* Rate */
    if (attr->sample_rate == 0) {
        config.rate = get_highest_sample_rate(profile);
        ALOGV("%s(), use highest rate %u", __FUNCTION__, config.rate);
    } else if (profile_is_sample_rate_valid(profile, attr->sample_rate)) {
        if (direction == PCM_IN && !attr->isIEMsSource &&
            (((uint64_t)attr->sample_rate * period_us) % 16000000) != 0) {
            ALOGW("%s(), UL enh not support rate %u x period_us %u. use %u", __FUNCTION__,
                  config.rate, period_us, profile_get_default_sample_rate(profile));
            config.rate = profile_get_default_sample_rate(profile);
        } else {
            ALOGV("%s(), rate %u ok", __FUNCTION__, config.rate);
            config.rate = attr->sample_rate;
        }
    } else {
        config.rate = get_highest_sample_rate(profile);
        ALOGW("%s(), rate %u invalid => use %u", __FUNCTION__,
              attr->sample_rate, config.rate);
    }

    /* Format */
    if (attr->audio_format == AUDIO_FORMAT_DEFAULT) {
        config.format = get_highest_pcm_fmt(profile);
        ALOGV("%s(), use default fmt %s", __FUNCTION__, get_format_string(config.format));
    } else {
        fmt = pcm_format_from_audio_format(attr->audio_format);
        if (profile_is_format_valid(profile, fmt)) {
            config.format = fmt;
            ALOGV("%s(), fmt %s ok", __FUNCTION__, get_format_string(config.format));
        } else {
            config.format = get_highest_pcm_fmt(profile);
            ALOGW("%s(), fmt %d invalid => use %s", __FUNCTION__,
                  attr->audio_format, get_format_string(config.format));
        }
    }

    /* Channels */
    if (attr->num_channels == 0) {
        config.channels = profile_get_default_channel_count(profile);
        ALOGV("%s(), use default ch %d", __FUNCTION__, config.channels);
    } else if (profile_is_channel_count_valid(profile, attr->num_channels)) {
        config.channels = attr->num_channels;
        ALOGV("%s(), ch %d ok", __FUNCTION__, config.channels);
    } else {
        config.channels = profile_get_default_channel_count(profile);
        ALOGW("%s(), ch %u invalid => use %d", __FUNCTION__,
              attr->num_channels, config.channels);
    }

    int ret = proxy_prepare(proxy, profile, &config);
    if (ret) {
        ALOGW("%s(), proxy_prepare change config, ret %d", __FUNCTION__, ret);
        ASSERT(0);
        return ret;
    }

    /* reassign period_size & period_count */
    period_size = ((uint64_t)proxy_get_sample_rate(proxy) * period_us) / 1000000;
    period_size = ((period_size + 15) & ~15); /* round_to_16_mult() */
    if (period_size < profile->min_period_size) {
        period_size = profile->min_period_size;
    } else if (period_size > profile->max_period_size) {
        period_size = profile->max_period_size;
    }

    ALOGD("%s(), alsa proxy: rate %d format %s channels %d " \
          "period size %d => %d, period_count %d => %d",
          __FUNCTION__,
          proxy_get_sample_rate(proxy),
          get_format_string(proxy_get_format(proxy)),
          proxy_get_channel_count(proxy),
          proxy_get_period_size(proxy), period_size,
          proxy_get_period_count(proxy), period_count);

    proxy->alsa_config.period_size = period_size;
    proxy->alsa_config.period_count = period_count;
    if (direction == PCM_OUT) {
        if (config.rate > 96000) {
            proxy->alsa_config.start_threshold = period_size * period_count;
        } else {
            proxy->alsa_config.start_threshold = period_size;
        }
    }

    /* align attr with config */
    attr->sample_rate = proxy_get_sample_rate(proxy);
    attr->audio_format = audio_format_from_pcm_format(proxy_get_format(proxy));
    attr->num_channels = proxy_get_channel_count(proxy);
    if (direction == PCM_OUT) {
        attr->audio_channel_mask = audio_channel_out_mask_from_count(proxy_get_channel_count(proxy));
    } else if (direction == PCM_IN) {
        attr->audio_channel_mask = audio_channel_in_mask_from_count(proxy_get_channel_count(proxy));
    } else {
        attr->audio_channel_mask = AUDIO_CHANNEL_NONE;
    }

    attr->buffer_size = proxy_get_period_size(proxy) *
                        proxy_get_period_count(proxy) *
                        getSizePerFrameByAttr(attr);
    attr->periodUs = ((uint64_t)proxy_get_period_size(proxy) * 1000000) / proxy_get_sample_rate(proxy);
    attr->mInterrupt = ((double)period_us) / 1000;

    return 0;
}


}
