#include "AboutDialog.h"
#include "ui_AboutDialog.h"
#include "common/misc.h"

#include <QPainter>

#include "version.h"

AboutDialog::AboutDialog(QWidget *parent) :
	QDialog(parent),
	ui(new Ui::AboutDialog)
{
	ui->setupUi(this);
	Qt::WindowFlags flags = windowFlags();
	flags &= ~Qt::WindowContextHelpButtonHint;
	setWindowFlags(flags);

	misc::setFixedSize(this);

	QString copyright_holder = "S.Fuchita";
	QString twitter_account = "soramimi_jp";

	pixmap.load(":/image/about.png");

	setWindowTitle(tr("About %1").arg(qApp->applicationName()));

	setStyleSheet("QLabel { color: black; }");

	ui->label_title->setText(appVersion());
	ui->label_copyright->setText(QString("Copyright (C) %1 %2").arg(copyright_year).arg(copyright_holder));
	ui->label_twitter->setText(twitter_account.isEmpty() ? QString() : QString("(@%1)").arg(twitter_account));
	QString t = QString("Qt %1").arg(qVersion());
#if defined(_MSC_VER)
	t += QString(", msvc=%1").arg(_MSC_VER);
#elif defined(__clang__)
	t += QString(", clang=%1.%2").arg(__clang_major__).arg(__clang_minor__);
#elif defined(__GNUC__)
	t += QString(", gcc=%1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#endif
	ui->label_qt->setText(t);
}

AboutDialog::~AboutDialog()
{
	delete ui;
}

void AboutDialog::mouseReleaseEvent(QMouseEvent *)
{
	accept();
}

void AboutDialog::paintEvent(QPaintEvent *)
{
	QPainter pr(this);
	int w = width();
	int h = height();
	pr.drawPixmap(0, 0, w, h, pixmap);
}

QString AboutDialog::appVersion()
{
	return QString("%1, v%2 (%3)").arg(qApp->applicationName()).arg(product_version).arg(source_revision);
}
