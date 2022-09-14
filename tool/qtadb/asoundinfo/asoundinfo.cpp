#include "asoundinfo.h"
#include "ui_asoundinfo.h"
#include "../abstractProcess/MyProcess.h"
//#include "about.h"

#include <QFileDialog>
#include <QtXlsx>
#include <QDebug>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QMessageBox>

#define ALSASOUND_CARD_LOCATION "/proc/asound/cards"
#define ALSASOUND_DEVICE_LOCATION "/proc/asound/devices/"
#define ALSASOUND_ASOUND_LOCATION "/proc/asound"
#define ALSASOUND_PCM_LOCATION "/proc/asound/pcm"
#define AUDIO_DEVICE_EXT_CONFIG_FILE "/vendor/etc/audio_device.xml"
#define PROC_READ_BUFFER_SIZE (256)

static QString keypcmPlayback = QString("playback");
static QString keypcmCapture = QString("capture");

AsoundInfo::AsoundInfo(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::AsoundInfo)
{
    ui->setupUi(this);

    init();
}

AsoundInfo::~AsoundInfo()
{
    delete ui;
}

void AsoundInfo::init()
{
    setWindowTitle(tr("读tinymix文件"));

    ui->tableWidget->horizontalHeader()->setVisible(false);
    ui->tableWidget->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    qDebug()<<"ui->tableWidget->horizontalHeader";
    connect(ui->act_new, SIGNAL(triggered()), this, SLOT(slot_newExcel()));
    connect(ui->btn_openExcel, SIGNAL(clicked()), this, SLOT(slot_openExcel()));
    connect(ui->btn_saveExcel, SIGNAL(clicked()), this, SLOT(slot_saveExcel()));
    connect(ui->act_open, SIGNAL(triggered()), this, SLOT(slot_openExcel()));
    connect(ui->act_save, SIGNAL(triggered()), this, SLOT(slot_saveExcel()));
    connect(ui->act_exit, SIGNAL(triggered()), this, SLOT(close()));
    connect(ui->act_aboutQt, SIGNAL(triggered()), qApp, SLOT(aboutQt()));
}

void AsoundInfo::readToTable(const QString sheet)
{
    QXlsx::Document xlsx(m_fileName);
    xlsx.selectSheet(sheet);                        //设置当前Sheet

    //获取当前Sheet的行列
    QXlsx::CellRange range = xlsx.dimension();
    int rows = range.rowCount();
    int cols = range.columnCount();
    int row2 =xlsx.dimension().lastRow();
    int col2= xlsx.dimension().lastColumn();
    qDebug() << "curSheet:"<<m_fileName<<rows<<cols<<row2<<col2;
    //清空并设置tableWidget的行列
    ui->tableWidget->clear();
    ui->tableWidget->setRowCount(rows);
    ui->tableWidget->setColumnCount(cols);
    qDebug() << "curSheet:" << xlsx.currentSheet()->sheetName() << sheet;
    for(int i = 0; i < rows; i++){
        for (int j = 0; j < cols; j++){
            QString text = xlsx.read(i+1, j+1).toString();
//            qDebug() << "text:" << text;
            QTableWidgetItem *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);                //设置居中对齐
            ui->tableWidget->setItem(i, j, item);
        }
    }

    cellList.clear();       //读完Excel清空修改记录
    qDebug() << "cellList:" << cellList;
}

void AsoundInfo::tinymixToTable(const QString sheet)
{
     qDebug() << "tinymixToTable:";
}

void AsoundInfo::writeToExcel(const QString sheet)
{
    qDebug() << "write:" << sheet;
    QXlsx::Document xlsx(m_fileName);
    xlsx.selectSheet(sheet);

    //遍历修改记录并写入Excel文件保存
    foreach (QPoint point, cellList) {
        int row = point.x();
        int col = point.y();
        QString text = ui->tableWidget->item(row - 1, col - 1)->text();
        xlsx.write(row, col, text);
        qDebug() << "write:" << point << row << col << text;
    }
    xlsx.save();
}



//QAxObject* newSheet = worksheets->querySubObject("Add()");
void AsoundInfo::slot_openExcel()
{
    QString path = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString fileName = QFileDialog::getOpenFileName(this, tr("打开Excel文件"), path,
                                            tr("Excel(*.xls *.xlsx);;All File(*.*)"));
    qDebug() << "fileName:" << fileName;
    if(fileName.isEmpty()){
        return;
    }
    m_fileName = fileName;
    ui->lineEdit->setText(m_fileName);
//    xlsx.setObjectName(fileName);
//    xlsx.saveAs("fileName.xls");
    QXlsx::Document xlsx(m_fileName);
    QStringList sheetList = xlsx.sheetNames();      //获取Excel文件的所有表单
    qDebug() << "sheetList:" << sheetList << QFileInfo(m_fileName).fileName();
    ui->comboBox->addItems(sheetList);

    cBox_text = ui->comboBox->currentText();
    qDebug() << "cBox_text open:" << cBox_text;

    QString curSheet = ui->comboBox->currentText();
    readToTable(curSheet);

    ui->statusBar->showMessage(tr("文件已导入!"), 3000);
}

