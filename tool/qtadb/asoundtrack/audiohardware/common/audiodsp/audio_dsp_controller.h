#ifndef MTK_AUDIO_DSP_CONTROLLER_H
#define MTK_AUDIO_DSP_CONTROLLER_H

#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>

#include <uthash.h> /* uthash */
#include <utlist.h> /* linked list */

#include <audio_log.h>
#include <audio_lock.h>


#ifdef __cplusplus
extern "C" {
#endif



/*
 * =============================================================================
 *                     ref struct
 * =============================================================================
 */

struct alock_t;


/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */

#define MAX_RECOVERY_LOCK_TIMEOUT_MS (10000)


/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */

enum { /* dsp_id_t */
    AUDIO_OPENDSP_USE_CM4_A, /* => SCP_A_ID */
    AUDIO_OPENDSP_USE_CM4_B, /* => SCP_B_ID */
    AUDIO_OPENDSP_USE_HIFI3_A, /* => ADSP_A_ID */
    AUDIO_OPENDSP_USE_HIFI3_B, /* => ADSP_B_ID */
    NUM_OPENDSP_TYPE,
    AUDIO_OPENDSP_ID_INVALID
};



/*
 * =============================================================================
 *                     struct definition
 * =============================================================================
 */

struct audio_dsp_recovery_lock_t {
    void *self;

    struct alock_t *alock;
    volatile int32_t cnt;

    UT_hash_handle hh;
};



/*
 * =============================================================================
 *                     hook function
 * =============================================================================
 */

typedef void (*audio_dsp_start_cbk_t)(void *arg);
typedef void (*audio_dsp_stop_cbk_t)(void *arg);


/*
 * =============================================================================
 *                     public function
 * =============================================================================
 */

void audio_dsp_controller_init(void);
void audio_dsp_controller_deinit(void);

void audio_dsp_cbk_register(
    audio_dsp_start_cbk_t start_cbk,
    audio_dsp_stop_cbk_t stop_cbk,
    void *arg);


void set_audio_dsp_recovery_mode(const bool recovery_mode);
bool get_audio_dsp_recovery_mode(void);


void *new_adsp_rlock(void);
void  free_adsp_rlock(void *lock);

void lock_all_adsp_rlock(void);
void unlock_all_adsp_rlock(void);

#define LOCK_ADSP_RLOCK(lock) \
    do { \
        struct audio_dsp_recovery_lock_t *__rlock = (struct audio_dsp_recovery_lock_t *)lock; \
        const int __k_max_try_cnt = 10; \
        int __try_cnt = 0; \
        if (__rlock != NULL) { \
            for (__try_cnt = 0; __try_cnt < __k_max_try_cnt; __try_cnt++) { \
                if (__rlock->cnt == 0) { \
                    break; \
                } \
                usleep(300); /* avoid RT thread keep unlock and then lock */ \
            } \
            if (__try_cnt == __k_max_try_cnt) { \
                AUD_LOG_W("%s(), free CPU, cnt %d", __FUNCTION__, __rlock->cnt); \
            } \
            LOCK_ALOCK_MS(__rlock->alock, MAX_RECOVERY_LOCK_TIMEOUT_MS); \
        } \
    } while(0)


#define TRYLOCK_ADSP_RLOCK(lock) \
    ({ \
        struct audio_dsp_recovery_lock_t *__rlock = (struct audio_dsp_recovery_lock_t *)lock; \
        int __old_count = 0; \
        int __ret = 0; \
        if (__rlock == NULL) { \
            __ret = -1; \
        } else { \
            __old_count = android_atomic_inc(&__rlock->cnt); \
            __ret = LOCK_ALOCK_TRYLOCK(__rlock->alock); \
            __old_count = android_atomic_dec(&__rlock->cnt); \
        } \
        __ret; \
    })


#define UNLOCK_ADSP_RLOCK(lock) \
    do { \
        struct audio_dsp_recovery_lock_t *__rlock = (struct audio_dsp_recovery_lock_t *)lock; \
        if (__rlock != NULL) { \
            UNLOCK_ALOCK(__rlock->alock); \
        } \
    } while(0)




#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* end of MTK_AUDIO_DSP_CONTROLLER_H */

