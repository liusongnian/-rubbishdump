/***********************************************************************
*Copyright 2010-20XX by 7ymekk
*
*   Licensed under the Apache License, Version 2.0 (the "License");
*   you may not use this file except in compliance with the License.
*   You may obtain a copy of the License at
*
*       http://www.apache.org/licenses/LICENSE-2.0
*
*   Unless required by applicable law or agreed to in writing, software
*   distributed under the License is distributed on an "AS IS" BASIS,
*   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*   See the License for the specific language governing permissions and
*   limitations under the License.
*
*   @author 7ymekk (7ymekk@gmail.com)
*
************************************************************************/


#ifndef ASOUNDINFOWIDGET_H
#define ASOUNDINFOWIDGET_H

#include <QWidget>
#include "../classes/phone.h"
#include <QTreeWidgetItem>


extern "C" {
#include <string.h>
#include <stdio.h>
#include "../json/cJSON.h"
}

class AudioPcmDeviceparam;
class AudioDeviceDescriptor;
class AudioMixerinfo;
class AudioALSADeviceConfigManager;
namespace Ui {
    class AsoundinfoWidget;
}

/* Configuration for a pcm_params */
#define PARAMS_MAXLEN 128

typedef struct param_value{
    unsigned int min;
    unsigned int max;
} param_value_t;

typedef struct pcm_params {
    char Access[PARAMS_MAXLEN];
    char Format0[PARAMS_MAXLEN];
    char Format1[PARAMS_MAXLEN];
    char Format_Name[PARAMS_MAXLEN];
    char Subformat[PARAMS_MAXLEN];
    param_value_t rate;
    param_value_t channels;
    param_value_t sample_bits;
    param_value_t frame_bits;
    param_value_t period_time;
    param_value_t period_size;
    param_value_t period_bytes;
    param_value_t periods;
    param_value_t buffer_time;
    param_value_t buffer_size;
    param_value_t buffer_bytes;
    param_value_t tick_time;
}pcm_params_t;

class AsoundinfoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit AsoundinfoWidget(QWidget *parent = 0, Phone *phone = NULL);
    ~AsoundinfoWidget();

    QString sdk;
    void init();
    void updateParentItem(QTreeWidgetItem* item);

    void setParentPartiallyChecked(QTreeWidgetItem *itm);
    void init_Asound_info();
    void loadStyleSheet();
    QTreeWidgetItem* addChildNode(QTreeWidgetItem *parent, int index, QString namePre);
    QTreeWidgetItem* addChildEmpNode(QTreeWidgetItem *parent, int index);
    QTreeWidgetItem* addChildstreamNode(QTreeWidgetItem *parent, int index);

    QString adbCommand() const;
    CJSON_PUBLIC(cJSON *)  readcontrol(const cJSON * const object, int num);
    void  check_item_type(const cJSON * const object);
    const char *  qstringtochar(QString *qs);
    void  value_to_string(const cJSON * const object,QString *qs);
    void  dealwithpcm();
    bool  fopen(QString filename,std::vector<char> * result_out,std::vector<char> * result_err);
    bool opendir(QString filename,std::vector<char> * result_out,std::vector<char>  *result_err);
    bool exec(QString c,std::vector<char> * result_out,std::vector<char>  *result_err);
    bool isAdspOptionEnable();
    struct pcm_params * pcm_params_get(unsigned int card, unsigned int device,unsigned int flags);
    int pcm_stream_status(AudioDeviceDescriptor *temp,int subdevice);
    int pcm_stream_info();
    void GetpcminfoAttribute(AudioDeviceDescriptor *mAudioDeviceDescriptor ,char * result_out,int stream_type);
    void dump();
    unsigned int GetPcmIndexByString(QString stringpair);
    unsigned int GetCardIndexByString(QString stringpair);
    unsigned int GetPcmBufferSize(unsigned int  pcmindex, unsigned int direction);
    unsigned int GetCardIndex() {return mCardIndex;}
    void AudioALSADeviceParser();
    void GetAllPcmAttribute(void);
    void GetAllCompressAttribute(void);
    void AddPcmString(char *InputBuffer);
    int QueryPcmDriverCapability();
    int GetPcmDriverparameters(AudioPcmDeviceparam *PcmDeviceparam, struct pcm_params *params);
    void SetPcmCapability(AudioDeviceDescriptor *Descriptor, const char *Buffer);
    void getCardName();
    void ParseCardIndex();
    unsigned int pcm_params_get_max(char *data);
    unsigned int pcm_params_get_min(char *data);
    void gettinymixinfo();
    void update_pcm_info(QString str);
    void init_kcontrol_info();
    void init_kcon_tree();
    void mixer_dump();
    void init_audio_device();

    void audioservice();
    /**
     * Audio Pcm vector
     */
    QVector <AudioDeviceDescriptor *> mAudioDeviceVector;
    QVector <AudioDeviceDescriptor *> mAudioComprDevVector;
    QVector <AudioMixerinfo *> mAudioMixerVector;
    QString mCardName;
    unsigned int mCardIndex;
    pcm_params_t params;
    AudioALSADeviceConfigManager  *mDeviceConfigManager;
    /*
     * flag of dynamic enable verbose/debug log
     */
    int mLogEnable;
