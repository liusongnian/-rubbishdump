#include "audio_sw_mixer.h"

#include <stdint.h>
#include <stdbool.h>

#include <limits.h>

#include <stdarg.h> /* va_list, va_start, va_arg, va_end */


#include <uthash.h> /* uthash */
#include <utlist.h> /* linked list */

#ifdef AUDIO_IN_FREERTOS
#include <tinysys_config.h>

#include <FreeRTOS.h>
#include <FreeRTOSConfig.h>

#include <interrupt.h>
#include <task.h>

#include <audio_log_hal.h>

#include <xgpt.h>
#else /* HAL*/
#include <unistd.h>
#include <pthread.h>

#include <sched.h>
#include <errno.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/prctl.h>

#include "AudioSystemLibCUtil.h"

#include <audio_log.h>
#include <audio_time.h>
#endif

#include <audio_assert.h>

#include <audio_memory_control.h>
#include <audio_lock.h>

#ifdef AUDIO_IN_FREERTOS
#include <audio_fmt_conv.h>
#else
#include <audio_fmt_conv_hal.h>
#endif


#include <wrapped_audio.h>


#ifdef __cplusplus
extern "C" {
#endif



/*
 * =============================================================================
 *                     log
 * =============================================================================
 */

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "audio_sw_mixer"


/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */

#define NUM_TARGET_THREAD (2)
#define EXTRA_TIMEOUT_US_FOR_HIFI (3000)
#define TIMEOUT_TOLERANCE_US (2000)

#define LOG_MASK_ENABLE_ALL ((1 << AUD_SW_MIXER_PRIOR_SIZE) - 1)
#define DEFAULT_LOG_MASK_ENABLE_ADSP \
	(AUD_SW_MIXER_PRIOR_DEEP | \
	 AUD_SW_MIXER_PRIOR_PRIMARY | \
	 AUD_SW_MIXER_PRIOR_MIXER_MUSIC | \
	 AUD_SW_MIXER_PRIOR_VOIP | \
	 AUD_SW_MIXER_PRIOR_PLAYBACK)


#define NORMAL_TARGET_LATENCY_US (21333)
#define MAX_LOW_LATENCY_US (5333)


/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */

/* log */
typedef void (*log_fp_t)(const char *msg, ...);


/* ref */
struct sw_mixer_path_t;
struct sw_mixer_thread_t;
struct sw_mixer_manager_t;


/* mix function pointer */
typedef int (*sw_mixer_mix_fp)(
	struct sw_mixer_path_t *path,
	void *buffer,
	uint32_t bytes);


/* source */
struct sw_mixer_source_t {
	void *self; /* key */
	const char *task_name;
	const char *mixer_name;
	uint32_t key_idx;

	/* from user */
	uint32_t id;
	struct sw_mixer_attr_t attr;

	/* for manager */
	struct sw_mixer_manager_t *manager;
	struct alock_t *lock;
	struct alock_t *path_list_lock;
	struct sw_mixer_path_t *path_list;
	uint32_t num_path;

	uint32_t latency_us;

	log_fp_t log_fp;

	UT_hash_handle hh_manager;
};


/* target */
struct target_time_stamp_t {
	uint64_t open_time;
	uint64_t write_time;
	uint64_t ret_time;
	uint64_t close_time;

	float signal_intv;
	float mix_intv;
	float write_intv;
	float ret_intv;
	float total_intv;
};


struct sw_mixer_target_t {
	void *self; /* key */
	const char *task_name;
	const char *mixer_name;
	uint32_t key_idx;

	/* from user */
	uint32_t id;
	struct sw_mixer_attr_t attr;
	sw_mixer_write_cbk_t write_cbk;
	void *arg;

	/* for manager */
	struct sw_mixer_manager_t *manager;
	struct alock_t *lock;
	struct alock_t *path_list_lock;
	struct sw_mixer_path_t *path_list;
	uint32_t num_path;

	struct alock_t *mix_lock;
	struct alock_t *direct_lock;

	uint32_t latency_us;
	uint32_t thread_timeout_us;
	struct target_time_stamp_t time;

	uint32_t size_per_frame;
	uint32_t mix_unit_cnt;

	void *out_buf;
	void *mix_buf;

	struct sw_mixer_thread_t *thread;

	/* flags */
	uint8_t start;
	uint8_t wait_to_mix;
	uint8_t exit;
	uint8_t host;
	uint8_t is_direct;



	UT_hash_handle hh_manager;
};


/* path */
struct path_time_stamp_t {
	uint64_t open_time;
	uint64_t write_time;
	uint64_t close_time;

	float framework_intv;
	float path_intv;
	float write_intv;
	float total_intv;
};


struct sw_mixer_path_t {
	struct sw_mixer_source_t *source; /* key to hh_target */
	struct sw_mixer_target_t *target; /* key to hh_source */

	const char *mixer_name;
	char name[32];

	struct alock_t *lock;

	void *fmt_hdl;
	struct audio_ringbuf_t fmt_buf;

	uint32_t source_conv_sz;        /* sz after format convert */
	uint32_t target_conv_sz;        /* sz after format convert */
	uint32_t pseudo_buf_sz;         /* like HW buf */
	uint32_t start_threshold;       /* >= th, start */
	uint32_t signal_threshold;      /* <= th, signal write to continue */

	sw_mixer_mix_fp mix_fp;
	struct path_time_stamp_t time;

	/* flags */
	uint8_t start;          /* start to join to mix */
	uint8_t underflow;      /* has been underflow */
	uint8_t suspend;        /* underflow + mute */
	uint8_t data_enough;    /* data enough to mix */

	uint8_t do_mix;         /* join to mix */

	uint8_t is_wait;        /* wait lock or not */
	uint8_t is_sync;        /* is sync write */
	uint8_t is_dup;         /* is dup write */

	uint8_t able_to_direct;


	UT_hash_handle hh_source;
	UT_hash_handle hh_target;
};


/* unsync host thread for unsync path */
struct sw_mixer_thread_t {
	struct alock_t *lock;

	char name[16];

	struct sw_mixer_target_t *target;

	/* flags */
	uint8_t alive;
	uint8_t idle;
	uint8_t avail;


#ifdef AUDIO_IN_FREERTOS
	TaskHandle_t freertos_task;
#else
	pthread_t h_thread;
#endif
};


/* manager */
struct sw_mixer_manager_t {
	uint32_t id;
	struct alock_t *lock;

	struct sw_mixer_source_t *source_list;
	uint32_t source_key_idx;
	struct sw_mixer_target_t *target_list;
	uint32_t target_key_idx;

	struct sw_mixer_thread_t threads[NUM_TARGET_THREAD];
	uint32_t num_thread;
};


/*
 * =============================================================================
 *                     wrap functions
 * =============================================================================
 */

#ifdef AUDIO_IN_FREERTOS
#define aud_fmt_conv_create_wrap aud_fmt_conv_create
#define aud_fmt_conv_process_wrap aud_fmt_conv_process
#define aud_fmt_conv_destroy_wrap aud_fmt_conv_destroy
#else
#define aud_fmt_conv_create_wrap aud_fmt_conv_hal_create
#define aud_fmt_conv_process_wrap aud_fmt_conv_hal_process
#define aud_fmt_conv_destroy_wrap aud_fmt_conv_hal_destroy
#endif



/*
 * =============================================================================
 *                     private global members
 * =============================================================================
 */

static struct sw_mixer_manager_t g_manager[NUM_AUD_SW_MIXER_TYPE];
static uint32_t g_log_mask;



/*
 * =============================================================================
 *                     utilities
 * =============================================================================
 */

inline void usleep_wrap(const uint32_t time_us)
{
#ifdef AUDIO_IN_FREERTOS
	vTaskDelay(pdMS_TO_TICKS(time_us / 1000));
#else
	usleep(time_us);
#endif
}


inline uint64_t get_current_time_us()
{
#ifndef AUDIO_IN_FREERTOS
	struct timespec ts;

	audio_get_timespec_monotonic(&ts);
	return audio_timespec_to_ns(&ts) / 1000;
#else
	return read_xgpt_stamp_ns() / 1000;
#endif
}


#ifndef AUDIO_IN_FREERTOS
int audio_sched_setschedule(pid_t pid, int policy, int sched_priority)
{
	int ret = 0;
	struct sched_param sched_p;

	ret = sched_getparam(pid, &sched_p);
	if (ret)
		AUD_LOG_E("%s(), sched_getparam failed, errno: %d, ret %d", __FUNCTION__, errno, ret);

	sched_p.sched_priority = sched_priority;

	ret = sched_setscheduler(pid, policy, &sched_p);
	if (ret)
		AUD_LOG_E("%s(), sched_setscheduler failed, errno: %d, ret %d", __FUNCTION__, errno, ret);

	return ret;
}
#endif


inline struct sw_mixer_manager_t *get_sw_mixer_manager(const uint32_t id)
{
	if (id >= NUM_AUD_SW_MIXER_TYPE) {
		AUD_LOG_W("%s(), id %u error!!", __FUNCTION__, id);
		return NULL;
	}

	return &g_manager[id];
}


inline int compare_target_order(void *_a, void *_b)
{
	struct sw_mixer_target_t *a = NULL;
	struct sw_mixer_target_t *b = NULL;
	int ret = 0;

	if (!_a || !_b)
		return 0;

	a = (struct sw_mixer_target_t *)_a;
	b = (struct sw_mixer_target_t *)_b;

	if (a->attr.host_order > b->attr.host_order)
		ret = 1;
	else if (a->attr.host_order == b->attr.host_order)
		ret = 0;
	else
		ret = -1;

	return ret;
}


inline int compare_path_source_order(void *_a, void *_b)
{
	struct sw_mixer_path_t *a = NULL;
	struct sw_mixer_path_t *b = NULL;
	int ret = 0;

	if (!_a || !_b)
		return 0;

	a = (struct sw_mixer_path_t *)_a;
	b = (struct sw_mixer_path_t *)_b;

	if (a->source->attr.host_order > b->source->attr.host_order)
		ret = 1;
	else if (a->source->attr.host_order == b->source->attr.host_order)
		ret = 0;
	else
		ret = -1;

	return ret;
}


inline int compare_path_target_order(void *_a, void *_b)
{
	struct sw_mixer_path_t *a = NULL;
	struct sw_mixer_path_t *b = NULL;
	int ret = 0;

	if (!_a || !_b)
		return 0;

	a = (struct sw_mixer_path_t *)_a;
	b = (struct sw_mixer_path_t *)_b;

	if (a->target->attr.host_order > b->target->attr.host_order)
		ret = 1;
	else if (a->target->attr.host_order == b->target->attr.host_order)
		ret = 0;
	else
		ret = -1;

	return ret;
}


static const char *get_mixer_name_by_id(const uint32_t id)
{
	char *name = NULL;

	switch (id) {
	case AUD_SW_MIXER_TYPE_MUSIC:
		name = "MIXER_MUSIC";
		break;
	case AUD_SW_MIXER_TYPE_PLAYBACK:
		name = "MIXER_PLAYBACK";
		break;
	default:
		AUD_LOG_W("%s(), id %u error!!", __FUNCTION__, id);
		name = "MIXER_NULL";
	}

	return name;
}


static const char *get_task_name_by_order(const uint32_t order)
{
	char *name = NULL;

	switch (order) {
	case AUD_SW_MIXER_PRIOR_IEM:
		name = "T_IEMs";
		break;
	case AUD_SW_MIXER_PRIOR_FAST:
		name = "T_FAST";
		break;
	case AUD_SW_MIXER_PRIOR_DEEP:
		name = "T_DEEP";
		break;
	case AUD_SW_MIXER_PRIOR_OFFLOAD:
		name = "T_OFFLOAD";
		break;
	case AUD_SW_MIXER_PRIOR_PRIMARY:
		name = "T_PRIMARY";
		break;
	case AUD_SW_MIXER_PRIOR_VOIP:
		name = "T_VOIP";
		break;
	case AUD_SW_MIXER_PRIOR_BT:
		name = "T_BT";
		break;
	case AUD_SW_MIXER_PRIOR_USB:
		name = "T_USB";
		break;
	case AUD_SW_MIXER_PRIOR_HIFI:
		name = "T_HIFI";
		break;
	case AUD_SW_MIXER_PRIOR_PLAYBACK:
		name = "T_PLAYBACK";
		break;
	case AUD_SW_MIXER_PRIOR_MIXER_MUSIC:
		name = "T_MUSIC";
		break;
	case AUD_SW_MIXER_PRIOR_FM_ADSP:
		name = "T_FM";
		break;
	default:
		AUD_LOG_W("%s(), order %u error!!", __FUNCTION__, order);
		name = "T_NULL";
	}

	return name;
}


inline uint64_t get_size_per_frame(const struct aud_fmt_cfg_t *fmt_cfg)
{
	return fmt_cfg->num_channels * AUDIO_BYTES_PER_SAMPLE(fmt_cfg->audio_format);
}


inline uint64_t get_size_per_second(const struct aud_fmt_cfg_t *fmt_cfg)
{
	return fmt_cfg->sample_rate * get_size_per_frame(fmt_cfg);
}


inline uint64_t get_buffer_lantency_us(
	const struct aud_fmt_cfg_t *fmt_cfg, uint64_t bytes)
{
	uint32_t size_per_second = 0;

	size_per_second = get_size_per_second(fmt_cfg);
	if (size_per_second == 0)
		return 0;
	return (bytes * (uint64_t)1000000) / (size_per_second);
}


static void config_path_buf_cfg(struct sw_mixer_path_t *path,
				const struct sw_mixer_source_t *source,
				const struct sw_mixer_target_t *target,
				const uint32_t source_conv_sz,
				const uint32_t target_conv_sz)
{
	if (!path || !source || !target || !source_conv_sz || !target_conv_sz) {
		AUD_LOG_W("%s(), %p %p %p %u %u error!!", __FUNCTION__,
			  path, source, target, source_conv_sz, target_conv_sz);
		return;
	}

	if (source_conv_sz == path->source_conv_sz &&
	    target_conv_sz == path->target_conv_sz)
		return;

	path->source_conv_sz = source_conv_sz;
	path->target_conv_sz = target_conv_sz;

	if ((source_conv_sz % target_conv_sz) == 0) { /* 16384 -> 8192, 8192 -> 8192 */
		if ((source->latency_us <= MAX_LOW_LATENCY_US &&
		     target->attr.fmt_cfg.sample_rate <= 96000) ||
		    source->attr.host_order == AUD_SW_MIXER_PRIOR_IEM) {
			path->pseudo_buf_sz = 2 * source_conv_sz;
			path->start_threshold = target_conv_sz;
			path->signal_threshold = 0;
		} else {
			path->pseudo_buf_sz = 2 * source_conv_sz;
			path->start_threshold = 2 * source_conv_sz;
			path->signal_threshold = path->start_threshold;
		}
	} else if (source->latency_us <= MAX_LOW_LATENCY_US &&
		   target->attr.fmt_cfg.sample_rate <= 96000) {
		path->pseudo_buf_sz = 2 * GET_MAX_VALUE(source_conv_sz, target_conv_sz);
		path->start_threshold = target_conv_sz;
		path->signal_threshold = path->start_threshold;
	} else if ((target_conv_sz % source_conv_sz) == 0) { /* 2048 -> 8192 */
		path->pseudo_buf_sz = 2 * target_conv_sz;
		path->start_threshold = 2 * target_conv_sz;
		path->signal_threshold = path->start_threshold;
	} else if (source_conv_sz > target_conv_sz) { /* 8192 -> 7680 */
		path->pseudo_buf_sz = 2 * source_conv_sz;
		path->start_threshold = 2 * source_conv_sz;
		path->signal_threshold = path->start_threshold;
	} else { /* 7680 -> 8192 */
		path->pseudo_buf_sz = 2 * target_conv_sz;
		path->start_threshold = 2 * target_conv_sz;
		path->signal_threshold = path->start_threshold;
	}
}


static void dump_path_list(struct sw_mixer_target_t *target)
{
	struct sw_mixer_path_t *itor_path = NULL;
	struct sw_mixer_path_t *tmp_path = NULL;

	char path_str[64] = {0};
	char dump_str[1024] = {0};
	int ret = 0;

	if (!target)
		return;

	if (target->num_path == 0) {
		AUD_LOG_D("%s(), %-16s, no path", __FUNCTION__, target->mixer_name);
		return;
	}


	ret = snprintf(dump_str, sizeof(dump_str), "%s(), %-16s, ",
		       __FUNCTION__, target->mixer_name);
	if (ret < 0 || ret >= sizeof(dump_str)) {
		AUD_LOG_E("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
			  dump_str, sizeof(dump_str), ret);
		return;
	}

	HASH_ITER(hh_target, target->path_list, itor_path, tmp_path) {
		ret = snprintf(path_str, sizeof(path_str), "%-20s[%u, %u, %s] ",
			       itor_path->name,
			       itor_path->source->attr.buffer_size,
			       itor_path->source->attr.host_order,
			       itor_path->is_sync ? "y" : "n");
		if (ret >= 0 && ret < sizeof(path_str))
			strcat_safe(dump_str, path_str, sizeof(dump_str));
	}

	AUD_LOG_D("%s", dump_str);
}


static bool check_mute_buf(const uint8_t *buf, const uint32_t size)
{
	const uint8_t *data = NULL;
	const uint8_t *end = NULL;

	bool ret = true;

	if (!buf || !size)
		return true;

	end = buf + size;
	for (data = buf; data != end; data++) {
		if (*data) {
			ret = false;
			break;
		}
	}

	return ret;
}



/*
 * =============================================================================
 *                     func declare
 * =============================================================================
 */

#ifndef AUDIO_IN_FREERTOS
static void *sw_mixer_mix_thread_hal(void *arg);
#endif
static void sw_mixer_mix_thread(void *arg);


/*
 * =============================================================================
 *                     init
 * =============================================================================
 */

void init_audio_sw_mixer(void)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_thread_t *thread = NULL;

	uint32_t id = 0;
	int i = 0;


#ifdef AUDIO_IN_FREERTOS
	BaseType_t xReturn = pdFAIL;
#endif
	int ret = 0;


#ifdef AUDIO_IN_FREERTOS
	g_log_mask = DEFAULT_LOG_MASK_ENABLE_ADSP;
#else
	g_log_mask = 0;
#endif


	for (id = 0; id < NUM_AUD_SW_MIXER_TYPE; id++) {
		manager = get_sw_mixer_manager(id);
		if (manager) {
			memset(manager, 0, sizeof(struct sw_mixer_manager_t));
			NEW_ALOCK(manager->lock);

			manager->id = id;

			/* create mixer thread */
			for (i = 0; i < NUM_TARGET_THREAD; i++) {
				thread = &manager->threads[i];

				NEW_ALOCK(thread->lock);

				ret = snprintf(thread->name,
					       sizeof(thread->name),
					       "mix%u:%u", manager->id, manager->num_thread);
				if (ret < 0 || ret >= sizeof(thread->name)) {
					AUD_LOG_E("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
						  thread->name, sizeof(thread->name), ret);
				}

				manager->num_thread++;

				thread->alive = true;
				thread->avail = true;

#ifdef AUDIO_IN_FREERTOS
				xReturn = kal_xTaskCreate(
						  sw_mixer_mix_thread,
						  thread->name,
						  2048,
						  (void *)thread,
						  2,
						  &thread->freertos_task);
				AUD_ASSERT(xReturn == pdPASS);
#else
				ret = pthread_create(&thread->h_thread, NULL,
						     sw_mixer_mix_thread_hal,
						     (void *)thread);
				AUD_ASSERT(ret == 0);
#endif
			}
		}
	}
}


