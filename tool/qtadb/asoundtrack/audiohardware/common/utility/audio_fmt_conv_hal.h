#ifndef MTK_AUDIO_FMT_CONV_HAL_H
#define MTK_AUDIO_FMT_CONV_HAL_H

#include <audio_fmt_conv.h>



#ifdef __cplusplus
extern "C" {
#endif


/*
 * =============================================================================
 *                     for dlopen
 * =============================================================================
 */

void audio_fmt_conv_hal_init(void);
void audio_fmt_conv_hal_deinit(void);


/*
 * =============================================================================
 *                     wrap api for hal
 * =============================================================================
 */

int aud_fmt_conv_hal_create(struct aud_fmt_cfg_t *source,
                            struct aud_fmt_cfg_t *target,
                            void                **handler);

int aud_fmt_conv_hal_process(void  *buf_in,  uint32_t  size_in,
                             void **buf_out, uint32_t *size_out,
                             void  *handler);

int aud_fmt_conv_hal_destroy(void *handler);





#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* end of MTK_AUDIO_FMT_CONV_HAL_H */

