#ifndef ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_OFFLOAD_H
#define ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_OFFLOAD_H

#include "AudioALSAPlaybackHandlerBase.h"
#include "sound/compress_params.h"
#include <tinycompress/tinycompress.h>
#include <sound/asound.h>
#include "sound/compress_offload.h"
#include <pthread.h>
#include <cutils/list.h>

namespace android {
#define OFFLOAD_BUFFER_SIZE_PER_ACCESSS     (32768)

enum {
    OFFLOAD_STATE_IDLE,
    OFFLOAD_STATE_PLAYING,
    OFFLOAD_STATE_PAUSED,
    OFFLOAD_STATE_EARLY_DRAIN,
    OFFLOAD_STATE_DRAINED,
};

enum {
    OFFLOAD_WRITE_EMPTY,
    OFFLOAD_WRITE_REMAIN,
    OFFLOAD_WRITE_ERROR,
};

enum {
    OFFLOAD_CMD_WRITE,
    OFFLOAD_CMD_PAUSE,
    OFFLOAD_CMD_RESUME,
    OFFLOAD_CMD_DRAIN,
    OFFLOAD_CMD_CLOSE,
    OFFLOAD_CMD_FLUSH,
};

enum offload_compr_cmd_t{
    OFFLOAD_COMPR_START = 0,
    OFFLOAD_COMPR_PAUSE,
    OFFLOAD_COMPR_RESUME,
    OFFLOAD_COMPR_STOP,
    OFFLOAD_COMPR_DRAIN,
    OFFLOAD_COMPR_WAIT,
    OFFLOAD_COMPR_NONBLOCK,
};

struct tstamp {
    unsigned long frames;
    unsigned int samplerate;
};

struct offload_cmd {
    struct listnode node;
    int cmd;
};
struct offload_thread_property {
    pthread_mutex_t             offload_mutex;
    pthread_cond_t              offload_cond;
    struct listnode             offload_cmd_list;
    pthread_t                   offload_pthread;
};

struct offload_stream_property {
    int offload_state;
    int offload_state_pre;
    unsigned int fragment_size;
    int num_channels;
    int sample_rate;
    int bit_rate;
    unsigned int offload_gain[2];
    void    *tmpbsBuffer;
    int remain_write;
};

struct offload_write_info {
    void *tmpBuffer;
    unsigned int  bytes;
};


struct offload_buffer {
    size_t fragment_size;
    int fragments;
};

struct offload_codec {
    __u32 id;
    __u32 ch_in;
    __u32 ch_out;
    __u32 sample_rate;
    __u32 bit_rate;
    __u32 rate_control;
    __u32 profile;
    __u32 level;
    __u32 ch_mode;
    __u32 format;
    __u32 align;
    union snd_codec_options options;
    __u32 reserved[3];
};


struct snd_offload_params {
    struct offload_buffer buffer;
    struct offload_codec codec;
};

class AudioDspStreamManager;

class AudioALSAPlaybackHandlerOffload : public AudioALSAPlaybackHandlerBase {
public:
    AudioALSAPlaybackHandlerOffload(const stream_attribute_t *stream_attribute_source);
    virtual ~AudioALSAPlaybackHandlerOffload();

    /**
     * open/close audio hardware
     */
    virtual status_t open();
    virtual status_t close();
    virtual int pause();
    virtual int resume();
    virtual int flush();
    virtual status_t routing(const audio_devices_t output_devices);
    virtual status_t setVolume(uint32_t vl);

    /**
     * write data to audio hardware
     */
    virtual ssize_t  write(const void *buffer, size_t bytes);

    virtual int drain(audio_drain_type_t type);


    virtual status_t setFilterMng(AudioMTKFilterManager *pFilterMng);

    int process_write();
    int process_writewait();
    int process_drain();

    void offload_callback(stream_callback_event_t event);

    void offload_initialize();
    virtual bool setOffloadRoutingFlag(bool enable);
    bool getOffloadRoutingFlag();
    int isformatnotsupport();
    int setDspRuntimeEn(bool condition);
    int compress_operation(offload_compr_cmd_t cmd);

protected:

private:
    //void set_codec_samplerate(int pcmindex, int cardindex);
    int setAfeDspSharemem(bool condition);
    bool enablePcmDump(bool enable);
    uint32_t ChooseTargetSampleRate(uint32_t SampleRate);
    bool SetLowJitterMode(bool bEnable, uint32_t SampleRate);
    status_t openDspHwPcm();
    uint32_t GetLowJitterModeSampleRate();
    audio_format_t  mFormat;
    uint32_t   mWriteBsbufSize;
    bool       mReady;
    bool       mWriteWait;
    uint8_t mTaskScene;
    AudioLock mWriteWaitLock;
    AudioLock mComprCloseLock;
    AudioDspStreamManager *mDspStreamManager;
    float mVolume;
    bool raw_pcm_dump;

};

} // end namespace android

#endif // end of ANDROID_AUDIO_ALSA_PLAYBACK_HANDLER_NORMAL_H
