#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "SpeechConfig"
#include <SpeechConfig.h>
#include <utstring.h>
#include "AudioSystemLibUtil.h"
#include <inttypes.h>

#include <AudioUtility.h>//Mutex/assert
#include <audio_memory_control.h>

#define ECHO_REF_PARAM_SIZE 32

namespace android {

#define SPH_DUMP_STR_SIZE (500)
#define SPH_PARAM_UNIT_DUMP_STR_SIZE (1024)
/*
 * =============================================================================
 *                     typedef
 * =============================================================================
 */
struct SPEECH_PARAM_SUPPORT_STRUCT {
    bool isNetworkSupport;
    bool isTTYSupport;
    bool isSuperVolumeSupport;
    bool isSphSceneSupport;

    SPEECH_PARAM_SUPPORT_STRUCT() : isNetworkSupport(0), isTTYSupport(0),
        isSuperVolumeSupport(0), isSphSceneSupport(0) {}
};

struct SPEECH_NETWORK_STRUCT {
    char name[128];
    uint16_t supportBit;//4

    SPEECH_NETWORK_STRUCT() : name(), supportBit(0) {}
};

struct SPEECH_ECHOREF_PARAM_STRUCT {
    /* speech common parameters */
    unsigned short speech_common_para[3];

    SPEECH_ECHOREF_PARAM_STRUCT() : speech_common_para() {}
};

enum PARAM_PRINT_FORMAT_TYPE {
    PARAM_PRINT_FORMAT_HEX,
    PARAM_PRINT_FORMAT_DEC,
    NUM_PARAM_PRINT_FORMAT
};

/*
 * =============================================================================
 *                     Singleton Pattern
 * =============================================================================
 */
SpeechConfig *SpeechConfig::uniqueSpeechConfig = NULL;


SpeechConfig *SpeechConfig::getInstance() {
    static Mutex mGetInstanceLock;
    Mutex::Autolock _l(mGetInstanceLock);
    ALOGV("%s()", __FUNCTION__);

    if (uniqueSpeechConfig == NULL) {
        uniqueSpeechConfig = new SpeechConfig();
    }
    ASSERT(uniqueSpeechConfig != NULL);
    return uniqueSpeechConfig;
}

/*
 * =============================================================================
 *                     class implementation
 * =============================================================================
 */
SpeechConfig::SpeechConfig() {
    ALOGD("%s()", __FUNCTION__);
    mAppHandle = NULL;
    mSpeechParamVerFirst = 0;
    mSpeechParamVerLast = 0;
    mNumSpeechNetwork = 0;

    mSphParamSupport = NULL;
    mListSpeechNetwork = NULL;
    mNameForEachSpeechNetwork = NULL;
    AUDIO_ALLOC_STRUCT(SPEECH_PARAM_SUPPORT_STRUCT, mSphParamSupport);
    AUDIO_ALLOC_STRUCT_ARRAY(SPEECH_NETWORK_STRUCT, 12, mListSpeechNetwork);
    AUDIO_ALLOC_STRUCT_ARRAY(SPEECH_NETWORK_STRUCT, 12, mNameForEachSpeechNetwork);

    if (mSphParamSupport == NULL) {
        ALOGE("%s(), mSphParamSupport == NULL", __FUNCTION__);
    } else if (mListSpeechNetwork == NULL) {
        ALOGE("%s(), mListSpeechNetwork == NULL", __FUNCTION__);
    } else if (mNameForEachSpeechNetwork == NULL) {
        ALOGE("%s(), mNameForEachSpeechNetwork == NULL", __FUNCTION__);
    } else {
        init();
    }
}

SpeechConfig::~SpeechConfig() {
    ALOGD("%s()", __FUNCTION__);
    AUDIO_FREE_POINTER(mNameForEachSpeechNetwork);
    AUDIO_FREE_POINTER(mListSpeechNetwork);
    AUDIO_FREE_POINTER(mSphParamSupport);

}

void SpeechConfig::init() {
    ALOGD("%s()", __FUNCTION__);
    initAppParser();
    initSpeechNetwork();
    initFeatureSupport();

}

void SpeechConfig::initFeatureSupport() {
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
    } else {
        const char *strSphVersion = appOps->appHandleGetFeatureOptionValue(mAppHandle, "SPH_PARAM_VERSION");
        if (strSphVersion != NULL) {
            int ret = sscanf(strSphVersion, "%" SCNd8 ".%" SCNd8, &mSpeechParamVerFirst, &mSpeechParamVerLast);
            if (ret != 2) {
                ALOGE("%s(), sscanf fail! ret:%d", __FUNCTION__, ret);
            }
            switch (mSpeechParamVerFirst) {
            case 2:
                mSphParamSupport->isNetworkSupport = true;
                break;
            case 1:
                mSphParamSupport->isNetworkSupport = true;
                break;
            default:
                mSphParamSupport->isNetworkSupport = false;
                break;
            }
        } else {
            mSpeechParamVerFirst = 0;
            mSpeechParamVerLast = 0;
            mSphParamSupport->isNetworkSupport = false;
        }

        const char *strSphSV = appOps->appHandleGetFeatureOptionValue(mAppHandle, "SPH_PARAM_SV");
        if (strSphSV != NULL) {
            if (strcmp(strSphSV, "yes") == 0) {
                mSphParamSupport->isSuperVolumeSupport = true;
            } else {
                mSphParamSupport->isSuperVolumeSupport = false;
            }
        } else {
            mSphParamSupport->isSuperVolumeSupport = false;
        }
        /*  Scene*/
        AudioType *audioTypeSph = appOps->appHandleGetAudioTypeByName(mAppHandle, "Speech");
        uint8_t numCategoryType = appOps->audioTypeGetNumOfCategoryType(audioTypeSph);
        for (uint8_t idxCateInXml = 0; idxCateInXml < numCategoryType; idxCateInXml++) {
            if (strcmp("Scene", appOps->audioTypeGetCategoryTypeByIndex(audioTypeSph, idxCateInXml)->name) == 0) {
                mSphParamSupport->isSphSceneSupport = true;
                break;
            }
        }
    }
    ALOGD("%s(), SPH_PARAM_VERSION(%x.%x), Network(%d), SuperVolume(%d)",
          __FUNCTION__, mSpeechParamVerFirst, mSpeechParamVerLast,
          mSphParamSupport->isNetworkSupport,
          mSphParamSupport->isSuperVolumeSupport);

}