void deinit_audio_sw_mixer(void)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_thread_t *thread = NULL;

	uint32_t id = 0;
	int i = 0;

	for (id = 0; id < NUM_AUD_SW_MIXER_TYPE; id++) {
		manager = get_sw_mixer_manager(id);
		if (manager) {
			/* destroy mixer thread */
			for (i = 0; i < NUM_TARGET_THREAD; i++) {
				thread = &manager->threads[i];
				thread->alive = false;
				LOCK_ALOCK(thread->lock);
				if (thread->idle) {
					SIGNAL_ALOCK(thread->lock);
					thread->idle = false;
				}
				UNLOCK_ALOCK(thread->lock);


#ifdef AUDIO_IN_FREERTOS
#if (INCLUDE_vTaskDelete == 1)
				if (thread->freertos_task != 0) {
					vTaskDelete(thread->freertos_task);
					thread->freertos_task = 0;
				}
#endif
#else
				pthread_join(thread->h_thread, NULL);
#endif
				FREE_ALOCK(thread->lock);
			}

			/* free manager var */
			FREE_ALOCK(manager->lock);
		}
	}
}


/*
 * =============================================================================
 *                     log
 * =============================================================================
 */

static void mixer_log_d(const char *message, ...)
{
	static char printf_msg[256];
	int ret = 0;

	va_list args;
	va_start(args, message);

	ret = vsnprintf(printf_msg, sizeof(printf_msg), message, args);
	if (ret >= 0 && ret < sizeof(printf_msg))
		AUD_LOG_D("%s", printf_msg);

	va_end(args);
}


static void mixer_log_w(const char *message, ...)
{
	static char printf_msg[256];
	int ret = 0;

	va_list args;
	va_start(args, message);

	ret = vsnprintf(printf_msg, sizeof(printf_msg), message, args);
	if (ret >= 0 && ret < sizeof(printf_msg))
		AUD_LOG_W("%s", printf_msg);

	va_end(args);
}


