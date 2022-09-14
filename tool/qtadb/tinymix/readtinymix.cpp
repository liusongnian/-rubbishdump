#include "readtinymix.h"
#include "ui_readtinymix.h"
#include "../abstractProcess/MyProcess.h"
//#include "about.h"

#include <QFileDialog>
#include <QtXlsx>
#include <QDebug>
#include <QStandardPaths>
#include <QTableWidgetItem>
#include <QMessageBox>

ReadTinymix::ReadTinymix(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::ReadTinymix)
{
    ui->setupUi(this);

    init();
}

ReadTinymix::~ReadTinymix()
{
    delete ui;
}

void ReadTinymix::init()
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

void ReadTinymix::readToTable(const QString sheet)
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

void ReadTinymix::tinymixToTable(const QString sheet)
{
     qDebug() << "tinymixToTable:";
}

void ReadTinymix::writeToExcel(const QString sheet)
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
void ReadTinymix::slot_openExcel()
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

void ReadTinymix::slot_saveExcel()
{
    qDebug() << "save!" << cBox_text << ui->comboBox->currentText();
    writeToExcel(cBox_text);
    ui->statusBar->showMessage(tr("文件已保存!"), 3000);
}

void ReadTinymix::slot_newExcel()
{
    qDebug() << "slot_newExcel!";
    Process proc;
    QString error_message;
    std::vector<char> result_err;
    std::vector<char> result_out;
    int process_exit_code = 0;
    QString cmd = QString("\"%1\" ").arg(adbCommand());
    cmd +="shell tinymix";
    qDebug() <<cmd;
    int rows = 4;
    int cols = 4;
    proc.start(cmd, false);
    process_exit_code = proc.wait();
    result_err = proc.errbytes;
    result_out = proc.outbytes;
    error_message = proc.errstring();
    //qDebug()<<result_out;
    char *data4 = &result_out[0];
    cJSON* root = cJSON_Parse(data4);
    cJSON* item;
    QString text;
    item=cJSON_GetObjectItem(root,"controls");
    rows=cJSON_GetObjectItem(root,"Number of controls")->valueint;
    ui->tableWidget->clear();
    ui->tableWidget->setRowCount(rows);
    ui->tableWidget->setColumnCount(cols);
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
            qDebug() << "text:" <<text<<"j="<<j;
            QTableWidgetItem *item = new QTableWidgetItem(text);
            item->setTextAlignment(Qt::AlignCenter);                //设置居中对齐
            ui->tableWidget->setItem(i, j, item);
        }
    }
    ui->statusBar->showMessage(tr("tinymix已导入!"), 3000);
}

void ReadTinymix::on_comboBox_activated(const QString &arg1)
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

void ReadTinymix::on_tableWidget_cellChanged(int row, int column)
{
//    qDebug() << "cellChanged:" << row << column;
    QPoint point(row + 1, column + 1);
    cellList.append(point);
//    qDebug() << "point:" << point << point.x() << point.y() << "cellList:" << cellList;
}


QString ReadTinymix::adbCommand() const
{
    qDebug()<<"adb";
    return "/work/version/platform-tools/adb";
}

const char * ReadTinymix::qstringtochar(QString *qs)
{
     std::string str = qs->toStdString();
     return str.c_str();
}

CJSON_PUBLIC(cJSON *)  ReadTinymix::readcontrol(const cJSON * const object, int num)
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
void  ReadTinymix::check_item_type(const cJSON * const object)
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

void  ReadTinymix::value_to_string(const cJSON * const object,QString *qs)
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

void ReadTinymix::on_act_about_triggered()
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
