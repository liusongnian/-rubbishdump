/********************************************************************************
** Form generated from reading UI file 'eventLog.ui'
**
** Created by: Qt User Interface Compiler version 5.11.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_EVENTLOG_H
#define UI_EVENTLOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QPlainTextEdit>

QT_BEGIN_NAMESPACE

class Ui_EventLog
{
public:
    QGridLayout *gridLayout;
    QPlainTextEdit *plainTextEdit;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *EventLog)
    {
        if (EventLog->objectName().isEmpty())
            EventLog->setObjectName(QStringLiteral("EventLog"));
        EventLog->resize(500, 300);
        gridLayout = new QGridLayout(EventLog);
        gridLayout->setObjectName(QStringLiteral("gridLayout"));
        plainTextEdit = new QPlainTextEdit(EventLog);
        plainTextEdit->setObjectName(QStringLiteral("plainTextEdit"));
        plainTextEdit->setUndoRedoEnabled(false);
        plainTextEdit->setReadOnly(true);

        gridLayout->addWidget(plainTextEdit, 0, 0, 1, 1);

        buttonBox = new QDialogButtonBox(EventLog);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Close);

        gridLayout->addWidget(buttonBox, 1, 0, 1, 1);


        retranslateUi(EventLog);
        QObject::connect(buttonBox, SIGNAL(accepted()), EventLog, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), EventLog, SLOT(reject()));

        QMetaObject::connectSlotsByName(EventLog);
    } // setupUi

    void retranslateUi(QDialog *EventLog)
    {
        EventLog->setWindowTitle(QApplication::translate("EventLog", "Event Log", nullptr));
    } // retranslateUi

};

namespace Ui {
    class EventLog: public Ui_EventLog {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_EVENTLOG_H