static void mixer_log_dummy(const char *message, ...)
{
	if (message == NULL) {
		AUD_LOG_E("%s(), NULL!! return", __FUNCTION__);
		return;
	}
}


void set_sw_mixer_log_mask(const uint32_t mask)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_source_t *tmp_source = NULL;

	g_log_mask = mask;


	uint32_t id = 0;
	for (id = 0; id < NUM_AUD_SW_MIXER_TYPE; id++) {
		manager = get_sw_mixer_manager(id);

		LOCK_ALOCK(manager->lock);
		HASH_ITER(hh_manager, manager->source_list, source, tmp_source) {
			LOCK_ALOCK(source->lock);
			source->log_fp = (g_log_mask & (1 << source->attr.host_order))
					 ? mixer_log_d
					 : mixer_log_dummy;
			UNLOCK_ALOCK(source->lock);
		}
		UNLOCK_ALOCK(manager->lock);
	}
}


const char *get_sw_mixer_log_mask(void)
{
	static char dump_str[1024] = {0};
	char path_str[64] = {0};
	int ret = 0;

	uint32_t order = 0;

	ret = snprintf(dump_str, sizeof(dump_str), "%d (enable all by %d)\n\n",
		       g_log_mask, LOG_MASK_ENABLE_ALL);
	if (ret < 0 || ret >= sizeof(dump_str)) {
		AUD_LOG_E("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
			  dump_str, sizeof(dump_str), ret);
		memset(dump_str, 0, sizeof(dump_str));
		return dump_str;
	}

	for (order = 0; order < AUD_SW_MIXER_PRIOR_SIZE; order++) {
		ret = snprintf(path_str, sizeof(path_str), "%-16s: %-3s (enable by %d)\n",
			       get_task_name_by_order(order),
			       (g_log_mask & (1 << order)) ? "on" : "off",
			       (1 << order));
		if (ret >= 0 && ret < sizeof(path_str))
			strcat_safe(dump_str, path_str, sizeof(dump_str));
	}

	return dump_str;
}


/*
 * =============================================================================
 *                     mix
 * =============================================================================
 */

inline uint32_t get_thread_timeout_us(struct sw_mixer_target_t *target)
{
#ifndef AUDIO_IN_FREERTOS
	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return NORMAL_TARGET_LATENCY_US + TIMEOUT_TOLERANCE_US;
	}
	return target->latency_us + TIMEOUT_TOLERANCE_US;
#else
	/* default: 4K bytes, 48K x 2ch x 24or32 bits PCM => 10.666 ms */
	uint32_t timeout_us = (10666 * 5) / 3;

	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return timeout_us;
	}

	switch (target->id) {
	case AUD_SW_MIXER_TYPE_MUSIC:
		timeout_us = (target->latency_us * 5) / 3;
		break;
	case AUD_SW_MIXER_TYPE_PLAYBACK:
		timeout_us = (target->latency_us * 2);
		break;
	default:
		AUD_LOG_W("%s(), id %u error!!", __FUNCTION__, target->id);
	}

	if (target->attr.fmt_cfg.sample_rate > 48000)
		timeout_us += EXTRA_TIMEOUT_US_FOR_HIFI;

	return timeout_us;
#endif
}


static bool sw_mixer_check_ready_to_mix(struct sw_mixer_target_t *target,
					const bool dump_warn_log)
{
	struct sw_mixer_path_t *itor_path = NULL;
	struct sw_mixer_path_t *tmp_path = NULL;

	uint32_t num_ready_path = 0;
	bool ready_to_mix = true;

	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return false;
	}

	HASH_ITER(hh_target, target->path_list, itor_path, tmp_path) {
		if (itor_path->start == true) {
			if (itor_path->data_enough == false) {
				if (dump_warn_log)
					AUD_LOG_W("%s underflow!!", itor_path->name);
				ready_to_mix = false;
				break;
			}
			num_ready_path++;
		}
	}
	if (num_ready_path == 0)
		ready_to_mix = false;

	return ready_to_mix;
}


static void sw_mixer_signal_to_mix(struct sw_mixer_target_t *target)
{
	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return;
	}

	LOCK_ALOCK(target->mix_lock);
	if (target->wait_to_mix) {
		if (sw_mixer_check_ready_to_mix(target, false)) {
			SIGNAL_ALOCK(target->mix_lock);
			target->wait_to_mix = false;
		}
	}
	UNLOCK_ALOCK(target->mix_lock);
}


static void sw_mixer_wait_to_mix(struct sw_mixer_target_t *target)
{
	int ret = 0;

	uint64_t start_time_us = 0;
	uint64_t spend_time_us = 0;
	uint64_t left_time_us = 0;

	log_fp_t log_fp = (g_log_mask != 0) ? mixer_log_d : mixer_log_dummy;

	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return;
	}

	LOCK_ALOCK(target->mix_lock);

	start_time_us = get_current_time_us();
	left_time_us = target->thread_timeout_us;

	while (!sw_mixer_check_ready_to_mix(target, false) && left_time_us) {
		/* wait data signal */
		if (target->is_direct == false) {
			target->wait_to_mix = true;
			log_fp("%s(+), %-16s, target %-12s thread wait data",
			       __FUNCTION__,
			       target->mixer_name,
			       target->task_name);
			ret = WAIT_ALOCK_US(target->mix_lock, left_time_us);
			log_fp("%s(-), %-16s, target %-12s thread wait data",
			       __FUNCTION__,
			       target->mixer_name,
			       target->task_name);
			target->wait_to_mix = false;
		}

		/* wake up & check status */
		if (target->exit)
			break;
		if (target->is_direct) {
			/* let thread sleep when direct mode */
			AUD_LOG_D("%s(), %-16s, target %-12s thread enter direct mode",
				  __FUNCTION__,
				  target->mixer_name,
				  target->task_name);
			target->wait_to_mix = true;
			WAIT_ALOCK(target->mix_lock);
			target->wait_to_mix = false;

			LOCK_ALOCK(target->direct_lock); /* wait direct write done */
			AUD_LOG_D("%s(), %-16s, target %-12s thread exit direct mode",
				  __FUNCTION__,
				  target->mixer_name,
				  target->task_name);
			UNLOCK_ALOCK(target->direct_lock);

			/* restart waiting loop */
			start_time_us = get_current_time_us();
			left_time_us = target->thread_timeout_us + target->latency_us; /* + previous write */
			continue;
		}

		if (ret) {
			AUD_LOG_W("%s(), %-16s, target %-12s wait %u us timeout, path exit %d start %d ready %d direct %d",
				  __FUNCTION__,
				  target->mixer_name,
				  target->task_name,
				  (uint32_t)left_time_us,
				  target->exit,
				  target->start,
				  sw_mixer_check_ready_to_mix(target, true),
				  target->is_direct);
		}

		spend_time_us = get_current_time_us() - start_time_us;
		if (target->thread_timeout_us > spend_time_us)
			left_time_us = target->thread_timeout_us - spend_time_us;
		else
			left_time_us = 0;
	}
	UNLOCK_ALOCK(target->mix_lock);
}


static int sw_mixer_do_mix(struct sw_mixer_target_t *target)
{
	struct sw_mixer_path_t *itor_path = NULL;
	struct sw_mixer_path_t *tmp_path = NULL;

	uint32_t target_data_size = 0;
	uint32_t path_data_size = 0;

	uint32_t num_mix_path = 0;
	struct sw_mixer_path_t *a_ready_path = NULL;

	int16_t *copy_buf_16 = NULL;
	int32_t *copy_buf_32 = NULL;

	int32_t *mix_buf_32 = NULL;
	int64_t *mix_buf_64 = NULL;

	int16_t *out_buf_16 = NULL;
	int32_t *out_buf_32 = NULL;

	uint32_t timeout_latency_us = 0;

	log_fp_t log_fp = (g_log_mask != 0) ? mixer_log_d : mixer_log_dummy;

	uint32_t i = 0;

	int ret = 0;


	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return 0;
	}
	target_data_size = target->attr.buffer_size;

	target->time.open_time = get_current_time_us();
	target->time.signal_intv = (target->time.close_time == 0)
				   ? 0.0
				   : (float)(target->time.open_time - target->time.close_time) / 1000;


	LOCK_ALOCK(target->path_list_lock);

	/* check path to do mix or not */
	HASH_ITER(hh_target, target->path_list, itor_path, tmp_path) {
		LOCK_ALOCK(itor_path->lock);

		/* bypass not ready path */
		if (itor_path->start == false) {
			UNLOCK_ALOCK(itor_path->lock);
			continue;
		}

		/* check underflow */
		path_data_size = audio_ringbuf_count(&itor_path->fmt_buf);
		if (path_data_size < target_data_size) {
			itor_path->start = false;
			itor_path->underflow = true;
			AUD_LOG_W("%s(), %-16s, path %-20s, data %u not enough",
				  __FUNCTION__,
				  itor_path->mixer_name,
				  itor_path->name,
				  path_data_size);
			UNLOCK_ALOCK(itor_path->lock);
			continue;
		}
		UNLOCK_ALOCK(itor_path->lock);

		/* join to mix */
		itor_path->do_mix = true;
		num_mix_path++;
		a_ready_path = itor_path;
	}

	if (num_mix_path == 0) { /* re-wait for data to mix */
		UNLOCK_ALOCK(target->path_list_lock);
		AUD_LOG_W("%s(), %-16s, target %-12s, no any ready path to mix!!",
			  __FUNCTION__, target->mixer_name, target->task_name);
		return 0;
	} else if (num_mix_path == 1) { /* call write directly if only one path */
		itor_path = a_ready_path;
		LOCK_ALOCK(itor_path->lock);

		AUD_ASSERT(audio_ringbuf_count(&itor_path->fmt_buf) >= target_data_size);

		audio_ringbuf_copy_to_linear(
			target->out_buf,
			&itor_path->fmt_buf,
			target_data_size);

		path_data_size = audio_ringbuf_count(&itor_path->fmt_buf);
		if (path_data_size < target_data_size)
			itor_path->data_enough = false;

		UNLOCK_ALOCK(itor_path->lock);

		UNLOCK_ALOCK(target->path_list_lock);
		goto DO_WRITE;
	}


	/* mixing (num_mix_path > 1) */
	copy_buf_16 = (int16_t *)target->out_buf; /* reuse out buf as copy buf */
	copy_buf_32 = (int32_t *)target->out_buf; /* reuse out buf as copy buf */
	mix_buf_32 = (int32_t *)target->mix_buf;
	mix_buf_64 = (int64_t *)target->mix_buf;
	out_buf_16 = (int16_t *)target->out_buf;
	out_buf_32 = (int32_t *)target->out_buf;

	memset(target->mix_buf, 0, target_data_size * 2);

	HASH_ITER(hh_target, target->path_list, itor_path, tmp_path) {
		/* bypass not to do mix path */
		if (itor_path->do_mix == false)
			continue;

		LOCK_ALOCK(itor_path->lock);

		AUD_ASSERT(audio_ringbuf_count(&itor_path->fmt_buf) >= target_data_size);

		audio_ringbuf_copy_to_linear(
			target->out_buf,
			&itor_path->fmt_buf,
			target_data_size);

		path_data_size = audio_ringbuf_count(&itor_path->fmt_buf);
		if (path_data_size < target_data_size)
			itor_path->data_enough = false;

		UNLOCK_ALOCK(itor_path->lock);

		/* add sum */
		if (target->attr.fmt_cfg.audio_format == AUDIO_FORMAT_PCM_16_BIT) {
			for (i = 0; i < target->mix_unit_cnt; i++)
				mix_buf_32[i] += (int32_t)copy_buf_16[i];
		} else if (target->attr.fmt_cfg.audio_format == AUDIO_FORMAT_PCM_8_24_BIT ||
			   target->attr.fmt_cfg.audio_format == AUDIO_FORMAT_PCM_32_BIT) {
			for (i = 0; i < target->mix_unit_cnt; i++)
				mix_buf_64[i] += (int64_t)copy_buf_32[i];
		} else
			AUD_ASSERT(0);
	}

	UNLOCK_ALOCK(target->path_list_lock);

	/* limiter */
	if (target->attr.fmt_cfg.audio_format == AUDIO_FORMAT_PCM_16_BIT) {
		for (i = 0; i < target->mix_unit_cnt; i++) {
			if (mix_buf_32[i] > 0 && mix_buf_32[i] > SHRT_MAX)
				out_buf_16[i] = SHRT_MAX;
			else if (mix_buf_32[i] < 0 && mix_buf_32[i] < SHRT_MIN)
				out_buf_16[i] = SHRT_MIN;
			else
				out_buf_16[i] = (int16_t)(mix_buf_32[i]);
		}
	} else if (target->attr.fmt_cfg.audio_format == AUDIO_FORMAT_PCM_8_24_BIT) {
		for (i = 0; i < target->mix_unit_cnt; i++) {
			if (mix_buf_64[i] > 0 && mix_buf_64[i] > 8388607) {
				out_buf_32[i] = 8388607; /* 0x7FFFFF */
			} else if (mix_buf_64[i] < 0 && mix_buf_64[i] < -8388608) {
				out_buf_32[i] = -8388608; /* -0x7FFFFF - 1 */
			} else
				out_buf_32[i] = (int32_t)(mix_buf_64[i]);
		}
	} else if (target->attr.fmt_cfg.audio_format == AUDIO_FORMAT_PCM_32_BIT) {
		for (i = 0; i < target->mix_unit_cnt; i++) {
			if (mix_buf_64[i] > 0 && mix_buf_64[i] > INT_MAX)
				out_buf_32[i] = INT_MAX;
			else if (mix_buf_64[i] < 0 && mix_buf_64[i] < INT_MIN)
				out_buf_32[i] = INT_MIN;
			else
				out_buf_32[i] = (int32_t)(mix_buf_64[i]);
		}
	} else
		AUD_ASSERT(0);