/*==============================================================================
 *                     SpeechConfig Imeplementation
 *============================================================================*/
bool SpeechConfig::getSpeechParamSupport(const SpeechFeatureType featureType) {
    bool isSupport = false;
    switch (featureType) {
    case SPEECH_FEATURE_SUPERVOLUME:
        isSupport = mSphParamSupport->isSuperVolumeSupport;
        break;
    case SPEECH_FEATURE_SCENE:
        isSupport = mSphParamSupport->isSphSceneSupport;
        break;

    default:
        ALOGD("%s() SpeechFeatureType(%d) NOT Supported!", __FUNCTION__, featureType);
    }
    return isSupport;
}

void SpeechConfig::initAppParser() {
    /* Init AppHandle */
    ALOGD("+%s() appHandleGetInstance", __FUNCTION__);
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return;
    }
    mAppHandle = appOps->appHandleGetInstance();
    ALOGD("-%s() appHandleRegXmlChangedCb", __FUNCTION__);

}

int SpeechConfig::initSpeechNetwork() {

    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL || mAppHandle == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return -ENODEV;
    } else {

        /* Query AudioType */
        AudioType *audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, "SpeechNetwork");

        //-----------
        //parse layer
        CategoryType *categoryNetwork = appOps->audioTypeGetCategoryTypeByName(audioType, "Network");
        mNumSpeechNetwork = appOps->categoryTypeGetNumOfCategory(categoryNetwork);

        //for speech param dump
        char *bufParamDump = new char[SPH_PARAM_UNIT_DUMP_STR_SIZE];
        memset(bufParamDump, 0, SPH_PARAM_UNIT_DUMP_STR_SIZE);
        int ret = snprintf(bufParamDump, SPH_DUMP_STR_SIZE, "xml(%s),", "SpeechNetwork");
        if (ret < 0 || ret >= SPH_DUMP_STR_SIZE) {
            ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, bufParamDump, SPH_DUMP_STR_SIZE, ret);
        }

        /* Query the ParamUnit */
        appOps->audioTypeReadLock(audioType, __FUNCTION__);
        //parse network
        for (int idxNetwork = 0; idxNetwork < mNumSpeechNetwork; idxNetwork++) {
            Category *CateNetwork = appOps->categoryTypeGetCategoryByIndex(categoryNetwork, idxNetwork);
            audio_strncpy(mListSpeechNetwork[idxNetwork].name, CateNetwork->name, 128);
            String8 categoryPath("Network,");
            categoryPath += CateNetwork->name;

            ParamUnit *paramUnit = appOps->audioTypeGetParamUnit(audioType, categoryPath.string());
            if (!paramUnit) {
                appOps->audioTypeUnlock(audioType);
                ALOGE("%s() can't find paramUnit, Assert!!! audioType=%s, categoryPath=%s",
                      __FUNCTION__, audioType->name, categoryPath.string());
                if (bufParamDump != NULL) {
                    delete[] bufParamDump;
                }
                ASSERT(0);
                return 0;
            }

            Param *param = appOps->paramUnitGetParamByName(paramUnit, "speech_network_support");
            if (!param) {
                if (bufParamDump != NULL) {
                    delete[] bufParamDump;
                }
                appOps->audioTypeUnlock(audioType);
                ASSERT(param);
                return -ENODEV;
            }
            mListSpeechNetwork[idxNetwork].supportBit = *(uint16_t *)param->data;

            char dumpByNetworkName[SPH_DUMP_STR_SIZE] = {0};
            ret = snprintf(dumpByNetworkName, SPH_DUMP_STR_SIZE, " %s=0x%x,", mListSpeechNetwork[idxNetwork].name,
                           mListSpeechNetwork[idxNetwork].supportBit);
            if (ret < 0 || ret >= SPH_DUMP_STR_SIZE) {
                ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, dumpByNetworkName, SPH_DUMP_STR_SIZE, ret);
            }
            audio_strncat(bufParamDump, dumpByNetworkName, SPH_DUMP_STR_SIZE);
        }
        appOps->audioTypeUnlock(audioType);
        //--------------------------------------------------------------------------------
        if (bufParamDump != NULL) {
            if (bufParamDump[0] != 0) {
                ALOGD("%s(), %s", __FUNCTION__, bufParamDump);
            }
            memset(bufParamDump, 0, SPH_PARAM_UNIT_DUMP_STR_SIZE);
        }

        //init the Name mapping table  for each SpeechNetwork
        bool isNetworkFound = false;
        for (int bitIndex = 0; bitIndex < 12; bitIndex++) {
            isNetworkFound = false;
            for (int idxNetwork = 0; idxNetwork < mNumSpeechNetwork; idxNetwork++) {
                if (((mListSpeechNetwork[idxNetwork].supportBit >> bitIndex) & 1) == 1) {
                    audio_strncpy(mNameForEachSpeechNetwork[bitIndex].name, mListSpeechNetwork[idxNetwork].name, 128);
                    isNetworkFound = true;
                    break;
                }
            }
            if (!isNetworkFound) {
                audio_strncpy(mNameForEachSpeechNetwork[bitIndex].name, mListSpeechNetwork[0].name, 128);
            }
            char dumpByNetworkBit[SPH_DUMP_STR_SIZE] = {0};
            int ret = snprintf(dumpByNetworkBit, SPH_DUMP_STR_SIZE, "[%d]=%s,", bitIndex,
                               mNameForEachSpeechNetwork[bitIndex].name);
            if (ret < 0 || ret >= SPH_DUMP_STR_SIZE) {
                ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, dumpByNetworkBit, SPH_DUMP_STR_SIZE, ret);
            }
            audio_strncat(bufParamDump, dumpByNetworkBit, SPH_DUMP_STR_SIZE);
        }
        if (bufParamDump != NULL) {
            if (bufParamDump[0] != 0) {
                ALOGD("%s(), Bit%s", __FUNCTION__, bufParamDump);
            }
            delete[] bufParamDump;
        }
    }
    return 0;

}

