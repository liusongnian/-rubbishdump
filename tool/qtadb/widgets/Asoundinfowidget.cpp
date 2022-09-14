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


#include "Asoundinfowidget.h"
#include "ui_Asoundinfowidget.h"
#include "DepartNodeItem.h"
#include "EmployeeNodeItem.h"
#include "StreamNodeItem.h"
#include "./widgets/audio_drv/AudioALSADeviceConfigManager.h"

#include <QFileDialog>
#include <QMessageBox>
#include "../abstractProcess/MyProcess.h"

#define ALSASOUND_CARD_LOCATION "/proc/asound/cards"
#define ALSASOUND_DEVICE_LOCATION "/proc/asound/devices/"
#define ALSASOUND_ASOUND_LOCATION "/proc/asound"
#define ALSASOUND_PCM_LOCATION "/proc/asound/pcm"
#define AUDIO_DEVICE_EXT_CONFIG_FILE "/vendor/etc/audio_device.xml"
#define PROC_READ_BUFFER_SIZE (256)

static QString keypcmPlayback = QString("playback");
static QString keypcmCapture = QString("capture");

AsoundinfoWidget::AsoundinfoWidget(QWidget *parent,Phone *phone) :
    QWidget(parent),
    ui(new Ui::AsoundinfoWidget)
{
    ui->setupUi(this);

    this->phone=phone;

    QSettings settings;
    this->sdk=settings.value("sdkPath").toString();
    init_kcontrol_info();
    init_audio_device();
#if 0
    init_Asound_info();
    init();
    connect(this->ui->buttonBootIMG, SIGNAL(clicked()), this, SLOT(bootIMG()));
    connect(this->ui->buttonFlashRadio, SIGNAL(clicked()), this, SLOT(flashRadio()));
    connect(this->ui->buttonFlashRecovery, SIGNAL(clicked()), this, SLOT(flashRecovery()));
    connect(this->ui->buttonFlashSPL, SIGNAL(clicked()), this, SLOT(flashSPL()));
    connect(this->ui->buttonFlashZip, SIGNAL(clicked()), this, SLOT(flashZip()));
    connect(ui->tree, SIGNAL(itemChanged(QTreeWidgetItem*,int)), this, SLOT(treeItemChanged(QTreeWidgetItem*,int)));
#endif
}

AsoundinfoWidget::~AsoundinfoWidget()
{
    delete ui;
}

void AsoundinfoWidget::init_Asound_info()
{
    qDebug() << "init_Asound_info!";
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
    ParseCardIndex();
    AudioALSADeviceParser();

}

void AsoundinfoWidget::init_kcontrol_info()
{
    qDebug() << "init_kcontrol_info!";
    gettinymixinfo();
}

void AsoundinfoWidget::init_audio_device()
{
    qDebug() << "init_audio_device!";
    mDeviceConfigManager=  AudioALSADeviceConfigManager().getInstance();
    mDeviceConfigManager->LoadAudioConfig("/work2/audio_device.xml");
    qDebug()<<__FUNCTION__;
}

/*


audioserver运行的时候启动三个service： audioflinger service， audiopolicy service， soundtrigger service。

frameworks/av/media/audioserver/main_audioserver.cpp----binder srevice
dumpsys -l |grep audio
  audio
  media.aaudio---> binder clice
  media.audio_flinger---> binder clice
  media.audio_policy---> binder clice

  media.audio_flinger:

                                                                 app               {APP}
-------------------------------------------------------------------------------------------
audio...xml(audio_policy_configuration.xml)                        |
    |                                         |binder IPC |        |
AudioPolicyManager <----->AudioPolicyService<--------------->AudioFlinger-(mDevicesFactoryHal,mEffectsFactoryHal)      {Framework}
                                              |binder IPC |        |
-------------------------------------------------------------------------------------------
                                                                   |
                                                                  Hal
AudioFlinger:
    mDevicesFactoryHal = DevicesFactoryHalInterface::create(); https://blog.csdn.net/u011279649/article/details/119569549
       ---"android.hardware.audio", "IDevicesFactory");
       ---package + "@" + version + "::" + interface;
       ---"libaudiohal@" + version + ".so"
            --- "create" + interface= createIDevicesFactory -->>getService-->>HIDL
    mEffectsFactoryHal = EffectsFactoryHalInterface::create();---
______________________________________               _____________________________________        _________________________________________________
  "libaudiohal@" + version + ".so"    |              | HIDL android.hardware.audio@7.0.so|        |HIDL android.hardware.audio@7.0-impl-mediatek.so|              |
frameworks/av/media/libaudiohal/impl/<-------------->| hardweare/interfaces              <--------------->vendor/...  binder srevice                      |
______________________________________|              |___________________________________|        |________________________________________________|


dumpsys -l -->look ServiceManager service
HAL service
https://blog.csdn.net/u011279649/article/details/119606531


*/
void AsoundinfoWidget::audioservice()
{
     //get cat /system/etc/init/audioserver.rc   -->>frameworks/av/media/audioserver/audioserver.rc
    //dealwith it

    //get audioserver  ldd audioserver

    //get  audio_policy_configuration.xml   文件读取和配置

    //audio_hw_hal.cpp  接口实现

}