DO_WRITE:
	/* do write */
	target->time.write_time = get_current_time_us();
	target->time.mix_intv = (float)(target->time.write_time - target->time.open_time) / 1000;
	if (target->write_cbk) {
		LOCK_ALOCK(target->direct_lock);
		ret = target->write_cbk(target->out_buf, target_data_size, target->arg);
		UNLOCK_ALOCK(target->direct_lock);
	}
	target->time.ret_time = get_current_time_us();
	target->time.write_intv = (float)(target->time.ret_time - target->time.write_time) / 1000;

	/* signal write done */
	LOCK_ALOCK(target->path_list_lock);

	HASH_ITER(hh_target, target->path_list, itor_path, tmp_path) {
		/* bypass not to do mix path */
		if (itor_path->do_mix == false)
			continue;
		itor_path->do_mix = false;

		LOCK_ALOCK(itor_path->lock);
		if (itor_path->is_wait) {
			path_data_size = audio_ringbuf_count(&itor_path->fmt_buf);
			if (path_data_size <= itor_path->signal_threshold) {
				SIGNAL_ALOCK(itor_path->lock);
				itor_path->is_wait = false;
			}
		}
		UNLOCK_ALOCK(itor_path->lock);
	}
	UNLOCK_ALOCK(target->path_list_lock);

	target->time.close_time = get_current_time_us();
	target->time.ret_intv = (float)(target->time.close_time - target->time.ret_time) / 1000;


#ifdef AUDIO_IN_FREERTOS
	timeout_latency_us = (target->latency_us * 4) / 3;
#else
	timeout_latency_us = target->latency_us + TIMEOUT_TOLERANCE_US;
#endif
	target->time.total_intv = target->time.signal_intv + target->time.mix_intv + target->time.write_intv + target->time.ret_intv;
	if (target->time.total_intv > timeout_latency_us)
		log_fp = mixer_log_w;

	log_fp("%s(), %-16s, target %-12s, sz %u, latency/total: %0.3f/%0.3f (%0.3f+%0.3f+%0.3f+%0.3f) ms",
	       __FUNCTION__,
	       target->mixer_name, target->task_name, target_data_size,
	       ((float)target->latency_us) / 1000,
	       target->time.total_intv,
	       target->time.signal_intv,
	       target->time.mix_intv,
	       target->time.write_intv,
	       target->time.ret_intv);

	return ret;
}


#ifndef AUDIO_IN_FREERTOS
static void *sw_mixer_mix_thread_hal(void *arg)
{
	struct sw_mixer_thread_t *thread = NULL;
	thread = (struct sw_mixer_thread_t *)arg;
	if (!thread) {
		AUD_LOG_W("%s(), thread NULL!!", __FUNCTION__);
		return NULL;
	}

	prctl(PR_SET_NAME, (unsigned long)thread->name, 0, 0, 0);
	setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);

	sw_mixer_mix_thread(thread);

	return NULL;
}
#endif


static void sw_mixer_mix_thread(void *arg)
{
	struct sw_mixer_thread_t *thread = NULL;
	struct sw_mixer_target_t *target = NULL;

#ifndef AUDIO_IN_FREERTOS
	bool is_rt_thread = false;
#endif

	thread = (struct sw_mixer_thread_t *)arg;
	if (!thread) {
		AUD_LOG_W("%s(), thread NULL!!", __FUNCTION__);
		return;
	}

	LOCK_ALOCK(thread->lock);

	while (thread->alive) {
		/* wait idle -> lunch */
		thread->idle = true;
		AUD_LOG_D("%s(), thread %s idle", __FUNCTION__, thread->name);
		WAIT_ALOCK(thread->lock);
		thread->idle = false;

		/* wake up & check status */
		if (!thread->alive) {
			AUD_LOG_D("%s(), thread %s terminated", __FUNCTION__, thread->name);
			break;
		}
		if (!thread->target || thread->target->exit) {
			AUD_LOG_W("%s(), target detach!!", __FUNCTION__);
			continue;
		}
		target = thread->target;

		/* do mixing */
		LOCK_ALOCK(target->lock);
		AUD_LOG_D("%s(), thread %s running: target %s", __FUNCTION__,
			  thread->name, target->task_name);

#ifndef AUDIO_IN_FREERTOS
		if (target->latency_us <= MAX_LOW_LATENCY_US ||
		    target->attr.host_order == AUD_SW_MIXER_PRIOR_IEM) {
			audio_sched_setschedule(0, SCHED_RR, sched_get_priority_min(SCHED_RR));
			is_rt_thread = true;
		}
#endif

		while (!target->exit) {
			/* wait data if need */
			sw_mixer_wait_to_mix(target);

			/* wake up & check status */
			if (target->exit)
				break;
			if (!target->start)
				continue;

			/* mix */
			sw_mixer_do_mix(target);
		}
		UNLOCK_ALOCK(target->lock);

#ifndef AUDIO_IN_FREERTOS
		if (is_rt_thread) {
			setpriority(PRIO_PROCESS, 0, ANDROID_PRIORITY_AUDIO);
			is_rt_thread = false;
		}
#endif
	}

	UNLOCK_ALOCK(thread->lock);
}



/*
 * =============================================================================
 *                     path
 * =============================================================================
 */

static uint32_t sw_mixer_update_wait_latency_us(
	const struct sw_mixer_path_t *path,
	struct sw_mixer_source_t *source,
	struct sw_mixer_target_t *target,
	uint32_t desire_wait_latency_us)
{
	uint32_t last_write_intv_us = 0;
	uint32_t max_wait_latency_us = 0;

	uint32_t wait_latency_us = 0;

	if (!path || !source || !target || !desire_wait_latency_us)
		return 0;

	last_write_intv_us = (path->time.close_time == 0)
			     ? (get_current_time_us() - path->time.open_time)
			     : (get_current_time_us() - path->time.close_time);
	if (last_write_intv_us >= source->latency_us)
		return 0;

	max_wait_latency_us = source->latency_us - last_write_intv_us;
	if (desire_wait_latency_us > max_wait_latency_us)
		wait_latency_us = max_wait_latency_us;
	else
		wait_latency_us = desire_wait_latency_us;

	return wait_latency_us;
}


