#ifndef ANDROID_SPEECH_UTILITY_H
#define ANDROID_SPEECH_UTILITY_H

#include <stdint.h>
#include <string.h>

#include <errno.h>

#include <sys/prctl.h> /*  for prctl & PR_SET_NAME */
#include <sys/resource.h> /*  for PRIO_PROCESS */

#include <audio_log.h>

#include <SpeechType.h>

#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdarg.h> /* va_list, va_start, va_arg, va_end */

namespace android {


/*
 * =============================================================================
 *                     typedeh
 * =============================================================================
 */

/** all on => "adb shell setprop af.speech.log.mask 7" */
typedef uint32_t sph_log_level_mask_t;

enum { /* sph_log_level_mask_t */
    SPH_LOG_LEVEL_MASK_DEBUG    = (1 << 0),
    SPH_LOG_LEVEL_MASK_VERBOSE  = (1 << 1),
    SPH_LOG_LEVEL_MASK_TRASH    = (1 << 2)
};

/* dynamic enable log */
static const char *kPropertyKeySpeechLogMask = "vendor.audiohal.speech.log.mask";


/*
 * =============================================================================
 *                    CCCI control
 * =============================================================================
 */

// instead of include <hardware/ccci_intf.h>, only for platform after 93MD to reduce memory
#define CCCI_IOC_MAGIC 'C'
#define CCCI_IOC_GET_MD_STATE   _IOR(CCCI_IOC_MAGIC, 1, unsigned int)
#define CCCI_IOC_SMEM_BASE      _IOR(CCCI_IOC_MAGIC, 48, unsigned int)
#define CCCI_IOC_SMEM_LEN       _IOR(CCCI_IOC_MAGIC, 49, unsigned int)

#define CCCI_DEV_NODE_DRIVER    "/dev/ccci_aud"
#define CCCI_DEV_NODE_SMEM      "/dev/ccci_raw_audio"


/*
 * =============================================================================
 *                     ref struct
 * =============================================================================
 */

struct sph_msg_t;


/*
 * =============================================================================
 *                     utility
 * =============================================================================
 */

void sph_memcpy(void *des, const void *src, uint32_t size);
void sph_memset(void *dest, uint8_t value, uint32_t size);

uint32_t get_uint32_from_mixctrl(const char *property_name);
void set_uint32_to_mixctrl(const char *property_name, const uint32_t value);

uint32_t get_uint32_from_property(const char *property_name);
void set_uint32_to_property(const char *property_name, const uint32_t value);

void get_string_from_property(const char *property_name, char *string, const uint32_t string_size);
void set_string_to_property(const char *property_name, const char *string);


uint16_t sph_sample_rate_enum_to_value(const sph_sample_rate_t sample_rate_enum);
sph_sample_rate_t sph_sample_rate_value_to_enum(const uint16_t sample_rate_value);

inline void dynamic_speech_log(uint32_t sph_log_level_mask, const char *file_path, const char *message, ...){
    if (!file_path || !message) {
        return;
    }

    if ((sph_log_level_mask & get_uint32_from_property(kPropertyKeySpeechLogMask)) == 0) {
        return;
    }

    char printf_msg[256];
    const char *slash = strrchr(file_path, '/');
    const char *file_name = (slash) ? slash + 1 : file_path;

    va_list args;
    va_start(args, message);
    int ret = vsnprintf(printf_msg, sizeof(printf_msg), message, args);
    if (ret >= 0) {
        ALOGD("[%s] %s", file_name, printf_msg);
    }
    va_end(args);
}

/* CCCI */
int speech_ccci_smem_put(int fd, unsigned char *address, unsigned int length);
int speech_ccci_smem_get(unsigned char **address, unsigned int *length);



#define SPH_LOG_D(fmt, arg...) \
    dynamic_speech_log(SPH_LOG_LEVEL_MASK_DEBUG, __FILE__, fmt, ##arg)

#define SPH_LOG_V(fmt, arg...) \
    dynamic_speech_log(SPH_LOG_LEVEL_MASK_VERBOSE, __FILE__, fmt, ##arg)

#define SPH_LOG_T(fmt, arg...) \
    dynamic_speech_log(SPH_LOG_LEVEL_MASK_TRASH, __FILE__, fmt, ##arg)

#ifdef SLOG_ENG
#undef SLOG_ENG
#endif

#ifdef CONFIG_MT_ENG_BUILD
#define SLOG_ENG ALOGD
#else
#define SLOG_ENG ALOGV
#endif


#define PRINT_SPH_MSG(ALOGX, description, p_sph_msg) \
    do { \
        if (description == NULL || (p_sph_msg) == NULL) { \
            break; \
        } \
        if ((p_sph_msg)->buffer_type == SPH_MSG_BUFFER_TYPE_MAILBOX) { \
            ALOGX("%s(), %s, id: 0x%x, param16: 0x%x, param32: 0x%x", \
                  __FUNCTION__, description, \
                  (p_sph_msg)->msg_id,  (p_sph_msg)->param_16bit, (p_sph_msg)->param_32bit); \
        } else if ((p_sph_msg)->buffer_type == SPH_MSG_BUFFER_TYPE_PAYLOAD) { \
            ALOGX("%s(), %s, id: 0x%x, type: %d, size: %u, addr: %p", \
                  __FUNCTION__, description, \
                  (p_sph_msg)->msg_id, (p_sph_msg)->payload_data_type, \
                  (p_sph_msg)->payload_data_size, (p_sph_msg)->payload_data_addr); \
        } else { \
            ALOGW("%s(), buffer_type %d not supporty!!", \
                  __FUNCTION__, (p_sph_msg)->buffer_type); \
        } \
    } while (0)



#define CONFIG_THREAD(thread_name, android_priority) \
    do { \
        snprintf(thread_name, sizeof(thread_name), "%s_%d_%d", __FUNCTION__, getpid(), gettid()); \
        prctl(PR_SET_NAME, (unsigned long)thread_name, 0, 0, 0); \
        int retval = setpriority(PRIO_PROCESS, 0, android_priority); \
        if (retval != 0) { \
            ALOGE("thread %s created. setpriority %s failed!! errno: %d, retval: %d", \
                  thread_name, #android_priority, errno, retval); \
        } else { \
            SLOG_ENG("thread %s created. setpriority %s done", \
                     thread_name, #android_priority); \
        } \
    } while(0)



} /* end namespace android */

#endif /* end of ANDROID_SPEECH_UTILITY_H */