void AsoundinfoWidget::init()
{
    ui->tree->setHeaderHidden(true);
    // 1.创建表格
    ui->tree->setColumnCount(1);
    // 2.拿到表头
    QHeaderView *head = ui->tree->header();
    // 3.设置不能拉伸的列的宽度，设置哪一列能拉伸
    head->setSectionResizeMode(0,QHeaderView::Stretch);
    //head->setSectionResizeMode(1, QHeaderView::Fixed);
    //ui->tree->setColumnWidth(1, 30);
    // 4.（最重要的一步）去掉默认的拉伸最后列属性
    head->setStretchLastSection(false);

    //展开和收缩时信号，以达到变更我三角图片；
    connect(ui->tree, SIGNAL(itemClicked(QTreeWidgetItem *, int)), this, SLOT(onItemClicked(QTreeWidgetItem *, int)));
    connect(ui->tree, SIGNAL(itemExpanded(QTreeWidgetItem *)), this, SLOT(onItemExpanded(QTreeWidgetItem *)));
    connect(ui->tree, SIGNAL(itemCollapsed(QTreeWidgetItem *)), this, SLOT(onItemCollapsed(QTreeWidgetItem *)));
    size_t i = 0;
    qDebug()<<"dump size ="<<mAudioDeviceVector.size();

    for (i = 0 ; i < mAudioDeviceVector.size(); i++) {
        AudioDeviceDescriptor *temp = mAudioDeviceVector.at(i);
        qDebug()<<"mStreamName: "<<temp->mStreamName;
        qDebug()<<"card index: "<<temp->mCardindex;
        qDebug()<<"pcm index: "<<temp->mPcmindex;
        qDebug()<<"playback: "<<temp->mplayback;
        qDebug()<<"capture: "<<temp->mRecord;
        // 一级列表节点
        QTreeWidgetItem *pRootDeptItem = new QTreeWidgetItem();
        pRootDeptItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        //设置Data用于区分，Item是分组节点还是子节点，0代表分组节点，1代表子节点
        pRootDeptItem->setData(0, Qt::UserRole, 0);
        pRootDeptItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        //pRootDeptItem->setCheckState(1, Qt::Unchecked);
        DepartNodeItem *pItemName = new DepartNodeItem(ui->tree);
        pItemName->setLevel(0);
        QString qsGroupName = temp->mStreamName;
        pItemName->setText(qsGroupName);
        //插入分组节点
        ui->tree->addTopLevelItem(pRootDeptItem);
        ui->tree->setItemWidget(pRootDeptItem, 0, pItemName);

        if(temp->mplayback==1)
        {
            addChildNode(pRootDeptItem,1,"playback");
        }
        if(temp->mRecord==1)
        {
            addChildNode(pRootDeptItem,1,"record");
        }
    }
}


QTreeWidgetItem* AsoundinfoWidget::addChildNode(QTreeWidgetItem *parent, int index, QString namePre)
{
    QTreeWidgetItem *pDeptItem = new QTreeWidgetItem();
    pDeptItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    //设置Data用于区分，Item是分组节点还是子节点，0代表分组节点，1代表子节点
    pDeptItem->setData(0, Qt::UserRole, 0);
    DepartNodeItem *pItemName = new DepartNodeItem(ui->tree);
    pDeptItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    //pDeptItem->setCheckState(1, Qt::Unchecked);
    int level = 0;
    DepartNodeItem *departNode = dynamic_cast<DepartNodeItem*>(ui->tree->itemWidget(parent, 0));
    if (departNode) {
        level = departNode->getLevel();
        level ++;
    }
    pItemName->setLevel(level);
    pItemName->setText(namePre);
    //擦入分组节点
    parent->addChild(pDeptItem);
    ui->tree->setItemWidget(pDeptItem, 0, pItemName);

    return pDeptItem;
}

