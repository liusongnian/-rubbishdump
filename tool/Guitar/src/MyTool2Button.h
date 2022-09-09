#ifndef MYTOOL2BUTTON_H
#define MYTOOL2BUTTON_H

#include <QToolButton>

class MyTool2Button : public QToolButton {
	Q_OBJECT
public:
	enum Indicator {
		None,
		Dot,
		Number,
	};
private:
	Indicator indicator = None;
	int number = -1;
	void setIndicatorMode(Indicator i);
public:
	explicit MyTool2Button(QWidget *parent = nullptr);
	void setNumber(int n);
	void setDot(bool f);
protected:
    void paintEvent(QPaintEvent *event) override;
};

#endif // MYTOOL2BUTTON_H