void AsoundInfo::slot_saveExcel()
{
    qDebug() << "save!" << cBox_text << ui->comboBox->currentText();
    writeToExcel(cBox_text);
    ui->statusBar->showMessage(tr("文件已保存!"), 3000);
}


void AsoundInfo::dealwithpcm()
{
    qDebug() << "slot_newExcel!";
    Process proc;
    QString error_message;
    QString message;
    std::vector<char> result_err;
    std::vector<char> result_out;
    fopen(ALSASOUND_PCM_LOCATION,&result_out,&result_err);
    qDebug()<<result_out;
    char *data = &result_out[0];
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    qDebug()<<qlist;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<"pcm- "<<qlist.at(i);
        QString tmp = qlist.at(i);
        QStringList tmplist =  tmp.split(QRegExp(":"),QString::SkipEmptyParts);
        qDebug()<<tmplist;
        for (int i=0;i<tmplist.length();i++)
        {
            qDebug()<<"route="<<tmplist.at(i);

        }
    }
}

void AsoundInfo::slot_newExcel()
{
    qDebug() << "slot_newExcel!";
    std::vector<char> result_err;
    std::vector<char> result_out;
    //dealwithpcm();
    fopen(ALSASOUND_PCM_LOCATION,&result_out,&result_err);
    qDebug()<<result_out;
    char *data = &result_out[0];
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    qDebug()<<qlist;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<"pcm- "<<qlist.at(i);
        QString tmp = qlist.at(i);
        QStringList tmplist =  tmp.split(QRegExp(":"),QString::SkipEmptyParts);
        qDebug()<<tmplist;
        for (int i=0;i<tmplist.length();i++)
        {
            qDebug()<<"route="<<tmplist.at(i);

        }
    }
    //ParseCardIndex();
    //AudioALSADeviceParser();

}

void AsoundInfo::on_comboBox_activated(const QString &arg1)
{
    qDebug() << "arg1:" << arg1;
    if (!cellList.isEmpty()){
        int ret = QMessageBox::warning(this, tr("警告"), tr("文件已修改,是否保存?"),
                                       tr("是"), tr("否"), 0, 1);
        switch (ret) {
        case 0:
            qDebug() << "cBox_text:::" << cBox_text;
            writeToExcel(cBox_text);
            break;
        default:
            break;
        }
    }
    cBox_text = arg1;
    qDebug() << "cBox_text:" << cBox_text;
    readToTable(cBox_text);
}

void AsoundInfo::on_tableWidget_cellChanged(int row, int column)
{
//    qDebug() << "cellChanged:" << row << column;
    QPoint point(row + 1, column + 1);
    cellList.append(point);
//    qDebug() << "point:" << point << point.x() << point.y() << "cellList:" << cellList;
}


QString AsoundInfo::adbCommand() const
{
    qDebug()<<"adb";
    return "/work/version/platform-tools/adb";
}

const char * AsoundInfo::qstringtochar(QString *qs)
{
     std::string str = qs->toStdString();
     return str.c_str();
}

CJSON_PUBLIC(cJSON *)  AsoundInfo::readcontrol(const cJSON * const object, int num)
{
    if(object==NULL)
    {
        return NULL;
    }
    if(((object->type) & 0xFF) == cJSON_Object)
    {
        cJSON *obj;
        QString TempString;
        char*  ch;
        TempString = tr("%1").arg(num);
        QString s1=QString("control").append(TempString);
        qDebug()<<s1;
        QByteArray ba = s1.toLatin1();
        ch=ba.data();
        obj=cJSON_GetObjectItem(object,ch);
        return obj;
    }
    else
    {
        return NULL;
    }
}
void  AsoundInfo::check_item_type(const cJSON * const object)
{
    switch ((object->type) & 0xFF)
    {
        case cJSON_NULL:
             qDebug()<<"cJSON_NULL";
        break;
        case cJSON_False:
             qDebug()<<"cJSON_False";
                     break;
        case cJSON_True:
             qDebug()<<"cJSON_True";
                     break;
        case cJSON_Number:
             qDebug()<<"cJSON_Number";
                     break;
        case cJSON_Raw:
             qDebug()<<"cJSON_Raw";
                     break;
        case cJSON_String:
             qDebug()<<"cJSON_String";
                     break;
        case cJSON_Array:
             qDebug()<<"cJSON_Array";
                     break;
        case cJSON_Object:
            qDebug()<<"cJSON_Object";
                    break;
        default:
            qDebug()<<"default";
    }
}