void AsoundinfoWidget::updateParentItem(QTreeWidgetItem *item)
{
    QTreeWidgetItem *parent = item->parent();
    if(parent == NULL)
    {
        return ;
    }
    int selectedCount = 0;
    int childCount = parent->childCount();
    //判断有多少个子项被选中
    for(int i=0; i<childCount; i++) {
        QTreeWidgetItem* childItem = parent->child(i);
        if(childItem->checkState(1) == Qt::Checked) {
            selectedCount++;
        }
    }
    if(selectedCount <= 0)  //如果没有子项被选中，父项设置为未选中状态
    {
        //parent->setCheckState(1,Qt::Unchecked);
    }
    else if(selectedCount>0 && selectedCount<childCount)    //如果有部分子项被选中，父项设置为部分选中状态，即用灰色显示
    {
       // parent->setCheckState(1,Qt::PartiallyChecked);
        // 重点：针对半选中状态的设置需要单独调用setParentPartiallyChecked后返回，否则上级节点的状态及当前节点的下级第一个节点的状态会不正确
        setParentPartiallyChecked(parent);
        return;
    }
    else if(selectedCount == childCount)    //如果子项全部被选中，父项则设置为选中状态
    {
        //parent->setCheckState(1,Qt::Checked);
    }
    updateParentItem(parent);
}

void AsoundinfoWidget::setParentPartiallyChecked(QTreeWidgetItem *itm)
{
    QTreeWidgetItem *parent = itm->parent();
    if(parent) {
        //parent->setCheckState(1,Qt::PartiallyChecked);
        setParentPartiallyChecked(parent);
    }
}

void AsoundinfoWidget::treeItemChanged(QTreeWidgetItem *item, int column)
{
    qDebug()<<__FUNCTION__;
    if(Qt::Checked == item->checkState(1)){
        int count = item->childCount(); //返回子项的个数
        if(count >0) {
            for(int i=0; i<count; i++) {
                //item->child(i)->setCheckState(1,Qt::Checked);
            }
        } else {
            updateParentItem(item);
        }
    } else if(Qt::Unchecked == item->checkState(1)) {
        int count = item->childCount();
        if(count > 0) {
            for(int i=0; i<count; i++) {
                //item->child(i)->setCheckState(1,Qt::Unchecked);
            }
        } else {
            updateParentItem(item);
        }
    }
}

void AsoundinfoWidget::update_pcm_info(QString str)
{
     for (int i = 0 ; i < mAudioDeviceVector.size(); i++) {
         AudioDeviceDescriptor *temp = mAudioDeviceVector.at(i);
         if(temp->mStreamName==str)
         {
             qDebug()<<"mStreamName: "<<temp->mStreamName;
             qDebug()<<"card index: "<<temp->mCardindex;
             qDebug()<<"pcm index: "<<temp->mPcmindex;
             qDebug()<<"playback: "<<temp->mplayback;
             qDebug()<<"capture: "<<temp->mRecord;
             if(temp->mplayback==1)
             {
                 qDebug()<<"mPlayinfo.card: "<<temp->mPlayinfo.card;
                 qDebug()<<"mPlayinfo.device: "<<temp->mPlayinfo.device;
                 qDebug()<<"mPlayinfo.stream_type: "<<temp->mPlayinfo.stream_type;
                 qDebug()<<"mPlayinfo.subdevice: "<<temp->mPlayinfo.subdevice;
                 qDebug()<<"mPlayinfo.subdevices_count: "<<temp->mPlayinfo.subdevices_count;
                 qDebug()<<"mPlayinfo.subdevices_avail: "<<temp->mPlayinfo.subdevices_avail;

                 qDebug()<<"mPlayparam.mRateMin: "<<temp->mPlayparam.mRateMin<<"-"<<"mPlayparam.mRateMax: "<<temp->mPlayparam.mRateMax;
                 qDebug()<<"mPlayparam.mChannelMin: "<<temp->mPlayparam.mChannelMin<<"-"<<"mPlayparam.mChannelMax: "<<temp->mPlayparam.mChannelMax;
                 qDebug()<<"mPlayparam.mSampleBitMin: "<<temp->mPlayparam.mSampleBitMin<<"-"<<"mPlayparam.mSampleBitMax: "<<temp->mPlayparam.mSampleBitMax;
                 qDebug()<<"mPlayparam.mPreriodSizeMin: "<<temp->mPlayparam.mPreriodSizeMin<<"-"<<"mPlayparam.mPreriodSizeMax: "<<temp->mPlayparam.mPreriodSizeMax;
                 qDebug()<<"mPlayparam.mPreriodCountMin: "<<temp->mPlayparam.mPreriodCountMin<<"-"<<"mPlayparam.mPreriodCountMax: "<<temp->mPlayparam.mPreriodCountMax;
             }
             if(temp->mRecord==1)
             {
                 qDebug()<<"mRecordinfo.card: "<<temp->mRecordinfo.card;
                 qDebug()<<"mRecordinfo.device: "<<temp->mRecordinfo.device;
                 qDebug()<<"mRecordinfo.stream_type: "<<temp->mRecordinfo.stream_type;
                 qDebug()<<"mRecordinfo.subdevice: "<<temp->mRecordinfo.subdevice;
                 qDebug()<<"mRecordinfo.subdevices_count: "<<temp->mRecordinfo.subdevices_count;
                 qDebug()<<"mRecordinfo.subdevices_avail: "<<temp->mRecordinfo.subdevices_avail;

                 qDebug()<<"mRecordparam.mRateMin: "<<temp->mRecordparam.mRateMin<<"-"<<"mRecordparam.mRateMax: "<<temp->mRecordparam.mRateMax;
                 qDebug()<<"mRecordparam.mChannelMin: "<<temp->mRecordparam.mChannelMin<<"-"<<"mRecordparam.mChannelMax: "<<temp->mRecordparam.mChannelMax;
                 qDebug()<<"mRecordparam.mSampleBitMin: "<<temp->mRecordparam.mSampleBitMin<<"-"<<"mRecordparam.mSampleBitMax: "<<temp->mRecordparam.mSampleBitMax;
                 qDebug()<<"mRecordparam.mPreriodSizeMin: "<<temp->mRecordparam.mPreriodSizeMin<<"-"<<"mRecordparam.mPreriodSizeMax: "<<temp->mRecordparam.mPreriodSizeMax;
                 qDebug()<<"mRecordparam.mPreriodCountMin: "<<temp->mRecordparam.mPreriodCountMin<<"-"<<"mRecordparam.mPreriodCountMax: "<<temp->mRecordparam.mPreriodCountMax;
             }
         }
     }
}

