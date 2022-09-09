#ifndef IMAGEVIEW2WIDGET_H
#define IMAGEVIEW2WIDGET_H

#include <QScrollBar>
#include <QWidget>
#include "Git.h"
#include "MainWindow.h"
#include "AbstractCharacterBasedApplication.h"

class FileDiff2Widget;

class FileDiffSliderWidget;

class ImageView2Widget : public QWidget {
	Q_OBJECT
private:
	struct Private;
	Private *m;

	bool isValidImage() const;
	QSize imageSize() const;

	QSizeF imageScrollRange() const;
	void internalScrollImage(double x, double y);
	void scrollImage(double x, double y);
	void setImageScale(double scale);
	QBrush getTransparentBackgroundBrush();
	bool hasFocus() const;
	void setScrollBarRange(QScrollBar *h, QScrollBar *v);
	void updateScrollBarRange();
protected:
	QMainWindow *mainwindow();
	void resizeEvent(QResizeEvent *) override;
	void paintEvent(QPaintEvent *) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void wheelEvent(QWheelEvent *) override;
public:
	explicit ImageView2Widget(QWidget *parent = nullptr);
	~ImageView2Widget() override;

	void bind(QMainWindow *m, FileDiff2Widget *filediffwidget, QScrollBar *vsb, QScrollBar *hsb);

	void clear();

	void setImage(QString mimetype, QByteArray const &ba);

	void setLeftBorderVisible(bool f);

	void refrectScrollBar();

	static QString formatText(const Document::Line &line2);
signals:
	void scrollByWheel(int lines);
};

#endif // IMAGEVIEW2WIDGET_H
