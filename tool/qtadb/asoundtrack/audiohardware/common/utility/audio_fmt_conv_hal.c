#include <audio_fmt_conv_hal.h>

#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dlfcn.h> /* dlopen & dlsym */

#include <audio_log.h>
#include <audio_assert.h>

#include <audio_fmt_conv.h>


#ifdef __cplusplus
extern "C" {
#endif

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "audio_fmt_conv_hal"



/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */

#if defined(__LP64__)
#define AUDIO_FMT_CONV_LIB_PATH "/vendor/lib64/libaudiofmtconv.so"
#else
#define AUDIO_FMT_CONV_LIB_PATH "/vendor/lib/libaudiofmtconv.so"
#endif

#define LINK_AUD_FMT_CONV_API_NAME "link_aud_fmt_conv_api"


/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */

typedef int (*link_aud_fmt_conv_api_fp_t)(struct aud_fmt_conv_api_t *api);


/*
 * =============================================================================
 *                     private global members
 * =============================================================================
 */

static void *dlopen_handle; /* for dlopen libaudiofmtconv.so */

static link_aud_fmt_conv_api_fp_t link_aud_fmt_conv_api_fp;
static struct aud_fmt_conv_api_t g_fmt_conv_api;



/*
 * =============================================================================
 *                     for dlopen
 * =============================================================================
 */

void audio_fmt_conv_hal_init(void)
{
    char *dlopen_lib_path = NULL;

    /* get dlopen_lib_path */
    if (access(AUDIO_FMT_CONV_LIB_PATH, R_OK) == 0) {
        dlopen_lib_path = AUDIO_FMT_CONV_LIB_PATH;
    } else {
        AUD_LOG_E("%s(), dlopen_lib_path not found!!", __FUNCTION__);
        AUD_ASSERT(dlopen_lib_path != NULL);
        return;
    }

    /* dlopen for libaudiocomponentenginec.so */
    dlopen_handle = dlopen(dlopen_lib_path, RTLD_NOW);
    if (dlopen_handle == NULL) {
        AUD_LOG_E("dlopen(%s) fail(%s)!!", dlopen_lib_path, dlerror());
        AUD_ASSERT(dlopen_handle != NULL);
        return;
    }

    link_aud_fmt_conv_api_fp = (link_aud_fmt_conv_api_fp_t)dlsym(dlopen_handle,
                               LINK_AUD_FMT_CONV_API_NAME);
    if (link_aud_fmt_conv_api_fp == NULL) {
        AUD_LOG_E("dlsym(%s) for %s fail(%s)!!",
                  dlopen_lib_path, LINK_AUD_FMT_CONV_API_NAME, dlerror());
        AUD_ASSERT(link_aud_fmt_conv_api_fp != NULL);
        return;
    }

    link_aud_fmt_conv_api_fp(&g_fmt_conv_api);
}


void audio_fmt_conv_hal_deinit(void)
{
    memset(&g_fmt_conv_api, 0, sizeof(struct aud_fmt_conv_api_t));
    if (dlopen_handle != NULL) {
        dlclose(dlopen_handle);
        dlopen_handle = NULL;
        link_aud_fmt_conv_api_fp = NULL;
    }
}


/*
 * =============================================================================
 *                     wrap api for hal
 * =============================================================================
 */

/* HAL => aud_fmt_conv_create() */
int aud_fmt_conv_hal_create(struct aud_fmt_cfg_t *source,
                            struct aud_fmt_cfg_t *target,
                            void                **handler)
{
    return g_fmt_conv_api.create(source, target, handler);
}

/* HAL => aud_fmt_conv_process() */
int aud_fmt_conv_hal_process(void  *buf_in,  uint32_t  size_in,
                             void **buf_out, uint32_t *size_out,
                             void  *handler)

{
    return g_fmt_conv_api.process(buf_in, size_in, buf_out, size_out, handler);
}

/* HAL => aud_fmt_conv_destroy() */
int aud_fmt_conv_hal_destroy(void *handler)
{
    return g_fmt_conv_api.destroy(handler);
}




#ifdef __cplusplus
}  /* extern "C" */
#endif

