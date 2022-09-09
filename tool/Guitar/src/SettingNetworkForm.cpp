#include "SettingNetworkForm.h"
#include "ui_SettingNetworkForm.h"
#include "SettingsDialog.h"

SettingNetworkForm::SettingNetworkForm(QWidget *parent) :
	AbstractSettingForm(parent),
	ui(new Ui::SettingNetworkForm)
{
	ui->setupUi(this);
}

SettingNetworkForm::~SettingNetworkForm()
{
	delete ui;
}

void SettingNetworkForm::exchange(bool save)
{
	if (save) {
		if (ui->radioButton_auto_detect->isChecked()) {
			settings()->proxy_type = "auto";
		} else if (ui->radioButton_manual->isChecked()) {
			settings()->proxy_type = "manual";
		} else {
			settings()->proxy_type = "none";
		}
		settings()->proxy_server = ui->lineEdit_proxy_server->text();
	} else {
		if (settings()->proxy_type == "auto") {
			ui->radioButton_auto_detect->click();
		} else if (settings()->proxy_type == "manual") {
			ui->radioButton_manual->click();
		} else {
			ui->radioButton_no_proxy->click();
		}
		ui->lineEdit_proxy_server->setText(settings()->proxy_server);
	}
}

void SettingNetworkForm::updateProxyServerLineEdit()
{
	bool f = ui->radioButton_manual->isChecked();
	ui->lineEdit_proxy_server->setEnabled(f);
}

void SettingNetworkForm::on_radioButton_no_proxy_clicked()
{
	updateProxyServerLineEdit();
}

void SettingNetworkForm::on_radioButton_auto_detect_clicked()
{
	updateProxyServerLineEdit();
}

void SettingNetworkForm::on_radioButton_manual_clicked()
{
	updateProxyServerLineEdit();
}