void AsoundinfoWidget::onItemExpanded(QTreeWidgetItem * item)
{
    qDebug()<<__FUNCTION__;
    bool bIsChild = item->data(0, Qt::UserRole).toBool();

    if (!bIsChild) {
        DepartNodeItem *departNode = dynamic_cast<DepartNodeItem*>(ui->tree->itemWidget(item, 0));
        qDebug()<<__FUNCTION__<<departNode->getName();
        update_pcm_info(departNode->getName());
        if (departNode) {
            departNode->setExpanded(true);
        }
    }

}

void AsoundinfoWidget::onItemCollapsed(QTreeWidgetItem * item)
{
    bool bIsChild = item->data(0, Qt::UserRole).toBool();
    if (!bIsChild) {
        DepartNodeItem *departNode = dynamic_cast<DepartNodeItem*>(ui->tree->itemWidget(item, 0));
        if (departNode) {
            departNode->setExpanded(false);
        }
    }
}

void AsoundinfoWidget::onItemClicked(QTreeWidgetItem * item, int column)
{
    qDebug()<<__FUNCTION__;
    if (column == 0) {
        bool bIsChild = item->data(0, Qt::UserRole).toBool();
        if (!bIsChild)
        {
            qDebug()<<__FUNCTION__<<bIsChild;
            item->setExpanded(!item->isExpanded());
        }
        qDebug()<<__FUNCTION__<<"parent";
    }
}

void AsoundinfoWidget::onItem_kcon_Expanded(QTreeWidgetItem * item)
{
    qDebug()<<__FUNCTION__;
}

void AsoundinfoWidget::onItem_kcon_Collapsed(QTreeWidgetItem * item)
{
    qDebug()<<__FUNCTION__;
}

void AsoundinfoWidget::onItem_kcon_Clicked(QTreeWidgetItem * item, int column)
{
    qDebug()<<__FUNCTION__;
}

void AsoundinfoWidget::loadStyleSheet()
{
    QFile file("../QSS/MainWindow.css");
    file.open(QFile::ReadOnly);
    if (file.isOpen())
    {
        this->setStyleSheet("");
        QString qsstyleSheet = QString(file.readAll());
        this->setStyleSheet(qsstyleSheet);
    }
    file.close();
}