void SpeechConfig::getNameForEachSpeechNetwork(unsigned char bitIndex, char *netName) {
    ALOGD("%s(), mNameForEachSpeechNetwork[%d].name = %s",
          __FUNCTION__, bitIndex, mNameForEachSpeechNetwork[bitIndex].name);
    audio_strncpy(netName, mNameForEachSpeechNetwork[bitIndex].name, sizeof(mNameForEachSpeechNetwork[bitIndex].name));
}

int SpeechConfig::getBtDelayTime(const char *btDeviceName) {
    int btDelayMs = 0;
    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL || btDeviceName == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return -ENODEV;
    } else {
        /* Get the BT device delay parameter */
        AudioType *audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, "BtInfo");
        if (audioType) {
            String8 categoryPath("BT headset,");
            categoryPath += btDeviceName;

            ParamUnit *paramUnit = appOps->audioTypeGetParamUnit(audioType, categoryPath.string());
            if (!paramUnit) {
                ASSERT(paramUnit);
                return -ENODEV;
            }

            Param *param = appOps->paramUnitGetParamByName(paramUnit, "voice_cp_delay_ms");
            if (!param) {
                ASSERT(param);
                return -ENODEV;
            }
            btDelayMs = *(int *)param->data;
        }
        ALOGD("%s(), btDeviceName=%s, btDelayMs=%d", __FUNCTION__, btDeviceName, btDelayMs);
        return btDelayMs;
    }
}