static int sw_mixer_mix_fp_sync_write(
	struct sw_mixer_path_t *path,
	void *buffer,
	uint32_t bytes)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;

	void *buf_out = NULL;
	uint32_t size_out = 0;

	uint32_t target_data_size = 0;

	uint32_t data_size = 0;
	uint32_t data_latency_us = 0;
	uint32_t data_size_left = 0;

	uint32_t wait_latency_us = 0;

	log_fp_t log_fp;

	int ret = 0;


	/* check arg */
	if (!path) {
		AUD_LOG_W("%s(), path NULL!!", __FUNCTION__);
		return 0;
	}
	if (!buffer) {
		AUD_LOG_W("%s(), buffer NULL!!", __FUNCTION__);
		return 0;
	}
	if (!bytes) {
		AUD_LOG_W("%s(), bytes 0!!", __FUNCTION__);
		return 0;
	}


	source = path->source;
	target = path->target;
	target_data_size = target->attr.buffer_size;

	source->log_fp("%s(+),   %-16s, path %-20s, bytes  %5u", __FUNCTION__,
		       path->mixer_name, path->name, bytes);

	path->time.open_time = get_current_time_us();
	path->time.framework_intv = (path->time.close_time == 0)
				    ? 0.0
				    : (float)(path->time.open_time - path->time.close_time) / 1000;


	/* sample rate & bit format converter */
	aud_fmt_conv_process_wrap(
		buffer, bytes,
		&buf_out, &size_out,
		path->fmt_hdl);


	/* copy to ring buf */
	LOCK_ALOCK(path->lock);

	config_path_buf_cfg(path, source, target, size_out, target_data_size);

	/* check overflow */
	data_size = audio_ringbuf_count(&path->fmt_buf);
	if (data_size > (4 * path->pseudo_buf_sz)) {
		UNLOCK_ALOCK(path->lock);
		AUD_LOG_E("%s(-),   %-16s, path %-20s, size_out %u rb %u buf_sz %u, drop!!",
			  __FUNCTION__,
			  path->mixer_name, path->name,
			  size_out, data_size, path->pseudo_buf_sz);
		usleep_wrap(source->latency_us);
		path->time.close_time = get_current_time_us();
		return bytes;
	}

	/* copy */
	audio_ringbuf_copy_from_linear(
		&path->fmt_buf,
		buf_out,
		size_out);

	data_size = audio_ringbuf_count(&path->fmt_buf);
	data_latency_us = get_buffer_lantency_us(&target->attr.fmt_cfg, data_size);
	if (data_size >= path->start_threshold) {
		path->start = true;
		path->underflow = false;
		target->start = true;
	}
	if (path->start && data_size >= target_data_size)
		path->data_enough = true;

	/* simulate to sync wait */
	if (path->start == false)
		wait_latency_us = 0;
	else if (source->attr.host_order == AUD_SW_MIXER_PRIOR_IEM)
		wait_latency_us = source->latency_us;
	else if (data_size > path->pseudo_buf_sz)
		wait_latency_us = source->latency_us + TIMEOUT_TOLERANCE_US;
	else if (data_size > path->signal_threshold)
		wait_latency_us = source->latency_us;
	else
		wait_latency_us = 0;

	wait_latency_us = sw_mixer_update_wait_latency_us(
				  path, source, target, wait_latency_us);
	path->is_wait = (wait_latency_us == 0) ? false : true;


	/* signal */
	if (path->data_enough)
		sw_mixer_signal_to_mix(target);

	/* sync pcm timing */
	path->time.write_time = get_current_time_us();
	path->time.path_intv = (float)(path->time.write_time - path->time.open_time) / 1000;
	if (path->is_wait) {
		ret = WAIT_ALOCK_US(path->lock, wait_latency_us);
		path->is_wait = false;
	}
	data_size_left = audio_ringbuf_count(&path->fmt_buf);
	UNLOCK_ALOCK(path->lock);


	path->time.close_time = get_current_time_us();
	path->time.write_intv = (float)(path->time.close_time - path->time.write_time) / 1000;

	path->time.total_intv = path->time.framework_intv + path->time.path_intv + path->time.write_intv;
	log_fp = ((uint32_t)(path->time.total_intv * 1000) > (data_latency_us + TIMEOUT_TOLERANCE_US))
		 ? mixer_log_w
		 : source->log_fp;

	log_fp("%s(-),   %-16s, path %-20s, rb cnt %5u=>%5u, start_th %5u, wait %0.3f ms %7s, latency/total: %0.3f/%0.3f (%0.3f+%0.3f+%0.3f) ms",
	       __FUNCTION__,
	       path->mixer_name, path->name,
	       data_size, data_size_left, path->start_threshold,
	       ((float)wait_latency_us) / 1000,
	       (wait_latency_us != 0 && ret == 0) ? "sync" : "unsync",
	       ((float)source->latency_us) / 1000,
	       path->time.total_intv,
	       path->time.framework_intv,
	       path->time.path_intv,
	       path->time.write_intv);

	return bytes;
}


static int sw_mixer_mix_fp_unsync_write(
	struct sw_mixer_path_t *path,
	void *buffer,
	uint32_t bytes)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;

	void *buf_out = NULL;
	uint32_t size_out = 0;

	uint32_t target_data_size = 0;

	uint32_t data_size = 0;
	uint32_t data_latency_us = 0;
	uint32_t data_size_left = 0;

	uint32_t wait_latency_us = 0;

	log_fp_t log_fp;

	int ret = 0;


	/* check arg */
	if (!path) {
		AUD_LOG_W("%s(), path NULL!!", __FUNCTION__);
		return 0;
	}
	if (!buffer) {
		AUD_LOG_W("%s(), buffer NULL!!", __FUNCTION__);
		return 0;
	}
	if (!bytes) {
		AUD_LOG_W("%s(), bytes 0!!", __FUNCTION__);
		return 0;
	}


	source = path->source;
	target = path->target;
	target_data_size = target->attr.buffer_size;

	source->log_fp("%s(+), %-16s, path %-20s, bytes  %5u", __FUNCTION__,
		       path->mixer_name, path->name, bytes);

	path->time.open_time = get_current_time_us();
	path->time.framework_intv = (path->time.close_time == 0)
				    ? 0.0
				    : (float)(path->time.open_time - path->time.close_time) / 1000;


	/* sample rate & bit format converter */
	aud_fmt_conv_process_wrap(
		buffer, bytes,
		&buf_out, &size_out,
		path->fmt_hdl);


	/* copy to ring buf */
	LOCK_ALOCK(path->lock);

	config_path_buf_cfg(path, source, target, size_out, target_data_size);

	/* check overflow */
	data_size = audio_ringbuf_count(&path->fmt_buf);
	if (data_size > (4 * path->pseudo_buf_sz)) {
		UNLOCK_ALOCK(path->lock);
		AUD_LOG_E("%s(), %-16s, path %-20s, size_out %u rb %u buf_sz %u, drop!!",
			  __FUNCTION__,
			  path->mixer_name, path->name,
			  size_out, data_size, path->pseudo_buf_sz);
		usleep_wrap(source->latency_us);
		path->time.close_time = get_current_time_us();
		return bytes;
	}

	/* copy */
	audio_ringbuf_copy_from_linear(
		&path->fmt_buf,
		buf_out,
		size_out);

	data_size = audio_ringbuf_count(&path->fmt_buf);
	data_latency_us = get_buffer_lantency_us(&target->attr.fmt_cfg, data_size);
	if (data_size >= path->start_threshold) {
		path->start = true;
		path->underflow = false;
		target->start = true;
	}
	if (path->start && data_size >= target_data_size)
		path->data_enough = true;

	/* simulate to sync wait */
	if (path->start == false)
		wait_latency_us = 0;
	else if (data_size > path->pseudo_buf_sz)
		wait_latency_us = source->latency_us + TIMEOUT_TOLERANCE_US;
	else if (data_size > path->signal_threshold)
		wait_latency_us = source->latency_us;
	else
		wait_latency_us = 0;

	wait_latency_us = sw_mixer_update_wait_latency_us(
				  path, source, target, wait_latency_us);
	path->is_wait = (wait_latency_us == 0) ? false : true;


	/* signal */
	if (path->data_enough)
		sw_mixer_signal_to_mix(target);

	/* sync pcm timing */
	path->time.write_time = get_current_time_us();
	path->time.path_intv = (float)(path->time.write_time - path->time.open_time) / 1000;
	if (path->is_wait) {
		ret = WAIT_ALOCK_US(path->lock, wait_latency_us);
		path->is_wait = false;
	}
	data_size_left = audio_ringbuf_count(&path->fmt_buf);
	UNLOCK_ALOCK(path->lock);


	path->time.close_time = get_current_time_us();
	path->time.write_intv = (float)(path->time.close_time - path->time.write_time) / 1000;

	path->time.total_intv = path->time.framework_intv + path->time.path_intv + path->time.write_intv;
	log_fp = ((uint32_t)(path->time.total_intv * 1000) > (data_latency_us + TIMEOUT_TOLERANCE_US))
		 ? mixer_log_w
		 : source->log_fp;

	log_fp("%s(-), %-16s, path %-20s, rb cnt %5u=>%5u, start_th %5u, wait %0.3f ms %6s, latency/total: %0.3f/%0.3f (%0.3f+%0.3f+%0.3f) ms",
	       __FUNCTION__,
	       path->mixer_name, path->name,
	       data_size, data_size_left, path->start_threshold,
	       ((float)wait_latency_us) / 1000,
	       (wait_latency_us != 0 && ret == 0) ? "sync" : "unsync",
	       ((float)source->latency_us) / 1000,
	       path->time.total_intv,
	       path->time.framework_intv,
	       path->time.path_intv,
	       path->time.write_intv);

	return bytes;
}


static int sw_mixer_mix_fp_dup_write(
	struct sw_mixer_path_t *path,
	void *buffer,
	uint32_t bytes)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;

	void *buf_out = NULL;
	uint32_t size_out = 0;

	uint32_t target_data_size = 0;

	uint32_t data_size = 0;
	uint32_t data_latency_us = 0;

	log_fp_t log_fp;


	/* check arg */
	if (!path) {
		AUD_LOG_W("%s(), path NULL!!", __FUNCTION__);
		return 0;
	}
	if (!buffer) {
		AUD_LOG_W("%s(), buffer NULL!!", __FUNCTION__);
		return 0;
	}
	if (!bytes) {
		AUD_LOG_W("%s(), bytes 0!!", __FUNCTION__);
		return 0;
	}


	source = path->source;
	target = path->target;
	target_data_size = target->attr.buffer_size;

	source->log_fp("%s(+), %-16s, path %-20s, bytes  %5u", __FUNCTION__,
		       path->mixer_name, path->name, bytes);

	path->time.open_time = get_current_time_us();
	path->time.framework_intv = (path->time.close_time == 0)
				    ? 0.0
				    : (float)(path->time.open_time - path->time.close_time) / 1000;


	/* sample rate & bit format converter */
	aud_fmt_conv_process_wrap(
		buffer, bytes,
		&buf_out, &size_out,
		path->fmt_hdl);


	/* copy to ring buf */
	LOCK_ALOCK(path->lock);

	config_path_buf_cfg(path, source, target, size_out, target_data_size);

	/* check overflow */
	data_size = audio_ringbuf_count(&path->fmt_buf);
	if (data_size > (4 * path->pseudo_buf_sz)) {
		UNLOCK_ALOCK(path->lock);
		AUD_LOG_E("%s(), %-16s, path %-20s, size_out %u rb %u buf_sz %u, drop!!",
			  __FUNCTION__,
			  path->mixer_name, path->name,
			  size_out, data_size, path->pseudo_buf_sz);
		path->time.close_time = get_current_time_us();
		return bytes;
	}

	/* copy */
	audio_ringbuf_copy_from_linear(
		&path->fmt_buf,
		buf_out,
		size_out);

	data_size = audio_ringbuf_count(&path->fmt_buf);
	data_latency_us = get_buffer_lantency_us(&target->attr.fmt_cfg, data_size);
	if (data_size >= path->start_threshold) {
		path->start = true;
		path->underflow = false;
		target->start = true;
	}
	if (path->start && data_size >= target_data_size)
		path->data_enough = true;


	/* signal */
	if (path->data_enough)
		sw_mixer_signal_to_mix(target);

	/* sync pcm timing */
	path->time.write_time = get_current_time_us();
	path->time.path_intv = (float)(path->time.write_time - path->time.open_time) / 1000;
	UNLOCK_ALOCK(path->lock);


	path->time.close_time = get_current_time_us();
	path->time.write_intv = (float)(path->time.close_time - path->time.write_time) / 1000;

	path->time.total_intv = path->time.framework_intv + path->time.path_intv + path->time.write_intv;
	log_fp = ((uint32_t)(path->time.total_intv * 1000) > (data_latency_us + TIMEOUT_TOLERANCE_US))
		 ? mixer_log_w
		 : source->log_fp;

	log_fp("%s(-), %-16s, path %-20s, rb cnt %5u, latency/total: %0.3f/%0.3f (%0.3f+%0.3f+%0.3f) ms",
	       __FUNCTION__,
	       path->mixer_name, path->name,
	       data_size,
	       ((float)source->latency_us) / 1000,
	       path->time.total_intv,
	       path->time.framework_intv,
	       path->time.path_intv,
	       path->time.write_intv);

	return bytes;
}