void  AsoundInfo::value_to_string(const cJSON * const object,QString *qs)
{
    char v[10]={0};
    QString value;
    for(int i=0;i<cJSON_GetObjectItem(object,"num_values")->valueint;i++)
    {
        sprintf(v,"value%d",i);
        switch ((cJSON_GetObjectItem(object,v)->type) & 0xFF)
        {
            case cJSON_NULL:
                 qDebug()<<"cJSON_NULL";
            break;
            case cJSON_False:
                 qDebug()<<"cJSON_False";
                         break;
            case cJSON_True:
                 qDebug()<<"cJSON_True";
                         break;
            case cJSON_Number:
                 qDebug()<<"cJSON_Number";
                 qDebug()<<cJSON_GetObjectItem(object,v)->valueint;
                 value +=QString::number(cJSON_GetObjectItem(object,v)->valueint);
                 qDebug()<<"vint = "<<value;
                         break;
            case cJSON_Raw:
                 qDebug()<<"cJSON_Raw";
                         break;
            case cJSON_String:
                 qDebug()<<"cJSON_String";
                 qDebug()<<cJSON_GetObjectItem(object,v)->valuestring;
                 value =cJSON_GetObjectItem(object,v)->valuestring;
                 qDebug()<<"vstringv = "<<value;
                         break;
            case cJSON_Array:
                 qDebug()<<"cJSON_Array";
                         break;
            case cJSON_Object:
                qDebug()<<"cJSON_Object";
                        break;
            default:
                qDebug()<<"default";
        }
    }
    *qs=value;
}

void AsoundInfo::on_act_about_triggered()
{
    Process proc;
    QString error_message;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int process_exit_code = 0;
    QString cmd = QString("\"%1\" ").arg(adbCommand());
    cmd +="shell tinymix";
    qDebug() <<cmd;
    proc.start(cmd, false);
    process_exit_code = proc.wait();
    result_err = proc.errbytes;
    result_out = proc.outbytes;
    error_message = proc.errstring();
    //qDebug()<<result_out;
    char *data4 = &result_out[0];
    cJSON* root = cJSON_Parse(data4);
    cJSON* item;
    item=cJSON_GetObjectItem(root,"controls");

    if (item)
    {
        cJSON *obj;
        obj=readcontrol(item,14);
        if (obj)
        {
            check_item_type(obj);
            cJSON* control;
            control=cJSON_GetObjectItem(obj,"type");
            qDebug()<<control->valuestring;
        }
        else{
            qDebug()<<"obj is null";
        }
    }else{
        qDebug()<<"item is null";
    }
    qDebug()<<item->valuestring;
}

bool AsoundInfo::fopen(QString filename,std::vector<char> * result_out,std::vector<char>  *result_err)
{
    Process proc;
    QString error_message;
    int process_exit_code = 0;
    QString cmd = QString("\"%1\" shell cat ").arg(adbCommand());
    //QString cmd ="/work/version/platform-tools/adb shell cat /proc/asound/pcm";
    cmd +=filename;
    qDebug() <<cmd;
    proc.start(cmd, false);
    process_exit_code = proc.wait();
    *result_err = proc.errbytes;
    *result_out = proc.outbytes;
    error_message = proc.errstring();
    qDebug()<<"error_message="<<error_message<<" process_exit_code="<<process_exit_code;
    return process_exit_code;
}

bool AsoundInfo::exec(QString c,std::vector<char> * result_out,std::vector<char>  *result_err)
{
    Process proc;
    QString error_message;
    int process_exit_code = 0;
    QString cmd = QString("\"%1\" shell ").arg(adbCommand());
    cmd +=c;
    qDebug() <<cmd;
    proc.start(cmd, false);
    process_exit_code = proc.wait();
    *result_err = proc.errbytes;
    *result_out = proc.outbytes;
    error_message = proc.errstring();
    qDebug()<<"error_message="<<error_message<<" process_exit_code="<<process_exit_code;
    return process_exit_code;
}

#if 0

void AsoundInfo::dump() {
    size_t i = 0;
    qDebug()<<"dump size ="<<mAudioDeviceVector.size();
    for (i = 0 ; i < mAudioDeviceVector.size(); i++) {
        AudioDeviceDescriptor *temp = mAudioDeviceVector.at(i);
        qDebug()<<"mStreamName: "<<temp->mStreamName;
        qDebug()<<"card index: "<<temp->mCardindex;
        qDebug()<<"pcm index: "<<temp->mPcmindex;
        qDebug()<<"playback: "<<temp->mplayback;
        qDebug()<<"capture: "<<temp->mRecord;
    }
}

void AsoundInfo::AudioALSADeviceParser()
{
    getCardName();
    ParseCardIndex();
    GetAllPcmAttribute();

    if (isAdspOptionEnable()) {
        GetAllCompressAttribute();
    }

    QueryPcmDriverCapability();
    dump();
    pcm_stream_info();
    //show_pcm_stream_info();
}
#endif

#if 0

