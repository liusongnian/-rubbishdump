#include <audio_dsp_controller.h>

#include "AudioSystemLibCUtil.h"

#include <audio_log.h>
#include <audio_lock.h>

#include <audio_task.h>
#include <errno.h>

#include <audio_dsp_service.h>

#if defined(MTK_AUDIO_SCP_RECOVERY_SUPPORT)
#include <audio_scp_service.h>
#endif


#ifdef __cplusplus
extern "C" {
#endif


#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "audio_dsp_controller"



/*
 * =============================================================================
 *                     MACRO
 * =============================================================================
 */


/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */

struct audio_dsp_cbk_info_t {
    audio_dsp_start_cbk_t start_cbk;
    audio_dsp_stop_cbk_t  stop_cbk;
    void *arg;
};



/*
 * =============================================================================
 *                     private global members
 * =============================================================================
 */

static struct alock_t *g_dsp_controller_lock;

static bool g_dsp_recovery_mode[NUM_OPENDSP_TYPE]; /* single dsp status */
static bool g_recovery_mode; /* overall dsp status by client */

static uint32_t g_recovery_count; /* all dsp fail count */

static uint32_t g_opendsp_id[NUM_OPENDSP_TYPE]; /* for arg to opendsp_id */

static struct alock_t *g_dsp_recovery_lock;
static struct audio_dsp_recovery_lock_t *g_dsp_recovery_lock_list;

static struct audio_dsp_cbk_info_t g_cbk_info;



/*
 * =============================================================================
 *                     private function implementation
 * =============================================================================
 */

static void audio_dsp_ready_event(void *arg) {
    uint32_t *p_opendsp_id = NULL;
    uint32_t opendsp_id = AUDIO_OPENDSP_ID_INVALID;

    p_opendsp_id = (uint32_t *)arg;
    if (p_opendsp_id == NULL) {
        AUD_LOG_E("%s(), p_opendsp_id == NULL!! arg %p", __FUNCTION__, arg);
        return;
    }
    opendsp_id = *p_opendsp_id;
    if (opendsp_id >= NUM_OPENDSP_TYPE) {
        AUD_LOG_E("%s(), opendsp_id %u error!!", __FUNCTION__, opendsp_id);
        return;
    }

    LOCK_ALOCK_MS(g_dsp_controller_lock, MAX_RECOVERY_LOCK_TIMEOUT_MS);

    if (g_dsp_recovery_mode[opendsp_id] == false) {
        AUD_LOG_E("%s(), current g_dsp_recovery_mode[%u] %d duplicated!! bypass",
                  __FUNCTION__,
                  opendsp_id, g_dsp_recovery_mode[opendsp_id]);
        UNLOCK_ALOCK(g_dsp_controller_lock);
        return;
    }
    AUD_LOG_D("%s(), g_dsp_recovery_mode[%u] %d => 0, old g_recovery_count %u",
              __FUNCTION__,
              opendsp_id,
              g_dsp_recovery_mode[opendsp_id],
              g_recovery_count);
    g_dsp_recovery_mode[opendsp_id] = false;

    if (g_recovery_count == 0) {
        AUD_LOG_E("%s(), g_recovery_count %u error!!",
                  __FUNCTION__, g_recovery_count);
        UNLOCK_ALOCK(g_dsp_controller_lock);
        return;
    }
    g_recovery_count--;

    if (g_recovery_count == 0) {
        if (g_cbk_info.start_cbk != NULL) {
            g_cbk_info.start_cbk(g_cbk_info.arg);
        }
    }

    UNLOCK_ALOCK(g_dsp_controller_lock);
}


static void audio_dsp_stop_event(void *arg) {
    uint32_t *p_opendsp_id = NULL;
    uint32_t opendsp_id = AUDIO_OPENDSP_ID_INVALID;

    p_opendsp_id = (uint32_t *)arg;
    if (p_opendsp_id == NULL) {
        AUD_LOG_E("%s(), p_opendsp_id == NULL!! arg %p", __FUNCTION__, arg);
        return;
    }
    opendsp_id = *p_opendsp_id;
    if (opendsp_id >= NUM_OPENDSP_TYPE) {
        AUD_LOG_E("%s(), opendsp_id %u error!!", __FUNCTION__, opendsp_id);
        return;
    }

    LOCK_ALOCK_MS(g_dsp_controller_lock, MAX_RECOVERY_LOCK_TIMEOUT_MS);

    if (g_dsp_recovery_mode[opendsp_id] == true) {
        AUD_LOG_E("%s(), current g_dsp_recovery_mode[%u] %d duplicated!! bypass",
                  __FUNCTION__,
                  opendsp_id, g_dsp_recovery_mode[opendsp_id]);
        UNLOCK_ALOCK(g_dsp_controller_lock);
        return;
    }
    AUD_LOG_D("%s(), g_dsp_recovery_mode[%u] %d => 1, old g_recovery_count %u",
              __FUNCTION__,
              opendsp_id,
              g_dsp_recovery_mode[opendsp_id],
              g_recovery_count);
    g_dsp_recovery_mode[opendsp_id] = true;

    g_recovery_count++;
    if (g_recovery_count == 1) {
        if (g_cbk_info.stop_cbk != NULL) {
            g_cbk_info.stop_cbk(g_cbk_info.arg);
        }
    }

    UNLOCK_ALOCK(g_dsp_controller_lock);
}


static bool audio_dsp_need_recovery(int core_id) {
    int adsp_status = is_adsp_ready(core_id);

    if (adsp_status == 1) {
        return false;
    } else if (adsp_status == -EINVAL) {
        return false; //no support this adsp, no need recovery
    }
    return true;
}


/*
 * =============================================================================
 *                     public function implementation
 * =============================================================================
 */

void audio_dsp_controller_init(void) {
    uint32_t opendsp_id = AUDIO_OPENDSP_ID_INVALID;

    NEW_ALOCK(g_dsp_controller_lock);
    NEW_ALOCK(g_dsp_recovery_lock);

    LOCK_ALOCK_MS(g_dsp_controller_lock, MAX_RECOVERY_LOCK_TIMEOUT_MS);

    g_recovery_count = 0;


    for (opendsp_id = 0; opendsp_id < NUM_OPENDSP_TYPE; opendsp_id++) {
        g_dsp_recovery_mode[opendsp_id] = false;
        g_opendsp_id[opendsp_id] = opendsp_id;
    }

    memset(&g_cbk_info, 0, sizeof(g_cbk_info));


    /* query adsp status when boot / HAL reinit */
    adsp_register_feature(AUDIO_CONTROLLER_FEATURE_ID);
    g_dsp_recovery_mode[AUDIO_OPENDSP_USE_HIFI3_A] =
        audio_dsp_need_recovery(ADSP_A_ID);
    g_dsp_recovery_mode[AUDIO_OPENDSP_USE_HIFI3_B] =
        audio_dsp_need_recovery(ADSP_B_ID);
    adsp_deregister_feature(AUDIO_CONTROLLER_FEATURE_ID);

    /* only use core 0 as nofifier for HiFi3 */
    adsp_cbk_register(
        AUDIO_CONTROLLER_FEATURE_ID,
        audio_dsp_ready_event,
        audio_dsp_stop_event,
        AUDIO_CONTROLLER_FEATURE_PRI,
        &g_opendsp_id[AUDIO_OPENDSP_USE_HIFI3_A]);

#if defined(MTK_AUDIO_SCP_RECOVERY_SUPPORT)
    g_dsp_recovery_mode[AUDIO_OPENDSP_USE_CM4_A] =
        (is_scp_ready() == 1) ? false : true;

    audio_scp_cbk_register(
        audio_dsp_ready_event,
        audio_dsp_stop_event,
        &g_opendsp_id[AUDIO_OPENDSP_USE_CM4_A]);
#endif

    for (opendsp_id = 0; opendsp_id < NUM_OPENDSP_TYPE; opendsp_id++) {
        if (g_dsp_recovery_mode[opendsp_id] == true) {
            AUD_LOG_W("%s(), g_dsp_recovery_mode[%u] = %d", __FUNCTION__,
                      opendsp_id, g_dsp_recovery_mode[opendsp_id]);
            g_recovery_count++;
        }
    }

    AUD_LOG_D("%s(), g_recovery_count: %u", __FUNCTION__, g_recovery_count);
    UNLOCK_ALOCK(g_dsp_controller_lock);
}


void audio_dsp_controller_deinit(void) {
    FREE_ALOCK(g_dsp_recovery_lock);
    FREE_ALOCK(g_dsp_controller_lock);
}


void audio_dsp_cbk_register(
    audio_dsp_start_cbk_t start_cbk,
    audio_dsp_stop_cbk_t stop_cbk,
    void *arg) {
    AUD_LOG_D("%s(), start %p => %p, stop %p => %p, arg %p => %p", __FUNCTION__,
              g_cbk_info.start_cbk, start_cbk,
              g_cbk_info.stop_cbk, stop_cbk,
              g_cbk_info.arg, arg);

    g_cbk_info.start_cbk = start_cbk;
    g_cbk_info.stop_cbk = stop_cbk;
    g_cbk_info.arg = arg;
}


void set_audio_dsp_recovery_mode(const bool recovery_mode) {
    AUD_LOG_D("%s(), recovery_mode: %d => %d", __FUNCTION__,
              g_recovery_mode, recovery_mode);
    g_recovery_mode = recovery_mode;
}


bool get_audio_dsp_recovery_mode(void) {
    return g_recovery_mode;
}


void *new_adsp_rlock(void) {
    struct audio_dsp_recovery_lock_t *lock = NULL;

    AUDIO_ALLOC_STRUCT(struct audio_dsp_recovery_lock_t, lock);
    if (lock == NULL) {
        return NULL;
    }

    NEW_ALOCK(lock->alock);
    if (lock->alock == NULL) {
        AUDIO_FREE_POINTER(lock);
        return NULL;
    }

    LOCK_ALOCK(g_dsp_recovery_lock);
    HASH_ADD(hh, g_dsp_recovery_lock_list, self, sizeof(void *), lock);
    UNLOCK_ALOCK(g_dsp_recovery_lock);

    return lock;
}


void free_adsp_rlock(void *lock) {
    struct audio_dsp_recovery_lock_t *rlock = (struct audio_dsp_recovery_lock_t *)lock;

    if (rlock == NULL) {
        return;
    }

    LOCK_ALOCK(g_dsp_recovery_lock);
    HASH_DELETE(hh, g_dsp_recovery_lock_list, rlock);
    UNLOCK_ALOCK(g_dsp_recovery_lock);

    FREE_ALOCK(rlock->alock);
    AUDIO_FREE_POINTER(rlock);
}


void lock_all_adsp_rlock(void) {
    struct audio_dsp_recovery_lock_t *itor_lock = NULL;
    struct audio_dsp_recovery_lock_t *tmp_lock = NULL;
    int old_count = 0;

    LOCK_ALOCK_MS(g_dsp_recovery_lock, MAX_RECOVERY_LOCK_TIMEOUT_MS);

    HASH_ITER(hh, g_dsp_recovery_lock_list, itor_lock, tmp_lock) {
        old_count = android_atomic_inc(&itor_lock->cnt);
        LOCK_ALOCK_MS(itor_lock->alock, MAX_RECOVERY_LOCK_TIMEOUT_MS);
        old_count = android_atomic_dec(&itor_lock->cnt);
    }
}


void unlock_all_adsp_rlock(void) {
    struct audio_dsp_recovery_lock_t *itor_lock = NULL;
    struct audio_dsp_recovery_lock_t *tmp_lock = NULL;

    HASH_ITER(hh, g_dsp_recovery_lock_list, itor_lock, tmp_lock) {
        UNLOCK_ALOCK(itor_lock->alock);
    }

    UNLOCK_ALOCK(g_dsp_recovery_lock);
}




#ifdef __cplusplus
}  /* extern "C" */
#endif

