#ifndef CREATEREPOSITORYDIALOG_H
#define CREATEREPOSITORYDIALOG_H

#include <QDialog>

namespace Ui {
class CreateRepositoryDialog;
}

class MainWindow;

class CreateRepositoryDialog : public QDialog {
	Q_OBJECT
private:
	QString already_exists_;
public:
	explicit CreateRepositoryDialog(MainWindow *parent, QString const &dir = QString());
	~CreateRepositoryDialog() override;

	QString path() const;
	QString name() const;
	QString remoteName() const;
	QString remoteURL() const;
	QString overridedSshKey();
private slots:
	void on_groupBox_remote_toggled(bool arg1);
	void on_lineEdit_name_textChanged(QString const &arg1);
	void on_lineEdit_path_textChanged(QString const &arg1);
	void on_pushButton_browse_path_clicked();
	void on_pushButton_test_repo_clicked();
private:
	Ui::CreateRepositoryDialog *ui;
	void validate(bool change_name);
	MainWindow *mainwindow();

public slots:
	void accept() override;
};

#endif // CREATEREPOSITORYDIALOG_H
