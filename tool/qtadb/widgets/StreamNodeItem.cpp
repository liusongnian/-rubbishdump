#include "StreamNodeItem.h"
#include "ui_StreamNodeItem.h"

#include <QDebug>
#include <QPainter>

StreamNodeItem::StreamNodeItem(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::StreamNodeItem),
    m_headLabelWidth(0),
    m_level(0),
    m_indentation(0),
    m_fullName("")
{
    ui->setupUi(this);
    initControl();
}

StreamNodeItem::~StreamNodeItem()
{
    delete ui;
}

void StreamNodeItem::setFullName(const QString &fullName)
{
    m_fullName = fullName;
    ui->lbFullName->setText(fullName);
}

void StreamNodeItem::setHeadPixmap(const QPixmap &headPath)
{
    ui->lbHeadPic->setPixmap(headPath);
}

void StreamNodeItem::setHeadPath(const QString &headPath)
{
    /*
    ui->lbHeadPic->setScaledContents(true);
    QString style = ui->lbHeadPic->styleSheet();
    style.append("image:url(").append(headPath).append(");");
    qDebug() << style;
    ui->lbHeadPic->setStyleSheet(style);
    */
    // 方式3.加载QPixmap
    QPixmap pixmap1;
    pixmap1.load(headPath);
    QPixmap pixmap2;
    pixmap2.load("/work2/qtadb-master/images/head_mask.png");

    qDebug()<<__LINE__<<__FUNCTION__<<  pixmap1.size()<<  pixmap2.size();
    qDebug() << "m_level:" << m_level << "  m_indentation:" << m_indentation << " m_headLabelWidth:" << m_headLabelWidth << "  " << HEAD_LABEL_WIDTH;
    QPixmap roundPic = this->getRoundImage(pixmap1, pixmap2, QSize(m_headLabelWidth,HEAD_LABEL_WIDTH));
                        qDebug()<<__LINE__<<__FUNCTION__;
    this->setHeadPixmap(roundPic);
                                            qDebug()<<__LINE__<<__FUNCTION__;
}

QSize StreamNodeItem::getHeadLabelSize() const
{
    return ui->lbHeadPic->size();
}

int StreamNodeItem::getIndentation()
{
    return this->m_indentation;

}

int StreamNodeItem::getLevel()
{
    return this->m_level;
}

void StreamNodeItem::setLevel(int level)
{
    this->m_level = level;
    this->m_indentation = this->m_level * INDENTATION;
    this->m_headLabelWidth = this->m_indentation + HEAD_LABEL_WIDTH;
    ui->lbHeadPic->setMinimumWidth(m_indentation);
}

QString StreamNodeItem::getFullName()
{
    return m_fullName;
}

void StreamNodeItem::initControl()
{

}

QPixmap StreamNodeItem::getRoundImage(const QPixmap &src, QPixmap &mask, QSize masksize)
{
    if (masksize == QSize(0, 0))
    {
        masksize = mask.size();
    }
    else
    {
        mask = mask.scaled(masksize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    qDebug()<<__LINE__<<__FUNCTION__;
    QImage resultImage(masksize, QImage::Format_ARGB32_Premultiplied);
    qDebug()<<__LINE__<<__FUNCTION__;
    QPainter painter(&resultImage);
    qDebug()<<__LINE__<<__FUNCTION__;
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(resultImage.rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.drawPixmap(m_indentation, 0, mask);
    qDebug()<<__LINE__<<__FUNCTION__;
    painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    qDebug()<<__LINE__<<__FUNCTION__;
    painter.drawPixmap(m_indentation, 0, src.scaled(masksize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    qDebug()<<__LINE__<<__FUNCTION__;
    painter.end();
    return QPixmap::fromImage(resultImage);
}

void StreamNodeItem::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
}
