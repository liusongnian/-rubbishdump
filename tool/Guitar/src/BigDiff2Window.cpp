#include <memory>

#include "BigDiff2Window.h"
#include "ui_BigDiff2Window.h"

struct BigDiff2Window::Private {
	TextEditorEnginePtr text_editor_engine;
	FileDiff2Widget::InitParam_ param;
};

BigDiff2Window::BigDiff2Window(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::BigDiff2Window)
	, m(new Private)
{
	ui->setupUi(this);
	Qt::WindowFlags flags = windowFlags();
	flags &= ~Qt::WindowContextHelpButtonHint;
	flags |= Qt::WindowMaximizeButtonHint;
	setWindowFlags(flags);

	ui->widget_diff->setMaximizeButtonEnabled(false);

	connect(ui->widget_diff, &FileDiff2Widget::textcodecChanged, [&](){ updateDiffView(); });
}

BigDiff2Window::~BigDiff2Window()
{
	delete m;
	delete ui;
}

void BigDiff2Window::setTextCodec(QTextCodec *codec)
{
	m->text_editor_engine = std::make_shared<TextEditorEngine>();
	ui->widget_diff->setTextCodec(codec);
}

void BigDiff2Window::updateDiffView()
{
	ui->widget_diff->updateDiffView(m->param.diff, m->param.uncommited);
}

void BigDiff2Window::init(SubmoduleMainWindow *mw, FileDiff2Widget::InitParam_ const &param)
{
	ui->widget_diff->bind(mw);
	m->param = param;

	{
		QString name = m->param.diff.path;
		int i = name.lastIndexOf('/');
		if (i >= 0) {
			name = name.mid(i + 1);
		}
		ui->lineEdit_center->setText(name);
	}
	auto Text = [](QString id){
		if (id.startsWith(PATH_PREFIX)) {
			id = id.mid(1);
		}
		return id;
	};
	ui->lineEdit_left->setText(Text(m->param.diff.blob.a_id));
	ui->lineEdit_right->setText(Text(m->param.diff.blob.b_id));

	switch (m->param.view_style) {
	case FileDiff2Widget::ViewStyle::LeftOnly:
		ui->widget_diff->setLeftOnly(m->param.bytes_a, m->param.diff);
		break;
	case FileDiff2Widget::ViewStyle::RightOnly:
		ui->widget_diff->setRightOnly(m->param.bytes_b, m->param.diff);
		break;
	case FileDiff2Widget::ViewStyle::SideBySideText:
		ui->widget_diff->setSideBySide(m->param.bytes_a, m->param.diff, m->param.uncommited, m->param.workingdir);
		break;
	case FileDiff2Widget::ViewStyle::SideBySideImage:
		ui->widget_diff->setSideBySide_(m->param.bytes_a, m->param.bytes_b, m->param.workingdir);
		break;
	}
}