static int sw_mixer_mix_fp_direct_write(
	struct sw_mixer_path_t *path,
	void *buffer,
	uint32_t bytes)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;

	void *buf_out = NULL;
	uint32_t size_out = 0;

	uint32_t data_latency_us = 0;
	uint32_t data_size_left = 0;

	log_fp_t log_fp;


	/* check arg */
	if (!path) {
		AUD_LOG_W("%s(), path NULL!!", __FUNCTION__);
		return 0;
	}
	if (!buffer) {
		AUD_LOG_W("%s(), buffer NULL!!", __FUNCTION__);
		return 0;
	}
	if (!bytes) {
		AUD_LOG_W("%s(), bytes 0!!", __FUNCTION__);
		return 0;
	}


	source = path->source;
	target = path->target;

	source->log_fp("%s(+),   %-16s, path %-20s, bytes  %5u", __FUNCTION__,
		       path->mixer_name, path->name, bytes);

	path->time.open_time = get_current_time_us();
	path->time.framework_intv = (path->time.close_time == 0)
				    ? 0.0
				    : (float)(path->time.open_time - path->time.close_time) / 1000;


	/* sample rate & bit format converter */
	aud_fmt_conv_process_wrap(
		buffer, bytes,
		&buf_out, &size_out,
		path->fmt_hdl);

	data_latency_us = get_buffer_lantency_us(&target->attr.fmt_cfg, size_out);

	/* start to write */
	LOCK_ALOCK(path->lock);

	data_size_left = audio_ringbuf_count(&path->fmt_buf);
	if (data_size_left != 0) {
		audio_ringbuf_copy_from_linear(
			&path->fmt_buf,
			buf_out,
			size_out);
		audio_ringbuf_copy_to_linear(
			buf_out,
			&path->fmt_buf,
			size_out);
	}

	/* make sure the path should be fine from direct mode to indirect mode */
	path->start = true;
	path->underflow = false;
	path->data_enough = false;
	path->is_wait = false;
	target->start = true;

	path->time.write_time = get_current_time_us();
	path->time.path_intv = (float)(path->time.write_time - path->time.open_time) / 1000;

	/* write to target directly */
	if (target->write_cbk)
		target->write_cbk(buf_out, size_out, target->arg);

	UNLOCK_ALOCK(path->lock);


	path->time.close_time = get_current_time_us();
	path->time.write_intv = (float)(path->time.close_time - path->time.write_time) / 1000;

	path->time.total_intv = path->time.framework_intv + path->time.path_intv + path->time.write_intv;
	log_fp = ((uint32_t)(path->time.total_intv * 1000) > (data_latency_us + TIMEOUT_TOLERANCE_US))
		 ? mixer_log_w
		 : source->log_fp;

	if (source->attr.host_order == AUD_SW_MIXER_PRIOR_DEEP &&
	    (uint32_t)(path->time.write_intv * 1000) > 2 * source->latency_us) /* deep idle */
		log_fp = source->log_fp;

	log_fp("%s(-),   %-16s, path %-20s, size_out %5u, rb %5u, latency/total: %0.3f/%0.3f (%0.3f+%0.3f+%0.3f) ms",
	       __FUNCTION__,
	       path->mixer_name, path->name, size_out, data_size_left,
	       ((float)source->latency_us) / 1000,
	       path->time.total_intv,
	       path->time.framework_intv,
	       path->time.path_intv,
	       path->time.write_intv);

	return bytes;
}


static void sw_mixer_assign_mix_fp(struct sw_mixer_path_t *path)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;

	struct sw_mixer_attr_t *source_attr = NULL;
	struct sw_mixer_attr_t *target_attr = NULL;

	uint64_t size_factor_source = 0;
	uint64_t size_factor_target = 0;

	if (!path) {
		AUD_ASSERT(path);
		return;
	}
	source = path->source;
	target = path->target;
	if (!source || !target) {
		AUD_ASSERT(source);
		AUD_ASSERT(target);
		return;
	}
	source_attr = &source->attr;
	target_attr = &target->attr;
	if (!source_attr || !target_attr) {
		AUD_ASSERT(source_attr);
		AUD_ASSERT(target_attr);
		return;
	}

	size_factor_source = source_attr->buffer_size * get_size_per_second(&target_attr->fmt_cfg);
	size_factor_target = target_attr->buffer_size * get_size_per_second(&source_attr->fmt_cfg);
	if (!target->host) {
		path->mix_fp = sw_mixer_mix_fp_dup_write;
		path->is_dup = true;
#ifdef AUDIO_IN_FREERTOS
	} else if (source_attr->host_order == AUD_SW_MIXER_PRIOR_VOIP) {
		path->mix_fp = sw_mixer_mix_fp_unsync_write;
		path->is_sync = false;
	} else if ((source_attr->buffer_size % 1024) != 0) {
		path->mix_fp = sw_mixer_mix_fp_unsync_write;
		path->is_sync = false;
#endif
	} else if (size_factor_target && (size_factor_source % size_factor_target) == 0) {
		path->mix_fp = sw_mixer_mix_fp_sync_write;
		path->is_sync = true;
	} else {
		path->mix_fp = sw_mixer_mix_fp_unsync_write;
		path->is_sync = false;
	}
}


static void sw_mixer_manager_create_path(
	struct sw_mixer_source_t *source,
	struct sw_mixer_target_t *target)
{
	struct sw_mixer_path_t *path = NULL;

	int ret = 0;


	/* check arg */
	if (!source) {
		AUD_LOG_W("%s(), source NULL!!", __FUNCTION__);
		return;
	}
	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return;
	}


	/* create path */
	AUDIO_ALLOC_STRUCT(struct sw_mixer_path_t, path);
	if (!path)
		return;

	path->source = source;
	path->target = target;

	path->mixer_name = source->mixer_name;
	ret = snprintf(path->name, sizeof(path->name),
		       "%s=>%s", source->task_name, target->task_name);
	if (ret < 0 || ret >= sizeof(path->name)) {
		AUD_LOG_E("%s(), snprintf %s fail!! sz %zu, ret %d", __FUNCTION__,
			  path->name, sizeof(path->name), ret);
	}

	AUD_LOG_D("%s(), %-16s, path %-20s, " \
		  "source: order %u sz %u fmt %u ch %d rate %u latency_us %u, " \
		  "target: order %u sz %u fmt %u ch %d rate %u latency_us %u",
		  __FUNCTION__,
		  path->mixer_name,
		  path->name,
		  source->attr.host_order,
		  source->attr.buffer_size,
		  source->attr.fmt_cfg.audio_format,
		  source->attr.fmt_cfg.num_channels,
		  source->attr.fmt_cfg.sample_rate,
		  source->latency_us,
		  target->attr.host_order,
		  target->attr.buffer_size,
		  target->attr.fmt_cfg.audio_format,
		  target->attr.fmt_cfg.num_channels,
		  target->attr.fmt_cfg.sample_rate,
		  target->latency_us);


	NEW_ALOCK(path->lock);

	/* SRC */
	ret = aud_fmt_conv_create_wrap(
		      &source->attr.fmt_cfg,
		      &target->attr.fmt_cfg,
		      &path->fmt_hdl);
	if (ret != 0) {
		AUD_ASSERT(ret == 0);
		FREE_ALOCK(path->lock);
		AUDIO_FREE_POINTER(path);
		return;
	}
	path->able_to_direct = (target->latency_us >= NORMAL_TARGET_LATENCY_US &&
				target->attr.host_order != AUD_SW_MIXER_PRIOR_IEM &&
				path->fmt_hdl == NULL); /* SRC takes too long time */

	/* ring buf */
	if (source->latency_us > target->latency_us)
		path->fmt_buf.size  = 2 * source->attr.buffer_size + 16;
	else
		path->fmt_buf.size  = 2 * target->attr.buffer_size + 16;
	AUDIO_ALLOC_CHAR_BUFFER(path->fmt_buf.base, path->fmt_buf.size);
	path->fmt_buf.read  = path->fmt_buf.base;
	path->fmt_buf.write = path->fmt_buf.base;


	/* hook mixing function pointer */
	sw_mixer_assign_mix_fp(path);


	/* add to path list */
	LOCK_ALOCK(source->path_list_lock);
	LOCK_ALOCK(target->path_list_lock);

	HASH_ADD_INORDER(
		hh_source,
		source->path_list,
		target->key_idx, /* key for path of each source */
		sizeof(uint32_t),
		path,
		compare_path_target_order);
	source->num_path = HASH_CNT(hh_source, source->path_list);

	HASH_ADD_INORDER(
		hh_target,
		target->path_list,
		source->key_idx, /* key for path of each target */
		sizeof(uint32_t),
		path,
		compare_path_source_order);
	target->num_path = HASH_CNT(hh_target, target->path_list);

	dump_path_list(target);

	UNLOCK_ALOCK(target->path_list_lock);
	UNLOCK_ALOCK(source->path_list_lock);
}


static void sw_mixer_manager_destroy_path(
	struct sw_mixer_source_t *source,
	struct sw_mixer_target_t *target)
{
	struct sw_mixer_path_t *path_from_source = NULL;
	struct sw_mixer_path_t *path_from_target = NULL;
	struct sw_mixer_path_t *path = NULL;

	struct sw_mixer_path_t *the_only_path = NULL;

	uint32_t data_size = 0;

	uint64_t fmt_start;
	uint32_t fmt_time;


	/* check arg */
	if (!source) {
		AUD_LOG_W("%s(), source NULL!!", __FUNCTION__);
		return;
	}
	if (!target) {
		AUD_LOG_W("%s(), target NULL!!", __FUNCTION__);
		return;
	}


	AUD_LOG_D("%s(), %-16s, path %s=>%s",
		  __FUNCTION__, source->mixer_name,
		  source->task_name, target->task_name);


	/* lock */
	LOCK_ALOCK(source->path_list_lock);
	LOCK_ALOCK(target->path_list_lock);