struct pcm_params * AsoundInfo::pcm_params_get(unsigned int card, unsigned int device,unsigned int flags)
{
    qDebug()<<"pcm_params_get card="<<card<<" device="<<device<<" flags=",flags;
    char compress_info_path[PROC_READ_BUFFER_SIZE];
    int result;
    char *Rch;
    char *rest_of_str = NULL;
    std::vector<char> result_err;
    std::vector<char> result_out;
    result= snprintf(compress_info_path, sizeof(compress_info_path),"asoundtool  -D %d -d %d -i %d", card, device,flags);
    int ret = exec(compress_info_path,&result_out,&result_err);
    if(!ret)
    {
        qDebug()<<__FUNCTION__<<"exec error return";
        return NULL;
    }
    char *data = &result_out[0];
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    qDebug()<<qlist;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<qlist.at(i);
        QString str = qlist.at(i);
        char* ch=NULL;
        QByteArray ba = str.toLatin1(); // must
        ch=ba.data();
        if (strncmp(ch, "Access", 6) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
               Rch = strtok_r(NULL, ": ", &rest_of_str);
            }
            qDebug()<<"Access:"<<Rch;
            strcpy(params.Access,Rch);
            continue;
        }
        if (strncmp(ch, "Format[0]", 9) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
               Rch = strtok_r(NULL, ": ", &rest_of_str);
            }
            qDebug()<<"Format[0]:"<<Rch;
            strcpy(params.Format0,Rch);
            continue;
        }
        if (strncmp(ch, "Format[1]", 9) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
               Rch = strtok_r(NULL, ": ", &rest_of_str);
            }
            qDebug()<<"Format[1]:"<<Rch;
            strcpy(params.Format1,Rch);
            continue;
        }
        if (strncmp(ch, "Format_Name", 11) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
               Rch = strtok_r(NULL, ": ", &rest_of_str);
            }
            qDebug()<<"Format_Name:"<<Rch;
            strcpy(params.Format_Name,Rch);
            continue;
        }
        if (strncmp(ch, "Subformat", 9) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
               Rch = strtok_r(NULL, ": ", &rest_of_str);
            }
            qDebug()<<"Subformat:"<<Rch;
            strcpy(params.Subformat,Rch);
            continue;
        }
        if (strncmp(ch, "PCM_PARAM_RATE", 14) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL)
            {
                qDebug()<<"PCM_PARAM_RATE:"<< rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
            continue;
        }
        if (strncmp(ch, "PCM_PARAM_CHANNELS", 18) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_CHANNELS:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_SAMPLE_BITS", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_SAMPLE_BITS:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_FRAME_BITS", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_FRAME_BITS:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_PERIOD_TIME", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_PERIOD_TIME:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_PERIOD_SIZE", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_PERIOD_SIZE:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_PERIOD_BYTES", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_PERIOD_BYTES:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_PERIODS", 17) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_PERIODS:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_BUFFER_TIME", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_BUFFER_TIME:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_BUFFER_SIZE", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_BUFFER_SIZE:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_BUFFER_BYTES", 20) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_BUFFER_BYTES:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
        if (strncmp(ch, "PCM_PARAM_TICK_TIME", 18) == 0) {
            Rch = strtok_r(ch, ":", &rest_of_str);
            if(Rch!=NULL){
                qDebug()<<"PCM_PARAM_TICK_TIME:"<<rest_of_str;
                params.rate.min=pcm_params_get_min(rest_of_str);
                params.rate.max=pcm_params_get_max(rest_of_str);
            }
        }
    }

}

unsigned int AsoundInfo::pcm_params_get_min(char *data)
{
    qDebug()<<"pcm_params_get_min"<<data;
    char *Rch;
    char *rest_of_str = NULL;
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[:]"),QString::SkipEmptyParts);
    for (int i=0;i<qlist.length();i++)
    {
        qDebug()<<qlist.at(i);
        QString str = qlist.at(i);
        char* ch;
        QByteArray ba = str.toLatin1(); // must
        ch=ba.data();
        if (strncmp(ch, "min", 3) == 0) {
            Rch = strtok_r(ch, "min=", &rest_of_str);
            qDebug()<<"min:"<<Rch;
            return atoi(Rch);
        }
    }
}

unsigned int AsoundInfo::pcm_params_get_max(char *data)
{
    qDebug()<<"pcm_params_get_max"<<data;
    char *Rch;
    char *rest_of_str = NULL;
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[:]"),QString::SkipEmptyParts);
    for (int i=0;i<qlist.length();i++)
    {
        qDebug()<<qlist.at(i);
        QString str = qlist.at(i);
        char* ch;
        QByteArray ba = str.toLatin1(); // must
        ch=ba.data();
        if (strncmp(ch, "max", 3) == 0) {
            Rch = strtok_r(ch, "max=", &rest_of_str);
            qDebug()<<"max:"<<Rch;
            return atoi(Rch);
        }
    }
}

