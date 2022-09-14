#ifndef ANDROID_ADSP_CALLFINAL_H
#define ANDROID_ADSP_CALLFINAL_H

#include <tinyalsa/asoundlib.h>

#include <AudioLock.h>

#include <AudioALSAHardwareResourceManager.h>

struct ipi_msg_t;
struct aurisys_dsp_config_t;
struct aurisys_lib_manager_t;
struct arsi_task_config_t;

namespace android {

class AudioDspCallFinal {
public:
    /** virtual dtor */
    virtual ~AudioDspCallFinal();

    /** singleton */
    static AudioDspCallFinal *getInstance(void);

    int getDspCallFinalRuntimeEnable(void);
    void SetArsiTaskConfigCallFinal(struct arsi_task_config_t *pTaskConfig);
    void SetArsiAttributeCallFinal();
    void CreateAurisysLibManagerCallFinal();
    void DestroyAurisysLibManagerCallFinal();
    void initCallFinalTask(const audio_devices_t input_device, const audio_devices_t output_device,
                           uint16_t sample_rate, int mdIdx);
    void deinitCallFinalTask();
    void startCallFinalTask();
    void stopCallFinalTask();
    bool isOutputDevCallFinalSupport(int outputDevice);
    int setCallFinalDspShareMem(bool condition);

protected:
    AudioDspCallFinal();

    audio_devices_t mInputDevice;
    audio_devices_t mOutputDevice;
    uint16_t mSampleRate;

    struct pcm_config       mConfig;
    struct pcm_config       mSpkIvConfig;
    struct pcm              *mPcmDlIn;
    struct pcm              *mPcmDlOut;
    struct pcm              *mPcmIv;
    struct pcm              *mPcmDsp;

    struct aurisys_dsp_config_t *mAurisysDspConfigCallFinal;
    struct aurisys_lib_manager_t *mAurisysLibManagerCallFinal;
    static void processDmaMsgCallFinal(struct ipi_msg_t *msg, void *buf, uint32_t size, void *arg);

    AudioALSAHardwareResourceManager *mHardwareResourceManager;

private:
    /** singleton */
    static AudioDspCallFinal *mAudioDspCallFinal;
    int setCallFinalruntime(bool condition);
    int setCallFinalRefruntime(bool condition);

    String8 mUlTurnOnSeq, mDlTurnOnSeq1, mDlTurnOnSeq2, mEchoRefTurnOnSeq;
};

} /* end of namespace android */

#endif /* end of ANDROID_ADSP_CALLFINAL_H */
