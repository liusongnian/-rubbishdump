#ifndef LOGTABLE2WIDGET_H
#define LOGTABLE2WIDGET_H

#include <QTableWidget>

class RepositoryWrapper2Frame;
class LogTable2WidgetDelegate;

/**
 * @brief コミットログテーブルウィジェット
 */
class LogTable2Widget : public QTableWidget {
	Q_OBJECT
	friend class LogTable2WidgetDelegate;
private:
	struct Private;
	Private *m;
    RepositoryWrapper2Frame *frame();
public:
	explicit LogTable2Widget(QWidget *parent = nullptr);
	~LogTable2Widget() override;
    void bind(RepositoryWrapper2Frame *frame);
protected:
	void paintEvent(QPaintEvent *) override;
	void resizeEvent(QResizeEvent *e) override;
protected slots:
	void verticalScrollbarValueChanged(int value) override;
};

#endif // LOGTABLE2WIDGET_H