QTreeWidgetItem *AsoundinfoWidget::addChildEmpNode(QTreeWidgetItem *parent, int index)
{
    QTreeWidgetItem *pDeptItem = new QTreeWidgetItem();
    //设置Data用于区分，Item是分组节点还是子节点，0代表分组节点，1代表子节点
    pDeptItem->setData(0, Qt::UserRole, 1);
    pDeptItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    //pDeptItem->setCheckState(1, Qt::Unchecked);
    int level = 0;
    DepartNodeItem *departNode = dynamic_cast<DepartNodeItem*>(ui->tree->itemWidget(parent, 0));
    if (departNode) {
        level = departNode->getLevel();
        level ++;
    }

    EmployeeNodeItem *pItemName = new EmployeeNodeItem(ui->tree);
    pItemName->setLevel(level);
    // 加载本地文件，需要修改成本地的路径
    qDebug()<<__LINE__<<__FUNCTION__;
    pItemName->setHeadPath(QString::fromLocal8Bit("/work2/qtadb-master/images/pic/%1.jpg").arg(index));
                    qDebug()<<__LINE__<<__FUNCTION__;
    QString qfullName = QString::fromLocal8Bit("人员%1").arg(index);
                        qDebug()<<__LINE__<<__FUNCTION__;
    pItemName->setFullName(qfullName);
    //擦入分组节点
                    qDebug()<<__LINE__<<__FUNCTION__;
    parent->addChild(pDeptItem);
                    qDebug()<<__LINE__<<__FUNCTION__;
    ui->tree->setItemWidget(pDeptItem, 0, pItemName);
    return pDeptItem;
}

QTreeWidgetItem *AsoundinfoWidget::addChildstreamNode(QTreeWidgetItem *parent, int index)
{
    QTreeWidgetItem *pDeptItem = new QTreeWidgetItem();
    //设置Data用于区分，Item是分组节点还是子节点，0代表分组节点，1代表子节点
    pDeptItem->setData(0, Qt::UserRole, 1);
    pDeptItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    //pDeptItem->setCheckState(1, Qt::Unchecked);
    int level = 0;
    DepartNodeItem *departNode = dynamic_cast<DepartNodeItem*>(ui->tree->itemWidget(parent, 0));
    if (departNode) {
        level = departNode->getLevel();
        level ++;
    }

    EmployeeNodeItem *pItemName = new EmployeeNodeItem(ui->tree);
    pItemName->setLevel(level);
    // 加载本地文件，需要修改成本地的路径
    qDebug()<<__LINE__<<__FUNCTION__;
    pItemName->setHeadPath(QString::fromLocal8Bit("/work2/qtadb-master/images/pic/%1.jpg").arg(index));
                    qDebug()<<__LINE__<<__FUNCTION__;
    QString qfullName = QString::fromLocal8Bit("人员%1").arg(index);
                        qDebug()<<__LINE__<<__FUNCTION__;
    pItemName->setFullName(qfullName);
    //擦入分组节点
                    qDebug()<<__LINE__<<__FUNCTION__;
    parent->addChild(pDeptItem);
                    qDebug()<<__LINE__<<__FUNCTION__;
    ui->tree->setItemWidget(pDeptItem, 0, pItemName);
    return pDeptItem;
}

//void AsoundinfoWidget::changeEvent(QEvent *e)
//{
//    QWidget::changeEvent(e);
//    switch (e->type()) {
//    case QEvent::LanguageChange:
//        ui->retranslateUi(this);
//        break;
//    default:
//        break;
//    }
//}

void AsoundinfoWidget::bootIMG()
{
    QString output;
    QProcess fastboot;
    fastboot.setProcessChannelMode(QProcess::MergedChannels);
    fastboot.start("\"" + this->sdk + "\"fastboot devices");
    fastboot.waitForFinished();
    output = fastboot.readAll();
    if (output.contains("fastboot"))
    {
        QProcess *process=new QProcess();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QString tmp;
        QString imgFileName = QFileDialog::getOpenFileName(this, tr("Choose img file..."), ".", tr("IMG File ")+"(*.img)");
        if (!imgFileName.isEmpty())
        {
            process->start("\"" + sdk + "\"fastboot boot " + imgFileName);
            process->waitForFinished(-1);
            process->terminate();
        }
        else
            QMessageBox::warning(this, "Error!", "Operation cancelled!", QMessageBox::Ok);
        delete process;
    }
    else
    {
        this->phone->slotConnectionChanged(FASTBOOT,this->phone->serialNumber);
    }
}