int AsoundInfo::pcm_stream_status(AudioDeviceDescriptor *temp,int subdevice)
{
    char compress_info_path[PROC_READ_BUFFER_SIZE];
    std::vector<char> result_err;
    std::vector<char> result_out;
    int result;
    if (temp->mplayback == 1) {
        result= snprintf(compress_info_path, sizeof(compress_info_path),"cat  /proc/asound/card%d/pcm%dp/sub%d/status", temp->mCardindex, temp->mPcmindex,subdevice);
        int ret = exec(compress_info_path,&result_out,&result_err);
        qDebug()<<__FUNCTION__<<result_out;
    }
    if (temp->mRecord == 1) {
        result= snprintf(compress_info_path, sizeof(compress_info_path),"cat  /proc/asound/card%d/pcm%dc/sub%d/status", temp->mCardindex, temp->mPcmindex,subdevice);
        int ret = exec(compress_info_path,&result_out,&result_err);
        qDebug()<<__FUNCTION__<<result_out;
    }
}
int AsoundInfo::pcm_stream_info()
{
    struct pcm_params *params = NULL;
    AudioDeviceDescriptor *temp = NULL;;
    int Direction = 0;
    char pcm_info_path[PROC_READ_BUFFER_SIZE];
    int result;
    char *Rch;
    char *rest_of_str = NULL;
    std::vector<char> result_err;
    std::vector<char> result_out;
    qDebug()<<"pcm_stream_info";
    for (int i = 0; i < mAudioDeviceVector.size(); i++) {
        temp = mAudioDeviceVector.at(i);
        qDebug()<<"pcm:"<<temp->mPcmindex<<" temp->mStreamName:"<<temp->mStreamName<<" mCodecName:"<<temp->mCodecName<<" mCardindex:"<<temp->mCardindex<<" mPcmindex:"<<temp->mPcmindex;
        if (temp->mplayback == 1) {
            result= snprintf(pcm_info_path, sizeof(pcm_info_path),"cat  /proc/asound/card%d/pcm%dp/info", temp->mCardindex, temp->mPcmindex);
            int ret = exec(pcm_info_path,&result_out,&result_err);
            qDebug()<<__FUNCTION__<<result_out;
            GetpcminfoAttribute(temp,&result_out[0],0);
            if(temp->mPlayinfo.subdevices_count!=temp->mPlayinfo.subdevices_avail)
            {
                qDebug()<<__FUNCTION__<<"stream is alive";
                //qDebug()<<__FUNCTION__<<"subdevices_count="<<temp->mPlayinfo.subdevices_count<<"  subdevices_avail="<<temp->mPlayinfo.subdevices_avail;
                pcm_stream_status(temp,0);
            }
        }
        if (temp->mRecord == 1) {
            result= snprintf(pcm_info_path, sizeof(pcm_info_path),"cat  /proc/asound/card%d/pcm%dc/info", temp->mCardindex, temp->mPcmindex);
            int ret = exec(pcm_info_path,&result_out,&result_err);
            qDebug()<<__FUNCTION__<<result_out;
            GetpcminfoAttribute(temp,&result_out[0],1);
            if(temp->mRecordinfo.subdevices_count!=temp->mRecordinfo.subdevices_avail)
            {
                qDebug()<<__FUNCTION__<<"stream is alive";
                //qDebug()<<__FUNCTION__<<"subdevices_count="<<temp->mRecordinfo.subdevices_count<<"  subdevices_avail="<<temp->mRecordinfo.subdevices_avail;
                pcm_stream_status(temp,0);
            }
        }
    }
    return 0;
}

int AsoundInfo::QueryPcmDriverCapability() {
    struct pcm_params *params = NULL;
    AudioDeviceDescriptor *temp = NULL;;
    int Direction = 0;

    for (int i = 0; i < mAudioDeviceVector.size(); i++) {
        temp = mAudioDeviceVector.at(i);
        qDebug()<<"pcm:"<<temp->mPcmindex<<" temp->mStreamName:"<<temp->mStreamName<<" mCodecName:"<<temp->mCodecName<<" mCardindex:"<<temp->mCardindex<<" mPcmindex:"<<temp->mPcmindex;
        if (temp->mplayback == 1) {
            params = pcm_params_get(temp->mCardindex, temp->mPcmindex, 1);
            if (params == NULL) {
                qDebug()<<"Device %zu does not exist playback"<<i;
            } else {
                if (temp->mplayback == 1) {
                    GetPcmDriverparameters(&temp->mPlayparam, params);
                }
            }
        }

        if (temp->mRecord == 1) {
            params = pcm_params_get(temp->mCardindex, temp->mPcmindex, 0);
            if (params == NULL) {
                qDebug()<<"Device %zu does not exist capture"<<i;
            } else {
                if (temp->mRecord == 1) {
                    GetPcmDriverparameters(&temp->mRecordparam, params);
                }
            }
        }
    }
    return 0;
}

