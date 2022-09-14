#ifndef MTK_AUDIO_SW_MIXER_H
#define MTK_AUDIO_SW_MIXER_H

#include <stdint.h>

#include <uthash.h> /* uthash */
#include <utlist.h> /* linked list */

#ifdef AUDIO_IN_FREERTOS
#include <audio_ringbuf_pure.h>
#include <audio_fmt_conv.h>
#else
#include <audio_ringbuf.h>
#include <audio_fmt_conv_hal.h>
#endif



#ifdef __cplusplus
extern "C" {
#endif



/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */

enum {
	AUD_SW_MIXER_TYPE_MUSIC,
	AUD_SW_MIXER_TYPE_PLAYBACK,

	NUM_AUD_SW_MIXER_TYPE
};


enum {
	AUD_SW_MIXER_PRIOR_IEM,
	AUD_SW_MIXER_PRIOR_FAST,
	AUD_SW_MIXER_PRIOR_DEEP,
	AUD_SW_MIXER_PRIOR_OFFLOAD,
	AUD_SW_MIXER_PRIOR_PRIMARY,
	AUD_SW_MIXER_PRIOR_MIXER_MUSIC,
	AUD_SW_MIXER_PRIOR_VOIP,
	AUD_SW_MIXER_PRIOR_FM_ADSP,
	AUD_SW_MIXER_PRIOR_BT,
	AUD_SW_MIXER_PRIOR_USB,
	AUD_SW_MIXER_PRIOR_HIFI,
	AUD_SW_MIXER_PRIOR_PLAYBACK,

	AUD_SW_MIXER_PRIOR_SIZE
};



/*
 * =============================================================================
 *                     struct definition
 * =============================================================================
 */

struct sw_mixer_attr_t {
	uint32_t host_order; /* value: prior high > low */
	uint32_t buffer_size;
	struct aud_fmt_cfg_t fmt_cfg;
};



/*
 * =============================================================================
 *                     type definition
 * =============================================================================
 */

typedef int (*sw_mixer_write_cbk_t)(
	void *buffer,
	uint32_t bytes,
	void *arg);



/*
 * =============================================================================
 *                     public function
 * =============================================================================
 */

/* init */
void init_audio_sw_mixer(void);
void deinit_audio_sw_mixer(void);


/* log */
void set_sw_mixer_log_mask(const uint32_t mask);
const char *get_sw_mixer_log_mask(void);


/* source */
void *sw_mixer_source_attach(
	const uint32_t id,
	struct sw_mixer_attr_t *attr);

int sw_mixer_source_write(
	void *hdl,
	void *buffer,
	uint32_t bytes);

int sw_mixer_get_queued_frames(void *source_hdl);

bool sw_mixer_is_write_sync(void *source_hdl);

void sw_mixer_source_detach(void *hdl);



/* target */
void *sw_mixer_target_attach(
	const uint32_t id,
	struct sw_mixer_attr_t *attr,
	sw_mixer_write_cbk_t write_cbk,
	void *arg);

void sw_mixer_target_detach(void *hdl);


#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* end of MTK_AUDIO_SW_MIXER_H */



