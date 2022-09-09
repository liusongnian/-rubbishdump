#ifndef FILEDIFF2WIDGET_H
#define FILEDIFF2WIDGET_H

#include "FileDiffSliderWidget.h"
#include "FileView2Widget.h"
#include "Git.h"
#include "MainWindow.h"
#include "SubmoduleMainWindow.h"
#include "texteditor/AbstractCharacterBasedApplication.h"
#include <QDialog>
#include <memory>

namespace Ui {
class FileDiff2Widget;
}

enum class ViewType1 {
	None,
	Left,
	Right
};

using TextDiffLine = Document::Line;
using TextDiffLineList = QList<Document::Line>;

struct ObjectContent1 {
	QString id;
	QString path;
	QByteArray bytes;
	TextDiffLineList lines;
};
using ObjectContentPtr1 = std::shared_ptr<ObjectContent1>;

class QTableWidgetItem;

class FileDiff2Widget : public QWidget {
	Q_OBJECT
    friend class BigDiff2Window;
public:
	struct DiffData {
        ObjectContentPtr1 left;
        ObjectContentPtr1 right;
		std::vector<std::string> original_lines;
		DiffData()
		{
			clear();
		}
		void clear()
		{
            left = std::make_shared<ObjectContent1>();
            right = std::make_shared<ObjectContent1>();
			original_lines.clear();
		}
	};

//	struct DrawData {
//		int v_scroll_pos = 0;
//		int h_scroll_pos = 0;
//		int char_width = 0;
//		int line_height = 0;
//		QColor bgcolor_text;
//		QColor bgcolor_add;
//		QColor bgcolor_del;
//		QColor bgcolor_add_dark;
//		QColor bgcolor_del_dark;
//		QColor bgcolor_gray;
//		QWidget *forcus = nullptr;
//		DrawData()
//		{
//			bgcolor_text = QColor(255, 255, 255);
//			bgcolor_gray = QColor(224, 224, 224);
//			bgcolor_add = QColor(192, 240, 192);
//			bgcolor_del = QColor(255, 224, 224);
//			bgcolor_add_dark = QColor(64, 192, 64);
//			bgcolor_del_dark = QColor(240, 64, 64);
//		}
//	};

	enum ViewStyle {
		None,
		SingleFile,
		LeftOnly,
		RightOnly,
		SideBySideText,
		SideBySideImage,
	};

private:
    Ui::FileDiff2Widget *ui;

	struct Private;
	Private *m;

	struct InitParam_ {
		ViewStyle view_style = ViewStyle::None;
		QByteArray bytes_a;
		QByteArray bytes_b;
		Git::Diff diff;
		bool uncommited = false;
		QString workingdir;
	};

	ViewStyle viewstyle() const;

	GitPtr git();
	Git::Object cat_file(GitPtr g, QString const &id);

	int totalTextLines() const;

	void resetScrollBarValue();
	void updateSliderCursor();

	int fileviewHeight() const;

	void setDiffText(const Git::Diff &diff, TextDiffLineList const &left, TextDiffLineList const &right);


	void setLeftOnly(QByteArray const &ba, const Git::Diff &diff);
	void setRightOnly(QByteArray const &ba, const Git::Diff &diff);
	void setSideBySide(QByteArray const &ba, const Git::Diff &diff, bool uncommited, QString const &workingdir);
	void setSideBySide_(QByteArray const &ba_a, QByteArray const &ba_b, QString const &workingdir);

	bool isValidID_(QString const &id);

    FileViewType1 setupPreviewWidget();

	void makeSideBySideDiffData(const Git::Diff &diff, const std::vector<std::string> &original_lines, TextDiffLineList *left_lines, TextDiffLineList *right_lines);
	void onUpdateSliderBar();
	void refrectScrollBar();
	void setOriginalLines_(QByteArray const &ba, const Git::SubmoduleItem *submodule, const Git::CommitItem *submodule_commit);
	QString diffObjects(GitPtr g, QString const &a_id, QString const &b_id);
    SubmoduleMainWindow *mainwindow();
	bool setSubmodule(const Git::Diff &diff);
protected:
	void resizeEvent(QResizeEvent *) override;
	void keyPressEvent(QKeyEvent *event) override;
public:
	explicit FileDiff2Widget(QWidget *parent = nullptr);
	~FileDiff2Widget() override;

    void bind(SubmoduleMainWindow *mw);

	void clearDiffView();

	void setSingleFile(QByteArray const &ba, QString const &id, QString const &path);

	void updateControls();
	void scrollToBottom();

	void updateDiffView(const Git::Diff &info, bool uncommited);
	void updateDiffView(const QString &id_left, const QString &id_right, QString const &path = QString());

	void setMaximizeButtonEnabled(bool f);
	void setFocusAcceptable(Qt::FocusPolicy focuspolicy);
	QPixmap makeDiffPixmap(DiffPane pane, int width, int height);
    void setViewType(FileViewType1 type);
	void setTextCodec(QTextCodec *codec);
	void setTextCodec(char const *name);
private slots:
	void onVerticalScrollValueChanged(int);
	void onHorizontalScrollValueChanged(int);
	void onDiffWidgetWheelScroll(int lines);
	void onScrollValueChanged2(int value);
	void onDiffWidgetResized();
	void on_toolButton_fullscreen_clicked();

	void scrollTo(int value);
	void onMoved(int cur_row, int cur_col, int scr_row, int scr_col);
	void on_toolButton_menu_clicked();

signals:
//	void moveNextItem();
//	void movePreviousItem();
	void textcodecChanged();
};

#endif // FILEDIFF2WIDGET_H