int AsoundInfo::GetPcmDriverparameters(AudioPcmDeviceparam *PcmDeviceparam, struct pcm_params *params) {
    qDebug()<<__FUNCTION__;

    PcmDeviceparam->mRateMin = params->rate.min;
    PcmDeviceparam->mRateMax = params->rate.max;
    //qDebug()<<(mLogEnable, "Rate:\tmin=%uHz\tmax=%uHz\n", PcmDeviceparam->mRateMin, PcmDeviceparam->mRateMax);

    PcmDeviceparam->mChannelMin = params->channels.min;
    PcmDeviceparam->mChannelMax = params->channels.max;
    //qDebug()<<(mLogEnable, "Channels:\tmin=%u\t\tmax=%u\n", PcmDeviceparam->mChannelMin, PcmDeviceparam->mChannelMax);

    PcmDeviceparam->mSampleBitMin = params->sample_bits.min;
    PcmDeviceparam->mSampleBitMax = params->sample_bits.max;
    //qDebug()<<(mLogEnable, "Sample bits:\tmin=%u\t\tmax=%u\n", PcmDeviceparam->mSampleBitMin, PcmDeviceparam->mSampleBitMax);

    PcmDeviceparam->mPreriodSizeMin = params->period_size.min;
    PcmDeviceparam->mPreriodSizeMax = params->period_size.max;
    //qDebug()<<(mLogEnable, "Period size:\tmin=%u\t\tmax=%u\n", PcmDeviceparam->mPreriodSizeMin, PcmDeviceparam->mPreriodSizeMax);

    PcmDeviceparam->mPreriodCountMin = params->periods.min;
    PcmDeviceparam->mPreriodCountMax = params->periods.max;
    //qDebug()<<(mLogEnable, "Period count:\tmin=%u\t\tmax=%u\n", PcmDeviceparam->mPreriodCountMin,    PcmDeviceparam->mPreriodCountMax);

    PcmDeviceparam->mBufferBytes = params->buffer_bytes.max;
    //qDebug()<<"PCM_PARAM_BUFFER_BYTES :\t max=%u\t\n", PcmDeviceparam->mBufferBytes;

    return 0;
}

bool AsoundInfo::isAdspOptionEnable()
{
    qDebug()<<__FUNCTION__;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int mPcmFile = fopen("/proc/config.gz | gzip -d",&result_out,&result_err);
    if (!mPcmFile)
    {
        qDebug()<<("%s(), config.gz open success", __FUNCTION__);
        QString str(&result_out[0]);
        if(str.contains("CONFIG_MTK_AUDIODSP_SUPPORT=y", Qt::CaseSensitive)) //true
        {
            qDebug()<<__FUNCTION__<<"DSP =Y";
            return true;
        }else
        {
            qDebug()<<__FUNCTION__<<"DSP =N";
            return false;
        }
    }else
    {
        qDebug()<<("%s(), config.gz open fail", __FUNCTION__);
    }
}

bool AsoundInfo::opendir(QString dirname,std::vector<char> * result_out,std::vector<char>  *result_err)
{
    Process proc;
    QString error_message;
    int process_exit_code = 0;
    QString cmd = QString("\"%1\" shell ls ").arg(adbCommand());
    //QString cmd ="/work/version/platform-tools/adb shell cat /proc/asound/pcm";
    cmd +=dirname;
    qDebug() <<cmd;
    proc.start(cmd, false);
    process_exit_code = proc.wait();
    *result_err = proc.errbytes;
    *result_out = proc.outbytes;
    error_message = proc.errstring();
    qDebug()<<"error_message="<<error_message<<" process_exit_code="<<process_exit_code;
    return process_exit_code;
}

#if 0
boot AsoundInfo::readdir(QStringList * qlist ,std::vector<char> * result_out)
{
    char data = &result_out[0];
    QString str = QString(data);
    qDebug()<<str;
    *qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    qDebug()<<qlist;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<"pcm- "<<qlist.at(i);
        QString tmp = qlist.at(i);
        QStringList tmplist =  tmp.split(QRegExp(":"),QString::SkipEmptyParts);
        qDebug()<<tmplist;
        for (int i=0;i<tmplist.length();i++)
        {
            qDebug()<<"route="<<tmplist.at(i);

        }
    }
}
#endif

