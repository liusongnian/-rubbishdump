/********************************************************************************
** Form generated from reading UI file 'passphrase.ui'
**
** Created by: Qt User Interface Compiler version 5.11.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PASSPHRASE_H
#define UI_PASSPHRASE_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_Passphrase
{
public:
    QVBoxLayout *verticalLayout;
    QLabel *headline;
    QHBoxLayout *horizontalLayout;
    QLabel *label;
    QLineEdit *lineEdit;
    QSpacerItem *verticalSpacer;
    QDialogButtonBox *buttonBox;

    void setupUi(QDialog *Passphrase)
    {
        if (Passphrase->objectName().isEmpty())
            Passphrase->setObjectName(QStringLiteral("Passphrase"));
        Passphrase->resize(356, 95);
        verticalLayout = new QVBoxLayout(Passphrase);
        verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
        headline = new QLabel(Passphrase);
        headline->setObjectName(QStringLiteral("headline"));
        QSizePolicy sizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(headline->sizePolicy().hasHeightForWidth());
        headline->setSizePolicy(sizePolicy);

        verticalLayout->addWidget(headline);

        horizontalLayout = new QHBoxLayout();
        horizontalLayout->setObjectName(QStringLiteral("horizontalLayout"));
        label = new QLabel(Passphrase);
        label->setObjectName(QStringLiteral("label"));
        QSizePolicy sizePolicy1(QSizePolicy::Fixed, QSizePolicy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy1);

        horizontalLayout->addWidget(label);

        lineEdit = new QLineEdit(Passphrase);
        lineEdit->setObjectName(QStringLiteral("lineEdit"));
        lineEdit->setFrame(true);
        lineEdit->setEchoMode(QLineEdit::Password);

        horizontalLayout->addWidget(lineEdit);


        verticalLayout->addLayout(horizontalLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Minimum, QSizePolicy::Expanding);

        verticalLayout->addItem(verticalSpacer);

        buttonBox = new QDialogButtonBox(Passphrase);
        buttonBox->setObjectName(QStringLiteral("buttonBox"));
        buttonBox->setOrientation(Qt::Horizontal);
        buttonBox->setStandardButtons(QDialogButtonBox::Cancel|QDialogButtonBox::Ok);

        verticalLayout->addWidget(buttonBox);

#ifndef QT_NO_SHORTCUT
        label->setBuddy(lineEdit);
#endif // QT_NO_SHORTCUT

        retranslateUi(Passphrase);
        QObject::connect(buttonBox, SIGNAL(accepted()), Passphrase, SLOT(accept()));
        QObject::connect(buttonBox, SIGNAL(rejected()), Passphrase, SLOT(reject()));

        QMetaObject::connectSlotsByName(Passphrase);
    } // setupUi

    void retranslateUi(QDialog *Passphrase)
    {
        Passphrase->setWindowTitle(QApplication::translate("Passphrase", "Passphrase", nullptr));
        headline->setText(QApplication::translate("Passphrase", "Please enter Passphrase for key", nullptr));
        label->setText(QApplication::translate("Passphrase", "Passphrase", nullptr));
        lineEdit->setInputMask(QString());
        lineEdit->setText(QString());
    } // retranslateUi

};

namespace Ui {
    class Passphrase: public Ui_Passphrase {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PASSPHRASE_H
