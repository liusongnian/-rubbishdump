#include <SpeechUtility.h>

#include <stdlib.h>

#include <string.h>

#include "AudioSystemLibCUtil.h"

#include <audio_log.h>
#include <audio_time.h>

#include <AudioALSADriverUtility.h>
#include <AudioLock.h>

#include <sys/mman.h>



#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SpeechUtility"

namespace android {

typedef struct {
    char const *kPropertyKey;
    char const *kMixctrlKey;

} mixctrl_table;

const mixctrl_table prop_mix_table[] = {
    {"vendor.audiohal.modem_1.epof",         "Speech_MD_EPOF"},
    {"vendor.audiohal.modem_1.status",       "Speech_MD_Status"},
    {"vendor.audiohal.wait.ack.msgid",       "Speech_A2M_Msg_ID"},
    {"vendor.audiohal.recovery.mic_mute_on", "Speech_Mic_Mute"},
    {"vendor.audiohal.recovery.dl_mute_on",  "Speech_DL_Mute"},
    {"vendor.audiohal.recovery.ul_mute_on",  "Speech_UL_Mute"},
    {"vendor.audiohal.recovery.phone1.md",   "Speech_Phone1_MD_Idx"},
    {"vendor.audiohal.recovery.phone2.md",   "Speech_Phone2_MD_Idx"},
    {"vendor.audiohal.recovery.phone_id",    "Speech_Phone_ID"},
    {"vendor.streamout.btscowb",             "Speech_BT_SCO_WB"},
    {"vendor.audiohal.speech.shm_init",      "Speech_SHM_Init"},
    {"vendor.audiohal.speech.shm_usip",      "Speech_SHM_USIP"},
    {"vendor.audiohal.speech.shm_widx",      "Speech_SHM_Widx"},
    {"vendor.audiohal.modem_1.headversion",  "Speech_MD_HeadVersion"},
    {"vendor.audiohal.modem_1.version",      "Speech_MD_Version"},
    {"vendor.audiohal.recovery.dynamic_dl_mute_on", "Speech_Dynamic_DL_Mute"},
    {"vendor.audiohal.speech.init_custparam", "Speech_Cust_Param_Init"}
};

#ifndef NUM_MIXCTRL_KEY
#define NUM_MIXCTRL_KEY (sizeof(prop_mix_table) / sizeof(prop_mix_table[0]))
#endif


void sph_memcpy(void *des, const void *src, uint32_t size) {
    char *p_src = (char *)src;
    char *p_des = (char *)des;
    uint32_t i = 0;

    for (i = 0; i < size; i++) {
        p_des[i] = p_src[i];
        asm("" ::: "memory");
    }
    asm volatile("dsb ish": : : "memory");
}


void sph_memset(void *dest, uint8_t value, uint32_t size) {
    char *p_des = (char *)dest;
    uint32_t i = 0;

    for (i = 0; i < size; i++) {
        p_des[i] = value;
        asm("" ::: "memory");
    }
    asm volatile("dsb ish": : : "memory");
}

uint32_t get_uint32_from_mixctrl(const char *property_name) {

    static AudioLock mixctrlLock;
    AL_AUTOLOCK(mixctrlLock);

    uint32_t value;
    char mixctrl_name[PROPERTY_KEY_MAX];
    uint32_t idx = 0;

    static struct mixer *mixer = AudioALSADriverUtility::getInstance()->getMixer();
    if (mixer == NULL) {
        return get_uint32_from_property(property_name);
    }

    for (idx = 0; idx < NUM_MIXCTRL_KEY; ++idx) {
        if (strcmp(prop_mix_table[idx].kPropertyKey, property_name) == 0) {
            strncpy(mixctrl_name, prop_mix_table[idx].kMixctrlKey, PROPERTY_KEY_MAX - 1);
            mixctrl_name[PROPERTY_KEY_MAX - 1] = '\0'; // for coverity check null-terminated string
            break;
        }
    }
    if (idx == NUM_MIXCTRL_KEY) {
        ALOGE("%s(), invalid property name:%s", __FUNCTION__, property_name);
        return get_uint32_from_property(property_name);
    }
    if (strlen(mixctrl_name) == 0) {
        ALOGE("%s(), invalid mixctrl_name", __FUNCTION__);
        return get_uint32_from_property(property_name);
    }
    struct mixer_ctl *ctl = AudioALSADriverUtility::getInstance()->getMixerCtrlByName(mixer, mixctrl_name);
    if (ctl == NULL) {
        value = get_uint32_from_property(property_name);
    } else {
        value = AudioALSADriverUtility::getInstance()->mixerCtrlGetValue(ctl, 0);
    }
    ALOGV("%s(), property:%s, mixctrl:%s, value:0x%x", __FUNCTION__, property_name, mixctrl_name, value);
    return value;
}

void set_uint32_to_mixctrl(const char *property_name, const uint32_t value) {

    static AudioLock mixctrlLock;
    AL_AUTOLOCK(mixctrlLock);

    char mixctrl_name[PROPERTY_KEY_MAX];
    uint32_t idx = 0;

    static struct mixer *mixer = AudioALSADriverUtility::getInstance()->getMixer();
    if (mixer == NULL) {
        set_uint32_to_property(property_name, value);
        return;
    }

    for (idx = 0; idx < NUM_MIXCTRL_KEY; ++idx) {
        if (strcmp(prop_mix_table[idx].kPropertyKey, property_name) == 0) {
            strncpy(mixctrl_name, prop_mix_table[idx].kMixctrlKey, PROPERTY_KEY_MAX - 1);
            mixctrl_name[PROPERTY_KEY_MAX - 1] = '\0'; // for coverity check null-terminated string
            break;
        }
    }
    if (idx == NUM_MIXCTRL_KEY) {
        ALOGE("%s(), Invalid property name:%s", __FUNCTION__, property_name);
        set_uint32_to_property(property_name, value);
        return;
    }

    struct mixer_ctl *ctl = AudioALSADriverUtility::getInstance()->getMixerCtrlByName(mixer, mixctrl_name);
    if (ctl == NULL) {
        set_uint32_to_property(property_name, value);
    } else {
        if (AudioALSADriverUtility::getInstance()->mixerCtrlSetValue(ctl, 0, value)) {
            ALOGE("%s() , Error: %s %d", __FUNCTION__, mixctrl_name, value);
        }
    }
    ALOGV("%s(), property:%s, mixctrl:%s, value:0x%x", __FUNCTION__, property_name, mixctrl_name, value);
    return;
}

uint32_t get_uint32_from_property(const char *property_name) {

    uint32_t retval = 0;
    char property_value[PROPERTY_VALUE_MAX] = {0};
    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg = 0;

    audio_get_timespec_monotonic(&ts_start);
    property_get(property_name, property_value, "0"); // default 0
    audio_get_timespec_monotonic(&ts_stop);

    time_diff_msg = get_time_diff_ms(&ts_start, &ts_stop);
    if ((time_diff_msg) >= 300) {
        ALOGE("%s(), property_name: %s, get %ju ms is too long",
              __FUNCTION__, property_name, time_diff_msg);
    }
    int ret = sscanf(property_value, "%u", &retval);
    if (ret != 1) {
        ALOGE("%s(), sscanf fail! ret:%d", __FUNCTION__, ret);
    }
    return retval;
}


void set_uint32_to_property(const char *property_name, const uint32_t value) {
    if (!property_name) {
        return;
    }
    char property_value[PROPERTY_VALUE_MAX] = {0};
    int ret = snprintf(property_value, sizeof(property_value), "%u", value);
    if (ret < 0 || ret >= sizeof(property_value)) {
        ALOGE("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
              property_value, sizeof(property_value), ret);
    }

    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg = 0;

    audio_get_timespec_monotonic(&ts_start);
    property_set(property_name, property_value);
    audio_get_timespec_monotonic(&ts_stop);

    time_diff_msg = get_time_diff_ms(&ts_start, &ts_stop);
    if ((time_diff_msg) >= 300) {
        ALOGE("%s(), property_name: %s, set %ju ms is too long",
              __FUNCTION__, property_name, time_diff_msg);
    }
}

void get_string_from_property(const char *property_name, char *string, const uint32_t string_size) {
    if (!property_name || !string || !string_size) {
        return;
    }

    char property_string[PROPERTY_VALUE_MAX] = {0};
    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg = 0;

    audio_get_timespec_monotonic(&ts_start);
    property_get(property_name, property_string, ""); // default none
    audio_get_timespec_monotonic(&ts_stop);

    time_diff_msg = get_time_diff_ms(&ts_start, &ts_stop);
    if ((time_diff_msg) >= 300) {
        ALOGE("%s(), property_name: %s, get %ju ms is too long",
              __FUNCTION__, property_name, time_diff_msg);
    }
    strncpy(string, property_string, string_size - 1);
    string[string_size - 1] = '\0'; // for coverity check null-terminated string
}


void set_string_to_property(const char *property_name, const char *string) {
    char property_string[PROPERTY_VALUE_MAX] = {0};
    strncpy(property_string, string, PROPERTY_VALUE_MAX - 1);
    property_string[PROPERTY_VALUE_MAX - 1] = '\0'; // for coverity check null-terminated string

    struct timespec ts_start;
    struct timespec ts_stop;
    uint64_t time_diff_msg = 0;

    audio_get_timespec_monotonic(&ts_start);
    property_set(property_name, property_string);
    audio_get_timespec_monotonic(&ts_stop);

    time_diff_msg = get_time_diff_ms(&ts_start, &ts_stop);
    if ((time_diff_msg) >= 300) {
        ALOGE("%s(), property_name:%s, set %ju ms is too long",
              __FUNCTION__, property_name, time_diff_msg);
    }
}


uint16_t sph_sample_rate_enum_to_value(const sph_sample_rate_t sample_rate_enum) {
    uint16_t sample_rate_value = 32000;

    switch (sample_rate_enum) {
    case SPH_SAMPLE_RATE_08K:
        sample_rate_value = 8000;
        break;
    case SPH_SAMPLE_RATE_16K:
        sample_rate_value = 16000;
        break;
    case SPH_SAMPLE_RATE_32K:
        sample_rate_value = 32000;
        break;
    case SPH_SAMPLE_RATE_48K:
        sample_rate_value = 48000;
        break;
    default:
        ALOGW("%s(), sample_rate_enum %d not support!! use 32000 instead",
              __FUNCTION__, sample_rate_enum);
        sample_rate_value = 32000;
    }

    return sample_rate_value;
}


sph_sample_rate_t sph_sample_rate_value_to_enum(const uint16_t sample_rate_value) {
    sph_sample_rate_t sample_rate_enum = SPH_SAMPLE_RATE_32K;

    switch (sample_rate_value) {
    case 8000:
        sample_rate_enum = SPH_SAMPLE_RATE_08K;
        break;
    case 16000:
        sample_rate_enum = SPH_SAMPLE_RATE_16K;
        break;
    case 32000:
        sample_rate_enum = SPH_SAMPLE_RATE_32K;
        break;
    case 48000:
        sample_rate_enum = SPH_SAMPLE_RATE_48K;
        break;
    default:
        ALOGW("%s(), sample_rate_value %d not support!! use 32000 instead",
              __FUNCTION__, sample_rate_value);
        sample_rate_enum = SPH_SAMPLE_RATE_32K;
    }

    return sample_rate_enum;
}


// CCCI control
/*============================================================================*/

int speech_ccci_smem_put(int fd, unsigned char *address, unsigned int length) {
    if (fd < 0) {
        return -EINVAL;
    }
    close(fd);
    ALOGD("munmap on (%d) for addr=%p, len=%d\n", fd, address, length);
    return munmap(address, length);
}

int speech_ccci_smem_get(unsigned char **address, unsigned int *length) {
    char dev_port[32];
    int fd, ret;
    unsigned int addr = 0, len = 0;

    fd = open(CCCI_DEV_NODE_SMEM, O_RDWR);
    if (fd < 0) {
        ALOGE("open %s failed, errno=%d", dev_port, errno);
        return -ENODEV;
    }
    ret = ioctl(fd, CCCI_IOC_SMEM_BASE, &addr);
    if (ret) {
        ALOGE("CCCI_IOC_SMEM_BASE fail on %s, err=%d\n", dev_port, errno);
        close(fd);
        fd = -1;
        return ret;
    }
    ret = ioctl(fd, CCCI_IOC_SMEM_LEN, &len);
    if (ret) {
        ALOGE("CCCI_IOC_SMEM_LEN fail on %s, err=%d\n", dev_port, errno);
        close(fd);
        fd = -1;
        return ret;
    }
    ALOGD("mmap on %s(%d) for addr=0x%x, len=%d\n", dev_port, fd, addr, len);
    *address = (unsigned char *)mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    *length = len;
    if (*address == MAP_FAILED) {
        ALOGE("mmap on %s failed, %d\n", dev_port, errno);
        close(fd);
        fd = -1;
        return -EFAULT;
    }
    return fd;
}

} /* end namespace android */