void AsoundInfo::GetpcminfoAttribute(AudioDeviceDescriptor *mAudioDeviceDescriptor ,char * result_out ,int stream_type)
{
    qDebug()<<__FUNCTION__;
    char *Rch;
    char *rest_of_str = NULL;
    char *data = &result_out[0];
    QString str = QString(data);
    //qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    //qDebug()<<qlist;
    for (int i=0;i<qlist.length();i++)
    {
        //qDebug()<<qlist.at(i);
        QString str = qlist.at(i);
        char* ch;
        QByteArray ba = str.toLatin1(); // must
        ch=ba.data();
        if(mAudioDeviceDescriptor!=NULL) {
            if (stream_type==0)
            {
                Rch = strtok_r(ch, ": ", &rest_of_str);
                //qDebug()<<"GetpcminfoAttribute rest_of_str="<<rest_of_str;
                if (Rch  != NULL) {
                    if (strncmp(Rch, "card", 4) == 0) {
                        mAudioDeviceDescriptor->mPlayinfo.card = atoi(rest_of_str);
                    } else if (strncmp(Rch, "device", 6) == 0) {
                        mAudioDeviceDescriptor->mPlayinfo.stream_type = atoi(rest_of_str);
                    } else if (strncmp(Rch, "stream", 6) == 0) {
                        Rch = strtok_r(NULL, ": ", &rest_of_str);
                        if ((strncmp(Rch, "PLAYBACK", 8)) == 0) {
                            mAudioDeviceDescriptor->mPlayinfo.stream_type = 1;
                        } else if ((strncmp(Rch, "CAPTURE", 7)) == 0) {
                            qDebug()<<Rch<<"------ERROR-----";
                            assert("NULL");
                        }
                    } else if ((strncmp(Rch, "subdevices_count", 16)) == 0) {
                        mAudioDeviceDescriptor->mPlayinfo.subdevices_count = atoi(rest_of_str);
                    } else if ((strncmp(Rch, "subdevices_avail", 16)) == 0) {
                        mAudioDeviceDescriptor->mPlayinfo.subdevices_avail = atoi(rest_of_str);
                    }
                }
            }
            if (stream_type==1)
            {
                Rch = strtok_r(ch, ": ", &rest_of_str);
                if (Rch  != NULL) {
                    if (strncmp(Rch, "card", 4) == 0) {
                        mAudioDeviceDescriptor->mRecordinfo.card = atoi(rest_of_str);
                    } else if (strncmp(Rch, "device", 6) == 0) {
                        mAudioDeviceDescriptor->mRecordinfo.stream_type = atoi(rest_of_str);
                    } else if (strncmp(Rch, "stream", 6) == 0) {
                        Rch = strtok_r(NULL, ": ", &rest_of_str);
                        if ((strncmp(Rch, "PLAYBACK", 8)) == 0) {
                            qDebug()<<Rch<<"------ERROR-----";
                            assert("NULL");
                        } else if ((strncmp(Rch, "CAPTURE", 7)) == 0) {
                            mAudioDeviceDescriptor->mRecordinfo.stream_type = 1;
                        }
                    } else if ((strncmp(Rch, "subdevices_count", 16)) == 0) {
                        mAudioDeviceDescriptor->mRecordinfo.subdevices_count = atoi(rest_of_str);
                    } else if ((strncmp(Rch, "subdevices_avail", 16)) == 0) {
                        mAudioDeviceDescriptor->mRecordinfo.subdevices_avail = atoi(rest_of_str);
                    }
                }
            }
        }
    }
}

void AsoundInfo::GetAllCompressAttribute(void) {
    qDebug()<<("%s()", __FUNCTION__);
    char *tempbuffer;
    char compress_info_path[PROC_READ_BUFFER_SIZE];
    int result;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int mPcmFile = opendir(ALSASOUND_PCM_LOCATION,&result_out,&result_err);
    struct dirent *de;
    result= snprintf(compress_info_path, sizeof(compress_info_path),
                     "%s/card%d/", ALSASOUND_ASOUND_LOCATION, mCardIndex);

    if (result < 0) {
        qDebug()<<("%s(), Soundcard path fail result = %d", __FUNCTION__, result);
        return;
    }
    char *data = &result_out[0];
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    qDebug()<<qlist;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<qlist.at(i);
        QString str = qlist.at(i);
        char* ch;
        QByteArray ba = str.toLatin1(); // must
        ch=ba.data();
        if (strncmp(ch, "compr", 5) == 0) {
            strncat(compress_info_path, ch, strlen(ch));
            strncat(compress_info_path, "/info", 5);
            int compress_file = fopen(compress_info_path,&result_out,&result_err);
            if (!compress_file) {
                qDebug()<<"Compress info open success"<<__FUNCTION__;
                char *Rch;
                char *rest_of_str = NULL;
                AudioDeviceDescriptor *mAudioDeviceDescriptor = NULL;
                mAudioDeviceDescriptor = new AudioDeviceDescriptor();
                tempbuffer = &result_out[0];
                if(mAudioDeviceDescriptor!=NULL) {
                    Rch = strtok_r(tempbuffer, ": ", &rest_of_str);
                    if (Rch  != NULL) {
                        if (strncmp(Rch, "card", 4) == 0) {
                            mAudioDeviceDescriptor->mCardindex = atoi(rest_of_str);
                        } else if (strncmp(Rch, "device", 6) == 0) {
                            mAudioDeviceDescriptor->mPcmindex = atoi(rest_of_str);
                        } else if (strncmp(Rch, "stream", 6) == 0) {
                            Rch = strtok_r(NULL, ": ", &rest_of_str);
                            if ((strncmp(Rch, "PLAYBACK", 8)) == 0) {
                                mAudioDeviceDescriptor->mplayback = 1;
                            } else if ((strncmp(Rch, "CAPTURE", 7)) == 0) {
                                mAudioDeviceDescriptor->mRecord = 1;
                            }
                        } else if ((strncmp(Rch, "id", 2)) == 0) {
                            Rch = strtok_r(NULL, " ", &rest_of_str);
                            mAudioDeviceDescriptor->mStreamName = QString(Rch);
                        }
                    }
                }
                mAudioComprDevVector.append(mAudioDeviceDescriptor);
            } else {
                qDebug()<<__FUNCTION__<<"Compress file open fail";
            }
        }
    }
}

