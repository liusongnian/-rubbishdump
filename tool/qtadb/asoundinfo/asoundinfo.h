#ifndef ASOUNDINFO_H
#define ASOUNDINFO_H

#include <QMainWindow>
#include<vector>
#include "../asoundlib/asoundlib.h"

extern "C" {
#include <string.h>
#include <stdio.h>
#include "../json/cJSON.h"
}

class AudioPcmDeviceparam;
class AudioDeviceDescriptor;

namespace Ui {
class AsoundInfo;
}

class AsoundInfo : public QMainWindow
{
    Q_OBJECT

public:
    explicit AsoundInfo(QWidget *parent = 0);
    ~AsoundInfo();

    void init();                                //初始化
    void readToTable(const QString sheet);      //读Excel到tableWidget
    void tinymixToTable(const QString sheet);
    void writeToExcel(const QString sheet);     //保存修改内容到Excel
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
private slots:
    void slot_openExcel();

    void slot_saveExcel();

    void on_comboBox_activated(const QString &arg1);

    void on_tableWidget_cellChanged(int row, int column);

    void on_act_about_triggered();

    void slot_newExcel();

private:
    Ui::AsoundInfo *ui;

    QString m_fileName;       //文件名
    QString cBox_text;
    QList<QPoint> cellList; //记录修改内容的位置

public:
    unsigned int GetPcmIndexByString(QString stringpair);
    unsigned int GetCardIndexByString(QString stringpair);
    unsigned int GetPcmBufferSize(unsigned int  pcmindex, unsigned int direction);
    unsigned int GetCardIndex() {return mCardIndex;}
    void AudioALSADeviceParser();
private:
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
    /**
     * Audio Pcm vector
     */
    QVector <AudioDeviceDescriptor *> mAudioDeviceVector;
    QVector <AudioDeviceDescriptor *> mAudioComprDevVector;
    QString mCardName;
    unsigned int mCardIndex;
    //pcm_params_t params;

    /*
     * flag of dynamic enable verbose/debug log
     */
    int mLogEnable;
};
#if 0
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
#endif
#endif // ASOUNDINFO_H