int SpeechConfig::getEchoRefParam(uint8_t *usbDelayMs) {
    uint16_t sizeByteParam = 0, numDevice, size = 0;
    char paramBuf[ECHO_REF_PARAM_SIZE] = {0};

    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL || mAppHandle == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return -ENODEV;
    } else {
        /* Query AudioType */
        AudioType *audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, "SpeechEchoRef");
        CategoryType *categoryDevice = appOps->audioTypeGetCategoryTypeByName(audioType, "Device");
        numDevice = appOps->categoryTypeGetNumOfCategory(categoryDevice);

        /* Query the ParamUnit */
        appOps->audioTypeReadLock(audioType, __FUNCTION__);
        //parse network
        for (int idxDevice = 0; idxDevice < numDevice; idxDevice++) {
            Category *cateDevice = appOps->categoryTypeGetCategoryByIndex(categoryDevice, idxDevice);
            String8 categoryPath("Device,");
            categoryPath += cateDevice->name;

            ParamUnit *paramUnit = appOps->audioTypeGetParamUnit(audioType, categoryPath.string());
            if (!paramUnit) {
                appOps->audioTypeUnlock(audioType);
                ALOGE("%s() can't find paramUnit, Assert!!! audioType=%s, categoryPath=%s",
                      __FUNCTION__, audioType->name, categoryPath.string());
                ASSERT(0);
                return 0;
            }

            Param *param = appOps->paramUnitGetParamByName(paramUnit, "EchoRef_para");
            if (!param) {
                appOps->audioTypeUnlock(audioType);
                ASSERT(param);
                return -ENODEV;
            }
            appOps->audioTypeUnlock(audioType);

            sizeByteParam = appOps->paramGetNumOfBytes(param);
            if (size + sizeByteParam < ECHO_REF_PARAM_SIZE) {
                memcpy(paramBuf + size, param->data, sizeByteParam);
                size += sizeByteParam;
            } else {
                ALOGE("%s() Memcpy FAIL! paramBuf size:%d, request:%d", __FUNCTION__, ECHO_REF_PARAM_SIZE, size + sizeByteParam);
                return -ENOMEM;
            }

        }
        ALOGD("%s(), xml(%s), total size(b)=%d", __FUNCTION__, "SpeechEchoRef", size);
        SPEECH_ECHOREF_PARAM_STRUCT *echoRefParam = NULL;
        echoRefParam = (SPEECH_ECHOREF_PARAM_STRUCT *)paramBuf;
        ALOGV("%s(), %d, 0x%x, %d", __FUNCTION__,
              echoRefParam->speech_common_para[0],
              echoRefParam->speech_common_para[1],
              echoRefParam->speech_common_para[2]);

        *usbDelayMs = echoRefParam->speech_common_para[1] & 0xFF;

    }
    return 0;

}