	/* get path && delete from list */
	HASH_FIND(hh_source,
		  source->path_list,
		  &target->key_idx,
		  sizeof(uint32_t),
		  path_from_source);
	AUD_ASSERT(path_from_source != NULL);
	HASH_DELETE(hh_source,
		    source->path_list,
		    path_from_source);
	source->num_path = HASH_CNT(hh_source, source->path_list);

	HASH_FIND(hh_target,
		  target->path_list,
		  &source->key_idx,
		  sizeof(uint32_t),
		  path_from_target);
	AUD_ASSERT(path_from_target != NULL);
	HASH_DELETE(hh_target,
		    target->path_list,
		    path_from_target);
	target->num_path = HASH_CNT(hh_target, target->path_list);
	if (target->num_path == 0)
		target->start = false;
	else if (target->num_path == 1) {
		/* update mixing function pointer */
		the_only_path = target->path_list;
		sw_mixer_assign_mix_fp(the_only_path);
	}

	AUD_ASSERT(path_from_source == path_from_target);
	path = path_from_source;

	dump_path_list(target);

	/* broadcast to update status */
	sw_mixer_signal_to_mix(target);

	/* unlock */
	UNLOCK_ALOCK(target->path_list_lock);
	UNLOCK_ALOCK(source->path_list_lock);



	/* destroy path */
	LOCK_ALOCK(path->lock);

	data_size = audio_ringbuf_count(&path->fmt_buf);
	AUDIO_FREE_POINTER(path->fmt_buf.base);

	fmt_start = get_current_time_us();
	aud_fmt_conv_destroy_wrap(path->fmt_hdl);
	fmt_time = get_current_time_us() - fmt_start;

	UNLOCK_ALOCK(path->lock);

	FREE_ALOCK(path->lock);
	AUDIO_FREE_POINTER(path);

	if (data_size != 0 || fmt_time > 3000) {
		AUD_LOG_W("%s(-), %-16s, path %s=>%s, rb cnt %u, conv close %.3f ms",
			  __FUNCTION__, source->mixer_name,
			  source->task_name, target->task_name,
			  data_size, ((float)fmt_time) / 1000);
	}
}


/*
 * =============================================================================
 *                     source
 * =============================================================================
 */

void *sw_mixer_source_attach(
	const uint32_t id,
	struct sw_mixer_attr_t *attr)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;
	struct sw_mixer_target_t *tmp_target = NULL;


	/* check arg */
	if (id >= NUM_AUD_SW_MIXER_TYPE) {
		AUD_LOG_W("%s(), id %u error!!", __FUNCTION__, id);
		return NULL;
	}
	if (!attr) {
		AUD_LOG_W("%s(), attr NULL!!", __FUNCTION__);
		return NULL;
	}
	if (!attr->buffer_size) {
		AUD_LOG_W("%s(), buffer_size 0!!", __FUNCTION__);
		return NULL;
	}


	/* create source */
	AUDIO_ALLOC_STRUCT(struct sw_mixer_source_t, source);
	if (!source)
		return NULL;

	/* init */
	source->self = source;
	source->task_name = get_task_name_by_order(attr->host_order);
	source->mixer_name = get_mixer_name_by_id(id);

	source->id = id;
	memcpy(&source->attr, attr, sizeof(source->attr));

	NEW_ALOCK(source->lock);
	NEW_ALOCK(source->path_list_lock);
	source->latency_us = get_buffer_lantency_us(
				     &source->attr.fmt_cfg,
				     source->attr.buffer_size);
	source->log_fp = (g_log_mask & (1 << attr->host_order))
			 ? mixer_log_d
			 : mixer_log_dummy;

	AUD_LOG_D("%s(), %-16s, name %-12s order %u sz %u fmt %u ch %d rate %u latency_us %u",
		  __FUNCTION__,
		  source->mixer_name,
		  source->task_name,
		  source->attr.host_order,
		  source->attr.buffer_size,
		  source->attr.fmt_cfg.audio_format,
		  source->attr.fmt_cfg.num_channels,
		  source->attr.fmt_cfg.sample_rate,
		  source->latency_us);

	if (source->latency_us == 0) {
		AUD_ASSERT(0);
		FREE_ALOCK(source->path_list_lock);
		FREE_ALOCK(source->lock);
		AUDIO_FREE_POINTER(source);
		return NULL;
	}


	/* get manager */
	manager = get_sw_mixer_manager(id);
	source->manager = manager;


	/* attach */
	LOCK_ALOCK(manager->lock);
	LOCK_ALOCK(source->lock);

	/* assign idx */
	source->key_idx = manager->source_key_idx;
	if (manager->source_key_idx == 0xFFFFFFFF)
		manager->source_key_idx = 0;
	else
		manager->source_key_idx++;

	/* add to list */
	HASH_ADD(
		hh_manager,
		manager->source_list,
		self,
		sizeof(void *),
		source);

	/* create source-target path */
	HASH_ITER(hh_manager, manager->target_list, target, tmp_target) {
		sw_mixer_manager_create_path(source, target);
	}

	UNLOCK_ALOCK(source->lock);
	UNLOCK_ALOCK(manager->lock);

	return source;
}


void sw_mixer_source_update_write_bytes(struct sw_mixer_path_t *path,
					struct sw_mixer_source_t *source,
					struct sw_mixer_target_t *target,
					const uint32_t bytes)
{
	uint32_t data_size = 0;

	if (!path || !source || !target || !bytes)
		return;

	/* user attach source & write attr bytes not match!! */
	source->latency_us = get_buffer_lantency_us(
				     &source->attr.fmt_cfg,
				     bytes);
	AUD_LOG_W("%s(), %-16s, name %-12s order %u sz %u=>%u fmt %u ch %d rate %u latency_us %u",
		  __FUNCTION__,
		  source->mixer_name,
		  source->task_name,
		  source->attr.host_order,
		  source->attr.buffer_size,
		  bytes,
		  source->attr.fmt_cfg.audio_format,
		  source->attr.fmt_cfg.num_channels,
		  source->attr.fmt_cfg.sample_rate,
		  source->latency_us);
	source->attr.buffer_size = bytes;

	/* update new mix fp */
	sw_mixer_assign_mix_fp(path);

	/* resize ring buf */
	LOCK_ALOCK(path->lock);
	data_size = audio_ringbuf_count(&path->fmt_buf);
	if (data_size == 0) {
		AUDIO_FREE_POINTER(path->fmt_buf.base);
		if (source->latency_us > target->latency_us)
			path->fmt_buf.size  = 2 * source->attr.buffer_size + 16;
		else
			path->fmt_buf.size  = 2 * target->attr.buffer_size + 16;
		AUDIO_ALLOC_CHAR_BUFFER(path->fmt_buf.base, path->fmt_buf.size);
		path->fmt_buf.read  = path->fmt_buf.base;
		path->fmt_buf.write = path->fmt_buf.base;
	} else {
		AUD_LOG_W("%s(), %-16s, path %-20s, resize rb fail due to left %u in rb",
			  __FUNCTION__, path->mixer_name, path->name, data_size);
	}
	UNLOCK_ALOCK(path->lock);
}


int sw_mixer_source_write(
	void *hdl,
	void *buffer,
	uint32_t bytes)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;

	struct sw_mixer_path_t *path = NULL;
	struct sw_mixer_path_t *tmp_path = NULL;



	/* check arg */
	if (!hdl) {
		AUD_LOG_W("%s(), hdl NULL!!", __FUNCTION__);
		return 0;
	}
	if (!buffer) {
		AUD_LOG_W("%s(), buffer NULL!!", __FUNCTION__);
		return 0;
	}
	if (!bytes) {
		AUD_LOG_W("%s(), bytes 0!!", __FUNCTION__);
		return 0;
	}


	/* get source */
	source = (struct sw_mixer_source_t *)hdl;


	/* write */
	LOCK_ALOCK(source->lock);
	LOCK_ALOCK(source->path_list_lock);

	HASH_ITER(hh_source, source->path_list, path, tmp_path) {
		target = path->target;

		/* write source data to target directly since no need to mix */
		LOCK_ALOCK(target->mix_lock);
		if (target->num_path == 1 &&
		    path->is_sync &&
		    path->able_to_direct &&
		    audio_ringbuf_count(&path->fmt_buf) == 0) {
			if (target->is_direct == false) {
				/* notify thread to enter direct mode */
				target->is_direct = true;
				if (target->wait_to_mix) {
					SIGNAL_ALOCK(target->mix_lock);
					target->wait_to_mix = false;
				}
			}
			UNLOCK_ALOCK(target->mix_lock);

			LOCK_ALOCK(target->direct_lock);
			sw_mixer_mix_fp_direct_write(path, buffer, bytes);
			UNLOCK_ALOCK(target->direct_lock);
			continue;
		}

		if (target->is_direct == true) {
			/* notify thread to exit direct mode */
			target->is_direct = false;
			if (target->wait_to_mix) {
				SIGNAL_ALOCK(target->mix_lock);
				target->wait_to_mix = false;
			}
		}
		UNLOCK_ALOCK(target->mix_lock);

		/* workaround that user attach size is not as same as write */
		if (bytes != source->attr.buffer_size) {
			sw_mixer_source_update_write_bytes(path,
							   source,
							   target,
							   bytes);
		}

		/* switch to unsync mode when underflow */
		if (path->underflow && path->is_sync && target->num_path > 1) {
			path->mix_fp = sw_mixer_mix_fp_unsync_write;
			path->is_sync = false;
		}

		/* write to path */
		if (path->mix_fp)
			path->mix_fp(path, buffer, bytes);
	}

	UNLOCK_ALOCK(source->path_list_lock);
	UNLOCK_ALOCK(source->lock);

	return bytes;
}


int sw_mixer_get_queued_frames(void *source_hdl)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;
	struct sw_mixer_path_t *path = NULL;

	struct sw_mixer_path_t *itor_path = NULL;
	struct sw_mixer_path_t *tmp_path = NULL;

	uint32_t queued_frames = 0;


	/* check arg */
	if (!source_hdl) {
		AUD_LOG_W("%s(), source_hdl NULL!!", __FUNCTION__);
		return 0;
	}


	/* get source */
	source = (struct sw_mixer_source_t *)source_hdl;


	/* get path */
	LOCK_ALOCK(source->lock);
	LOCK_ALOCK(source->path_list_lock);

	if (!source->path_list) {
		UNLOCK_ALOCK(source->path_list_lock);
		UNLOCK_ALOCK(source->lock);
		AUD_ASSERT(0);
		return 0;
	}

	HASH_ITER(hh_source, source->path_list, itor_path, tmp_path) {
		target = itor_path->target;
		if (target && target->host) {
			path = itor_path;
			break;
		}
	}
	if (path == NULL)
		path = source->path_list;
	target = path->target;
	if (!target) {
		UNLOCK_ALOCK(source->path_list_lock);
		UNLOCK_ALOCK(source->lock);
		AUD_ASSERT(0);
		return 0;
	}

	/* get queued frames */
	LOCK_ALOCK(path->lock);
	queued_frames = audio_ringbuf_count(&path->fmt_buf) /
			target->size_per_frame;
	if (source->attr.fmt_cfg.sample_rate != target->attr.fmt_cfg.sample_rate) {
		queued_frames *= source->attr.fmt_cfg.sample_rate;
		queued_frames /= target->attr.fmt_cfg.sample_rate;
	}
	UNLOCK_ALOCK(path->lock);

	source->log_fp("%s(), %-20s, queued_frames %u", __FUNCTION__,
		       path->name, queued_frames);

	UNLOCK_ALOCK(source->path_list_lock);
	UNLOCK_ALOCK(source->lock);

	return queued_frames;
}


