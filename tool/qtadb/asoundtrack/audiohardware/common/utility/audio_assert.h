#ifndef AUDIO_ASSERT_H
#define AUDIO_ASSERT_H

#include <string.h>

#include <audio_log.h>


#ifdef HAVE_AEE_FEATURE
#include <aee.h>
#define audio_aee_exception(x...) aee_system_exception(x)
#define audio_aee_warning(x...)   aee_system_warning(x)
#else
#define audio_aee_exception(x...)
#define audio_aee_warning(x...)
#endif /* end of HAVE_AEE_FEATURE */


#define AUD_ASSERT_OPT(exp, db_opt) \
    do { \
        if (!(exp)) { \
            const char *filename = NULL; \
            const char *slash = strrchr(__FILE__, '/'); \
            if (slash != NULL) { \
                filename = slash + 1; \
            } else { \
                filename = __FILE__; \
            } \
            AUD_LOG_E("AUD_ASSERT("#exp") fail: \""  __FILE__ "\", %uL", __LINE__); \
            audio_aee_exception("[Audio]", NULL, (db_opt), " %s, %uL", \
                                filename, __LINE__); \
        } \
    } while(0)


#define AUD_ASSERT(exp)    AUD_ASSERT_OPT(exp, DB_OPT_DEFAULT)
#define AUD_ASSERT_FT(exp) AUD_ASSERT_OPT(exp, DB_OPT_DEFAULT | DB_OPT_FTRACE)



#define AUD_WARNING_OPT(string, db_opt) \
    do { \
        const char *filename = NULL; \
        const char *slash = strrchr(__FILE__, '/'); \
        if (slash != NULL) { \
            filename = slash + 1; \
        } else { \
            filename = __FILE__; \
        } \
        AUD_LOG_W("AUD_WARNING(" string"): \""  __FILE__ "\", %uL", __LINE__); \
        audio_aee_warning("[Audio]", NULL, (db_opt), string"! %s, %uL", \
                          filename, __LINE__); \
    } while(0)


#define AUD_WARNING(string)    AUD_WARNING_OPT(string, DB_OPT_DEFAULT)
#define AUD_WARNING_FT(string) AUD_WARNING_OPT(string, DB_OPT_DEFAULT | DB_OPT_FTRACE)



#endif /* end of AUDIO_ASSERT_H */