int SpeechConfig::getDriverParam(uint8_t paramType, void *paramBuf) {
    uint16_t sizeByteParam = 0, size = 0;
    const char *paramName[] = {
        "speech_common_para",
        "debug_info"
    };

    if (paramType >= NUM_DRIVER_PARAM) {
        ALOGE("%s(), invalid paramType(%d)!!!", __FUNCTION__, paramType);
        return -EINVAL;
    }

    AppOps *appOps = appOpsGetInstance();
    if (appOps == NULL || mAppHandle == NULL) {
        ALOGE("Error %s %d", __FUNCTION__, __LINE__);
        ASSERT(0);
        return -ENODEV;
    } else {
        /* Query AudioType */
        AudioType *audioType = appOps->appHandleGetAudioTypeByName(mAppHandle, "SpeechGeneral");
        CategoryType *categoryDevice = appOps->audioTypeGetCategoryTypeByName(audioType, "CategoryLayer");

        /* Query the ParamUnit */
        appOps->audioTypeReadLock(audioType, __FUNCTION__);
        String8 categoryPath("CategoryLayer,Common");

        ParamUnit *paramUnit = appOps->audioTypeGetParamUnit(audioType, categoryPath.string());
        if (!paramUnit) {
            appOps->audioTypeUnlock(audioType);
            ALOGE("%s() can't find paramUnit, Assert!!! audioType=%s, categoryPath=%s",
                  __FUNCTION__, audioType->name, categoryPath.string());
            ASSERT(0);
            return 0;
        }
        //for speech param dump
        char *bufParamDump = new char[SPH_PARAM_UNIT_DUMP_STR_SIZE];
        memset(bufParamDump, 0, SPH_PARAM_UNIT_DUMP_STR_SIZE);
        int ret = snprintf(bufParamDump, SPH_DUMP_STR_SIZE, "xml(%s),(path=%s,id=%d):", "SpeechGeneral",
                           categoryPath.string(), paramUnit->paramId);
        if (ret < 0 || ret >= SPH_DUMP_STR_SIZE) {
            ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, bufParamDump, SPH_DUMP_STR_SIZE, ret);
        }
        //--------------------------------------------------------------------------------
        Param *param = appOps->paramUnitGetParamByName(paramUnit, paramName[paramType]);
        ASSERT(param);
        if (param) {
            sizeByteParam = appOps->paramGetNumOfBytes(param);
            memcpy(paramBuf, param->data, sizeByteParam);
            size += sizeByteParam;
            speechDataDump(bufParamDump, "SpeechGeneral", param);
            appOps->audioTypeUnlock(audioType);
        }
        //--------------------------------------------------------------------------------
        if (bufParamDump != NULL) {
            if (bufParamDump[0] != 0) {
                ALOGD("%s(),%s total size(b)=%d", __FUNCTION__, bufParamDump, size);
            }
            delete[] bufParamDump;
        }
    }
    return 0;
}


int SpeechConfig::speechDataDump(char *dumpBuf,
                                 const char *nameXml,
                                 const Param *param) {
    if (dumpBuf == NULL) {
        ALOGE("%s(), dumpBuf is NULL!!!", __FUNCTION__);
        return -ENOMEM;
    }
    if (nameXml == NULL) {
        ALOGE("%s(), name of Xml is NULL!!!", __FUNCTION__);
        return -EINVAL;
    }
    if (param == NULL) {
        ALOGE("%s(), xml(%s), param is NULL!!!", __FUNCTION__, nameXml);
        return -EINVAL;
    }

    char sphDumpStr[SPH_DUMP_STR_SIZE] = {0};
    int idxDump = 0, sizeUShortDump = 0, sizeByteParam = 0, printFormat = PARAM_PRINT_FORMAT_DEC;
    //speech parameter dump
    if (strcmp(nameXml, "SpeechGeneral") == 0) {
        AppOps *appOps = appOpsGetInstance();
        if (appOps == NULL) {
            ALOGE("Error %s %d", __FUNCTION__, __LINE__);
            ASSERT(0);
            return -ENODEV;
        }
        sizeByteParam = appOps->paramGetNumOfBytes((Param *)param);
        sizeUShortDump = sizeByteParam >> 1;
    } else if (strcmp(nameXml, "SpeechEchoRef") == 0) {
        if (strcmp(param->name, "EchoRef_para") == 0) {
            sizeUShortDump = 3;
        }
    }

    int ret = snprintf(sphDumpStr, SPH_DUMP_STR_SIZE, "%s[%d]=", param->name, sizeUShortDump);
    if (ret < 0 || ret >= SPH_DUMP_STR_SIZE) {
        ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, sphDumpStr, SPH_DUMP_STR_SIZE, ret);
    }

    for (idxDump = 0; idxDump < sizeUShortDump; idxDump++) {
        char sphDumpTemp[100] = {0};
        if (printFormat == PARAM_PRINT_FORMAT_DEC) {
            ret = snprintf(sphDumpTemp, 100, "[%d]%d,", idxDump, *((uint16_t *)param->data + idxDump));
            if (ret < 0 || ret >= 100) {
                ALOGE("%s(), snprintf %s fail!! sz %d, ret %d", __FUNCTION__, sphDumpTemp, 100, ret);
            }
        }
        audio_strncat(sphDumpStr, sphDumpTemp, SPH_DUMP_STR_SIZE);
    }

    if (idxDump != 0) {
        audio_strncat(dumpBuf, sphDumpStr, SPH_DUMP_STR_SIZE);
    }
    return NO_ERROR;
}

}

//namespace android