bool sw_mixer_is_write_sync(void *source_hdl)
{
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_path_t *path = NULL;

	bool ret = false;


	/* check arg */
	if (!source_hdl) {
		AUD_LOG_W("%s(), source_hdl NULL!!", __FUNCTION__);
		return 0;
	}


	/* get source */
	source = (struct sw_mixer_source_t *)source_hdl;


	/* get path */
	LOCK_ALOCK(source->lock);
	LOCK_ALOCK(source->path_list_lock);

	if (!source->path_list) {
		UNLOCK_ALOCK(source->path_list_lock);
		UNLOCK_ALOCK(source->lock);
		AUD_ASSERT(0);
		return 0;
	}
	path = source->path_list;
	ret = path->is_sync;


	UNLOCK_ALOCK(source->path_list_lock);
	UNLOCK_ALOCK(source->lock);

	return ret;
}


void sw_mixer_source_detach(void *hdl)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_target_t *target = NULL;
	struct sw_mixer_target_t *tmp_target = NULL;


	/* check arg */
	if (!hdl) {
		AUD_LOG_W("%s(), hdl NULL!!", __FUNCTION__);
		return;
	}


	/* get source */
	source = (struct sw_mixer_source_t *)hdl;
	AUD_LOG_D("%s(), %-16s, name %s",
		  __FUNCTION__, source->mixer_name, source->task_name);

	/* get manager */
	if (!source->manager) {
		AUD_LOG_W("%s(), manager NULL!!", __FUNCTION__);
		return;
	}
	manager = source->manager;


	/* detach */
	LOCK_ALOCK(manager->lock);
	LOCK_ALOCK(source->lock);

	/* destroy source-target path */
	HASH_ITER(hh_manager, manager->target_list, target, tmp_target) {
		sw_mixer_manager_destroy_path(source, target);
	}

	/* delete from list */
	HASH_DELETE(
		hh_manager,
		manager->source_list,
		source);

	UNLOCK_ALOCK(source->lock);
	UNLOCK_ALOCK(manager->lock);

	/* free */
	FREE_ALOCK(source->path_list_lock);
	FREE_ALOCK(source->lock);

	AUDIO_FREE_POINTER(source);
}



/*
 * =============================================================================
 *                     target
 * =============================================================================
 */

void *sw_mixer_target_attach(
	const uint32_t id,
	struct sw_mixer_attr_t *attr,
	sw_mixer_write_cbk_t write_cbk,
	void *arg)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_target_t *target = NULL;
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_source_t *tmp_source = NULL;
	struct sw_mixer_thread_t *thread = NULL;

	uint8_t idx = 0;


	/* check arg */
	if (id >= NUM_AUD_SW_MIXER_TYPE) {
		AUD_LOG_W("%s(), id %u error!!", __FUNCTION__, id);
		return NULL;
	}
	if (!attr) {
		AUD_LOG_W("%s(), attr NULL!!", __FUNCTION__);
		return NULL;
	}
	if (attr->buffer_size == 0) {
		AUD_LOG_W("%s(), buffer_size %u error!!", __FUNCTION__, attr->buffer_size);
		return NULL;
	}
	if (!write_cbk) {
		AUD_LOG_W("%s(), write_cbk NULL!!", __FUNCTION__);
		return NULL;
	}


	/* create target */
	AUDIO_ALLOC_STRUCT(struct sw_mixer_target_t, target);
	if (!target)
		return NULL;

	/* init */
	target->self = target;
	target->task_name = get_task_name_by_order(attr->host_order);
	target->mixer_name = get_mixer_name_by_id(id);

	target->id = id;
	memcpy(&target->attr, attr, sizeof(target->attr));
	target->write_cbk = write_cbk;
	target->arg = arg;

	NEW_ALOCK(target->lock);
	NEW_ALOCK(target->path_list_lock);
	NEW_ALOCK(target->mix_lock);
	NEW_ALOCK(target->direct_lock);


	target->latency_us = get_buffer_lantency_us(
				     &target->attr.fmt_cfg,
				     target->attr.buffer_size);
	target->thread_timeout_us = get_thread_timeout_us(target);
	target->size_per_frame = get_size_per_frame(&target->attr.fmt_cfg);

	switch (target->attr.fmt_cfg.audio_format) {
	case AUDIO_FORMAT_PCM_16_BIT:
		target->mix_unit_cnt = target->attr.buffer_size / 2;
		break;
	case AUDIO_FORMAT_PCM_8_24_BIT:
	case AUDIO_FORMAT_PCM_32_BIT:
		target->mix_unit_cnt = target->attr.buffer_size / 4;
		break;
	default:
		AUD_ASSERT(0);
		target->mix_unit_cnt = target->attr.buffer_size / 4;
	}

	AUDIO_ALLOC_BUFFER(target->out_buf, target->attr.buffer_size);
	AUDIO_ALLOC_BUFFER(target->mix_buf,  2 * target->attr.buffer_size);

	AUD_LOG_D("%s(), %-16s, name %-12s order %u sz %u fmt %u ch %d rate %u latency_us %u",
		  __FUNCTION__,
		  target->mixer_name,
		  target->task_name,
		  target->attr.host_order,
		  target->attr.buffer_size,
		  target->attr.fmt_cfg.audio_format,
		  target->attr.fmt_cfg.num_channels,
		  target->attr.fmt_cfg.sample_rate,
		  target->latency_us);

	if (target->latency_us == 0) {
		AUD_ASSERT(0);
		FREE_ALOCK(target->direct_lock);
		FREE_ALOCK(target->mix_lock);
		FREE_ALOCK(target->path_list_lock);
		FREE_ALOCK(target->lock);
		AUDIO_FREE_POINTER(target);
		return NULL;
	}


	/* get manager */
	manager = get_sw_mixer_manager(id);
	target->manager = manager;


	/* attach */
	LOCK_ALOCK(manager->lock);

	/* assign idx */
	target->key_idx = manager->target_key_idx;
	if (manager->target_key_idx == 0xFFFFFFFF)
		manager->target_key_idx = 0;
	else
		manager->target_key_idx++;

	/* assign thread */
	for (idx = 0; idx < NUM_TARGET_THREAD; idx++) {
		if (manager->threads[idx].avail) {
			manager->threads[idx].avail = false;
			thread = &manager->threads[idx];
			break;
		}
	}
	if (!thread) {
		AUD_ASSERT(0);
		FREE_ALOCK(target->direct_lock);
		FREE_ALOCK(target->mix_lock);
		FREE_ALOCK(target->path_list_lock);
		FREE_ALOCK(target->lock);
		AUDIO_FREE_POINTER(target);
		UNLOCK_ALOCK(manager->lock);
		return NULL;
	}

	LOCK_ALOCK(thread->lock);
	LOCK_ALOCK(target->lock);
	if (thread->target != NULL) {
		AUD_LOG_E("%s(), previous target %s not detach!!",
			  __FUNCTION__, thread->target->task_name);
		AUD_ASSERT(0);
	}
	thread->target = target;
	target->thread = thread;
	if (thread->idle) {
		SIGNAL_ALOCK(thread->lock);
		thread->idle = false;
	}

	/* add to list */
	HASH_ADD_INORDER(
		hh_manager,
		manager->target_list,
		self,
		sizeof(void *),
		target,
		compare_target_order);

	if (HASH_CNT(hh_manager, manager->target_list) == 1)
		target->host = true;

	/* create source-target path */
	HASH_ITER(hh_manager, manager->source_list, source, tmp_source) {
		sw_mixer_manager_create_path(source, target);
	}

	UNLOCK_ALOCK(target->lock);
	UNLOCK_ALOCK(thread->lock);
	UNLOCK_ALOCK(manager->lock);

	return target;
}


void sw_mixer_target_detach(void *hdl)
{
	struct sw_mixer_manager_t *manager = NULL;
	struct sw_mixer_target_t *target = NULL;
	struct sw_mixer_source_t *source = NULL;
	struct sw_mixer_source_t *tmp_source = NULL;
	struct sw_mixer_thread_t *thread;


	/* check arg */
	if (!hdl) {
		AUD_LOG_W("%s(), hdl NULL!!", __FUNCTION__);
		return;
	}


	/* get target */
	target = (struct sw_mixer_target_t *)hdl;
	AUD_LOG_D("%s(), %-16s, name %s",
		  __FUNCTION__, target->mixer_name, target->task_name);

	/* get manager */
	if (!target->manager) {
		AUD_LOG_W("%s(), manager NULL!!", __FUNCTION__);
		return;
	}
	manager = target->manager;

	target->exit = true;


	/* detach */
	LOCK_ALOCK(manager->lock);

	/* terminate thread */
	LOCK_ALOCK(target->mix_lock);
	if (target->wait_to_mix) {
		SIGNAL_ALOCK(target->mix_lock);
		target->wait_to_mix = false;
	}
	UNLOCK_ALOCK(target->mix_lock);

	thread = target->thread;
	LOCK_ALOCK(thread->lock); /* stop here until thread idle */
	LOCK_ALOCK(target->lock);

	thread->target = NULL;
	target->thread = NULL;

	/* destroy source-target path */
	HASH_ITER(hh_manager, manager->source_list, source, tmp_source) {
		sw_mixer_manager_destroy_path(source, target);
	}

	/* delete from list */
	HASH_DELETE(
		hh_manager,
		manager->target_list,
		target);

	thread->avail = true;

	UNLOCK_ALOCK(target->lock);
	UNLOCK_ALOCK(thread->lock);
	UNLOCK_ALOCK(manager->lock);

	/* free */
	AUDIO_FREE_POINTER(target->mix_buf);
	AUDIO_FREE_POINTER(target->out_buf);

	FREE_ALOCK(target->direct_lock);
	FREE_ALOCK(target->mix_lock);
	FREE_ALOCK(target->path_list_lock);
	FREE_ALOCK(target->lock);

	AUDIO_FREE_POINTER(target);
}


#ifdef __cplusplus
}  /* extern "C" */
#endif


