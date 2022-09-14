#ifndef ANDROID_ULTRASOUND_OOC_MANAGER_H
#define ANDROID_ULTRASOUND_OOC_MANAGER_H


#include "AudioALSAHardwareResourceManager.h"
#include "AudioALSAUltrasoundOutOfCallController.h"
#include <media/AudioParameter.h>


namespace android
{

#define MAX_ULTRASOUND_USECASES 16

struct UltrasoundUsecaseInfo {
    bool Enable;
    int  Idx;
    UltrasoundUsecaseInfo()
    {
        Enable=false;
        Idx=-1;
    }

    bool IsApplicable() {return Idx >= 0;}
};


struct UltrasoundUsecaseConfig {
    String8 UsecaseName;
    UltrasoundDeviceConfig UltrasoundDevCfg;

    void Dump() {
        ALOGE("UsecaseName:%s ", UsecaseName.string());
        UltrasoundDevCfg.Dump();
    }
};

class UltrasoundOutOfCallManager
{
    public:
        virtual ~UltrasoundOutOfCallManager();
        static UltrasoundOutOfCallManager *GetInstance();
        void GetUsecaseInfo(AudioParameter &param, UltrasoundUsecaseInfo &usecase);
        status_t HandleUsecaseRequest(UltrasoundUsecaseInfo &usecase);
        status_t EnableUsecase(const UltrasoundUsecaseInfo &usecase);
        status_t DisableUsecase(const UltrasoundUsecaseInfo &usecase);

    protected:
        UltrasoundOutOfCallManager();

        AudioVolumeInterface *mAudioALSAVolumeController;
        Mutex mOutOfCallManagerLock;
        audio_devices_t mInputDeviceCopy;
        audio_devices_t mOutputDeviceCopy;

    private:
        int mUsecaseActivated[MAX_ULTRASOUND_USECASES];
        int mActivUsecaseCount;
        int mLogFlag;

        static UltrasoundOutOfCallManager *mUltrasoundOutOfCallManager; // singleton
        status_t EnableEngineAndUsecase(const UltrasoundUsecaseInfo &usecase);
        status_t DisableEngineAndUsecase(const UltrasoundUsecaseInfo &usecase);
        String8 GetUsecaseString(int i);
        const char* GetUltrasoundUsecaseName(int i);
        UltrasoundDeviceConfig GetUltrasoundDeviceConfig(int i);
    };

} // end namespace android

#endif // end of ANDROID_ULTRASOUND_OOC_MANAGER_H