void AsoundInfo::GetAllPcmAttribute(void) {
    qDebug()<< __FUNCTION__;
    qDebug()<<__FUNCTION__;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int mPcmFile = fopen(ALSASOUND_PCM_LOCATION,&result_out,&result_err);
    if (!mPcmFile)
    {
        qDebug()<<("%s(), Pcm open success", __FUNCTION__);
        AddPcmString(&result_out[0]);
    }else
    {
        qDebug()<<("%s(), Pcm open fail", __FUNCTION__);
    }
    if (mPcmFile)
    {
            qDebug()<<("%s() fclose mPcmFile fail", __FUNCTION__);
    }
}

void AsoundInfo::AddPcmString(char *InputBuffer) {
    qDebug()<<"AddPcmString InputBuffer = "<<InputBuffer;
    char *Rch;
    char *rest_of_str = NULL;
    char *data = InputBuffer;
    QString str = QString(data);
    qDebug()<<str;
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    qDebug()<<qlist;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<"AddPcmString i ="<<i;
        qDebug()<<"AddPcmString pcm"<<qlist.at(i);

        QString str = qlist.at(i);
        char* ch=NULL;
        QByteArray ba = str.toLatin1(); // must
        ch=ba.data();
        qDebug()<<"ch ="<<ch;
        AudioDeviceDescriptor *mAudiioDeviceDescriptor = NULL;
        Rch = strtok_r(ch, "-", &rest_of_str);
        // parse for stream name
        if (Rch  != NULL) {
            mAudiioDeviceDescriptor = new AudioDeviceDescriptor();
            mAudiioDeviceDescriptor->mCardindex = atoi(Rch);
            qDebug()<<"mCardindex ="<<Rch;
            Rch = strtok_r(NULL, ":", &rest_of_str);
            mAudiioDeviceDescriptor->mPcmindex = atoi(Rch);
            qDebug()<<"mPcmindex ="<<Rch;
            Rch = strtok_r(NULL, ": ", &rest_of_str);
            mAudiioDeviceDescriptor->mStreamName = QString(Rch);
            qDebug()<<"mStreamName ="<<Rch;
            Rch = strtok_r(NULL, ": ", &rest_of_str);
            mAudioDeviceVector.append(mAudiioDeviceDescriptor);
        }
        // parse for playback or record support
        while (Rch  != NULL) {
            Rch = strtok_r(NULL, ": ", &rest_of_str);
            if (mAudiioDeviceDescriptor != NULL) {
                SetPcmCapability(mAudiioDeviceDescriptor, Rch);
            }
        }
    }
}

void AsoundInfo::SetPcmCapability(AudioDeviceDescriptor *Descriptor, const char *Buffer) {
    if (Buffer == NULL) {
        return;
    }
    QString CompareString = QString(Buffer);
    //ALOGD("SetPcmCapability CompareString = %s",CompareString.string ());
    if ((CompareString.compare(keypcmPlayback)) == 0) {
        qDebug()<<"SetPcmCapability playback support";
        Descriptor->mplayback = 1;
    }
    if ((CompareString.compare(keypcmCapture)) == 0) {
        qDebug()<<"SetPcmCapability capture support";
        Descriptor->mRecord = 1;
    }
}

void AsoundInfo::getCardName() {
   qDebug()<<"sound card name = "<<mCardName;
}

void AsoundInfo::ParseCardIndex()
{
    /*
     * $adb shell cat /proc/asound/cards
     *  0 [mtsndcard      ]: mt-snd-card - mt-snd-card
     *                       mt-snd-card
     * mCardIndex = 0;
     */
    qDebug()<<__FUNCTION__;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int mCardFile = fopen(ALSASOUND_CARD_LOCATION,&result_out,&result_err);
    qDebug()<<result_out;
    bool isCardIndexFound = false;
    if (!mCardFile) {
        qDebug()<<("card open success");
            char *tempbuffer = &result_out[0];
            if (strchr(tempbuffer, '[')) {  // this line contain '[' character
                char *Rch = strtok(tempbuffer, "[");
                if (!Rch) {
                    goto release_source;
                }
                mCardIndex= atoi(Rch);
                qDebug()<<"tcurrent CardIndex = %d, Rch = %s"<<mCardIndex<<Rch;
                Rch = strtok(NULL, " ]");
                if (!Rch) {
                    goto release_source;
                }
                qDebug()<<"tcurrent sound card name = %s"<<Rch;
                mCardName = Rch;
            }
    } else {
        qDebug()<<("Pcm open fail1");
    }
release_source:
    if (mCardFile) {
      qDebug()<<("Pcm open fail2");
    }
}

#endif
