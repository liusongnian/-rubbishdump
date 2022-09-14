#ifndef ANDROID_SPEECH_PCM_MIXER_TELEPHONYTX_H
#define ANDROID_SPEECH_PCM_MIXER_TELEPHONYTX_H

#include <SpeechPcmMixerBase.h>

namespace android {

enum {
    PCM_TELEPHONY_TX_TYPE_REPLACE = 0,
    PCM_TELEPHONY_TX_TYPE_MIX = 1,
    PCM_TELEPHONY_TX_TYPE_MAX
};

class SpeechPcmMixerTelephonyTx : public SpeechPcmMixerBase {

public:
    virtual ~SpeechPcmMixerTelephonyTx() {}
    static SpeechPcmMixerTelephonyTx    *GetInstance();
    virtual status_t                    pcmMixerOn(SpeechDriverInterface *pSpeechDriver);
    virtual status_t                    pcmMixerOff(SpeechDriverInterface *pSpeechDriver);
    virtual uint32_t                    getPcmMixerType() { return PCM_MIXER_TYPE_TELEPHONYTX; }
    virtual void                        setTelephonyTxType(uint16_t pcmTelephonyTxType);

protected:
    SpeechPcmMixerTelephonyTx();

private:
    static SpeechPcmMixerTelephonyTx    *mTelephonyTx; // singleton
    uint16_t                             mPcmTelephonyTxType;

};
} // end namespace android

#endif //ANDROID_SPEECH_PCM_MIXER_TELEPHONYTX_H