#ifndef READTINYMIX_H
#define READTINYMIX_H

#include <QMainWindow>

extern "C" {
#include <string.h>
#include <stdio.h>
#include "../json/cJSON.h"
}

namespace Ui {
class ReadTinymix;
}

class ReadTinymix : public QMainWindow
{
    Q_OBJECT

public:
    explicit ReadTinymix(QWidget *parent = 0);
    ~ReadTinymix();

    void init();                                //初始化
    void readToTable(const QString sheet);      //读Excel到tableWidget
    void tinymixToTable(const QString sheet);
    void writeToExcel(const QString sheet);     //保存修改内容到Excel
    QString adbCommand() const;
    CJSON_PUBLIC(cJSON *)  readcontrol(const cJSON * const object, int num);
    void  check_item_type(const cJSON * const object);
    const char *  qstringtochar(QString *qs);
    void  value_to_string(const cJSON * const object,QString *qs);
private slots:
    void slot_openExcel();

    void slot_saveExcel();

    void on_comboBox_activated(const QString &arg1);

    void on_tableWidget_cellChanged(int row, int column);

    void on_act_about_triggered();

    void slot_newExcel();

private:
    Ui::ReadTinymix *ui;

    QString m_fileName;       //文件名
    QString cBox_text;
    QList<QPoint> cellList; //记录修改内容的位置
};

#endif // READTINYMIX_H
