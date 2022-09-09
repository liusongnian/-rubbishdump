#ifndef FILEPROPERTYDIALOG_H
#define FILEPROPERTYDIALOG_H

#include <QDialog>

namespace Ui {
class FilePropertyDialog;
}

class MainWindow;
class SubmoduleMainWindow;

class FilePropertyDialog : public QDialog {
	Q_OBJECT
private:
	MainWindow *mainwindow;
	SubmoduleMainWindow *submodulemainwindow;
public:
	explicit FilePropertyDialog(QWidget *parent = nullptr);
	~FilePropertyDialog() override;

	void exec(MainWindow *mw, QString const &path, QString const &id);
	void exec(SubmoduleMainWindow *mw, QString const &path, QString const &id);
private:
	Ui::FilePropertyDialog *ui;
};

#endif // FILEPROPERTYDIALOG_H
