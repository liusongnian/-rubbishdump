#include "AudioALSADriverUtility.h"
#include "AudioSystemLibCUtil.h"
#include <AudioLock.h>
#include <stdint.h>
#include <sys/types.h>
#include <stdlib.h>
#include <AudioALSADeviceParser.h>

#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "AudioALSADriverUtility"

namespace android {

AudioALSADriverUtility *AudioALSADriverUtility::mAudioALSADriverUtility = NULL;
AudioALSADriverUtility *AudioALSADriverUtility::getInstance() {
    static AudioLock mGetInstanceLock;
    AL_AUTOLOCK(mGetInstanceLock);

    if (mAudioALSADriverUtility == NULL) {
        mAudioALSADriverUtility = new AudioALSADriverUtility();
    }
    ASSERT(mAudioALSADriverUtility != NULL);
    return mAudioALSADriverUtility;
}

int AudioALSADriverUtility::GetPropertyValue(const char *ProPerty_Key) {
    int result;
    char value[PROPERTY_VALUE_MAX] = {0};
    property_get(ProPerty_Key, value, "0");
    result = atoi(value);
    return result;
}

int AudioALSADriverUtility::setPropertyValue(const char *ProPerty_Key, int value) {
    int result;
    char valuestring[PROPERTY_VALUE_MAX];
    int ret_str = 0;

    ret_str = snprintf(valuestring, sizeof(valuestring), "%d", value);

    if (ret_str >= 0 && ret_str < sizeof(valuestring)) {
        property_set(ProPerty_Key, valuestring);
    } else {
        ALOGE("%s(), snprintf fail", __FUNCTION__);
        ASSERT(0);
    }
    return 0;
}

AudioALSADriverUtility::AudioALSADriverUtility() :
    mMixer(NULL) {
    ALOGD("%s()", __FUNCTION__);

    mMixer = mixer_open(AudioALSADeviceParser::getInstance()->GetCardIndex());
    ALOGD("mMixer = %p", mMixer);
    ASSERT(mMixer != NULL);
}

AudioALSADriverUtility::~AudioALSADriverUtility() {
    ALOGD("%s()", __FUNCTION__);

    mixer_close(mMixer);
    mMixer = NULL;
}

int AudioALSADriverUtility::mixerCtrlGetValueByName(const char *name, unsigned int id) {
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(mMixer, name);
    int ret;

    if (!ctl) {
        ALOGE("%s(), no mixer ctrl name", __FUNCTION__);
        ASSERT(false);
        return -1;
    }

    ret = mixer_ctl_get_value(ctl, id);
    if (ret) {
        ALOGW("%s() fail, %s id %d", __FUNCTION__, name, id);
    }

    return ret;
}

int AudioALSADriverUtility::mixerCtrlSetValueByName(const char *name, unsigned int id, int value) {
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(mMixer, name);
    int ret;

    if (!ctl) {
        ALOGE("%s(), no mixer ctrl name", __FUNCTION__);
        ASSERT(false);
        return -1;
    }

    ret = mixer_ctl_set_value(ctl, id, value);
    if (ret) {
        ALOGW("%s() fail, %s id %d, value %d", __FUNCTION__, name, id, value);
    }

    return ret;
}

int AudioALSADriverUtility::mixerCtrlSetEnumByName(const char *name, unsigned int id, const char *string) {
    struct mixer_ctl *ctl = mixer_get_ctl_by_name(mMixer, name);
    int ret;

    if (!ctl) {
        ALOGE("%s(), no mixer ctrl name", __FUNCTION__);
        ASSERT(false);
        return -1;
    }

    ret = mixer_ctl_set_enum_by_string(ctl, string);
    if (ret) {
        ALOGW("%s() fail, %s id %d, string %s", __FUNCTION__, name, id, string);
    }

    return ret;
}

struct mixer_ctl *AudioALSADriverUtility::getMixerCtrlByName(struct mixer *mixer, const char *name) {
    return mixer_get_ctl_by_name(mixer, name);
}

int AudioALSADriverUtility::mixerCtrlGetValue(struct mixer_ctl *ctl, unsigned int id) {
    return mixer_ctl_get_value(ctl, id);
}

int AudioALSADriverUtility::mixerCtrlSetValue(struct mixer_ctl *ctl, unsigned int id, int value) {
    return mixer_ctl_set_value(ctl, id, value);
}


} // end of namespace android