void AsoundinfoWidget::flashSPL()
{
    QString output;
    QProcess fastboot;
    fastboot.setProcessChannelMode(QProcess::MergedChannels);
    fastboot.start("\"" + this->sdk + "\"fastboot devices");
    fastboot.waitForFinished();
    output = fastboot.readAll();
    if (output.contains("fastboot"))
    {
        QProcess *process=new QProcess();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QString tmp;
        QString imgFileName = QFileDialog::getOpenFileName(this, tr("Choose hboot img file..."), ".", tr("IMG File ")+"(*.img)");
        if (!imgFileName.isEmpty())
        {
            process->start("\"" + sdk + "\"fastboot flash hboot " + imgFileName);
            process->waitForFinished(-1);
            tmp = process->readAll();
            if (tmp.contains("error"))
                QMessageBox::warning(this, tr("Error!"), tmp, QMessageBox::Ok);
            else
                QMessageBox::information(this, tr("Success!"), tmp, QMessageBox::Ok);
            process->terminate();
        }
        else
            QMessageBox::warning(this, tr("Error!"), tr("Operation cancelled!"), QMessageBox::Ok);
        delete process;
    }
    else
    {
        this->phone->slotConnectionChanged(FASTBOOT,this->phone->serialNumber);
    }
}

void AsoundinfoWidget::flashRadio()
{
    QString output;
    QProcess fastboot;
    fastboot.setProcessChannelMode(QProcess::MergedChannels);
    fastboot.start("\"" + this->sdk + "\"fastboot devices");
    fastboot.waitForFinished();
    output = fastboot.readAll();
    if (output.contains("fastboot"))
    {
        QProcess *process=new QProcess();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QString tmp;
        QString imgFileName = QFileDialog::getOpenFileName(this, tr("Choose radio img file..."), ".", tr("IMG File ")+"(*.img)");
        if (!imgFileName.isEmpty())
        {
            process->start("\"" + sdk + "\"fastboot flash radio " + imgFileName);
            process->waitForFinished(-1);
            tmp = process->readAll();
            if (tmp.contains("error"))
                QMessageBox::warning(this, tr("Error!"), tmp, QMessageBox::Ok);
            else
                QMessageBox::information(this, tr("Success!"), tmp, QMessageBox::Ok);
            process->terminate();
        }
        else
            QMessageBox::warning(this, tr("Error!"), tr("Operation cancelled!"), QMessageBox::Ok);
        delete process;
    }
    else
    {
        this->phone->slotConnectionChanged(FASTBOOT,this->phone->serialNumber);
    }
}

void AsoundinfoWidget::flashRecovery()
{
    QString output;
    QProcess fastboot;
    fastboot.setProcessChannelMode(QProcess::MergedChannels);
    fastboot.start("\"" + this->sdk + "\"fastboot devices");
    fastboot.waitForFinished();
    output = fastboot.readAll();
    if (output.contains("fastboot"))
    {
        QProcess *process=new QProcess();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QString tmp;
        QString imgFileName = QFileDialog::getOpenFileName(this, tr("Choose recovery img file..."), ".", tr("IMG File ")+"(*.img)");
        if (!imgFileName.isEmpty())
        {
            process->start("\"" + sdk + "\"fastboot flash recovery " + imgFileName);
            process->waitForFinished(-1);
            tmp = process->readAll();
            if (tmp.contains("error"))
                QMessageBox::warning(this, tr("Error!"), tmp, QMessageBox::Ok);
            else
                QMessageBox::information(this, tr("Success!"), tmp, QMessageBox::Ok);
            process->terminate();
        }
        else
            QMessageBox::warning(this, tr("Error!"), tr("Operation cancelled!"), QMessageBox::Ok);
        delete process;
    }
    else
    {
        this->phone->slotConnectionChanged(FASTBOOT,this->phone->serialNumber);
    }
}

void AsoundinfoWidget::flashZip()
{
    QString output;
    QProcess fastboot;
    fastboot.setProcessChannelMode(QProcess::MergedChannels);
    fastboot.start("\"" + this->sdk + "\"fastboot devices");
    fastboot.waitForFinished();
    output = fastboot.readAll();
    if (output.contains("fastboot"))
    {
        QProcess *process=new QProcess();
        process->setProcessChannelMode(QProcess::MergedChannels);
        QString tmp;
        QString imgFileName = QFileDialog::getOpenFileName(this, tr("Choose zipped img file..."), ".", tr("IMG File ")+"(*.zip)");
        if (!imgFileName.isEmpty())
        {
            process->start("\"" + sdk + "\"fastboot flash zip " + imgFileName);
            process->waitForFinished(-1);
            tmp = process->readAll();
            if (tmp.contains("error"))
                QMessageBox::warning(this, tr("Error!"), tmp, QMessageBox::Ok);
            else
                QMessageBox::information(this, tr("Success!"), tmp, QMessageBox::Ok);
            process->terminate();
        }
        else
            QMessageBox::warning(this, tr("Error!"), tr("Operation cancelled!"), QMessageBox::Ok);
        delete process;
    }
    else
    {
        this->phone->slotConnectionChanged(FASTBOOT,this->phone->serialNumber);
    }
}



