#ifndef ANDROID_AUDIO_MTK_DEVICE_STRING_H
#define ANDROID_AUDIO_MTK_DEVICE_STRING_H

#include <stdint.h>
#include <sys/types.h>

#include "AudioType.h"
#include "AudioSystemLibUtil.h"

namespace android {
extern String8 keyCardName;

extern String8 keypcmDl1Meida;
extern String8 keypcmUl1Capture;
extern String8 keypcmPcm2voice;
extern String8 keypcmHDMI;
extern String8 keypcmUlDlLoopback;
extern String8 keypcmI2Splayback;
extern String8 keypcmMRGrxPlayback;
extern String8 keypcmMRGrxCapture;
extern String8 keypcmFMI2SPlayback;
extern String8 keypcmFMI2SCapture;
extern String8 keypcmFMHostless;
extern String8 keypcmI2S0Dl1Playback;
extern String8 keypcmDl1SpkPlayback;
extern String8 keypcmScpVoicePlayback;
extern String8 keypcmCS43130;
extern String8 keypcmCS35L35;
extern String8 keypcmDl1AwbCapture;
extern String8 keypcmVoiceCallBT;
extern String8 keypcmVOIPCallBTPlayback;
extern String8 keypcmVOIPCallBTCapture;
extern String8 keypcmTDMLoopback;
extern String8 keypcmMRGTxPlayback;
extern String8 keypcmUl2Capture;
extern String8 keypcmI2SAwbCapture;
extern String8 keypcmMODADCI2S;
extern String8 keypcmADC2AWB;
extern String8 keypcmIO2DAI;
extern String8 keypcmHpimpedancePlayback;
extern String8 keypcmModomDaiCapture;
extern String8 keypcmOffloadGdmaPlayback;
extern String8 keypcmDl2Meida;    //DL2 playback
extern String8 keypcmDl3Meida;        //DL3 playback
extern String8 keypcmBTCVSDCapture;
extern String8 keypcmBTCVSDPlayback;
extern String8 keypcmExtSpkMeida;
extern String8 keypcmVoiceMD1;
extern String8 keypcmVoiceMD2;
extern String8 keypcmVoiceMD1BT;
extern String8 keypcmVoiceMD2BT;
extern String8 keypcmVoiceUltra;
extern String8 keypcmVoiceUSB;
extern String8 keypcmVoiceUSBEchoRef;
extern String8 keypcmI2S2ADCCapture;
extern String8 keypcmVoiceDaiCapture;
extern String8 keypcmOffloadPlayback;
extern String8 keypcmExtHpMedia;
extern String8 keypcmDL1DATA2PLayback;
extern String8 keypcmPcmRxCapture;
extern String8 keypcmVUL2Capture;

#if defined(MTK_AUDIO_KS)
extern String8 keypcmPlayback1;
extern String8 keypcmPlayback2;
extern String8 keypcmPlayback3;
extern String8 keypcmPlayback4;
extern String8 keypcmPlayback5;
extern String8 keypcmPlayback6;
extern String8 keypcmPlayback8;
extern String8 keypcmPlayback12;
extern String8 keypcmPlaybackHDMI;

extern String8 keypcmCapture1;
extern String8 keypcmCapture2;
extern String8 keypcmCapture3;
extern String8 keypcmCapture4;
extern String8 keypcmCapture6;
extern String8 keypcmCapture7;
extern String8 keypcmCapture8;

extern String8 keypcmCaptureMono1;

extern String8 keypcmHostlessFm;
extern String8 keypcmHostlessLpbk;
extern String8 keypcmHostlessSpeech;
extern String8 keypcmHostlessSphEchoRef;
extern String8 keypcmHostlessSpkInit;
extern String8 keypcmHostlessADDADLI2SOut;
extern String8 keypcmHostlessSRCBargein;
extern String8 keypcmHostlessHwGainAAudio;
extern String8 keypcmHostlessSrcAAudio;

#if defined(MTK_AUDIODSP_SUPPORT)
extern String8 keypcmPlaybackDspprimary;
extern String8 keypcmPlaybackDspVoip;
extern String8 keypcmPlaybackDspDeepbuf;
extern String8 keypcmPlaybackDsp;
extern String8 keypcmPlaybackDspMixer1;
extern String8 keypcmPlaybackDspMixer2;
extern String8 keypcmCaptureDspUl1;
extern String8 keypcmPlaybackDspA2DP;
extern String8 keypcmPlaybackDspDataProvider;
extern String8 keypcmCallfinalDsp;
extern String8 keypcmPlaybackDspFast;
extern String8 keypcmPlaybackDspKtv;
extern String8 keypcmCaptureDspRaw;
extern String8 keypcmPlaybackDspFm;
#endif
#if defined(MTK_VOW_SUPPORT)
extern String8 keypcmVOWCapture;
#endif

//ultra-sound
#ifdef MTK_ULTRASOUND_PROXIMITY_SUPPORT
extern String8 keypcmUltra;
extern String8 keypcmCapture5;
extern String8 keypcmPlayback7;
#endif

#endif
extern String8 keypcmVOWBargeInCapture;
}

#endif
