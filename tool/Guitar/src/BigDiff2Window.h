#ifndef BIGDIFF2WINDOW_H
#define BIGDIFF2WINDOW_H

#include <QDialog>
#include "Git.h"
#include "FileDiff2Widget.h"

namespace Ui {
class BigDiff2Window;
}

class BigDiff2Window : public QDialog {
	Q_OBJECT
private:
	struct Private;
	Private *m;
public:
	explicit BigDiff2Window(QWidget *parent = nullptr);
	~BigDiff2Window() override;

    void init(SubmoduleMainWindow *mw, const FileDiff2Widget::InitParam_ &param);
	void setTextCodec(QTextCodec *codec);
private:
	Ui::BigDiff2Window *ui;
	void updateDiffView();
	QString fileName() const;
};

#endif // BIGDIFF2WINDOW_H