QString AsoundinfoWidget::adbCommand() const
{
    qDebug()<<"adb";
    return "/work/version/platform-tools/adb";
}

const char * AsoundinfoWidget::qstringtochar(QString *qs)
{
     std::string str = qs->toStdString();
     return str.c_str();
}

CJSON_PUBLIC(cJSON *)  AsoundinfoWidget::readcontrol(const cJSON * const object, int num)
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
void  AsoundinfoWidget::check_item_type(const cJSON * const object)
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

void  AsoundinfoWidget::value_to_string(const cJSON * const object,QString *qs)
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

void AsoundinfoWidget::mixer_dump()
{
    size_t i = 0;
    qDebug()<<__FUNCTION__<<"dump size ="<<mAudioMixerVector.size();
    for (i = 0 ; i < mAudioMixerVector.size(); i++) {
        AudioMixerinfo *temp = mAudioMixerVector.at(i);
        qDebug()<<"ctl_num: "<<temp->ctl_num;
        qDebug()<<"type: "<<temp->type;
        qDebug()<<"data_size: "<<temp->data_size;
        qDebug()<<"name_value: "<<temp->name_value;
    }
}

void AsoundinfoWidget::gettinymixinfo()
{
    qDebug() << "gettinymixinfo!";
    Process proc;
    QString error_message;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int process_exit_code = 0;
    QString cmd = QString("\"%1\" ").arg(adbCommand());
    cmd +="shell tinymix -a";
    qDebug() <<cmd;
    int rows = 4;
    int cols = 4;
    proc.start(cmd, false);
    process_exit_code = proc.wait();
    result_err = proc.errbytes;
    result_out = proc.outbytes;
    error_message = proc.errstring();
    qDebug()<<result_out;
    char *data = &result_out[0];
    QString str = QString(data);
    QStringList qlist =  str.split(QRegExp("[\r\n]"),QString::SkipEmptyParts);
    AudioMixerinfo *mAudioMixerinfo = NULL;
    int flag=0;
    for (int i=0;i<qlist.length()-1;i++)
    {
        qDebug()<<qlist.at(i);
        QStringList tmp =  qlist.at(i).split(QRegExp("[\t]"),QString::SkipEmptyParts);
        if(flag==0){
            for(int j=0;j<tmp.length();j++)
            {
                qDebug()<<"tmp"<<j<<"="<<tmp.at(j);
                if(tmp.at(j).contains("Mixer name"))
                {
                    qDebug()<<"find Mixer name";
                    continue;
                }
                if(tmp.at(j).contains("Number of controls"))
                {
                    qDebug()<<"find Number of controls";
                    continue;
                }
                if(j==0&&tmp.at(j)=='0')
                {
                    qDebug()<<"find";
                    flag=1;
                }
            }
        }else{
            if(tmp.length()==4&&flag==1){
                mAudioMixerinfo= new AudioMixerinfo();
                mAudioMixerinfo->ctl_num=tmp.at(0);
                mAudioMixerinfo->type=tmp.at(1);
                mAudioMixerinfo->data_size=tmp.at(2);
                mAudioMixerinfo->name_value=tmp.at(3);
                mAudioMixerVector.append(mAudioMixerinfo);
            }else{
                assert(NULL);
            }
        }
    }
    mixer_dump();
    init_kcon_tree();
#if 0
    char *data4 = &result_out[0];
    cJSON* root = cJSON_Parse(data4);
    cJSON* item;
    QString text;
    item=cJSON_GetObjectItem(root,"controls");
    rows=cJSON_GetObjectItem(root,"Number of controls")->valueint;
    qDebug()<<"rows= "<<rows;
    for(int i = 0; i < rows; i++){
        cJSON *obj;
        QString value;
        obj=readcontrol(item,i);
        for (int j = 0; j < cols; j++){
            if(j==0)
            {
                text = cJSON_GetObjectItem(obj,"type")->valuestring;
            }else if(j==1)
            {
                text = QString::number(cJSON_GetObjectItem(obj,"num_values")->valueint);
            }else if(j==2)
            {
                text = cJSON_GetObjectItem(obj,"name")->valuestring;
            }else if(j==3)
            {
                value_to_string(obj,&value);
                text = value;
            }
            qDebug() << "text:" <<text<<"j="<<j;               //设置居中对齐
        }
    }
#endif
}

