#ifndef SPEECH_FEATURE_DEF_H
#define SPEECH_FEATURE_DEF_H

#include <stdint.h>
namespace android {

/*
 * =============================================================================
 *                     enum
 * =============================================================================
 */

enum TtyModeType {
    AUD_TTY_OFF  = 0,
    AUD_TTY_FULL = 1,
    AUD_TTY_VCO  = 2,
    AUD_TTY_HCO  = 4,
    AUD_TTY_ERR  = -1
};

enum {
    TTY_SWITCH_SKIP = 0,
    TTY_SWITCH_BY_MODE = 1,
    TTY_SWITCH_FORCE_OFF = 2
};

enum {
    AUD_RTT_OFF = 0,
    AUD_RTT_ON = 1
};

#define MAX_SPEECH_FEATURE_CUST_SCENE_LEN      (128)
#define MAX_SPEECH_FEATURE_CUST_INFO_LEN      (256)

/** speech feature type for switch on/off , max 15*/
enum SpeechFeatureType {
    SPEECH_FEATURE_LOOPBACK = 0,
    SPEECH_FEATURE_BTNREC = 1,
    SPEECH_FEATURE_DMNR = 2,
    SPEECH_FEATURE_LSPK_DMNR = 3,
    SPEECH_FEATURE_HAC = 4,
    SPEECH_FEATURE_SUPERVOLUME = 5,
    SPEECH_FEATURE_BT_WB = 6,
    SPEECH_FEATURE_SCENE = 7,
    SPEECH_FEATURE_CUSTOM_INFO = 8,
    NUM_SPEECH_FEATURE
};

enum speech_info_from_modem_type_dynamic_param_t {
    SPEECH_INFO_FROM_MODEM_BAND               = 0,
    SPEECH_INFO_FROM_MODEM_NETWORK            = 1,
    NUM_SPEECH_INFO_FROM_MODEM_TYPE
};

const char SpeechInfoFromModemType[8][128] = {
    "Band",
    "Network"
};

}
#endif // end of SPEECH_FEATURE_DEF_H