//protected:
//    void changeEvent(QEvent *e);

private:
    Ui::AsoundinfoWidget *ui;
    Phone *phone;
    QTreeWidget *tree;

private slots:
    void flashSPL();
    void flashRecovery();
    void flashRadio();
    void flashZip();
    void bootIMG();
    //申明信号与槽,当树形控件的子选项被改变时执行
    void treeItemChanged(QTreeWidgetItem* item , int column);
    void onItemExpanded(QTreeWidgetItem * item);
    void onItemCollapsed(QTreeWidgetItem * item);
    void onItemClicked(QTreeWidgetItem * item, int column);
    void onItem_kcon_Expanded(QTreeWidgetItem * item);
    void onItem_kcon_Collapsed(QTreeWidgetItem * item);
    void onItem_kcon_Clicked(QTreeWidgetItem * item, int column);
};


class AudioPcmDeviceparam {
public:
    AudioPcmDeviceparam() :
        mBufferBytes(0),
        mRateMax(0),
        mRateMin(0),
        mChannelMax(0),
        mChannelMin(0),
        mSampleBitMax(0),
        mSampleBitMin(0),
        mPreriodSizeMax(0),
        mPreriodSizeMin(0),
        mPreriodCountMax(0),
        mPreriodCountMin(0) {
    };
    unsigned int mBufferBytes;
    unsigned int mRateMax;
    unsigned int mRateMin;
    unsigned int mChannelMax;
    unsigned int mChannelMin;
    unsigned int mSampleBitMax;
    unsigned int mSampleBitMin;
    unsigned int mPreriodSizeMax;
    unsigned int mPreriodSizeMin;
    unsigned int mPreriodCountMax;
    unsigned int mPreriodCountMin;
};

class AudioPcmDeviceinfo {
public:
    AudioPcmDeviceinfo() :
        card(0),
        device(0),
        subdevice(0),
        stream_type(0),
        subdevices_count(0),
        subdevices_avail(0) {
    };
    unsigned int card;
    unsigned int device;
    unsigned int subdevice;
    unsigned int stream_type;
    unsigned int subdevices_count;
    unsigned int subdevices_avail;
};

class AudioDeviceDescriptor {
public:
    friend class AsoundInfo;
    AudioDeviceDescriptor() :
        mCardindex(0),
        mPcmindex(0),
        mplayback(0),
        mRecord(0) {
    };
    virtual ~AudioDeviceDescriptor() {};
    QString mStreamName;
    QString mCodecName;
    unsigned int mCardindex;
    unsigned int mPcmindex;
    unsigned int mplayback;
    unsigned int mRecord;
    AudioPcmDeviceparam mPlayparam;
    AudioPcmDeviceparam mRecordparam;
    AudioPcmDeviceinfo  mPlayinfo;
    AudioPcmDeviceinfo  mRecordinfo;
};

class AudioMixerinfo {
public:
    QString mCodecName;
    QString ctl_num;
    QString type;
    QString data_size;
    QString name_value;
    QString name;
    QString value;
};


#endif // ASOUNDINFOWIDGET_H