void AsoundinfoWidget::init_kcon_tree()
{
    ui->tree_kcon->setHeaderHidden(true);
    // 1.创建表格
    ui->tree_kcon->setColumnCount(1);
    // 2.拿到表头
    QHeaderView *head = ui->tree->header();
    // 3.设置不能拉伸的列的宽度，设置哪一列能拉伸
    head->setSectionResizeMode(0,QHeaderView::Stretch);
    //head->setSectionResizeMode(1, QHeaderView::Fixed);
    //ui->tree->setColumnWidth(1, 30);
    // 4.（最重要的一步）去掉默认的拉伸最后列属性
    head->setStretchLastSection(false);

    //展开和收缩时信号，以达到变更我三角图片；
    connect(ui->tree_kcon, SIGNAL(itemClicked(QTreeWidgetItem *, int)), this, SLOT(onItem_kcon_Clicked(QTreeWidgetItem *, int)));
    connect(ui->tree_kcon, SIGNAL(itemExpanded(QTreeWidgetItem *)), this, SLOT(onItem_kcon_Expanded(QTreeWidgetItem *)));
    connect(ui->tree_kcon, SIGNAL(itemCollapsed(QTreeWidgetItem *)), this, SLOT(onItem_kcon_Collapsed(QTreeWidgetItem *)));
    size_t i = 0;
    qDebug()<<"dump size ="<<mAudioDeviceVector.size();

    for (i = 0 ; i < mAudioDeviceVector.size(); i++) {
        AudioDeviceDescriptor *temp = mAudioDeviceVector.at(i);
        qDebug()<<"mStreamName: "<<temp->mStreamName;
        qDebug()<<"card index: "<<temp->mCardindex;
        qDebug()<<"pcm index: "<<temp->mPcmindex;
        qDebug()<<"playback: "<<temp->mplayback;
        qDebug()<<"capture: "<<temp->mRecord;
        // 一级列表节点
        QTreeWidgetItem *pRootDeptItem = new QTreeWidgetItem();
        pRootDeptItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
        //设置Data用于区分，Item是分组节点还是子节点，0代表分组节点，1代表子节点
        pRootDeptItem->setData(0, Qt::UserRole, 0);
        pRootDeptItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        //pRootDeptItem->setCheckState(1, Qt::Unchecked);
        DepartNodeItem *pItemName = new DepartNodeItem(ui->tree_kcon);
        pItemName->setLevel(0);
        QString qsGroupName = temp->mStreamName;
        pItemName->setText(qsGroupName);
        //插入分组节点
        ui->tree_kcon->addTopLevelItem(pRootDeptItem);
        ui->tree_kcon->setItemWidget(pRootDeptItem, 0, pItemName);
    }
}

bool AsoundinfoWidget::fopen(QString filename,std::vector<char> * result_out,std::vector<char>  *result_err)
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

bool AsoundinfoWidget::exec(QString c,std::vector<char> * result_out,std::vector<char>  *result_err)
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

#if 1

void AsoundinfoWidget::dump() {
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

void AsoundinfoWidget::AudioALSADeviceParser()
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

#if 1

struct pcm_params * AsoundinfoWidget::pcm_params_get(unsigned int card, unsigned int device,unsigned int flags)
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

unsigned int AsoundinfoWidget::pcm_params_get_min(char *data)
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

unsigned int AsoundinfoWidget::pcm_params_get_max(char *data)
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

int AsoundinfoWidget::pcm_stream_status(AudioDeviceDescriptor *temp,int subdevice)
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
int AsoundinfoWidget::pcm_stream_info()
{
    struct pcm_params *params = NULL;
    AudioDeviceDescriptor *temp = NULL;
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

int AsoundinfoWidget::QueryPcmDriverCapability() {
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

int AsoundinfoWidget::GetPcmDriverparameters(AudioPcmDeviceparam *PcmDeviceparam, struct pcm_params *params) {
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

bool AsoundinfoWidget::isAdspOptionEnable()
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

bool AsoundinfoWidget::opendir(QString dirname,std::vector<char> * result_out,std::vector<char>  *result_err)
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
boot AsoundinfoWidget::readdir(QStringList * qlist ,std::vector<char> * result_out)
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

void AsoundinfoWidget::GetpcminfoAttribute(AudioDeviceDescriptor *mAudioDeviceDescriptor ,char * result_out ,int stream_type)
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

void AsoundinfoWidget::GetAllCompressAttribute(void) {
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

void AsoundinfoWidget::GetAllPcmAttribute(void) {
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

void AsoundinfoWidget::AddPcmString(char *InputBuffer) {
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

void AsoundinfoWidget::SetPcmCapability(AudioDeviceDescriptor *Descriptor, const char *Buffer) {
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

void AsoundinfoWidget::getCardName() {
   qDebug()<<"sound card name = "<<mCardName;
}

void AsoundinfoWidget::ParseCardIndex()
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

//framer av flinger






