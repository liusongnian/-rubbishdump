#include "FileView2Widget.h"
#include "common/misc.h"
#include <QMenu>
#include <QPainter>
#include <QStackedWidget>
#include <QVBoxLayout>

FileView2Widget::FileView2Widget(QWidget *parent)
	: QWidget(parent)
{
	setObjectName(QStringLiteral("FileView2Widget"));
	ui_verticalLayout = new QVBoxLayout(this);
	ui_verticalLayout->setSpacing(0);
	ui_verticalLayout->setObjectName(QStringLiteral("verticalLayout"));
	ui_verticalLayout->setContentsMargins(0, 0, 0, 0);
	ui_stackedWidget = new QStackedWidget(this);
	ui_stackedWidget->setObjectName(QStringLiteral("stackedWidget"));
	ui_page_none = new QWidget();
	ui_page_none->setObjectName(QStringLiteral("page_none"));
	ui_stackedWidget->addWidget(ui_page_none);
	ui_page_text = new X_TextEditorWidget();
	ui_page_text->setObjectName(QStringLiteral("page_text"));
	ui_page_text->setFocusPolicy(Qt::ClickFocus);
	ui_stackedWidget->addWidget(ui_page_text);
	ui_page_image = new X_ImageViewWidget();
	ui_page_image->setObjectName(QStringLiteral("page_image"));
	ui_page_image->setFocusPolicy(Qt::ClickFocus);
	ui_stackedWidget->addWidget(ui_page_image);
	ui_verticalLayout->addWidget(ui_stackedWidget);
	setWindowTitle(QApplication::translate("FileView2Widget", "Form", Q_NULLPTR));
	ui_stackedWidget->setCurrentIndex(1);
	QMetaObject::connectSlotsByName(this);

	ui_page_text->setRenderingMode(TextEditorWidget::DecoratedMode);
	ui_page_text->setTheme(TextEditorTheme::Light());
	ui_page_text->showHeader(false);
	ui_page_text->showFooter(false);
	ui_page_text->setAutoLayout(true);
	ui_page_text->setReadOnly(true);
	ui_page_text->setToggleSelectionAnchorEnabled(false);
	ui_page_text->setFocusFrameVisible(true);

	ui_stackedWidget->setCurrentWidget(ui_page_none);
}



void FileView2Widget::setTextCodec(QTextCodec *codec)
{
	ui_page_text->setTextCodec(codec);
}

void FileView2Widget::bind(QMainWindow *mw, FileDiff2Widget *fdw, QScrollBar *vsb, QScrollBar *hsb, TextEditorThemePtr const &theme)
{
	ui_page_text->bindScrollBar(vsb, hsb);
    ui_page_image->bind(mw, (FileDiffWidget *)fdw, vsb, hsb);  //tmp bypass
	ui_page_text->setTheme(theme);
}

void FileView2Widget::setViewType(FileViewType1 type)
{
	view_type = type;
	switch (view_type) {
    case FileViewType1::Text:
		ui_stackedWidget->setCurrentWidget(ui_page_text);
		return;
    case FileViewType1::Image:
		ui_stackedWidget->setCurrentWidget(ui_page_image);
		return;
	default:
		ui_stackedWidget->setCurrentWidget(ui_page_none);
		return;
	}
}

const TextEditorTheme *FileView2Widget::theme() const
{
	return ui_page_text->theme();
}

//int FileView2Widget::latin1Width(QString const &s) const
//{
//	return ui_page_text->latin1Width(s);
//}

int FileView2Widget::lineHeight() const
{
	return ui_page_text->lineHeight();
}

void FileView2Widget::setDiffMode(TextEditorEnginePtr const &editor_engine, QScrollBar *vsb, QScrollBar *hsb)
{
	ui_page_text->setTextEditorEngine(editor_engine);
	return ui_page_text->bindScrollBar(vsb, hsb);
}

void FileView2Widget::refrectScrollBar()
{
	switch (view_type) {
    case FileViewType1::Text:
		ui_page_text->refrectScrollBar();
		return;
    case FileViewType1::Image:
		ui_page_image->refrectScrollBar();
		return;
	}
}

void FileView2Widget::move(int cur_row, int cur_col, int scr_row, int scr_col, bool auto_scroll)
{
	return ui_page_text->move(cur_row, cur_col, scr_row, scr_col, auto_scroll);
}

void FileView2Widget::setImage(QString const &mimetype, QByteArray const &ba, QString const &object_id, QString const &path)
{
    setViewType(FileViewType1::Image);
	this->source_id = object_id;
#ifdef APP_GUITAR
	ui_page_image->setImage(mimetype, ba, object_id, path);
#else
	ui_page_image->setImage(mimetype, ba);
#endif
}

void FileView2Widget::setText(const QList<Document::Line> *source, QMainWindow *mw, QString const &object_id, QString const &object_path)
{
    setViewType(FileViewType1::Text);
	this->source_id = object_id;
#ifdef APP_GUITAR
	ui_page_text->setDocument(source, qobject_cast<MainWindow *>(mw), object_id, object_path);
	scrollToTop();
	texteditor()->moveCursorOut(); // 現在行を -1 にして、カーソルを非表示にする。
#else
	ui_page_text->setDocument(source);
	scrollToTop();
#endif
}

void FileView2Widget::setText(QByteArray const &ba, QMainWindow *mw, QString const &object_id, QString const &object_path)
{
	std::vector<std::string> lines;
	char const *begin = ba.data();
	char const *end = begin + ba.size();
	misc::splitLines(begin, end, &lines, true);
	QList<Document::Line> source;
	source.reserve(lines.size());
	int num = 0;
	for (std::string const &line : lines) {
		Document::Line t(line);
		t.line_number = ++num;
		source.push_back(t);
	}
	setText(&source, mw, object_id, object_path);
}

void FileView2Widget::scrollToTop()
{
	ui_page_text->scrollToTop();
}

void FileView2Widget::write(QKeyEvent *e)
{
	ui_page_text->write(e);
}

TextEditorWidget *FileView2Widget::texteditor()
{
	return ui_page_text;
}

